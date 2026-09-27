#include "features/status/status_http.h"
#include "core/app.h"
#include "core/http_server.h"
#include "features/input/input.h"
#include "features/settings/settings.h"
#include "platform/power.h"

#include <stdio.h>
#include <unistd.h>

// The inner heap's bounds (see __libnx_initheap in main.c).
extern void *fake_heap_start;
extern void *fake_heap_end;

static bool g_keep_awake;

// GET /status
static void get_status(HttpRequest *req) {
    // Heap figures for sizing INNER_HEAP_SIZE: how far the heap has grown
    // (newlib rarely gives memory back, so this tracks the high-water mark).
    // Read from the break rather than mallinfo(): the prebuilt newlib's
    // mallinfo layout does not match its header.
    size_t heap_size = (size_t)((char *)fake_heap_end - (char *)fake_heap_start);
    size_t heap_arena = (size_t)((char *)sbrk(0) - (char *)fake_heap_start);

    u32 ver = hosversionGet();
    char battery[64] = "";
    uint32_t pct = 0;
    bool charging = false;
    if (settings_get_battery(&pct, &charging))
        snprintf(battery, sizeof(battery), ",\"batteryPercent\":%u,\"charging\":%s",
                 pct, charging ? "true" : "false");

    http_send_json(req->fd, 200,
                   "{\"version\":\"%s\","
                   "\"firmware\":\"%u.%u.%u\","
                   "\"controllerAttached\":%s,"
                   "\"keepAwake\":%s,"
                   "\"uptimeSeconds\":%llu,"
                   "\"heapSizeBytes\":%zu,"
                   "\"heapArenaBytes\":%zu"
                   "%s}",
                   app_version(),
                   HOSVER_MAJOR(ver), HOSVER_MINOR(ver), HOSVER_MICRO(ver),
                   input_is_attached() ? "true" : "false",
                   g_keep_awake ? "true" : "false",
                   (unsigned long long)app_uptime_seconds(),
                   heap_size, heap_arena,
                   battery);
}

void status_http_register(const Config *cfg) {
    // The setting only counts when idle:sys actually opened.
    g_keep_awake = cfg->keep_awake && power_keepawake_available();
    http_server_register_route("GET", "/status", get_status);
}
