#include "features/crash/crash.h"
#include "features/files/files.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const struct {
    const char *dir;
    bool fatal;
} kDirs[] = {
    { "/atmosphere/crash_reports", false },
    { "/atmosphere/fatal_reports", true },
};

// "<11 digits>_<16 hex digits>.log"
static bool parse_name(const char *name, uint64_t *ts, uint64_t *pid) {
    size_t len = strlen(name);
    if (len != 11 + 1 + 16 + 4 || name[11] != '_' || strcmp(name + 28, ".log") != 0)
        return false;
    char *end = NULL;
    *ts = strtoull(name, &end, 10);
    if (end != name + 11)
        return false;
    *pid = strtoull(name + 12, &end, 16);
    return end == name + 28;
}

// Copies the value after "<key>" on its line (trimmed) into out.
static void header_value(const char *head, const char *key, char *out, size_t outsz) {
    const char *p = strstr(head, key);
    if (!p)
        return;
    p += strlen(key);
    while (*p == ' ' || *p == '\t')
        p++;
    size_t n = strcspn(p, "\r\n");
    if (n >= outsz)
        n = outsz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static void read_header(CrashReport *r) {
    char fspath[128];
    snprintf(fspath, sizeof(fspath), FILES_ROOT "%s", r->path);
    FILE *f = fopen(fspath, "rb");
    if (!f)
        return;
    char head[1024];
    size_t n = fread(head, 1, sizeof(head) - 1, f);
    fclose(f);
    head[n] = '\0';
    header_value(head, "Result:", r->result, sizeof(r->result));
    header_value(head, "Process Name:", r->process, sizeof(r->process));

    // A crash report's screenshot: same name, .jpg.
    size_t plen = strlen(fspath);
    struct stat st;
    memcpy(fspath + plen - 4, ".jpg", 4);
    r->has_screenshot = stat(fspath, &st) == 0;
}

int crash_list(CrashReport *out, int max) {
    int count = 0;
    for (size_t d = 0; d < sizeof(kDirs) / sizeof(kDirs[0]); d++) {
        char fsdir[96];
        snprintf(fsdir, sizeof(fsdir), FILES_ROOT "%s", kDirs[d].dir);
        DIR *dir = opendir(fsdir);
        if (!dir)
            continue;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            uint64_t ts, pid;
            if (!parse_name(ent->d_name, &ts, &pid))
                continue;
            // Keep the `max` newest, sorted newest first.
            int at = count;
            while (at > 0 && out[at - 1].timestamp < ts)
                at--;
            if (at >= max)
                continue;
            int last = count < max ? count : max - 1;
            memmove(&out[at + 1], &out[at], (size_t)(last - at) * sizeof(*out));
            if (count < max)
                count++;
            CrashReport *r = &out[at];
            memset(r, 0, sizeof(*r));
            snprintf(r->path, sizeof(r->path), "%s/%.32s", kDirs[d].dir, ent->d_name);
            r->fatal = kDirs[d].fatal;
            r->timestamp = ts;
            r->program_id = pid;
        }
        closedir(dir);
    }
    // Only the reports kept are opened.
    for (int i = 0; i < count; i++) {
        char fspath[128];
        struct stat st;
        snprintf(fspath, sizeof(fspath), FILES_ROOT "%s", out[i].path);
        if (stat(fspath, &st) == 0)
            out[i].size = (long long)st.st_size;
        read_header(&out[i]);
    }
    return count;
}

void crash_format_time(uint64_t timestamp, char *out, size_t outsz) {
    // POSIX seconds between 2017 and 2100; anything else is a tick count.
    if (timestamp < 1483228800ULL || timestamp > 4102444800ULL) {
        snprintf(out, outsz, "tick %llu", (unsigned long long)timestamp);
        return;
    }
    // By hand, not strftime: strftime drags newlib's timezone and scanf code
    // into the binary.
    time_t t = (time_t)timestamp;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    snprintf(out, outsz, "%04d-%02d-%02d %02d:%02d:%02d UTC", tmv.tm_year + 1900,
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}
