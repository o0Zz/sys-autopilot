// Scaled and cropped screenshots, and the luma thumbnails the screen wait
// compares. No libnx beyond screen_capture_jpeg(), so the host tests run it
// against their stub.
#include "features/screen/screen.h"
#include "core/http_server.h"
#include "core/request.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Two thumbnail pixels "differ" when their luma is further apart than this:
// enough to ignore JPEG noise between two captures of a still screen.
#define THUMB_NOISE 24

static void *req_alloc(void *ctx, size_t size) {
    return request_alloc(ctx, size);
}

void screen_opts_default(ScreenOpts *o) {
    memset(o, 0, sizeof(*o));
    o->stack = ViLayerStack_Screenshot;
    o->jpeg.div = 1;
    o->jpeg.quality = SCREEN_DEFAULT_QUALITY;
}

bool screen_scale_div(double scale, int *div) {
    static const int kDivs[] = {1, 2, 4, 8};
    for (size_t i = 0; i < sizeof(kDivs) / sizeof(kDivs[0]); i++) {
        double want = 1.0 / kDivs[i];
        if (scale > want * 0.99 && scale < want * 1.01) {
            *div = kDivs[i];
            return true;
        }
    }
    return false;
}

bool screen_opts_set_quality(ScreenOpts *o, int quality) {
    if (quality < 1 || quality > 100)
        return false;
    o->jpeg.quality = quality;
    o->transcode = true;
    return true;
}

bool screen_opts_set_crop(ScreenOpts *o, int x, int y, int w, int h) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0)
        return false;
    o->jpeg.crop_x = x;
    o->jpeg.crop_y = y;
    o->jpeg.crop_w = w;
    o->jpeg.crop_h = h;
    o->transcode = true;
    return true;
}

const u8 *screen_capture(HttpRequest *req, const ScreenOpts *o, size_t *out_size,
                         Result *out_rc, char *err, size_t errsz) {
    *out_rc = 0;
    u8 *buf = request_alloc(req, CAPSSC_JPEG_BUFFER_SIZE);
    if (!buf) {
        snprintf(err, errsz, "out of request memory");
        return NULL;
    }
    u64 size = 0;
    Result rc = screen_capture_jpeg(o->stack, buf, CAPSSC_JPEG_BUFFER_SIZE, &size);
    if (R_FAILED(rc)) {
        *out_rc = rc;
        snprintf(err, errsz, "capture failed (rc=0x%x)", rc);
        return NULL;
    }
    if (!o->transcode) {
        *out_size = (size_t)size;
        return buf;
    }

    // Move the capture to the end of the buffer and encode from its start:
    // the transcoder never writes past what it has already read.
    u8 *in = buf + CAPSSC_JPEG_BUFFER_SIZE - size;
    memmove(in, buf, (size_t)size);
    size_t out_len = 0;
    const char *jerr = jpeg_scale(in, (size_t)size, buf, CAPSSC_JPEG_BUFFER_SIZE, &o->jpeg,
                                  req_alloc, req, &out_len, NULL, NULL);
    if (jerr) {
        snprintf(err, errsz, "%s", jerr);
        return NULL;
    }
    *out_size = out_len;
    return buf;
}

bool screen_capture_thumb(HttpRequest *req, ViLayerStack stack, u8 *jpeg_buf, ScreenThumb *out,
                          char *err, size_t errsz) {
    u64 size = 0;
    Result rc = screen_capture_jpeg(stack, jpeg_buf, CAPSSC_JPEG_BUFFER_SIZE, &size);
    if (R_FAILED(rc)) {
        snprintf(err, errsz, "capture failed (rc=0x%x)", rc);
        return false;
    }
    size_t mark = request_mark(req);
    const char *jerr = jpeg_luma_thumbnail(jpeg_buf, (size_t)size, 8, out->px,
                                           sizeof(out->px), &out->w, &out->h, req_alloc, req);
    request_rewind(req, mark);
    if (jerr) {
        snprintf(err, errsz, "%s", jerr);
        return false;
    }
    return true;
}

double screen_thumb_diff(const ScreenThumb *a, const ScreenThumb *b) {
    if (a->w != b->w || a->h != b->h || a->w * a->h == 0)
        return 100.0;
    int n = a->w * a->h, changed = 0;
    for (int i = 0; i < n; i++)
        if (abs(a->px[i] - b->px[i]) > THUMB_NOISE)
            changed++;
    return 100.0 * changed / n;
}

void screen_wait_init(ScreenWait *w) {
    *w = (ScreenWait){
        .mode = SCREEN_WAIT_CHANGE,
        .stack = ViLayerStack_Screenshot,
        .timeout_ms = SCREEN_WAIT_DEFAULT_TIMEOUT_MS,
        .threshold_pct = SCREEN_WAIT_DEFAULT_THRESHOLD,
        .stable_ms = SCREEN_WAIT_DEFAULT_STABLE_MS,
    };
}

bool screen_wait_set_until(ScreenWait *w, const char *until) {
    if (strcmp(until, "stable") == 0) {
        w->mode = SCREEN_WAIT_STABLE;
        w->threshold_pct = SCREEN_WAIT_STABLE_THRESHOLD;
        return true;
    }
    return strcmp(until, "change") == 0;
}

#define SCREEN_WAIT_POLL_MS 200

bool screen_wait(HttpRequest *req, const ScreenWait *w, ScreenWaitResult *res,
                 char *err, size_t errsz) {
    memset(res, 0, sizeof(*res));
    int timeout = w->timeout_ms < 0 ? 0 : w->timeout_ms > HTTP_MAX_WAIT_MS ? HTTP_MAX_WAIT_MS
                                                                         : w->timeout_ms;
    size_t mark = request_mark(req);
    u8 *jpeg = request_alloc(req, CAPSSC_JPEG_BUFFER_SIZE);
    ScreenThumb *ref = request_alloc(req, sizeof(*ref));
    ScreenThumb *cur = request_alloc(req, sizeof(*cur));
    if (!jpeg || !ref || !cur) {
        request_rewind(req, mark);
        snprintf(err, errsz, "out of request memory");
        return false;
    }

    // The OS refuses captures for a moment during some transitions (an
    // applet starting, the software keyboard opening): exactly when a wait
    // runs. A failed capture is skipped; the wait fails only when none
    // succeeds before the timeout.
    bool have_ref = false;
    uint64_t start = http_server_now_ms(), stable_since = start;
    for (;;) {
        bool got = screen_capture_thumb(req, w->stack, jpeg, have_ref ? cur : ref, err, errsz);
        uint64_t now = http_server_now_ms();
        res->elapsed_ms = (int)(now - start);
        if (got && !have_ref) {
            have_ref = true;
            stable_since = now;
        } else if (got) {
            res->diff_pct = screen_thumb_diff(ref, cur);
            bool changed = res->diff_pct >= w->threshold_pct;
            if (w->mode == SCREEN_WAIT_CHANGE) {
                res->met = changed;
            } else {
                // Stable: compare each frame with the one before it.
                if (changed)
                    stable_since = now;
                else
                    res->met = (int)(now - stable_since) >= w->stable_ms;
                ScreenThumb *t = ref;
                ref = cur;
                cur = t;
            }
        }
        if (res->met || res->elapsed_ms >= timeout)
            break;
        int left = timeout - res->elapsed_ms;
        if (!http_server_wait_ms(left < SCREEN_WAIT_POLL_MS ? left : SCREEN_WAIT_POLL_MS))
            break;
    }
    request_rewind(req, mark);
    return have_ref;
}
