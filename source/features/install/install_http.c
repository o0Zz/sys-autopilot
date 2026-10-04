#include "features/install/install_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/install/install.h"

#include <strings.h>

// Adapts the HTTP request body into the installer's sequential read callback.
static long install_body_read(void *ctx, void *buf, size_t len) {
    return (long)http_read_body((HttpRequest *)ctx, buf, len);
}

// POST|PUT /install[?storage=sd|nand]: streamed NSP/XCI.
static void handle_install(HttpRequest *req) {
    if (!req->has_content_length) {
        http_send_error(req->fd, 411, "Content-Length required (stream the NSP/XCI with curl -T)");
        return;
    }

    InstallStorage storage = INSTALL_STORAGE_SD;
    char sval[8];
    if (http_query_get(req, "storage", sval, sizeof(sval)) && strcasecmp(sval, "nand") == 0)
        storage = INSTALL_STORAGE_NAND;

    void *work = request_alloc(req, install_work_size());
    if (!work) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }

    InstallResult res;
    install_stream(install_body_read, req, storage, work, &res);

    const char *esc = request_json_escape(req, res.message);
    if (res.ok) {
        http_send_json(req->fd, 200,
                       "{\"ok\":true,\"titleId\":\"%016llx\",\"version\":%u,\"message\":\"%s\"}",
                       (unsigned long long)res.title_id, res.version, esc);
    } else {
        http_send_json(req->fd, res.http_status,
                       "{\"ok\":false,\"error\":\"%s\"}", esc);
    }
}

void install_http_register(void) {
    http_server_register_route("POST", "/install", handle_install);
    http_server_register_route("PUT",  "/install", handle_install);
}
