#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Baseline JPEG transcoder sized for a sysmodule: it decodes one MCU row at a
// time and re-encodes on the fly, so working memory stays around 64 KB for a
// 1280x720 screenshot instead of the 2.7 MB a decoded frame would take.
//
// Input: baseline (SOF0/SOF1) Huffman JPEG, 8-bit, 1 or 3 components, any
// sampling factors up to 4, restart markers allowed. Progressive and
// arithmetic-coded files are rejected.
//
// Output: baseline 4:4:4 JPEG at 1/div of the (optionally cropped) input,
// each output pixel the average of a div x div box.

// Working memory comes from the caller (request memory in the sysmodule,
// malloc in the host tests). Returns NULL when exhausted.
typedef void *(*JpegAlloc)(void *ctx, size_t size);

typedef struct {
    int div;     // 1, 2, 4 or 8
    int quality; // 1..100, as in libjpeg
    // Crop rectangle in source pixels; crop_w == 0 means the whole image.
    // It is widened to multiples of div and clipped to the image.
    int crop_x, crop_y, crop_w, crop_h;
} JpegScaleOpts;

// Transcodes in[0..in_len) into out[0..out_cap). `out` may sit below `in` in
// the same buffer: writes never reach the input not yet read, and running
// into it fails cleanly. On success returns NULL and sets *out_len (and the
// output size in *out_w / *out_h when not NULL); otherwise returns a static
// error message.
const char *jpeg_scale(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                       const JpegScaleOpts *opts, JpegAlloc alloc, void *alloc_ctx,
                       size_t *out_len, int *out_w, int *out_h);

// Decodes in[0..in_len) to an 8-bit luma image at 1/div (div 8 skips the
// inverse DCT entirely, so it is cheap). out receives *out_w x *out_h bytes,
// row-major. Returns NULL on success or a static error message.
const char *jpeg_luma_thumbnail(const uint8_t *in, size_t in_len, int div,
                                uint8_t *out, size_t out_cap, int *out_w, int *out_h,
                                JpegAlloc alloc, void *alloc_ctx);

// Reads the image size from the frame header without decoding.
bool jpeg_get_size(const uint8_t *in, size_t in_len, int *w, int *h);
