#include "features/network/network_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/network/network.h"

// GET /network/dns
static void get_dns(HttpRequest *req) {
    DnsConfig c;
    char err[160];
    NetworkResult r = network_get_dns(&c, err, sizeof(err));
    if (r != NETWORK_OK) {
        http_send_error(req->fd, network_http_status(r), err);
        return;
    }
    http_send_json(req->fd, 200,
        "{\"automatic\":%s,\"primary\":\"%s\",\"secondary\":\"%s\"}",
        c.is_automatic ? "true" : "false", c.primary, c.secondary);
}

// POST /network/dns {"automatic":true} | {"primary":"1.2.3.4","secondary":"5.6.7.8"}
static void post_dns(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;

    bool automatic;
    char primary[NETWORK_DNS_ARG_SIZE], secondary[NETWORK_DNS_ARG_SIZE];
    const char *arg_err;
    if (!network_dns_from_json(doc, 0, &automatic, primary, secondary, &arg_err)) {
        http_send_error(req->fd, 400, arg_err);
        return;
    }

    // The console reconnects to apply it: say so, since the next requests may
    // meet a dropped connection or a 503 for a few seconds.
    char err[160];
    NetworkResult r = network_set_dns(automatic, primary, secondary, err, sizeof(err));
    if (r != NETWORK_OK)
        http_send_error(req->fd, network_http_status(r), err);
    else if (automatic)
        http_send_json(req->fd, 200, "{\"ok\":true,\"automatic\":true,\"reconnecting\":true}");
    else
        http_send_json(req->fd, 200,
            "{\"ok\":true,\"automatic\":false,\"primary\":\"%s\",\"secondary\":\"%s\","
            "\"reconnecting\":true}",
            primary, secondary);
}

void network_http_register(void) {
    http_server_register_route("GET",  "/network/dns", get_dns);
    http_server_register_route("POST", "/network/dns", post_dns);
}
