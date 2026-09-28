#include "features/process/process_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/process/process.h"

#include <string.h>

// Title ids are 16 hex digits, matching the form /titles reports them in. They
// are accepted from the query string (GET) or the JSON body (POST), and always
// reported back as strings: a u64 does not survive a JSON number in most
// clients.
static bool arg_title_id(HttpRequest *req, uint64_t *out) {
    if (!process_available()) {
        http_send_error(req->fd, 500, "process control unavailable");
        return false;
    }

    char raw[32] = {0};
    if (strcmp(req->method, "GET") == 0) {
        if (!http_query_get(req, "titleId", raw, sizeof(raw))) {
            http_send_error(req->fd, 400, "missing 'titleId' query parameter");
            return false;
        }
    } else {
        JsonDoc *doc = request_read_json(req);
        if (!doc)
            return false; // response already sent
        int t = json_obj_get(doc, 0, "titleId");
        if (t < 0 || !json_get_string(doc, t, raw, sizeof(raw))) {
            http_send_error(req->fd, 400, "missing string 'titleId'");
            return false;
        }
    }

    if (!process_parse_title_id(raw, out)) {
        http_send_error(req->fd, 400, "malformed 'titleId' (expected hex)");
        return false;
    }
    return true;
}

static void send_process_error(HttpRequest *req, const char *what, uint32_t rc) {
    http_send_json(req->fd, 500, "{\"ok\":false,\"error\":\"%s\",\"rc\":\"0x%08x\"}",
                   what, rc);
}

// GET /process?titleId=T
static void get_status(HttpRequest *req) {
    uint64_t tid;
    if (!arg_title_id(req, &tid))
        return;

    ProcessStatus st;
    process_status(tid, &st);
    if (st.running)
        http_send_json(req->fd, 200,
                       "{\"titleId\":\"%016llx\",\"running\":true,\"pid\":\"%llu\"}",
                       (unsigned long long)tid, (unsigned long long)st.pid);
    else
        http_send_json(req->fd, 200,
                       "{\"titleId\":\"%016llx\",\"running\":false}",
                       (unsigned long long)tid);
}

// POST /process/start {"titleId":"T"}
static void post_start(HttpRequest *req) {
    uint64_t tid;
    if (!arg_title_id(req, &tid))
        return;

    uint64_t pid = 0;
    uint32_t rc = 0;
    if (!process_start(tid, &pid, &rc)) {
        send_process_error(req, "launch failed", rc);
        return;
    }
    http_send_json(req->fd, 200,
                   "{\"ok\":true,\"titleId\":\"%016llx\",\"pid\":\"%llu\"}",
                   (unsigned long long)tid, (unsigned long long)pid);
}

// POST /process/stop {"titleId":"T"}
static void post_stop(HttpRequest *req) {
    uint64_t tid;
    if (!arg_title_id(req, &tid))
        return;

    uint32_t rc = 0;
    if (!process_stop(tid, &rc)) {
        send_process_error(req, "terminate failed", rc);
        return;
    }
    http_send_json(req->fd, 200, "{\"ok\":true,\"titleId\":\"%016llx\"}",
                   (unsigned long long)tid);
}

// POST /process/restart {"titleId":"T"}
static void post_restart(HttpRequest *req) {
    uint64_t tid;
    if (!arg_title_id(req, &tid))
        return;

    uint64_t pid = 0;
    uint32_t rc = 0;
    if (!process_restart(tid, &pid, &rc)) {
        send_process_error(req, "restart failed", rc);
        return;
    }
    http_send_json(req->fd, 200,
                   "{\"ok\":true,\"titleId\":\"%016llx\",\"pid\":\"%llu\"}",
                   (unsigned long long)tid, (unsigned long long)pid);
}

void process_http_register(void) {
    http_server_register_route("GET",  "/process",         get_status);
    http_server_register_route("POST", "/process/start",   post_start);
    http_server_register_route("POST", "/process/stop",    post_stop);
    http_server_register_route("POST", "/process/restart", post_restart);
}
