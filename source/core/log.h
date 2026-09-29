#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

struct tm;

// Leveled logging to a file on the SD card. LOG_TO_FILE is always defined in
// the sysmodule build, but the sink writes only at or above the level set at
// runtime via log_set_level() (driven by the `log` key in config.ini), since a
// sysmodule has no stdout. Without LOG_TO_FILE (host tests), the LOG* macros
// are no-ops.
//
// Every line is one fixed-layout record, so the file reads as columns:
//
//   |I|2026-09-29 14:03:12.345|server  | listening on port 4150
//
// level (D/I/W/E), local date and time with milliseconds, module padded to
// LOG_MODULE_WIDTH, message. Messages carry no trailing newline.
//
// File logging is best-effort: a failed open/write is silently ignored so it
// never affects server behavior. When the file reaches LOG_FILE_SIZE_MAX it is
// renamed to LOG_OLD_FILE_PATH (replacing the previous one) and a new file is
// started, so the log never takes more than twice that on the SD card.
//
// IMPORTANT: the file sink performs SD-card (fsp-srv) I/O. No filesystem I/O
// may happen during the PSC sleep transition (between the sleep notification
// and acknowledgement) or the console hangs on wake. log_set_suspended(true)
// hard-blocks all writes for that window, regardless of the level.

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_OFF,
} LogLevel;

#define LOG_MODULE_WIDTH 8
#define LOG_LINE_MAX 512

#ifndef LOG_FILE_PATH
#define LOG_FILE_PATH "sdmc:/config/sys-autopilot/log.txt"
#endif
#ifndef LOG_OLD_FILE_PATH
#define LOG_OLD_FILE_PATH "sdmc:/config/sys-autopilot/log.old.txt"
#endif
#ifndef LOG_FILE_SIZE_MAX
#define LOG_FILE_SIZE_MAX (256 * 1024)
#endif

// Formats one record, newline included, into out (no I/O). Returns its length;
// an over-long message is truncated but the line still ends with '\n'. `ms` is
// the millisecond part of the time in `tm`.
size_t log_format_line(char *out, size_t size, LogLevel level, const char *module,
                       const struct tm *tm, int ms, const char *fmt, va_list ap);

#if defined(LOG_TO_FILE)
// Minimum level written to the file. LOG_LEVEL_OFF (the default until this is
// called) disables the sink. Enabling it rotates an oversized file first.
void log_set_level(LogLevel level);
// Temporarily block ALL file writes (used across the sleep/wake window so no
// SD-card I/O occurs while the system is suspending). Independent of the
// level set by log_set_level().
void log_set_suspended(bool suspended);
void log_write(LogLevel level, const char *module, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
#define LOGD(module, ...) log_write(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define LOGI(module, ...) log_write(LOG_LEVEL_INFO, module, __VA_ARGS__)
#define LOGW(module, ...) log_write(LOG_LEVEL_WARN, module, __VA_ARGS__)
#define LOGE(module, ...) log_write(LOG_LEVEL_ERROR, module, __VA_ARGS__)

#else
#define LOGD(module, ...) ((void)0)
#define LOGI(module, ...) ((void)0)
#define LOGW(module, ...) ((void)0)
#define LOGE(module, ...) ((void)0)
static inline void log_set_level(LogLevel l) { (void)l; }
static inline void log_set_suspended(bool s) { (void)s; }
#endif
