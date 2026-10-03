#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Atmosphere's crash and fatal reports on the SD card, newest first, so an
// agent can tell at a glance whether (and how) the thing it just launched
// died. Atmosphere writes them as
//   /atmosphere/crash_reports/<timestamp>_<program id>.log  (creport)
//   /atmosphere/fatal_reports/<timestamp>_<program id>.log  (fatal errors)
// where the timestamp is POSIX seconds, or the system tick when the clock was
// unavailable. A crash report may come with a .jpg screenshot of the same
// name.

#define CRASH_MAX_REPORTS 32
#define CRASH_DEFAULT_LIMIT 10

typedef struct {
    char path[80];        // SD path, e.g. /atmosphere/crash_reports/....log
    bool fatal;           // from fatal_reports
    uint64_t timestamp;
    uint64_t program_id;
    long long size;
    char result[32];      // e.g. "0x2A8 (2162-0001)", "" when unreadable
    char process[20];     // process name, "" when the report has none
    bool has_screenshot;
} CrashReport;

// Fills out[0..max) with the newest reports, newest first, and returns how
// many there are.
int crash_list(CrashReport *out, int max);

// "2026-10-03 12:00:01 UTC" for a POSIX timestamp, "tick <n>" otherwise.
void crash_format_time(uint64_t timestamp, char *out, size_t outsz);
