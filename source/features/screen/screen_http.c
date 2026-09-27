#include "features/screen/screen_http.h"
#include "core/http_server.h"
#include "core/request.h"
#include "features/screen/screen.h"

_Static_assert(CAPSSC_JPEG_BUFFER_SIZE <= REQUEST_MEMORY_SIZE,
               "request memory too small for a screenshot");

// GET /screenshot[?stack=...]
static void get_screenshot(HttpRequest *req) {
    ViLayerStack stack = ViLayerStack_Screenshot;
    char val[32];
    if (http_query_get(req, "stack", val, sizeof(val)) && !screen_parse_stack(val, &stack)) {
        http_send_error(req->fd, 400, "invalid 'stack' (use screenshot|default|lcd|recording|lastframe)");
        return;
    }

    u8 *jpeg = request_alloc(req, CAPSSC_JPEG_BUFFER_SIZE);
    if (!jpeg) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }
    u64 size = 0;
    Result rc = screen_capture_jpeg(stack, jpeg, CAPSSC_JPEG_BUFFER_SIZE, &size);
    if (R_FAILED(rc)) {
        http_send_json(req->fd, 500, "{\"error\":\"capture failed\",\"rc\":\"0x%x\"}", rc);
        return;
    }
    http_send_response(req->fd, 200, "image/jpeg", jpeg, (size_t)size);
}

void screen_http_register(void) {
    http_server_register_route("GET", "/screenshot", get_screenshot);
}
