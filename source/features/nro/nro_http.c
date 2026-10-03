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
    size_t args_len = 0;
    int arr = json_obj_get(doc, 0, "args");
    for (int i = 0; arr >= 0 && i < json_arr_len(doc, arr); i++) {
        char *dst = args + args_len;
        if (!json_get_string(doc, json_arr_get(doc, arr, i), dst, NRO_ARGS_MAX - args_len)) {
            http_send_error(req->fd, 400, "'args' must be strings, 1 KB in all");
            return;
        }
        args_len += strlen(dst) + 1;
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
