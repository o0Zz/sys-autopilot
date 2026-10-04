#include "features/nro/nro_mcp.h"
#include "core/request.h"
#include "features/mcp/mcp_server.h"
#include "features/nro/nro.h"
#include "features/nro/nro_tools.h"

#include <stdio.h>

static void tool_launch_nro(McpCall *call) {
    char path[512];
    if (!mcp_arg_string(call, "path", path, sizeof(path))) {
        mcp_reply_error(call, "missing 'path'");
        return;
    }
    char *args = request_alloc(call->req, NRO_ARGS_MAX);
    char *buf = request_alloc(call->req, NRO_BUF_SIZE);
    if (!args || !buf) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    size_t args_len;
    const char *arg_err;
    if (!nro_args_from_json(call->doc, call->args, args, &args_len, &arg_err)) {
        mcp_reply_error(call, arg_err);
        return;
    }

    char err[160];
    if (!nro_launch(path, args, args_len, buf, err, sizeof(err))) {
        mcp_reply_error(call, err);
        return;
    }
    char msg[600];
    snprintf(msg, sizeof(msg), "sent %s to hbmenu, which is starting it", path);
    mcp_reply_text(call, msg);
}

void nro_mcp_register(void) {
    mcp_server_register_tool(&kToolLaunchNro, tool_launch_nro);
}
