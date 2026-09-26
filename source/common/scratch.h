#pragma once

#include <stddef.h>

// One request-scoped arena for the large transient buffers: the screenshot
// JPEG, the title installer's stream and header buffers, the title listing,
// file I/O. The server handles one request at a time, so none of these ever
// need to outlive the request that uses them. Carving them all out of a single
// arena instead of giving each feature its own static buffer means only the
// largest of them costs resident memory, not the sum.
//
// Sized for the biggest single user: capssc's JPEG buffer.
#define SCRATCH_SIZE 0x80000

// Frees everything. Called at the start of every request.
void scratch_reset(void);

// Bump-allocates `size` bytes (16-byte aligned, not zeroed) that live until
// the next reset or release. NULL when the arena is exhausted.
void *scratch_alloc(size_t size);

// Mark/release for helpers that may run several times within one request:
// take a mark, allocate, and release back to it before returning.
size_t scratch_mark(void);
void scratch_release(size_t mark);
