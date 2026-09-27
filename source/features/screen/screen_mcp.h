#pragma once

#include <switch.h>

#include "features/mcp/mcp_server.h"

// Registers the screenshot tool.
void screen_mcp_register(void);

// Captures a screenshot into request memory for an MCP reply. Returns the
// buffer (size in *out_size), or NULL with *out_rc set.
const u8 *screen_mcp_capture(McpCall *call, ViLayerStack stack, u64 *out_size, Result *out_rc);
