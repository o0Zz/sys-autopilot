#include "features/screen/screen_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/screen/screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(CAPSSC_JPEG_BUFFER_SIZE + 0x14000 <= REQUEST_MEMORY_SIZE,
               "request memory too small for a re-encoded screenshot");

static bool query_int(HttpRequest *req, const char *key, int *out) {
    char val[16];
    if (!http_query_get(req, key, val, sizeof(val)) || val[0] == '\0')
        return false;
    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (*end != '\0')
        return false;
    *out = (int)v;
    return true;
}

// "x,y,w,h" as four non-negative integers. Not sscanf: that drags newlib's
// scanf into the binary.
static bool parse_crop(const char *s, int v[4]) {
    for (int i = 0; i < 4; i++) {
        char *end = NULL;
        long n = strtol(s, &end, 10);
        if (end == s || n < 0 || n > 100000 || *end != (i < 3 ? ',' : '\0'))
            return false;
        v[i] = (int)n;
        s = end + 1;
    }
    return true;
}

// stack, scale, quality and crop=x,y,w,h. Sends a 400 and returns false on a
// bad value.
static bool parse_opts(HttpRequest *req, ScreenOpts *o) {
    screen_opts_default(o);
    char val[32];
    if (http_query_get(req, "stack", val, sizeof(val)) && !screen_parse_stack(val, &o->stack)) {
        http_send_error(req->fd, 400, SCREEN_STACK_ERROR);
        return false;
    }
    if (http_query_get(req, "scale", val, sizeof(val))) {
        double scale = 0;
        json_parse_double(val, NULL, &scale);
        if (!screen_scale_div(scale, &o->jpeg.div)) {
            http_send_error(req->fd, 400, "invalid 'scale' (use 1, 0.5, 0.25 or 0.125)");
            return false;
        }
        o->transcode |= o->jpeg.div != 1;
    }
    if (http_query_get(req, "quality", val, sizeof(val))) {
        if (!screen_opts_set_quality(o, atoi(val))) {
            http_send_error(req->fd, 400, "invalid 'quality' (1-100)");
            return false;
        }
    }
    if (http_query_get(req, "crop", val, sizeof(val))) {
        int c[4];
        if (!parse_crop(val, c) || !screen_opts_set_crop(o, c[0], c[1], c[2], c[3])) {
            http_send_error(req->fd, 400, "invalid 'crop' (use crop=x,y,width,height)");
            return false;
        }
    }
    return true;
}

// GET /screenshot[?stack=...&scale=0.5&quality=80&crop=x,y,w,h]
static void get_screenshot(HttpRequest *req) {
    ScreenOpts o;
    if (!parse_opts(req, &o))
        return;
    size_t size = 0;
    Result rc = 0;
    char err[96];
    const u8 *jpeg = screen_capture(req, &o, &size, &rc, err, sizeof(err));
    if (!jpeg) {
        if (rc)
            http_send_json(req->fd, 500, "{\"error\":\"capture failed\",\"rc\":\"0x%x\"}", rc);
        else
            http_send_error(req->fd, 500, err);
        return;
    }
    http_send_response(req->fd, 200, "image/jpeg", jpeg, size);
}

// GET /wait/screen?until=change|stable&timeoutMs=&thresholdPercent=&stableMs=&stack=
static void get_wait_screen(HttpRequest *req) {
    ScreenWait w;
    screen_wait_init(&w);
    char val[32];
    if (http_query_get(req, "until", val, sizeof(val)) && !screen_wait_set_until(&w, val)) {
        http_send_error(req->fd, 400, "invalid 'until' (change or stable)");
        return;
    }
    if (http_query_get(req, "stack", val, sizeof(val)) && !screen_parse_stack(val, &w.stack)) {
        http_send_error(req->fd, 400, SCREEN_STACK_ERROR);
        return;
    }
    query_int(req, "timeoutMs", &w.timeout_ms);
    query_int(req, "stableMs", &w.stable_ms);
    if (http_query_get(req, "thresholdPercent", val, sizeof(val))) {
        w.threshold_pct = 0;
        json_parse_double(val, NULL, &w.threshold_pct);
        if (w.threshold_pct <= 0.0 || w.threshold_pct > 100.0) {
            http_send_error(req->fd, 400, "invalid 'thresholdPercent' (0-100]");
            return;
        }
    }

    ScreenWaitResult res;
    char err[96];
    if (!screen_wait(req, &w, &res, err, sizeof(err))) {
        http_send_error(req->fd, 500, err);
        return;
    }
    char diff[24];
    http_send_json(req->fd, 200, "{\"met\":%s,\"elapsedMs\":%d,\"diffPercent\":%s}",
                   res.met ? "true" : "false", res.elapsed_ms,
                   json_fmt_fixed(diff, sizeof(diff), res.diff_pct, 2));
}

void screen_http_register(void) {
    http_server_register_route("GET", "/screenshot", get_screenshot);
    http_server_register_route("GET", "/wait/screen", get_wait_screen);
}
