#include "features/power/power_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/power/power_tools.h"
#include "platform/power.h"

// The action runs in the server loop once this response has been flushed.
static void schedule(McpCall *call, PowerAction action, const char *ok_msg) {
    if (!power_actions_available()) {
        mcp_reply_error(call, "power control unavailable");
        return;
    }
    mcp_reply_text(call, ok_msg);
    power_schedule(action);
}

static void tool_sleep(McpCall *call) {
    schedule(call, PowerAction_Sleep,
             "entering sleep mode; the server will be unreachable until "
             "a human wakes the console");
}

static void tool_restart(McpCall *call) {
    schedule(call, PowerAction_Restart,
             "rebooting; the server returns once the console boots back "
             "into CFW (bootloader menus may require human intervention)");
}

static void tool_power_off(McpCall *call) {
    schedule(call, PowerAction_PowerOff,
             "powering off; a human must press the power button to turn "
             "the console back on");
}

void power_mcp_register(void) {
    mcp_server_register_tool(&kToolSleep,    tool_sleep);
    mcp_server_register_tool(&kToolRestart,  tool_restart);
    mcp_server_register_tool(&kToolPowerOff, tool_power_off);
}
