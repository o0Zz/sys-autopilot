#include <switch.h>

#include "core/app.h"
#include "core/config.h"
#include "core/http_server.h"
#include "core/log.h"
#include "features/feature_list.h"
#include "platform/device_info.h"
#include "platform/netif.h"
#include "platform/power.h"

// Inner heap: socket transfer memory + stdio buffers + dir listing JSON +
// headroom for the title installer (ncm IPC, mounting the cnmt NCA). The large
// transient buffers (JPEG, I/O, installer, title listing) live in request
// memory (core/request.h), not here; GET /status reports how much of this
// heap is actually used.
#define INNER_HEAP_SIZE 0x100000

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// Sysmodules use time:s (the npdm grants it); used for the OAuth
// "# issued <date>" stamps and the read-only get_datetime tool.
u32 __nx_time_service_type = TimeServiceType_System;

// Internal libnx helper that wires newlib's time() to the time service.
void __libnx_init_time(void);

void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

// Socket buffers. The pool bsd allocates for us is
// sb_efficiency * (tcp_tx + tcp_rx + udp_tx + udp_rx), and every socket in
// existence draws its buffers from it -- including connections merely waiting
// in the listen backlog. At sb_efficiency 2 that pool held the listener plus
// exactly one connection, so while any request was in flight the next client
// was accepted by the stack and then immediately reset, which is what made
// browsers (several connections per page load) and the REST clients see
// "connection forcibly closed" at random.
//
// Smaller per-socket buffers buy more of them for roughly the same memory:
// this pool is ~520K against ~232K before, and holds ten sockets instead of
// two. The REST payloads are small, and a 16K receive window still streams a
// screenshot or a sysmodule upload at several MB/s on a LAN.
static const SocketInitConfig kSocketConfig = {
    .tcp_tx_buf_size     = 0x4000,
    .tcp_rx_buf_size     = 0x4000,
    .tcp_tx_buf_max_size = 0,       // fixed size
    .tcp_rx_buf_max_size = 0,       // fixed size
    .udp_tx_buf_size     = 0x2400,
    .udp_rx_buf_size     = 0x2400,  // mDNS packets are ~1.5K
    .sb_efficiency       = 10,
    // Sessions are bsd IPC channels, not sockets: this server is
    // single-threaded, so it never needs more than a couple.
    .num_bsd_sessions    = 4,
    .bsd_service_type    = BsdServiceType_User,
};

void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    rc = hiddbgInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    rc = capsscInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    // Optional: wall-clock time for OAuth token issuance stamps.
    rc = timeInitialize();
    if (R_SUCCEEDED(rc))
        __libnx_init_time();

    rc = socketInitialize(&kSocketConfig);
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    // Register with PSC so we can quiesce sockets across sleep (must happen
    // while the sm session is open). Failure is non-fatal but means sleep
    // will crash the console, so abort loudly in that case.
    if (!power_init())
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_NotFound));

    // For agent-requested sleep/restart/power-off. Non-fatal when missing:
    // the endpoints report unavailability.
    power_spsm_init();

    // idle:sys, used to hold off auto-sleep. Non-fatal: without it the console
    // sleeps on its timeout and stops answering. The session is opened
    // unconditionally because config.ini is only read after __appInit; whether
    // we actually ping is decided by the server loop.
    if (!power_keepawake_init())
        LOGW("power", "idle:sys unavailable; auto-sleep cannot be held off");

    // Gather device facts (model/firmware/Atmosphère) for the mDNS TXT record
    // now, while the sm session is still open: the underlying set:sys/spl
    // smGetService calls would fail after smExit(). Best-effort; failures just
    // leave the corresponding TXT fields empty.
    device_info_init();

    // Network Interface Manager: nifm gives us the real LAN IP for mDNS
    // (gethostid() only ever returns loopback in a sysmodule). Must be opened
    // while sm is up; the session is held for the process lifetime.
    if (!netif_init())
        LOGE("netif", "nifm init failed; mDNS A records unavailable");

    // The services each feature needs (settings, installer, process
    // control). Opened while sm is up; all best-effort, a missing service just
    // makes its endpoints report unavailability.
    features_init();

    smExit();
}

void __appExit(void)
{
    features_exit();
    netif_exit();
    power_keepawake_exit();
    power_spsm_exit();
    power_exit();
    timeExit();
    socketExit();
    capsscExit();
    hiddbgExit();
    fsdevUnmountAll();
    fsExit();
}

#ifdef __cplusplus
}
#endif

int main(int argc, char* argv[])
{
    Config cfg;
    config_load(&cfg);

    // Logged here, not in __appInit: the file sink stays off until config_load
    // has read the `log` level.
    const DeviceInfo *dev = device_info_get();
    LOGI("main", "sys-autopilot %s started (build %s %s) - https://github.com/o0Zz/sys-autopilot",
         app_version(), __DATE__, __TIME__);
    LOGI("main", "model=%s firmware=%s atmosphere=%s", dev->model, dev->firmware,
         dev->atmosphere[0] != '\0' ? dev->atmosphere : "n/a");

    // Every feature plugs its routes and MCP tools into the servers.
    features_register(&cfg);

    // Blocks forever.
    http_server_run(&cfg);
    return 0;
}
