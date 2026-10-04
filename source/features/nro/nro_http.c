#include "features/nro/nro_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/nro/nro.h"

#include <string.h>

// POST /nro/launch {"path":"/switch/app.nro","args":["--test"]}
static void post_launch(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    char path[512];
    int t = json_obj_get(doc, 0, "path");
    if (t < 0 || !json_get_string(doc, t, path, sizeof(path))) {
        http_send_error(req->fd, 400, "missing string 'path'");
        return;
    }
    char *args = request_alloc(req, NRO_ARGS_MAX);
    char *buf = request_alloc(req, NRO_BUF_SIZE);
    if (!args || !buf) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }
    size_t args_len;
    const char *arg_err;
    if (!nro_args_from_json(doc, 0, args, &args_len, &arg_err)) {
        http_send_error(req->fd, 400, arg_err);
        return;
    }

    char err[160];
    if (!nro_launch(path, args, args_len, buf, err, sizeof(err))) {
        http_send_error(req->fd, strstr(err, "not listening") ? 503 : 400, err);
        return;
    }
    http_send_json(req->fd, 200, "{\"ok\":true}");
}

void nro_http_register(void) {
    http_server_register_route("POST", "/nro/launch", post_launch);
}
