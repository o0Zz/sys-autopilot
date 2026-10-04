#pragma once

#include <stddef.h>

#include "core/http.h"
#include "util/json.h"

// Request memory: buffers that live exactly as long as the request being
// served.
//
// A handler takes what it needs with request_alloc() and never frees it. The
// HTTP server releases everything at once when the response is done
// (request_release, called only from http_server.c). The server handles one
// request at a time, so this single region is shared by every feature: only
// the largest request's needs cost resident memory, not the sum of every
// feature's buffers.
//
// Sized for the most demanding request: a screenshot (the 512K capssc JPEG
// buffer) re-encoded at full size (about 70K of transcoder state, see
// util/jpeg.h), plus, when MCP is built, the JSON-RPC document and read
// buffers the MCP server holds around it.
#ifdef FEATURE_MCP
#define REQUEST_MEMORY_SIZE 0xA4000
#else
#define REQUEST_MEMORY_SIZE 0x94000
#endif

// Returns `size` bytes (16-byte aligned, not zeroed) valid until the end of
// the request, or NULL when request memory is exhausted.
void *request_alloc(HttpRequest *req, size_t size);

// Marks the current allocation level, and frees everything allocated since
// a mark. For handlers that loop (the wait tools): each pass rewinds to the
// mark taken before it, so a long wait does not exhaust request memory.
size_t request_mark(const HttpRequest *req);
void request_rewind(HttpRequest *req, size_t mark);

// Frees everything allocated for `req`. The HTTP server calls this once the
// response has been sent; handlers never do.
void request_release(HttpRequest *req);

// JSON-escapes `s` (without quotes) into request memory, for strings that
// come from the user or the file system (paths, names) and go into a JSON
// response. Returns "" when request memory is exhausted.
const char *request_json_escape(HttpRequest *req, const char *s);

// Reads the request body (16 KB at most) and parses it as a JSON object (an
// empty body counts as {}). The root object is token 0. Returns NULL after sending a 4xx error.
// Both the body and the tokens live in request memory.
JsonDoc *request_read_json(HttpRequest *req);
