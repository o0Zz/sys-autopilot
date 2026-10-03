#include "features/crash/crash_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/crash/crash.h"

#include <stdio.h>
#include <stdlib.h>

// GET /crash-reports[?limit=N]
static void get_reports(HttpRequest *req) {
    int limit = CRASH_DEFAULT_LIMIT;
    char val[16];
    if (http_query_get(req, "limit", val, sizeof(val)))
        limit = atoi(val);
    if (limit < 1 || limit > CRASH_MAX_REPORTS) {
        http_send_error(req->fd, 400, "invalid 'limit' (1-32)");
        return;
    }

    enum { ENTRY_SIZE = 640, BODY_SIZE = CRASH_MAX_REPORTS * ENTRY_SIZE + 32 };
    CrashReport *reports = request_alloc(req, sizeof(*reports) * CRASH_MAX_REPORTS);
    char *body = request_alloc(req, BODY_SIZE);
    if (!reports || !body) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }
    int n = crash_list(reports, limit);

    size_t pos = (size_t)snprintf(body, BODY_SIZE, "{\"reports\":[");
    for (int i = 0; i < n; i++) {
        const CrashReport *r = &reports[i];
        char when[40];
        crash_format_time(r->timestamp, when, sizeof(when));
        pos += (size_t)snprintf(
            body + pos, BODY_SIZE - pos,
            "%s{\"path\":\"%s\",\"kind\":\"%s\",\"time\":\"%s\",\"timestamp\":\"%llu\","
            "\"titleId\":\"%016llx\",\"processName\":\"%s\",\"result\":\"%s\",\"size\":%lld,"
            "\"screenshot\":%s}",
            i ? "," : "", r->path, r->fatal ? "fatal" : "crash", when,
            (unsigned long long)r->timestamp, (unsigned long long)r->program_id,
            request_json_escape(req, r->process), request_json_escape(req, r->result), r->size,
            r->has_screenshot ? "true" : "false");
    }
    pos += (size_t)snprintf(body + pos, BODY_SIZE - pos, "]}");
    http_send_response(req->fd, 200, "application/json", body, pos);
}

void crash_http_register(void) {
    http_server_register_route("GET", "/crash-reports", get_reports);
}
