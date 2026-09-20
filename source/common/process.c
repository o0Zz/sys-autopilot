#include "process.h"
#include "input.h"
#include "log.h"

#include <stddef.h>

#ifdef __SWITCH__

#include <switch.h>

// How long process_restart() waits for a terminated program to disappear
// before giving up. Termination is not observably instantaneous: pm returns
// once it has asked the kernel to kill the process, and pm:dmnt keeps
// resolving the program id until the process object is actually reaped.
#define RESTART_GRACE_NS   (3ULL * 1000000000ULL)
#define RESTART_POLL_NS    (50ULL * 1000000ULL)

static bool g_initialized;
static u64 g_last_pid;
static u64 g_last_program_id;

bool process_init(void) {
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        LOGF("process: pmshellInitialize failed rc=0x%x\n", rc);
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

bool process_status(uint64_t program_id, ProcessStatus *out) {
    if (!g_initialized || out == NULL)
        return false;

    // pm:dmnt accepts a single session and Atmosphere's own dmnt already holds
    // it, so it is not available here at all. Resolve the pid we were handed at
    // launch through pm:info instead: if it still maps to this program, the
    // process is alive.
    if (g_last_pid != 0 && g_last_program_id == program_id) {
        // __appInit closes the sm session, so no service can be opened at
        // request time without reopening it first. smInitialize is refcounted
        // and reconnects to the named port, so this is safe to do per call.
        Result irc = smInitialize();
        if (R_SUCCEEDED(irc)) {
            irc = pminfoInitialize();
            smExit();
        }
        if (R_FAILED(irc)) {
            LOGF("process: pminfoInitialize failed rc=0x%x\n", irc);
            out->running = false;
            out->pid = 0;
            return true;
        }
        u64 resolved = 0;
        Result rc = pminfoGetProgramId(&resolved, g_last_pid);
        pminfoExit();
        out->running = R_SUCCEEDED(rc) && resolved == program_id;
        out->pid = out->running ? g_last_pid : 0;
        if (!out->running)
            g_last_pid = 0;
        return true;
    }

    u64 pid = 0;
    Result rc = R_FAILED(pmdmntInitialize()) ? MAKERESULT(Module_Libnx, LibnxError_NotFound)
                                             : pmdmntGetProcessId(&pid, program_id);
    pmdmntExit();
    if (R_SUCCEEDED(rc)) {
        out->running = true;
        out->pid = pid;
    } else {
        // Any failure here means "no process with that program id". pm does not
        // distinguish "not running" from other lookup errors in a way worth
        // surfacing, and the query itself succeeded.
        out->running = false;
        out->pid = 0;
    }
    return true;
}

// boot2 launches SD sysmodules while the system pool still has room. Launching
// one later fails with svc::ResultLimitReached (0x10801) because that pool is
// fully committed by then, so borrow from the application pool for the launch.
// The boost is an absolute size, not an increment; 0 releases it.
#define SYSTEM_MEMORY_BOOST 0x4000000ull // 64 MiB

static bool g_boosted;

static void set_boost(u64 size) {
    Result rc = pmshellBoostSystemMemoryResourceLimit(size);
    LOGF("process: boost %llu -> rc=0x%x\n", (unsigned long long)size, rc);
    if (R_SUCCEEDED(rc))
        g_boosted = size != 0;
}

bool process_start(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    if (out_rc)
        *out_rc = 0;
    if (!g_initialized)
        return false;

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
        LOGF("process: launch %016llx failed rc=0x%x\n",
             (unsigned long long)program_id, rc);
        // Do not leave the application pool short, or our own input dead,
        // after a launch that never happened.
        if (g_boosted)
            set_boost(0);
        hiddbgInitialize();
        if (out_rc)
            *out_rc = rc;
        return false;
    }
    g_last_pid = pid;
    g_last_program_id = program_id;
    if (out_pid)
        *out_pid = pid;
    return true;
}

bool process_stop(uint64_t program_id, uint32_t *out_rc) {
    if (out_rc)
        *out_rc = 0;
    if (!g_initialized)
        return false;

    Result rc = pmshellTerminateProgram(program_id);
    if (R_FAILED(rc)) {
        LOGF("process: terminate %016llx failed rc=0x%x\n",
             (unsigned long long)program_id, rc);
        if (out_rc)
            *out_rc = rc;
        return false;
    }
    g_last_pid = 0;
    if (g_boosted)
        set_boost(0);
    hiddbgInitialize(); // take hid:dbg back now that the module has released it
    return true;
}

bool process_restart(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    if (out_rc)
        *out_rc = 0;
    if (!g_initialized)
        return false;

    ProcessStatus st;
    if (process_status(program_id, &st) && st.running) {
        uint32_t stop_rc = 0;
        if (!process_stop(program_id, &stop_rc)) {
            if (out_rc)
                *out_rc = stop_rc;
            return false;
        }

        // Wait for the process to actually be gone before relaunching, so we
        // never hand the caller a pid from a launch that raced the teardown.
        u64 waited = 0;
        while (waited < RESTART_GRACE_NS) {
            if (!process_status(program_id, &st) || !st.running)
                break;
            svcSleepThread(RESTART_POLL_NS);
            waited += RESTART_POLL_NS;
        }
        if (st.running) {
            LOGF("process: %016llx still running after stop\n",
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

bool process_status(uint64_t program_id, ProcessStatus *out) {
    if (out == NULL)
        return false;
    out->running = g_fake_running && g_fake_program_id == program_id;
    out->pid = out->running ? g_fake_pid : 0;
    return true;
}

bool process_start(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    if (out_rc)
        *out_rc = 0;
    if (g_fake_running && g_fake_program_id == program_id)
        return false; // already running
    g_fake_running = true;
    g_fake_program_id = program_id;
    g_fake_pid = 0x42;
    if (out_pid)
        *out_pid = g_fake_pid;
    return true;
}

bool process_stop(uint64_t program_id, uint32_t *out_rc) {
    if (out_rc)
        *out_rc = 0;
    if (!g_fake_running || g_fake_program_id != program_id)
        return false;
    g_fake_running = false;
    return true;
}

bool process_restart(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc) {
    process_stop(program_id, out_rc);
    return process_start(program_id, out_pid, out_rc);
}

#endif
