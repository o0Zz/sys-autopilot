#include "features/network/network.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Parse a dotted-quad IPv4 string (a.b.c.d -> out[0..3]). Returns false on
// malformed input. Avoids inet_pton so there's no dependency on the socket
// layer being initialized.
static bool str_to_ip(const char *s, uint8_t out[4]) {
    if (!s || !s[0]) return false;
    unsigned vals[4];
    int n = 0;
    const char *p = s;
    for (; n < 4; n++) {
        if (*p < '0' || *p > '9') return false;
        unsigned v = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (unsigned)(*p - '0');
            if (++digits > 3 || v > 255) return false;
            p++;
        }
        vals[n] = v;
        if (n < 3) { if (*p != '.') return false; p++; }
    }
    if (*p != '\0') return false; // trailing junk
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)vals[i];
    return true;
}

static NetworkResult apply_dns(bool automatic, const uint8_t primary[4],
                               const uint8_t secondary[4], char *err, size_t errsz);

NetworkResult network_set_dns(bool automatic, const char *primary, const char *secondary,
                              char *err, size_t errsz) {
    uint8_t p[4] = {0}, s[4] = {0};
    if (!automatic) {
        if (!str_to_ip(primary, p)) {
            snprintf(err, errsz, "invalid primary DNS (expected a dotted IPv4 address)");
            return NETWORK_INVALID;
        }
        if (secondary && secondary[0] && !str_to_ip(secondary, s)) {
            snprintf(err, errsz, "invalid secondary DNS (expected a dotted IPv4 address)");
            return NETWORK_INVALID;
        }
    }
    return apply_dns(automatic, p, s, err, errsz);
}

#ifdef __SWITCH__
#include <switch.h>
#include "core/log.h"

// Saving a profile makes nifm reconnect with it. For a few seconds after that
// (about 2 s for manual DNS, 6 s for automatic, measured), nifm reports no
// active connection, and neither the effective DNS nor the profile can be read.
static NetworkResult reconnecting(char *err, size_t errsz, Result rc) {
    snprintf(err, errsz,
             "no active network connection (0x%x); after a DNS change the console "
             "reconnects for a few seconds, retry then", rc);
    return NETWORK_UNAVAILABLE;
}

NetworkResult network_get_dns(DnsConfig *out, char *err, size_t errsz) {
    memset(out, 0, sizeof(*out));
    // nifmGetCurrentIpConfigInfo works with nifm:u (already initialized at boot
    // for mDNS). It reports the *effective* DNS regardless of auto/manual.
    u32 addr = 0, mask = 0, gw = 0, dns1 = 0, dns2 = 0;
    Result rc = nifmGetCurrentIpConfigInfo(&addr, &mask, &gw, &dns1, &dns2);
    if (R_FAILED(rc))
        return reconnecting(err, errsz, rc);

    // Whether DNS is manual comes from the saved profile. Do not guess when it
    // cannot be read: "automatic" next to manual servers is worse than an error.
    NifmNetworkProfileData prof;
    rc = nifmGetCurrentNetworkProfile(&prof);
    if (R_FAILED(rc))
        return reconnecting(err, errsz, rc);
    out->is_automatic = prof.ip_setting_data.dns_setting.is_automatic != 0;

    // dns1/dns2 are network byte order (a.b.c.d in memory order).
    const u8 *b1 = (const u8 *)&dns1, *b2 = (const u8 *)&dns2;
    snprintf(out->primary, sizeof(out->primary), "%u.%u.%u.%u",
             b1[0], b1[1], b1[2], b1[3]);
    snprintf(out->secondary, sizeof(out->secondary), "%u.%u.%u.%u",
             b2[0], b2[1], b2[2], b2[3]);
    return NETWORK_OK;
}

static NetworkResult apply_dns(bool automatic, const uint8_t primary[4],
                               const uint8_t secondary[4], char *err, size_t errsz) {
    // Setting a profile requires an nifm:a (admin) session. nifm is opened as
    // Admin once at boot (netif_init); libnx's refcounted nifmInitialize ignores
    // the service type on later calls, so we must NOT re-init/exit here (that
    // would either no-op over a wrong session or close the boot session). We
    // rely on the existing admin session.
    NifmNetworkProfileData prof;
    Result rc = nifmGetCurrentNetworkProfile(&prof);
    if (R_FAILED(rc))
        return reconnecting(err, errsz, rc);

    prof.ip_setting_data.dns_setting.is_automatic = automatic ? 1 : 0;
    if (!automatic) {
        memcpy(prof.ip_setting_data.dns_setting.primary_dns_server.addr, primary, 4);
        memcpy(prof.ip_setting_data.dns_setting.secondary_dns_server.addr, secondary, 4);
    }

    Uuid uuid = prof.uuid;
    rc = nifmSetNetworkProfile(&prof, &uuid);
    if (R_FAILED(rc)) {
        snprintf(err, errsz, "set profile failed (0x%x)", rc);
        return NETWORK_FAILED;
    }
    LOGI("network", "DNS set to %s (%u.%u.%u.%u/%u.%u.%u.%u)",
         automatic ? "automatic" : "manual", primary[0], primary[1], primary[2],
         primary[3], secondary[0], secondary[1], secondary[2], secondary[3]);
    return NETWORK_OK;
}

#else // !__SWITCH__ : host stubs so the REST/MCP layer links in tests.

NetworkResult network_get_dns(DnsConfig *out, char *err, size_t errsz) {
    (void)out;
    snprintf(err, errsz, "network config unavailable on host");
    return NETWORK_FAILED;
}

static NetworkResult apply_dns(bool automatic, const uint8_t primary[4],
                               const uint8_t secondary[4], char *err, size_t errsz) {
    (void)automatic; (void)primary; (void)secondary;
    snprintf(err, errsz, "network config unavailable on host");
    return NETWORK_FAILED;
}

#endif // __SWITCH__
