#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal multicast-DNS (RFC 6762) + DNS-SD (RFC 6763) responder.
//
// Advertises the console under "<hostname>.local" (A record) and the
// "_sys-autopilot._tcp.local" service (PTR/SRV/TXT) so clients can either
// resolve the name directly or browse for the service without knowing the IP.
//
// The wire-format helpers (parse/build) are pure and host-testable; the
// socket lifecycle (open/announce/close + poll integration) is Switch-only.

// Service type advertised over DNS-SD.
#define MDNS_SERVICE_TYPE "_sys-autopilot._tcp.local"
#define MDNS_MULTICAST_ADDR "224.0.0.251"
#define MDNS_PORT 5353

// Immutable advertising parameters, computed once at startup.
typedef struct {
    char instance[64];   // DNS-SD instance label, e.g. "nintendo-switch"
    char host[80];       // "<hostname>.local"
    char service[64];    // MDNS_SERVICE_TYPE
    uint16_t port;       // HTTP service port (host byte order)
    uint32_t ipv4;       // our IPv4, host order (octet 1 in the high byte)
    char txt[256];       // DNS-SD TXT rdata: length-prefixed key=value blocks
    size_t txt_len;      // bytes used in txt
} MdnsConfig;

// --- pure wire-format layer (host-testable) ----------------------------------

#include "core/config.h"

// Populates `cfg` from the runtime configuration: instance/host names from
// the configured hostname, the HTTP `port`, the local IPv4 (from nifm), and the
// DNS-SD TXT record (version, mdns_add_txt() pairs, auth, model, firmware,
// atmosphere).
// Returns false if the local IP could not be determined (discovery is then
// skipped). On host builds this fills placeholder values and always succeeds.
bool mdns_config_init(MdnsConfig *cfg, const Config *app_cfg);

// Adds a "key=value" pair to the TXT record built by mdns_config_init (the
// string must outlive the server). A pair with key "auth" replaces the value
// derived from the configuration. Returns false when the table is full.
bool mdns_add_txt(const char *pair);

// Given an incoming mDNS query packet (query, query_len), appends any matching
// answer records to out (capacity out_cap) and returns the response length, or
// 0 if nothing should be sent. Handles A (for cfg->host), PTR (for the service
// type), and SRV/TXT (for the instance). cfg must be fully populated.
// When out_unicast is non-NULL, it is set true if a matched question requested
// a unicast (QU) response, meaning the reply should go to the sender rather
// than the multicast group.
size_t mdns_build_response(const MdnsConfig *cfg,
                           const uint8_t *query, size_t query_len,
                           uint8_t *out, size_t out_cap, bool *out_unicast);

// Builds an unsolicited announcement (gratuitous PTR/SRV/TXT/A) into out.
// Returns the packet length, or 0 on failure.
size_t mdns_build_announcement(const MdnsConfig *cfg,
                               uint8_t *out, size_t out_cap);

#ifdef __SWITCH__
// --- Switch socket layer ------------------------------------------------------

// Opens the mDNS UDP socket (binds :5353, joins the multicast group) using the
// populated cfg. Returns the fd, or -1 on failure.
int mdns_open(const MdnsConfig *cfg);

// Handles a readable mDNS socket: reads one datagram, and if it matches,
// multicasts the response.
void mdns_handle_readable(int fd, const MdnsConfig *cfg);

// Sends an unsolicited announcement to the multicast group. Returns true if
// the datagram was sent (false if the network isn't routable yet), so callers
// can retry until an announcement actually goes out.
bool mdns_announce(int fd, const MdnsConfig *cfg);

// Closes the socket (dropping group membership). Safe with fd < 0.
void mdns_close(int fd);
#endif
