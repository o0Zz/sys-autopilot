#include "features/oauth/oauth_mcp.h"
#include "core/log.h"
#include "features/mcp/mcp_server.h"
#include "features/oauth/oauth.h"
#include "features/oauth/oauth_tools.h"

#include <stdio.h>

// Mints a bearer token over the already-authenticated MCP channel so agents
// can use the raw HTTP API (e.g. curl for large file uploads) without being
// given credentials out of band.
static void tool_create_token(McpCall *call) {
    char token[65];
    if (!oauth_mint_token(token, sizeof(token), "via create_token tool")) {
        mcp_reply_error(call, "failed to persist token");
        return;
    }
    char host[160];
    snprintf(host, sizeof(host), "%s", call->req->host[0] ? call->req->host : "<switch-ip>:<port>");
    char text[640];
    snprintf(text, sizeof(text),
             "token: %s\n\n"
             "Use it as a Bearer credential with the raw HTTP API, e.g. to "
             "upload a file:\n"
             "  curl -H 'Authorization: Bearer %s' -T myapp.nro "
             "'http://%s/files?path=/switch/myapp.nro'\n\n"
             "The token does not expire; revoke it by deleting its line from "
             "config/sys-autopilot/tokens.txt on the SD card.",
             token, token, host);
    mcp_reply_text(call, text);
}

static void tool_revoke_token(McpCall *call) {
    char token[160];
    if (!mcp_arg_string(call, "token", token, sizeof(token))) {
        mcp_reply_error(call, "missing 'token'");
        return;
    }
    if (!oauth_revoke_token(token)) {
        mcp_reply_error(call,
                        "unknown token (note: the static config.ini token "
                        "cannot be revoked this way)");
        return;
    }
    LOGF("mcp: revoked a token\n");
    mcp_reply_text(call, "token revoked");
}

void oauth_mcp_register(void) {
    mcp_server_register_tool(&kToolCreateToken, tool_create_token);
    mcp_server_register_tool(&kToolRevokeToken, tool_revoke_token);
}
