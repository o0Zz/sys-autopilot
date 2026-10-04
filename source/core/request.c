#include "core/request.h"

#include <string.h>

// Largest JSON body request_read_json() accepts.
#define REQUEST_JSON_MAX 16384

static unsigned char g_request_memory[REQUEST_MEMORY_SIZE] __attribute__((aligned(0x1000)));

void *request_alloc(HttpRequest *req, size_t size) {
    size_t start = (req->mem_used + 15) & ~(size_t)15;
    if (start > REQUEST_MEMORY_SIZE || size > REQUEST_MEMORY_SIZE - start)
        return NULL;
    req->mem_used = start + size;
    return g_request_memory + start;
}

size_t request_mark(const HttpRequest *req) {
    return req->mem_used;
}

void request_rewind(HttpRequest *req, size_t mark) {
    if (mark < req->mem_used)
        req->mem_used = mark;
}

void request_release(HttpRequest *req) {
    req->mem_used = 0;
}

const char *request_json_escape(HttpRequest *req, const char *s) {
    size_t len = strlen(s);
    size_t esc_len = json_escaped_len(s, len);
    char *out = request_alloc(req, esc_len + 1);
    if (!out)
        return "";
    json_escape(s, len, out, esc_len + 1);
    return out;
}

JsonDoc *request_read_json(HttpRequest *req) {
    if (req->has_content_length && req->content_length > REQUEST_JSON_MAX) {
        http_send_error(req->fd, 413, "request body too large");
        return NULL;
    }
    char *body = request_alloc(req, REQUEST_JSON_MAX + 1);
    JsonDoc *doc = request_alloc(req, sizeof(*doc));
    if (!body || !doc) {
        http_send_error(req->fd, 500, "out of request memory");
        return NULL;
    }

    size_t total = 0;
    ssize_t n;
    while (total < REQUEST_JSON_MAX &&
           (n = http_read_body(req, body + total, REQUEST_JSON_MAX - total)) > 0)
        total += (size_t)n;

    if (total == 0) {
        strcpy(body, "{}");
        total = 2;
    }
    if (json_parse(doc, body, total) != 0 || doc->ntok < 1 ||
        doc->tok[0].type != JSMN_OBJECT) {
        http_send_error(req->fd, 400, "request body must be a JSON object");
        return NULL;
    }
    return doc;
}
