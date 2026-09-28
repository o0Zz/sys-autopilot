#include "features/titles/titles_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/titles/titles.h"
#include "util/json.h"

#include <stdio.h>
#include <string.h>

// Maps an NcmStorageId to the short label used in the JSON response.
static const char *storage_label(uint8_t storage_id) {
    switch (storage_id) {
        case 5: return "sd";
        case 4: return "nand";
        case 2: return "gamecard";
        default: return "other";
    }
}

// GET /titles
static void get_titles(HttpRequest *req) {
    // Each JSON entry is well under 256 bytes; TITLES_MAX entries fit.
    enum { BODY_SIZE = TITLES_MAX * 256 + 32 };
    TitleInfo *titles = request_alloc(req, sizeof(*titles) * TITLES_MAX);
    void *work = request_alloc(req, titles_work_size());
    char *body = request_alloc(req, BODY_SIZE);
    if (!titles || !work || !body) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }

    int count = 0;
    titles_list(titles, TITLES_MAX, &count, work);

    size_t pos = (size_t)snprintf(body, BODY_SIZE, "{\"titles\":[");
    for (int i = 0; i < count && pos < BODY_SIZE - 256; i++) {
        char name[160];
        json_escape(titles[i].name, strlen(titles[i].name), name, sizeof(name));
        pos += (size_t)snprintf(body + pos, BODY_SIZE - pos,
            "%s{\"titleId\":\"%016llx\",\"version\":%u,\"storage\":\"%s\",\"name\":\"%s\"}",
            i ? "," : "", (unsigned long long)titles[i].title_id,
            titles[i].version, storage_label(titles[i].storage_id), name);
    }
    pos += (size_t)snprintf(body + pos, BODY_SIZE - pos, "]}");
    http_send_response(req->fd, 200, "application/json", body, pos);
}

void titles_http_register(void) {
    http_server_register_route("GET", "/titles", get_titles);
}
