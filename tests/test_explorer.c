// Host-side unit test for source/common/explorer.c (the built-in file explorer
// page and its root redirect).
#include "explorer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

static char out[65536];

// Runs a handler against a socketpair and returns what it wrote.
static size_t capture(void (*handler)(HttpRequest *), const char *host) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    HttpRequest req;
    memset(&req, 0, sizeof(req));
    req.fd = sv[0];
    snprintf(req.host, sizeof(req.host), "%s", host);
    handler(&req);
    shutdown(sv[0], SHUT_WR);

    size_t total = 0;
    ssize_t n;
    while (total < sizeof(out) - 1 &&
           (n = read(sv[1], out + total, sizeof(out) - 1 - total)) > 0)
        total += (size_t)n;
    out[total] = '\0';
    close(sv[0]);
    close(sv[1]);
    return total;
}

static void send_unauthorized(HttpRequest *req) {
    http_send_unauthorized(req, true, true);
}

int main(void) {
    // 1. The page is served as HTML, with a Content-Length matching the body.
    capture(explorer_handle_page, "console:4150");
    assert(strncmp(out, "HTTP/1.1 200 OK\r\n", 17) == 0);
    assert(strstr(out, "Content-Type: text/html\r\n"));

    const char *body = strstr(out, "\r\n\r\n");
    assert(body);
    body += 4;
    const char *clen = strstr(out, "Content-Length: ");
    assert(clen);
    assert((size_t)atoi(clen + 16) == strlen(body));

    assert(strncmp(body, "<!doctype html>", 15) == 0);
    // The page drives the real API, same-origin, and can write as well as read.
    assert(strstr(body, "'/files?path='"));
    assert(strstr(body, "method: 'PUT'"));
    assert(strstr(body, "method: 'DELETE'"));

    // 2. The bare address redirects to the explorer.
    capture(explorer_handle_root, "console:4150");
    assert(strncmp(out, "HTTP/1.1 302 Found\r\n", 20) == 0);
    assert(strstr(out, "Location: /explorer\r\n"));

    // 3. Browsers pick the first scheme they understand, so Basic must be
    //    offered ahead of Bearer or the login prompt never appears.
    capture(send_unauthorized, "console:4150");
    const char *basic = strstr(out, "WWW-Authenticate: Basic ");
    const char *bearer = strstr(out, "WWW-Authenticate: Bearer ");
    assert(basic && bearer && basic < bearer);

    printf("all explorer tests passed\n");
    return 0;
}
