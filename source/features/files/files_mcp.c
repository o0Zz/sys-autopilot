#include "features/files/files_mcp.h"
#include "core/log.h"
#include "core/request.h"
#include "features/files/files.h"
#include "features/files/files_tools.h"
#include "features/mcp/mcp_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define READ_FILE_MAX 32768

// Extracts the "path" argument and resolves it. Replies with a tool error on
// failure.
static bool get_path_arg(McpCall *call, char *fspath, size_t fspath_sz) {
    char path[512];
    if (!mcp_arg_string(call, "path", path, sizeof(path))) {
        mcp_reply_error(call, "missing 'path'");
        return false;
    }
    const char *err = NULL;
    if (!files_resolve(path, fspath, fspath_sz, &err)) {
        mcp_reply_error(call, err);
        return false;
    }
    return true;
}

static void tool_list_directory(McpCall *call) {
    char fspath[768];
    if (!get_path_arg(call, fspath, sizeof(fspath)))
        return;

    files_trim_slash(fspath);

    // The reply escapes the listing into request memory too, so give back
    // what the listing does not use.
    const char *err = NULL;
    size_t cap, len;
    char *json = request_alloc_rest(call->req, &cap);
    bool ok = files_build_listing(fspath, fspath + strlen(FILES_ROOT), json, cap, &len, &err);
    request_trim(call->req, json, len);
    if (!ok) {
        mcp_reply_error(call, err);
        return;
    }
    mcp_reply_text_len(call, json, len);
}

// Replaces invalid UTF-8 sequences with '?' so the output is always a legal
// JSON string payload.
static void sanitize_utf8(char *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)buf[i];
        size_t extra;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else { buf[i++] = '?'; continue; }

        bool ok = i + extra < len; // full sequence present
        for (size_t k = 1; ok && k <= extra; k++)
            ok = ((unsigned char)buf[i + k] & 0xC0) == 0x80;
        if (ok)
            i += extra + 1;
        else
            buf[i++] = '?';
    }
}

static void tool_read_file(McpCall *call) {
    char fspath[768];
    if (!get_path_arg(call, fspath, sizeof(fspath)))
        return;

    struct stat st;
    if (stat(fspath, &st) != 0 || S_ISDIR(st.st_mode)) {
        mcp_reply_error(call, "no such file");
        return;
    }

    long long offset = mcp_arg_int(call, "offset", 0);
    long long length = mcp_arg_int(call, "length", READ_FILE_MAX);
    files_clamp_range((long long)st.st_size, &offset, &length);
    if (length > READ_FILE_MAX)
        length = READ_FILE_MAX;

    char *data = request_alloc(call->req, (size_t)length);
    if (!data) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    FILE *f = fopen(fspath, "rb");
    if (!f) {
        mcp_reply_error(call, "open failed");
        return;
    }
    if (offset > 0)
        fseek(f, (long)offset, SEEK_SET);
    size_t got = fread(data, 1, (size_t)length, f);
    fclose(f);

    sanitize_utf8(data, got);
    mcp_reply_text_len(call, data, got);
}

static void tool_upload_file(McpCall *call) {
    char fspath[768];
    if (!get_path_arg(call, fspath, sizeof(fspath)))
        return;

    size_t bytes = 0;
    const char *err = NULL;
    const char *tmp = mcp_call_streamed_content(call, &bytes, &err);
    if (!tmp) {
        mcp_reply_error(call, err);
        return;
    }

    files_mkdirs_for(fspath);
    remove(fspath); // rename() does not overwrite on all newlib targets
    if (rename(tmp, fspath) != 0) {
        mcp_reply_error(call, "failed to move upload into place");
        return;
    }

    char msg[820];
    snprintf(msg, sizeof(msg), "wrote %zu bytes to %s", bytes, fspath + strlen(FILES_ROOT));
    LOGI("files", "%s", msg);
    mcp_reply_text(call, msg);
}

static void tool_move_file(McpCall *call) {
    char src[768];
    if (!get_path_arg(call, src, sizeof(src)))
        return;

    char to[512];
    if (!mcp_arg_string(call, "to", to, sizeof(to)) || to[0] == '\0') {
        mcp_reply_error(call, "missing 'to' (destination path)");
        return;
    }
    char dst[768];
    const char *err = NULL;
    if (!files_resolve(to, dst, sizeof(dst), &err) || !files_move_path(src, dst, &err)) {
        mcp_reply_error(call, err);
        return;
    }

    char msg[820];
    snprintf(msg, sizeof(msg), "moved to %s", dst + strlen(FILES_ROOT));
    LOGI("files", "%s", msg);
    mcp_reply_text(call, msg);
}

static void tool_delete_file(McpCall *call) {
    char fspath[768];
    if (!get_path_arg(call, fspath, sizeof(fspath)))
        return;
    const char *err = NULL;
    if (!files_delete_path(fspath, &err)) {
        mcp_reply_error(call, err);
        return;
    }
    mcp_reply_text(call, "deleted");
}

// Computes a file's SHA-256. With an optional "expected" hash, also reports
// whether they match, so the agent can verify an upload in a single round-trip.
static void tool_hash_file(McpCall *call) {
    char fspath[768];
    if (!get_path_arg(call, fspath, sizeof(fspath)))
        return;

    void *buf = request_alloc(call->req, FILES_IO_BUF_SIZE);
    if (!buf) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    char hexbuf[65];
    long long size = 0;
    const char *err = NULL;
    if (!files_hash_sha256(fspath, buf, FILES_IO_BUF_SIZE, hexbuf, &size, &err)) {
        mcp_reply_error(call, err);
        return;
    }

    // Optional case-insensitive comparison against an expected digest.
    char expected[80];
    bool have_expected = mcp_arg_string(call, "expected", expected, sizeof(expected));

    char msg[256];
    if (have_expected) {
        bool matched = strcasecmp(expected, hexbuf) == 0;
        snprintf(msg, sizeof(msg),
                 "{\"algorithm\":\"sha256\",\"hash\":\"%s\",\"size\":%lld,\"matched\":%s}",
                 hexbuf, size, matched ? "true" : "false");
    } else {
        snprintf(msg, sizeof(msg),
                 "{\"algorithm\":\"sha256\",\"hash\":\"%s\",\"size\":%lld}",
                 hexbuf, size);
    }
    mcp_reply_text(call, msg);
}

static void tool_wait_for_file(McpCall *call) {
    char fspath[768];
    if (!get_path_arg(call, fspath, sizeof(fspath)))
        return;
    char contains[FILES_WAIT_MAX_NEEDLE + 1] = "";
    int tok = json_obj_get(call->doc, call->args, "contains");
    if (tok >= 0 && !mcp_arg_string(call, "contains", contains, sizeof(contains))) {
        mcp_reply_error(call, "'contains' must be a string of at most 256 bytes");
        return;
    }
    bool new_only = false;
    mcp_arg_bool(call, "newOnly", &new_only);
    int timeout = mcp_arg_int(call, "timeoutMs", FILES_WAIT_DEFAULT_TIMEOUT_MS);

    char *buf = request_alloc(call->req, FILES_IO_BUF_SIZE);
    if (!buf) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    FilesWait res;
    files_wait(fspath, contains, new_only, timeout, buf, FILES_IO_BUF_SIZE, &res);

    char msg[160];
    if (res.met && contains[0])
        snprintf(msg, sizeof(msg), "found after %d ms (file is %lld bytes)", res.elapsed_ms,
                 res.size);
    else if (res.met)
        snprintf(msg, sizeof(msg), "file exists after %d ms (%lld bytes)", res.elapsed_ms,
                 res.size);
    else if (!res.exists)
        snprintf(msg, sizeof(msg), "timed out after %d ms: the file does not exist",
                 res.elapsed_ms);
    else
        snprintf(msg, sizeof(msg), "timed out after %d ms: text not found (file is %lld bytes)",
                 res.elapsed_ms, res.size);
    mcp_reply_text(call, msg);
}

void files_mcp_register(void) {
    mcp_server_register_tool(&kToolListDirectory, tool_list_directory);
    mcp_server_register_tool(&kToolReadFile,      tool_read_file);
    mcp_server_register_tool(&kToolUploadFile,    tool_upload_file);
    mcp_server_register_tool(&kToolDeleteFile,    tool_delete_file);
    mcp_server_register_tool(&kToolMoveFile,      tool_move_file);
    mcp_server_register_tool(&kToolHashFile,      tool_hash_file);
    mcp_server_register_tool(&kToolWaitForFile,   tool_wait_for_file);
}
