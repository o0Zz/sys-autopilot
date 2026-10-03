// Host tests for launch_nro's nxlink client (features/nro/nro.c), against a
// fake hbmenu netloader in a child process that speaks the server side of the
// protocol and checks the zlib stream it receives.
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "features/nro/nro.h"

#define NRO_DIR FAKE_SD_NRO "/switch/app"
#define NRO_PATH NRO_DIR "/app.nro"
#define NRO_SIZE 40000 // three stored blocks

static uint8_t g_nro[NRO_SIZE];

static void write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static size_t read_file(const char *path, void *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool recv_all(int fd, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

// Parses a zlib stream of stored blocks. 1: complete and valid, 0: needs
// more input, -1: invalid.
static int parse_zlib(const uint8_t *s, size_t n, uint8_t *out, size_t *out_len) {
    if (n < 2)
        return 0;
    if (s[0] != 0x78 || ((s[0] << 8) | s[1]) % 31 != 0)
        return -1;
    size_t pos = 2, o = 0;
    for (;;) {
        if (n - pos < 5)
            return 0;
        int final = s[pos] & 1;
        if ((s[pos] >> 1) != 0)
            return -1; // not stored
        unsigned len = s[pos + 1] | s[pos + 2] << 8;
        unsigned nlen = s[pos + 3] | s[pos + 4] << 8;
        if ((len ^ 0xFFFF) != nlen)
            return -1;
        pos += 5;
        if (n - pos < len)
            return 0;
        memcpy(out + o, s + pos, len);
        o += len;
        pos += len;
        if (final)
            break;
    }
    if (n - pos < 4)
        return 0;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < o; i++) {
        a = (a + out[i]) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t want = (uint32_t)s[pos] << 24 | s[pos + 1] << 16 | s[pos + 2] << 8 | s[pos + 3];
    if (want != ((b << 16) | a))
        return -1;
    *out_len = o;
    return 1;
}

// The fake netloader: serves one connection the way nx-hbmenu does, then
// exits 0 if everything it received was well-formed.
static void serve_once(int lfd, int32_t first_response) {
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        _exit(10);
    int32_t namelen, filelen;
    char name[256];
    if (!recv_all(c, &namelen, 4) || namelen <= 0 || namelen >= (int32_t)sizeof(name) ||
        !recv_all(c, name, (size_t)namelen) || !recv_all(c, &filelen, 4))
        _exit(11);
    name[namelen] = '\0';
    if (send(c, &first_response, 4, 0) != 4)
        _exit(12);
    if (first_response != 0)
        _exit(0);

    // Like hbmenu: recreate the file at once, then fill it.
    char path[512];
    snprintf(path, sizeof(path), FAKE_SD_NRO "/switch/%s", name);
    FILE *out = fopen(path, "wb");
    if (!out)
        _exit(13);

    static uint8_t stream[NRO_SIZE + 1024], data[NRO_SIZE + 1024];
    size_t have = 0, data_len = 0;
    int state = 0;
    while (state == 0) {
        uint32_t chunk;
        if (!recv_all(c, &chunk, 4) || chunk == 0 || chunk > 16 * 1024 ||
            have + chunk > sizeof(stream) || !recv_all(c, stream + have, chunk))
            _exit(14);
        have += chunk;
        state = parse_zlib(stream, have, data, &data_len);
    }
    if (state < 0 || data_len != (size_t)filelen)
        _exit(15);
    fwrite(data, 1, data_len, out);
    fclose(out);

    int32_t ok = 0, cmdlen;
    char args[1024];
    if (send(c, &ok, 4, 0) != 4 || !recv_all(c, &cmdlen, 4) || cmdlen < 0 ||
        cmdlen > (int32_t)sizeof(args) || (cmdlen && !recv_all(c, args, (size_t)cmdlen)))
        _exit(16);
    write_file(FAKE_SD_NRO "/args.bin", args, (size_t)cmdlen);
    close(c);
    _exit(0);
}

static int listen_netloader(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(NRO_NETLOADER_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
    assert(listen(fd, 1) == 0);
    return fd;
}

// Runs nro_launch against a fake netloader answering first_response.
static bool launch_with_server(int32_t first_response, const char *args, size_t args_len,
                               char *err, size_t errsz) {
    int lfd = listen_netloader();
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
        serve_once(lfd, first_response);
    close(lfd);
    static char buf[NRO_BUF_SIZE];
    bool ok = nro_launch("/switch/app/app.nro", args, args_len, buf, err, errsz);
    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return ok;
}

static void assert_nro_intact(void) {
    static uint8_t back[NRO_SIZE + 16];
    assert(read_file(NRO_PATH, back, sizeof(back)) == NRO_SIZE);
    assert(memcmp(back, g_nro, NRO_SIZE) == 0);
    struct stat st;
    assert(stat(NRO_PATH ".launch", &st) != 0); // temp copy gone
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    assert(system("rm -rf " FAKE_SD_NRO " && mkdir -p " NRO_DIR) == 0);
    for (size_t i = 0; i < NRO_SIZE; i++)
        g_nro[i] = (uint8_t)(i * 7 + (i >> 8));
    write_file(NRO_PATH, g_nro, NRO_SIZE);

    char err[160];
    static const char kArgs[] = "--test\0two words";
    assert(launch_with_server(0, kArgs, sizeof(kArgs), err, sizeof(err)));
    assert_nro_intact();
    char args[64];
    assert(read_file(FAKE_SD_NRO "/args.bin", args, sizeof(args)) == sizeof(kArgs));
    assert(memcmp(args, kArgs, sizeof(kArgs)) == 0);
    printf("launch ok\n");

    // hbmenu refusing the file leaves the NRO where it was.
    assert(!launch_with_server(-3, NULL, 0, err, sizeof(err)));
    assert(strstr(err, "refused"));
    assert_nro_intact();
    printf("refused ok\n");

    // No netloader listening.
    static char buf[NRO_BUF_SIZE];
    assert(!nro_launch("/switch/app/app.nro", NULL, 0, buf, err, sizeof(err)));
    assert(strstr(err, "press Y"));
    assert_nro_intact();
    printf("not listening ok\n");

    assert(!nro_launch("/apps/app.nro", NULL, 0, buf, err, sizeof(err)));
    assert(strstr(err, "under /switch/"));
    assert(!nro_launch("/switch/app/app.txt", NULL, 0, buf, err, sizeof(err)));
    assert(!nro_launch("/switch/app/missing.nro", NULL, 0, buf, err, sizeof(err)));
    assert(strstr(err, "no such file"));
    assert(!nro_launch("/switch/../x.nro", NULL, 0, buf, err, sizeof(err)));
    printf("validation ok\n");

    printf("all nro tests passed\n");
    return 0;
}
