// Host-side stubs for the Switch-coupled modules (input, screen, app
// metadata) so the MCP tools can be exercised end-to-end in tests.
#include <switch.h>
#include <string.h>
#include <strings.h>

#include "features/input/input.h"
#include "features/screen/screen.h"
#include "core/app.h"

u64 stub_tap_mask;
int stub_tap_duration;
int stub_tap_count;
u64 stub_hold_mask;
int stub_stick_side;
int stub_touch_x, stub_touch_y, stub_touch_duration;
int stub_swipe_from_x, stub_swipe_from_y, stub_swipe_to_x, stub_swipe_to_y;
int stub_touch_count;
float stub_stick_x, stub_stick_y;
bool stub_cleared;

void input_suspend(void) {}
Result input_attach(void) { return 0; }
Result input_detach(void) { return 0; }
bool input_is_attached(void) { return true; }

Result input_tap(u64 mask, int duration_ms) {
    stub_tap_mask = mask;
    stub_tap_duration = duration_ms;
    stub_tap_count++;
    return 0;
}

Result input_hold(u64 mask) { stub_hold_mask |= mask; return 0; }
Result input_release(u64 mask) { stub_hold_mask &= ~mask; return 0; }
Result input_clear(void) { stub_cleared = true; stub_hold_mask = 0; return 0; }

Result input_stick(int side, float x, float y, int duration_ms) {
    (void)duration_ms;
    stub_stick_side = side;
    stub_stick_x = x;
    stub_stick_y = y;
    return 0;
}

Result input_touch_tap(int x, int y, int duration_ms) {
    stub_touch_x = x;
    stub_touch_y = y;
    stub_touch_duration = duration_ms;
    stub_touch_count++;
    return 0;
}

Result input_touch_swipe(int from_x, int from_y, int to_x, int to_y,
                         int duration_ms) {
    stub_swipe_from_x = from_x;
    stub_swipe_from_y = from_y;
    stub_swipe_to_x = to_x;
    stub_swipe_to_y = to_y;
    stub_touch_duration = duration_ms;
    stub_touch_count++;
    return 0;
}

static const u8 kFakeJpeg[] = "FAKEJPEGDATA";

Result screen_capture_jpeg(ViLayerStack stack, u8 *buf, size_t buf_size, u64 *out_size) {
    (void)stack;
    if (buf_size < sizeof(kFakeJpeg) - 1)
        return 1;
    memcpy(buf, kFakeJpeg, sizeof(kFakeJpeg) - 1);
    *out_size = sizeof(kFakeJpeg) - 1;
    return 0;
}

bool screen_parse_stack(const char *name, ViLayerStack *out) {
    if (strcasecmp(name, "screenshot") == 0)     *out = ViLayerStack_Screenshot;
    else if (strcasecmp(name, "default") == 0)   *out = ViLayerStack_Default;
    else if (strcasecmp(name, "lcd") == 0)       *out = ViLayerStack_Lcd;
    else if (strcasecmp(name, "recording") == 0) *out = ViLayerStack_Recording;
    else if (strcasecmp(name, "lastframe") == 0) *out = ViLayerStack_LastFrame;
    else return false;
    return true;
}

const char *app_version(void) { return "test"; }
uint64_t app_uptime_seconds(void) { return 42; }
