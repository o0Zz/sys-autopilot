#pragma once

#include <switch.h>

#include "features/mcp/mcp_server.h"
#include "features/screen/screen.h"

// Registers the screenshot and wait_for_screen tools.
void screen_mcp_register(void);

// Reads the screenshot options from a tool's arguments: "stack", the scale
// under `scale_key` ("scale" for the screenshot tool, "screenshotScale" for
// tools that attach one), "quality" and "crop". Returns NULL or a message
// for the agent.
const char *screen_mcp_parse_opts(McpCall *call, const char *scale_key, ScreenOpts *o);
