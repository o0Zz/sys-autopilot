// Generated from process_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolProcessStatus = {
    "{\"name\":\"process_status\",\"description\":\"Check whether the program with the given title id is current"
    "ly running, and return its process id if so. Use this to confirm a sysmodule actually came up after "
    "starting it, or actually went away after stopping it.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"titleId\":{\"type\":\"string\",\"description\":\"Program (title) id as 16 hex digits, e.g. \\\"69000000000000"
    "0d\\\". This is the id /titles reports, and the directory name under /atmosphere/contents for a sysmod"
    "ule.\"}},\"required\":[\"titleId\"]}}"
};

static const McpToolDef kToolProcessStart = {
    "{\"name\":\"process_start\",\"description\":\"Launch the program with the given title id. Works for sysmodu"
    "les under /atmosphere/contents whether or not they have a flags/boot2.flag, so a module can be kept "
    "out of the boot sequence and started on demand instead. Fails if it is already running.\",\"inputSchem"
    "a\":{\"type\":\"object\",\"properties\":{\"titleId\":{\"type\":\"string\",\"description\":\"Program (title) id as 16"
    " hex digits, e.g. \\\"690000000000000d\\\". This is the id /titles reports, and the directory name under"
    " /atmosphere/contents for a sysmodule.\"}},\"required\":[\"titleId\"]}}"
};

static const McpToolDef kToolProcessStop = {
    "{\"name\":\"process_stop\",\"description\":\"Terminate the program with the given title id. WARNING: this i"
    "s a hard kill, so the program does not get to shut down cleanly; a sysmodule that holds system resou"
    "rces may leave them attached until the next reboot. Fails if it is not running.\",\"inputSchema\":{\"typ"
    "e\":\"object\",\"properties\":{\"titleId\":{\"type\":\"string\",\"description\":\"Program (title) id as 16 hex dig"
    "its, e.g. \\\"690000000000000d\\\". This is the id /titles reports, and the directory name under /atmosp"
    "here/contents for a sysmodule.\"}},\"required\":[\"titleId\"]}}"
};

static const McpToolDef kToolProcessRestart = {
    "{\"name\":\"process_restart\",\"description\":\"Stop the program with the given title id (if running), wait"
    " for it to actually exit, then start it again. This is the normal way to load a rebuilt sysmodule af"
    "ter uploading its exefs.nsp, and avoids the race of starting it while the old process is still dying"
    ".\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"titleId\":{\"type\":\"string\",\"description\":\"Program (t"
    "itle) id as 16 hex digits, e.g. \\\"690000000000000d\\\". This is the id /titles reports, and the direct"
    "ory name under /atmosphere/contents for a sysmodule.\"}},\"required\":[\"titleId\"]}}"
};
