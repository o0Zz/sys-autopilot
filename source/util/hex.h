#pragma once

#include <stddef.h>
#include <stdint.h>

// Writes n bytes as 2n lowercase hex chars plus a NUL into out (2n + 1 bytes).
void hex_encode(const uint8_t *in, size_t n, char *out);
