#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/config.h"
#include "core/http.h"

// The HTTP server knows no feature. Features plug themselves in at startup
// by registering their routes (see source/features/feature_list.c), and the server
// dispatches each request to the matching handler.

typedef void (*HttpHandler)(HttpRequest *req);

// Capacity of the route table; registration fails (and logs) past it.
#define HTTP_MAX_ROUTES 64 // a full build registers 59

// Routes `method` + exact `path` to `handler`. Strings must outlive the
// server (string literals). Returns false when the table is full.
bool http_server_register_route(const char *method, const char *path, HttpHandler handler);

// Same, but matches every path that starts with `prefix`.
bool http_server_register_prefix(const char *method, const char *prefix, HttpHandler handler);

// Paths starting with `prefix` are served without credentials. Used by the
// OAuth login, which is itself how a client obtains credentials.
bool http_server_add_public_prefix(const char *prefix);

// Accepts bearer tokens beyond the static config token. `resource_metadata_path`
// (may be NULL) is advertised in 401 responses so OAuth clients can discover
// how to log in.
typedef bool (*HttpTokenValidator)(const char *token);
void http_server_set_token_validator(HttpTokenValidator validator,
                                     const char *resource_metadata_path);

// Called before the console sleeps, once all sockets are closed. Features
// that hold resources which must not survive sleep release them here.
#define HTTP_MAX_SLEEP_HOOKS 4
typedef void (*HttpSleepHook)(void);
bool http_server_on_sleep(HttpSleepHook hook);

// Called before every authorized request is routed. Features that lend a
// resource to another program use it to notice the program is gone and take
// the resource back.
#define HTTP_MAX_REQUEST_HOOKS 2
typedef void (*HttpRequestHook)(void);
bool http_server_on_request(HttpRequestHook hook);

// --- waiting inside a handler ----------------------------------------------------
// The wait tools block their handler for up to HTTP_MAX_WAIT_MS. They sleep
// through http_server_wait_ms(), which keeps calling the idle hook. The
// server loop installs one that holds off auto-sleep while the loop itself
// is not running, and that cuts the wait short when the console asks to
// sleep, so the loop can release its sockets in time.

#define HTTP_MAX_WAIT_MS 30000

// Returns false to end the wait early.
typedef bool (*HttpIdleHook)(void);
void http_server_set_idle_hook(HttpIdleHook hook);

// Sleeps `ms`, calling the idle hook at least every 500ms. Returns false,
// possibly early, when the hook ended the wait; the caller then gives up.
bool http_server_wait_ms(int ms);

// Monotonic milliseconds, for measuring waits.
uint64_t http_server_now_ms(void);

// Authenticates and dispatches one parsed request, then releases its request
// memory. Used by the server loop; exposed so host tests can drive routes.
void http_server_dispatch(const Config *cfg, HttpRequest *req);

// Runs the server forever. Binds to cfg->port and retries bind/listen
// failures internally.
void http_server_run(const Config *cfg);
