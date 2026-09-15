#pragma once

#include <stdbool.h>
#include <stdint.h>

// Process control for sysmodules and applications, by program (title) id.
//
// This is what lets an agent iterate on a sysmodule without rebooting: stop it,
// overwrite its exefs.nsp through /files, and start it again. Programs under
// /atmosphere/contents are launched with storage id None, which is what
// Atmosphere's own boot2 uses for them.
//
// Note that a program does NOT need a flags/boot2.flag to be launched here;
// that flag only governs whether boot2 starts it automatically. Leaving it off
// and starting the program through this API is the safer arrangement while
// developing, because a build that crashes on startup can no longer take the
// console down with it before anything is reachable.

typedef struct {
    bool     running;
    uint64_t pid; // only meaningful when running
} ProcessStatus;

// Opens the pm:shell and pm:dmnt sessions. Must be called while an sm session
// is open (i.e. from __appInit for the sysmodule). Returns false when pm is
// unavailable, e.g. in the dev .nro build; process_available() then stays false
// and every call below fails cleanly.
bool process_init(void);
void process_exit(void);
bool process_available(void);

// Liveness. Returns true on success and fills *out; a program that is simply
// not running is a successful query with out->running == false.
bool process_status(uint64_t program_id, ProcessStatus *out);

// Launches the program. *out_pid receives the new process id. On failure the
// Horizon result is stored in *out_rc (0 when the failure was not a Result).
bool process_start(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc);

// Terminates the program. Terminating one that is not running is a failure,
// reported through *out_rc.
bool process_stop(uint64_t program_id, uint32_t *out_rc);

// Stop (if running), wait for it to actually disappear, then start. Returns
// false and sets *out_rc if the program is still running after the grace
// period, so a caller never races a half-dead process.
bool process_restart(uint64_t program_id, uint64_t *out_pid, uint32_t *out_rc);
