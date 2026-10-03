// Host tests for the streaming JPEG transcoder (util/jpeg.c): decoding the
// fixtures cjpeg made (4:2:0 with restart markers, 4:4:4, grayscale),
// scaling, cropping, re-encoding, and the in-place buffer layout the
// screenshot path uses.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/jpeg.h"
#include "jpeg_fixtures.h"

#define W 48
#define H 40

// Every allocation the transcoder makes, freed by free_all().
static void *g_allocs[64];
static int g_nallocs;
static size_t g_alloc_bytes;

static void *test_alloc(void *ctx, size_t size) {
    (void)ctx;
    assert(g_nallocs < 64);
    g_alloc_bytes += size;
    return g_allocs[g_nallocs++] = malloc(size);
}

static void free_all(void) {
    while (g_nallocs > 0)
        free(g_allocs[--g_nallocs]);
    g_alloc_bytes = 0;
}

// The pattern the fixtures were made from, as luma.
static int pattern_luma(int x, int y) {
    int r = (x * 5) % 256, g = (y * 6) % 256, b = 128 + (((x / 8) + (y / 8)) % 2) * 60;
    return (int)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
}

// Mean absolute difference between a decoded luma image and the pattern,
// box-averaged by div over the crop origin (x0, y0).
static double luma_error(const uint8_t *img, int w, int h, int div, int x0, int y0) {
    double err = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sum = 0, n = 0;
            for (int yy = y0 + y * div; yy < y0 + (y + 1) * div && yy < H; yy++)
                for (int xx = x0 + x * div; xx < x0 + (x + 1) * div && xx < W; xx++) {
                    sum += pattern_luma(xx, yy);
                    n++;
                }
            int want = (sum + n / 2) / n;
            err += abs(img[y * w + x] - want);
        }
    return err / (w * h);
}

static void test_decode_fixtures(void) {
    static const struct { const uint8_t *data; size_t len; const char *name; } kFix[] = {
        { kC420, sizeof(kC420), "4:2:0+restarts" },
        { kC444, sizeof(kC444), "4:4:4" },
        { kGray, sizeof(kGray), "gray" },
    };
    for (size_t i = 0; i < sizeof(kFix) / sizeof(kFix[0]); i++) {
        int w = 0, h = 0;
        assert(jpeg_get_size(kFix[i].data, kFix[i].len, &w, &h));
        assert(w == W && h == H);

        for (int div = 1; div <= 8; div *= 2) {
            uint8_t thumb[W * H];
            const char *err = jpeg_luma_thumbnail(kFix[i].data, kFix[i].len, div, thumb,
                                                  sizeof(thumb), &w, &h, test_alloc, NULL);
            assert(err == NULL);
            assert(w == (W + div - 1) / div && h == (H + div - 1) / div);
            double e = luma_error(thumb, w, h, div, 0, 0);
            printf("  %s div %d: mean luma error %.2f\n", kFix[i].name, div, e);
            assert(e < 3.0);
            free_all();
        }
    }
    printf("decode fixtures ok\n");
}

// Transcodes, then decodes the result and checks it against the pattern.
static void check_scale(const uint8_t *src, size_t len, JpegScaleOpts o, int want_w, int want_h,
                        int x0, int y0) {
    static uint8_t out[64 * 1024];
    size_t out_len = 0;
    int w = 0, h = 0;
    const char *err = jpeg_scale(src, len, out, sizeof(out), &o, test_alloc, NULL, &out_len, &w, &h);
    assert(err == NULL);
    assert(w == want_w && h == want_h);
    assert(out_len > 100 && out[0] == 0xFF && out[1] == 0xD8);
    assert(out[out_len - 2] == 0xFF && out[out_len - 1] == 0xD9);
    free_all();

    int dw = 0, dh = 0;
    assert(jpeg_get_size(out, out_len, &dw, &dh) && dw == want_w && dh == want_h);
    uint8_t thumb[W * H];
    err = jpeg_luma_thumbnail(out, out_len, 1, thumb, sizeof(thumb), &dw, &dh, test_alloc, NULL);
    assert(err == NULL && dw == want_w && dh == want_h);
    double e = luma_error(thumb, dw, dh, o.div, x0, y0);
    printf("  div %d q %d crop %d,%d %dx%d -> %dx%d, %zu bytes, error %.2f\n", o.div, o.quality,
           o.crop_x, o.crop_y, o.crop_w, o.crop_h, w, h, out_len, e);
    assert(e < 4.0);
    free_all();
}

static void test_scale(void) {
    check_scale(kC420, sizeof(kC420), (JpegScaleOpts){ .div = 1, .quality = 95 }, 48, 40, 0, 0);
    check_scale(kC420, sizeof(kC420), (JpegScaleOpts){ .div = 2, .quality = 90 }, 24, 20, 0, 0);
    check_scale(kC444, sizeof(kC444), (JpegScaleOpts){ .div = 4, .quality = 90 }, 12, 10, 0, 0);
    check_scale(kGray, sizeof(kGray), (JpegScaleOpts){ .div = 8, .quality = 90 }, 6, 5, 0, 0);
    // Crops: origin widened down to a multiple of div, far edge clipped.
    check_scale(kC420, sizeof(kC420),
                (JpegScaleOpts){ .div = 1, .quality = 95, .crop_x = 5, .crop_y = 9,
                                 .crop_w = 20, .crop_h = 13 }, 20, 13, 5, 9);
    check_scale(kC444, sizeof(kC444),
                (JpegScaleOpts){ .div = 2, .quality = 95, .crop_x = 5, .crop_y = 9,
                                 .crop_w = 100, .crop_h = 100 }, 22, 16, 4, 8);
    printf("scale ok\n");
}

// The screenshot path: input moved to the end of one buffer, output written
// from its start.
static void test_in_place(void) {
    static uint8_t buf[8 * 1024];
    size_t in_len = sizeof(kC420);
    uint8_t *in = buf + sizeof(buf) - in_len;
    memcpy(in, kC420, in_len);
    JpegScaleOpts o = { .div = 2, .quality = 80 };
    size_t out_len = 0;
    const char *err = jpeg_scale(in, in_len, buf, sizeof(buf), &o, test_alloc, NULL, &out_len,
                                 NULL, NULL);
    assert(err == NULL);
    free_all();
    uint8_t thumb[W * H];
    int w, h;
    assert(jpeg_luma_thumbnail(buf, out_len, 1, thumb, sizeof(thumb), &w, &h, test_alloc, NULL) == NULL);
    assert(w == 24 && h == 20 && luma_error(thumb, w, h, 2, 0, 0) < 4.0);
    free_all();

    // No room between output and input: fails cleanly instead of overwriting.
    static uint8_t tight[sizeof(kC420) + 64];
    in = tight + 64;
    memcpy(in, kC420, sizeof(kC420));
    o.div = 1;
    o.quality = 100;
    err = jpeg_scale(in, sizeof(kC420), tight, sizeof(tight), &o, test_alloc, NULL, &out_len,
                     NULL, NULL);
    assert(err && strstr(err, "too small"));
    free_all();
    printf("in place ok\n");
}

static void test_errors(void) {
    uint8_t out[1024];
    size_t out_len;
    JpegScaleOpts o = { .div = 2, .quality = 80 };
    const char *err = jpeg_scale(kProg, sizeof(kProg), out, sizeof(out), &o, test_alloc, NULL,
                                 &out_len, NULL, NULL);
    assert(err && strstr(err, "progressive"));
    free_all();

    err = jpeg_scale(kC420, 200, out, sizeof(out), &o, test_alloc, NULL, &out_len, NULL, NULL);
    assert(err != NULL);
    free_all();

    static const uint8_t junk[] = "not a jpeg at all";
    err = jpeg_scale(junk, sizeof(junk), out, sizeof(out), &o, test_alloc, NULL, &out_len, NULL, NULL);
    assert(err && strstr(err, "not a JPEG"));
    free_all();

    o.div = 3;
    err = jpeg_scale(kC420, sizeof(kC420), out, sizeof(out), &o, test_alloc, NULL, &out_len, NULL, NULL);
    assert(err && strstr(err, "scale"));
    free_all();

    o.div = 1;
    o.crop_x = 100;
    o.crop_y = 0;
    o.crop_w = 10;
    o.crop_h = 10;
    err = jpeg_scale(kC420, sizeof(kC420), out, sizeof(out), &o, test_alloc, NULL, &out_len, NULL, NULL);
    assert(err && strstr(err, "outside"));
    free_all();
    printf("errors ok\n");
}

int main(void) {
    test_decode_fixtures();
    test_scale();
    test_in_place();
    test_errors();
    printf("all jpeg tests passed\n");
    return 0;
}
