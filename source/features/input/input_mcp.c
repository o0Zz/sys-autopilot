#include "features/input/input_mcp.h"
#include "features/input/input.h"
#include "features/input/input_args.h"
#include "features/input/input_tools.h"
#include "features/mcp/mcp_server.h"
#include "features/screen/screen_mcp.h"

#include <stdio.h>

#define TAP_SEQ_MAX 32

static int clamp_delay(int ms) {
    if (ms < 0) return 0;
    if (ms > INPUT_MAX_DURATION_MS) return INPUT_MAX_DURATION_MS;
    return ms;
}

// Input result with an optional trailing screenshot ({"screenshot":true} in
// the tool arguments), saving the agent a separate screenshot round trip.
static void reply_input(McpCall *call, Result rc, const char *ok_msg) {
    if (R_FAILED(rc)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "input failed (rc=0x%x)", rc);
        mcp_reply_error(call, msg);
        return;
    }
    bool want = false;
    mcp_arg_bool(call, "screenshot", &want);
    if (!want) {
        mcp_reply_text(call, ok_msg);
        return;
    }

    int delay = clamp_delay(mcp_arg_int(call, "screenshotDelayMs", 250));
    if (delay > 0)
        svcSleepThread((s64)delay * 1000000LL);

    u64 size = 0;
    Result cap_rc;
    const u8 *jpeg = screen_mcp_capture(call, ViLayerStack_Screenshot, &size, &cap_rc);
    if (!jpeg) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s (screenshot failed: rc=0x%x)", ok_msg, cap_rc);
        mcp_reply_text(call, msg);
        return;
    }
    mcp_reply_text_and_image(call, ok_msg, jpeg, (size_t)size);
}

static void tool_tap_buttons(McpCall *call) {
    uint64_t mask;
    const char *err = NULL;
    if (!args_get_buttons(call->doc, call->args, &mask, &err)) {
        mcp_reply_error(call, err);
        return;
    }
    int duration = args_get_duration(call->doc, call->args, INPUT_DEFAULT_TAP_MS);
    reply_input(call, input_tap(mask, duration), "ok");
}

static void tool_tap_sequence(McpCall *call) {
    const JsonDoc *doc = call->doc;
    int taps = json_obj_get(doc, call->args, "taps");
    int n = json_arr_len(doc, taps);
    if (n <= 0) {
        mcp_reply_error(call, "missing 'taps' (non-empty array)");
        return;
    }
    if (n > TAP_SEQ_MAX) {
        mcp_reply_error(call, "too many taps (max 32)");
        return;
    }
    for (int i = 0; i < n; i++) {
        int el = json_arr_get(doc, taps, i);
        uint64_t mask;
        const char *err = NULL;
        char msg[96];
        if (!args_get_buttons(doc, el, &mask, &err)) {
            snprintf(msg, sizeof(msg), "step %d: %s", i, err);
            mcp_reply_error(call, msg);
            return;
        }
        Result rc = input_tap(mask, args_get_duration(doc, el, INPUT_DEFAULT_TAP_MS));
        if (R_FAILED(rc)) {
            snprintf(msg, sizeof(msg), "step %d: input failed (rc=0x%x)", i, rc);
            mcp_reply_error(call, msg);
            return;
        }
        if (i + 1 < n) {
            long long delay = 150;
            int t = json_obj_get(doc, el, "delayAfterMs");
            if (t >= 0)
                json_get_int(doc, t, &delay);
            svcSleepThread((s64)clamp_delay((int)delay) * 1000000LL);
        }
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "performed %d taps", n);
    reply_input(call, 0, msg);
}

static void hold_or_release(McpCall *call, bool hold) {
    uint64_t mask;
    const char *err = NULL;
    if (!args_get_buttons(call->doc, call->args, &mask, &err)) {
        mcp_reply_error(call, err);
        return;
    }
    reply_input(call, hold ? input_hold(mask) : input_release(mask), "ok");
}

static void tool_hold_buttons(McpCall *call)    { hold_or_release(call, true); }
static void tool_release_buttons(McpCall *call) { hold_or_release(call, false); }

static void tool_set_stick(McpCall *call) {
    int side, duration;
    float x, y;
    const char *err = NULL;
    if (!args_get_stick(call->doc, call->args, &side, &x, &y, &duration, &err)) {
        mcp_reply_error(call, err);
        return;
    }
    reply_input(call, input_stick(side, x, y, duration), "ok");
}

static void tool_tap_screen(McpCall *call) {
    int x, y, duration;
    const char *err = NULL;
    if (!args_get_touch(call->doc, call->args, &x, &y, &duration, &err)) {
        mcp_reply_error(call, err);
        return;
    }
    reply_input(call, input_touch_tap(x, y, duration), "ok");
}

static void tool_swipe_screen(McpCall *call) {
    int x0, y0, x1, y1, duration;
    const char *err = NULL;
    if (!args_get_swipe(call->doc, call->args, &x0, &y0, &x1, &y1, &duration, &err)) {
        mcp_reply_error(call, err);
        return;
    }
    reply_input(call, input_touch_swipe(x0, y0, x1, y1, duration), "ok");
}

static void tool_clear_input(McpCall *call) {
    reply_input(call, input_clear(), "ok");
}

void input_mcp_register(void) {
    mcp_server_register_tool(&kToolTapButtons,     tool_tap_buttons);
    mcp_server_register_tool(&kToolTapSequence,    tool_tap_sequence);
    mcp_server_register_tool(&kToolHoldButtons,    tool_hold_buttons);
    mcp_server_register_tool(&kToolReleaseButtons, tool_release_buttons);
    mcp_server_register_tool(&kToolSetStick,       tool_set_stick);
    mcp_server_register_tool(&kToolTapScreen,      tool_tap_screen);
    mcp_server_register_tool(&kToolSwipeScreen,    tool_swipe_screen);
    mcp_server_register_tool(&kToolClearInput,     tool_clear_input);
}
