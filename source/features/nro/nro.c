// nxlink client: hands an NRO from the SD card to hbmenu's netloader.
//
// The protocol (nx-hbmenu common/netloader.c), all integers 32-bit
// little-endian, the host's own byte order on both ends:
//   -> name length, name (saved as sdmc:/switch/<name>), file length
//   <- 0, or a negative error (-3: hbmenu has no use for that extension)
//   -> the file as one zlib stream, in chunks: length, then up to 16 KiB
//   <- 0 once the stream has ended
//   -> argument length, arguments (NUL-separated)
// hbmenu then closes the connection and starts the NRO.
//
// The zlib stream is made of "stored" deflate blocks: no compression, so no
// zlib, and the transfer never leaves the console anyway.
#include "features/nro/nro.h"
#include "core/http.h"
#include "core/http_server.h"
#include "core/log.h"
#include "features/files/files.h"
#include "platform/netif.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define CONNECT_TIMEOUT_MS 1000
#define IO_TIMEOUT_MS 10000

// Data bytes per stored block. A chunk carries one block, plus the zlib
// header on the first and the checksum on the last, and must fit the 16 KiB
// hbmenu reads a chunk into (ZLIB_CHUNK).
#define BLOCK_DATA 16000
_Static_assert(4 + 2 + 5 + BLOCK_DATA + 4 <= NRO_BUF_SIZE, "NRO chunk does not fit the buffer");
_Static_assert(2 + 5 + BLOCK_DATA + 4 <= 16 * 1024, "NRO chunk too large for hbmenu");

// The NRO is moved here while it is sent: hbmenu recreates (and truncates)
// the original path as soon as the transfer starts.
#define LAUNCH_SUFFIX ".launch"

// How many times (100ms apart) to look for the file hbmenu wrote.
#define NRO_SETTLE_TRIES 30

static const char kPrefix[] = "/switch/";

static int connect_to(uint32_t s_addr) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;
    http_set_nonblocking(fd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NRO_NETLOADER_PORT);
    addr.sin_addr.s_addr = s_addr;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        return fd;
    if (errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    struct pollfd p = { .fd = fd, .events = POLLOUT, .revents = 0 };
    int soerr = 0;
    socklen_t len = sizeof(soerr);
    if (poll(&p, 1, CONNECT_TIMEOUT_MS) <= 0 ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0 || soerr != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Loopback first; the console's LAN address in case its stack refuses that.
static int connect_netloader(void) {
    int fd = connect_to(htonl(INADDR_LOOPBACK));
    uint32_t ip;
    if (fd < 0 && netif_current_ipv4(&ip) && ip != htonl(INADDR_LOOPBACK))
        fd = connect_to(ip);
    return fd;
}

static bool recv_all(int fd, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n > 0) {
            p += n;
            len -= (size_t)n;
            continue;
        }
        if (n == 0)
            return false;
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return false;
        struct pollfd pf = { .fd = fd, .events = POLLIN, .revents = 0 };
        if (poll(&pf, 1, IO_TIMEOUT_MS) <= 0)
            return false;
    }
    return true;
}

static bool send_i32(int fd, int32_t v) {
    return http_write_all(fd, &v, sizeof(v));
}

static bool recv_i32(int fd, int32_t *v) {
    return recv_all(fd, v, sizeof(*v));
}

// Streams the file as a zlib stream of stored blocks, one block per chunk.
static bool send_stream(int fd, FILE *f, long long size, char *buf, char *err, size_t errsz) {
    uint32_t a = 1, b = 0; // Adler-32
    long long left = size;
    for (bool first = true; left > 0; first = false) {
        size_t n = left > BLOCK_DATA ? BLOCK_DATA : (size_t)left;
        bool last = (long long)n == left;
        uint8_t *c = (uint8_t *)buf + 4;
        size_t pos = 0;
        if (first) {
            c[pos++] = 0x78; // deflate, 32K window
            c[pos++] = 0x01; // no dictionary, check bits
        }
        c[pos++] = last ? 1 : 0; // BFINAL, BTYPE 00 (stored)
        c[pos++] = (uint8_t)n;
        c[pos++] = (uint8_t)(n >> 8);
        c[pos++] = (uint8_t)~n;
        c[pos++] = (uint8_t)(~n >> 8);
        if (fread(c + pos, 1, n, f) != n) {
            snprintf(err, errsz, "reading the NRO failed");
            return false;
        }
        for (size_t i = 0; i < n; i++) {
            a = (a + c[pos + i]) % 65521;
            b = (b + a) % 65521;
        }
        pos += n;
        if (last) {
            uint32_t sum = (b << 16) | a;
            c[pos++] = (uint8_t)(sum >> 24);
            c[pos++] = (uint8_t)(sum >> 16);
            c[pos++] = (uint8_t)(sum >> 8);
            c[pos++] = (uint8_t)sum;
        }
        uint32_t chunk = (uint32_t)pos;
        memcpy(buf, &chunk, 4);
        if (!http_write_all(fd, buf, 4 + pos)) {
            snprintf(err, errsz, "connection to hbmenu lost during the transfer");
            return false;
        }
        left -= (long long)n;
    }
    return true;
}

static bool converse(int fd, FILE *f, const char *name, long long size, const char *args,
                     size_t args_len, char *buf, char *err, size_t errsz) {
    int32_t resp = 0;
    int32_t namelen = (int32_t)strlen(name);
    if (!send_i32(fd, namelen) || !http_write_all(fd, name, (size_t)namelen) ||
        !send_i32(fd, (int32_t)size) || !recv_i32(fd, &resp)) {
        snprintf(err, errsz, "hbmenu's netloader did not answer");
        return false;
    }
    if (resp != 0) {
        snprintf(err, errsz, "hbmenu refused the file (error %d)", (int)resp);
        return false;
    }
    if (!send_stream(fd, f, size, buf, err, errsz))
        return false;
    if (!recv_i32(fd, &resp) || resp != 0) {
        snprintf(err, errsz, "hbmenu did not confirm the transfer");
        return false;
    }
    if (!send_i32(fd, (int32_t)args_len) ||
        (args_len > 0 && !http_write_all(fd, args, args_len))) {
        snprintf(err, errsz, "sending the arguments failed");
        return false;
    }
    return true;
}

bool nro_launch(const char *path, const char *args, size_t args_len, char *buf,
                char *err, size_t errsz) {
    size_t plen = strlen(path);
    size_t prefix = sizeof(kPrefix) - 1;
    if (strncmp(path, kPrefix, prefix) != 0 || plen <= prefix + 4 ||
        strcasecmp(path + plen - 4, ".nro") != 0) {
        snprintf(err, errsz, "path must be an .nro under /switch/ (where hbmenu's netloader saves it)");
        return false;
    }
    if (args_len > NRO_ARGS_MAX) {
        snprintf(err, errsz, "arguments too long (max %d bytes)", NRO_ARGS_MAX);
        return false;
    }
    char fspath[768];
    const char *rerr = NULL;
    if (!files_resolve(path, fspath, sizeof(fspath), &rerr)) {
        snprintf(err, errsz, "%s", rerr);
        return false;
    }
    struct stat st;
    if (stat(fspath, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(err, errsz, "no such file");
        return false;
    }
    if (st.st_size <= 0 || (long long)st.st_size > 0x7fffffffLL) {
        snprintf(err, errsz, "unexpected file size");
        return false;
    }

    int fd = connect_netloader();
    if (fd < 0) {
        snprintf(err, errsz, "hbmenu's netloader is not listening: open the Homebrew Menu, "
                             "press Y to start the netloader, then retry");
        return false;
    }

    char tmp[800];
    snprintf(tmp, sizeof(tmp), "%s" LAUNCH_SUFFIX, fspath);
    remove(tmp);
    if (rename(fspath, tmp) != 0) {
        close(fd);
        snprintf(err, errsz, "could not move the NRO aside for the transfer");
        return false;
    }

    FILE *f = fopen(tmp, "rb");
    bool ok = f != NULL;
    if (!ok)
        snprintf(err, errsz, "could not open the NRO");
    else
        ok = converse(fd, f, path + prefix, (long long)st.st_size, args, args_len, buf, err,
                      errsz);
    if (f)
        fclose(f);
    close(fd);

    // On success hbmenu has written the NRO back in place, and the copy goes.
    // The new file cannot even be stat()ed while hbmenu still holds it open
    // (seen on hardware: hbmenu closes it only as it exits), so give it a
    // moment before deciding it is not there.
    struct stat now;
    bool written = false;
    for (int tries = 0; ok && tries < NRO_SETTLE_TRIES; tries++) {
        if (stat(fspath, &now) == 0 && now.st_size == st.st_size) {
            written = true;
            break;
        }
        http_server_wait_ms(100);
    }
    if (written) {
        remove(tmp);
    } else {
        // Failed, or this hbmenu saved it elsewhere: put the original back.
        remove(fspath);
        if (rename(tmp, fspath) != 0)
            LOGE("nro", "could not restore %s (errno %d); the original is %s", fspath, errno, tmp);
    }
    if (ok)
        LOGI("nro", "sent %s to hbmenu (%lld bytes)", path, (long long)st.st_size);
    else
        LOGW("nro", "launch %s failed: %s", path, err);
    return ok;
}

bool nro_args_from_json(const JsonDoc *doc, int obj, char *args, size_t *args_len,
                        const char **err) {
    *args_len = 0;
    int arr = json_obj_get(doc, obj, "args");
    for (int i = 0; arr >= 0 && i < json_arr_len(doc, arr); i++) {
        char *dst = args + *args_len;
        if (!json_get_string(doc, json_arr_get(doc, arr, i), dst, NRO_ARGS_MAX - *args_len)) {
            *err = "'args' must be strings, 1 KB in all";
            return false;
        }
        *args_len += strlen(dst) + 1;
    }
    return true;
}
