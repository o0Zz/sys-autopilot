#pragma once

#include "json.h"

#include <stdbool.h>
#include <stdint.h>

// Shared JSON argument extraction for input endpoints (REST bodies and MCP
// tool arguments use identical shapes).

// {"buttons":["A","B"], ...} -> combined mask. *err set on failure.
bool args_get_buttons(const JsonDoc *doc, int obj, uint64_t *out_mask,
                      const char **err);

// Optional {"durationMs": N}; returns fallback when absent.
int args_get_duration(const JsonDoc *doc, int obj, int fallback);

// {"side":"left|right","x":F,"y":F,"durationMs":N} -> parsed stick command.
bool args_get_stick(const JsonDoc *doc, int obj, int *out_side, float *out_x,
                    float *out_y, int *out_duration, const char **err);

// Touch coordinates are integers in 1280x720 panel space (the space of the
// screenshot the caller is looking at); out-of-range values are rejected
// rather than clamped, so a caller reading the wrong resolution hears about it.
#define ARGS_TOUCH_MAX_X 1279
#define ARGS_TOUCH_MAX_Y 719

// {"x":N,"y":N,"durationMs":N} -> tap point.
bool args_get_touch(const JsonDoc *doc, int obj, int *out_x, int *out_y,
                    int *out_duration, const char **err);

// {"fromX":N,"fromY":N,"toX":N,"toY":N,"durationMs":N} -> swipe.
bool args_get_swipe(const JsonDoc *doc, int obj, int *out_from_x, int *out_from_y,
                    int *out_to_x, int *out_to_y, int *out_duration,
                    const char **err);
