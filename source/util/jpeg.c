// Streaming baseline JPEG transcoder: see jpeg.h.
//
// The decoder fills one MCU row ("band") of component planes at a time; the
// sink then box-averages the output rows that band covers and hands them to
// the encoder (or to a luma thumbnail). Nothing larger than a band and one
// 8-row output strip is ever held.
#include "util/jpeg.h"

#include <string.h>

#define JPEG_MAX_DIM 4096

// Natural (row-major) index of each zigzag position.
static const uint8_t kZigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// cos(k * pi / 16) for k = 0..8; spelled out so no libm is needed.
static const float kCos16[9] = {
    1.0f, 0.98078528f, 0.92387953f, 0.83146961f, 0.70710678f,
    0.55557023f, 0.38268343f, 0.19509032f, 0.0f,
};

static float cos16(int m) {
    m %= 32;
    if (m > 16)
        m = 32 - m;
    return m > 8 ? -kCos16[16 - m] : kCos16[m];
}

// Orthonormal 8-point DCT basis: m[u][x] = C(u)/2 * cos((2x+1)u*pi/16), with
// C(0) = 1/sqrt(2). The forward transform uses it as is, the inverse
// transposed.
static void dct_basis(float m[8][8]) {
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            m[u][x] = (u == 0 ? 0.70710678f : 1.0f) * 0.5f * cos16((2 * x + 1) * u);
}

static uint8_t clamp_u8(float v) {
    if (v <= 0.0f)
        return 0;
    if (v >= 255.0f)
        return 255;
    return (uint8_t)(v + 0.5f);
}

static unsigned be16(const uint8_t *p) {
    return (unsigned)p[0] << 8 | p[1];
}

// --- decoder ---------------------------------------------------------------------

typedef struct {
    uint8_t bits[17]; // bits[l]: number of codes of length l
    uint8_t vals[256];
    int32_t mincode[17], maxcode[17], valptr[17];
    bool present;
} Huff;

typedef struct {
    int id, h, v, tq, td, ta;
    int pred;       // DC predictor
    uint8_t *plane; // one band: v*8 rows of `stride` pixels
    int stride;
} Comp;

typedef struct {
    const uint8_t *p, *end; // entropy-coded data still to read
    uint32_t bits;          // bit buffer, MSB first
    int nbits;
    bool marker;            // reached a marker: feed zeros from here on
    uint16_t qt[4][64];     // zigzag order, as stored in DQT
    bool qt_present[4];
    Huff dc[4], ac[4];
    Comp comp[3];
    int ncomp, width, height, hmax, vmax, mcus_x, mcus_y, restart;
    float m[8][8];
} Dec;

static void huff_build(Huff *h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->present = true;
}

static const char *parse_frame(Dec *d, const uint8_t *seg, unsigned len) {
    if (len < 8 || seg[0] != 8)
        return "unsupported JPEG (not 8-bit)";
    d->height = (int)be16(seg + 1);
    d->width = (int)be16(seg + 3);
    d->ncomp = seg[5];
    if (d->ncomp != 1 && d->ncomp != 3)
        return "unsupported JPEG (component count)";
    if (len != 8u + 3u * (unsigned)d->ncomp)
        return "corrupt JPEG (frame header)";
    if (d->width <= 0 || d->height <= 0 || d->width > JPEG_MAX_DIM || d->height > JPEG_MAX_DIM)
        return "unsupported JPEG (dimensions)";
    d->hmax = d->vmax = 1;
    for (int i = 0; i < d->ncomp; i++) {
        Comp *c = &d->comp[i];
        c->id = seg[6 + 3 * i];
        c->h = seg[7 + 3 * i] >> 4;
        c->v = seg[7 + 3 * i] & 15;
        c->tq = seg[8 + 3 * i];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3)
            return "corrupt JPEG (frame header)";
        if (c->h > d->hmax) d->hmax = c->h;
        if (c->v > d->vmax) d->vmax = c->v;
    }
    // A single-component scan is not interleaved: its MCU is one block,
    // whatever sampling factor the frame declares.
    if (d->ncomp == 1)
        d->comp[0].h = d->comp[0].v = d->hmax = d->vmax = 1;
    d->mcus_x = (d->width + 8 * d->hmax - 1) / (8 * d->hmax);
    d->mcus_y = (d->height + 8 * d->vmax - 1) / (8 * d->vmax);
    return NULL;
}

static const char *parse_scan(Dec *d, const uint8_t *seg, unsigned len) {
    if (d->ncomp == 0)
        return "corrupt JPEG (scan before frame)";
    int ns = seg[0];
    if (ns != d->ncomp)
        return "unsupported JPEG (multi-scan)";
    if (len != 6u + 2u * (unsigned)ns)
        return "corrupt JPEG (scan header)";
    for (int i = 0; i < ns; i++) {
        Comp *c = NULL;
        for (int j = 0; j < d->ncomp; j++)
            if (d->comp[j].id == seg[1 + 2 * i])
                c = &d->comp[j];
        if (!c)
            return "corrupt JPEG (scan component)";
        c->td = seg[2 + 2 * i] >> 4;
        c->ta = seg[2 + 2 * i] & 15;
        if (c->td > 3 || c->ta > 3 || !d->dc[c->td].present || !d->ac[c->ta].present ||
            !d->qt_present[c->tq])
            return "corrupt JPEG (missing table)";
    }
    const uint8_t *tail = seg + 1 + 2 * ns;
    if (tail[0] != 0 || tail[1] != 63 || tail[2] != 0)
        return "unsupported JPEG (not baseline)";
    return NULL;
}

// Parses everything up to the start of the entropy-coded data, leaving d->p
// there.
static const char *parse_headers(Dec *d) {
    const uint8_t *p = d->p, *end = d->end;
    if (end - p < 2 || p[0] != 0xFF || p[1] != 0xD8)
        return "not a JPEG";
    p += 2;
    for (;;) {
        if (p >= end || *p != 0xFF)
            return "corrupt JPEG (marker expected)";
        while (p < end && *p == 0xFF)
            p++;
        if (p >= end)
            return "truncated JPEG";
        uint8_t m = *p++;
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7))
            continue; // no payload
        if (m == 0xD9)
            return "corrupt JPEG (no image data)";
        if (end - p < 2)
            return "truncated JPEG";
        unsigned len = be16(p);
        if (len < 2 || (size_t)(end - p) < len)
            return "truncated JPEG";
        const uint8_t *seg = p + 2, *seg_end = p + len;
        p = seg_end;
        const char *err = NULL;
        switch (m) {
        case 0xDB: // DQT
            while (seg < seg_end) {
                int pq = seg[0] >> 4, tq = seg[0] & 15;
                seg++;
                if (tq > 3 || pq > 1 || seg_end - seg < (pq ? 128 : 64))
                    return "corrupt JPEG (DQT)";
                for (int i = 0; i < 64; i++)
                    d->qt[tq][i] = (uint16_t)(pq ? be16(seg + 2 * i) : seg[i]);
                d->qt_present[tq] = true;
                seg += pq ? 128 : 64;
            }
            break;
        case 0xC4: // DHT
            while (seg < seg_end) {
                if (seg_end - seg < 17)
                    return "corrupt JPEG (DHT)";
                int tc = seg[0] >> 4, th = seg[0] & 15;
                if (tc > 1 || th > 3)
                    return "corrupt JPEG (DHT)";
                Huff *h = tc ? &d->ac[th] : &d->dc[th];
                int total = 0;
                for (int l = 1; l <= 16; l++) {
                    h->bits[l] = seg[l];
                    total += seg[l];
                }
                seg += 17;
                if (total > 256 || seg_end - seg < total)
                    return "corrupt JPEG (DHT)";
                memcpy(h->vals, seg, (size_t)total);
                seg += total;
                huff_build(h);
            }
            break;
        case 0xDD: // DRI
            if (len != 4)
                return "corrupt JPEG (DRI)";
            d->restart = (int)be16(seg);
            break;
        case 0xC0: case 0xC1: // baseline / extended sequential, Huffman
            err = parse_frame(d, seg, len);
            break;
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
        case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            return "unsupported JPEG (progressive, lossless or arithmetic)";
        case 0xDA: // SOS
            err = parse_scan(d, seg, len);
            if (!err)
                d->p = seg_end;
            return err;
        default: // APPn, COM, ...
            break;
        }
        if (err)
            return err;
    }
}

static void fill(Dec *d) {
    while (d->nbits <= 24) {
        uint32_t b = 0;
        if (!d->marker && d->p < d->end) {
            b = *d->p++;
            if (b == 0xFF) {
                uint8_t next = d->p < d->end ? *d->p : 0xD9;
                if (next == 0x00) {
                    d->p++; // stuffed byte
                } else {
                    d->marker = true; // leave d->p on the marker
                    d->p--;
                    b = 0;
                }
            }
        }
        d->bits |= b << (24 - d->nbits);
        d->nbits += 8;
    }
}

static int getbits(Dec *d, int n) {
    if (n == 0)
        return 0;
    fill(d);
    int v = (int)(d->bits >> (32 - n));
    d->bits <<= n;
    d->nbits -= n;
    return v;
}

static int extend(int v, int n) {
    return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
}

static int huff_decode(Dec *d, const Huff *h) {
    int code = getbits(d, 1);
    for (int l = 1; l <= 16; l++) {
        if (code <= h->maxcode[l])
            return h->vals[h->valptr[l] + code - h->mincode[l]];
        code = (code << 1) | getbits(d, 1);
    }
    return -1;
}

// Skips to just past the next RSTn marker and resets the predictors.
static bool restart(Dec *d) {
    d->bits = 0;
    d->nbits = 0;
    d->marker = false;
    while (d->p + 1 < d->end && !(d->p[0] == 0xFF && d->p[1] >= 0xD0 && d->p[1] <= 0xD7))
        d->p++;
    if (d->p + 1 >= d->end)
        return false;
    d->p += 2;
    for (int i = 0; i < d->ncomp; i++)
        d->comp[i].pred = 0;
    return true;
}

// Decodes one block's coefficients (dequantized, natural order) into blk,
// or only advances through the data when blk is NULL.
static bool decode_block(Dec *d, Comp *c, float *blk) {
    int t = huff_decode(d, &d->dc[c->td]);
    if (t < 0 || t > 11)
        return false;
    c->pred += t ? extend(getbits(d, t), t) : 0;
    const uint16_t *q = d->qt[c->tq];
    if (blk) {
        memset(blk, 0, 64 * sizeof(float));
        blk[0] = (float)(c->pred * (int)q[0]);
    }
    for (int k = 1; k < 64;) {
        int rs = huff_decode(d, &d->ac[c->ta]);
        if (rs < 0)
            return false;
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15)
                break; // EOB
            k += 16;
            continue;
        }
        k += r;
        if (k > 63)
            return false;
        int v = extend(getbits(d, s), s);
        if (blk)
            blk[kZigzag[k]] = (float)(v * (int)q[k]);
        k++;
    }
    return true;
}

static void idct_store(const Dec *d, const float *blk, uint8_t *dst, int stride, bool dc_only) {
    bool flat = dc_only;
    if (!flat) {
        flat = true;
        for (int i = 1; i < 64 && flat; i++)
            flat = blk[i] == 0.0f;
    }
    if (flat) {
        uint8_t v = clamp_u8(blk[0] / 8.0f + 128.0f);
        for (int y = 0; y < 8; y++)
            memset(dst + y * stride, v, 8);
        return;
    }
    float tmp[64];
    for (int v = 0; v < 8; v++)
        for (int x = 0; x < 8; x++) {
            float s = 0.0f;
            for (int u = 0; u < 8; u++)
                s += d->m[u][x] * blk[v * 8 + u];
            tmp[v * 8 + x] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 128.0f;
            for (int v = 0; v < 8; v++)
                s += d->m[v][y] * tmp[v * 8 + x];
            dst[y * stride + x] = clamp_u8(s);
        }
}

static const char *dec_open(const uint8_t *in, size_t in_len, JpegAlloc alloc, void *ctx,
                            Dec **out) {
    Dec *d = alloc(ctx, sizeof(*d));
    if (!d)
        return "out of memory";
    memset(d, 0, sizeof(*d));
    d->p = in;
    d->end = in + in_len;
    const char *err = parse_headers(d);
    if (err)
        return err;
    for (int i = 0; i < d->ncomp; i++) {
        Comp *c = &d->comp[i];
        c->stride = d->mcus_x * c->h * 8;
        c->plane = alloc(ctx, (size_t)c->stride * (size_t)(c->v * 8));
        if (!c->plane)
            return "out of memory";
    }
    dct_basis(d->m);
    *out = d;
    return NULL;
}

// --- encoder ---------------------------------------------------------------------

static const uint8_t kStdLumQ[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,     12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,     14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,   24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};
static const uint8_t kStdChromQ[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

// The standard Huffman tables (ITU T.81 Annex K.3): code counts per length
// 1..16, then the symbols.
static const uint8_t kDcLumBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kDcChromBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t kDcVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t kAcLumBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t kAcLumVals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51,
    0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1,
    0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8,
    0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2,
    0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};
static const uint8_t kAcChromBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t kAcChromVals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07,
    0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09,
    0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2,
    0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};

enum { T_DC_LUM, T_AC_LUM, T_DC_CHROM, T_AC_CHROM };

typedef struct {
    uint8_t *out;
    size_t pos, cap;
    const uint8_t *const *guard; // decoder read position when out aliases in
    uint32_t bits;
    int nbits;
    bool overflow;
    uint8_t q[2][64];      // zigzag order, for DQT
    float qinv[2][64];     // natural order
    uint16_t code[4][256];
    uint8_t size[4][256];
    int pred[3];
    float m[8][8];
} Enc;

static void put_byte(Enc *e, uint8_t b) {
    const uint8_t *limit = e->out + e->cap;
    if (e->guard && *e->guard < limit)
        limit = *e->guard;
    if (e->out + e->pos >= limit) {
        e->overflow = true;
        return;
    }
    e->out[e->pos++] = b;
}

static void put_bytes(Enc *e, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        put_byte(e, b[i]);
}

static void put_u16(Enc *e, unsigned v) {
    put_byte(e, (uint8_t)(v >> 8));
    put_byte(e, (uint8_t)v);
}

static void put_bits(Enc *e, uint32_t code, int n) {
    e->bits = (e->bits << n) | (code & ((1u << n) - 1));
    e->nbits += n;
    while (e->nbits >= 8) {
        uint8_t b = (uint8_t)(e->bits >> (e->nbits - 8));
        put_byte(e, b);
        if (b == 0xFF)
            put_byte(e, 0); // stuffing
        e->nbits -= 8;
    }
    e->bits &= (1u << e->nbits) - 1;
}

static void huff_codes(const uint8_t bits[16], const uint8_t *vals, uint16_t *code, uint8_t *size) {
    int k = 0, c = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < bits[l - 1]; i++, k++) {
            code[vals[k]] = (uint16_t)c++;
            size[vals[k]] = (uint8_t)l;
        }
        c <<= 1;
    }
}

static void put_dht(Enc *e, int class_id, const uint8_t bits[16], const uint8_t *vals) {
    int n = 0;
    for (int i = 0; i < 16; i++)
        n += bits[i];
    put_byte(e, (uint8_t)class_id);
    put_bytes(e, bits, 16);
    put_bytes(e, vals, (size_t)n);
}

static void enc_init(Enc *e, int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;
    for (int k = 0; k < 64; k++) {
        int nat = kZigzag[k];
        int lq = (kStdLumQ[nat] * scale + 50) / 100;
        int cq = (kStdChromQ[nat] * scale + 50) / 100;
        lq = lq < 1 ? 1 : lq > 255 ? 255 : lq;
        cq = cq < 1 ? 1 : cq > 255 ? 255 : cq;
        e->q[0][k] = (uint8_t)lq;
        e->q[1][k] = (uint8_t)cq;
        e->qinv[0][nat] = 1.0f / (float)lq;
        e->qinv[1][nat] = 1.0f / (float)cq;
    }
    huff_codes(kDcLumBits, kDcVals, e->code[T_DC_LUM], e->size[T_DC_LUM]);
    huff_codes(kAcLumBits, kAcLumVals, e->code[T_AC_LUM], e->size[T_AC_LUM]);
    huff_codes(kDcChromBits, kDcVals, e->code[T_DC_CHROM], e->size[T_DC_CHROM]);
    huff_codes(kAcChromBits, kAcChromVals, e->code[T_AC_CHROM], e->size[T_AC_CHROM]);
    dct_basis(e->m);
}

static void enc_header(Enc *e, int w, int h) {
    static const uint8_t kSoiApp0[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    };
    put_bytes(e, kSoiApp0, sizeof(kSoiApp0));

    put_u16(e, 0xFFDB);
    put_u16(e, 2 + 2 * 65);
    put_byte(e, 0);
    put_bytes(e, e->q[0], 64);
    put_byte(e, 1);
    put_bytes(e, e->q[1], 64);

    put_u16(e, 0xFFC0);
    put_u16(e, 17);
    put_byte(e, 8);
    put_u16(e, (unsigned)h);
    put_u16(e, (unsigned)w);
    static const uint8_t kComps[] = {3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1};
    put_bytes(e, kComps, sizeof(kComps));

    put_u16(e, 0xFFC4);
    put_u16(e, 2 + 4 * 17 + 12 + 12 + 162 + 162);
    put_dht(e, 0x00, kDcLumBits, kDcVals);
    put_dht(e, 0x10, kAcLumBits, kAcLumVals);
    put_dht(e, 0x01, kDcChromBits, kDcVals);
    put_dht(e, 0x11, kAcChromBits, kAcChromVals);

    static const uint8_t kSos[] = {
        0xFF, 0xDA, 0x00, 0x0C, 3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0,
    };
    put_bytes(e, kSos, sizeof(kSos));
}

static void put_value(Enc *e, int table, int run, int v) {
    int a = v < 0 ? -v : v, n = 0;
    while (a) {
        n++;
        a >>= 1;
    }
    int sym = (run << 4) | n;
    put_bits(e, e->code[table][sym], e->size[table][sym]);
    if (n)
        put_bits(e, (uint32_t)(v < 0 ? v - 1 : v), n);
}

// One 8x8 block of samples (stride 8). qi: 0 luma, 1 chroma.
static void encode_block(Enc *e, const uint8_t *src, int qi, int *pred) {
    float tmp[64], f[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            float s = 0.0f;
            for (int x = 0; x < 8; x++)
                s += e->m[u][x] * (float)(src[y * 8 + x] - 128);
            tmp[y * 8 + u] = s;
        }
    for (int v = 0; v < 8; v++)
        for (int u = 0; u < 8; u++) {
            float s = 0.0f;
            for (int y = 0; y < 8; y++)
                s += e->m[v][y] * tmp[y * 8 + u];
            f[v * 8 + u] = s;
        }

    int zq[64];
    for (int k = 0; k < 64; k++) {
        float v = f[kZigzag[k]] * e->qinv[qi][kZigzag[k]];
        zq[k] = (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
        // Baseline AC tables stop at magnitude category 10.
        if (k > 0 && zq[k] > 1023) zq[k] = 1023;
        if (k > 0 && zq[k] < -1023) zq[k] = -1023;
    }
    int dc_table = qi ? T_DC_CHROM : T_DC_LUM;
    int ac_table = qi ? T_AC_CHROM : T_AC_LUM;
    put_value(e, dc_table, 0, zq[0] - *pred);
    *pred = zq[0];
    int run = 0;
    for (int k = 1; k < 64; k++) {
        if (zq[k] == 0) {
            run++;
            continue;
        }
        while (run > 15) {
            put_bits(e, e->code[ac_table][0xF0], e->size[ac_table][0xF0]);
            run -= 16;
        }
        put_value(e, ac_table, run, zq[k]);
        run = 0;
    }
    if (run)
        put_bits(e, e->code[ac_table][0x00], e->size[ac_table][0x00]);
}

// --- sink: box filter, then the encoder or a luma thumbnail ----------------------

typedef struct {
    int div, x0, y0, x1, y1; // crop in source pixels, x0/y0 multiples of div
    int out_w, out_h, oy;    // output size, next output row
    Enc *enc;                // NULL: thumbnail
    uint8_t *strip[3];       // 8 rows x strip_stride, Y Cb Cr
    int strip_stride, strip_rows;
    uint8_t *thumb;
} Sink;

static void encode_strip(Sink *s) {
    Enc *e = s->enc;
    uint8_t blk[64];
    for (int bx = 0; bx < s->strip_stride / 8; bx++) {
        for (int c = 0; c < 3; c++) {
            for (int y = 0; y < 8; y++) {
                const uint8_t *row = s->strip[c] + (y < s->strip_rows ? y : s->strip_rows - 1) *
                                                       s->strip_stride;
                for (int x = 0; x < 8; x++) {
                    int sx = bx * 8 + x;
                    blk[y * 8 + x] = row[sx < s->out_w ? sx : s->out_w - 1];
                }
            }
            encode_block(e, blk, c ? 1 : 0, &e->pred[c]);
        }
    }
    s->strip_rows = 0;
}

static uint8_t box_avg(const Dec *d, const Comp *c, int band_y0, int sx0, int sx1, int sy0, int sy1) {
    int cx0 = sx0 * c->h / d->hmax;
    int cx1 = (sx1 * c->h + d->hmax - 1) / d->hmax;
    int cy0 = (sy0 - band_y0) * c->v / d->vmax;
    int cy1 = ((sy1 - band_y0) * c->v + d->vmax - 1) / d->vmax;
    if (cx1 <= cx0) cx1 = cx0 + 1;
    if (cy1 <= cy0) cy1 = cy0 + 1;
    unsigned sum = 0;
    for (int y = cy0; y < cy1; y++) {
        const uint8_t *row = c->plane + y * c->stride;
        for (int x = cx0; x < cx1; x++)
            sum += row[x];
    }
    unsigned n = (unsigned)((cx1 - cx0) * (cy1 - cy0));
    return (uint8_t)((sum + n / 2) / n);
}

// Emits every output row whose source rows lie in the band [band_y0, band_y1).
static void emit_rows(const Dec *d, Sink *s, int band_y0, int band_y1) {
    while (s->oy < s->out_h) {
        int sy0 = s->y0 + s->oy * s->div;
        if (sy0 >= band_y1)
            return;
        int sy1 = sy0 + s->div < s->y1 ? sy0 + s->div : s->y1;
        int ncomp = s->enc ? 3 : 1;
        for (int c = 0; c < ncomp; c++) {
            uint8_t *dst = s->enc ? s->strip[c] + s->strip_rows * s->strip_stride
                                  : s->thumb + (size_t)s->oy * (size_t)s->out_w;
            for (int ox = 0; ox < s->out_w; ox++) {
                int sx0 = s->x0 + ox * s->div;
                int sx1 = sx0 + s->div < s->x1 ? sx0 + s->div : s->x1;
                dst[ox] = c < d->ncomp ? box_avg(d, &d->comp[c], band_y0, sx0, sx1, sy0, sy1)
                                       : 128; // grayscale input: neutral chroma
            }
        }
        s->oy++;
        if (s->enc && ++s->strip_rows == 8)
            encode_strip(s);
    }
}

static const char *dec_run(Dec *d, Sink *s) {
    float blk[64];
    bool dc_only = s->div == 8; // a block's mean is all an 8x8 box needs
    int mcu = 0;
    int band_h = 8 * d->vmax;
    for (int my = 0; my < d->mcus_y && s->oy < s->out_h; my++) {
        int band_y0 = my * band_h;
        int band_y1 = band_y0 + band_h < d->height ? band_y0 + band_h : d->height;
        // Bands outside the crop still have to be entropy-decoded, but not
        // transformed.
        bool needed = band_y1 > s->y0 && band_y0 < s->y1;
        for (int mx = 0; mx < d->mcus_x; mx++) {
            if (d->restart && mcu && mcu % d->restart == 0 && !restart(d))
                return "corrupt JPEG (restart marker)";
            for (int i = 0; i < d->ncomp; i++) {
                Comp *c = &d->comp[i];
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        if (!decode_block(d, c, needed ? blk : NULL))
                            return "corrupt JPEG data";
                        if (needed)
                            idct_store(d, blk,
                                       c->plane + by * 8 * c->stride + (mx * c->h + bx) * 8,
                                       c->stride, dc_only);
                    }
            }
            mcu++;
        }
        if (needed)
            emit_rows(d, s, band_y0, band_y1);
    }
    return NULL;
}

// Crop and output geometry, shared by both entry points.
static const char *sink_geometry(Sink *s, const Dec *d, int div, int cx, int cy, int cw, int ch) {
    if (div != 1 && div != 2 && div != 4 && div != 8)
        return "scale must be 1, 1/2, 1/4 or 1/8";
    s->div = div;
    s->x0 = 0;
    s->y0 = 0;
    s->x1 = d->width;
    s->y1 = d->height;
    if (cw > 0 && ch > 0) {
        s->x0 = cx < 0 ? 0 : cx > d->width ? d->width : cx;
        s->y0 = cy < 0 ? 0 : cy > d->height ? d->height : cy;
        long x1 = (long)cx + cw, y1 = (long)cy + ch;
        s->x1 = x1 > d->width ? d->width : (int)x1;
        s->y1 = y1 > d->height ? d->height : (int)y1;
        if (s->x1 <= s->x0 || s->y1 <= s->y0)
            return "crop rectangle is outside the image";
    }
    s->x0 -= s->x0 % div;
    s->y0 -= s->y0 % div;
    s->out_w = (s->x1 - s->x0 + div - 1) / div;
    s->out_h = (s->y1 - s->y0 + div - 1) / div;
    return NULL;
}

const char *jpeg_scale(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                       const JpegScaleOpts *opts, JpegAlloc alloc, void *alloc_ctx,
                       size_t *out_len, int *out_w, int *out_h) {
    Dec *d = NULL;
    const char *err = dec_open(in, in_len, alloc, alloc_ctx, &d);
    if (err)
        return err;
    Sink s = {0};
    err = sink_geometry(&s, d, opts->div, opts->crop_x, opts->crop_y, opts->crop_w, opts->crop_h);
    if (err)
        return err;

    Enc *e = alloc(alloc_ctx, sizeof(*e));
    s.strip_stride = (s.out_w + 7) & ~7;
    for (int c = 0; c < 3; c++)
        s.strip[c] = alloc(alloc_ctx, (size_t)s.strip_stride * 8);
    if (!e || !s.strip[0] || !s.strip[1] || !s.strip[2])
        return "out of memory";
    memset(e, 0, sizeof(*e));
    e->out = out;
    e->cap = out_cap;
    // Only guard when the output really sits below the input in one buffer.
    if (out < in && out + out_cap > in)
        e->guard = &d->p;
    enc_init(e, opts->quality);
    s.enc = e;

    enc_header(e, s.out_w, s.out_h);
    err = dec_run(d, &s);
    if (err)
        return err;
    if (s.strip_rows > 0)
        encode_strip(&s);
    put_bits(e, 0x7F, 7); // pad the last byte with ones
    put_u16(e, 0xFFD9);
    if (e->overflow)
        return "output buffer too small (lower the quality or scale)";
    *out_len = e->pos;
    if (out_w) *out_w = s.out_w;
    if (out_h) *out_h = s.out_h;
    return NULL;
}

const char *jpeg_luma_thumbnail(const uint8_t *in, size_t in_len, int div,
                                uint8_t *out, size_t out_cap, int *out_w, int *out_h,
                                JpegAlloc alloc, void *alloc_ctx) {
    Dec *d = NULL;
    const char *err = dec_open(in, in_len, alloc, alloc_ctx, &d);
    if (err)
        return err;
    Sink s = {0};
    err = sink_geometry(&s, d, div, 0, 0, 0, 0);
    if (err)
        return err;
    if ((size_t)s.out_w * (size_t)s.out_h > out_cap)
        return "thumbnail buffer too small";
    s.thumb = out;
    err = dec_run(d, &s);
    if (err)
        return err;
    *out_w = s.out_w;
    *out_h = s.out_h;
    return NULL;
}

bool jpeg_get_size(const uint8_t *in, size_t in_len, int *w, int *h) {
    const uint8_t *p = in, *end = in + in_len;
    if (in_len < 4 || p[0] != 0xFF || p[1] != 0xD8)
        return false;
    p += 2;
    while (end - p >= 4 && p[0] == 0xFF) {
        uint8_t m = p[1];
        unsigned len = be16(p + 2);
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (len < 7 || (size_t)(end - p) < 2 + (size_t)len)
                return false;
            *h = (int)be16(p + 5);
            *w = (int)be16(p + 7);
            return true;
        }
        if (m == 0xDA || len < 2)
            return false;
        p += 2 + len;
    }
    return false;
}
