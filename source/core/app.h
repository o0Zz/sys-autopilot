#pragma once

#include <stdint.h>

// Version string injected by the Makefile from package.json.
const char *app_version(void);

// Seconds since the sysmodule started.
uint64_t app_uptime_seconds(void);
