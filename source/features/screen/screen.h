#pragma once

#include <switch.h>

// Screen capture through caps:sc. The caller owns the buffer (a request
// handler takes it from request memory); it must hold
// CAPSSC_JPEG_BUFFER_SIZE bytes.
Result screen_capture_jpeg(ViLayerStack stack, u8 *buf, size_t buf_size, u64 *out_size);

// Maps a query-param name to a ViLayerStack. Returns true if recognized.
bool screen_parse_stack(const char *name, ViLayerStack *out);
