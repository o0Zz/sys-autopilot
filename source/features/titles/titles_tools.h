// Generated from titles_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolListInstalledTitles = {
    "{\"name\":\"list_installed_titles\",\"description\":\"List installed applications (titles) on the console. "
    "Returns one line per title: 16-hex title id, content-meta version, storage (sd/nand/gamecard), and d"
    "isplay name. Useful for confirming an install or finding a title id. Lists base applications only (n"
    "ot updates or DLC).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};
