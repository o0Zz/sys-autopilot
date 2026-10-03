#pragma once

#include <stdbool.h>
#include <stddef.h>

// Launching homebrew (.nro) without driving the Homebrew Menu by hand.
//
// Only the Homebrew Menu (hbmenu) can start an NRO, so this talks to its
// netloader, the same way `nxlink` does from a PC: the agent opens hbmenu and
// presses Y once, then each launch streams the NRO from the SD card to
// hbmenu over the console's own loopback interface. hbmenu writes what it
// receives to sdmc:/switch/<name> and starts it, so the NRO must already live
// under /switch/: it is sent back to its own path and argv[0] stays right.

// hbmenu's netloader port (NXLINK_SERVER_PORT in libnx).
#ifndef NRO_NETLOADER_PORT
#define NRO_NETLOADER_PORT 28280
#endif

#define NRO_ARGS_MAX 1024 // bytes of NUL-separated arguments
#define NRO_BUF_SIZE 0x4000

// Sends the NRO at `path` ("/switch/...nro") with the NUL-separated
// arguments args[0..args_len), using buf (NRO_BUF_SIZE bytes) as scratch.
// Returns true once hbmenu has the file and its arguments; false with a
// message in err otherwise. The NRO on the SD card is left as it was on any
// failure.
bool nro_launch(const char *path, const char *args, size_t args_len, char *buf,
                char *err, size_t errsz);
