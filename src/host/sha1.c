#include "host/sha1.h"

#include <string.h>

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void compress(tfx_sha1 *s, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)
            f = (b & c) | (~b & d), k = 0x5a827999;
        else if (i < 40)
            f = b ^ c ^ d, k = 0x6ed9eba1;
        else if (i < 60)
            f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdc;
        else
            f = b ^ c ^ d, k = 0xca62c1d6;
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

void tfx_sha1_init(tfx_sha1 *s) {
    static const uint32_t h0[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    memcpy(s->h, h0, sizeof h0);
    s->bytes = 0;
    s->used = 0;
}

void tfx_sha1_add(tfx_sha1 *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->bytes += n;
    while (n) {
        size_t take = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->block + s->used, p, take);
        s->used += take, p += take, n -= take;
        if (s->used == 64) {
            compress(s, s->block);
            s->used = 0;
        }
    }
}

void tfx_sha1_done(tfx_sha1 *s, uint8_t digest[20]) {
    uint64_t bits = s->bytes * 8;
    uint8_t pad = 0x80, zero = 0, len[8];
    tfx_sha1_add(s, &pad, 1);
    while (s->used != 56)
        tfx_sha1_add(s, &zero, 1);
    for (int i = 0; i < 8; i++)
        len[i] = (uint8_t)(bits >> (56 - 8 * i));
    tfx_sha1_add(s, len, 8);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++)
            digest[4 * i + j] = (uint8_t)(s->h[i] >> (24 - 8 * j));
}
