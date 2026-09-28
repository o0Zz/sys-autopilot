#include "features/files/files_http.h"
#include "core/http_server.h"
#include "core/log.h"
#include "core/request.h"
#include "features/files/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// Resolves the "path" query param. On failure sends a 400 and returns false.
static bool resolve_query_path(HttpRequest *req, char *out, size_t outsz) {
    char path[512];
    if (!http_query_get(req, "path", path, sizeof(path)) || path[0] == '\0') {
        http_send_error(req->fd, 400, "missing 'path' query parameter");
        return false;
    }
    const char *err = NULL;
    if (!files_resolve(path, out, outsz, &err)) {
        http_send_error(req->fd, 400, err);
        return false;
    }
    return true;
}

static const char *content_type_for(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";
    ext++;
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "log") == 0 ||
        strcasecmp(ext, "ini") == 0 || strcasecmp(ext, "cfg") == 0 ||
        strcasecmp(ext, "md") == 0)
        return "text/plain";
    if (strcasecmp(ext, "json") == 0)
        return "application/json";
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0)
        return "text/html";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, "png") == 0)
        return "image/png";
    return "application/octet-stream";
}

static void send_file(HttpRequest *req, const char *fspath, const struct stat *st) {
    long long offset = 0;
    long long length = -1;
    char val[32];
    if (http_query_get(req, "offset", val, sizeof(val)))
        offset = atoll(val);
    if (http_query_get(req, "length", val, sizeof(val)))
        length = atoll(val);

    files_clamp_range((long long)st->st_size, &offset, &length);

    char *buf = request_alloc(req, FILES_IO_BUF_SIZE);
    if (!buf) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }

    FILE *f = fopen(fspath, "rb");
    if (!f) {
        http_send_error(req->fd, 404, "file not found");
        return;
    }
    if (offset > 0 && fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        http_send_error(req->fd, 500, "seek failed");
        return;
    }

    http_send_header(req->fd, 200, content_type_for(fspath), (size_t)length);

    long long remaining = length;
    while (remaining > 0) {
        size_t chunk = remaining > FILES_IO_BUF_SIZE ? FILES_IO_BUF_SIZE : (size_t)remaining;
        size_t n = fread(buf, 1, chunk, f);
        if (n == 0)
            break;
        if (!http_write_all(req->fd, buf, n))
            break;
        remaining -= (long long)n;
    }
    fclose(f);
}

// GET /files?path=P: file download, or a JSON listing for a directory.
static void get_files(HttpRequest *req) {
    char fspath[768];
    if (!resolve_query_path(req, fspath, sizeof(fspath)))
        return;

    size_t rootlen = strlen(FILES_ROOT);

    // Trailing slash forces a directory interpretation.
    bool want_dir = files_trim_slash(fspath); // stat without trailing slash

    struct stat st;
    if (stat(fspath, &st) != 0) {
        http_send_error(req->fd, 404, "no such file or directory");
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        const char *err = NULL;
        size_t len = 0;
        char *json = files_build_listing(fspath, fspath + rootlen, &len, &err);
        if (!json) {
            http_send_error(req->fd, 500, err);
            return;
        }
        http_send_response(req->fd, 200, "application/json", json, len);
        free(json);
    } else if (want_dir) {
        http_send_error(req->fd, 400, "not a directory");
    } else {
        send_file(req, fspath, &st);
    }
}

// GET /files/hash?path=P
static void get_hash(HttpRequest *req) {
    char fspath[768];
    if (!resolve_query_path(req, fspath, sizeof(fspath)))
        return;

    void *buf = request_alloc(req, FILES_IO_BUF_SIZE);
    if (!buf) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }
    char hexbuf[65];
    long long size = 0;
    const char *err = NULL;
    if (!files_hash_sha256(fspath, buf, FILES_IO_BUF_SIZE, hexbuf, &size, &err)) {
        int code = strstr(err, "no such") ? 404
                 : strstr(err, "directory") ? 400
                 : 500;
        http_send_error(req->fd, code, err);
        return;
    }
    http_send_json(req->fd, 200,
                   "{\"path\":\"%s\",\"algorithm\":\"sha256\",\"hash\":\"%s\",\"size\":%lld}",
                   fspath + strlen(FILES_ROOT), hexbuf, size);
}

// PUT /files?path=P: streamed upload.
static void put_files(HttpRequest *req) {
    char fspath[768];
    if (!resolve_query_path(req, fspath, sizeof(fspath)))
        return;

    if (!req->has_content_length) {
        http_send_error(req->fd, 411, "Content-Length required");
        return;
    }

    char *buf = request_alloc(req, FILES_IO_BUF_SIZE);
    if (!buf) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }

    files_mkdirs_for(fspath);

    FILE *f = fopen(fspath, "wb");
    if (!f) {
        http_send_error(req->fd, 500, "failed to open file for writing");
        return;
    }

    size_t total = 0;
    bool write_err = false;
    while (total < req->content_length) {
        ssize_t n = http_read_body(req, buf, FILES_IO_BUF_SIZE);
        if (n <= 0)
            break;
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
            write_err = true;
            break;
        }
        total += (size_t)n;
    }
    fclose(f);

    if (write_err) {
        remove(fspath);
        http_send_error(req->fd, 507, "write failed (sd card full?)");
        return;
    }
    if (total != req->content_length) {
        remove(fspath);
        http_send_error(req->fd, 400, "incomplete upload");
        return;
    }

    LOGF("files: wrote %zu bytes to %s\n", total, fspath);
    http_send_json(req->fd, 201, "{\"written\":%zu,\"path\":\"%s\"}", total,
                   fspath + strlen(FILES_ROOT));
}

// DELETE /files?path=P: file or empty directory.
static void delete_files(HttpRequest *req) {
    char fspath[768];
    if (!resolve_query_path(req, fspath, sizeof(fspath)))
        return;

    const char *err = NULL;
    if (!files_delete_path(fspath, &err)) {
        http_send_error(req->fd, strstr(err, "no such") ? 404 : 500, err);
        return;
    }
    http_send_json(req->fd, 200, "{\"deleted\":\"%s\"}", fspath + strlen(FILES_ROOT));
}

// POST /files/move?path=P&to=Q
static void post_move(HttpRequest *req) {
    char src[768];
    if (!resolve_query_path(req, src, sizeof(src)))
        return;

    char to[512];
    if (!http_query_get(req, "to", to, sizeof(to)) || to[0] == '\0') {
        http_send_error(req->fd, 400, "missing 'to' query parameter");
        return;
    }
    char dst[768];
    const char *err = NULL;
    if (!files_resolve(to, dst, sizeof(dst), &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    if (!files_move_path(src, dst, &err)) {
        int code = strstr(err, "no such") ? 404 : strstr(err, "exists") ? 409 : 500;
        http_send_error(req->fd, code, err);
        return;
    }
    LOGF("files: moved %s -> %s\n", src, dst);
    http_send_json(req->fd, 200, "{\"moved\":\"%s\",\"to\":\"%s\"}",
                   src + strlen(FILES_ROOT), dst + strlen(FILES_ROOT));
}

void files_http_register(void) {
    http_server_register_route("GET",    "/files",      get_files);
    http_server_register_route("GET",    "/files/hash", get_hash);
    http_server_register_route("PUT",    "/files",      put_files);
    http_server_register_route("DELETE", "/files",      delete_files);
    http_server_register_route("POST",   "/files/move", post_move);
}
