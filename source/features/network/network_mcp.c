#include "features/network/network_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/network/network.h"
#include "features/network/network_tools.h"

#include <stdio.h>
#include <string.h>

static void tool_get_dns(McpCall *call) {
    DnsConfig c;
    char err[160];
    if (network_get_dns(&c, err, sizeof(err)) != NETWORK_OK) {
        mcp_reply_error(call, err);
        return;
    }
    char text[128];
    if (c.is_automatic)
        snprintf(text, sizeof(text), "automatic (DHCP): primary=%s secondary=%s",
                 c.primary, c.secondary);
    else
        snprintf(text, sizeof(text), "manual: primary=%s secondary=%s",
                 c.primary, c.secondary[0] ? c.secondary : "(none)");
    mcp_reply_text(call, text);
}

static void tool_set_dns(McpCall *call) {
    bool automatic;
    char primary[NETWORK_DNS_ARG_SIZE], secondary[NETWORK_DNS_ARG_SIZE];
    const char *arg_err;
    if (!network_dns_from_json(call->doc, call->args, &automatic, primary, secondary,
                               &arg_err)) {
        mcp_reply_error(call, arg_err);
        return;
    }
    char err[160];
    if (network_set_dns(automatic, primary, secondary, err, sizeof(err)) != NETWORK_OK) {
        mcp_reply_error(call, err);
        return;
    }
    char text[256];
    if (automatic)
        snprintf(text, sizeof(text), "DNS set to automatic (DHCP)");
    else
        snprintf(text, sizeof(text), "DNS set to manual: %s%s%s", primary,
                 secondary[0] ? ", " : "", secondary);
    snprintf(text + strlen(text), sizeof(text) - strlen(text),
             ". The console reconnects to apply it: wait a few seconds before the next call.");
    mcp_reply_text(call, text);
}

void network_mcp_register(void) {
    mcp_server_register_tool(&kToolGetDns, tool_get_dns);
    mcp_server_register_tool(&kToolSetDns, tool_set_dns);
}
