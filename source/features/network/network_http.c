#include "features/network/network_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/network/network.h"

// GET /network/dns
static void get_dns(HttpRequest *req) {
    DnsConfig c;
    char err[96];
    if (!network_get_dns(&c, err, sizeof(err))) {
        http_send_error(req->fd, 500, err);
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

    bool automatic = false;
    int t = json_obj_get(doc, 0, "automatic");
    if (t >= 0)
        json_get_bool(doc, t, &automatic);

    // Buffers sized generously: json_get_string needs headroom (it reserves a
    // few bytes for escape expansion), so a 16-byte buffer would reject a full
    // 15-char dotted IPv4. network_set_dns validates the actual format.
    char primary[64] = {0}, secondary[64] = {0};
    t = json_obj_get(doc, 0, "primary");
    if (t >= 0) json_get_string(doc, t, primary, sizeof(primary));
    t = json_obj_get(doc, 0, "secondary");
    if (t >= 0) json_get_string(doc, t, secondary, sizeof(secondary));

    if (!automatic && !primary[0]) {
        http_send_error(req->fd, 400, "provide 'primary' (IPv4) or set 'automatic':true");
        return;
    }

    char err[96];
    if (!network_set_dns(automatic, primary, secondary, err, sizeof(err)))
        http_send_error(req->fd, 500, err);
    else if (automatic)
        http_send_json(req->fd, 200, "{\"ok\":true,\"automatic\":true}");
    else
        http_send_json(req->fd, 200,
            "{\"ok\":true,\"automatic\":false,\"primary\":\"%s\",\"secondary\":\"%s\"}",
            primary, secondary);
}

void network_http_register(void) {
    http_server_register_route("GET",  "/network/dns", get_dns);
    http_server_register_route("POST", "/network/dns", post_dns);
}
