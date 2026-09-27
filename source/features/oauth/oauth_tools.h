// Generated from oauth_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolCreateToken = {
    "create_token",
    "{\"name\":\"create_token\",\"description\":\"Create a new bearer token for the raw HTTP API (returned as te"
    "xt). Useful when a tool call is impractical, e.g. uploading large files with curl: pass it as an 'Au"
    "thorization: Bearer <token>' header. The token is a long-lived credential for this console - do not "
    "store it outside the current task. Revoke by deleting its line from config/sys-autopilot/tokens.txt."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolRevokeToken = {
    "revoke_token",
    "{\"name\":\"revoke_token\",\"description\":\"Revoke a bearer token previously issued via create_token or th"
    "e OAuth login flow (removes it from tokens.txt). Use this to clean up tokens you created once they a"
    "re no longer needed. Careful: revoking the token your own MCP connection uses will lock you out.\",\"i"
    "nputSchema\":{\"type\":\"object\",\"properties\":{\"token\":{\"type\":\"string\",\"description\":\"The token to revo"
    "ke.\"}},\"required\":[\"token\"]}}"
};
