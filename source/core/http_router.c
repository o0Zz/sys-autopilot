// Route table, authentication policy and dispatch: the part of the HTTP
// server with no socket loop, so the host tests can drive it directly.
#include "core/http_server.h"
#include "core/log.h"
#include "core/request.h"

#include <string.h>

typedef struct {
    const char *method;
    const char *path;
    HttpHandler handler;
    bool prefix;
} Route;

#define MAX_PUBLIC_PREFIXES 4

static Route g_routes[HTTP_MAX_ROUTES];
static int g_route_count;

static const char *g_public_prefixes[MAX_PUBLIC_PREFIXES];
static int g_public_count;

static HttpTokenValidator g_token_validator;
static const char *g_resource_metadata_path;

static bool add_route(const char *method, const char *path, HttpHandler handler,
                      bool prefix) {
    if (g_route_count >= HTTP_MAX_ROUTES) {
        LOGF("http: route table full, dropping %s %s\n", method, path);
        return false;
    }
    g_routes[g_route_count++] = (Route){ method, path, handler, prefix };
    return true;
}

bool http_server_register_route(const char *method, const char *path, HttpHandler handler) {
    return add_route(method, path, handler, false);
}

bool http_server_register_prefix(const char *method, const char *prefix, HttpHandler handler) {
    return add_route(method, prefix, handler, true);
}

bool http_server_add_public_prefix(const char *prefix) {
    if (g_public_count >= MAX_PUBLIC_PREFIXES) {
        LOGF("http: public prefix table full, dropping %s\n", prefix);
        return false;
    }
    g_public_prefixes[g_public_count++] = prefix;
    return true;
}

void http_server_set_token_validator(HttpTokenValidator validator,
                                     const char *resource_metadata_path) {
    g_token_validator = validator;
    g_resource_metadata_path = resource_metadata_path;
}

// CORS preflights carry no Authorization header, and public prefixes are how
// a client obtains credentials in the first place.
static bool path_is_public(const HttpRequest *req) {
    if (strcmp(req->method, "OPTIONS") == 0)
        return true;
    for (int i = 0; i < g_public_count; i++) {
        if (strncmp(req->path, g_public_prefixes[i], strlen(g_public_prefixes[i])) == 0)
            return true;
    }
    return false;
}

static bool authorized(const Config *cfg, const HttpRequest *req, bool *out_basic_cfg) {
    bool basic_cfg = cfg->username[0] != '\0' && cfg->password[0] != '\0';
    *out_basic_cfg = basic_cfg;
    if (basic_cfg && http_check_basic_auth(req, cfg->username, cfg->password))
        return true;

    char bearer[160];
    if (!http_get_bearer(req, bearer, sizeof(bearer)))
        return false;
    if (cfg->token[0] != '\0' && http_secure_streq(bearer, cfg->token))
        return true;
    return g_token_validator && g_token_validator(bearer);
}

// CORS preflight: browsers send OPTIONS with no Authorization header before
// cross-origin requests (e.g. web-based MCP clients).
static void handle_options(HttpRequest *req) {
    static const char hdr[] =
        "HTTP/1.1 204 No Content\r\n"
        "Server: sys-autopilot\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type, Mcp-Protocol-Version\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: close\r\n"
        "\r\n";
    http_write_all(req->fd, hdr, sizeof(hdr) - 1);
}

static bool route_matches(const Route *r, const char *path) {
    return r->prefix ? strncmp(path, r->path, strlen(r->path)) == 0
                     : strcmp(path, r->path) == 0;
}

static void route(HttpRequest *req) {
    if (strcmp(req->method, "OPTIONS") == 0) {
        handle_options(req);
        return;
    }

    bool path_found = false;
    for (int i = 0; i < g_route_count; i++) {
        if (!route_matches(&g_routes[i], req->path))
            continue;
        path_found = true;
        if (strcmp(req->method, g_routes[i].method) == 0) {
            g_routes[i].handler(req);
            return;
        }
    }
    if (path_found)
        http_send_error(req->fd, 405, "method not allowed");
    else
        http_send_error(req->fd, 404, "not found");
}

void http_server_dispatch(const Config *cfg, HttpRequest *req) {
    bool basic_cfg = false;
    if (config_auth_enabled(cfg) && !path_is_public(req) &&
        !authorized(cfg, req, &basic_cfg)) {
        http_send_unauthorized(req, basic_cfg, true, g_resource_metadata_path);
    } else {
        route(req);
    }
    // The response is out: everything the handler took from request memory
    // is released here, and only here.
    request_release(req);
}
