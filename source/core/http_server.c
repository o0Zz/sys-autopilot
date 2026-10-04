// The server loop: sockets, keep-alive, mDNS, sleep/wake and keep-awake.
// Routing and authentication live in http_router.c.
#include "core/http_server.h"
#include "core/log.h"
#include "core/mdns.h"
#include "platform/netif.h"
#include "platform/power.h"

#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <switch.h>

// Keep-awake ping period. Must stay comfortably under the ~10s idle policy the
// lock screen ("press A three times", shown after boot and after every wake)
// applies, which is far shorter than any auto-sleep plan exposed in System
// Settings: a 30s ping never landed inside it and the console dropped straight
// back to sleep. One idle:sys IPC per period is negligible.
#define KEEPAWAKE_INTERVAL_NS (5ULL * 1000000000ULL)

static int create_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    http_set_nonblocking(fd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u16)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static HttpSleepHook g_sleep_hooks[HTTP_MAX_SLEEP_HOOKS];
static int g_sleep_hook_count;

bool http_server_on_sleep(HttpSleepHook hook) {
    if (g_sleep_hook_count >= HTTP_MAX_SLEEP_HOOKS) {
        LOGW("server", "sleep hook table full");
        return false;
    }
    g_sleep_hooks[g_sleep_hook_count++] = hook;
    return true;
}

// The request being served. The server is single-threaded, so there is only
// ever one; it is large (header buffer), so it lives here, not on the stack.
static HttpRequest g_request;

// Drains any request body the handler didn't consume, so the next request on a
// kept-alive connection starts on a clean boundary. Returns false if the body
// is unexpectedly large/stalled (treat as non-reusable).
static bool drain_body(HttpRequest *req) {
    if (!req->has_content_length)
        return true;
    char scratch[1024];
    int guard = 0;
    while (req->body_consumed < req->content_length) {
        if (++guard > 4096) // ~4MB cap; runaway -> just close
            return false;
        if (http_read_body(req, scratch, sizeof(scratch)) <= 0)
            return false;
    }
    return true;
}

typedef enum { CONN_CLOSE, CONN_KEEP } ConnDisp;

// Handles one request. Returns CONN_KEEP if the connection may be reused
// (keep-alive, body drained) or CONN_CLOSE to close.
static ConnDisp handle_one(int fd, const Config *cfg) {
    HttpRequest *req = &g_request;
    if (!http_read_request(fd, req))
        return CONN_CLOSE;

    // Keep-alive for HTTP/1.1 so the MCP SDK can reuse one socket across
    // initialize -> notifications/initialized -> tools/list. The GET stream is
    // declined with 405+close, so nothing tries to pipeline onto it.
    req->keep_alive = req->http11 && !req->conn_close;
    http_set_keep_alive(req->keep_alive);

    http_server_dispatch(cfg, req);
    return (req->keep_alive && drain_body(req)) ? CONN_KEEP : CONN_CLOSE;
}

// How long a freshly accepted connection gets to start sending its request.
// The server is single-threaded, so this is the whole server's dead time when
// a peer connects and then says nothing -- which browsers do routinely, they
// pre-open connections they may never use. The http layer's own 10s timeout
// still covers a transfer already under way; this only bounds the silence
// before one starts.
#define FIRST_REQUEST_MS 1000

// How long close_client waits for the peer to hang up before doing it itself.
#define CLIENT_LINGER_MS 50

// Ends a client connection without emitting an RST, and without leaving the
// console holding TIME_WAIT.
//
// Two hazards, both of which bite this server hard:
//
// close() on a socket that still has unread bytes queued resets the connection
// rather than finishing the handshake, and the peer reports a connection reset
// instead of the clean end of connection it would retry silently.
//
// And whichever side sends FIN first keeps the socket in TIME_WAIT afterwards.
// Those linger, they are drawn from the same small socket pool as live
// connections (see kSocketConfig in main.c), and the server closing every
// connection itself meant a burst of requests exhausted the pool and the
// console reset everything until they aged out.
//
// So: drain first and let the peer hang up. Responses always carry a
// Content-Length, so a client knows the body has ended and closes on its own,
// usually within a millisecond or two on a LAN -- which makes us the passive
// closer and leaves TIME_WAIT on the client where it belongs. Only a peer that
// is still idling when the grace period ends gets a FIN from us.
static void close_client(int fd) {
    bool peer_closed = false;
    for (int waited = 0; waited < CLIENT_LINGER_MS; waited += 10) {
        struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
        if (poll(&p, 1, 10) <= 0)
            continue; // timeout or EINTR: wait out the rest of the grace period
        char scratch[512];
        ssize_t n = recv(fd, scratch, sizeof(scratch), 0);
        if (n == 0) { // peer's FIN: it closed first, so we owe no TIME_WAIT
            peer_closed = true;
            break;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            break;
    }
    if (!peer_closed)
        shutdown(fd, SHUT_WR); // idle peer: end it ourselves, FIN before close
    close(fd);
}

// Serves an accepted connection (one or more keep-alive requests).
static void handle_connection(int fd, const Config *cfg) {
    // Non-blocking I/O: the http layer waits via poll() with an inactivity
    // timeout, so a stalled client can't wedge the server.
    http_set_nonblocking(fd);

    // Serve multiple requests on one connection (HTTP/1.1 keep-alive). Bounded
    // so a single client can't monopolize the single-threaded server. MCP
    // clients reuse the socket across initialize -> initialized -> tools/list,
    // so closing after each response broke their transport.
    for (int served = 0; served < 64; served++) {
        if (handle_one(fd, cfg) != CONN_KEEP)
            break;
        // Keep-alive: only stay if the next request is already arriving. The
        // server is single-threaded, so blocking here on a quiet socket would
        // starve everyone else; if the peer isn't immediately pipelining, close
        // and let it reconnect (the response already advertised keep-alive, so
        // pooled clients just open a fresh socket).
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 50) <= 0)
            break;
    }
}

// Keep-awake ping deadline, shared by the loop and the idle hook. Measured on
// the system tick rather than loop iterations: an iteration is ~100ms only
// when poll() times out, and stretches to seconds while a request is served
// or a failed bind is retried, so a tick count is not a clock. 0 means "ping
// now", so the first ping goes out before the console has had time to idle
// out.
static u64 g_keepawake_next;
static bool g_keep_awake; // cfg->keep_awake, for the idle hook

static void keepawake_maybe(void) {
    if (!g_keep_awake)
        return;
    u64 now = armGetSystemTick();
    if (now >= g_keepawake_next) {
        g_keepawake_next = now + armNsToTicks(KEEPAWAKE_INTERVAL_NS);
        power_keepawake_tick();
    }
}

// Idle hook for handlers that wait: keep the console awake, but give up as
// soon as it wants to sleep.
static bool wait_idle(void) {
    if (power_sleep_requested()) {
        LOGI("server", "sleep requested; ending a wait early");
        return false;
    }
    keepawake_maybe();
    return true;
}

// Delay before retrying a failed bind/listen or a broken poll.
#define RETRY_WAIT_NS 1000000000LL // 1s

// Closes the listener and the mDNS socket, if open, and marks both closed.
static void close_sockets(int *listen_fd, int *mdns_fd) {
    if (*listen_fd >= 0) {
        close(*listen_fd);
        *listen_fd = -1;
    }
    if (*mdns_fd >= 0) {
        mdns_close(*mdns_fd);
        *mdns_fd = -1;
    }
}

void http_server_run(const Config *cfg) {
    int listen_fd = -1;
    int mdns_fd = -1;
    bool suspended = false;

    // Build the mDNS / DNS-SD advertising parameters once. If the local IP
    // isn't available yet (interface down), discovery is retried lazily each
    // time the listener is (re)opened below.
    static MdnsConfig mdns_cfg; // off the stack; lives for the loop
    bool mdns_ready = mdns_config_init(&mdns_cfg, cfg);
    if (!mdns_ready)
        LOGW("mdns", "local IP not yet known; will retry");

    // Pending unsolicited announcements. Set when the mDNS socket (re)opens
    // and decremented only when a send actually succeeds, so we keep retrying
    // across the seconds it can take for routing to come up after the network
    // changes (sends fail with EHOSTUNREACH until then) instead of giving up.
    int mdns_announce_left = 0;

    // Throttle for the periodic nifm IP-change check (see netif.h). The loop
    // spins ~every 100ms; check every ~2s.
    int netcheck_ticks = 0;

    // A handler blocked in a wait tool keeps pinging through the idle hook.
    g_keep_awake = cfg->keep_awake;
    http_server_set_idle_hook(wait_idle);

    for (;;) {
        // Participate in sleep/wake transitions: all sockets must be closed
        // and no bsd IPC may be issued between the sleep acknowledgement and
        // the wake notification, or bsdsockets aborts and the whole console
        // crashes on the next wake. Since this loop is the only thread doing
        // socket I/O, nothing is in flight when we acknowledge here.
        PowerEvent pe = power_poll();
        if (pe == PowerEvent_Sleep) {
            // Log BEFORE suspending the sink, then block all further file I/O:
            // no fsp-srv (or bsd) IPC may occur between this point and the wake
            // notification, or the console hangs on wake. The log sink writes
            // to the SD card, so it must stay silent across the whole window.
            LOGI("power", "sleeping, releasing sockets");
            log_set_suspended(true);
            close_sockets(&listen_fd, &mdns_fd);
            // Features release what must not survive sleep (the input
            // feature's HDLS work buffer: holding hid transfer memory across
            // the transition crashes the sleep sequence).
            for (int i = 0; i < g_sleep_hook_count; i++)
                g_sleep_hooks[i]();
            suspended = true;
            power_ack();
        } else if (pe == PowerEvent_Wake) {
            power_ack();
            suspended = false;
            // Safe to touch the SD card again now that we are awake.
            log_set_suspended(false);
            LOGI("power", "awake");
            // Wake lands back on the lock screen and its short idle policy
            // starts running immediately: ping on the very next iteration.
            g_keepawake_next = 0;
        }
        if (suspended) {
            svcSleepThread(100000000LL); // 100ms between power_poll checks
            continue;
        }

        // Hold off auto-sleep: sleeping powers down the WLAN module, and the
        // console then answers nothing until someone physically presses a
        // button. Runs only while awake, so the ping never lands inside the
        // sleep window.
        keepawake_maybe();

        // React to network connectivity changes by polling nifm for our current
        // IP (see netif.h for why polling). The check runs only while awake, so
        // no nifm IPC ever hits the sleep window. On an IP change we rebuild BOTH
        // sockets: a network teardown invalidates them, and the listener can
        // otherwise silently stop accepting (poll() doesn't always report it)
        // while mDNS keeps working, leaving the API unreachable on a live
        // console.
        if (++netcheck_ticks >= 20) { // ~2s at 100ms/iteration
            netcheck_ticks = 0;
            if (netif_ipv4_changed()) {
                LOGW("server", "IP changed; rebuilding sockets");
                close_sockets(&listen_fd, &mdns_fd);
                mdns_ready = false; // re-query the IP on the next iteration
            }
        }

        if (listen_fd < 0) {
            listen_fd = create_listener(cfg->port);
            if (listen_fd < 0) {
                LOGE("server", "bind/listen on port %d failed (errno=%d), retrying",
                     cfg->port, errno);
                svcSleepThread(RETRY_WAIT_NS);
                continue;
            }
            LOGI("server", "listening on port %d", cfg->port);
        }

        // (Re)establish mDNS advertising. The listener binds to INADDR_ANY and
        // succeeds even before DHCP finishes, so we can't gate this on the
        // listener: instead keep retrying here until the local IP is known and
        // the socket is open. Cheap no-op once mdns_fd is up.
        if (mdns_fd < 0) {
            if (!mdns_ready)
                mdns_ready = mdns_config_init(&mdns_cfg, cfg);
            if (mdns_ready) {
                mdns_fd = mdns_open(&mdns_cfg);
                if (mdns_fd >= 0) {
                    LOGI("server", "mDNS up as %s", mdns_cfg.host);
                    mdns_announce_left = 3; // sent once routing is up (below)
                }
            }
        }

        // Send pending announcements, one per loop iteration, but only count
        // an announcement as sent when sendto() actually succeeds. Right after
        // a network change, routing isn't up yet and sends fail with
        // EHOSTUNREACH; retrying each iteration means we keep trying for as
        // long as it takes rather than burning the burst on dead sends.
        if (mdns_fd >= 0 && mdns_announce_left > 0) {
            if (mdns_announce(mdns_fd, &mdns_cfg))
                mdns_announce_left--;
        }

        struct pollfd pfds[2] = {
            { .fd = listen_fd, .events = POLLIN, .revents = 0 },
            { .fd = mdns_fd,   .events = POLLIN, .revents = 0 },
        };
        int pr = poll(pfds, mdns_fd >= 0 ? 2 : 1, 100);

        if (pr < 0) {
            // bsd service hiccup; rebuild both sockets.
            LOGE("server", "poll failed (errno=%d), rebuilding listener", errno);
            close_sockets(&listen_fd, &mdns_fd);
            svcSleepThread(RETRY_WAIT_NS);
            continue;
        }
        if (pr == 0)
            continue;

        // Service mDNS queries before HTTP accepts.
        if (mdns_fd >= 0 && (pfds[1].revents & POLLIN))
            mdns_handle_readable(mdns_fd, &mdns_cfg);

        if (!(pfds[0].revents & POLLIN))
            continue;

        int client = accept(listen_fd, NULL, NULL);
        if (client < 0) {
            // EAGAIN: spurious wakeup on the non-blocking listener.
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                LOGE("server", "accept failed (errno=%d)", errno);
            continue;
        }

        // Drop a peer that connects and then says nothing, rather than
        // letting it block every other client behind it.
        struct pollfd first = { .fd = client, .events = POLLIN, .revents = 0 };
        if (poll(&first, 1, FIRST_REQUEST_MS) > 0)
            handle_connection(client, cfg);
        close_client(client);

        // Agent-requested power action: executed only after the response has
        // been sent and the connection closed, so the client gets its
        // confirmation before the console goes away.
        PowerAction act = power_take_scheduled();
        if (act != PowerAction_None) {
            LOGI("server", "executing power action %d", act);
            svcSleepThread(200000000LL); // 200ms: let the response flush
            if (!power_perform(act))
                LOGE("server", "power action failed");
            // For sleep, the PSC ReadySleep event arrives on a subsequent
            // iteration and quiesces sockets/HDLS as usual.
        }
    }
}
