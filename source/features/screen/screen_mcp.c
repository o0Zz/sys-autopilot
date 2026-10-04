#include "features/screen/screen_mcp.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/screen/screen.h"
#include "features/screen/screen_tools.h"

#include <stdio.h>
#include <string.h>

// The screenshot shares request memory with the JSON-RPC document the MCP
// server already holds, and a re-encoded one with the transcoder's state;
// make sure all of it fits.
_Static_assert(CAPSSC_JPEG_BUFFER_SIZE + 0x10000 + 0x14000 <= REQUEST_MEMORY_SIZE,
               "request memory too small for an MCP screenshot");

const char *screen_mcp_parse_opts(McpCall *call, const char *scale_key, ScreenOpts *o) {
    screen_opts_default(o);

    char val[24];
    if (mcp_arg_string(call, "stack", val, sizeof(val)) && !screen_parse_stack(val, &o->stack))
        return SCREEN_STACK_ERROR;

    double scale;
    if (mcp_arg_double(call, scale_key, &scale)) {
        if (!screen_scale_div(scale, &o->jpeg.div))
            return "invalid scale (use 1, 0.5, 0.25 or 0.125)";
        o->transcode |= o->jpeg.div != 1;
    }

    if (json_obj_get(call->doc, call->args, "quality") >= 0 &&
        !screen_opts_set_quality(o, mcp_arg_int(call, "quality", 0)))
        return "invalid 'quality' (1-100)";

    int crop = json_obj_get(call->doc, call->args, "crop");
    if (crop >= 0 &&
        !screen_opts_set_crop(o, json_obj_int(call->doc, crop, "x", -1),
                              json_obj_int(call->doc, crop, "y", -1),
                              json_obj_int(call->doc, crop, "width", -1),
                              json_obj_int(call->doc, crop, "height", -1)))
        return "invalid 'crop' (needs integer x, y, width, height)";
    return NULL;
}

static void tool_screenshot(McpCall *call) {
    ScreenOpts o;
    const char *perr = screen_mcp_parse_opts(call, "scale", &o);
    if (perr) {
        mcp_reply_error(call, perr);
        return;
    }
    size_t size = 0;
    Result rc;
    char err[96];
    const u8 *jpeg = screen_capture(call->req, &o, &size, &rc, err, sizeof(err));
    if (!jpeg) {
        mcp_reply_error(call, err);
        return;
    }
    mcp_reply_image(call, NULL, jpeg, size);
}

static void tool_wait_for_screen(McpCall *call) {
    ScreenWait w;
    screen_wait_init(&w);
    w.timeout_ms = mcp_arg_int(call, "timeoutMs", w.timeout_ms);
    w.stable_ms = mcp_arg_int(call, "stableMs", w.stable_ms);
    char val[16];
    if (mcp_arg_string(call, "until", val, sizeof(val)) && !screen_wait_set_until(&w, val)) {
        mcp_reply_error(call, "invalid 'until' (change or stable)");
        return;
    }
    double threshold;
    if (mcp_arg_double(call, "thresholdPercent", &threshold)) {
        if (threshold <= 0.0 || threshold > 100.0) {
            mcp_reply_error(call, "invalid 'thresholdPercent' (0-100]");
            return;
        }
        w.threshold_pct = threshold;
    }
    // The trailing screenshot's options are checked before waiting, not after.
    ScreenOpts shot;
    const char *perr = screen_mcp_parse_opts(call, "screenshotScale", &shot);
    if (perr) {
        mcp_reply_error(call, perr);
        return;
    }
    w.stack = shot.stack;

    ScreenWaitResult res;
    char err[96];
    if (!screen_wait(call->req, &w, &res, err, sizeof(err))) {
        mcp_reply_error(call, err);
        return;
    }

    char msg[160], diff[24];
    json_fmt_fixed(diff, sizeof(diff), res.diff_pct, 1);
    if (res.met && w.mode == SCREEN_WAIT_CHANGE)
        snprintf(msg, sizeof(msg), "screen changed after %d ms (%s%% of the screen)",
                 res.elapsed_ms, diff);
    else if (res.met)
        snprintf(msg, sizeof(msg), "screen stable after %d ms", res.elapsed_ms);
    else
        snprintf(msg, sizeof(msg), "timed out after %d ms: screen %s (last difference %s%%)",
                 res.elapsed_ms, w.mode == SCREEN_WAIT_CHANGE ? "did not change" : "kept changing",
                 diff);

    bool want = false;
    mcp_arg_bool(call, "screenshot", &want);
    if (!want) {
        mcp_reply_text(call, msg);
        return;
    }
    size_t size = 0;
    Result rc;
    const u8 *jpeg = screen_capture(call->req, &shot, &size, &rc, err, sizeof(err));
    if (!jpeg) {
        char full[288];
        snprintf(full, sizeof(full), "%s (screenshot failed: %s)", msg, err);
        mcp_reply_text(call, full);
        return;
    }
    mcp_reply_image(call, msg, jpeg, size);
}

void screen_mcp_register(void) {
    mcp_server_register_tool(&kToolScreenshot, tool_screenshot);
    mcp_server_register_tool(&kToolWaitForScreen, tool_wait_for_screen);
}
