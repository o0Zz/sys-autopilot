#include "core/app.h"

#include <switch.h>

// Injected by the Makefile (git tag or commit hash).
#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

static u64 g_boot_tick;

__attribute__((constructor)) static void init_boot_tick(void) {
    g_boot_tick = armGetSystemTick();
}

const char *app_version(void) {
    return APP_VERSION;
}

uint64_t app_uptime_seconds(void) {
    return armTicksToNs(armGetSystemTick() - g_boot_tick) / 1000000000ULL;
}
