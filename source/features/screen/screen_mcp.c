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
        return "invalid 'stack'";

    double scale;
    if (mcp_arg_double(call, scale_key, &scale)) {
        if (!screen_scale_div(scale, &o->jpeg.div))
            return "invalid scale (use 1, 0.5, 0.25 or 0.125)";
        o->transcode |= o->jpeg.div != 1;
    }

    int quality = mcp_arg_int(call, "quality", 0);
    if (json_obj_get(call->doc, call->args, "quality") >= 0) {
        if (quality < 1 || quality > 100)
            return "invalid 'quality' (1-100)";
        o->jpeg.quality = quality;
        o->transcode = true;
    }

    int crop = json_obj_get(call->doc, call->args, "crop");
    if (crop >= 0) {
        int x = json_obj_int(call->doc, crop, "x", -1);
        int y = json_obj_int(call->doc, crop, "y", -1);
        int w = json_obj_int(call->doc, crop, "width", -1);
        int h = json_obj_int(call->doc, crop, "height", -1);
        if (x < 0 || y < 0 || w <= 0 || h <= 0)
            return "invalid 'crop' (needs integer x, y, width, height)";
        o->jpeg.crop_x = x;
        o->jpeg.crop_y = y;
        o->jpeg.crop_w = w;
        o->jpeg.crop_h = h;
        o->transcode = true;
    }
    return NULL;
}

const u8 *screen_mcp_capture(McpCall *call, const ScreenOpts *o, size_t *out_size,
                             char *err, size_t errsz) {
    Result rc;
    return screen_capture(call->req, o, out_size, &rc, err, errsz);
}

static void tool_screenshot(McpCall *call) {
    ScreenOpts o;
    const char *perr = screen_mcp_parse_opts(call, "scale", &o);
    if (perr) {
        mcp_reply_error(call, perr);
        return;
    }
    size_t size = 0;
    char err[96];
    const u8 *jpeg = screen_mcp_capture(call, &o, &size, err, sizeof(err));
    if (!jpeg) {
        mcp_reply_error(call, err);
        return;
    }
    mcp_reply_image(call, jpeg, size);
}

static void tool_wait_for_screen(McpCall *call) {
    ScreenWait w = {
        .mode = SCREEN_WAIT_CHANGE,
        .stack = ViLayerStack_Screenshot,
        .timeout_ms = mcp_arg_int(call, "timeoutMs", SCREEN_WAIT_DEFAULT_TIMEOUT_MS),
        .threshold_pct = SCREEN_WAIT_DEFAULT_THRESHOLD,
        .stable_ms = mcp_arg_int(call, "stableMs", SCREEN_WAIT_DEFAULT_STABLE_MS),
    };
    char val[16];
    if (mcp_arg_string(call, "until", val, sizeof(val))) {
        if (strcmp(val, "stable") == 0) {
            w.mode = SCREEN_WAIT_STABLE;
            w.threshold_pct = SCREEN_WAIT_STABLE_THRESHOLD;
        } else if (strcmp(val, "change") != 0) {
            mcp_reply_error(call, "invalid 'until' (change or stable)");
            return;
        }
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

    char msg[160];
    if (res.met && w.mode == SCREEN_WAIT_CHANGE)
        snprintf(msg, sizeof(msg), "screen changed after %d ms (%.1f%% of the screen)",
                 res.elapsed_ms, res.diff_pct);
    else if (res.met)
        snprintf(msg, sizeof(msg), "screen stable after %d ms", res.elapsed_ms);
    else
        snprintf(msg, sizeof(msg), "timed out after %d ms: screen %s (last difference %.1f%%)",
                 res.elapsed_ms, w.mode == SCREEN_WAIT_CHANGE ? "did not change" : "kept changing",
                 res.diff_pct);

    bool want = false;
    mcp_arg_bool(call, "screenshot", &want);
    if (!want) {
        mcp_reply_text(call, msg);
        return;
    }
    size_t size = 0;
    const u8 *jpeg = screen_mcp_capture(call, &shot, &size, err, sizeof(err));
    if (!jpeg) {
        char full[288];
        snprintf(full, sizeof(full), "%s (screenshot failed: %s)", msg, err);
        mcp_reply_text(call, full);
        return;
    }
    mcp_reply_text_and_image(call, msg, jpeg, size);
}

void screen_mcp_register(void) {
    mcp_server_register_tool(&kToolScreenshot, tool_screenshot);
    mcp_server_register_tool(&kToolWaitForScreen, tool_wait_for_screen);
}
