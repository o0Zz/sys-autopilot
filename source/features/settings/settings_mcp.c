#include "features/settings/settings_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/settings/settings.h"
#include "features/settings/settings_tools.h"

#include <stdio.h>
#include <strings.h>

static void tool_get_theme(McpCall *call) {
    bool dark = false;
    if (!settings_get_theme(&dark)) {
        mcp_reply_error(call, "failed to read theme");
        return;
    }
    mcp_reply_text(call, dark ? "dark" : "light");
}

static void tool_set_theme(McpCall *call) {
    char val[16] = {0};
    if (!mcp_arg_string(call, "theme", val, sizeof(val))) {
        mcp_reply_error(call, "missing 'theme' (\"light\" or \"dark\")");
        return;
    }
    bool dark;
    if (strcasecmp(val, "dark") == 0)       dark = true;
    else if (strcasecmp(val, "light") == 0) dark = false;
    else { mcp_reply_error(call, "'theme' must be \"light\" or \"dark\""); return; }

    if (!settings_set_theme(dark))
        mcp_reply_error(call, "failed to set theme");
    else
        mcp_reply_text(call,
                       dark ? "theme set to dark (applies after the HOME menu reloads — "
                              "sleep/wake or reboot)"
                            : "theme set to light (applies after the HOME menu reloads — "
                              "sleep/wake or reboot)");
}

static void tool_get_nickname(McpCall *call) {
    char name[128] = {0};
    if (!settings_get_nickname(name, sizeof(name)))
        mcp_reply_error(call, "failed to read nickname");
    else
        mcp_reply_text(call, name);
}

static void tool_set_nickname(McpCall *call) {
    char name[128] = {0};
    if (!mcp_arg_string(call, "nickname", name, sizeof(name)) || name[0] == '\0') {
        mcp_reply_error(call, "missing non-empty 'nickname'");
        return;
    }
    if (!settings_nickname_valid(name)) {
        mcp_reply_error(call, "'nickname' is longer than 32 characters");
        return;
    }
    if (!settings_set_nickname(name)) {
        mcp_reply_error(call, "failed to set nickname");
        return;
    }
    char msg[160];
    snprintf(msg, sizeof(msg), "nickname set to: %s", name);
    mcp_reply_text(call, msg);
}

// Shared get/set for the normalized 0..1 float settings (brightness, volume).
static void get_float(McpCall *call, const char *label, bool (*get)(float *)) {
    float v = 0.0f;
    if (!get(&v)) {
        mcp_reply_error(call, "failed to read setting");
        return;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "%s: %.2f", label, v);
    mcp_reply_text(call, msg);
}

static void set_float(McpCall *call, const char *key, bool (*set)(float)) {
    int t = json_obj_get(call->doc, call->args, key);
    double v;
    if (t < 0 || !json_get_double(call->doc, t, &v)) {
        char err[80];
        snprintf(err, sizeof(err), "missing numeric '%s' (0.0 - 1.0)", key);
        mcp_reply_error(call, err);
        return;
    }
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    if (!set((float)v)) {
        mcp_reply_error(call, "failed to set setting");
        return;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "%s set to %.2f", key, v);
    mcp_reply_text(call, msg);
}

static void tool_get_brightness(McpCall *call) { get_float(call, "brightness", settings_get_brightness); }
static void tool_set_brightness(McpCall *call) { set_float(call, "brightness", settings_set_brightness); }
static void tool_get_volume(McpCall *call)     { get_float(call, "volume", settings_get_volume); }
static void tool_set_volume(McpCall *call)     { set_float(call, "volume", settings_set_volume); }

static void tool_airplane_mode(McpCall *call) {
    if (!settings_disable_wireless()) {
        mcp_reply_error(call, "failed to disable wireless");
        return;
    }
    mcp_reply_text(call,
                   "wireless disabled. NOTE: the server is now unreachable until "
                   "wireless is re-enabled physically on the console.");
}

static void tool_get_auto_time(McpCall *call) {
    bool en = false;
    if (!settings_get_auto_time(&en))
        mcp_reply_error(call, "failed to read auto-time");
    else
        mcp_reply_text(call, en ? "enabled" : "disabled");
}

static void tool_set_auto_time(McpCall *call) {
    bool en;
    if (!mcp_arg_bool(call, "enabled", &en)) {
        mcp_reply_error(call, "missing boolean 'enabled'");
        return;
    }
    if (!settings_set_auto_time(en))
        mcp_reply_error(call, "failed to set auto-time");
    else
        mcp_reply_text(call, en ? "internet time sync enabled" : "internet time sync disabled");
}

static void tool_get_datetime(McpCall *call) {
    DateTime dt = {0};
    if (!settings_get_datetime(&dt)) {
        mcp_reply_error(call, "failed to read date/time");
        return;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "%04d-%02d-%02d %02d:%02d:%02d %s",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.timezone);
    mcp_reply_text(call, msg);
}

static void tool_set_datetime(McpCall *call) {
    // Start from the current value so callers may set only some fields.
    DateTime dt = {0};
    settings_get_datetime(&dt);
    dt.year   = mcp_arg_int(call, "year",   dt.year);
    dt.month  = mcp_arg_int(call, "month",  dt.month);
    dt.day    = mcp_arg_int(call, "day",    dt.day);
    dt.hour   = mcp_arg_int(call, "hour",   dt.hour);
    dt.minute = mcp_arg_int(call, "minute", dt.minute);
    dt.second = mcp_arg_int(call, "second", dt.second);

    if (!settings_datetime_valid(&dt)) {
        mcp_reply_error(call, "invalid date/time fields");
        return;
    }
    if (!settings_set_datetime(&dt)) {
        mcp_reply_error(call, "failed to set the clock");
        return;
    }
    char msg[160];
    snprintf(msg, sizeof(msg), "clock set to %04d-%02d-%02d %02d:%02d:%02d %s",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.timezone);
    mcp_reply_text(call, msg);
}

void settings_mcp_register(void) {
    mcp_server_register_tool(&kToolGetTheme,      tool_get_theme);
    mcp_server_register_tool(&kToolSetTheme,      tool_set_theme);
    mcp_server_register_tool(&kToolGetNickname,   tool_get_nickname);
    mcp_server_register_tool(&kToolSetNickname,   tool_set_nickname);
    mcp_server_register_tool(&kToolGetBrightness, tool_get_brightness);
    mcp_server_register_tool(&kToolSetBrightness, tool_set_brightness);
    mcp_server_register_tool(&kToolGetVolume,     tool_get_volume);
    mcp_server_register_tool(&kToolSetVolume,     tool_set_volume);
    mcp_server_register_tool(&kToolAirplaneMode,  tool_airplane_mode);
    mcp_server_register_tool(&kToolGetAutoTime,   tool_get_auto_time);
    mcp_server_register_tool(&kToolSetAutoTime,   tool_set_auto_time);
    mcp_server_register_tool(&kToolGetDatetime,   tool_get_datetime);
    mcp_server_register_tool(&kToolSetDatetime,   tool_set_datetime);
}
