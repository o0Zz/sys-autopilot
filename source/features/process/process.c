#include "features/process/process.h"
#include "features/input/input.h"
#include "core/http_server.h"
#include "core/log.h"

#include <stddef.h>

bool process_parse_title_id(const char *s, uint64_t *out) {
    const char *p = s;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    if (*p == '\0')
        return false;

    uint64_t v = 0;
    for (; *p; ++p) {
        int d;
        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else return false;
        v = (v << 4) | (uint64_t)d;
    }
    *out = v;
    return true;
}

#ifdef __SWITCH__

#include <switch.h>

// How long process_restart() waits for a terminated program to disappear
// before giving up. Termination is not observably instantaneous: pm returns
// once it has asked the kernel to kill the process, and pm:dmnt keeps
// resolving the program id until the process object is actually reaped.
#define RESTART_GRACE_NS   (3ULL * 1000000000ULL)
#define RESTART_POLL_NS    (50ULL * 1000000ULL)

// __appInit closes the sm session, so hid:dbg cannot be reopened at request
// time without reconnecting first. Without this the bare hiddbgInitialize()
// below silently leaves g_hiddbgSrv zeroed, and every later HDLS call fails
// with kernel InvalidHandle (0xe401) for the rest of the boot.
//
// hiddbgInitialize() is refcounted: calling it while the session is open only
// bumps the count, and the next hiddbgExit() in process_start() would then
// keep the session instead of handing it over. So only reopen a closed one.
static void reopen_hiddbg(void) {
    if (serviceIsActive(hiddbgGetServiceSession()))
        return;
    if (R_FAILED(smInitialize()))
        return;
    Result rc = hiddbgInitialize();
    if (R_FAILED(rc))
        LOGE("process", "hiddbgInitialize failed rc=0x%x", rc);
    smExit();
}

static bool g_initialized;
// The program process_start() last launched, while it runs. It holds what the
// launch lent it: hid:dbg, and the system memory boost.
static u64 g_last_pid;
static u64 g_last_program_id;

static void take_back(void);

bool process_init(void) {
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        LOGE("process", "pmshellInitialize failed rc=0x%x", rc);
        return false;
    }
    g_initialized = true;
    return true;
}

void process_exit(void) {
    if (!g_initialized)
        return;
    pmshellExit();
    g_initialized = false;
}

bool process_available(void) {
    return g_initialized;
}

// __appInit closes the sm session, so no service can be opened at request time
// without reopening it first. smInitialize is refcounted and reconnects to the
// named port, so this is safe to do per call. Pair with pminfoExit().
static bool open_pminfo(void) {
    Result rc = smInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = pminfoInitialize();
        smExit();
    }
    if (R_FAILED(rc)) {
        LOGE("process", "pminfoInitialize failed rc=0x%x", rc);
        return false;
    }
    return true;
}

int process_list(ProcessEntry *out, int max, uint32_t *out_rc) {
    *out_rc = 0;
    if (max > PROCESS_LIST_MAX)
        max = PROCESS_LIST_MAX;

    // Every pid on the system, including the kernel's initial processes, not
    // only the ones pm launched for us.
    u64 pids[PROCESS_LIST_MAX];
    s32 count = 0;
    Result rc = svcGetProcessList(&count, pids, (u32)max);
    if (R_FAILED(rc)) {
        LOGE("process", "svcGetProcessList failed rc=0x%x", rc);
        *out_rc = rc;
        return -1;
    }

    // Without pm:info the pids are still worth reporting on their own.
    bool have_info = open_pminfo();
    for (s32 i = 0; i < count; i++) {
        out[i].pid = pids[i];
        out[i].program_id = 0;
        out[i].has_program_id = have_info &&
            R_SUCCEEDED(pminfoGetProgramId(&out[i].program_id, pids[i]));
    }
    if (have_info)
        pminfoExit();
    return count;
}

// Finds a running program we did not launch ourselves by walking the full
// process list. Returns 0 when it is not running.
static u64 find_pid(u64 program_id) {
    u64 pids[PROCESS_LIST_MAX];
    s32 count = 0;
    if (R_FAILED(svcGetProcessList(&count, pids, PROCESS_LIST_MAX)) || !open_pminfo())
        return 0;
    u64 found = 0;
    for (s32 i = 0; i < count && !found; i++) {
        u64 tid = 0;
        if (R_SUCCEEDED(pminfoGetProgramId(&tid, pids[i])) && tid == program_id)
            found = pids[i];
    }
    pminfoExit();
    return found;
}

void process_status(uint64_t program_id, ProcessStatus *out) {
    // pm:dmnt accepts a single session and Atmosphere's own dmnt already holds
    // it, so it is not available here at all. Resolve the pid we were handed at
    // launch through pm:info instead: if it still maps to this program, the
    // process is alive.
    if (g_last_pid != 0 && g_last_program_id == program_id) {
        if (!open_pminfo()) {
            out->running = false;
            out->pid = 0;
            return;
        }
        u64 resolved = 0;
        Result rc = pminfoGetProgramId(&resolved, g_last_pid);
        pminfoExit();
        out->running = R_SUCCEEDED(rc) && resolved == program_id;
        out->pid = out->running ? g_last_pid : 0;
        if (!out->running) {
            LOGI("process", "%016llx exited; taking back hid:dbg",
                 (unsigned long long)program_id);
            take_back();
        }
        return;
    }

    u64 pid = 0;
    Result rc = R_FAILED(pmdmntInitialize()) ? MAKERESULT(Module_Libnx, LibnxError_NotFound)
                                             : pmdmntGetProcessId(&pid, program_id);
    pmdmntExit();
    // pm:dmnt is normally taken (see above), and pm does not distinguish "not
    // running" from other lookup errors in a way worth surfacing, so any
    // failure falls back to scanning every process.
    if (R_FAILED(rc))
        pid = find_pid(program_id);
    out->running = pid != 0;
    out->pid = pid;
}

// boot2 launches SD sysmodules while the system pool still has room. Launching
// one later fails with svc::ResultLimitReached (0x10801) because that pool is
// fully committed by then, so borrow from the application pool for the launch.
// The boost is an absolute size, not an increment; 0 releases it.
#define SYSTEM_MEMORY_BOOST 0x4000000ull // 64 MiB

static bool g_boosted;

static void set_boost(u64 size) {
    Result rc = pmshellBoostSystemMemoryResourceLimit(size);
    LOGI("process", "boost %llu -> rc=0x%x", (unsigned long long)size, rc);
    if (R_SUCCEEDED(rc))
        g_boosted = size != 0;
}

bool process_start(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    *out_rc = 0;
    if (!g_initialized)
        return false;

    // pm launches a second instance of a program that is already running.
    // With nx-ovlloader that hung the launch and then took the console down
    // (fatal), so refuse before touching the boost or hid:dbg.
    ProcessStatus st;
    process_status(program_id, &st);
    if (st.running) {
        *out_pid = st.pid;
        *out_rc = PROCESS_RC_ALREADY_RUNNING;
        return false;
    }

    // storageID None is what Atmosphere's boot2 uses to launch the sysmodules
    // under /atmosphere/contents; ldr resolves the program from the SD card.
    const NcmProgramLocation loc = {
        .program_id = program_id,
        .storageID  = NcmStorageId_None,
    };

    if (!g_boosted)
        set_boost(SYSTEM_MEMORY_BOOST);

    // hid:dbg is single-session. A launched sysmodule that drives virtual pads
    // (sys-con, for one) cannot initialise it while this server holds it, and
    // fails with SessionClosed before reaching main(). Hand it over for the
    // lifetime of that process; our own input endpoints stay unavailable until
    // it is stopped, which is the honest trade.
    input_suspend();
    hiddbgExit();

    u64 pid = 0;
    Result rc = pmshellLaunchProgram(0, &loc, &pid);
    if (R_FAILED(rc)) {
        LOGE("process", "launch %016llx failed rc=0x%x",
             (unsigned long long)program_id, rc);
        // Do not leave the application pool short, or our own input dead,
        // after a launch that never happened.
        if (g_boosted)
            set_boost(0);
        reopen_hiddbg();
        *out_rc = rc;
        return false;
    }
    g_last_pid = pid;
    g_last_program_id = program_id;
    *out_pid = pid;
    return true;
}

// The program process_start() launched is gone, stopped or exited on its own:
// take back what the launch lent it, or input stays dead and the application
// pool stays 64 MiB short until reboot.
static void take_back(void) {
    g_last_pid = 0;
    if (g_boosted)
        set_boost(0);
    reopen_hiddbg();
}

void process_reclaim(void) {
    if (g_last_pid == 0)
        return;
    ProcessStatus st;
    process_status(g_last_program_id, &st); // takes back when it is gone
}

bool process_stop(uint64_t program_id, uint32_t *out_rc) {
    *out_rc = 0;
    if (!g_initialized)
        return false;

    ProcessStatus st;
    process_status(program_id, &st);
    if (!st.running) {
        *out_rc = PROCESS_RC_NOT_RUNNING;
        return false;
    }

    Result rc = pmshellTerminateProgram(program_id);
    if (R_FAILED(rc)) {
        LOGE("process", "terminate %016llx failed rc=0x%x",
             (unsigned long long)program_id, rc);
        *out_rc = rc;
        return false;
    }
    // Also when this program was not launched here: one that boot2 started
    // may have held hid:dbg since boot, keeping ours from opening.
    take_back();
    return true;
}

bool process_restart(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    *out_rc = 0;
    if (!g_initialized)
        return false;

    ProcessStatus st;
    process_status(program_id, &st);
    if (st.running) {
        if (!process_stop(program_id, out_rc))
            return false;

        // Wait for the process to actually be gone before relaunching, so we
        // never hand the caller a pid from a launch that raced the teardown.
        u64 waited = 0;
        while (waited < RESTART_GRACE_NS) {
            process_status(program_id, &st);
            if (!st.running)
                break;
            svcSleepThread(RESTART_POLL_NS);
            waited += RESTART_POLL_NS;
        }
        if (st.running) {
            LOGW("process", "%016llx still running after stop",
                 (unsigned long long)program_id);
            return false;
        }
    }

    return process_start(program_id, out_pid, out_rc);
}

#else // host (tests): no pm

// The host build keeps a tiny in-memory model of one running program so the
// route and MCP layers can be exercised without a console.
static bool     g_fake_running;
static uint64_t g_fake_program_id;
static uint64_t g_fake_pid;

bool process_init(void) { return false; }
void process_exit(void) {}
bool process_available(void) { return true; } // tools testable on host

void process_status(uint64_t program_id, ProcessStatus *out) {
    out->running = g_fake_running && g_fake_program_id == program_id;
    out->pid = out->running ? g_fake_pid : 0;
}

bool process_start(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    *out_rc = 0;
    if (g_fake_running && g_fake_program_id == program_id) {
        *out_pid = g_fake_pid;
        *out_rc = PROCESS_RC_ALREADY_RUNNING;
        return false;
    }
    g_fake_running = true;
    g_fake_program_id = program_id;
    g_fake_pid = 0x42;
    *out_pid = g_fake_pid;
    return true;
}

bool process_stop(uint64_t program_id, uint32_t *out_rc) {
    *out_rc = 0;
    if (!g_fake_running || g_fake_program_id != program_id) {
        *out_rc = PROCESS_RC_NOT_RUNNING;
        return false;
    }
    g_fake_running = false;
    return true;
}

void process_reclaim(void) {}

bool process_restart(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    process_stop(program_id, out_rc);
    return process_start(program_id, out_pid, out_rc);
}

// One kernel initial process that pm does not know (no program id), then the
// fake program when it runs.
int process_list(ProcessEntry *out, int max, uint32_t *out_rc) {
    *out_rc = 0;
    int n = 0;
    if (n < max)
        out[n++] = (ProcessEntry){ .pid = 1 };
    if (g_fake_running && n < max)
        out[n++] = (ProcessEntry){ .pid = g_fake_pid, .program_id = g_fake_program_id,
                                   .has_program_id = true };
    return n;
}

#endif

// --- waiting ---------------------------------------------------------------------

#define PROCESS_WAIT_POLL_MS 100

bool process_wait(uint64_t program_id, bool want_running, int timeout_ms, ProcessStatus *out,
                  int *elapsed_ms) {
    if (timeout_ms < 0)
        timeout_ms = 0;
    if (timeout_ms > HTTP_MAX_WAIT_MS)
        timeout_ms = HTTP_MAX_WAIT_MS;
    uint64_t start = http_server_now_ms();
    for (;;) {
        process_status(program_id, out);
        *elapsed_ms = (int)(http_server_now_ms() - start);
        if (out->running == want_running)
            return true;
        if (*elapsed_ms >= timeout_ms)
            return false;
        int left = timeout_ms - *elapsed_ms;
        if (!http_server_wait_ms(left < PROCESS_WAIT_POLL_MS ? left : PROCESS_WAIT_POLL_MS))
            return false;
    }
}
