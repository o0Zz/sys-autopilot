#include "jsmn.h" // implementation (no JSMN_HEADER)
#include "util/json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int json_parse(JsonDoc *doc, const char *src, size_t len) {
    jsmn_parser p;
    jsmn_init(&p);
    doc->src = src;
    int n = jsmn_parse(&p, src, len, doc->tok, JSON_MAX_TOKENS);
    if (n < 0)
        return n;
    doc->ntok = n;
    return 0;
}

static int tok_len(const JsonDoc *doc, int tok) {
    return doc->tok[tok].end - doc->tok[tok].start;
}

static const char *tok_ptr(const JsonDoc *doc, int tok) {
    return doc->src + doc->tok[tok].start;
}

// True if tok is a string/primitive equal to s.
static bool json_streq(const JsonDoc *doc, int tok, const char *s) {
    int len = tok_len(doc, tok);
    return (int)strlen(s) == len && memcmp(tok_ptr(doc, tok), s, (size_t)len) == 0;
}

// Index of the token following tok's entire subtree.
static int json_skip(const JsonDoc *doc, int tok) {
    int end = doc->tok[tok].end;
    int i = tok + 1;
    while (i < doc->ntok && doc->tok[i].start < end)
        i++;
    return i;
}

int json_obj_get(const JsonDoc *doc, int obj, const char *key) {
    if (obj < 0 || obj >= doc->ntok || doc->tok[obj].type != JSMN_OBJECT)
        return -1;
    int i = obj + 1;
    for (int n = 0; n < doc->tok[obj].size; n++) {
        int val = i + 1;
        if (val >= doc->ntok)
            return -1;
        if (doc->tok[i].type == JSMN_STRING && json_streq(doc, i, key))
            return val;
        i = json_skip(doc, val);
    }
    return -1;
}

int json_arr_len(const JsonDoc *doc, int arr) {
    if (arr < 0 || arr >= doc->ntok || doc->tok[arr].type != JSMN_ARRAY)
        return -1;
    return doc->tok[arr].size;
}

int json_arr_get(const JsonDoc *doc, int arr, int idx) {
    if (json_arr_len(doc, arr) <= idx || idx < 0)
        return -1;
    int i = arr + 1;
    for (int n = 0; n < idx; n++)
        i = json_skip(doc, i);
    return i;
}

bool json_get_string(const JsonDoc *doc, int tok, char *out, size_t outsz) {
    if (tok < 0 || tok >= doc->ntok || doc->tok[tok].type != JSMN_STRING)
        return false;
    const char *p = tok_ptr(doc, tok);
    int len = tok_len(doc, tok);
    size_t o = 0;
    for (int i = 0; i < len; i++) {
        if (o + 4 >= outsz)
            return false;
        char c = p[i];
        if (c != '\\') {
            out[o++] = c;
            continue;
        }
        if (++i >= len)
            return false;
        switch (p[i]) {
            case '"':  out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; break;
            case '/':  out[o++] = '/';  break;
            case 'n':  out[o++] = '\n'; break;
            case 't':  out[o++] = '\t'; break;
            case 'r':  out[o++] = '\r'; break;
            case 'b':  out[o++] = '\b'; break;
            case 'f':  out[o++] = '\f'; break;
            case 'u': {
                if (i + 4 >= len)
                    return false;
                char hex[5] = { p[i+1], p[i+2], p[i+3], p[i+4], 0 };
                unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                i += 4;
                // Encode BMP codepoint as UTF-8 (surrogate pairs unsupported;
                // emitted as '?').
                if (cp < 0x80) {
                    out[o++] = (char)cp;
                } else if (cp < 0x800) {
                    out[o++] = (char)(0xC0 | (cp >> 6));
                    out[o++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    out[o++] = '?';
                } else {
                    out[o++] = (char)(0xE0 | (cp >> 12));
                    out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[o++] = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                return false;
        }
    }
    out[o] = '\0';
    return true;
}

bool json_get_int(const JsonDoc *doc, int tok, long long *out) {
    if (tok < 0 || tok >= doc->ntok || doc->tok[tok].type != JSMN_PRIMITIVE)
        return false;
    char buf[32];
    if (!json_raw(doc, tok, buf, sizeof(buf)))
        return false;
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (end == buf)
        return false;
    *out = v;
    return true;
}

int json_obj_int(const JsonDoc *doc, int obj, const char *key, int fallback) {
    long long v;
    return json_get_int(doc, json_obj_get(doc, obj, key), &v) ? (int)v : fallback;
}

bool json_get_double(const JsonDoc *doc, int tok, double *out) {
    if (tok < 0 || tok >= doc->ntok || doc->tok[tok].type != JSMN_PRIMITIVE)
        return false;
    char buf[48];
    if (!json_raw(doc, tok, buf, sizeof(buf)))
        return false;
    return json_parse_double(buf, NULL, out);
}

bool json_parse_double(const char *s, const char **end, double *out) {
    const char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    bool neg = *p == '-';
    if (*p == '-' || *p == '+')
        p++;
    // Digits accumulate into an integer mantissa (exact up to 2^53), then a
    // single scaling by a power of ten: "0.125" is 125 / 1000, exactly.
    double mant = 0;
    int digits = 0, exp10 = 0;
    for (; *p >= '0' && *p <= '9'; p++, digits++)
        mant = mant * 10 + (*p - '0');
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9'; p++, digits++, exp10--)
            mant = mant * 10 + (*p - '0');
    }
    if (digits == 0) {
        if (end)
            *end = s;
        return false;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        bool eneg = *q == '-';
        if (*q == '-' || *q == '+')
            q++;
        if (*q >= '0' && *q <= '9') {
            int e = 0;
            for (; *q >= '0' && *q <= '9'; q++)
                if (e < 1000)
                    e = e * 10 + (*q - '0');
            exp10 += eneg ? -e : e;
            p = q;
        }
    }
    double scale = 1;
    for (int i = exp10 < 0 ? -exp10 : exp10; i > 0; i--)
        scale *= 10;
    mant = exp10 < 0 ? mant / scale : mant * scale;
    if (end)
        *end = p;
    *out = neg ? -mant : mant;
    return true;
}

const char *json_fmt_fixed(char *buf, size_t size, double v, int decimals) {
    if (decimals < 0 || decimals > 6)
        decimals = decimals < 0 ? 0 : 6;
    unsigned long long scale = 1;
    for (int i = 0; i < decimals; i++)
        scale *= 10;
    // Exact, like printf: v = mant * 2^-shift, so v * scale is an integer
    // product shifted right, and the bits shifted out decide the rounding
    // (ties to even). Multiplying in floating point would round first and
    // turn e.g. 0.15 (just below) into a tie.
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    int bexp = (int)((bits >> 52) & 0x7ff);
    uint64_t mant = bits & ((1ULL << 52) - 1);
    if (bexp != 0)
        mant |= 1ULL << 52;
    else
        bexp = 1;
    int shift = 1075 - bexp;
    unsigned __int128 p = (unsigned __int128)mant * scale;
    unsigned long long n;
    if (shift <= 0) {
        n = shift > -64 ? (unsigned long long)(p << -shift) : ~0ULL;
    } else if (shift >= 128) {
        n = 0;
    } else {
        n = (unsigned long long)(p >> shift);
        unsigned __int128 rem = p & (((unsigned __int128)1 << shift) - 1);
        unsigned __int128 half = (unsigned __int128)1 << (shift - 1);
        if (rem > half || (rem == half && (n & 1)))
            n++;
    }
    const char *sign = (bits >> 63) ? "-" : "";
    if (decimals > 0)
        snprintf(buf, size, "%s%llu.%0*llu", sign, n / scale, decimals, n % scale);
    else
        snprintf(buf, size, "%s%llu", sign, n);
    return buf;
}

bool json_get_bool(const JsonDoc *doc, int tok, bool *out) {
    if (tok < 0 || tok >= doc->ntok || doc->tok[tok].type != JSMN_PRIMITIVE)
        return false;
    if (json_streq(doc, tok, "true"))  { *out = true;  return true; }
    if (json_streq(doc, tok, "false")) { *out = false; return true; }
    return false;
}

bool json_raw(const JsonDoc *doc, int tok, char *out, size_t outsz) {
    if (tok < 0 || tok >= doc->ntok)
        return false;
    int start = doc->tok[tok].start;
    int end = doc->tok[tok].end;
    if (doc->tok[tok].type == JSMN_STRING) {
        start -= 1; // include quotes
        end += 1;
    }
    size_t len = (size_t)(end - start);
    if (len + 1 > outsz)
        return false;
    memcpy(out, doc->src + start, len);
    out[len] = '\0';
    return true;
}

static size_t escape_char(char c, char *out /* >= 6 bytes or NULL */) {
    const char *rep = NULL;
    switch (c) {
        case '"':  rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\t': rep = "\\t";  break;
        case '\r': rep = "\\r";  break;
        case '\b': rep = "\\b";  break;
        case '\f': rep = "\\f";  break;
        default:
            if ((unsigned char)c < 0x20) {
                if (out)
                    snprintf(out, 7, "\\u%04x", (unsigned char)c);
                return 6;
            }
            if (out)
                *out = c;
            return 1;
    }
    if (out)
        memcpy(out, rep, 2);
    return 2;
}

size_t json_escaped_len(const char *in, size_t inlen) {
    size_t n = 0;
    for (size_t i = 0; i < inlen; i++)
        n += escape_char(in[i], NULL);
    return n;
}

size_t json_escape(const char *in, size_t inlen, char *out, size_t outsz) {
    size_t o = 0;
    for (size_t i = 0; i < inlen; i++) {
        char tmp[8];
        size_t n = escape_char(in[i], tmp);
        if (o + n + 1 > outsz)
            return (size_t)-1;
        memcpy(out + o, tmp, n);
        o += n;
    }
    out[o] = '\0';
    return o;
}
