// Generated from settings_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolGetTheme = {
    "get_theme",
    "{\"name\":\"get_theme\",\"description\":\"Get the system UI theme. Returns \\\"light\\\" or \\\"dark\\\".\",\"inputSc"
    "hema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetTheme = {
    "set_theme",
    "{\"name\":\"set_theme\",\"description\":\"Set the system UI theme (light or dark mode). NOTE: this updates "
    "the stored setting immediately, but the HOME menu only re-reads it when it reloads - so the visible "
    "change takes effect after sleep/wake or a reboot, not instantly.\",\"inputSchema\":{\"type\":\"object\",\"pr"
    "operties\":{\"theme\":{\"type\":\"string\",\"enum\":[\"light\",\"dark\"]}},\"required\":[\"theme\"]}}"
};

static const McpToolDef kToolGetNickname = {
    "get_nickname",
    "{\"name\":\"get_nickname\",\"description\":\"Get the console's device nickname (the name shown on the netwo"
    "rk and in settings).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetNickname = {
    "set_nickname",
    "{\"name\":\"set_nickname\",\"description\":\"Set the console's device nickname.\",\"inputSchema\":{\"type\":\"obj"
    "ect\",\"properties\":{\"nickname\":{\"type\":\"string\",\"description\":\"New device nickname (max ~127 bytes).\""
    "}},\"required\":[\"nickname\"]}}"
};

static const McpToolDef kToolGetBrightness = {
    "get_brightness",
    "{\"name\":\"get_brightness\",\"description\":\"Get the current screen brightness as a value from 0.0 (dimme"
    "st) to 1.0 (brightest).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetBrightness = {
    "set_brightness",
    "{\"name\":\"set_brightness\",\"description\":\"Set the screen brightness and apply it immediately. Note: if"
    " the console has auto-brightness enabled it may adjust again on its own.\",\"inputSchema\":{\"type\":\"obj"
    "ect\",\"properties\":{\"brightness\":{\"type\":\"number\",\"description\":\"0.0 (dimmest) to 1.0 (brightest).\"}}"
    ",\"required\":[\"brightness\"]}}"
};

static const McpToolDef kToolGetVolume = {
    "get_volume",
    "{\"name\":\"get_volume\",\"description\":\"Get the current master volume as a value from 0.0 (muted) to 1.0"
    " (max).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetVolume = {
    "set_volume",
    "{\"name\":\"set_volume\",\"description\":\"Set the master volume.\",\"inputSchema\":{\"type\":\"object\",\"properti"
    "es\":{\"volume\":{\"type\":\"number\",\"description\":\"0.0 (muted) to 1.0 (max).\"}},\"required\":[\"volume\"]}}"
};

static const McpToolDef kToolAirplaneMode = {
    "airplane_mode",
    "{\"name\":\"airplane_mode\",\"description\":\"Enable airplane mode (disable all wireless). WARNING: this di"
    "sconnects the console from the network, so the server becomes unreachable immediately and CANNOT be "
    "re-enabled remotely - a human must turn wireless back on from the console. There is no remote 'disab"
    "le airplane mode' for that reason. Only use when explicitly asked to.\",\"inputSchema\":{\"type\":\"object"
    "\",\"properties\":{}}}"
};

static const McpToolDef kToolGetAutoTime = {
    "get_auto_time",
    "{\"name\":\"get_auto_time\",\"description\":\"Get whether the clock is automatically synchronized over the "
    "internet. Returns \\\"enabled\\\" or \\\"disabled\\\".\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetAutoTime = {
    "set_auto_time",
    "{\"name\":\"set_auto_time\",\"description\":\"Enable or disable automatic internet clock synchronization. D"
    "isable it before setting the clock manually with set_datetime, or the manual time is overwritten.\",\""
    "inputSchema\":{\"type\":\"object\",\"properties\":{\"enabled\":{\"type\":\"boolean\"}},\"required\":[\"enabled\"]}}"
};

static const McpToolDef kToolGetDatetime = {
    "get_datetime",
    "{\"name\":\"get_datetime\",\"description\":\"Get the console's current local date, time, and timezone. Retu"
    "rns \\\"YYYY-MM-DD HH:MM:SS <timezone>\\\".\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetDatetime = {
    "set_datetime",
    "{\"name\":\"set_datetime\",\"description\":\"Set the console's clock. Provide any subset of date/time field"
    "s; omitted ones keep their current value. The time is interpreted in the console's current timezone."
    " (The timezone itself can't be changed remotely; set the date/time only.) If internet time sync is o"
    "n, the OS may later re-sync - call set_auto_time(false) first to make a manual time stick.\",\"inputSc"
    "hema\":{\"type\":\"object\",\"properties\":{\"year\":{\"type\":\"integer\",\"description\":\"e.g. 2026\"},\"month\":{\"t"
    "ype\":\"integer\",\"description\":\"1-12\"},\"day\":{\"type\":\"integer\",\"description\":\"1-31\"},\"hour\":{\"type\":\"i"
    "nteger\",\"description\":\"0-23\"},\"minute\":{\"type\":\"integer\",\"description\":\"0-59\"},\"second\":{\"type\":\"int"
    "eger\",\"description\":\"0-59\"}}}}"
};
