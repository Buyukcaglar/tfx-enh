#include "host/deflate.h"

#include <stdlib.h>
#include <string.h>

/* --- the format's tables (RFC 1951 3.2.5) ------------------------------------ */

static const uint16_t LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,    65,    97,    129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
/* the code length code's lengths are sent in this order */
static const uint8_t CL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

uint32_t tfx_crc32(uint32_t crc, const void *data, size_t n) {
    const uint8_t *p = data;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = crc >> 1 ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

/* --- the compressor ------------------------------------------------------------ */

#define WSIZE 32768
#define WMASK (WSIZE - 1)
#define HBITS 15
#define BLOCK 16384 /* symbols a block */
#define MAX_CHAIN 48
#define NICE 128

struct tfx_deflater {
    int32_t head[1 << HBITS];
    int32_t prev[WSIZE];
    uint16_t sym_len[BLOCK]; /* a literal's byte (dist 0), else a match's length */
    uint16_t sym_dist[BLOCK];
    uint8_t len_code[259];  /* a match length's code - 257 */
    uint8_t dist_code[512]; /* by dist - 1 below 256, else 256 + (dist - 1) / 128 */
};

tfx_deflater *tfx_deflater_new(void) {
    tfx_deflater *d = malloc(sizeof *d);
    if (!d)
        return NULL;
    for (int c = 0; c < 29; c++)
        for (int l = LEN_BASE[c]; l < LEN_BASE[c] + (1 << LEN_EXTRA[c]) && l <= 258; l++)
            d->len_code[l] = (uint8_t)c;
    d->len_code[258] = 28;
    for (int c = 0; c < 30; c++)
        for (int k = DIST_BASE[c] - 1; k < DIST_BASE[c] - 1 + (1 << DIST_EXTRA[c]); k++)
            d->dist_code[k < 256 ? k : 256 + (k >> 7)] = (uint8_t)c;
    return d;
}

void tfx_deflater_free(tfx_deflater *d) { free(d); }

typedef struct bits_out {
    uint8_t *out;
    size_t cap, n;
    uint64_t bits;
    int count;
} bits_out;

static void put(bits_out *w, uint32_t v, int k) {
    w->bits |= (uint64_t)v << w->count;
    w->count += k;
    while (w->count >= 8) {
        if (w->n < w->cap)
            w->out[w->n] = (uint8_t)w->bits;
        w->n++;
        w->bits >>= 8;
        w->count -= 8;
    }
}

static void align(bits_out *w) {
    if (w->count)
        put(w, 0, 8 - w->count);
}

/* code lengths for n symbols by their counts, none past limit (a tree of
 * two codes at least, as every decoder takes) */
static void build_lengths(const uint32_t *freq, int n, int limit, uint8_t *len) {
    int sym[288], m = 0;
    memset(len, 0, (size_t)n);
    for (int i = 0; i < n; i++)
        if (freq[i])
            sym[m++] = i;
    if (m < 2) {
        int a = m ? sym[0] : 0;
        len[a] = 1, len[a ? 0 : 1] = 1;
        return;
    }
    for (int i = 1; i < m; i++) /* by count, the least first */
        for (int j = i; j > 0 && freq[sym[j]] < freq[sym[j - 1]]; j--) {
            int t = sym[j];
            sym[j] = sym[j - 1], sym[j - 1] = t;
        }
    uint32_t w[2 * 288];
    int parent[2 * 288], depth[2 * 288];
    for (int k = 0; k < m; k++)
        w[k] = freq[sym[k]];
    int leaf = 0, node = m;
    for (int next = m; next < 2 * m - 1; next++) { /* the two queues: leaves, then the nodes made */
        int a = leaf < m && (node >= next || w[leaf] <= w[node]) ? leaf++ : node++;
        int b = leaf < m && (node >= next || w[leaf] <= w[node]) ? leaf++ : node++;
        w[next] = w[a] + w[b];
        parent[a] = parent[b] = next;
    }
    depth[2 * m - 2] = 0;
    for (int k = 2 * m - 3; k >= 0; k--)
        depth[k] = depth[parent[k]] + 1;
    int count[16] = {0};
    for (int k = 0; k < m; k++)
        count[depth[k] < limit ? depth[k] : limit]++;
    uint32_t total = 0;
    for (int i = 1; i <= limit; i++)
        total += (uint32_t)count[i] << (limit - i);
    while (total > (1u << limit)) { /* (the longest codes made to fit: one moved down a level) */
        count[limit]--;
        for (int i = limit - 1; i > 0; i--)
            if (count[i]) {
                count[i]--, count[i + 1] += 2;
                break;
            }
        total--;
    }
    int k = 0;
    for (int l = limit; l >= 1; l--)
        for (int j = 0; j < count[l]; j++)
            len[sym[k++]] = (uint8_t)l;
}

/* the canonical codes, bit-reversed as the stream sends them */
static void make_codes(const uint8_t *len, int n, uint16_t *code) {
    int count[16] = {0}, next[16];
    for (int i = 0; i < n; i++)
        count[len[i]]++;
    count[0] = 0;
    for (int b = 1, c = 0; b < 16; b++)
        c = (c + count[b - 1]) << 1, next[b] = c;
    for (int i = 0; i < n; i++)
        if (len[i]) {
            unsigned c = (unsigned)next[len[i]]++, r = 0;
            for (int b = 0; b < len[i]; b++)
                r = r << 1 | (c >> b & 1);
            code[i] = (uint16_t)r;
        }
}

static void put_symbols(tfx_deflater *d, bits_out *w, int nsym, const uint8_t *ll, const uint16_t *lc,
                        const uint8_t *dl, const uint16_t *dc) {
    for (int i = 0; i < nsym; i++) {
        int dist = d->sym_dist[i], l = d->sym_len[i];
        if (!dist) {
            put(w, lc[l], ll[l]);
            continue;
        }
        int c = d->len_code[l];
        put(w, lc[257 + c], ll[257 + c]);
        put(w, (uint32_t)(l - LEN_BASE[c]), LEN_EXTRA[c]);
        int k = dist - 1, e = d->dist_code[k < 256 ? k : 256 + (k >> 7)];
        put(w, dc[e], dl[e]);
        put(w, (uint32_t)(dist - DIST_BASE[e]), DIST_EXTRA[e]);
    }
    put(w, lc[256], ll[256]);
}

/* one block: the bytes in[start, end), as nsym symbols; the cheapest of
 * dynamic codes, the fixed codes and stored */
static void put_block(tfx_deflater *d, bits_out *w, const uint8_t *in, size_t start, size_t end, int nsym, int final) {
    uint32_t lf[286] = {0}, df[30] = {0};
    uint64_t extra = 0;
    for (int i = 0; i < nsym; i++) {
        if (!d->sym_dist[i]) {
            lf[d->sym_len[i]]++;
            continue;
        }
        int c = d->len_code[d->sym_len[i]], k = d->sym_dist[i] - 1, e = d->dist_code[k < 256 ? k : 256 + (k >> 7)];
        lf[257 + c]++, df[e]++;
        extra += LEN_EXTRA[c] + DIST_EXTRA[e];
    }
    lf[256] = 1;
    uint8_t ll[286], dl[30], all[316], cl[19];
    uint16_t lc[288], dc[32], cc[19];
    build_lengths(lf, 286, 15, ll);
    build_lengths(df, 30, 15, dl);
    int hlit = 286, hdist = 30;
    while (hlit > 257 && !ll[hlit - 1])
        hlit--;
    while (hdist > 1 && !dl[hdist - 1])
        hdist--;
    memcpy(all, ll, (size_t)hlit);
    memcpy(all + hlit, dl, (size_t)hdist);
    /* the lengths run-length coded: (symbol, its extra bits' value) */
    uint8_t rs[316], rx[316];
    int nr = 0, total = hlit + hdist;
    for (int i = 0; i < total;) {
        int l = all[i], run = 1;
        while (i + run < total && all[i + run] == l)
            run++;
        if (l == 0 && run >= 3) {
            int r = run < 138 ? run : 138;
            rs[nr] = r >= 11 ? 18 : 17, rx[nr++] = (uint8_t)(r >= 11 ? r - 11 : r - 3);
            i += r;
        } else if (l != 0 && run >= 4) {
            int r = run - 1 < 6 ? run - 1 : 6;
            rs[nr] = (uint8_t)l, rx[nr++] = 0;
            rs[nr] = 16, rx[nr++] = (uint8_t)(r - 3);
            i += 1 + r;
        } else
            rs[nr] = (uint8_t)l, rx[nr++] = 0, i++;
    }
    uint32_t cf[19] = {0};
    for (int i = 0; i < nr; i++)
        cf[rs[i]]++;
    build_lengths(cf, 19, 7, cl);
    int hclen = 19;
    while (hclen > 4 && !cl[CL_ORDER[hclen - 1]])
        hclen--;
    /* the three costs, in bits */
    uint64_t dyn = 3 + 14 + 3 * (uint64_t)hclen + extra, fix = 3 + extra;
    for (int i = 0; i < nr; i++)
        dyn += cl[rs[i]] + (rs[i] == 16 ? 2 : rs[i] == 17 ? 3 : rs[i] == 18 ? 7 : 0);
    for (int i = 0; i < 286; i++)
        dyn += (uint64_t)lf[i] * ll[i], fix += (uint64_t)lf[i] * (i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
    for (int i = 0; i < 30; i++)
        dyn += (uint64_t)df[i] * dl[i], fix += (uint64_t)df[i] * 5;
    size_t bytes = end - start;
    uint64_t stored = 8 * ((bytes + 65534) / 65535 * 5 + bytes) + 8;
    if (!bytes)
        stored = 8 * 5 + 8;
    if (stored <= dyn && stored <= fix) {
        size_t at = start;
        do {
            size_t k = end - at < 65535 ? end - at : 65535;
            put(w, final && at + k == end, 1), put(w, 0, 2);
            align(w);
            put(w, (uint32_t)k, 16), put(w, (uint32_t)k ^ 0xffff, 16);
            for (size_t j = 0; j < k; j++)
                put(w, in[at + j], 8);
            at += k;
        } while (at < end);
    } else if (fix <= dyn) {
        uint8_t fl[288], fd[32];
        for (int i = 0; i < 288; i++)
            fl[i] = (uint8_t)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        memset(fd, 5, sizeof fd);
        make_codes(fl, 288, lc);
        make_codes(fd, 32, dc);
        put(w, (uint32_t)final, 1), put(w, 1, 2);
        put_symbols(d, w, nsym, fl, lc, fd, dc);
    } else {
        make_codes(ll, 286, lc);
        make_codes(dl, 30, dc);
        make_codes(cl, 19, cc);
        put(w, (uint32_t)final, 1), put(w, 2, 2);
        put(w, (uint32_t)(hlit - 257), 5), put(w, (uint32_t)(hdist - 1), 5), put(w, (uint32_t)(hclen - 4), 4);
        for (int i = 0; i < hclen; i++)
            put(w, cl[CL_ORDER[i]], 3);
        for (int i = 0; i < nr; i++) {
            put(w, cc[rs[i]], cl[rs[i]]);
            if (rs[i] >= 16)
                put(w, rx[i], rs[i] == 16 ? 2 : rs[i] == 17 ? 3 : 7);
        }
        put_symbols(d, w, nsym, ll, lc, dl, dc);
    }
}

static uint32_t hash3(const uint8_t *p) {
    return ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u >> (32 - HBITS);
}

/* the longest match for p among the earlier positions (0 none), then p
 * entered in the chains */
static int longest(tfx_deflater *d, const uint8_t *in, size_t n, size_t p, int *dist) {
    uint32_t h = hash3(in + p);
    int best = 0;
    size_t maxlen = n - p < 258 ? n - p : 258;
    int32_t cur = d->head[h];
    for (int chain = MAX_CHAIN; cur >= 0 && p - (size_t)cur <= WSIZE && chain > 0; chain--) {
        const uint8_t *a = in + cur, *b = in + p;
        if (a[best] == b[best] && a[0] == b[0]) {
            size_t l = 0;
            while (l < maxlen && a[l] == b[l])
                l++;
            if ((int)l > best) {
                best = (int)l, *dist = (int)(p - (size_t)cur);
                if (l >= NICE || l == maxlen)
                    break;
            }
        }
        int32_t next = d->prev[cur & WMASK];
        if (next >= cur)
            break;
        cur = next;
    }
    d->prev[p & WMASK] = d->head[h];
    d->head[h] = (int32_t)p;
    return best >= 3 ? best : 0;
}

static void enter(tfx_deflater *d, const uint8_t *in, size_t n, size_t p) {
    if (n - p < 3)
        return;
    uint32_t h = hash3(in + p);
    d->prev[p & WMASK] = d->head[h];
    d->head[h] = (int32_t)p;
}

size_t tfx_deflate(tfx_deflater *d, const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    bits_out w = {out, cap, 0, 0, 0};
    memset(d->head, 0xff, sizeof d->head);
    size_t i = 0, block_start = 0;
    int nsym = 0;
    while (i < n) {
        int len = 0, dist = 0;
        if (n - i >= 3)
            len = longest(d, in, n, i, &dist);
        if (len && len < NICE && n - i - 1 >= 3) { /* one step lazy: a longer match from the next byte wins */
            int dist2 = 0, len2 = longest(d, in, n, i + 1, &dist2);
            size_t from = i + 2; /* (i and i + 1 entered) */
            if (len2 > len) {
                d->sym_len[nsym] = in[i], d->sym_dist[nsym++] = 0;
                i++, len = len2, dist = dist2;
            }
            d->sym_len[nsym] = (uint16_t)len, d->sym_dist[nsym++] = (uint16_t)dist;
            for (size_t k = from; k < i + (size_t)len; k++)
                enter(d, in, n, k);
            i += (size_t)len;
        } else if (len) {
            d->sym_len[nsym] = (uint16_t)len, d->sym_dist[nsym++] = (uint16_t)dist;
            for (size_t k = i + 1; k < i + (size_t)len; k++)
                enter(d, in, n, k);
            i += (size_t)len;
        } else {
            d->sym_len[nsym] = in[i], d->sym_dist[nsym++] = 0;
            i++;
        }
        if (nsym >= BLOCK - 1 && i < n) {
            put_block(d, &w, in, block_start, i, nsym, 0);
            block_start = i, nsym = 0;
        }
    }
    put_block(d, &w, in, block_start, n, nsym, 1);
    align(&w);
    return w.n <= cap ? w.n : 0;
}

/* --- the decompressor ---------------------------------------------------------- */

#define FAST 10

typedef struct huff {
    uint16_t count[16];  /* codes of each length */
    uint16_t symbol[288]; /* the symbols by code */
    uint16_t fast[1 << FAST]; /* the next FAST bits (reversed): symbol << 4 | length, 0 not there */
} huff;

typedef struct bits_in {
    const uint8_t *in;
    size_t n, pos;
    uint64_t bits;
    int count;
} bits_in;

static void need(bits_in *r, int k) {
    while (r->count < k) {
        r->bits |= (uint64_t)(r->pos < r->n ? r->in[r->pos] : 0) << r->count;
        r->pos++, r->count += 8;
    }
}

static uint32_t take(bits_in *r, int k) {
    if (!k)
        return 0;
    need(r, k);
    uint32_t v = (uint32_t)(r->bits & ((1u << k) - 1));
    r->bits >>= k, r->count -= k;
    return v;
}

static int overrun(const bits_in *r) { return r->pos > r->n && (r->pos - r->n) * 8 > (size_t)r->count; }

/* 0 on a set of lengths no code can have (oversubscribed); an incomplete
 * set is allowed, as zlib allows (a one-code distance tree) */
static int huff_build(huff *h, const uint8_t *len, int n) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++)
        h->count[len[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left = (left << 1) - h->count[l];
        if (left < 0)
            return 0;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++)
        offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++)
        if (len[i])
            h->symbol[offs[len[i]]++] = (uint16_t)i;
    memset(h->fast, 0, sizeof h->fast);
    int code = 0, k = 0;
    for (int l = 1; l <= FAST; l++) {
        for (int j = 0; j < h->count[l]; j++, code++, k++) {
            unsigned r = 0;
            for (int b = 0; b < l; b++)
                r = r << 1 | ((unsigned)code >> b & 1);
            for (unsigned f = r; f < (1u << FAST); f += 1u << l)
                h->fast[f] = (uint16_t)(h->symbol[k] << 4 | l);
        }
        code <<= 1;
    }
    return 1;
}

static int decode(bits_in *r, const huff *h) {
    need(r, FAST);
    uint16_t f = h->fast[r->bits & ((1u << FAST) - 1)];
    if (f) {
        r->bits >>= f & 15, r->count -= f & 15;
        return f >> 4;
    }
    int code = 0, first = 0, index = 0; /* (the slow way: a bit at a time) */
    for (int l = 1; l < 16; l++) {
        code |= (int)take(r, 1);
        int c = h->count[l];
        if (code - c < first)
            return h->symbol[index + (code - first)];
        index += c, first += c, first <<= 1, code <<= 1;
    }
    return -1;
}

int tfx_inflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *got) {
    bits_in r = {in, n, 0, 0, 0};
    size_t o = 0;
    int final;
    huff *lh = malloc(sizeof *lh), *dh = malloc(sizeof *dh);
    int ok = lh && dh;
    do {
        if (!ok)
            break;
        final = (int)take(&r, 1);
        int type = (int)take(&r, 2);
        if (type == 0) {
            r.bits >>= r.count & 7, r.count -= r.count & 7;
            uint32_t len = take(&r, 16), nlen = take(&r, 16);
            if ((len ^ 0xffff) != nlen || o + len > cap) {
                ok = 0;
                break;
            }
            for (uint32_t i = 0; i < len && ok; i++) /* (the bytes the bit buffer holds, then the stream's) */
                out[o++] = (uint8_t)take(&r, 8);
            ok = ok && !overrun(&r);
            continue;
        }
        if (type == 3) {
            ok = 0;
            break;
        }
        uint8_t len[320];
        int hlit = 288, hdist = 32;
        if (type == 1) {
            for (int i = 0; i < 288; i++)
                len[i] = (uint8_t)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
            memset(len + 288, 5, 32);
        } else {
            hlit = (int)take(&r, 5) + 257, hdist = (int)take(&r, 5) + 1;
            int hclen = (int)take(&r, 4) + 4;
            uint8_t cl[19] = {0};
            for (int i = 0; i < hclen; i++)
                cl[CL_ORDER[i]] = (uint8_t)take(&r, 3);
            huff ch;
            if (hlit > 286 || hdist > 30 || !huff_build(&ch, cl, 19)) {
                ok = 0;
                break;
            }
            for (int i = 0; i < hlit + hdist;) {
                int s = decode(&r, &ch), rep = 0, v = 0;
                if (s < 0) {
                    ok = 0;
                    break;
                }
                if (s < 16)
                    len[i++] = (uint8_t)s;
                else {
                    if (s == 16) {
                        if (!i) {
                            ok = 0;
                            break;
                        }
                        v = len[i - 1], rep = 3 + (int)take(&r, 2);
                    } else
                        rep = s == 17 ? 3 + (int)take(&r, 3) : 11 + (int)take(&r, 7);
                    if (i + rep > hlit + hdist) {
                        ok = 0;
                        break;
                    }
                    while (rep--)
                        len[i++] = (uint8_t)v;
                }
            }
            if (!ok || !len[256])
                break;
        }
        if (!huff_build(lh, len, hlit) || !huff_build(dh, len + hlit, hdist)) {
            ok = 0;
            break;
        }
        for (;;) {
            int s = decode(&r, lh);
            if (s < 0 || overrun(&r)) {
                ok = 0;
                break;
            }
            if (s < 256) {
                if (o >= cap) {
                    ok = 0;
                    break;
                }
                out[o++] = (uint8_t)s;
                continue;
            }
            if (s == 256)
                break;
            s -= 257;
            if (s >= 29) {
                ok = 0;
                break;
            }
            size_t l = LEN_BASE[s] + take(&r, LEN_EXTRA[s]);
            int e = decode(&r, dh);
            if (e < 0 || e >= 30) {
                ok = 0;
                break;
            }
            size_t dist = DIST_BASE[e] + take(&r, DIST_EXTRA[e]);
            if (dist > o || o + l > cap) {
                ok = 0;
                break;
            }
            for (size_t k = 0; k < l; k++, o++)
                out[o] = out[o - dist];
        }
    } while (ok && !final);
    ok = ok && !overrun(&r);
    free(lh);
    free(dh);
    if (ok && got)
        *got = o;
    return ok;
}
