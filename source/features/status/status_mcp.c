#include "features/status/status_mcp.h"
#include "core/app.h"
#include "features/input/input.h"
#include "features/mcp/mcp_server.h"
#include "features/settings/settings.h"
#include "features/status/status_tools.h"

#include <stdio.h>

static void tool_status(McpCall *call) {
    u32 ver = hosversionGet();
    char text[256];
    int n = snprintf(text, sizeof(text),
             "version: %s\nfirmware: %u.%u.%u\ncontrollerAttached: %s\nuptimeSeconds: %llu",
             app_version(),
             HOSVER_MAJOR(ver), HOSVER_MINOR(ver), HOSVER_MICRO(ver),
             input_is_attached() ? "true" : "false",
             (unsigned long long)app_uptime_seconds());
    uint32_t pct = 0;
    bool charging = false;
    if (n > 0 && (size_t)n < sizeof(text) && settings_get_battery(&pct, &charging))
        snprintf(text + n, sizeof(text) - (size_t)n,
                 "\nbatteryPercent: %u\ncharging: %s",
                 pct, charging ? "true" : "false");
    mcp_reply_text(call, text);
}

void status_mcp_register(void) {
    mcp_server_register_tool(&kToolStatus, tool_status);
}
