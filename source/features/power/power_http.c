#include "features/power/power_http.h"
#include "core/http_server.h"
#include "platform/power.h"

// The action runs in the server loop once this response has been flushed and
// the connection closed, so the client gets its confirmation first.
static void schedule(HttpRequest *req, PowerAction action, const char *note) {
    if (!power_actions_available()) {
        http_send_error(req->fd, 500, "power control unavailable");
        return;
    }
    http_send_json(req->fd, 200, "{\"ok\":true,\"note\":\"%s\"}", note);
    power_schedule(action);
}

static void post_sleep(HttpRequest *req) {
    schedule(req, PowerAction_Sleep,
             "entering sleep; server unreachable until console is woken physically");
}

static void post_restart(HttpRequest *req) {
    schedule(req, PowerAction_Restart,
             "rebooting; server returns after the console boots back into CFW (bootloader menus may require manual intervention)");
}

static void post_off(HttpRequest *req) {
    schedule(req, PowerAction_PowerOff,
             "powering off; physical power button required to turn back on");
}

void power_http_register(void) {
    http_server_register_route("POST", "/power/sleep",   post_sleep);
    http_server_register_route("POST", "/power/restart", post_restart);
    http_server_register_route("POST", "/power/off",     post_off);
}
