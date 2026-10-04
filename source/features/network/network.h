#pragma once

#include "util/json.h"

#include <stdbool.h>
#include <stddef.h>

// Network configuration (currently DNS) for the active connection profile.
// Setting DNS requires the nifm:a (admin) service; reading the current config
// works with nifm:u. Useful for pointing the console at a custom/black-hole DNS
// (e.g. 90DNS) so a console can stay on the LAN while Nintendo servers are
// blocked — required for offline use of fake-linked accounts.

typedef struct {
    bool is_automatic;   // true = DHCP-provided DNS, false = manual
    char primary[16];    // dotted IPv4, e.g. "207.246.121.77" ("" if unset)
    char secondary[16];  // dotted IPv4 ("" if unset)
} DnsConfig;

typedef enum {
    NETWORK_OK,
    NETWORK_INVALID,     // bad argument (HTTP 400)
    NETWORK_UNAVAILABLE, // no active connection, e.g. reconnecting (HTTP 503)
    NETWORK_FAILED,      // anything else (HTTP 500)
} NetworkResult;

// Read the current connection's DNS configuration. Any result but NETWORK_OK
// leaves a message in err. (Host build: a stub that reports failure.)
NetworkResult network_get_dns(DnsConfig *out, char *err, size_t errsz);

// Set the active profile's DNS. If automatic is true, reverts to DHCP DNS and
// primary/secondary are ignored. Otherwise primary must be a valid dotted IPv4;
// secondary may be NULL/empty. Persists to the saved network profile, which
// makes the console reconnect: the connection drops for a few seconds right
// after a success.
NetworkResult network_set_dns(bool automatic, const char *primary, const char *secondary,
                              char *err, size_t errsz);

// The HTTP status for a NetworkResult.
static inline int network_http_status(NetworkResult r) {
    return r == NETWORK_OK ? 200 : r == NETWORK_INVALID ? 400
         : r == NETWORK_UNAVAILABLE ? 503 : 500;
}

// Buffer size for network_dns_from_json's primary/secondary: json_get_string
// needs headroom (it reserves a few bytes for escape expansion), so a 16-byte
// buffer would reject a full 15-char dotted IPv4. network_set_dns validates
// the actual format.
#define NETWORK_DNS_ARG_SIZE 64

// {"automatic":true} | {"primary":"1.2.3.4","secondary":"5.6.7.8"}, as REST
// and MCP both take it. primary and secondary are NETWORK_DNS_ARG_SIZE bytes,
// "" when absent. False with *err set when neither automatic nor primary is
// given.
bool network_dns_from_json(const JsonDoc *doc, int obj, bool *automatic, char *primary,
                           char *secondary, const char **err);
