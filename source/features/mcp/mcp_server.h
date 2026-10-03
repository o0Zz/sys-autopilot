#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/http.h"
#include "util/json.h"

// MCP server: a stateless "Streamable HTTP" endpoint speaking JSON-RPC 2.0 at
// POST /mcp. It knows no tool: features register theirs at startup (see
// source/features/feature_list.c) and the server dispatches tools/call by name.

// One tool: its complete JSON definition (name, description, inputSchema), as
// returned in tools/list. Generated from each feature's *_tools.json by
// scripts/generate_resource.py, which puts the name first, so the schema
// always starts with {"name":"<name>" and doubles as the name.
typedef struct {
    const char *schema;
} McpToolDef;

// One tools/call being served.
typedef struct {
    HttpRequest *req;
    const char *id;       // JSON-RPC id, raw JSON (number or quoted string)
    const JsonDoc *doc;   // the request document
    int args;             // token of the "arguments" object, or -1 if absent
    bool content_streamed; // see mcp_call_streamed_content()
} McpCall;

typedef void (*McpToolHandler)(McpCall *call);

// Capacity of the tool table; registration fails (and logs) past it.
#define MCP_MAX_TOOLS 64 // a full build registers 48

// Registers the POST/GET /mcp routes and the "path=/mcp" mDNS TXT entry.
void mcp_server_http_register(void);

// Adds a tool. `def` must outlive the server (the generated constants do).
// Returns false when the table is full.
void mcp_server_register_tool(const McpToolDef *def, McpToolHandler handler);

// --- replies (each sends one complete response) ------------------------------

void mcp_reply_text(McpCall *call, const char *text);   // isError: false
// Same for text of known length that may contain NUL bytes (file contents).
void mcp_reply_text_len(McpCall *call, const char *text, size_t len);
void mcp_reply_error(McpCall *call, const char *text);  // isError: true (tool-level failure)
void mcp_reply_image(McpCall *call, const uint8_t *jpeg, size_t size);
// [text, image] content; `text` must not need JSON escaping (static messages).
void mcp_reply_text_and_image(McpCall *call, const char *text,
                              const uint8_t *jpeg, size_t size);
// JSON-RPC error (protocol-level failure, e.g. out of memory).
void mcp_reply_rpc_error(McpCall *call, int code, const char *msg);

// --- arguments ----------------------------------------------------------------

// Integer argument `key`, or `fallback` when absent or not an integer.
int mcp_arg_int(const McpCall *call, const char *key, int fallback);

// String argument `key`. False when absent, not a string, or too long.
bool mcp_arg_string(const McpCall *call, const char *key, char *out, size_t outsz);

// Number argument `key` (integer or not). False when absent or not a number.
bool mcp_arg_double(const McpCall *call, const char *key, double *out);

// Boolean argument `key`. False when absent or not a boolean.
bool mcp_arg_bool(const McpCall *call, const char *key, bool *out);

// A large base64 "content" argument (upload_file) never enters the JSON
// document: the transport decodes it straight to a temp file while the
// request streams in. Returns that file's path and decoded size, or NULL
// with *err set when the call carried no valid content. The tool moves the
// file into place; the transport deletes it afterwards if it is still there.
const char *mcp_call_streamed_content(McpCall *call, size_t *out_bytes, const char **err);
