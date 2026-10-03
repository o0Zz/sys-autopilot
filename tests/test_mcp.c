// Host end-to-end tests for the MCP endpoint: real HTTP request parsing and
// routing over a socketpair, with the tools every feature registers and
// input/screen stubbed (stubs.c).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "util/base64.h"
#include "features/input/buttons.h"
#include "core/config.h"
#include "features/files/files.h"
#include "core/http.h"
#include "core/http_server.h"
#include "features/files/files_http.h"
#include "features/files/files_mcp.h"
#include "features/input/input_mcp.h"
#include "features/network/network_mcp.h"
#include "features/oauth/oauth_mcp.h"
#include "features/power/power_mcp.h"
#include "features/process/process_mcp.h"
#include "features/screen/screen_mcp.h"
#include "features/settings/settings_mcp.h"
#include "features/status/status_mcp.h"
#include "features/titles/titles_mcp.h"
#include "features/mcp/mcp_server.h"
#include "features/oauth/oauth.h"
#include "platform/power.h"
#include "features/process/process.h"
#include "features/crash/crash_http.h"
#include "features/crash/crash_mcp.h"
#include "features/screen/screen_http.h"
#include "features/process/process_http.h"
#include "util/jpeg.h"
#include "jpeg_fixtures.h"

extern uint64_t stub_tap_mask;
extern int stub_tap_duration;
extern int stub_tap_count;
extern int stub_touch_x, stub_touch_y, stub_touch_duration;
extern int stub_swipe_from_x, stub_swipe_from_y, stub_swipe_to_x, stub_swipe_to_y;
extern int stub_touch_count;
extern const uint8_t *stub_frames[4];
extern size_t stub_frame_lens[4];
extern int stub_nframes;
extern int stub_captures;
extern char stub_typed[512];
extern int stub_keyboard_layout;
extern int stub_key_ms;

static const Config kNoAuth;

// Issues one POST /mcp request with the given JSON body; returns the raw HTTP
// response in a static buffer.
static const char *do_rpc(const char *body) {
    static char resp[256 * 1024];
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    // The handler writes the entire response before the test reads it back;
    // grow the buffers so large payloads (tools/list, screenshots) don't
    // deadlock the single-threaded harness.
    int bufsz = 512 * 1024;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));

    char req[64 * 1024];
    int rn = snprintf(req, sizeof(req),
                      "POST /mcp HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s",
                      strlen(body), body);
    assert(rn > 0 && write(sv[1], req, (size_t)rn) == rn);
    shutdown(sv[1], SHUT_WR);

    static HttpRequest hreq;
    assert(http_read_request(sv[0], &hreq));
    assert(strcmp(hreq.path, "/mcp") == 0);
    http_server_dispatch(&kNoAuth, &hreq);
    close(sv[0]);

    size_t total = 0;
    ssize_t n;
    while ((n = read(sv[1], resp + total, sizeof(resp) - 1 - total)) > 0)
        total += (size_t)n;
    resp[total] = '\0';
    close(sv[1]);
    return resp;
}

static void test_initialize(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                           "\"params\":{\"protocolVersion\":\"2025-03-26\","
                           "\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"0\"}}}");
    assert(strstr(r, "HTTP/1.1 200"));
    assert(strstr(r, "\"id\":1"));
    assert(strstr(r, "\"protocolVersion\":\"2025-03-26\"")); // echoed known version
    assert(strstr(r, "\"serverInfo\":{\"name\":\"sys-autopilot\""));
    assert(strstr(r, "\"tools\":{}"));
    printf("initialize ok\n");
}

static void test_notification(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    assert(strstr(r, "HTTP/1.1 202"));
    printf("notification ok\n");
}

static void test_ping_and_errors(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"ping\"}");
    assert(strstr(r, "\"id\":\"abc\""));
    assert(strstr(r, "\"result\":{}"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"bogus/method\"}");
    assert(strstr(r, "\"error\":{\"code\":-32601"));

    r = do_rpc("this is not json");
    assert(strstr(r, "-32700"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"no_such_tool\"}}");
    assert(strstr(r, "\"error\":{\"code\":-32602"));
    printf("ping/errors ok\n");
}

static void test_tools_list(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}");
    assert(strstr(r, "\"tap_buttons\""));
    assert(strstr(r, "\"tap_screen\""));
    assert(strstr(r, "\"swipe_screen\""));
    assert(strstr(r, "\"upload_file\""));
    assert(strstr(r, "\"move_file\""));
    assert(strstr(r, "\"hash_file\""));
    assert(strstr(r, "\"screenshot\""));
    assert(strstr(r, "\"inputSchema\""));
    assert(strstr(r, "\"wait_for_screen\""));
    assert(strstr(r, "\"wait_for_process\""));
    assert(strstr(r, "\"wait_for_file\""));
    assert(strstr(r, "\"type_text\""));
    assert(strstr(r, "\"list_crash_reports\""));
    printf("tools/list ok\n");
}

static void test_tap_buttons(void) {
    stub_tap_count = 0;
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_buttons\",\"arguments\":"
                           "{\"buttons\":[\"A\",\"home\"],\"durationMs\":5}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(stub_tap_count == 1);
    assert(stub_tap_mask == (BTN_A | BTN_HOME));
    assert(stub_tap_duration == 5);

    // Unknown button -> in-band tool error.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"tap_buttons\",\"arguments\":{\"buttons\":[\"Q\"]}}}");
    assert(strstr(r, "\"isError\":true"));
    printf("tap_buttons ok\n");
}

static void test_tap_sequence(void) {
    stub_tap_count = 0;
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_sequence\",\"arguments\":{\"taps\":["
                           "{\"buttons\":[\"RIGHT\"],\"delayAfterMs\":1},"
                           "{\"buttons\":[\"RIGHT\"],\"delayAfterMs\":1},"
                           "{\"buttons\":[\"A\"],\"durationMs\":2}]}}}");
    assert(strstr(r, "performed 3 taps"));
    assert(stub_tap_count == 3);
    assert(stub_tap_mask == BTN_A);
    printf("tap_sequence ok\n");
}

static void test_screenshot(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"screenshot\",\"arguments\":{}}}");
    // base64("FAKEJPEGDATA")
    char expect[64];
    size_t n = b64_encode((const uint8_t *)"FAKEJPEGDATA", 12, expect);
    expect[n] = '\0';
    assert(strstr(r, "\"type\":\"image\""));
    assert(strstr(r, expect));
    assert(strstr(r, "\"mimeType\":\"image/jpeg\""));
    printf("screenshot ok\n");
}

static void test_upload_and_files(void) {
    system("rm -rf " FAKE_SD " && mkdir -p " FAKE_SD);

    // Upload with content BEFORE path (key-order robustness), then read back.
    const char *payload = "Hello, upload!\nLine two.";
    char b64[128];
    size_t n = b64_encode((const uint8_t *)payload, strlen(payload), b64);
    b64[n] = '\0';

    char body[512];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"upload_file\",\"arguments\":"
             "{\"content\":\"%s\",\"path\":\"/up/test.txt\"}}}", b64);
    const char *r = do_rpc(body);
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "wrote 24 bytes"));

    FILE *f = fopen(FAKE_SD "/up/test.txt", "rb");
    assert(f);
    char readback[64] = {0};
    size_t got = fread(readback, 1, sizeof(readback), f);
    fclose(f);
    assert(got == strlen(payload) && memcmp(readback, payload, got) == 0);

    // read_file round-trip (content contains a quote + newline after this write).
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"read_file\",\"arguments\":{\"path\":\"/up/test.txt\"}}}");
    assert(strstr(r, "Hello, upload!\\nLine two."));

    // read_file with negative offset (tail).
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"read_file\",\"arguments\":"
               "{\"path\":\"/up/test.txt\",\"offset\":-9}}}");
    assert(strstr(r, "Line two."));
    assert(!strstr(r, "Hello"));

    // list_directory.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"list_directory\",\"arguments\":{\"path\":\"/up\"}}}");
    assert(strstr(r, "test.txt"));
    assert(strstr(r, "\\\"type\\\":\\\"file\\\"")); // listing JSON is escaped text

    // delete_file + temp cleanup check.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"delete_file\",\"arguments\":{\"path\":\"/up/test.txt\"}}}");
    assert(strstr(r, "deleted"));
    struct stat st;
    assert(stat(FAKE_SD "/up/test.txt", &st) != 0);
    assert(stat(MCP_UPLOAD_TMP, &st) != 0); // no leftover temp file

    // Invalid base64 content -> tool error, no file created.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"upload_file\",\"arguments\":"
               "{\"content\":\"!!notbase64!!\",\"path\":\"/up/bad.bin\"}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(stat(FAKE_SD "/up/bad.bin", &st) != 0);
    assert(stat(MCP_UPLOAD_TMP, &st) != 0);

    // Path traversal rejected.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"read_file\",\"arguments\":{\"path\":\"/../etc/passwd\"}}}");
    assert(strstr(r, "\"isError\":true"));
    printf("upload/files ok\n");
}

// Issues one /files request through the router; returns the raw HTTP response.
static const char *do_files(const char *method, const char *target) {
    static char resp[4096];
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    char req[1024];
    int rn = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\n\r\n", method, target);
    assert(rn > 0 && write(sv[1], req, (size_t)rn) == rn);
    shutdown(sv[1], SHUT_WR);

    static HttpRequest hreq;
    assert(http_read_request(sv[0], &hreq));
    http_server_dispatch(&kNoAuth, &hreq);
    close(sv[0]);

    size_t total = 0;
    ssize_t n;
    while ((n = read(sv[1], resp + total, sizeof(resp) - 1 - total)) > 0)
        total += (size_t)n;
    resp[total] = '\0';
    close(sv[1]);
    return resp;
}

static void test_move_file(void) {
    system("rm -rf " FAKE_SD " && mkdir -p " FAKE_SD "/mv");
    FILE *f = fopen(FAKE_SD "/mv/a.txt", "wb");
    assert(f && fputs("abc", f) >= 0);
    fclose(f);
    f = fopen(FAKE_SD "/mv/taken.txt", "wb");
    assert(f);
    fclose(f);
    struct stat st;

    // Rename within the same directory.
    const char *r = do_files("POST", "/files/move?path=/mv/a.txt&to=/mv/b.txt");
    assert(strncmp(r, "HTTP/1.1 200 ", 13) == 0);
    assert(strstr(r, "\"moved\":\"/mv/a.txt\",\"to\":\"/mv/b.txt\""));
    assert(stat(FAKE_SD "/mv/a.txt", &st) != 0);
    assert(stat(FAKE_SD "/mv/b.txt", &st) == 0 && st.st_size == 3);

    // Move into a directory that does not exist yet: parents are created.
    r = do_files("POST", "/files/move?path=/mv/b.txt&to=/mv/sub/deep/c.txt");
    assert(strncmp(r, "HTTP/1.1 200 ", 13) == 0);
    assert(stat(FAKE_SD "/mv/sub/deep/c.txt", &st) == 0);

    // Directories move too.
    r = do_files("POST", "/files/move?path=/mv/sub&to=/mv/renamed");
    assert(strncmp(r, "HTTP/1.1 200 ", 13) == 0);
    assert(stat(FAKE_SD "/mv/renamed/deep/c.txt", &st) == 0);

    // Never overwrites an existing destination.
    r = do_files("POST", "/files/move?path=/mv/renamed/deep/c.txt&to=/mv/taken.txt");
    assert(strncmp(r, "HTTP/1.1 409 ", 13) == 0);
    assert(stat(FAKE_SD "/mv/renamed/deep/c.txt", &st) == 0);

    // Missing source, missing 'to', and traversal in 'to'.
    r = do_files("POST", "/files/move?path=/mv/nope&to=/mv/x");
    assert(strncmp(r, "HTTP/1.1 404 ", 13) == 0);
    r = do_files("POST", "/files/move?path=/mv/taken.txt");
    assert(strncmp(r, "HTTP/1.1 400 ", 13) == 0);
    r = do_files("POST", "/files/move?path=/mv/taken.txt&to=/../escape");
    assert(strncmp(r, "HTTP/1.1 400 ", 13) == 0);
    assert(stat(FAKE_SD "/mv/taken.txt", &st) == 0);
    printf("move ok\n");
}

static void test_hash_file(void) {
    system("rm -rf " FAKE_SD " && mkdir -p " FAKE_SD);

    // Write a known file directly (avoids depending on upload ordering here).
    const char *payload = "Hello, upload!\nLine two.";
    const char *expect_hex =
        "0266a67eaf1212ea81e14133d35c8df97d980c17c48ce4ada3c569308e2e2e4e";
    FILE *f = fopen(FAKE_SD "/h.txt", "wb");
    assert(f);
    fwrite(payload, 1, strlen(payload), f);
    fclose(f);

    // No 'expected': returns the digest + size, no 'matched' field.
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"hash_file\",\"arguments\":{\"path\":\"/h.txt\"}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "\\\"algorithm\\\":\\\"sha256\\\""));
    assert(strstr(r, expect_hex));
    assert(strstr(r, "\\\"size\\\":24"));
    assert(!strstr(r, "matched"));

    // Correct 'expected' -> matched:true.
    char body[256];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"hash_file\",\"arguments\":"
             "{\"path\":\"/h.txt\",\"expected\":\"%s\"}}}", expect_hex);
    r = do_rpc(body);
    assert(strstr(r, "\\\"matched\\\":true"));

    // Uppercase 'expected' still matches (case-insensitive).
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"hash_file\",\"arguments\":"
             "{\"path\":\"/h.txt\",\"expected\":\"0266A67EAF1212EA81E14133D35C8DF9"
             "7D980C17C48CE4ADA3C569308E2E2E4E\"}}}");
    r = do_rpc(body);
    assert(strstr(r, "\\\"matched\\\":true"));

    // Wrong 'expected' -> matched:false (still not an error).
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"hash_file\",\"arguments\":"
               "{\"path\":\"/h.txt\",\"expected\":\"deadbeef\"}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "\\\"matched\\\":false"));

    // Missing file -> tool error.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":44,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"hash_file\",\"arguments\":{\"path\":\"/nope.txt\"}}}");
    assert(strstr(r, "\"isError\":true"));

    // Path traversal rejected.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":45,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"hash_file\",\"arguments\":{\"path\":\"/../etc/passwd\"}}}");
    assert(strstr(r, "\"isError\":true"));

    printf("hash_file ok\n");
}

static void test_input_with_screenshot(void) {
    // Input + screenshot in a single round trip: [text, image] content.
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_buttons\",\"arguments\":"
                           "{\"buttons\":[\"A\"],\"durationMs\":1,"
                           "\"screenshot\":true,\"screenshotDelayMs\":1}}}");
    char expect[64];
    size_t n = b64_encode((const uint8_t *)"FAKEJPEGDATA", 12, expect);
    expect[n] = '\0';
    assert(strstr(r, "\"type\":\"text\",\"text\":\"ok\""));
    assert(strstr(r, "\"type\":\"image\""));
    assert(strstr(r, expect));
    assert(strstr(r, "\"isError\":false"));

    // Without the flag: text only, no image block.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"tap_buttons\",\"arguments\":"
               "{\"buttons\":[\"A\"],\"durationMs\":1}}}");
    assert(!strstr(r, "\"type\":\"image\""));
    printf("input+screenshot ok\n");
}

static void test_create_token(void) {
    remove(OAUTH_TOKENS_PATH);
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"create_token\",\"arguments\":{}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "Authorization: Bearer"));

    // Extract the 64-hex token from "token: <hex>" and validate it works.
    const char *t = strstr(r, "token: ");
    assert(t);
    t += 7;
    char token[80];
    snprintf(token, sizeof(token), "%.64s", t);
    assert(strlen(token) == 64);
    assert(oauth_token_valid(token));

    // Persisted with the tool note.
    FILE *f = fopen(OAUTH_TOKENS_PATH, "rb");
    assert(f);
    char file[512] = {0};
    fread(file, 1, sizeof(file) - 1, f);
    fclose(f);
    assert(strstr(file, token));
    assert(strstr(file, "via create_token tool"));
    printf("create_token ok\n");

    // Revoke it again: token stops validating and leaves the file.
    char body[256];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"revoke_token\",\"arguments\":{\"token\":\"%s\"}}}",
             token);
    r = do_rpc(body);
    assert(strstr(r, "token revoked"));
    assert(!oauth_token_valid(token));
    f = fopen(OAUTH_TOKENS_PATH, "rb");
    assert(f);
    char file2[512] = {0};
    fread(file2, 1, sizeof(file2) - 1, f);
    fclose(f);
    assert(!strstr(file2, token));

    // Revoking an unknown token is an in-band tool error.
    r = do_rpc(body);
    assert(strstr(r, "\"isError\":true"));
    printf("revoke_token ok\n");
}

static void test_power_tools(void) {
    // Tool responds ok and schedules the action for after the response.
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"sleep\",\"arguments\":{}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "unreachable"));
    assert(power_take_scheduled() == PowerAction_Sleep);
    assert(power_take_scheduled() == PowerAction_None); // consumed

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"restart\",\"arguments\":{}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(power_take_scheduled() == PowerAction_Restart);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"power_off\",\"arguments\":{}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(power_take_scheduled() == PowerAction_PowerOff);
    printf("power tools ok\n");
}

static void test_process_tools(void) {
    // The host build of process.c models a single running program, which is
    // enough to drive every branch of the tool layer.
    const char *TID = "690000000000000d";
    char body[256];

    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"process_status\",\"arguments\":{\"titleId\":\"%s\"}}}", TID);
    const char *r = do_rpc(body);
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "is not running"));

    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"process_start\",\"arguments\":{\"titleId\":\"%s\"}}}", TID);
    r = do_rpc(body);
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "launched 690000000000000d"));

    // The list shows it next to a process pm does not track.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":38,\"method\":\"tools/call\",\"params\":"
               "{\"name\":\"process_list\",\"arguments\":{}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "690000000000000d"));
    assert(strstr(r, "(no title id)"));

    // Now visible as running.
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"process_status\",\"arguments\":{\"titleId\":\"%s\"}}}", TID);
    r = do_rpc(body);
    assert(strstr(r, "is running (pid"));

    // Launching twice is an in-band tool error, not a transport error.
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":33,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"process_start\",\"arguments\":{\"titleId\":\"%s\"}}}", TID);
    r = do_rpc(body);
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "is already running (pid"));

    // A 0x prefix is accepted, and restart works from the running state.
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":34,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"process_restart\",\"arguments\":{\"titleId\":\"0x%s\"}}}", TID);
    r = do_rpc(body);
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "restarted 690000000000000d"));

    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":35,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"process_stop\",\"arguments\":{\"titleId\":\"%s\"}}}", TID);
    r = do_rpc(body);
    assert(strstr(r, "\"isError\":false"));

    // Stopping something that is not running fails.
    r = do_rpc(body);
    assert(strstr(r, "\"isError\":true"));

    // Argument validation.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":36,\"method\":\"tools/call\",\"params\":"
               "{\"name\":\"process_start\",\"arguments\":{}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "titleId"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":37,\"method\":\"tools/call\",\"params\":"
               "{\"name\":\"process_start\",\"arguments\":{\"titleId\":\"nothex\"}}}");
    assert(strstr(r, "\"isError\":true"));

    printf("process tools ok\n");
}

// The REST handler is covered by test_move_file above; this checks the tool
// wrapper: argument plumbing, and errors arriving in-band rather than as HTTP
// status codes.
static void test_move_file_tool(void) {
    system("rm -rf " FAKE_SD " && mkdir -p " FAKE_SD "/mv");
    FILE *f = fopen(FAKE_SD "/mv/a.txt", "wb");
    assert(f && fputs("abc", f) >= 0);
    fclose(f);
    struct stat st;

    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"move_file\",\"arguments\":"
                           "{\"path\":\"/mv/a.txt\",\"to\":\"/mv/sub/b.txt\"}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "moved to /mv/sub/b.txt"));
    assert(stat(FAKE_SD "/mv/sub/b.txt", &st) == 0 && st.st_size == 3);

    // Missing source.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"move_file\",\"arguments\":"
               "{\"path\":\"/mv/nope\",\"to\":\"/mv/x\"}}}");
    assert(strstr(r, "\"isError\":true"));

    // Never overwrites an existing destination (moving onto itself is a
    // deliberate no-op, so it is not the case to test here).
    f = fopen(FAKE_SD "/mv/taken.txt", "wb");
    assert(f);
    fclose(f);
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"move_file\",\"arguments\":"
               "{\"path\":\"/mv/sub/b.txt\",\"to\":\"/mv/taken.txt\"}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(stat(FAKE_SD "/mv/sub/b.txt", &st) == 0);

    // Missing destination, and traversal in it.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"move_file\",\"arguments\":{\"path\":\"/mv/sub/b.txt\"}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "'to'"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":44,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"move_file\",\"arguments\":"
               "{\"path\":\"/mv/sub/b.txt\",\"to\":\"/../escape\"}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(stat(FAKE_SD "/mv/sub/b.txt", &st) == 0);
    printf("move_file tool ok\n");
}

static void test_touch_tools(void) {
    stub_touch_count = 0;
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_screen\",\"arguments\":"
                           "{\"x\":640,\"y\":360,\"durationMs\":40}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(stub_touch_count == 1);
    assert(stub_touch_x == 640 && stub_touch_y == 360 && stub_touch_duration == 40);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"swipe_screen\",\"arguments\":"
               "{\"fromX\":200,\"fromY\":600,\"toX\":200,\"toY\":100}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(stub_touch_count == 2);
    assert(stub_swipe_from_x == 200 && stub_swipe_from_y == 600);
    assert(stub_swipe_to_x == 200 && stub_swipe_to_y == 100);

    // Off-panel coordinates are refused before reaching the console.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"tap_screen\",\"arguments\":{\"x\":1280,\"y\":0}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "1279"));
    assert(stub_touch_count == 2);

    // Missing coordinates too.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":33,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"swipe_screen\",\"arguments\":{\"fromX\":0,\"fromY\":0}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(stub_touch_count == 2);
    printf("touch tools ok\n");
}

// --- screenshots, waits, typing, crash reports ----------------------------------

// Issues one request with an optional body through the router; returns the
// raw HTTP response.
static const char *do_http(const char *method, const char *target, const char *body,
                           size_t declared_len) {
    static char resp[64 * 1024];
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    char req[4096];
    size_t blen = body ? strlen(body) : 0;
    int rn = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s",
                      method, target, declared_len ? declared_len : blen, body ? body : "");
    assert(rn > 0 && write(sv[1], req, (size_t)rn) == rn);
    shutdown(sv[1], SHUT_WR);

    static HttpRequest hreq;
    assert(http_read_request(sv[0], &hreq));
    http_server_dispatch(&kNoAuth, &hreq);
    close(sv[0]);

    size_t total = 0;
    ssize_t n;
    while ((n = read(sv[1], resp + total, sizeof(resp) - 1 - total)) > 0)
        total += (size_t)n;
    resp[total] = '\0';
    close(sv[1]);
    return resp;
}

static void set_frames(int n, const uint8_t *a, size_t alen, const uint8_t *b, size_t blen,
                       const uint8_t *c, size_t clen) {
    const uint8_t *f[3] = { a, b, c };
    size_t l[3] = { alen, blen, clen };
    for (int i = 0; i < n; i++) {
        stub_frames[i] = f[i];
        stub_frame_lens[i] = l[i];
    }
    stub_nframes = n;
    stub_captures = 0;
}

// Decodes the (first) image of an MCP result and returns its dimensions.
static void image_size(const char *r, int *w, int *h) {
    const char *p = strstr(r, "\"data\":\"");
    assert(p);
    p += 8;
    const char *e = strchr(p, '"');
    assert(e);
    static char b64[64 * 1024];
    static uint8_t jpeg[48 * 1024];
    assert((size_t)(e - p) < sizeof(b64));
    memcpy(b64, p, (size_t)(e - p));
    b64[e - p] = '\0';
    size_t n = b64_decode(b64, (char *)jpeg, sizeof(jpeg));
    assert(n > 0);
    assert(jpeg_get_size(jpeg, n, w, h));
}

static void test_screenshot_scaled(void) {
    set_frames(1, kC420, sizeof(kC420), NULL, 0, NULL, 0);
    int w, h;
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":70,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"screenshot\",\"arguments\":{\"scale\":0.5}}}");
    assert(strstr(r, "\"type\":\"image\""));
    image_size(r, &w, &h);
    assert(w == 24 && h == 20);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":71,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"screenshot\",\"arguments\":"
               "{\"scale\":0.5,\"crop\":{\"x\":8,\"y\":8,\"width\":16,\"height\":16}}}}");
    image_size(r, &w, &h);
    assert(w == 8 && h == 8);

    // Quality alone re-encodes at full size.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":72,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"screenshot\",\"arguments\":{\"quality\":40}}}");
    image_size(r, &w, &h);
    assert(w == 48 && h == 40);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":73,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"screenshot\",\"arguments\":{\"scale\":0.3}}}");
    assert(strstr(r, "\"isError\":true") && strstr(r, "invalid scale"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":74,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"screenshot\",\"arguments\":{\"crop\":{\"x\":1}}}}");
    assert(strstr(r, "\"isError\":true") && strstr(r, "crop"));

    // Input tools take a scale for their trailing screenshot.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":75,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"tap_buttons\",\"arguments\":{\"buttons\":[\"A\"],"
               "\"durationMs\":1,\"screenshot\":true,\"screenshotDelayMs\":0,"
               "\"screenshotScale\":0.25}}}");
    image_size(r, &w, &h);
    assert(w == 12 && h == 10);

    // REST: scaled JPEG, and a 400 for a bad crop.
    r = do_http("GET", "/screenshot?scale=0.25&quality=70", NULL, 0);
    assert(strncmp(r, "HTTP/1.1 200 OK", 15) == 0 && strstr(r, "Content-Type: image/jpeg"));
    r = do_http("GET", "/screenshot?crop=1,2,3", NULL, 0);
    assert(strncmp(r, "HTTP/1.1 400", 12) == 0 && strstr(r, "crop"));

    stub_nframes = 0;
    printf("scaled screenshots ok\n");
}

static void test_wait_for_screen(void) {
    // Unchanged for two captures, then inverted.
    set_frames(3, kC420, sizeof(kC420), kC420, sizeof(kC420), kInv, sizeof(kInv));
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":80,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"wait_for_screen\",\"arguments\":"
                           "{\"timeoutMs\":5000}}}");
    assert(strstr(r, "screen changed after"));
    assert(stub_captures == 3);

    set_frames(1, kC420, sizeof(kC420), NULL, 0, NULL, 0);
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":81,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_screen\",\"arguments\":{\"timeoutMs\":300}}}");
    assert(strstr(r, "timed out after") && strstr(r, "did not change"));

    // Settles after one change, and attaches a scaled screenshot.
    set_frames(3, kC420, sizeof(kC420), kInv, sizeof(kInv), kInv, sizeof(kInv));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":82,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_screen\",\"arguments\":"
               "{\"until\":\"stable\",\"stableMs\":300,\"timeoutMs\":5000,"
               "\"screenshot\":true,\"screenshotScale\":0.5}}}");
    assert(strstr(r, "screen stable after"));
    int w, h;
    image_size(r, &w, &h);
    assert(w == 24 && h == 20);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":83,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_screen\",\"arguments\":{\"until\":\"never\"}}}");
    assert(strstr(r, "\"isError\":true"));

    set_frames(3, kC420, sizeof(kC420), kInv, sizeof(kInv), kInv, sizeof(kInv));
    r = do_http("GET", "/wait/screen?timeoutMs=3000", NULL, 0);
    assert(strstr(r, "\"met\":true"));

    stub_nframes = 0;
    printf("wait_for_screen ok\n");
}

static void test_wait_for_process(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":90,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"process_start\",\"arguments\":"
                           "{\"titleId\":\"0100000000005678\"}}}");
    assert(strstr(r, "launched"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":91,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_process\",\"arguments\":"
               "{\"titleId\":\"0100000000005678\"}}}");
    assert(strstr(r, "0100000000005678 is running"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":92,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_process\",\"arguments\":"
               "{\"titleId\":\"0100000000005678\",\"state\":\"stopped\",\"timeoutMs\":250}}}");
    assert(strstr(r, "timed out after") && strstr(r, "still running"));

    r = do_http("GET", "/wait/process?titleId=0100000000005678&state=running", NULL, 0);
    assert(strstr(r, "\"met\":true") && strstr(r, "\"running\":true"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":93,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"process_stop\",\"arguments\":"
               "{\"titleId\":\"0100000000005678\"}}}");
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":94,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_process\",\"arguments\":"
               "{\"titleId\":\"0100000000005678\",\"state\":\"stopped\"}}}");
    assert(strstr(r, "is not running after"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":95,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_process\",\"arguments\":"
               "{\"titleId\":\"0100000000005678\",\"state\":\"gone\"}}}");
    assert(strstr(r, "\"isError\":true"));
    printf("wait_for_process ok\n");
}

static void test_wait_for_file(void) {
    system("rm -rf " FAKE_SD "/log && mkdir -p " FAKE_SD "/log");
    FILE *f = fopen(FAKE_SD "/log/app.log", "wb");
    assert(f);
    fputs("booting\nREADY\n", f);
    fclose(f);

    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":100,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"wait_for_file\",\"arguments\":"
                           "{\"path\":\"/log/app.log\",\"contains\":\"READY\"}}}");
    assert(strstr(r, "found after"));
    // Only text written after the call counts with newOnly.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":101,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_file\",\"arguments\":"
               "{\"path\":\"/log/app.log\",\"contains\":\"READY\",\"newOnly\":true,"
               "\"timeoutMs\":250}}}");
    assert(strstr(r, "timed out after") && strstr(r, "text not found"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":102,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_file\",\"arguments\":"
               "{\"path\":\"/log/none.log\",\"timeoutMs\":200}}}");
    assert(strstr(r, "the file does not exist"));
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":103,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"wait_for_file\",\"arguments\":{\"path\":\"/log/app.log\"}}}");
    assert(strstr(r, "file exists after"));

    r = do_http("GET", "/wait/file?path=/log/app.log&contains=READY", NULL, 0);
    assert(strstr(r, "\"met\":true") && strstr(r, "\"exists\":true"));
    r = do_http("GET", "/wait/file?path=/log/app.log&contains=NOPE&timeoutMs=200", NULL, 0);
    assert(strstr(r, "\"met\":false"));
    printf("wait_for_file ok\n");
}

static void test_type_text(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":110,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"type_text\",\"arguments\":"
                           "{\"text\":\"Hi! a_b\\n\",\"keyMs\":5}}}");
    assert(strstr(r, "\"isError\":false") && strstr(r, "typed"));
    assert(strcmp(stub_typed, "Hi! a_b\n") == 0 && stub_key_ms == 5);

    stub_typed[0] = '\0';
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":111,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"type_text\",\"arguments\":{\"text\":\"caf\\u00e9\"}}}");
    assert(strstr(r, "\"isError\":true") && strstr(r, "cannot be typed"));
    assert(stub_typed[0] == '\0');

    // The same text on a French console, and a layout not supported yet.
    stub_keyboard_layout = 4;
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":112,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"type_text\",\"arguments\":{\"text\":\"caf\\u00e9\"}}}");
    assert(strstr(r, "\"isError\":false") && strcmp(stub_typed, "caf\xc3\xa9") == 0);
    stub_keyboard_layout = 8;
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":113,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"type_text\",\"arguments\":{\"text\":\"a\"}}}");
    assert(strstr(r, "\"isError\":true") && strstr(r, "German"));
    stub_keyboard_layout = 1;
    printf("type_text ok\n");
}

static void test_crash_reports(void) {
    system("rm -rf " FAKE_SD "/atmosphere && mkdir -p " FAKE_SD "/atmosphere/crash_reports "
           FAKE_SD "/atmosphere/fatal_reports");
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":120,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"list_crash_reports\",\"arguments\":{}}}");
    assert(strstr(r, "no crash or fatal reports"));

    FILE *f = fopen(FAKE_SD "/atmosphere/crash_reports/01759500000_0100000000001000.log", "wb");
    assert(f);
    fputs("Atmosph\xc3\xa8re Crash Report (v1.7):\n"
          "Result:                          0x2A8 (2162-0001)\n\n"
          "Process Info:\n"
          "    Process Name:                qlaunch\n"
          "    Program ID:                  0100000000001000\n", f);
    fclose(f);
    f = fopen(FAKE_SD "/atmosphere/crash_reports/01759500000_0100000000001000.jpg", "wb");
    assert(f);
    fclose(f);
    f = fopen(FAKE_SD "/atmosphere/fatal_reports/01759400000_010000000000000c.log", "wb");
    assert(f);
    fputs("Atmosph\xc3\xa8re Fatal Report (v1.1):\n"
          "Result:                          0x1234 (2052-0009)\n\n"
          "Program ID:                      010000000000000c\n"
          "Process Name:                    nifm\n", f);
    fclose(f);
    f = fopen(FAKE_SD "/atmosphere/crash_reports/notes.txt", "wb");
    assert(f);
    fclose(f);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":121,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"list_crash_reports\",\"arguments\":{}}}");
    const char *crash = strstr(r, "2025-10-03 14:00:00 UTC  crash  0100000000001000 (qlaunch)  "
                                  "0x2A8 (2162-0001)");
    const char *fatal = strstr(r, "2025-10-02 10:13:20 UTC  fatal  010000000000000c (nifm)  "
                                  "0x1234 (2052-0009)");
    assert(crash && fatal && crash < fatal); // newest first
    assert(strstr(r, "[+ .jpg screenshot]"));
    assert(!strstr(r, "notes.txt"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":122,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"list_crash_reports\",\"arguments\":{\"limit\":1}}}");
    assert(strstr(r, "qlaunch") && !strstr(r, "nifm"));

    r = do_http("GET", "/crash-reports", NULL, 0);
    assert(strstr(r, "\"kind\":\"crash\"") && strstr(r, "\"kind\":\"fatal\""));
    assert(strstr(r, "\"titleId\":\"0100000000001000\",\"processName\":\"qlaunch\","
                     "\"result\":\"0x2A8 (2162-0001)\""));
    assert(strstr(r, "\"screenshot\":true"));
    r = do_http("GET", "/crash-reports?limit=0", NULL, 0);
    assert(strncmp(r, "HTTP/1.1 400", 12) == 0);
    printf("crash reports ok\n");
}

// The REST fixes: escaped paths, uploads that never clobber the target, and
// reason phrases.
static void test_rest_fixes(void) {
    system("rm -rf " FAKE_SD "/rest && mkdir -p " FAKE_SD "/rest");
    const char *r = do_http("PUT", "/files?path=/rest/a%22b.txt", "first", 0);
    assert(strncmp(r, "HTTP/1.1 201 Created", 20) == 0);
    assert(strstr(r, "\"path\":\"/rest/a\\\"b.txt\""));

    // A body cut short leaves the existing file as it was, and no temp file.
    r = do_http("PUT", "/files?path=/rest/a%22b.txt", "sec", 100);
    assert(strncmp(r, "HTTP/1.1 400", 12) == 0 && strstr(r, "incomplete upload"));
    FILE *f = fopen(FAKE_SD "/rest/a\"b.txt", "rb");
    assert(f);
    char buf[16] = {0};
    assert(fread(buf, 1, sizeof(buf), f) == 5 && memcmp(buf, "first", 5) == 0);
    fclose(f);
    struct stat st;
    assert(stat(FAKE_SD "/rest/a\"b.txt" FILES_UPLOAD_SUFFIX, &st) != 0);

    // A complete upload replaces it.
    r = do_http("PUT", "/files?path=/rest/a%22b.txt", "second", 0);
    assert(strncmp(r, "HTTP/1.1 201", 12) == 0);
    f = fopen(FAKE_SD "/rest/a\"b.txt", "rb");
    assert(f && fread(buf, 1, sizeof(buf), f) == 6 && memcmp(buf, "second", 6) == 0);
    fclose(f);

    do_http("PUT", "/files?path=/rest/c.txt", "c", 0);
    r = do_http("POST", "/files/move?path=/rest/c.txt&to=/rest/a%22b.txt", NULL, 0);
    assert(strncmp(r, "HTTP/1.1 409 Conflict", 21) == 0);
    r = do_http("DELETE", "/files?path=/rest/a%22b.txt", NULL, 0);
    assert(strstr(r, "\"deleted\":\"/rest/a\\\"b.txt\""));
    printf("rest fixes ok\n");
}

static Config g_cfg_for_oauth;

int main(void) {
    snprintf(g_cfg_for_oauth.username, sizeof(g_cfg_for_oauth.username), "u");
    snprintf(g_cfg_for_oauth.password, sizeof(g_cfg_for_oauth.password), "p");
    oauth_init(&g_cfg_for_oauth);

    // What source/features/feature_list.c does for the MCP-capable features.
    files_http_register();
    mcp_server_http_register();
    screen_mcp_register();
    input_mcp_register();
    status_mcp_register();
    files_mcp_register();
    oauth_mcp_register();
    power_mcp_register();
    process_mcp_register();
    settings_mcp_register();
    titles_mcp_register();
    network_mcp_register();
    crash_mcp_register();
    crash_http_register();
    screen_http_register();
    process_http_register();

    test_initialize();
    test_notification();
    test_ping_and_errors();
    test_tools_list();
    test_tap_buttons();
    test_tap_sequence();
    test_screenshot();
    test_upload_and_files();
    test_move_file();
    test_hash_file();
    test_input_with_screenshot();
    test_create_token();
    test_power_tools();
    test_process_tools();
    test_touch_tools();
    test_move_file_tool();
    test_screenshot_scaled();
    test_wait_for_screen();
    test_wait_for_process();
    test_wait_for_file();
    test_type_text();
    test_crash_reports();
    test_rest_fixes();
    printf("all mcp tests passed\n");
    return 0;
}
