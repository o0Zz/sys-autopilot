#pragma once

#include <switch.h>

#include "core/http.h"
#include "util/jpeg.h"

// Screen capture through caps:sc. The caller owns the buffer (a request
// handler takes it from request memory); it must hold
// CAPSSC_JPEG_BUFFER_SIZE bytes.
Result screen_capture_jpeg(ViLayerStack stack, u8 *buf, size_t buf_size, u64 *out_size);

// Maps a query-param name to a ViLayerStack. Returns true if recognized.
bool screen_parse_stack(const char *name, ViLayerStack *out);

// --- scaled / cropped screenshots (screen_scale.c, host-testable) -------------

#define SCREEN_DEFAULT_QUALITY 80

typedef struct {
    ViLayerStack stack;
    // false: the hardware JPEG exactly as captured. true: decoded and
    // re-encoded through util/jpeg.h with `jpeg`.
    bool transcode;
    JpegScaleOpts jpeg;
} ScreenOpts;

// Full-size capture of the screenshot layer stack, no transcoding.
void screen_opts_default(ScreenOpts *o);

// Maps a scale factor (1, 0.5, 0.25 or 0.125) to its divisor. False for
// anything else.
bool screen_scale_div(double scale, int *div);

// Captures into request memory and transcodes when o->transcode is set.
// Returns the JPEG (size in *out_size), or NULL with a message in err and the
// capture result in *out_rc (0 when the failure came after the capture).
const u8 *screen_capture(HttpRequest *req, const ScreenOpts *o, size_t *out_size,
                         Result *out_rc, char *err, size_t errsz);

// Luma thumbnail of the screen at 1/8: 160x90 for the 1280x720 capture.
#define SCREEN_THUMB_MAX (160 * 90)

typedef struct {
    int w, h;
    u8 px[SCREEN_THUMB_MAX];
} ScreenThumb;

// Captures `stack` into jpeg_buf (CAPSSC_JPEG_BUFFER_SIZE bytes, reused across
// calls) and decodes it into *out. Decoder memory comes from request memory
// and is released before returning.
bool screen_capture_thumb(HttpRequest *req, ViLayerStack stack, u8 *jpeg_buf, ScreenThumb *out,
                          char *err, size_t errsz);

// Percentage (0..100) of thumbnail pixels whose luma differs by more than a
// small noise margin; 100 when the sizes differ.
double screen_thumb_diff(const ScreenThumb *a, const ScreenThumb *b);

// --- waiting for the screen ------------------------------------------------------

typedef enum {
    SCREEN_WAIT_CHANGE, // until the screen differs from when the wait began
    SCREEN_WAIT_STABLE, // until it stops changing for stable_ms
} ScreenWaitMode;

typedef struct {
    ScreenWaitMode mode;
    ViLayerStack stack;
    int timeout_ms;       // capped at HTTP_MAX_WAIT_MS
    double threshold_pct; // share of thumbnail pixels that counts as a change
    int stable_ms;        // SCREEN_WAIT_STABLE only
} ScreenWait;

typedef struct {
    bool met;        // false: timed out
    int elapsed_ms;
    double diff_pct; // last difference measured
} ScreenWaitResult;

#define SCREEN_WAIT_DEFAULT_TIMEOUT_MS 10000
// Defaults for what counts as a change. Stable is looser: highlighted menu
// items pulse, which alone moves up to ~1.5% of the screen frame to frame.
#define SCREEN_WAIT_DEFAULT_THRESHOLD  1.0
#define SCREEN_WAIT_STABLE_THRESHOLD   2.0
#define SCREEN_WAIT_DEFAULT_STABLE_MS  1000

// Polls the screen (as 1/8 luma thumbnails, about 5 times a second) until the
// condition holds or the timeout passes. Uses request memory, all of it
// released again before returning. False with err set when capturing fails.
bool screen_wait(HttpRequest *req, const ScreenWait *w, ScreenWaitResult *res,
                 char *err, size_t errsz);
