#include "host/flac.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BLOCK 4608
#define MAX_PART 4 /* partition orders 0 .. 4 */

static uint8_t crc8(const uint8_t *p, size_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (uint8_t)(c & 0x80 ? c << 1 ^ 0x07 : c << 1);
    }
    return c;
}

static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (int k = 0; k < 8; k++)
            c = (uint16_t)(c & 0x8000 ? c << 1 ^ 0x8005 : c << 1);
    }
    return c;
}

/* --- the encoder ------------------------------------------------------------------- */

typedef struct bw {
    uint8_t *p;
    size_t cap, n;
    uint64_t acc;
    int count, over;
} bw;

/* k bits of v (k <= 32), the most significant first */
static void wb(bw *b, uint32_t v, int k) {
    if (!k)
        return;
    b->acc = b->acc << k | (k == 32 ? v : v & ((1u << k) - 1));
    b->count += k;
    while (b->count >= 8) {
        b->count -= 8;
        if (b->n < b->cap)
            b->p[b->n] = (uint8_t)(b->acc >> b->count);
        else
            b->over = 1;
        b->n++;
    }
}

static void unary(bw *b, uint32_t q) { /* q zeros, then a one */
    while (q >= 32)
        wb(b, 0, 32), q -= 32;
    wb(b, 1, (int)q + 1);
}

/* a channel's best subframe: its kind and its cost in bits */
typedef struct plan {
    int kind; /* 0 constant, 1 verbatim, 2 fixed */
    int order, part;
    uint8_t k[1 << MAX_PART];
    uint64_t bits;
} plan;

static void residual(const int32_t *x, int n, int order, int32_t *r) {
    for (int i = order; i < n; i++)
        switch (order) {
        case 0: r[i] = x[i]; break;
        case 1: r[i] = x[i] - x[i - 1]; break;
        case 2: r[i] = x[i] - 2 * x[i - 1] + x[i - 2]; break;
        case 3: r[i] = x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3]; break;
        default: r[i] = x[i] - 4 * x[i - 1] + 6 * x[i - 2] - 4 * x[i - 3] + x[i - 4]; break;
        }
}

static uint32_t zigzag(int32_t r) { return (uint32_t)r << 1 ^ (uint32_t)(r >> 31); }

/* the Rice parameter for a partition (n residuals summing s, zigzagged)
 * and its cost, estimated (n (k + 1) + s >> k) */
static int rice_k(uint64_t s, uint32_t n, uint64_t *cost) {
    int best = 0;
    uint64_t bc = ~0ull;
    for (int k = 0; k <= 14; k++) {
        uint64_t c = (uint64_t)n * (uint64_t)(k + 1) + (s >> k);
        if (c < bc)
            bc = c, best = k;
    }
    *cost = bc + 4;
    return best;
}

static plan choose(const int32_t *x, int n, int bps) {
    plan best = {1, 0, 0, {0}, 8 + (uint64_t)n * (uint64_t)bps};
    int same = 1;
    for (int i = 1; i < n && same; i++)
        same = x[i] == x[0];
    if (same) {
        best.kind = 0, best.bits = 8 + (uint64_t)bps;
        return best;
    }
    int32_t rr[MAX_BLOCK];
    int max_part = 0;
    while (max_part < MAX_PART && n % (2 << max_part) == 0)
        max_part++;
    for (int order = 0; order <= 4 && order < n; order++) {
        residual(x, n, order, rr);
        uint64_t sum[1 << MAX_PART] = {0};
        int parts = 1 << max_part, len = n >> max_part;
        for (int p = 0; p < parts; p++)
            for (int i = p * len > order ? p * len : order; i < (p + 1) * len; i++)
                sum[p] += zigzag(rr[i]);
        for (int po = max_part; po >= 0; po--) {
            int np = 1 << po, plen = n >> po;
            if (plen <= order)
                continue;
            uint64_t bits = 8 + (uint64_t)order * (uint64_t)bps + 6, c;
            plan t = {2, order, po, {0}, 0};
            for (int p = 0; p < np; p++) {
                uint64_t s = 0;
                for (int q = p << (max_part - po); q < (p + 1) << (max_part - po); q++)
                    s += sum[q];
                t.k[p] = (uint8_t)rice_k(s, (uint32_t)(plen - (p ? 0 : order)), &c);
                bits += c;
            }
            t.bits = bits;
            if (bits < best.bits)
                best = t;
        }
    }
    return best;
}

static void put_subframe(bw *b, const int32_t *x, int n, int bps, const plan *p) {
    if (p->kind == 0) {
        wb(b, 0, 8);
        wb(b, (uint32_t)x[0], bps);
        return;
    }
    if (p->kind == 1) {
        wb(b, 1 << 1, 8);
        for (int i = 0; i < n; i++)
            wb(b, (uint32_t)x[i], bps);
        return;
    }
    wb(b, (uint32_t)(8 + p->order) << 1, 8);
    for (int i = 0; i < p->order; i++)
        wb(b, (uint32_t)x[i], bps);
    int32_t r[MAX_BLOCK];
    residual(x, n, p->order, r);
    wb(b, 0, 2), wb(b, (uint32_t)p->part, 4);
    int np = 1 << p->part, plen = n >> p->part;
    for (int q = 0; q < np; q++) {
        int k = p->k[q];
        wb(b, (uint32_t)k, 4);
        for (int i = q ? q * plen : p->order; i < (q + 1) * plen; i++) {
            uint32_t u = zigzag(r[i]);
            unary(b, u >> k);
            wb(b, u, k);
        }
    }
}

size_t tfx_flac_encode(const int16_t *pcm, int n, int block, uint8_t *out, size_t cap) {
    bw b = {out, cap, 0, 0, 0, 0};
    if (block > MAX_BLOCK || block < 16)
        return 0;
    int32_t *ch = malloc(sizeof(int32_t) * 4 * (size_t)block); /* L, R, mid, side */
    if (!ch)
        return 0;
    for (int at = 0, frame = 0; at < n; at += block, frame++) {
        int bs = n - at < block ? n - at : block;
        int32_t *l = ch, *r = ch + block, *m = ch + 2 * block, *s = ch + 3 * block;
        for (int i = 0; i < bs; i++) {
            l[i] = pcm[2 * (at + i)], r[i] = pcm[2 * (at + i) + 1];
            m[i] = (l[i] + r[i]) >> 1, s[i] = l[i] - r[i];
        }
        plan pl = choose(l, bs, 16), pr = choose(r, bs, 16), pm = choose(m, bs, 16), ps = choose(s, bs, 17);
        /* the channels: independent, left / side, side / right, mid / side */
        uint64_t cost[4] = {pl.bits + pr.bits, pl.bits + ps.bits, ps.bits + pr.bits, pm.bits + ps.bits};
        int mode = 0;
        for (int i = 1; i < 4; i++)
            if (cost[i] < cost[mode])
                mode = i;
        size_t start = b.n;
        wb(&b, 0xfff8, 16);
        wb(&b, 7, 4); /* the block size: 16 bits after the frame number */
        wb(&b, 9, 4); /* 44100 Hz */
        wb(&b, mode ? 7 + (uint32_t)mode : 1, 4);
        wb(&b, 4, 3), wb(&b, 0, 1); /* 16 bits a sample */
        if (frame < 128)
            wb(&b, (uint32_t)frame, 8);
        else
            wb(&b, 0xc0 | (uint32_t)frame >> 6, 8), wb(&b, 0x80 | (frame & 63), 8);
        wb(&b, (uint32_t)bs - 1, 16);
        if (b.over)
            break;
        wb(&b, crc8(out + start, b.n - start), 8);
        switch (mode) {
        case 0: put_subframe(&b, l, bs, 16, &pl), put_subframe(&b, r, bs, 16, &pr); break;
        case 1: put_subframe(&b, l, bs, 16, &pl), put_subframe(&b, s, bs, 17, &ps); break;
        case 2: put_subframe(&b, s, bs, 17, &ps), put_subframe(&b, r, bs, 16, &pr); break;
        default: put_subframe(&b, m, bs, 16, &pm), put_subframe(&b, s, bs, 17, &ps); break;
        }
        if (b.count)
            wb(&b, 0, 8 - b.count);
        if (b.over)
            break;
        wb(&b, crc16(out + start, b.n - start), 16);
    }
    free(ch);
    return b.over ? 0 : b.n;
}

/* --- the decoder ------------------------------------------------------------------- */

typedef struct br {
    const uint8_t *p;
    size_t n, at; /* in bits */
    int bad;
} br;

static uint32_t rb(br *b, int k) {
    uint32_t v = 0;
    if (b->at + (size_t)k > 8 * b->n) {
        b->bad = 1;
        return 0;
    }
    while (k > 0) {
        size_t byte = b->at >> 3;
        int off = (int)(b->at & 7), take = 8 - off < k ? 8 - off : k;
        v = v << take | (uint32_t)(b->p[byte] >> (8 - off - take) & ((1 << take) - 1));
        b->at += (size_t)take, k -= take;
    }
    return v;
}

static int32_t rs(br *b, int k) {
    uint32_t v = rb(b, k);
    return k && k < 32 && (v >> (k - 1) & 1) ? (int32_t)(v | ~0u << k) : (int32_t)v;
}

static uint32_t read_unary(br *b) {
    uint32_t q = 0;
    while (!b->bad && !rb(b, 1))
        q++;
    return q;
}

static int subframe(br *b, int32_t *x, int n, int bps) {
    if (rb(b, 1))
        return 0;
    int type = (int)rb(b, 6), wasted = 0;
    if (rb(b, 1))
        wasted = (int)read_unary(b) + 1, bps -= wasted;
    if (type == 0) {
        int32_t v = rs(b, bps);
        for (int i = 0; i < n; i++)
            x[i] = v;
    } else if (type == 1) {
        for (int i = 0; i < n; i++)
            x[i] = rs(b, bps);
    } else if (type >= 8 && type <= 12) {
        int order = type - 8;
        if (order > n)
            return 0;
        for (int i = 0; i < order; i++)
            x[i] = rs(b, bps);
        int method = (int)rb(b, 2), po = (int)rb(b, 4), np = 1 << po;
        if (method > 1 || n % np || (n >> po) < order)
            return 0;
        int pbits = method ? 5 : 4, esc = method ? 31 : 15;
        for (int p = 0, i = order; p < np; p++) {
            int k = (int)rb(b, pbits), end = (p + 1) * (n >> po);
            if (k == esc) {
                int raw = (int)rb(b, 5);
                for (; i < end; i++)
                    x[i] = rs(b, raw);
            } else
                for (; i < end && !b->bad; i++) {
                    uint32_t u = read_unary(b) << k | rb(b, k);
                    x[i] = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
                }
        }
        for (int i = order; i < n; i++) /* the residual on the prediction */
            switch (order) {
            case 1: x[i] += x[i - 1]; break;
            case 2: x[i] += 2 * x[i - 1] - x[i - 2]; break;
            case 3: x[i] += 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3]; break;
            case 4: x[i] += 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4]; break;
            }
    } else
        return 0; /* (LPC subframes: other encoders') */
    if (wasted)
        for (int i = 0; i < n; i++)
            x[i] = (int32_t)((uint32_t)x[i] << wasted);
    return !b->bad;
}

size_t tfx_flac_decode(const uint8_t *in, size_t len, int16_t *pcm, int n) {
    br b = {in, len, 0, 0};
    int32_t *a = malloc(sizeof(int32_t) * 2 * 65536);
    int made = 0, ok = a != NULL;
    while (ok && made < n) {
        size_t start = b.at >> 3;
        if ((rb(&b, 15) != 0x7ffc) | rb(&b, 1)) { /* (the sync code; a fixed block size) */
            ok = 0;
            break;
        }
        int bsc = (int)rb(&b, 4), src = (int)rb(&b, 4), chm = (int)rb(&b, 4), ssc = (int)rb(&b, 3);
        rb(&b, 1);
        uint32_t first = rb(&b, 8); /* the frame number, UTF-8 */
        for (int extra = first >= 0xc0 ? (first >= 0xe0 ? (first >= 0xf0 ? 3 : 2) : 1) : 0; extra > 0; extra--)
            rb(&b, 8);
        int bs = bsc == 1 ? 192 : bsc <= 5 ? 576 << (bsc - 2) : bsc == 6 ? (int)rb(&b, 8) + 1 : bsc == 7 ? (int)rb(&b, 16) + 1
                 : 256 << (bsc - 8);
        if (src == 12)
            rb(&b, 8);
        else if (src == 13 || src == 14)
            rb(&b, 16);
        rb(&b, 8); /* (the header's CRC) */
        if (bsc == 0 || (ssc != 4 && ssc != 0) || chm > 10 || bs > 65536 || made + bs > n) {
            ok = 0;
            break;
        }
        int32_t *x0 = a, *x1 = a + 65536;
        ok = subframe(&b, x0, bs, chm == 9 ? 17 : 16) && subframe(&b, x1, bs, chm == 8 || chm == 10 ? 17 : 16);
        if (!ok)
            break;
        b.at = (b.at + 7) & ~(size_t)7;
        size_t body = b.at >> 3;
        uint16_t crc = (uint16_t)rb(&b, 16);
        if (b.bad || crc != crc16(in + start, body - start)) {
            ok = 0;
            break;
        }
        for (int i = 0; i < bs; i++) {
            int32_t l = x0[i], r = x1[i];
            if (chm == 8) /* left / side */
                r = l - r;
            else if (chm == 9) /* side / right */
                l = x0[i] + r;
            else if (chm == 10) { /* mid / side */
                int32_t mid = (int32_t)((uint32_t)x0[i] << 1) | (x1[i] & 1);
                l = (mid + x1[i]) >> 1, r = (mid - x1[i]) >> 1;
            }
            pcm[2 * (made + i)] = (int16_t)l, pcm[2 * (made + i) + 1] = (int16_t)r;
        }
        made += bs;
    }
    free(a);
    return ok && made == n ? b.at >> 3 : 0;
}
