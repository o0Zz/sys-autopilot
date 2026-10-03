#include "features/input/input_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/input/input.h"
#include "features/input/input_args.h"
#include "features/input/keyboard.h"

#include <assert.h>

// input_args.h stays host-testable (no libnx), so it carries its own copy of
// the panel bounds; keep the two in step.
static_assert(ARGS_TOUCH_MAX_X == INPUT_TOUCH_WIDTH - 1, "touch width drift");
static_assert(ARGS_TOUCH_MAX_Y == INPUT_TOUCH_HEIGHT - 1, "touch height drift");

static void send_input_result(HttpRequest *req, Result rc) {
    if (R_FAILED(rc))
        http_send_json(req->fd, 500, "{\"error\":\"input failed\",\"rc\":\"0x%x\"}", rc);
    else
        http_send_json(req->fd, 200, "{\"ok\":true}");
}

static void post_tap(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    u64 mask;
    const char *err = NULL;
    if (!args_get_buttons(doc, 0, &mask, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_tap(mask, args_get_duration(doc, 0, INPUT_DEFAULT_TAP_MS)));
}

static void hold_or_release(HttpRequest *req, bool hold) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    u64 mask;
    const char *err = NULL;
    if (!args_get_buttons(doc, 0, &mask, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, hold ? input_hold(mask) : input_release(mask));
}

static void post_hold(HttpRequest *req)    { hold_or_release(req, true); }
static void post_release(HttpRequest *req) { hold_or_release(req, false); }

static void post_touch(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    int x, y, duration;
    const char *err = NULL;
    if (!args_get_touch(doc, 0, &x, &y, &duration, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_touch_tap(x, y, duration));
}

static void post_swipe(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    int x0, y0, x1, y1, duration;
    const char *err = NULL;
    if (!args_get_swipe(doc, 0, &x0, &y0, &x1, &y1, &duration, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_touch_swipe(x0, y0, x1, y1, duration));
}

static void post_stick(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    int side, duration;
    float x, y;
    const char *err = NULL;
    if (!args_get_stick(doc, 0, &side, &x, &y, &duration, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_stick(side, x, y, duration));
}

// POST /input/text {"text":"hello\n","keyMs":40}
static void post_text(HttpRequest *req) {
    JsonDoc *doc = request_read_json(req);
    if (!doc)
        return;
    char text[KEYBOARD_TEXT_MAX + 1];
    int t = json_obj_get(doc, 0, "text");
    if (t < 0 || !json_get_string(doc, t, text, sizeof(text)) || text[0] == '\0') {
        http_send_error(req->fd, 400, "missing 'text' (non-empty string, at most 256 bytes)");
        return;
    }
    int layout = input_keyboard_layout();
    char msg[160];
    if (!keyboard_check_text(layout, text, msg, sizeof(msg))) {
        http_send_error(req->fd, 400, msg);
        return;
    }
    send_input_result(req, input_type_text(layout, text,
                                           json_obj_int(doc, 0, "keyMs", INPUT_DEFAULT_KEY_MS)));
}

static void post_clear(HttpRequest *req)  { send_input_result(req, input_clear()); }
static void post_attach(HttpRequest *req) { send_input_result(req, input_attach()); }
static void post_detach(HttpRequest *req) { send_input_result(req, input_detach()); }

void input_http_register(void) {
    http_server_register_route("POST", "/input/tap",         post_tap);
    http_server_register_route("POST", "/input/hold",        post_hold);
    http_server_register_route("POST", "/input/release",     post_release);
    http_server_register_route("POST", "/input/stick",       post_stick);
    http_server_register_route("POST", "/input/touch",       post_touch);
    http_server_register_route("POST", "/input/swipe",       post_swipe);
    http_server_register_route("POST", "/input/text",        post_text);
    http_server_register_route("POST", "/input/clear",       post_clear);
    http_server_register_route("POST", "/controller/attach", post_attach);
    http_server_register_route("POST", "/controller/detach", post_detach);
}
