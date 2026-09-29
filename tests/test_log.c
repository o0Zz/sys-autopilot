// Host unit tests for the leveled file logger (log.c).
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "core/log.h"

static size_t fmt_line(char *out, size_t size, LogLevel level, const char *module,
                       const struct tm *tm, int ms, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t n = log_format_line(out, size, level, module, tm, ms, fmt, ap);
    va_end(ap);
    return n;
}

static long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static void read_file(const char *path, char *out, size_t size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    fclose(f);
}

static void test_format(void) {
    struct tm tm = {0};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 8;
    tm.tm_mday = 29;
    tm.tm_hour = 14;
    tm.tm_min = 3;
    tm.tm_sec = 7;

    char line[LOG_LINE_MAX];
    size_t n = fmt_line(line, sizeof(line), LOG_LEVEL_INFO, "server", &tm, 45,
                        "listening on port %d", 4150);
    const char *want = "|I|2026-09-29 14:03:07.045|server  | listening on port 4150\n";
    assert(strcmp(line, want) == 0);
    assert(n == strlen(want));

    // Module names are padded and cut to one width, so columns line up.
    fmt_line(line, sizeof(line), LOG_LEVEL_ERROR, "verylongmodule", &tm, 999, "x");
    assert(strcmp(line, "|E|2026-09-29 14:03:07.999|verylong| x\n") == 0);
    fmt_line(line, sizeof(line), LOG_LEVEL_DEBUG, "", &tm, 0, "x");
    assert(strncmp(line, "|D|", 3) == 0);
    assert(strstr(line, "|        | x\n"));

    // An over-long message is cut but the line still ends with a newline.
    char small[40];
    n = fmt_line(small, sizeof(small), LOG_LEVEL_WARN, "http", &tm, 0,
                 "%s", "a message far too long for the buffer");
    assert(n == sizeof(small) - 1);
    assert(small[n - 1] == '\n' && small[n] == '\0');
    assert(strncmp(small, "|W|", 3) == 0);

    // A buffer smaller than the header still yields a terminated line.
    n = fmt_line(small, 10, LOG_LEVEL_INFO, "http", &tm, 0, "hello");
    assert(n == 9 && small[8] == '\n' && small[9] == '\0');
}

static void test_levels_and_suspend(void) {
    remove(LOG_FILE_PATH);
    remove(LOG_OLD_FILE_PATH);

    // Off until a level is set.
    LOGE("test", "dropped");
    assert(file_size(LOG_FILE_PATH) < 0);

    log_set_level(LOG_LEVEL_WARN);
    LOGD("test", "dropped");
    LOGI("test", "dropped");
    assert(file_size(LOG_FILE_PATH) < 0);
    LOGW("test", "kept %d", 1);
    LOGE("test", "kept %d", 2);

    log_set_suspended(true);
    LOGE("test", "dropped while asleep");
    log_set_suspended(false);

    char buf[1024];
    read_file(LOG_FILE_PATH, buf, sizeof(buf));
    assert(!strstr(buf, "dropped"));
    const char *w = strstr(buf, "|W|");
    const char *e = strstr(buf, "|E|");
    assert(w && e && w < e);
    assert(strstr(w, "|test    | kept 1\n"));
    assert(strstr(e, "|test    | kept 2\n"));

    log_set_level(LOG_LEVEL_OFF);
    LOGE("test", "dropped");
    read_file(LOG_FILE_PATH, buf, sizeof(buf));
    assert(!strstr(buf, "dropped"));
}

static void test_rotation(void) {
    remove(LOG_FILE_PATH);
    remove(LOG_OLD_FILE_PATH);
    log_set_level(LOG_LEVEL_DEBUG);

    // Each line is ~70 bytes: 40 lines cross the limit at least once.
    for (int i = 0; i < 40; i++)
        LOGI("test", "line %02d padding padding padding", i);
    assert(file_size(LOG_OLD_FILE_PATH) >= LOG_FILE_SIZE_MAX);
    assert(file_size(LOG_FILE_PATH) < LOG_FILE_SIZE_MAX);

    char buf[4096];
    read_file(LOG_FILE_PATH, buf, sizeof(buf));
    assert(strstr(buf, "line 39"));

    // Enabling the sink rotates a file left oversized by an earlier run.
    FILE *f = fopen(LOG_FILE_PATH, "ab");
    assert(f);
    for (int i = 0; i < LOG_FILE_SIZE_MAX; i++)
        fputc('x', f);
    fclose(f);
    log_set_level(LOG_LEVEL_INFO);
    assert(file_size(LOG_FILE_PATH) < 0);
    assert(file_size(LOG_OLD_FILE_PATH) >= LOG_FILE_SIZE_MAX);

    log_set_level(LOG_LEVEL_OFF);
    remove(LOG_FILE_PATH);
    remove(LOG_OLD_FILE_PATH);
}

int main(void) {
    test_format();
    test_levels_and_suspend();
    test_rotation();
    printf("test_log: all passed\n");
    return 0;
}
