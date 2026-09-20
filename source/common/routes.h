#pragma once

#include "http.h"
#include <stdint.h>

// Dispatches a parsed request to the matching endpoint handler and sends a
// complete HTTP response.
void routes_handle(HttpRequest *req);

// Shared with the MCP status tool.
const char *routes_app_version(void);
uint64_t routes_uptime_seconds(void);

// Reported by GET /status. Set once from main() after the config is loaded,
// because routes.c has no access to the Config.
void routes_set_keep_awake(bool on);
