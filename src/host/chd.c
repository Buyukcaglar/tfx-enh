#include "host/chd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/flac.h"
#include "host/sha1.h"

#define SECTOR 2352
#define SUBCODE 96
#define FRAMES 8 /* a hunk's */
#define CODEC_CDZL 0x63647a6cu /* "cdzl" */
#define CODEC_CDFL 0x6364666cu /* "cdfl" */
#define NONE 4
#define SELF 5
#define MDFLAG_CHECKSUM 1

static void put_be(uint8_t *p, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; i--, v >>= 8)
        p[i] = (uint8_t)v;
}

static uint64_t get_be(const uint8_t *p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v = v << 8 | p[i];
    return v;
}

/* CRC-16-CCITT (0x1021, from 0xffff), as the format's */
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int k = 0; k < 8; k++)
            crc = (uint16_t)(crc & 0x8000 ? crc << 1 ^ 0x1021 : crc << 1);
    }
    return crc;
}

/* --- the layout ------------------------------------------------------------------ */

void tfx_chd_layout_cd(tfx_chd_layout *l, const tfx_cd_image *img) {
    memset(l, 0, sizeof *l);
    l->n_tracks = img->n_tracks;
    uint32_t at = 0;
    for (int i = 0; i < img->n_tracks; i++) {
        int32_t end = i + 1 < img->n_tracks ? img->track[i + 1].lba : img->end_lba;
        l->track[i].number = img->track[i].number;
        l->track[i].audio = img->track[i].audio;
        l->track[i].lba = img->track[i].lba;
        l->track[i].frames = end > img->track[i].lba ? end - img->track[i].lba : 0;
        l->track[i].chd_frame = at;
        at += ((uint32_t)l->track[i].frames + 3) & ~3u;
    }
    l->frames = at;
    l->hunks = (at + FRAMES - 1) / FRAMES;
}

/* the track holding CHD frame f, and the disc's sector (-1 padding) */
static int32_t sector_of(const tfx_chd_layout *l, uint32_t f, int *track) {
    for (int i = l->n_tracks - 1; i >= 0; i--)
        if (f >= l->track[i].chd_frame) {
            *track = i;
            uint32_t k = f - l->track[i].chd_frame;
            return k < (uint32_t)l->track[i].frames ? l->track[i].lba + (int32_t)k : -1;
        }
    return -1;
}

int tfx_chd_raw_hunk(const tfx_chd_layout *l, const tfx_cd_image *img, uint32_t h, uint8_t *raw,
                     size_t (*read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n), void *ctx) {
    memset(raw, 0, TFX_CHD_HUNK);
    int kind = 1;
    for (uint32_t k = 0; k < FRAMES; k++) {
        int t = 0;
        uint32_t f = h * FRAMES + k;
        int32_t s = f < l->frames ? sector_of(l, f, &t) : -1;
        if (s < 0)
            continue;
        int file = 0;
        while (file + 1 < img->n_files && img->file_lba[file + 1] <= s)
            file++;
        uint8_t *p = raw + k * TFX_CHD_FRAME;
        if (read(ctx, file, (uint64_t)(s - img->file_lba[file]) * SECTOR, p, SECTOR) != SECTOR)
            return 0;
        if (l->track[t].audio) { /* (the format keeps the samples big-endian) */
            kind = 2;
            for (int i = 0; i < SECTOR; i += 2) {
                uint8_t b = p[i];
                p[i] = p[i + 1], p[i + 1] = b;
            }
        }
    }
    return kind;
}

/* --- the codec ------------------------------------------------------------------- */

/* cdzl: the ECC bitmap (a byte: no frame's ECC left out), the sectors'
 * stream's length (2 bytes), the sectors deflated, the subcode deflated */
static size_t cdzl(tfx_deflater *d, const uint8_t *sectors, const uint8_t *sub, uint8_t *out) {
    size_t head = 1 + 2, a = tfx_deflate(d, sectors, FRAMES * SECTOR, out + head, TFX_CHD_HUNK - head);
    size_t b = a ? tfx_deflate(d, sub, FRAMES * SUBCODE, out + head + a, TFX_CHD_HUNK - head - a) : 0;
    if (!a || !b || head + a + b >= TFX_CHD_HUNK)
        return 0;
    out[0] = 0;
    put_be(out + 1, a, 2);
    return head + a + b;
}

/* cdfl: the sectors as 16-bit stereo samples (big-endian, as kept) in
 * FLAC frames of 2352 samples, then the subcode deflated */
static size_t cdfl(tfx_deflater *d, const uint8_t *sectors, const uint8_t *sub, uint8_t *out) {
    int16_t pcm[FRAMES * SECTOR / 2];
    for (int i = 0; i < FRAMES * SECTOR / 2; i++)
        pcm[i] = (int16_t)(sectors[2 * i] << 8 | sectors[2 * i + 1]);
    size_t a = tfx_flac_encode(pcm, FRAMES * SECTOR / 4, SECTOR, out, TFX_CHD_HUNK);
    size_t b = a ? tfx_deflate(d, sub, FRAMES * SUBCODE, out + a, TFX_CHD_HUNK - a) : 0;
    return a && b && a + b < TFX_CHD_HUNK ? a + b : 0;
}

size_t tfx_chd_compress(tfx_deflater *d, const uint8_t *raw, int kind, uint8_t *out, int *codec) {
    uint8_t sectors[FRAMES * SECTOR], sub[FRAMES * SUBCODE], other[TFX_CHD_HUNK];
    for (int k = 0; k < FRAMES; k++) {
        memcpy(sectors + k * SECTOR, raw + k * TFX_CHD_FRAME, SECTOR);
        memcpy(sub + k * SUBCODE, raw + k * TFX_CHD_FRAME + SECTOR, SUBCODE);
    }
    size_t n = kind == 2 ? cdfl(d, sectors, sub, out) : 0;
    *codec = 1;
    if (kind != 2 || !n || n > TFX_CHD_HUNK * 9 / 10) { /* (data, or audio FLAC does little for: deflate too) */
        size_t z = cdzl(d, sectors, sub, other);
        if (z && (!n || z < n))
            memcpy(out, other, z), n = z, *codec = 0;
    }
    if (!n) {
        memcpy(out, raw, TFX_CHD_HUNK);
        *codec = NONE;
        return TFX_CHD_HUNK;
    }
    return n;
}

static int expand(const uint8_t *src, size_t n, int codec, uint8_t *raw) {
    uint8_t sectors[FRAMES * SECTOR], sub[FRAMES * SUBCODE];
    size_t got = 0, at;
    if (codec == 0) {
        size_t head = 1 + 2;
        if (n < head)
            return 0;
        size_t a = (size_t)get_be(src + 1, 2);
        if (src[0] || head + a > n || !tfx_inflate(src + head, a, sectors, sizeof sectors, &got) ||
            got != sizeof sectors)
            return 0; /* (a frame with its ECC left out: chdman's, not made here) */
        at = head + a;
    } else {
        int16_t pcm[FRAMES * SECTOR / 2];
        if (!(at = tfx_flac_decode(src, n, pcm, FRAMES * SECTOR / 4)))
            return 0;
        for (int i = 0; i < FRAMES * SECTOR / 2; i++)
            sectors[2 * i] = (uint8_t)((uint16_t)pcm[i] >> 8), sectors[2 * i + 1] = (uint8_t)pcm[i];
    }
    if (!tfx_inflate(src + at, n - at, sub, sizeof sub, &got) || got != sizeof sub)
        return 0;
    for (int k = 0; k < FRAMES; k++) {
        memcpy(raw + k * TFX_CHD_FRAME, sectors + k * SECTOR, SECTOR);
        memcpy(raw + k * TFX_CHD_FRAME + SECTOR, sub + k * SUBCODE, SUBCODE);
    }
    return 1;
}

/* --- the writer ------------------------------------------------------------------ */

struct tfx_chd_writer {
    tfx_chd_layout layout;
    uint8_t *map; /* the raw map: 12 bytes a hunk */
    uint32_t at;  /* the next hunk */
    uint64_t offset; /* the next hunk's place in the file */
    uint64_t logical_left;
    tfx_sha1 raw_sha1;
    uint8_t *tail;
    size_t tail_n;
};

tfx_chd_writer *tfx_chd_writer_new(const tfx_chd_layout *l) {
    tfx_chd_writer *w = calloc(1, sizeof *w);
    if (!w || !(w->map = calloc(l->hunks ? l->hunks : 1, 12))) {
        free(w);
        return NULL;
    }
    w->layout = *l;
    w->offset = TFX_CHD_HEADER;
    w->logical_left = (uint64_t)l->frames * TFX_CHD_FRAME;
    tfx_sha1_init(&w->raw_sha1);
    return w;
}

void tfx_chd_writer_free(tfx_chd_writer *w) {
    if (!w)
        return;
    free(w->map);
    free(w->tail);
    free(w);
}

void tfx_chd_put(tfx_chd_writer *w, const uint8_t *raw, size_t n, int codec) {
    if (w->at >= w->layout.hunks)
        return;
    uint8_t *m = w->map + 12 * w->at++;
    m[0] = (uint8_t)codec;
    put_be(m + 1, n, 3);
    put_be(m + 4, w->offset, 6);
    put_be(m + 10, crc16(raw, TFX_CHD_HUNK), 2);
    w->offset += n;
    size_t k = w->logical_left < TFX_CHD_HUNK ? (size_t)w->logical_left : TFX_CHD_HUNK;
    tfx_sha1_add(&w->raw_sha1, raw, k);
    w->logical_left -= k;
}

typedef struct bits_out {
    uint8_t *p;
    size_t n;
    uint32_t acc;
    int count;
} bits_out;

/* MSB first, as the format's bit streams */
static void bits(bits_out *b, uint32_t v, int k) {
    for (int i = k - 1; i >= 0; i--) {
        b->acc = b->acc << 1 | (v >> i & 1);
        if (++b->count == 8)
            b->p[b->n++] = (uint8_t)b->acc, b->acc = 0, b->count = 0;
    }
}

static int bits_for(uint64_t v) {
    int n = 0;
    while (v)
        v >>= 1, n++;
    return n;
}

typedef struct md_hash {
    uint8_t tag[4], sha1[20];
} md_hash;

static int by_bytes(const void *a, const void *b) { return memcmp(a, b, sizeof(md_hash)); }

int tfx_chd_finish(tfx_chd_writer *w, const uint8_t **tail, size_t *n, uint8_t header[TFX_CHD_HEADER]) {
    const tfx_chd_layout *l = &w->layout;
    uint32_t hunks = l->hunks, max_len = 0;
    for (uint32_t h = 0; h < hunks; h++)
        if (w->map[12 * h] < NONE && get_be(w->map + 12 * h + 1, 3) > max_len)
            max_len = (uint32_t)get_be(w->map + 12 * h + 1, 3);
    /* the map: the codecs by a Huffman code (here every one 4 bits long),
     * then each hunk's length and CRC */
    size_t cap = 16 + 8 + (size_t)hunks * 6 + 16;
    char line[TFX_CD_TRACKS][160];
    size_t md_size = 0;
    for (int i = 0; i < l->n_tracks; i++) {
        snprintf(line[i], sizeof line[i],
                 "TRACK:%d TYPE:%s SUBTYPE:NONE FRAMES:%d PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", i + 1,
                 l->track[i].audio ? "AUDIO" : "MODE1_RAW", (int)l->track[i].frames);
        md_size += 16 + strlen(line[i]) + 1;
    }
    free(w->tail);
    if (!(w->tail = calloc(cap + md_size, 1)))
        return 0;
    int length_bits = bits_for(max_len);
    bits_out b = {w->tail + 16, 0, 0, 0};
    for (int i = 0; i < 16; i++)
        bits(&b, 4, 4);
    for (uint32_t h = 0; h < hunks; h++)
        bits(&b, w->map[12 * h], 4);
    for (uint32_t h = 0; h < hunks; h++) {
        const uint8_t *m = w->map + 12 * h;
        if (m[0] < NONE)
            bits(&b, (uint32_t)get_be(m + 1, 3), length_bits);
        bits(&b, (uint32_t)get_be(m + 10, 2), 16);
    }
    if (b.count)
        bits(&b, 0, 8 - b.count);
    uint8_t *mh = w->tail;
    put_be(mh, b.n, 4);
    put_be(mh + 4, TFX_CHD_HEADER, 6);
    put_be(mh + 10, crc16(w->map, (size_t)hunks * 12), 2);
    mh[12] = (uint8_t)length_bits, mh[13] = 0, mh[14] = 0, mh[15] = 0;
    uint64_t map_offset = w->offset, md_offset = map_offset + 16 + b.n;
    /* the metadata: a CHT2 line a track (with its NUL), chained */
    size_t at = 16 + b.n;
    md_hash hash[TFX_CD_TRACKS];
    for (int i = 0; i < l->n_tracks; i++) {
        size_t len = strlen(line[i]) + 1;
        uint8_t *e = w->tail + at;
        memcpy(e, "CHT2", 4);
        e[4] = MDFLAG_CHECKSUM;
        put_be(e + 5, len, 3);
        put_be(e + 8, i + 1 < l->n_tracks ? map_offset + at + 16 + len : 0, 8);
        memcpy(e + 16, line[i], len);
        memcpy(hash[i].tag, "CHT2", 4);
        tfx_sha1 s;
        tfx_sha1_init(&s);
        tfx_sha1_add(&s, line[i], len);
        tfx_sha1_done(&s, hash[i].sha1);
        at += 16 + len;
    }
    w->tail_n = at;
    /* the header: the data's SHA-1, and the whole's (the data's and the
     * metadata's, sorted) */
    uint8_t raw_sha1[20], sha1[20];
    tfx_sha1_done(&w->raw_sha1, raw_sha1);
    qsort(hash, (size_t)l->n_tracks, sizeof *hash, by_bytes);
    tfx_sha1 s;
    tfx_sha1_init(&s);
    tfx_sha1_add(&s, raw_sha1, 20);
    tfx_sha1_add(&s, hash, sizeof *hash * (size_t)l->n_tracks);
    tfx_sha1_done(&s, sha1);
    memset(header, 0, TFX_CHD_HEADER);
    memcpy(header, "MComprHD", 8);
    put_be(header + 8, TFX_CHD_HEADER, 4);
    put_be(header + 12, 5, 4);
    put_be(header + 16, CODEC_CDZL, 4);
    put_be(header + 20, CODEC_CDFL, 4);
    put_be(header + 32, (uint64_t)l->frames * TFX_CHD_FRAME, 8);
    put_be(header + 40, map_offset, 8);
    put_be(header + 48, l->n_tracks ? md_offset : 0, 8);
    put_be(header + 56, TFX_CHD_HUNK, 4);
    put_be(header + 60, TFX_CHD_FRAME, 4);
    memcpy(header + 64, raw_sha1, 20);
    memcpy(header + 84, sha1, 20);
    *tail = w->tail;
    *n = w->tail_n;
    return 1;
}

/* --- the reader ------------------------------------------------------------------ */

typedef struct bits_in {
    const uint8_t *p;
    size_t n, at; /* in bits */
} bits_in;

static uint32_t take(bits_in *b, int k) {
    uint32_t v = 0;
    for (int i = 0; i < k; i++, b->at++)
        v = v << 1 | (b->at / 8 < b->n ? b->p[b->at / 8] >> (7 - b->at % 8) & 1 : 0);
    return v;
}

/* the format's Huffman tree (lengths, run-length coded) and a symbol by it */
typedef struct tree {
    uint8_t len[16];
    uint32_t code[16];
} tree;

static int tree_read(bits_in *b, tree *t) {
    int n = 0;
    while (n < 16) {
        int v = (int)take(b, 4);
        if (v != 1) {
            t->len[n++] = (uint8_t)v;
            continue;
        }
        v = (int)take(b, 4);
        if (v == 1) {
            t->len[n++] = 1;
            continue;
        }
        int rep = (int)take(b, 4) + 3;
        while (rep-- && n < 16)
            t->len[n++] = (uint8_t)v;
    }
    uint32_t histo[33] = {0}, start = 0;
    for (int i = 0; i < 16; i++)
        if (t->len[i] > 8)
            return 0;
        else
            histo[t->len[i]]++;
    for (int l = 32; l > 0; l--) { /* (the canonical codes, longest first, as the format assigns them) */
        uint32_t next = (start + histo[l]) >> 1;
        if (l != 1 && next * 2 != start + histo[l])
            return 0;
        histo[l] = start, start = next;
    }
    for (int i = 0; i < 16; i++)
        if (t->len[i])
            t->code[i] = histo[t->len[i]]++;
    return 1;
}

static int tree_symbol(bits_in *b, const tree *t) {
    uint32_t code = 0;
    for (int l = 1; l <= 8; l++) {
        code = code << 1 | take(b, 1);
        for (int i = 0; i < 16; i++)
            if (t->len[i] == l && t->code[i] == code)
                return i;
    }
    return -1;
}

int tfx_chd_open(tfx_chd *c, int (*read)(void *ctx, uint64_t offset, void *buf, size_t n), void *ctx, char *err,
                 size_t cap) {
    memset(c, 0, sizeof *c);
    c->read = read, c->ctx = ctx, c->hunk_at = ~0u;
    uint8_t h[TFX_CHD_HEADER];
    if (!read(ctx, 0, h, sizeof h) || memcmp(h, "MComprHD", 8) || get_be(h + 12, 4) != 5) {
        snprintf(err, cap, "not a CHD file of version 5");
        return 0;
    }
    if (get_be(h + 16, 4) != CODEC_CDZL || (get_be(h + 20, 4) && get_be(h + 20, 4) != CODEC_CDFL) || get_be(h + 24, 4) || get_be(h + 56, 4) != TFX_CHD_HUNK ||
        get_be(h + 60, 4) != TFX_CHD_FRAME || memcmp(h + 104, (uint8_t[20]){0}, 20)) {
        snprintf(err, cap, "a CHD this program did not make (its codecs or its hunks)");
        return 0;
    }
    uint64_t logical = get_be(h + 32, 8), map_offset = get_be(h + 40, 8), md = get_be(h + 48, 8);
    c->hunks = (uint32_t)((logical + TFX_CHD_HUNK - 1) / TFX_CHD_HUNK);
    uint8_t mh[16];
    if (!read(ctx, map_offset, mh, 16)) {
        snprintf(err, cap, "the CHD's map cannot be read");
        return 0;
    }
    size_t map_bytes = (size_t)get_be(mh, 4);
    uint64_t at = get_be(mh + 4, 6);
    int length_bits = mh[12], self_bits = mh[13];
    uint8_t *packed = malloc(map_bytes + 1);
    c->map = calloc(c->hunks ? c->hunks : 1, 12);
    int ok = packed && c->map && read(ctx, map_offset + 16, packed, map_bytes) && !mh[14];
    bits_in b = {packed, map_bytes, 0};
    tree t;
    ok = ok && tree_read(&b, &t);
    uint8_t last = 0;
    int rep = 0;
    for (uint32_t i = 0; ok && i < c->hunks; i++) { /* the codecs, run-length coded */
        if (rep > 0) {
            c->map[12 * i] = last, rep--;
            continue;
        }
        int v = tree_symbol(&b, &t);
        if (v == 7) /* (a short run) */
            c->map[12 * i] = last, rep = 2 + tree_symbol(&b, &t);
        else if (v == 8) { /* (a long run) */
            int hi = tree_symbol(&b, &t);
            c->map[12 * i] = last, rep = 2 + 16 + (hi << 4) + tree_symbol(&b, &t);
        } else if (v == 0 || (v == 1 && get_be(h + 20, 4)) || v == NONE || v == SELF || v == 9 || v == 10)
            c->map[12 * i] = last = (uint8_t)v;
        else
            ok = 0; /* (other codecs, a parent: not made here) */
    }
    uint32_t last_self = 0;
    for (uint32_t i = 0; ok && i < c->hunks; i++) {
        uint8_t *m = c->map + 12 * i;
        uint64_t offset = at;
        uint32_t length = 0, crc = 0;
        switch (m[0]) {
        case 0:
        case 1:
            at += length = take(&b, length_bits);
            crc = take(&b, 16);
            break;
        case NONE:
            at += length = TFX_CHD_HUNK;
            crc = take(&b, 16);
            break;
        case SELF:
            offset = last_self = take(&b, self_bits);
            break;
        case 10: /* (the hunk after the last one referred to) */
            last_self++;
            /* fall through */
        case 9:
            m[0] = SELF, offset = last_self;
            break;
        }
        put_be(m + 1, length, 3);
        put_be(m + 4, offset, 6);
        put_be(m + 10, crc, 2);
    }
    ok = ok && b.at <= 8 * map_bytes && crc16(c->map, (size_t)c->hunks * 12) == get_be(mh + 10, 2);
    free(packed);
    if (!ok) {
        snprintf(err, cap, "the CHD's map is damaged or uses what this program does not make");
        tfx_chd_close(c);
        return 0;
    }
    /* the tracks, from the metadata */
    tfx_chd_layout *l = &c->layout;
    uint32_t frame = 0;
    int32_t lba = 0;
    for (int guard = 0; md && guard < 1000; guard++) {
        uint8_t e[16];
        char text[256];
        if (!read(ctx, md, e, 16)) {
            ok = 0;
            break;
        }
        size_t len = (size_t)get_be(e + 5, 3);
        uint64_t next = get_be(e + 8, 8);
        if ((!memcmp(e, "CHT2", 4) || !memcmp(e, "CHTR", 4)) && len < sizeof text && l->n_tracks < TFX_CD_TRACKS) {
            if (!read(ctx, md + 16, text, len)) {
                ok = 0;
                break;
            }
            text[len] = 0;
            int number = 0, frames = 0, pregap = 0;
            char type[32] = "";
            if (sscanf(text, "TRACK:%d TYPE:%31s SUBTYPE:%*s FRAMES:%d PREGAP:%d", &number, type, &frames, &pregap) < 3 ||
                pregap) { /* (a pregap kept in the image: not made here) */
                ok = 0;
                break;
            }
            int i = l->n_tracks++;
            l->track[i].number = number, l->track[i].audio = !strcmp(type, "AUDIO");
            l->track[i].lba = lba, l->track[i].frames = frames, l->track[i].chd_frame = frame;
            lba += frames, frame += ((uint32_t)frames + 3) & ~3u;
        }
        md = next;
    }
    l->frames = frame, l->hunks = c->hunks;
    c->end_lba = lba;
    if (!ok || !l->n_tracks || (uint64_t)frame * TFX_CHD_FRAME != logical) {
        snprintf(err, cap, "the CHD's track list is missing or does not fit its data");
        tfx_chd_close(c);
        return 0;
    }
    if (!(c->packed = malloc(TFX_CHD_HUNK)) || !(c->hunk = malloc(TFX_CHD_HUNK))) {
        snprintf(err, cap, "out of memory");
        tfx_chd_close(c);
        return 0;
    }
    return 1;
}

void tfx_chd_close(tfx_chd *c) {
    free(c->map);
    free(c->packed);
    free(c->hunk);
    memset(c, 0, sizeof *c);
}

int tfx_chd_read_hunk(tfx_chd *c, uint32_t h, uint8_t *out) {
    for (int guard = 0; guard < 4 && h < c->hunks; guard++) {
        const uint8_t *m = c->map + 12 * h;
        if (m[0] == SELF) {
            uint32_t to = (uint32_t)get_be(m + 4, 6);
            if (to >= h)
                return 0;
            h = to;
            continue;
        }
        size_t n = (size_t)get_be(m + 1, 3);
        uint64_t at = get_be(m + 4, 6);
        if (n > TFX_CHD_HUNK || !c->read(c->ctx, at, c->packed, n))
            return 0;
        if (m[0] == NONE ? n != TFX_CHD_HUNK || (memcpy(out, c->packed, n), 0) : !expand(c->packed, n, m[0], out))
            return 0;
        return crc16(out, TFX_CHD_HUNK) == get_be(m + 10, 2);
    }
    return 0;
}

void tfx_chd_cd_image(const tfx_chd *c, tfx_cd_image *img) {
    memset(img, 0, sizeof *img);
    img->n_tracks = c->layout.n_tracks;
    img->n_files = 1;
    snprintf(img->file[0], sizeof img->file[0], "TFX.CHD");
    img->file_size[0] = (uint64_t)c->end_lba * SECTOR;
    for (int i = 0; i < img->n_tracks; i++) {
        tfx_cd_track *t = &img->track[i];
        t->number = c->layout.track[i].number, t->audio = c->layout.track[i].audio;
        t->file = 0, t->lba = c->layout.track[i].lba, t->offset = (uint64_t)t->lba * SECTOR;
    }
    img->end_lba = c->end_lba;
}

size_t tfx_chd_cd_read(tfx_chd *c, uint64_t offset, uint8_t *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        uint64_t at = offset + done;
        int32_t lba = (int32_t)(at / SECTOR);
        size_t in = (size_t)(at % SECTOR), k = SECTOR - in < n - done ? SECTOR - in : n - done;
        int t = -1;
        for (int i = 0; i < c->layout.n_tracks; i++)
            if (c->layout.track[i].lba <= lba && lba < c->layout.track[i].lba + c->layout.track[i].frames)
                t = i;
        if (t < 0)
            break;
        uint32_t f = c->layout.track[t].chd_frame + (uint32_t)(lba - c->layout.track[t].lba), h = f / FRAMES;
        if (h != c->hunk_at) {
            if (!tfx_chd_read_hunk(c, h, c->hunk)) {
                c->hunk_at = ~0u;
                break;
            }
            c->hunk_at = h;
        }
        const uint8_t *s = c->hunk + (f % FRAMES) * TFX_CHD_FRAME;
        if (c->layout.track[t].audio) /* (little-endian again) */
            for (size_t i = 0; i < k; i++)
                buf[done + i] = s[(in + i) ^ 1];
        else
            memcpy(buf + done, s + in, k);
        done += k;
    }
    return done;
}
