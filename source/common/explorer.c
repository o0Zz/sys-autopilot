#include "explorer.h"
#include "explorer_page.h"

void explorer_handle_page(HttpRequest *req) {
    http_send_response(req->fd, 200, "text/html", kExplorerHtml,
                       sizeof(kExplorerHtml) - 1);
}

void explorer_handle_root(HttpRequest *req) {
    http_send_redirect(req->fd, "/explorer");
}
