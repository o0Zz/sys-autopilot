#include "scratch.h"

static unsigned char g_arena[SCRATCH_SIZE] __attribute__((aligned(0x1000)));
static size_t g_used;

void scratch_reset(void) {
    g_used = 0;
}

void *scratch_alloc(size_t size) {
    size_t start = (g_used + 15) & ~(size_t)15;
    if (start > SCRATCH_SIZE || size > SCRATCH_SIZE - start)
        return NULL;
    g_used = start + size;
    return g_arena + start;
}

size_t scratch_mark(void) {
    return g_used;
}

void scratch_release(size_t mark) {
    if (mark < g_used)
        g_used = mark;
}
