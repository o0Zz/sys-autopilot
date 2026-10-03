#include "features/settings/settings_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/settings/settings.h"

#include <string.h>
#include <strings.h>

static bool is_get(const HttpRequest *req) {
    return strcmp(req->method, "GET") == 0;
}

// GET|POST /settings/theme  {"theme":"light"|"dark"}
static void handle_theme(HttpRequest *req) {
    if (is_get(req)) {
        bool dark = false;
        if (!settings_get_theme(&dark)) {
            http_send_error(req->fd, 500, "failed to read theme");
            return;
        }
        http_send_json(req->fd, 200, "{\"theme\":\"%s\"}", dark ? "dark" : "light");
        return;
    }
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    char val[16] = {0};
    int t = json_obj_get(doc, 0, "theme");
    if (t < 0 || !json_get_string(doc, t, val, sizeof(val))) {
        http_send_error(req->fd, 400, "missing 'theme' (\"light\" or \"dark\")");
        return;
    }
    bool dark;
    if (strcasecmp(val, "dark") == 0)       dark = true;
    else if (strcasecmp(val, "light") == 0) dark = false;
    else { http_send_error(req->fd, 400, "'theme' must be \"light\" or \"dark\""); return; }

    if (!settings_set_theme(dark))
        http_send_error(req->fd, 500, "failed to set theme");
    else
        http_send_json(req->fd, 200, "{\"ok\":true,\"theme\":\"%s\"}", dark ? "dark" : "light");
}

// GET|POST /settings/nickname  {"nickname":"..."}
static void handle_nickname(HttpRequest *req) {
    char name[128] = {0};
    if (is_get(req)) {
        if (!settings_get_nickname(name, sizeof(name))) {
            http_send_error(req->fd, 500, "failed to read nickname");
            return;
        }
        http_send_json(req->fd, 200, "{\"nickname\":\"%s\"}", request_json_escape(req, name));
        return;
    }
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    int t = json_obj_get(doc, 0, "nickname");
    if (t < 0 || !json_get_string(doc, t, name, sizeof(name)) || name[0] == '\0') {
        http_send_error(req->fd, 400, "missing non-empty 'nickname'");
        return;
    }
    if (!settings_set_nickname(name))
        http_send_error(req->fd, 500, "failed to set nickname");
    else
        http_send_json(req->fd, 200, "{\"ok\":true,\"nickname\":\"%s\"}",
                       request_json_escape(req, name));
}

// Shared get/set for the two normalized 0..1 float settings.
static void handle_float(HttpRequest *req, const char *key,
                         bool (*get)(float *), bool (*set)(float)) {
    if (is_get(req)) {
        float v = 0.0f;
        if (!get(&v)) {
            http_send_error(req->fd, 500, "failed to read setting");
            return;
        }
        http_send_json(req->fd, 200, "{\"%s\":%.3f}", key, v);
        return;
    }
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    int t = json_obj_get(doc, 0, key);
    double v;
    if (t < 0 || !json_get_double(doc, t, &v)) {
        http_send_error(req->fd, 400, "missing numeric value (0.0 - 1.0)");
        return;
    }
    if (!set((float)v))
        http_send_error(req->fd, 500, "failed to set setting");
    else
        http_send_json(req->fd, 200, "{\"ok\":true,\"%s\":%.3f}", key,
                       v < 0 ? 0 : (v > 1 ? 1 : v));
}

static void handle_brightness(HttpRequest *req) {
    handle_float(req, "brightness", settings_get_brightness, settings_set_brightness);
}

static void handle_volume(HttpRequest *req) {
    handle_float(req, "volume", settings_get_volume, settings_set_volume);
}

// POST /settings/airplane. Enable-only: disabling wireless cuts our own
// connectivity.
static void post_airplane(HttpRequest *req) {
    if (!settings_disable_wireless()) {
        http_send_error(req->fd, 500, "failed to disable wireless");
        return;
    }
    http_send_json(req->fd, 200,
                   "{\"ok\":true,\"note\":\"wireless disabled; the server is now "
                   "unreachable until wireless is re-enabled on the console\"}");
}

// GET|POST /settings/auto-time  {"autoTime":true|false}
static void handle_auto_time(HttpRequest *req) {
    if (is_get(req)) {
        bool en = false;
        if (!settings_get_auto_time(&en)) {
            http_send_error(req->fd, 500, "failed to read auto-time");
            return;
        }
        http_send_json(req->fd, 200, "{\"autoTime\":%s}", en ? "true" : "false");
        return;
    }
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    int t = json_obj_get(doc, 0, "autoTime");
    bool en;
    if (t < 0 || !json_get_bool(doc, t, &en)) {
        http_send_error(req->fd, 400, "missing boolean 'autoTime'");
        return;
    }
    if (!settings_set_auto_time(en))
        http_send_error(req->fd, 500, "failed to set auto-time");
    else
        http_send_json(req->fd, 200, "{\"ok\":true,\"autoTime\":%s}", en ? "true" : "false");
}

// GET|POST /settings/datetime  {"year":..,"month":..,...} (any subset)
static void handle_datetime(HttpRequest *req) {
    DateTime dt = {0};
    if (is_get(req)) {
        if (!settings_get_datetime(&dt)) {
            http_send_error(req->fd, 500, "failed to read date/time");
            return;
        }
        http_send_json(req->fd, 200,
                       "{\"year\":%d,\"month\":%d,\"day\":%d,"
                       "\"hour\":%d,\"minute\":%d,\"second\":%d,"
                       "\"timezone\":\"%s\"}",
                       dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
                       dt.timezone);
        return;
    }
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;

    settings_get_datetime(&dt);
    dt.year   = json_obj_int(doc, 0, "year",   dt.year);
    dt.month  = json_obj_int(doc, 0, "month",  dt.month);
    dt.day    = json_obj_int(doc, 0, "day",    dt.day);
    dt.hour   = json_obj_int(doc, 0, "hour",   dt.hour);
    dt.minute = json_obj_int(doc, 0, "minute", dt.minute);
    dt.second = json_obj_int(doc, 0, "second", dt.second);

    if (!settings_datetime_valid(&dt)) {
        http_send_error(req->fd, 400, "invalid date/time fields");
        return;
    }
    if (!settings_set_datetime(&dt))
        http_send_error(req->fd, 500, "failed to set the clock");
    else
        http_send_json(req->fd, 200,
                       "{\"ok\":true,\"year\":%d,\"month\":%d,\"day\":%d,"
                       "\"hour\":%d,\"minute\":%d,\"second\":%d,\"timezone\":\"%s\"}",
                       dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
                       dt.timezone);
}

void settings_http_register(void) {
    http_server_register_route("GET",  "/settings/theme",      handle_theme);
    http_server_register_route("POST", "/settings/theme",      handle_theme);
    http_server_register_route("GET",  "/settings/nickname",   handle_nickname);
    http_server_register_route("POST", "/settings/nickname",   handle_nickname);
    http_server_register_route("GET",  "/settings/brightness", handle_brightness);
    http_server_register_route("POST", "/settings/brightness", handle_brightness);
    http_server_register_route("GET",  "/settings/volume",     handle_volume);
    http_server_register_route("POST", "/settings/volume",     handle_volume);
    http_server_register_route("POST", "/settings/airplane",   post_airplane);
    http_server_register_route("GET",  "/settings/auto-time",  handle_auto_time);
    http_server_register_route("POST", "/settings/auto-time",  handle_auto_time);
    http_server_register_route("GET",  "/settings/datetime",   handle_datetime);
    http_server_register_route("POST", "/settings/datetime",   handle_datetime);
}
