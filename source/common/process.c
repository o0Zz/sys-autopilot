#include "process.h"
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

bool process_init(void) {
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        LOGF("process: pmshellInitialize failed rc=0x%x\n", rc);
        return false;
    }
    rc = pmdmntInitialize();
    if (R_FAILED(rc)) {
        LOGF("process: pmdmntInitialize failed rc=0x%x\n", rc);
        pmshellExit();
        return false;
    }
    g_initialized = true;
    return true;
}

void process_exit(void) {
    if (!g_initialized)
        return;
    pmdmntExit();
    pmshellExit();
    g_initialized = false;
}

bool process_available(void) {
    return g_initialized;
}

bool process_status(uint64_t program_id, ProcessStatus *out) {
    if (!g_initialized || out == NULL)
        return false;

    u64 pid = 0;
    Result rc = pmdmntGetProcessId(&pid, program_id);
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

    u64 pid = 0;
    Result rc = pmshellLaunchProgram(0, &loc, &pid);
    if (R_FAILED(rc)) {
        LOGF("process: launch %016llx failed rc=0x%x\n",
             (unsigned long long)program_id, rc);
        if (out_rc)
            *out_rc = rc;
        return false;
    }
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
