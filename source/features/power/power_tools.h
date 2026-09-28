// Generated from power_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolSleep = {
    "{\"name\":\"sleep\",\"description\":\"Put the console into sleep mode. WARNING: the server becomes unreacha"
    "ble immediately and CANNOT be woken remotely - a human must physically press a button on the console"
    " or a paired controller to wake it. Only use when explicitly asked to.\",\"inputSchema\":{\"type\":\"objec"
    "t\",\"properties\":{}}}"
};

static const McpToolDef kToolRestart = {
    "{\"name\":\"restart\",\"description\":\"Reboot the console. WARNING: the server goes down immediately, and "
    "whether it returns without human help depends on the bootloader: setups that boot into a menu (e.g. "
    "Hekate without autoboot) require someone to manually re-launch the firmware. In-memory state (held b"
    "uttons, virtual controller) is lost. Only use when explicitly asked to.\",\"inputSchema\":{\"type\":\"obje"
    "ct\",\"properties\":{}}}"
};

static const McpToolDef kToolPowerOff = {
    "{\"name\":\"power_off\",\"description\":\"Fully power off the console. WARNING: the server becomes permanen"
    "tly unreachable - a human must physically press the power button to turn the console back on. Only u"
    "se when explicitly asked to.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
};
