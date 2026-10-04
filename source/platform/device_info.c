#include "platform/device_info.h"

#include <stdio.h>

#ifdef __SWITCH__
#include <switch.h>

// Exosphère (Atmosphère secure monitor) config item exposing the packed
// API version. Not a named SplConfigItem in libnx, so we use the raw value
// like nx.js does. The version is packed in the high bytes of the u64.
#define EXOSPHERE_API_VERSION 65000

static char g_firmware[16];
static char g_atmosphere[16];
static char g_serial[0x18];
static DeviceInfo g_info = {
    .model = "unknown", .firmware = "", .atmosphere = "", .serial = "",
};

static const char *model_name(SetSysProductModel m) {
    switch (m) {
        case SetSysProductModel_Nx:     // Erista
        case SetSysProductModel_Copper:
            return "v1";
        case SetSysProductModel_Iowa:   // Mariko (revised)
        case SetSysProductModel_Calcio:
            return "v2";
        case SetSysProductModel_Hoag:   // Mariko Lite
            return "lite";
        case SetSysProductModel_Aula:   // Mariko OLED
            return "oled";
        default:
            return "unknown";
    }
}

void device_info_init(void) {
    // OS firmware version (cached for libnx with hosversionSet), product model
    // and serial number via set:sys (already granted in the NPDM).
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));

        SetSysProductModel pm = SetSysProductModel_Invalid;
        if (R_SUCCEEDED(setsysGetProductModel(&pm)))
            g_info.model = model_name(pm);

        SetSysSerialNumber sn = {0};
        if (R_SUCCEEDED(setsysGetSerialNumber(&sn)) && sn.number[0] != '\0') {
            snprintf(g_serial, sizeof(g_serial), "%s", sn.number);
            g_info.serial = g_serial;
        }
        setsysExit();
    }

    u32 hv = hosversionGet();
    if (hv != 0) {
        snprintf(g_firmware, sizeof(g_firmware), "%u.%u.%u",
                 HOSVER_MAJOR(hv), HOSVER_MINOR(hv), HOSVER_MICRO(hv));
        g_info.firmware = g_firmware;
    }

    // Atmosphère version via the SPL service (requires spl: in the NPDM).
    // Only meaningful when running under Atmosphère; best-effort otherwise.
    if (hosversionIsAtmosphere() && R_SUCCEEDED(splInitialize())) {
        u64 packed = 0;
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)EXOSPHERE_API_VERSION, &packed))) {
            u8 major = (packed >> 56) & 0xFF;
            u8 minor = (packed >> 48) & 0xFF;
            u8 micro = (packed >> 40) & 0xFF;
            snprintf(g_atmosphere, sizeof(g_atmosphere), "%u.%u.%u",
                     major, minor, micro);
            g_info.atmosphere = g_atmosphere;
        }
        splExit();
    }
}

const DeviceInfo *device_info_get(void) {
    return &g_info;
}

#else // host / test build: no Switch services available.

const DeviceInfo *device_info_get(void) {
    static const DeviceInfo info = {
        .model = "unknown",
        .firmware = "",
        .atmosphere = "",
        .serial = "TEST0000000001", // deterministic for host tests
    };
    return &info;
}

#endif
