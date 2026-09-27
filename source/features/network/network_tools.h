// Generated from network_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolGetDns = {
    "get_dns",
    "{\"name\":\"get_dns\",\"description\":\"Get the console's current DNS configuration for the active network "
    "connection: whether it's automatic (DHCP) or manual, and the primary/secondary DNS server addresses."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};

static const McpToolDef kToolSetDns = {
    "set_dns",
    "{\"name\":\"set_dns\",\"description\":\"Set the active connection's DNS servers. Pass 'primary' (and option"
    "al 'secondary') as IPv4 strings to set manual DNS, or 'automatic':true to revert to DHCP. Useful for"
    " pointing the console at a custom/black-hole DNS (e.g. 90DNS) to block Nintendo servers while stayin"
    "g on the LAN.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"automatic\":{\"type\":\"boolean\",\"descripti"
    "on\":\"true = revert to DHCP DNS (ignores primary/secondary).\"},\"primary\":{\"type\":\"string\",\"descriptio"
    "n\":\"Primary DNS IPv4, e.g. \\\"207.246.121.77\\\".\"},\"secondary\":{\"type\":\"string\",\"description\":\"Optiona"
    "l secondary DNS IPv4.\"}}}}"
};
