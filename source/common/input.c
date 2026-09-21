#include "input.h"
#include "buttons.h"
#include "log.h"

#include <assert.h>
#include <string.h>
#include <strings.h>

// buttons.h mirrors the libnx bit values so it stays host-testable; make
// sure they can never drift.
static_assert(BTN_A == HidNpadButton_A, "button mask drift");
static_assert(BTN_B == HidNpadButton_B, "button mask drift");
static_assert(BTN_X == HidNpadButton_X, "button mask drift");
static_assert(BTN_Y == HidNpadButton_Y, "button mask drift");
static_assert(BTN_LSTICK == HidNpadButton_StickL, "button mask drift");
static_assert(BTN_RSTICK == HidNpadButton_StickR, "button mask drift");
static_assert(BTN_L == HidNpadButton_L, "button mask drift");
static_assert(BTN_R == HidNpadButton_R, "button mask drift");
static_assert(BTN_ZL == HidNpadButton_ZL, "button mask drift");
static_assert(BTN_ZR == HidNpadButton_ZR, "button mask drift");
static_assert(BTN_PLUS == HidNpadButton_Plus, "button mask drift");
static_assert(BTN_MINUS == HidNpadButton_Minus, "button mask drift");
static_assert(BTN_LEFT == HidNpadButton_Left, "button mask drift");
static_assert(BTN_UP == HidNpadButton_Up, "button mask drift");
static_assert(BTN_RIGHT == HidNpadButton_Right, "button mask drift");
static_assert(BTN_DOWN == HidNpadButton_Down, "button mask drift");
static_assert(BTN_HOME == HiddbgNpadButton_Home, "button mask drift");
static_assert(BTN_CAPTURE == HiddbgNpadButton_Capture, "button mask drift");

static u8 g_workmem[0x1000] __attribute__((aligned(0x1000)));
static HiddbgHdlsSessionId g_session_id;
static HiddbgHdlsHandle g_handle;
static HiddbgHdlsState g_state;
static bool g_workbuf_attached;
static bool g_attached;
static bool g_touch_active;

// The HDLS work buffer is transfer memory mapped into the hid sysmodule.
// It is attached lazily (first input request) and MUST be released across
// sleep: holding it during a sleep transition breaks hid's own power
// handling and crashes the console (omm aborts with psc error 2165-1001).
static Result ensure_workbuf(void) {
    if (g_workbuf_attached)
        return 0;
    Result rc = hiddbgAttachHdlsWorkBuffer(&g_session_id, g_workmem, sizeof(g_workmem));
    if (R_FAILED(rc)) {
        LOGF("input: hiddbgAttachHdlsWorkBuffer failed rc=0x%x\n", rc);
        return rc;
    }
    g_workbuf_attached = true;
    return 0;
}

// --- touch screen ------------------------------------------------------------
//
// The touch panel does not go through HDLS: hiddbg replays the state we set
// into hid's own touch sampler, so this needs no work buffer, no virtual
// device, and it coexists with the Pro Controller above. The state is
// latched -- hid keeps reporting the last one we pushed on every sampling
// frame -- so a gesture is driven frame by frame, and every gesture MUST end
// with touch_release(), which reports "no finger" and hands the physical
// panel back to the player.

#define TOUCH_FRAME_MS 16 // ~60Hz, one hid sampling frame
#define TOUCH_DIAMETER 20 // fingertip-ish, in panel pixels

static void sleep_ms(int ms) {
    svcSleepThread((s64)ms * 1000000LL);
}

// attributes: HidTouchAttribute_Start on the first frame of a gesture,
// _End on the last, 0 while the finger moves or rests. Repeating _Start
// would look like a new finger landing on every frame.
static Result touch_push(u32 attributes, int x, int y) {
    HidTouchState st = {0};
    st.delta_time = (u64)TOUCH_FRAME_MS * 1000000ULL;
    st.attributes = attributes;
    st.finger_id = 0;
    st.x = (u32)x;
    st.y = (u32)y;
    st.diameter_x = TOUCH_DIAMETER;
    st.diameter_y = TOUCH_DIAMETER;

    Result rc = hiddbgSetTouchScreenAutoPilotState(&st, 1);
    if (R_FAILED(rc)) {
        LOGF("input: hiddbgSetTouchScreenAutoPilotState failed rc=0x%x\n", rc);
        return rc;
    }
    g_touch_active = true;
    return 0;
}

// Report an empty touch state for one frame before unsetting: applications
// latch the last state they were given, so unsetting straight from a finger
// down can leave one stuck.
static void touch_release(void) {
    if (!g_touch_active)
        return;
    // Count 0 = no fingers on the panel. The pointer still has to be valid:
    // libnx hands it to hid as a buffer descriptor either way.
    HidTouchState none = {0};
    hiddbgSetTouchScreenAutoPilotState(&none, 0);
    sleep_ms(TOUCH_FRAME_MS);
    hiddbgUnsetTouchScreenAutoPilotState();
    g_touch_active = false;
}

static int clamp_coord(int v, int max) {
    if (v < 0)
        return 0;
    if (v > max)
        return max;
    return v;
}

// A gesture costs a Start frame and an End frame, so two frames is the
// shortest thing hid can actually sample as a press.
static int clamp_touch_duration(int ms, int fallback) {
    if (ms <= 0)
        ms = fallback;
    if (ms < 2 * TOUCH_FRAME_MS)
        return 2 * TOUCH_FRAME_MS;
    if (ms > INPUT_MAX_DURATION_MS)
        return INPUT_MAX_DURATION_MS;
    return ms;
}

void input_suspend(void) {
    touch_release();
    if (g_attached) {
        hiddbgDetachHdlsVirtualDevice(g_handle);
        g_attached = false;
    }
    if (g_workbuf_attached) {
        hiddbgReleaseHdlsWorkBuffer(g_session_id);
        g_workbuf_attached = false;
    }
    memset(&g_state, 0, sizeof(g_state));
    g_state.battery_level = 4;
}

void input_exit(void) {
    input_suspend();
}

Result input_attach(void) {
    Result rc = ensure_workbuf();
    if (R_FAILED(rc))
        return rc;
    if (g_attached)
        return 0;

    HiddbgHdlsDeviceInfo device = {0};
    device.deviceType = HidDeviceType_FullKey3; // Pro Controller
    device.npadInterfaceType = HidNpadInterfaceType_Bluetooth;
    device.singleColorBody = RGBA8_MAXALPHA(60, 60, 60);
    device.singleColorButtons = RGBA8_MAXALPHA(230, 230, 230);
    device.colorLeftGrip = RGBA8_MAXALPHA(60, 60, 230);
    device.colorRightGrip = RGBA8_MAXALPHA(230, 60, 60);

    rc = hiddbgAttachHdlsVirtualDevice(&g_handle, &device);
    if (R_FAILED(rc)) {
        LOGF("input: hiddbgAttachHdlsVirtualDevice failed rc=0x%x\n", rc);
        return rc;
    }
    g_attached = true;

    // Push an initial neutral state so the controller registers.
    memset(&g_state, 0, sizeof(g_state));
    g_state.battery_level = 4;
    return hiddbgSetHdlsState(g_handle, &g_state);
}

Result input_detach(void) {
    if (!g_attached)
        return 0;
    Result rc = hiddbgDetachHdlsVirtualDevice(g_handle);
    if (R_SUCCEEDED(rc))
        g_attached = false;
    return rc;
}

bool input_is_attached(void) {
    return g_attached;
}

static Result apply_state(void) {
    return hiddbgSetHdlsState(g_handle, &g_state);
}

static int clamp_duration(int ms) {
    if (ms <= 0)
        return INPUT_DEFAULT_TAP_MS;
    if (ms > INPUT_MAX_DURATION_MS)
        return INPUT_MAX_DURATION_MS;
    return ms;
}

Result input_tap(u64 mask, int duration_ms) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;

    duration_ms = clamp_duration(duration_ms);

    g_state.buttons |= mask;
    rc = apply_state();
    if (R_FAILED(rc))
        return rc;

    svcSleepThread((s64)duration_ms * 1000000LL);

    g_state.buttons &= ~mask;
    return apply_state();
}

Result input_hold(u64 mask) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;
    g_state.buttons |= mask;
    return apply_state();
}

Result input_release(u64 mask) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;
    g_state.buttons &= ~mask;
    return apply_state();
}

Result input_clear(void) {
    touch_release(); // no-op unless a gesture died half-way through
    if (!g_attached)
        return 0;
    g_state.buttons = 0;
    memset(&g_state.analog_stick_l, 0, sizeof(g_state.analog_stick_l));
    memset(&g_state.analog_stick_r, 0, sizeof(g_state.analog_stick_r));
    return apply_state();
}

static s32 stick_value(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (s32)(v * JOYSTICK_MAX);
}

Result input_stick(int side, float x, float y, int duration_ms) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;

    HidAnalogStickState *stick = (side == 0) ? &g_state.analog_stick_l
                                             : &g_state.analog_stick_r;
    stick->x = stick_value(x);
    stick->y = stick_value(y);
    rc = apply_state();
    if (R_FAILED(rc))
        return rc;

    if (duration_ms > 0) {
        if (duration_ms > INPUT_MAX_DURATION_MS)
            duration_ms = INPUT_MAX_DURATION_MS;
        svcSleepThread((s64)duration_ms * 1000000LL);
        stick->x = 0;
        stick->y = 0;
        rc = apply_state();
    }
    return rc;
}

// Press the panel at (x,y) for duration_ms, then lift. The Start frame and
// the End frame each cost one sampling frame, so anything shorter than two
// frames would never be sampled as a press at all.
Result input_touch_tap(int x, int y, int duration_ms) {
    x = clamp_coord(x, INPUT_TOUCH_WIDTH - 1);
    y = clamp_coord(y, INPUT_TOUCH_HEIGHT - 1);
    duration_ms = clamp_touch_duration(duration_ms, INPUT_DEFAULT_TOUCH_MS);

    Result rc = touch_push(HidTouchAttribute_Start, x, y);
    if (R_SUCCEEDED(rc)) {
        sleep_ms(TOUCH_FRAME_MS);
        rc = touch_push(0, x, y);
    }
    if (R_SUCCEEDED(rc)) {
        sleep_ms(duration_ms - 2 * TOUCH_FRAME_MS);
        rc = touch_push(HidTouchAttribute_End, x, y);
        sleep_ms(TOUCH_FRAME_MS);
    }
    touch_release();
    return rc;
}

// Drag from one point to another over duration_ms, one interpolated point
// per sampling frame, so applications see a continuous gesture rather than a
// teleport (which most flick/scroll handlers ignore).
Result input_touch_swipe(int from_x, int from_y, int to_x, int to_y,
                         int duration_ms) {
    from_x = clamp_coord(from_x, INPUT_TOUCH_WIDTH - 1);
    from_y = clamp_coord(from_y, INPUT_TOUCH_HEIGHT - 1);
    to_x = clamp_coord(to_x, INPUT_TOUCH_WIDTH - 1);
    to_y = clamp_coord(to_y, INPUT_TOUCH_HEIGHT - 1);
    duration_ms = clamp_touch_duration(duration_ms, INPUT_DEFAULT_SWIPE_MS);

    int steps = duration_ms / TOUCH_FRAME_MS; // >= 2, see clamp_touch_duration

    Result rc = touch_push(HidTouchAttribute_Start, from_x, from_y);
    for (int i = 1; R_SUCCEEDED(rc) && i < steps; i++) {
        sleep_ms(TOUCH_FRAME_MS);
        rc = touch_push(0, from_x + (to_x - from_x) * i / steps,
                           from_y + (to_y - from_y) * i / steps);
    }
    if (R_SUCCEEDED(rc)) {
        sleep_ms(TOUCH_FRAME_MS);
        rc = touch_push(HidTouchAttribute_End, to_x, to_y);
        sleep_ms(TOUCH_FRAME_MS);
    }
    touch_release();
    return rc;
}
