#pragma once

#include <switch.h>

#define INPUT_DEFAULT_TAP_MS 100
#define INPUT_MAX_DURATION_MS 10000

// Touch panel resolution. Coordinates are in panel space, which is exactly
// the space of the 1280x720 JPEG /screenshot returns: (0,0) is top-left.
#define INPUT_TOUCH_WIDTH  1280
#define INPUT_TOUCH_HEIGHT 720

#define INPUT_DEFAULT_TOUCH_MS 100
#define INPUT_DEFAULT_SWIPE_MS 300

// Detaches the virtual device and releases the HDLS work buffer. MUST be
// called when the console prepares for sleep (holding the work buffer
// across a sleep transition crashes the console); everything re-attaches
// lazily on the next input request.
void input_suspend(void);

// Alias of input_suspend for shutdown paths. Call before hiddbgExit.
void input_exit(void);

// Attach/detach the virtual Pro Controller.
Result input_attach(void);
Result input_detach(void);
bool   input_is_attached(void);

// Press+release: applies mask, sleeps duration_ms, releases mask.
Result input_tap(u64 mask, int duration_ms);

Result input_hold(u64 mask);
Result input_release(u64 mask);
Result input_clear(void);

// side: 0 = left stick, 1 = right stick. x/y in [-1.0, 1.0].
// If duration_ms > 0, the stick returns to neutral afterwards.
Result input_stick(int side, float x, float y, int duration_ms);

// Touch screen. Independent of the virtual controller above: these inject
// into hid's touch sampler and need neither an attach nor the work buffer.
// Both are synchronous (they drive the gesture frame by frame) and always
// hand the physical panel back before returning. Coordinates are clamped to
// the panel; durations are clamped to INPUT_MAX_DURATION_MS.
Result input_touch_tap(int x, int y, int duration_ms);
Result input_touch_swipe(int from_x, int from_y, int to_x, int to_y,
                         int duration_ms);
