#pragma once

#include "util/json.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// System-settings helpers wrapping Switch services (set:sys, lbl, audctl,
// nifm, psm). Each get/set returns true on success. The required services are
// initialized once at startup by settings_init() (called while the sm session
// is open) and torn down by settings_exit(). On host (test) builds the getters
// return deterministic placeholders and setters succeed as no-ops.

// Initializes the services used here (set:sys, lbl, audctl, psm). nifm is
// initialized elsewhere. Best-effort: a service that fails to open simply
// makes its corresponding get/set return false. Call from __appInit.
void settings_init(void);

// Tears down services opened by settings_init(). Call from __appExit.
void settings_exit(void);

// --- theme (UI color set) -----------------------------------------------------
// *out_dark / dark: true for the dark theme, false for light.
bool settings_get_theme(bool *out_dark);
bool settings_set_theme(bool dark);

// --- device nickname ----------------------------------------------------------
bool settings_get_nickname(char *out, size_t outsz);
bool settings_set_nickname(const char *name);

// System Settings caps the nickname at 32 characters; set accepts more and
// System Settings then shows a cut-off name. Shared by REST and MCP.
#define SETTINGS_NICKNAME_MAX_CHARS 32
bool settings_nickname_valid(const char *name);

// --- screen brightness --------------------------------------------------------
// Brightness is 0.0..1.0. Setting also applies it to the backlight immediately.
bool settings_get_brightness(float *out);
bool settings_set_brightness(float value);

// --- master volume ------------------------------------------------------------
// Normalized 0.0..1.0 (mapped to the system's target volume min/max range).
bool settings_get_volume(float *out);
bool settings_set_volume(float value);

// --- airplane mode (wireless) -------------------------------------------------
// One-way by design: disabling wireless cuts the server's own connectivity, so
// only disabling is exposed. Returns true if the request was issued.
bool settings_disable_wireless(void);

// --- battery (read-only) ------------------------------------------------------
// Reports charge percentage (0..100) and whether a charger is connected.
bool settings_get_battery(uint32_t *out_percent, bool *out_charging);

// --- automatic clock sync (internet time) ------------------------------------
bool settings_get_auto_time(bool *out_enabled);
bool settings_set_auto_time(bool enabled);

// --- date / time / timezone ---------------------------------------------------
// A wall-clock value in the device's timezone.
typedef struct {
    int  year;        // e.g. 2026
    int  month;       // 1..12
    int  day;         // 1..31
    int  hour;        // 0..23
    int  minute;      // 0..59
    int  second;      // 0..59
    char timezone[36]; // IANA location name, e.g. "America/New_York"
} DateTime;

// Reads the current local date/time and the device timezone.
bool settings_get_datetime(DateTime *out);

// Sets the clock from the given wall-clock fields (interpreted in the device's
// current timezone) by writing the network system clock — the path a sysmodule
// is permitted to use; the displayed time follows it. The timezone field is not
// applied (timezone writes are rejected). Returns false if the write failed.
bool settings_set_datetime(const DateTime *dt);

// True when every field is in range (year 2000-2100, day within its month).
// Shared by the REST and MCP front-ends so both reject the same input.
bool settings_datetime_valid(const DateTime *dt);

// --- request arguments ---------------------------------------------------------
// Shared by the REST and MCP front-ends, which take the same JSON shapes.

// {"theme":"light"|"dark"} -> *out_dark. False with *err set otherwise.
bool settings_theme_from_json(const JsonDoc *doc, int obj, bool *out_dark, const char **err);

// The current date/time with any of {"year","month","day","hour","minute",
// "second"} found in obj applied over it, so callers may set only some fields.
void settings_datetime_from_json(const JsonDoc *doc, int obj, DateTime *out);
