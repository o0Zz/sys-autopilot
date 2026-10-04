#include "features/files/files.h"
#include "core/http_server.h"
#include "util/hex.h"
#include "util/json.h"
#include "util/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

bool files_resolve(const char *userpath, char *out, size_t outsz, const char **err) {
    if (userpath == NULL || userpath[0] == '\0') {
        *err = "missing path";
        return false;
    }
    if (userpath[0] != '/') {
        *err = "path must be absolute (start with /)";
        return false;
    }
    // Reject any ".." path segment.
    const char *p = userpath;
    while ((p = strstr(p, "..")) != NULL) {
        bool start_ok = (p == userpath) || p[-1] == '/';
        bool end_ok = p[2] == '\0' || p[2] == '/';
        if (start_ok && end_ok) {
            *err = "path traversal not allowed";
            return false;
        }
        p += 2;
    }
    if ((size_t)snprintf(out, outsz, FILES_ROOT "%s", userpath) >= outsz) {
        *err = "path too long";
        return false;
    }
    return true;
}

void files_mkdirs_for(const char *fspath) {
    char tmp[768];
    snprintf(tmp, sizeof(tmp), "%s", fspath);
    // Skip the root prefix ("sdmc:/").
    char *p = strchr(tmp, '/');
    if (!p)
        return;
    p++;
    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

// Appends to a heap-grown buffer, doubling as needed. Returns false on OOM.
static bool buf_append(char **buf, size_t *len, size_t *cap, const char *data, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t newcap = *cap ? *cap : 1024;
        while (*len + n + 1 > newcap)
            newcap *= 2;
        char *nb = realloc(*buf, newcap);
        if (!nb)
            return false;
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
    return true;
}

char *files_build_listing(const char *fspath, const char *userpath,
                          size_t *out_len, const char **err) {
    DIR *dir = opendir(fspath);
    if (!dir) {
        *err = "directory not found";
        return NULL;
    }

    char *json = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;

    char head[600];
    char escaped[520];
    json_escape(userpath, strlen(userpath), escaped, sizeof(escaped));
    int n = snprintf(head, sizeof(head), "{\"path\":\"%s\",\"entries\":[", escaped);
    ok = buf_append(&json, &len, &cap, head, (size_t)n);

    struct dirent *ent;
    bool first = true;
    while (ok && (ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full[1024];
        snprintf(full, sizeof(full), "%s%s%s", fspath,
                 fspath[strlen(fspath) - 1] == '/' ? "" : "/", ent->d_name);

        struct stat st;
        bool have_st = stat(full, &st) == 0;
        bool is_dir = have_st && S_ISDIR(st.st_mode);

        if (json_escape(ent->d_name, strlen(ent->d_name), escaped,
                        sizeof(escaped)) == (size_t)-1)
            continue;

        char entry[640];
        if (is_dir) {
            n = snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"type\":\"dir\"}",
                         first ? "" : ",", escaped);
        } else {
            n = snprintf(entry, sizeof(entry),
                         "%s{\"name\":\"%s\",\"type\":\"file\",\"size\":%lld,\"mtime\":%lld}",
                         first ? "" : ",", escaped,
                         have_st ? (long long)st.st_size : 0LL,
                         have_st ? (long long)st.st_mtime : 0LL);
        }
        ok = buf_append(&json, &len, &cap, entry, (size_t)n);
        first = false;
    }
    closedir(dir);

    if (ok)
        ok = buf_append(&json, &len, &cap, "]}", 2);

    if (!ok) {
        free(json);
        *err = "out of memory building listing";
        return NULL;
    }
    *out_len = len;
    return json;
}

bool files_delete_path(const char *fspath, const char **err) {
    struct stat st;
    if (stat(fspath, &st) != 0) {
        *err = "no such file or directory";
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        // Checked by hand: the console's rmdir() does not report ENOTEMPTY.
        DIR *d = opendir(fspath);
        bool empty = true;
        if (d) {
            struct dirent *e;
            while (empty && (e = readdir(d)) != NULL)
                empty = strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0;
            closedir(d);
        }
        if (!empty) {
            *err = "directory not empty";
            return false;
        }
        if (rmdir(fspath) != 0) {
            *err = "rmdir failed";
            return false;
        }
    } else if (remove(fspath) != 0) {
        *err = "delete failed";
        return false;
    }
    return true;
}

bool files_move_path(const char *src, const char *dst, const char **err) {
    struct stat st;
    if (stat(src, &st) != 0) {
        *err = "no such file or directory";
        return false;
    }
    if (strcmp(src, dst) == 0)
        return true;
    if (stat(dst, &st) == 0) {
        *err = "destination already exists";
        return false;
    }
    files_mkdirs_for(dst);
    if (rename(src, dst) != 0) {
        *err = "move failed";
        return false;
    }
    return true;
}

bool files_trim_slash(char *fspath) {
    size_t plen = strlen(fspath);
    if (plen <= strlen(FILES_ROOT) + 1 || fspath[plen - 1] != '/')
        return false;
    fspath[plen - 1] = '\0';
    return true;
}

void files_clamp_range(long long fsize, long long *offset, long long *length) {
    if (*offset < 0) {
        *offset = fsize + *offset;
        if (*offset < 0)
            *offset = 0;
    }
    if (*offset > fsize)
        *offset = fsize;
    long long avail = fsize - *offset;
    if (*length < 0 || *length > avail)
        *length = avail;
}

bool files_hash_sha256(const char *fspath, void *buf, size_t buf_size,
                       char out_hex[65], long long *out_size, const char **err) {
    struct stat st;
    if (stat(fspath, &st) != 0) {
        *err = "no such file or directory";
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        *err = "is a directory";
        return false;
    }

    FILE *f = fopen(fspath, "rb");
    if (!f) {
        *err = "open failed";
        return false;
    }

    Sha256Stream sha;
    sha256_stream_init(&sha);

    long long total = 0;
    size_t n;
    while ((n = fread(buf, 1, buf_size, f)) > 0) {
        sha256_stream_update(&sha, buf, n);
        total += (long long)n;
    }
    bool read_err = ferror(f) != 0;
    fclose(f);
    if (read_err) {
        *err = "read failed";
        return false;
    }

    uint8_t digest[32];
    sha256_stream_final(&sha, digest);

    hex_encode(digest, sizeof(digest), out_hex);

    *out_size = total;
    return true;
}

// --- waiting -----------------------------------------------------------------------

#define FILES_WAIT_POLL_MS 200

static bool mem_find(const char *hay, size_t hlen, const char *needle, size_t nlen) {
    if (nlen == 0)
        return true;
    for (size_t i = 0; i + nlen <= hlen; i++)
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0)
            return true;
    return false;
}

// Searches fspath from byte `from` to its end, overlapping the previous
// search by the needle's length so a match across the boundary is not lost.
static bool search_file(const char *fspath, long long from, const char *needle, size_t nlen,
                        char *buf, size_t bufsz) {
    FILE *f = fopen(fspath, "rb");
    if (!f)
        return false;
    long long off = from > (long long)(nlen - 1) ? from - (long long)(nlen - 1) : 0;
    bool found = false;
    if (fseek(f, (long)off, SEEK_SET) == 0) {
        size_t keep = 0;
        for (;;) {
            size_t n = fread(buf + keep, 1, bufsz - keep, f);
            if (n == 0)
                break;
            size_t total = keep + n;
            if (mem_find(buf, total, needle, nlen)) {
                found = true;
                break;
            }
            keep = nlen - 1 < total ? nlen - 1 : total;
            memmove(buf, buf + total - keep, keep);
        }
    }
    fclose(f);
    return found;
}

void files_wait(const char *fspath, const char *contains, bool new_only, int timeout_ms,
                char *buf, size_t bufsz, FilesWait *res) {
    memset(res, 0, sizeof(*res));
    if (timeout_ms < 0)
        timeout_ms = 0;
    if (timeout_ms > HTTP_MAX_WAIT_MS)
        timeout_ms = HTTP_MAX_WAIT_MS;
    size_t nlen = contains ? strlen(contains) : 0;

    // Everything before `scanned` has been searched (or does not count).
    long long scanned = 0;
    struct stat st;
    if (new_only && stat(fspath, &st) == 0)
        scanned = (long long)st.st_size;

    uint64_t start = http_server_now_ms();
    for (;;) {
        res->exists = stat(fspath, &st) == 0 && S_ISREG(st.st_mode);
        if (res->exists) {
            res->size = (long long)st.st_size;
            if (nlen == 0) {
                res->met = true;
            } else {
                if (res->size < scanned)
                    scanned = 0; // rewritten: search it all again
                if (res->size > scanned &&
                    search_file(fspath, scanned, contains, nlen, buf, bufsz))
                    res->met = true;
                scanned = res->size;
            }
        }
        res->elapsed_ms = (int)(http_server_now_ms() - start);
        if (res->met || res->elapsed_ms >= timeout_ms)
            return;
        int left = timeout_ms - res->elapsed_ms;
        if (!http_server_wait_ms(left < FILES_WAIT_POLL_MS ? left : FILES_WAIT_POLL_MS))
            return;
    }
}
