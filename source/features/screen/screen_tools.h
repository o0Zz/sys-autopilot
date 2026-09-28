// Generated from screen_tools.json by scripts/generate_resource.py -- do not edit by hand.
#pragma once

#include "features/mcp/mcp_server.h"

static const McpToolDef kToolScreenshot = {
    "{\"name\":\"screenshot\",\"description\":\"Capture the current Switch screen as a JPEG image (1280x720). Re"
    "turns the image directly so you can see what is on screen.\",\"inputSchema\":{\"type\":\"object\",\"properti"
    "es\":{\"stack\":{\"type\":\"string\",\"enum\":[\"screenshot\",\"default\",\"lcd\",\"recording\",\"lastframe\"],\"descrip"
    "tion\":\"Layer stack to capture. Default: screenshot (what the Capture button sees).\"}}}}"
};
