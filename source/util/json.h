#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifndef JSMN_HEADER
#define JSMN_HEADER
#endif
#include "jsmn.h"

#define JSON_MAX_TOKENS 1024

typedef struct {
    const char *src;
    jsmntok_t tok[JSON_MAX_TOKENS];
    int ntok;
} JsonDoc;

// Parses src (len bytes). Returns 0 on success, negative jsmn error otherwise.
int json_parse(JsonDoc *doc, const char *src, size_t len);

// Returns the token index of the value for `key` in object token obj, or -1.
int json_obj_get(const JsonDoc *doc, int obj, const char *key);

// Number of elements in array token arr, or -1 if not an array.
int json_arr_len(const JsonDoc *doc, int arr);

// Token index of element i in array token arr, or -1.
int json_arr_get(const JsonDoc *doc, int arr, int i);

// Value getters. Return false on type mismatch.
// json_get_string unescapes JSON escapes (\n, \", \uXXXX -> UTF-8, ...).
bool json_get_string(const JsonDoc *doc, int tok, char *out, size_t outsz);
bool json_get_int(const JsonDoc *doc, int tok, long long *out);
bool json_get_double(const JsonDoc *doc, int tok, double *out);
bool json_get_bool(const JsonDoc *doc, int tok, bool *out);

// Decimal number parsing and formatting without newlib's float-capable
// strtod/printf, which cost ~50K of code on the Switch (the build maps
// snprintf to the integer-only sniprintf; see the Makefile).
//
// json_parse_double reads an optionally signed decimal with an optional
// fraction and exponent ("-1.5e3"), after leading blanks. Returns false
// (with *end == s) when there are no digits. end may be NULL.
bool json_parse_double(const char *s, const char **end, double *out);

// Formats v with `decimals` (0-6) digits after the point, rounded like
// printf's "%.*f" (ties to even). Returns buf.
const char *json_fmt_fixed(char *buf, size_t size, double v, int decimals);

// Integer value of `key` in object token obj, or fallback when the key is
// absent or not an integer.
int json_obj_int(const JsonDoc *doc, int obj, const char *key, int fallback);

// Raw (still-escaped) token text, e.g. for echoing a JSON-RPC id. Includes
// quotes for strings. Returns false if it doesn't fit.
bool json_raw(const JsonDoc *doc, int tok, char *out, size_t outsz);

// Appends the JSON-escaped form of in[0..inlen) to out (NUL-terminated),
// returning the escaped length, or (size_t)-1 if it doesn't fit.
size_t json_escape(const char *in, size_t inlen, char *out, size_t outsz);

// Escaped length of in[0..inlen) without writing output.
size_t json_escaped_len(const char *in, size_t inlen);
