#include "core/log.h"

#include <stdio.h>
#include <time.h>

// newlib's integer-only printf variants: log lines never print floats, and
// the float-capable vsnprintf costs ~15K of code.
#ifdef __SWITCH__
#define LOG_VSNPRINTF vsniprintf
#define LOG_SNPRINTF  sniprintf
#else
#define LOG_VSNPRINTF vsnprintf
#define LOG_SNPRINTF  snprintf
#endif

static const char kLevelChar[] = {'D', 'I', 'W', 'E'};

size_t log_format_line(char *out, size_t size, LogLevel level, const char *module,
                       const struct tm *tm, int ms, const char *fmt, va_list ap) {
    if (size < 2)
        return 0;
    // Room for the trailing newline: the line always ends with one, even when
    // the message is cut.
    size_t room = size - 1;
    char lvl = (unsigned)level < sizeof(kLevelChar) ? kLevelChar[level] : '?';
    int n = LOG_SNPRINTF(out, room, "|%c|%04d-%02d-%02d %02d:%02d:%02d.%03d|%-*.*s| ",
                         lvl, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec, ms,
                         LOG_MODULE_WIDTH, LOG_MODULE_WIDTH, module ? module : "");
    size_t len = n < 0 ? 0 : (size_t)n < room ? (size_t)n : room - 1;
    if (len < room - 1) {
        // vsnprintf returns the length it would have written, not what it
        // wrote: clamp to what is actually in the buffer.
        n = LOG_VSNPRINTF(out + len, room - len, fmt, ap);
        if (n > 0)
            len += (size_t)n < room - len ? (size_t)n : room - len - 1;
    }
    out[len++] = '\n';
    out[len] = '\0';
    return len;
}

#ifdef LOG_TO_FILE

#include <sys/stat.h>

static LogLevel g_log_level = LOG_LEVEL_OFF;
static bool g_log_suspended;

#ifdef __SWITCH__
#include <switch.h>

// Local wall-clock time is extrapolated from a system-tick anchor: at most two
// time-service calls a minute instead of two per line, and milliseconds that
// advance steadily between lines. Without the time service the anchor stays
// at boot, and the log shows 1970-01-01 plus the uptime.
static u64 g_anchor_tick;     // system tick at g_anchor_local
static s64 g_anchor_local;    // local seconds (UTC + zone offset) at the anchor
static u64 g_checked_tick;
static bool g_anchored;

static bool wall_local_now(s64 *out) {
    u64 utc;
    if (R_FAILED(timeGetCurrentTime(TimeType_Default, &utc)))
        return false;
    TimeCalendarTime cal;
    TimeCalendarAdditionalInfo info;
    s64 offset = 0;
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(utc, &cal, &info)))
        offset = info.offset;
    *out = (s64)utc + offset;
    return true;
}

static void now_local(time_t *sec, int *ms) {
    u64 tick = armGetSystemTick();
    if (!g_anchored || armTicksToNs(tick - g_checked_tick) >= 60ULL * 1000000000ULL) {
        g_checked_tick = tick;
        g_anchored = true;
        s64 wall;
        if (wall_local_now(&wall)) {
            s64 est = g_anchor_local +
                      (s64)(armTicksToNs(tick - g_anchor_tick) / 1000000000ULL);
            // Re-anchor only when the clock moved (set by the user, network
            // sync, zone change): the anchor is up to a second behind, so
            // re-anchoring every check would make time step back and forth.
            if (est - wall > 1 || wall - est > 1) {
                g_anchor_tick = tick;
                g_anchor_local = wall;
            }
        }
    }
    u64 ns = armTicksToNs(tick - g_anchor_tick);
    *sec = (time_t)(g_anchor_local + (s64)(ns / 1000000000ULL));
    *ms = (int)((ns / 1000000ULL) % 1000);
}

static void now_tm(struct tm *tm, int *ms) {
    time_t sec;
    now_local(&sec, ms);
    // Already local: split it without applying a zone again.
    gmtime_r(&sec, tm);
}
#else
static void now_tm(struct tm *tm, int *ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, tm);
    *ms = (int)(ts.tv_nsec / 1000000);
}
#endif

static void rotate(void) {
    remove(LOG_OLD_FILE_PATH);
    rename(LOG_FILE_PATH, LOG_OLD_FILE_PATH);
}

void log_set_level(LogLevel level) {
    g_log_level = level;
    if (level == LOG_LEVEL_OFF || g_log_suspended)
        return;
    struct stat st;
    if (stat(LOG_FILE_PATH, &st) == 0 && st.st_size >= LOG_FILE_SIZE_MAX)
        rotate();
}

void log_set_suspended(bool suspended) {
    g_log_suspended = suspended;
}

// Best-effort append to the SD-card log file. Opened per line (the server is
// single-threaded and not log-heavy) so output is flushed promptly, survives a
// crash, and never holds a handle that blocks readers of the file (the
// explorer's live tail, GET /files). Failures are ignored.
void log_write(LogLevel level, const char *module, const char *fmt, ...) {
    // Never touch the filesystem while suspended (PSC sleep window): fsp-srv
    // I/O there hangs the wake.
    if (level < g_log_level || g_log_level == LOG_LEVEL_OFF || g_log_suspended)
        return;

    char line[LOG_LINE_MAX];
    struct tm tm;
    int ms;
    now_tm(&tm, &ms);
    va_list ap;
    va_start(ap, fmt);
    size_t len = log_format_line(line, sizeof(line), level, module, &tm, ms, fmt, ap);
    va_end(ap);

    FILE *f = fopen(LOG_FILE_PATH, "ab");
    if (!f)
        return;
    fwrite(line, 1, len, f);
    long size = ftell(f);
    fclose(f);
    if (size >= LOG_FILE_SIZE_MAX)
        rotate();
}

#endif
