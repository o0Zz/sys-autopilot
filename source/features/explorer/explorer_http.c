#include "features/explorer/explorer_http.h"
#include "core/http_server.h"
#include "features/explorer/explorer_html.h"

static void get_page(HttpRequest *req) {
    http_send_response(req->fd, 200, "text/html", kExplorerHtml, sizeof(kExplorerHtml) - 1);
}

static void get_root(HttpRequest *req) {
    http_send_redirect(req->fd, "/explorer");
}

void explorer_http_register(void) {
    http_server_register_route("GET", "/",         get_root);
    http_server_register_route("GET", "/explorer", get_page);
}
