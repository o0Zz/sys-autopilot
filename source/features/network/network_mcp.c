#include "features/network/network_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/network/network.h"
#include "features/network/network_tools.h"

#include <stdio.h>

static void tool_get_dns(McpCall *call) {
    DnsConfig c;
    char err[96];
    if (!network_get_dns(&c, err, sizeof(err))) {
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
    bool automatic = false;
    mcp_arg_bool(call, "automatic", &automatic);
    char primary[64] = {0}, secondary[64] = {0};
    mcp_arg_string(call, "primary", primary, sizeof(primary));
    mcp_arg_string(call, "secondary", secondary, sizeof(secondary));

    if (!automatic && !primary[0]) {
        mcp_reply_error(call, "provide 'primary' (IPv4) or set 'automatic':true");
        return;
    }
    char err[96];
    if (!network_set_dns(automatic, primary, secondary, err, sizeof(err))) {
        mcp_reply_error(call, err);
        return;
    }
    char text[192];
    if (automatic)
        snprintf(text, sizeof(text), "DNS set to automatic (DHCP)");
    else
        snprintf(text, sizeof(text), "DNS set to manual: %s%s%s", primary,
                 secondary[0] ? ", " : "", secondary);
    mcp_reply_text(call, text);
}

void network_mcp_register(void) {
    mcp_server_register_tool(&kToolGetDns, tool_get_dns);
    mcp_server_register_tool(&kToolSetDns, tool_set_dns);
}
