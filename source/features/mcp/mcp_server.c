#include "features/mcp/mcp_server.h"
#include "core/app.h"
#include "core/config.h"
#include "core/http_server.h"
#include "core/log.h"
#include "core/mdns.h"
#include "core/request.h"
#include "features/mcp/jstream.h"
#include "util/base64.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Cap for the *reduced* JSON-RPC document (everything except a streamed
// upload_file content field, which goes straight to disk).
#define MCP_DOC_MAX 16384

// Socket read size while streaming the request body through jstream.
#define MCP_READ_CHUNK 0x2000

// Decode buffer for streamed content. jstream hands the sink at most one
// fed chunk, so it holds the decode of MCP_READ_CHUNK base64 chars.
#define UPLOAD_DECODE_SIZE ((MCP_READ_CHUNK / 4 + 1) * 3)

// Temp destination for streamed upload content (renamed into place on
// success, deleted otherwise).
#ifndef MCP_UPLOAD_TMP
#define MCP_UPLOAD_TMP CONFIG_DIR "/.upload.tmp"
#endif

#define MCP_PROTO_LATEST "2025-06-18"

// --- tool registry -------------------------------------------------------------

typedef struct {
    const McpToolDef *def;
    McpToolHandler handler;
} Tool;

static Tool g_tools[MCP_MAX_TOOLS];
static int g_tool_count;

void mcp_server_register_tool(const McpToolDef *def, McpToolHandler handler) {
    if (g_tool_count >= MCP_MAX_TOOLS) {
        LOGW("mcp", "tool table full, dropping %.64s", def->schema);
        return;
    }
    g_tools[g_tool_count++] = (Tool){ def, handler };
}

// --- upload content sink -------------------------------------------------------

typedef struct {
    HttpRequest *req; // decode buffer comes from its request memory
    uint8_t *decbuf;
    FILE *f;
    B64Decoder dec;
    size_t bytes;
    bool failed;
} UploadCtx;

static UploadCtx g_upload;

// Note: failures (bad base64, write error) latch u->failed and drain the
// rest so the request still streams through and the JSON-RPC layer can
// report a proper in-band tool error instead of a protocol error.
static void upload_sink(const char *data, size_t len, void *ctx) {
    UploadCtx *u = ctx;
    if (u->failed)
        return; // keep draining
    if (!u->decbuf) {
        u->decbuf = request_alloc(u->req, UPLOAD_DECODE_SIZE);
        if (!u->decbuf) {
            u->failed = true;
            return;
        }
    }
    if (!u->f) {
        char dir[] = MCP_UPLOAD_TMP;
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mkdir(dir, 0777);
        }
        u->f = fopen(MCP_UPLOAD_TMP, "wb");
        if (!u->f) {
            u->failed = true;
            return;
        }
    }
    ssize_t dn = b64dec_update(&u->dec, data, len, u->decbuf);
    if (dn < 0 || fwrite(u->decbuf, 1, (size_t)dn, u->f) != (size_t)dn) {
        u->failed = true;
        return;
    }
    u->bytes += (size_t)dn;
}

// Closes and removes any leftover upload temp state. No-op when nothing was
// streamed (the common case), so non-upload requests never touch the SD card.
static void upload_cleanup(void) {
    if (g_upload.f)
        fclose(g_upload.f);
    if (g_upload.f || g_upload.bytes > 0 || g_upload.failed)
        remove(MCP_UPLOAD_TMP);
    memset(&g_upload, 0, sizeof(g_upload));
}

const char *mcp_call_streamed_content(McpCall *call, size_t *out_bytes, const char **err) {
    if (!call->content_streamed) {
        *err = "missing 'content' (base64 string)";
        return NULL;
    }
    if (g_upload.failed || !g_upload.f || !b64dec_finish(&g_upload.dec)) {
        *err = "invalid base64 content";
        return NULL;
    }
    fclose(g_upload.f);
    g_upload.f = NULL;
    *out_bytes = g_upload.bytes;
    return MCP_UPLOAD_TMP;
}

// --- response helpers ----------------------------------------------------------

// Writes the response envelope's opening, {"jsonrpc":"2.0","id":<id>,"<key>":,
// into head[RPC_HEAD_MAX]. Returns its length.
#define RPC_HEAD_MAX 96
static size_t rpc_head(char *head, const char *id, const char *key) {
    return (size_t)snprintf(head, RPC_HEAD_MAX, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"%s\":",
                            id, key);
}

// Sends {"jsonrpc":"2.0","id":<id>,"<key>":<value>} with exact Content-Length.
static void send_rpc_value(int fd, const char *id, const char *key,
                           const char *value, size_t value_len) {
    char head[RPC_HEAD_MAX];
    size_t hn = rpc_head(head, id, key);
    http_send_header(fd, 200, "application/json", hn + value_len + 1);
    http_write_all(fd, head, hn);
    http_write_all(fd, value, value_len);
    http_write_all(fd, "}", 1);
}

static void send_rpc_error(int fd, const char *id, int code, const char *msg) {
    char val[256];
    int n = snprintf(val, sizeof(val), "{\"code\":%d,\"message\":\"%s\"}", code, msg);
    send_rpc_value(fd, id, "error", val, (size_t)n);
}

void mcp_reply_rpc_error(McpCall *call, int code, const char *msg) {
    send_rpc_error(call->req->fd, call->id, code, msg);
}

// Sends a result of the form <pre><payload><post> (payload may be huge).
static void send_result_stream(int fd, const char *id, const char *pre,
                               const char *payload, size_t payload_len,
                               const char *post) {
    char head[RPC_HEAD_MAX];
    size_t hn = rpc_head(head, id, "result");
    size_t total = hn + strlen(pre) + payload_len + strlen(post) + 1;
    http_send_header(fd, 200, "application/json", total);
    http_write_all(fd, head, hn);
    http_write_all(fd, pre, strlen(pre));
    if (payload_len > 0)
        http_write_all(fd, payload, payload_len);
    http_write_all(fd, post, strlen(post));
    http_write_all(fd, "}", 1);
}

#define TEXT_PRE      "{\"content\":[{\"type\":\"text\",\"text\":\""
#define TEXT_POST_OK  "\"}],\"isError\":false}"
#define TEXT_POST_ERR "\"}],\"isError\":true}"

static void send_tool_text(McpCall *call, bool is_error, const char *text, size_t tlen) {
    size_t esc_len = json_escaped_len(text, tlen);
    char *escaped = request_alloc(call->req, esc_len + 1);
    if (!escaped) {
        mcp_reply_rpc_error(call, -32603, "out of request memory");
        return;
    }
    json_escape(text, tlen, escaped, esc_len + 1);
    send_result_stream(call->req->fd, call->id, TEXT_PRE, escaped, esc_len,
                       is_error ? TEXT_POST_ERR : TEXT_POST_OK);
}

void mcp_reply_text(McpCall *call, const char *text) {
    send_tool_text(call, false, text, strlen(text));
}

void mcp_reply_text_len(McpCall *call, const char *text, size_t len) {
    send_tool_text(call, false, text, len);
}

void mcp_reply_error(McpCall *call, const char *text) {
    send_tool_text(call, true, text, strlen(text));
}

// Streams `jpeg` as base64. Encodes in multiples of 3 bytes so the chunks
// concatenate to the whole-buffer encoding (padding only on the final one).
static bool write_base64(McpCall *call, const uint8_t *jpeg, size_t size) {
    enum { RAW_CHUNK = 3072, ENC_CHUNK = 4096 };
    char *enc = request_alloc(call->req, ENC_CHUNK);
    if (!enc)
        return false;
    for (size_t off = 0; off < size; off += RAW_CHUNK) {
        size_t chunk = size - off > RAW_CHUNK ? RAW_CHUNK : size - off;
        size_t n = b64_encode(jpeg + off, chunk, enc);
        if (!http_write_all(call->req->fd, enc, n))
            return false;
    }
    return true;
}

static void send_image_result(McpCall *call, const char *text,
                              const uint8_t *jpeg, size_t size) {
    static const char img_only_pre[] = "{\"content\":[{\"type\":\"image\",\"data\":\"";
    static const char img_pre[] = "\"},{\"type\":\"image\",\"data\":\"";
    static const char img_post[] = "\",\"mimeType\":\"image/jpeg\"}],\"isError\":false}";
    int fd = call->req->fd;

    char head[RPC_HEAD_MAX];
    size_t hn = rpc_head(head, call->id, "result");
    size_t total = hn + b64_encoded_len(size) + sizeof(img_post) - 1 + 1;
    if (text)
        total += sizeof(TEXT_PRE) - 1 + strlen(text) + sizeof(img_pre) - 1;
    else
        total += sizeof(img_only_pre) - 1;

    http_send_header(fd, 200, "application/json", total);
    http_write_all(fd, head, hn);
    if (text) {
        http_write_all(fd, TEXT_PRE, sizeof(TEXT_PRE) - 1);
        http_write_all(fd, text, strlen(text));
        http_write_all(fd, img_pre, sizeof(img_pre) - 1);
    } else {
        http_write_all(fd, img_only_pre, sizeof(img_only_pre) - 1);
    }
    if (!write_base64(call, jpeg, size))
        return;
    http_write_all(fd, img_post, sizeof(img_post) - 1);
    http_write_all(fd, "}", 1);
}

void mcp_reply_image(McpCall *call, const uint8_t *jpeg, size_t size) {
    send_image_result(call, NULL, jpeg, size);
}

void mcp_reply_text_and_image(McpCall *call, const char *text,
                              const uint8_t *jpeg, size_t size) {
    send_image_result(call, text, jpeg, size);
}

// --- arguments ----------------------------------------------------------------

int mcp_arg_int(const McpCall *call, const char *key, int fallback) {
    return json_obj_int(call->doc, call->args, key, fallback);
}

bool mcp_arg_string(const McpCall *call, const char *key, char *out, size_t outsz) {
    int tok = json_obj_get(call->doc, call->args, key);
    return tok >= 0 && json_get_string(call->doc, tok, out, outsz);
}

bool mcp_arg_double(const McpCall *call, const char *key, double *out) {
    int tok = json_obj_get(call->doc, call->args, key);
    return tok >= 0 && json_get_double(call->doc, tok, out);
}

bool mcp_arg_bool(const McpCall *call, const char *key, bool *out) {
    int tok = json_obj_get(call->doc, call->args, key);
    return tok >= 0 && json_get_bool(call->doc, tok, out);
}

// --- JSON-RPC dispatch -------------------------------------------------------

static void handle_initialize(HttpRequest *req, const char *id, const JsonDoc *doc, int params) {
    // Respond with the client's protocol version when we know it; otherwise
    // the latest we support.
    char proto[24] = MCP_PROTO_LATEST;
    int vtok = json_obj_get(doc, params, "protocolVersion");
    if (vtok >= 0) {
        char v[24];
        if (json_get_string(doc, vtok, v, sizeof(v)) &&
            (strcmp(v, "2024-11-05") == 0 || strcmp(v, "2025-03-26") == 0 ||
             strcmp(v, "2025-06-18") == 0))
            snprintf(proto, sizeof(proto), "%s", v);
    }

    char val[256];
    int n = snprintf(val, sizeof(val),
                     "{\"protocolVersion\":\"%s\","
                     "\"capabilities\":{\"tools\":{}},"
                     "\"serverInfo\":{\"name\":\"sys-autopilot\",\"version\":\"%s\"}}",
                     proto, app_version());
    send_rpc_value(req->fd, id, "result", val, (size_t)n);
}

// tools/list: {"tools":[<def>,<def>,...]}, streamed straight from the
// registered definitions (about 19K in all) rather than assembled in memory.
static void handle_tools_list(HttpRequest *req, const char *id) {
    static const char pre[] = "{\"tools\":[";
    static const char post[] = "]}";
    char head[RPC_HEAD_MAX];
    size_t hn = rpc_head(head, id, "result");
    size_t total = hn + sizeof(pre) - 1 + sizeof(post) - 1 + 1;
    for (int i = 0; i < g_tool_count; i++)
        total += strlen(g_tools[i].def->schema) + (i ? 1 : 0); // + separating comma

    http_send_header(req->fd, 200, "application/json", total);
    http_write_all(req->fd, head, hn);
    http_write_all(req->fd, pre, sizeof(pre) - 1);
    for (int i = 0; i < g_tool_count; i++) {
        if (i)
            http_write_all(req->fd, ",", 1);
        http_write_all(req->fd, g_tools[i].def->schema, strlen(g_tools[i].def->schema));
    }
    http_write_all(req->fd, post, sizeof(post) - 1);
    http_write_all(req->fd, "}", 1);
}

// True if def is the tool called `name` (see McpToolDef for the layout).
static bool tool_is(const McpToolDef *def, const char *name) {
    static const char prefix[] = "{\"name\":\"";
    const char *p = def->schema + sizeof(prefix) - 1;
    size_t n = strlen(name);
    return strncmp(p, name, n) == 0 && p[n] == '"';
}

static void handle_tools_call(HttpRequest *req, const char *id, const JsonDoc *doc,
                              int params, bool content_streamed) {
    char name[40] = "";
    int ntok = json_obj_get(doc, params, "name");
    if (ntok < 0 || !json_get_string(doc, ntok, name, sizeof(name))) {
        send_rpc_error(req->fd, id, -32602, "missing tool name");
        return;
    }
    McpCall call = {
        .req = req,
        .id = id,
        .doc = doc,
        .args = json_obj_get(doc, params, "arguments"), // may be -1
        .content_streamed = content_streamed,
    };
    for (int i = 0; i < g_tool_count; i++) {
        if (tool_is(g_tools[i].def, name)) {
            g_tools[i].handler(&call);
            return;
        }
    }
    send_rpc_error(req->fd, id, -32602, "unknown tool");
}

static void handle_post(HttpRequest *req) {
    if (!req->has_content_length) {
        http_send_error(req->fd, 411, "Content-Length required");
        return;
    }

    char *doc_buf = request_alloc(req, MCP_DOC_MAX);
    char *chunk = request_alloc(req, MCP_READ_CHUNK);
    JsonDoc *doc = request_alloc(req, sizeof(*doc));
    if (!doc_buf || !chunk || !doc) {
        http_send_error(req->fd, 500, "out of request memory");
        return;
    }

    memset(&g_upload, 0, sizeof(g_upload));
    g_upload.req = req;
    b64dec_init(&g_upload.dec);

    Jstream js;
    jstream_init(&js, doc_buf, MCP_DOC_MAX, upload_sink, &g_upload);

    ssize_t n;
    int jerr = 0;
    while ((n = http_read_body(req, chunk, MCP_READ_CHUNK)) > 0) {
        jerr = jstream_feed(&js, chunk, (size_t)n);
        if (jerr)
            break;
    }
    bool content_streamed = false;
    if (!jerr && n >= 0)
        jerr = jstream_finish(&js, &content_streamed);

    if (jerr || n < 0) {
        upload_cleanup();
        const char *msg = (jerr == JSTREAM_EDOC)     ? "request too large"
                        : (jerr == JSTREAM_ECONTENT) ? "invalid base64 content"
                                                     : "parse error";
        send_rpc_error(req->fd, "null", -32700, msg);
        return;
    }

    if (json_parse(doc, doc_buf, js.doc_len) != 0 || doc->ntok < 1 ||
        doc->tok[0].type != JSMN_OBJECT) {
        upload_cleanup();
        send_rpc_error(req->fd, "null", -32700, "parse error");
        return;
    }

    // id: echoed verbatim (number or string); absent => notification.
    char id[80] = "null";
    bool has_id = false;
    int id_tok = json_obj_get(doc, 0, "id");
    if (id_tok >= 0 && json_raw(doc, id_tok, id, sizeof(id)))
        has_id = true;

    char method[48] = "";
    int mtok = json_obj_get(doc, 0, "method");
    if (mtok < 0 || !json_get_string(doc, mtok, method, sizeof(method))) {
        upload_cleanup();
        send_rpc_error(req->fd, id, -32600, "missing method");
        return;
    }

    int params = json_obj_get(doc, 0, "params"); // may be -1

    LOGI("mcp", "%s", method);

    if (!has_id || strncmp(method, "notifications/", 14) == 0) {
        // Notification: accept and discard.
        upload_cleanup();
        http_send_header(req->fd, 202, "text/plain", 0);
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        handle_initialize(req, id, doc, params);
    } else if (strcmp(method, "ping") == 0) {
        send_rpc_value(req->fd, id, "result", "{}", 2);
    } else if (strcmp(method, "tools/list") == 0) {
        handle_tools_list(req, id);
    } else if (strcmp(method, "tools/call") == 0) {
        if (params < 0)
            send_rpc_error(req->fd, id, -32602, "missing params");
        else
            handle_tools_call(req, id, doc, params, content_streamed);
    } else {
        send_rpc_error(req->fd, id, -32601, "method not found");
    }

    // Whatever happened, no upload temp state may survive the request.
    upload_cleanup();
}

static void handle_get(HttpRequest *req) {
    // We have no server-initiated messages, so we decline the optional
    // server->client SSE stream with 405 (spec-sanctioned; capable clients
    // proceed over POST). Force the connection closed so the client retires
    // this socket cleanly rather than reusing it into a reset.
    static const char body[] = "{\"error\":\"method not allowed\"}";
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 405 Method Not Allowed\r\n"
                     "Server: sys-autopilot\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     sizeof(body) - 1);
    http_write_all(req->fd, hdr, (size_t)n);
    http_write_all(req->fd, body, sizeof(body) - 1);
    req->keep_alive = false;
}

void mcp_server_http_register(void) {
    http_server_register_route("POST", "/mcp", handle_post);
    http_server_register_route("GET",  "/mcp", handle_get);
    mdns_add_txt("path=/mcp");
}
