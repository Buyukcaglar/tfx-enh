/* The game's own copies of the user's content: deflate and inflate
 * (host/deflate.h), FLAC frames (host/flac.h), the zip archive
 * (host/zip.h) and the CD's CHD (host/chd.h). Round trips over made-up data of every kind the copies
 * hold (text, zeros, noise, 16-bit audio-like waves, short and long runs;
 * a made-up disc of a data track and audio tracks of odd lengths); no
 * game data. With arguments, a tool for the cross-checks against other
 * implementations:
 *   test_pack deflate IN OUT     IN as a raw deflate stream
 *   test_pack inflate IN OUT     a raw deflate stream inflated
 *   test_pack chd SHEET.CUE OUT  the disc as a CHD (one thread)
 *   test_pack chdverify CHD SHEET.CUE  every sector read back against the
 *                                sheet's, the data's SHA-1 against the header */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/chd.h"
#include "host/deflate.h"
#include "host/flac.h"
#include "host/sha1.h"
#include "host/zip.h"

#ifndef _WIN32
#define _fseeki64 fseeko
#define _ftelli64 ftello
#endif

static int fails;
#define CHECK(c) ((c) ? (void)0 : (void)(fails++, printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c)))

static uint32_t seed = 12345;
static uint32_t rnd(void) { return seed = seed * 1103515245u + 12345u, seed >> 8; }

static void round_trip(tfx_deflater *d, const char *what, const uint8_t *in, size_t n) {
    size_t cap = n + n / 8 + 64;
    uint8_t *z = malloc(cap), *back = malloc(n + 1);
    size_t zn = tfx_deflate(d, in, n, z, cap), got = 0;
    CHECK(zn > 0);
    int ok = zn && tfx_inflate(z, zn, back, n + 1, &got);
    CHECK(ok && got == n && !memcmp(in, back, n));
    printf("%-10s %8zu -> %8zu\n", what, n, zn);
    if (zn > 1) /* a stream cut short is refused */
        CHECK(!tfx_inflate(z, zn / 2, back, n + 1, &got) || got < n);
    free(z);
    free(back);
}

static void test_deflate(void) {
    CHECK(tfx_crc32(0, "123456789", 9) == 0xcbf43926u);
    static const uint8_t abc[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                    0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    uint8_t h[20];
    tfx_sha1 s1;
    tfx_sha1_init(&s1);
    tfx_sha1_add(&s1, "abc", 3);
    tfx_sha1_done(&s1, h);
    CHECK(!memcmp(h, abc, 20));
    tfx_deflater *d = tfx_deflater_new();
    enum { N = 1 << 20 };
    uint8_t *b = malloc(N);
    round_trip(d, "empty", b, 0);
    b[0] = 'x';
    round_trip(d, "one", b, 1);
    memset(b, 0, N);
    round_trip(d, "zeros", b, N);
    for (size_t i = 0; i < N; i++)
        b[i] = (uint8_t)rnd();
    round_trip(d, "noise", b, N);
    static const char *words[] = {"the ", "aircraft ", "mission ", "TFX ", "pilot ", "\r\n", "F-22 ", "Eurofighter "};
    size_t n = 0;
    while (n < N - 16) {
        const char *w = words[rnd() % 8];
        memcpy(b + n, w, strlen(w)), n += strlen(w);
    }
    round_trip(d, "text", b, n);
    for (size_t i = 0; i + 1 < N; i += 2) { /* a wave, 16-bit, with some noise */
        int v = 12000 * ((i / 2 % 200) < 100 ? 1 : -1) + (int)(rnd() % 64);
        b[i] = (uint8_t)v, b[i + 1] = (uint8_t)(v >> 8);
    }
    round_trip(d, "wave", b, N);
    for (size_t i = 0; i < N; i++) /* runs of every length */
        b[i] = (uint8_t)(i / (1 + (i >> 12) % 300) & 3);
    round_trip(d, "runs", b, N);
    for (int k = 0; k < 200; k++) { /* small mixed inputs */
        size_t m = rnd() % 3000;
        for (size_t i = 0; i < m; i++)
            b[i] = (uint8_t)(rnd() % (uint32_t)(1 + k % 7));
        uint8_t z[8000], back[3001];
        size_t zn = tfx_deflate(d, b, m, z, sizeof z), got = 0;
        CHECK(zn && tfx_inflate(z, zn, back, sizeof back, &got) && got == m && !memcmp(b, back, m));
    }
    { /* a stream that does not fit is reported */
        uint8_t z[16];
        for (size_t i = 0; i < 4096; i++)
            b[i] = (uint8_t)rnd();
        CHECK(tfx_deflate(d, b, 4096, z, sizeof z) == 0);
    }
    tfx_deflater_free(d);
    free(b);
}

static void flac_trip(const char *what, const int16_t *pcm, int n, int block) {
    static uint8_t z[1 << 20];
    static int16_t back[1 << 18];
    size_t zn = tfx_flac_encode(pcm, n, block, z, sizeof z);
    CHECK(zn > 0);
    CHECK(zn && tfx_flac_decode(z, zn, back, n) == zn && !memcmp(back, pcm, sizeof(int16_t) * 2 * (size_t)n));
    printf("%-10s %8d -> %8zu\n", what, 4 * n, zn);
}

static void test_flac(void) {
    static int16_t pcm[2 * 65536];
    int n = 4704; /* a CD hunk's */
    flac_trip("silence", pcm, n, 2352);
    for (int i = 0; i < n; i++) { /* tones, the channels alike, then apart, then noise */
        double t = i / 44100.0;
        pcm[2 * i] = (int16_t)(9000 * sin(6.2832 * 440 * t) + 3000 * sin(6.2832 * 1250 * t));
        pcm[2 * i + 1] = (int16_t)(pcm[2 * i] / 2 + (int)(rnd() % 16));
    }
    flac_trip("tones", pcm, n, 2352);
    for (int i = 0; i < n; i++)
        pcm[2 * i] = (int16_t)(i % 2 ? 32767 : -32768), pcm[2 * i + 1] = (int16_t)(i % 3 ? -32768 : 32767);
    flac_trip("extremes", pcm, n, 2352);
    for (int i = 0; i < 2 * 65536; i++)
        pcm[i] = (int16_t)rnd();
    flac_trip("noise", pcm, 65536, 4096);
    flac_trip("odd", pcm, 1000, 333);
    static uint8_t z[64];
    CHECK(tfx_flac_encode(pcm, 4704, 2352, z, sizeof z) == 0);
}

static void test_zip(void) {
    tfx_zip_out *o = tfx_zip_out_new();
    static uint8_t a[100000], b[5], back[100000];
    for (size_t i = 0; i < sizeof a; i++)
        a[i] = (uint8_t)(i % 251 < 120 ? i % 7 : rnd());
    memcpy(b, "hello", 5);
    CHECK(tfx_zip_out_add(o, "DID.DAT", a, sizeof a, 0x6000, 0x1c55));
    CHECK(tfx_zip_out_add(o, "MISSIONS/MISS830.MIS", b, 5, 0, 0x21));
    CHECK(tfx_zip_out_add(o, "EMPTY", b, 0, 0, 0x21));
    size_t n = 0;
    const uint8_t *z = tfx_zip_out_finish(o, &n);
    CHECK(z != NULL);
    tfx_zip r;
    CHECK(tfx_zip_open(&r, z, n) && r.count == 3);
    int i = tfx_zip_find(&r, "missions\\miss830.mis");
    CHECK(i == 1 && r.entry[1].size == 5 && tfx_zip_extract(&r, i, back) && !memcmp(back, b, 5));
    CHECK(tfx_zip_extract(&r, 0, back) && !memcmp(back, a, sizeof a) && r.entry[0].method == 8);
    CHECK(tfx_zip_find(&r, "EMPTY") == 2 && tfx_zip_extract(&r, 2, back) && tfx_zip_find(&r, "NONE") < 0);
    printf("zip        %8zu -> %8zu\n", sizeof a + 5, n);
    ((uint8_t *)z)[200] ^= 0x40; /* damage in a member: its CRC */
    CHECK(!tfx_zip_extract(&r, 0, back));
    tfx_zip_close(&r);
    tfx_zip_out_free(o);
}

/* --- the CHD: a made-up disc in memory (track 1 data, its sectors
 * numbered; tracks 2 .. 4 audio of odd lengths) ----------------------------- */

#define DISC_SECTORS (101 + 37 + 1 + 230)
static uint8_t *disc;

static long long disc_size(void *ctx, const char *name) { return (void)ctx, (void)name, (long long)DISC_SECTORS * 2352; }

static size_t disc_read(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n) {
    (void)ctx, (void)file;
    uint64_t end = (uint64_t)DISC_SECTORS * 2352;
    if (offset >= end)
        return 0;
    if (offset + n > end)
        n = (size_t)(end - offset);
    memcpy(buf, disc + offset, n);
    return n;
}

typedef struct mem_file {
    uint8_t *p;
    size_t n;
} mem_file;

static int mem_read(void *ctx, uint64_t offset, void *buf, size_t n) {
    mem_file *m = ctx;
    if (offset + n > m->n)
        return 0;
    memcpy(buf, m->p + offset, n);
    return 1;
}

static void test_chd(void) {
    disc = malloc((size_t)DISC_SECTORS * 2352);
    for (size_t i = 0; i < (size_t)DISC_SECTORS * 2352; i++)
        disc[i] = (uint8_t)(i / 2352 < 101 ? i / 2352 * 7 + (i % 2352 < 16 ? i : 0) : i * 31 + i / 2352 + rnd() % 3);
    /* track 1 at 0 (101 sectors), 2 at 101 = 00:01:26 (37), 3 at 138 =
     * 00:01:63 (1), 4 at 139 = 00:01:64 (230) */
    static const char text[] = "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
                               "  TRACK 02 AUDIO\n    INDEX 01 00:01:26\n  TRACK 03 AUDIO\n    INDEX 01 00:01:63\n"
                               "  TRACK 04 AUDIO\n    INDEX 01 00:01:64\n";
    char err[200];
    static tfx_cd_image img;
    CHECK(tfx_cdi_parse(&img, text, strlen(text), disc_size, NULL, err, sizeof err));
    tfx_chd_layout l;
    tfx_chd_layout_cd(&l, &img);
    CHECK(l.n_tracks == 4 && l.track[0].frames == 101 && l.track[1].frames == 37 && l.track[2].frames == 1 &&
          l.track[3].frames == 230);
    CHECK(l.track[1].chd_frame == 104 && l.track[2].chd_frame == 144 && l.track[3].chd_frame == 148 && l.frames == 380);
    tfx_chd_writer *w = tfx_chd_writer_new(&l);
    tfx_deflater *d = tfx_deflater_new();
    mem_file f = {calloc((size_t)l.hunks * TFX_CHD_HUNK + 65536, 1), TFX_CHD_HEADER};
    static uint8_t raw[TFX_CHD_HUNK];
    int stored = 0, flac = 0;
    for (uint32_t h = 0; h < l.hunks; h++) {
        int codec = -1, kind = tfx_chd_raw_hunk(&l, &img, h, raw, disc_read, NULL);
        CHECK(kind == (h < 13 ? 1 : 2));
        size_t k = tfx_chd_compress(d, raw, kind, f.p + f.n, &codec);
        stored += codec == 4, flac += codec == 1;
        f.n += k;
        tfx_chd_put(w, raw, k, codec);
    }
    const uint8_t *tail;
    size_t tn = 0;
    CHECK(tfx_chd_finish(w, &tail, &tn, f.p));
    memcpy(f.p + f.n, tail, tn), f.n += tn;
    tfx_chd_writer_free(w);
    tfx_deflater_free(d);
    printf("chd        %8zu -> %8zu (%u hunks: %d FLAC, %d stored)\n", (size_t)DISC_SECTORS * 2352, f.n, l.hunks, flac,
           stored);
    tfx_chd c;
    CHECK(tfx_chd_open(&c, mem_read, &f, err, sizeof err));
    tfx_cd_image back;
    tfx_chd_cd_image(&c, &back);
    CHECK(back.n_tracks == 4 && back.end_lba == DISC_SECTORS && back.track[3].lba == 139 && back.track[1].audio &&
          !back.track[0].audio);
    static uint8_t s[3000];
    int same = 1;
    for (int32_t lba = 0; lba < DISC_SECTORS; lba++)
        same &= tfx_chd_cd_read(&c, (uint64_t)lba * 2352, s, 2352) == 2352 && !memcmp(s, disc + (size_t)lba * 2352, 2352);
    CHECK(same);
    CHECK(tfx_chd_cd_read(&c, (uint64_t)200 * 2352 + 1000, s, 3000) == 3000 && !memcmp(s, disc + 200 * 2352 + 1000, 3000));
    tfx_chd_close(&c);
    f.p[f.n - tn + 20] ^= 1; /* a damaged map is refused */
    CHECK(!tfx_chd_open(&c, mem_read, &f, err, sizeof err));
    free(f.p);
    free(disc);
}

/* --- the tools ------------------------------------------------------------------- */

static char sheet_dir[400];
static FILE *sheet_files[TFX_CD_FILES];

static long long sheet_size(void *ctx, const char *name) {
    char p[700];
    snprintf(p, sizeof p, "%s%s", sheet_dir, name);
    FILE *f = fopen(p, "rb");
    (void)ctx;
    if (!f)
        return -1;
    _fseeki64(f, 0, SEEK_END);
    long long n = _ftelli64(f);
    fclose(f);
    return n;
}

static size_t sheet_read(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n) {
    const tfx_cd_image *img = ctx;
    if (!sheet_files[file]) {
        char p[700];
        snprintf(p, sizeof p, "%s%s", sheet_dir, img->file[file]);
        sheet_files[file] = fopen(p, "rb");
    }
    if (!sheet_files[file] || _fseeki64(sheet_files[file], (long long)offset, SEEK_SET))
        return 0;
    return fread(buf, 1, n, sheet_files[file]);
}

static int load_sheet(const char *sheet, tfx_cd_image *img) {
    static char text[65536];
    char err[200];
    FILE *f = fopen(sheet, "rb");
    if (!f)
        return 0;
    size_t n = fread(text, 1, sizeof text, f);
    fclose(f);
    snprintf(sheet_dir, sizeof sheet_dir, "%s", sheet);
    char *slash = strrchr(sheet_dir, '/'), *back = strrchr(sheet_dir, '\\');
    if (back > slash)
        slash = back;
    if (slash)
        slash[1] = 0;
    else
        sheet_dir[0] = 0;
    if (!tfx_cdi_parse(img, text, n, sheet_size, NULL, err, sizeof err)) {
        printf("%s\n", err);
        return 0;
    }
    return 1;
}

static int chd_tool(const char *sheet, const char *out) {
    static tfx_cd_image img;
    if (!load_sheet(sheet, &img))
        return 1;
    tfx_chd_layout l;
    tfx_chd_layout_cd(&l, &img);
    tfx_chd_writer *w = tfx_chd_writer_new(&l);
    tfx_deflater *d = tfx_deflater_new();
    FILE *o = fopen(out, "wb");
    static uint8_t raw[TFX_CHD_HUNK], packed[TFX_CHD_HUNK], header[TFX_CHD_HEADER];
    if (!o || !w || !d)
        return 2;
    fwrite(header, 1, sizeof header, o);
    for (uint32_t h = 0; h < l.hunks; h++) {
        int codec, kind = tfx_chd_raw_hunk(&l, &img, h, raw, sheet_read, &img);
        if (!kind)
            return 1;
        size_t k = tfx_chd_compress(d, raw, kind, packed, &codec);
        fwrite(packed, 1, k, o);
        tfx_chd_put(w, raw, k, codec);
    }
    const uint8_t *tail;
    size_t tn;
    tfx_chd_finish(w, &tail, &tn, header);
    fwrite(tail, 1, tn, o);
    _fseeki64(o, 0, SEEK_SET);
    fwrite(header, 1, sizeof header, o);
    fclose(o);
    printf("%d tracks, %u frames, %u hunks\n", l.n_tracks, l.frames, l.hunks);
    return 0;
}

static int file_read(void *ctx, uint64_t offset, void *buf, size_t n) {
    return !_fseeki64(ctx, (long long)offset, SEEK_SET) && fread(buf, 1, n, ctx) == n;
}

static int chd_verify(const char *chd, const char *sheet) {
    static tfx_cd_image img;
    FILE *f = fopen(chd, "rb");
    tfx_chd c;
    char err[200];
    if (!f || !load_sheet(sheet, &img) || !tfx_chd_open(&c, file_read, f, err, sizeof err)) {
        printf("cannot open: %s\n", f ? err : chd);
        return 1;
    }
    tfx_cd_image back;
    tfx_chd_cd_image(&c, &back);
    int bad = back.n_tracks != img.n_tracks || back.end_lba != img.end_lba;
    for (int i = 0; i < img.n_tracks; i++)
        bad |= back.track[i].lba != img.track[i].lba || back.track[i].audio != img.track[i].audio;
    static uint8_t a[2352], b[2352], raw[TFX_CHD_HUNK];
    for (int32_t lba = 0; lba < img.end_lba && bad < 10; lba++)
        if (tfx_chd_cd_read(&c, (uint64_t)lba * 2352, a, 2352) != 2352 ||
            sheet_read(&img, 0, (uint64_t)lba * 2352, b, 2352) != 2352 || memcmp(a, b, 2352))
            printf("sector %d differs\n", (int)lba), bad++;
    tfx_sha1 s;
    tfx_sha1_init(&s);
    uint64_t left = (uint64_t)c.layout.frames * TFX_CHD_FRAME;
    for (uint32_t h = 0; h < c.hunks; h++) {
        if (!tfx_chd_read_hunk(&c, h, raw)) {
            printf("hunk %u damaged\n", h), bad++;
            break;
        }
        size_t k = left < TFX_CHD_HUNK ? (size_t)left : TFX_CHD_HUNK;
        tfx_sha1_add(&s, raw, k), left -= k;
    }
    uint8_t sum[20], header[TFX_CHD_HEADER];
    tfx_sha1_done(&s, sum);
    file_read(f, 0, header, sizeof header);
    bad |= memcmp(sum, header + 64, 20) != 0;
    printf("%d tracks, %d sectors, %u hunks: %s\n", back.n_tracks, (int)back.end_lba, c.hunks, bad ? "DIFFERENT" : "the same");
    tfx_chd_close(&c);
    fclose(f);
    return bad != 0;
}

static int tool(char **argv) {
    if (!strcmp(argv[1], "chd"))
        return chd_tool(argv[2], argv[3]);
    if (!strcmp(argv[1], "chdverify"))
        return chd_verify(argv[2], argv[3]);
    FILE *f = fopen(argv[2], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *in = malloc((size_t)n + 1);
    size_t cap = (size_t)n * 20 + 1024, got = 0;
    uint8_t *out = malloc(cap);
    if (fread(in, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    if (!strcmp(argv[1], "deflate")) {
        tfx_deflater *d = tfx_deflater_new();
        got = tfx_deflate(d, in, (size_t)n, out, cap);
        tfx_deflater_free(d);
    } else if (!tfx_inflate(in, (size_t)n, out, cap, &got))
        return 1;
    f = fopen(argv[3], "wb");
    fwrite(out, 1, got, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4)
        return tool(argv);
    test_deflate();
    test_flac();
    test_zip();
    test_chd();
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
