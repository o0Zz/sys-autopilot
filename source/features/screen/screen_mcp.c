#include "features/screen/screen_mcp.h"
#include "core/request.h"
#include "features/screen/screen.h"
#include "features/screen/screen_tools.h"

#include <stdio.h>

// The screenshot shares request memory with the JSON-RPC document the MCP
// server already holds; make sure both fit.
_Static_assert(CAPSSC_JPEG_BUFFER_SIZE + 0x10000 <= REQUEST_MEMORY_SIZE,
               "request memory too small for an MCP screenshot");

const u8 *screen_mcp_capture(McpCall *call, ViLayerStack stack, u64 *out_size, Result *out_rc) {
    u8 *jpeg = request_alloc(call->req, CAPSSC_JPEG_BUFFER_SIZE);
    if (!jpeg) {
        *out_rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        return NULL;
    }
    *out_rc = screen_capture_jpeg(stack, jpeg, CAPSSC_JPEG_BUFFER_SIZE, out_size);
    return R_SUCCEEDED(*out_rc) ? jpeg : NULL;
}

static void tool_screenshot(McpCall *call) {
    ViLayerStack stack = ViLayerStack_Screenshot;
    char val[24];
    if (mcp_arg_string(call, "stack", val, sizeof(val)) && !screen_parse_stack(val, &stack)) {
        mcp_reply_error(call, "invalid 'stack'");
        return;
    }

    u64 size = 0;
    Result rc;
    const u8 *jpeg = screen_mcp_capture(call, stack, &size, &rc);
    if (!jpeg) {
        char msg[64];
        snprintf(msg, sizeof(msg), "capture failed (rc=0x%x)", rc);
        mcp_reply_error(call, msg);
        return;
    }
    mcp_reply_image(call, jpeg, (size_t)size);
}

void screen_mcp_register(void) {
    mcp_server_register_tool(&kToolScreenshot, tool_screenshot);
}
