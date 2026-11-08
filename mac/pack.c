#include "pack.h"

#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "files.h"
#include "host/cdaudio.h"
#include "host/chd.h"
#include "host/profile.h"
#include "host/zip.h"

static void fail_with(char *err, size_t cap, const char *why) { snprintf(err, cap, "%s", why); }

/* the whole file into memory (n: its size); NULL when it cannot be read */
static uint8_t *read_all(const char *path, size_t *n, size_t limit) {
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0)
        return NULL;
    uint8_t *data = NULL;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && (size_t)st.st_size <= limit &&
        (data = malloc((size_t)st.st_size + 1))) {
        size_t at = 0;
        while (at < (size_t)st.st_size) {
            ssize_t k = read(fd, data + at, (size_t)st.st_size - at);
            if (k <= 0)
                break;
            at += (size_t)k;
        }
        if (at != (size_t)st.st_size) {
            free(data);
            data = NULL;
        } else
            *n = at;
    }
    close(fd);
    return data;
}

/* NAME.new written whole and synced, then renamed over NAME */
static int write_whole(const char *path, const uint8_t *data, size_t n) {
    char tmp[TFX_MAC_PATH + 8];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return 0;
    int ok = 1;
    for (size_t at = 0; ok && at < n;) {
        ssize_t k = write(fd, data + at, n - at);
        ok = k > 0;
        at += ok ? (size_t)k : 0;
    }
    ok = ok && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    ok = ok && rename(tmp, path) == 0;
    if (!ok)
        unlink(tmp);
    return ok;
}

/* ---- the zip -------------------------------------------------------------------- */

/* a path's parts ('\' or '/', "." skipped) joined by '/' */
static void normal(const char *rel, char *out, size_t cap) {
    size_t n = 0;
    out[0] = 0;
    while (*rel) {
        while (*rel == '\\' || *rel == '/')
            rel++;
        const char *e = rel;
        while (*e && *e != '\\' && *e != '/')
            e++;
        size_t len = (size_t)(e - rel);
        if (len && !(len == 1 && rel[0] == '.') && n + len + 2 < cap) {
            if (n)
                out[n++] = '/';
            memcpy(out + n, rel, len);
            n += len;
            out[n] = 0;
        }
        rel = e;
    }
}

int tfx_mac_pack_find(const tfx_mac_pack *p, const char *rel) {
    char want[260];
    normal(rel, want, sizeof want);
    for (int i = 0; p && i < p->count; i++)
        if (!strcasecmp(p->name[i], want))
            return i;
    return -1;
}

void tfx_mac_pack_free(tfx_mac_pack *p) {
    for (int i = 0; p->data && i < p->count; i++)
        free(p->data[i]);
    free(p->data);
    free(p->size);
    free(p->name);
    memset(p, 0, sizeof *p);
}

int tfx_mac_pack_load(tfx_mac_pack *p, const char *zip, char *err, size_t cap) {
    memset(p, 0, sizeof *p);
    size_t n = 0;
    uint8_t *bytes = read_all(zip, &n, (size_t)1 << 30);
    tfx_zip z = {0};
    if (!bytes || !tfx_zip_open(&z, bytes, n)) {
        free(bytes);
        fail_with(err, cap, bytes ? "the copy of the game's files is not a zip this reads" : "the copy of the game's files cannot be read");
        return 0;
    }
    int ok = (p->name = calloc((size_t)z.count + 1, sizeof *p->name)) && (p->data = calloc((size_t)z.count + 1, sizeof *p->data)) &&
             (p->size = calloc((size_t)z.count + 1, sizeof *p->size));
    for (int i = 0; ok && i < z.count; i++) {
        snprintf(p->name[i], sizeof p->name[i], "%s", z.entry[i].name);
        p->size[i] = z.entry[i].size;
        p->count = i + 1;
        ok = (p->data[i] = malloc(z.entry[i].size + 1)) && tfx_zip_extract(&z, i, p->data[i]);
        if (!ok)
            snprintf(err, cap, "%s in the copy of the game's files is damaged", z.entry[i].name);
    }
    if (!p->name || !p->data || !p->size)
        fail_with(err, cap, "out of memory");
    tfx_zip_close(&z);
    free(bytes);
    if (!ok)
        tfx_mac_pack_free(p);
    return ok;
}

/* the folder's file by its profile path (any case, as DOS) */
static int profile_file(const char *dir, const char *rel, char *out, size_t cap) {
    struct stat st;
    return tfx_mac_resolve(dir, rel, out, cap, 0) && !stat(out, &st) && S_ISREG(st.st_mode);
}

int tfx_mac_pack_zip(const char *game_dir, const char *zip, char *err, size_t cap) {
    int count;
    const tfx_profile_file *f = tfx_profile_baseline(&count);
    tfx_zip_out *z = tfx_zip_out_new();
    if (!z) {
        fail_with(err, cap, "out of memory");
        return 0;
    }
    int ok = 1, added = 0;
    for (int i = 0; ok && i < count; i++) {
        char path[TFX_MAC_PATH];
        if (!profile_file(game_dir, f[i].path, path, sizeof path))
            continue; /* (a file the folder lacks: the profile check has said so) */
        size_t n = 0;
        uint8_t *data = read_all(path, &n, (size_t)1 << 30);
        struct stat st;
        uint16_t date = 0x21, time = 0;
        if (!stat(path, &st)) {
            struct tm t;
            localtime_r(&st.st_mtime, &t);
            if (t.tm_year >= 80) {
                date = (uint16_t)(((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
                time = (uint16_t)((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
            }
        }
        char name[260];
        snprintf(name, sizeof name, "%s", f[i].path);
        for (char *q = name; *q; q++)
            if (*q == '\\')
                *q = '/';
        ok = data && tfx_zip_out_add(z, name, data, (uint32_t)n, time, date);
        added += ok;
        free(data);
        if (!ok)
            snprintf(err, cap, "%s cannot be read", f[i].path);
    }
    size_t n = 0;
    const uint8_t *bytes = ok ? tfx_zip_out_finish(z, &n) : NULL;
    if (ok && (!bytes || !added)) {
        fail_with(err, cap, added ? "out of memory" : "the folder holds none of the game's files");
        ok = 0;
    }
    if (ok && !write_whole(zip, bytes, n)) {
        fail_with(err, cap, "the copy of the game's files cannot be written");
        ok = 0;
    }
    tfx_zip_out_free(z);
    return ok;
}

int tfx_mac_pack_zip_matches(const char *game_dir, const char *zip) {
    tfx_mac_pack p;
    char err[200];
    if (!tfx_mac_pack_load(&p, zip, err, sizeof err))
        return 0;
    int count, same = 1;
    const tfx_profile_file *f = tfx_profile_baseline(&count);
    for (int i = 0; i < count && same; i++) {
        char path[TFX_MAC_PATH];
        struct stat st;
        int k = tfx_mac_pack_find(&p, f[i].path);
        int here = profile_file(game_dir, f[i].path, path, sizeof path) && !stat(path, &st);
        same = here ? k >= 0 && (long long)p.size[k] == (long long)st.st_size : k < 0;
    }
    tfx_mac_pack_free(&p);
    return same;
}

/* ---- the CD ---------------------------------------------------------------------- */

typedef struct disc {
    tfx_cd_image img;
    tfx_chd_layout layout;
    char dir[TFX_MAC_PATH];
} disc;

static void sheet_file(const disc *d, const char *name, char *out, size_t cap) {
    if (name[0] == '/')
        snprintf(out, cap, "%s", name);
    else if (!tfx_mac_resolve(d->dir, name, out, cap, 0)) /* (the name as the sheet spells it, any case) */
        snprintf(out, cap, "%s/%s", d->dir, name);
}

static long long sheet_size(void *ctx, const char *name) {
    char p[TFX_MAC_PATH];
    struct stat st;
    sheet_file(ctx, name, p, sizeof p);
    return stat(p, &st) || !S_ISREG(st.st_mode) ? -1 : (long long)st.st_size;
}

static int load_sheet(disc *d, const char *cue, char *err, size_t cap) {
    memset(d, 0, sizeof *d);
    size_t n = 0;
    char *text = (char *)read_all(cue, &n, 65536);
    if (!text) {
        fail_with(err, cap, "the CD sheet cannot be read");
        return 0;
    }
    snprintf(d->dir, sizeof d->dir, "%s", cue);
    char *slash = strrchr(d->dir, '/');
    if (slash)
        *slash = 0;
    else
        snprintf(d->dir, sizeof d->dir, ".");
    int ok = tfx_cdi_parse(&d->img, text, n, sheet_size, d, err, cap);
    free(text);
    if (ok)
        tfx_chd_layout_cd(&d->layout, &d->img);
    return ok;
}

typedef struct worker {
    const disc *d;
    int files[TFX_CD_FILES]; /* descriptors + 1 (0: not open) */
    tfx_deflater *z;
    uint32_t from, to; /* hunks [from, to) of the batch, every step-th */
    uint32_t step, first;
    uint8_t *raw, *packed; /* the batch's, TFX_CHD_HUNK a hunk */
    uint32_t *len;
    int *codec;
    int ok;
} worker;

static size_t worker_read(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n) {
    worker *w = ctx;
    if (!w->files[file]) {
        char p[TFX_MAC_PATH];
        sheet_file(w->d, w->d->img.file[file], p, sizeof p);
        w->files[file] = open(p, O_RDONLY) + 1;
    }
    if (w->files[file] <= 0)
        return 0;
    size_t got = 0;
    while (got < n) {
        ssize_t k = pread(w->files[file] - 1, buf + got, n - got, (off_t)(offset + got));
        if (k <= 0)
            break;
        got += (size_t)k;
    }
    return got;
}

static void *work_hunks(void *arg) {
    worker *w = arg;
    for (uint32_t h = w->from; h < w->to && w->ok; h += w->step) {
        uint32_t k = h - w->first;
        uint8_t *raw = w->raw + (size_t)k * TFX_CHD_HUNK;
        int kind = tfx_chd_raw_hunk(&w->d->layout, &w->d->img, h, raw, worker_read, w);
        if (!kind)
            w->ok = 0;
        else
            w->len[k] = (uint32_t)tfx_chd_compress(w->z, raw, kind, w->packed + (size_t)k * TFX_CHD_HUNK, &w->codec[k]);
    }
    return NULL;
}

static int fd_read(void *ctx, uint64_t offset, void *buf, size_t n) {
    return pread(*(int *)ctx, buf, n, (off_t)offset) == (ssize_t)n;
}

int tfx_mac_pack_chd_matches(const char *cue, const char *chd) {
    static disc d;
    static tfx_chd c;
    tfx_cd_image back;
    char err[200];
    int fd = open(chd, O_RDONLY);
    int same = fd >= 0 && load_sheet(&d, cue, err, sizeof err) && tfx_chd_open(&c, fd_read, &fd, err, sizeof err);
    if (same) {
        tfx_chd_cd_image(&c, &back);
        same = back.n_tracks == d.img.n_tracks && back.end_lba == d.img.end_lba;
        for (int i = 0; same && i < back.n_tracks; i++)
            same = back.track[i].lba == d.img.track[i].lba && back.track[i].audio == d.img.track[i].audio;
        tfx_chd_close(&c);
    }
    if (fd >= 0)
        close(fd);
    return same;
}

static int write_out(int fd, const void *data, size_t n) {
    const uint8_t *p = data;
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k <= 0)
            return 0;
        p += k, n -= (size_t)k;
    }
    return 1;
}

int tfx_mac_pack_chd(const char *cue, const char *chd, tfx_mac_pack_progress progress, void *ctx, char *err,
                     size_t cap) {
    static disc d;
    if (!load_sheet(&d, cue, err, cap))
        return 0;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t threads = cpus < 1 ? 1 : cpus > 32 ? 32 : (uint32_t)cpus;
    uint32_t batch = 64 * threads, hunks = d.layout.hunks;
    worker w[32];
    memset(w, 0, sizeof w);
    uint8_t *raw = malloc((size_t)batch * TFX_CHD_HUNK), *packed = malloc((size_t)batch * TFX_CHD_HUNK);
    uint32_t *len = malloc(sizeof *len * batch);
    int *codec = malloc(sizeof *codec * batch);
    tfx_chd_writer *cw = tfx_chd_writer_new(&d.layout);
    int ok = raw && packed && len && codec && cw;
    for (uint32_t t = 0; ok && t < threads; t++)
        ok = (w[t].z = tfx_deflater_new()) != NULL;
    char tmp[TFX_MAC_PATH + 8];
    snprintf(tmp, sizeof tmp, "%s.new", chd);
    int out = ok ? open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
    if (!ok)
        fail_with(err, cap, "out of memory");
    else if (out < 0) {
        fail_with(err, cap, "the copy of the CD cannot be written");
        ok = 0;
    }
    uint8_t header[TFX_CHD_HEADER] = {0};
    ok = ok && write_out(out, header, sizeof header);
    for (uint32_t first = 0; ok && first < hunks; first += batch) {
        uint32_t to = first + batch < hunks ? first + batch : hunks;
        pthread_t th[32];
        int started[32] = {0};
        for (uint32_t t = 0; t < threads; t++) {
            w[t].d = &d, w[t].from = first + t, w[t].to = to, w[t].step = threads, w[t].first = first;
            w[t].raw = raw, w[t].packed = packed, w[t].len = len, w[t].codec = codec, w[t].ok = 1;
            started[t] = pthread_create(&th[t], NULL, work_hunks, &w[t]) == 0;
            if (!started[t])
                work_hunks(&w[t]);
        }
        for (uint32_t t = 0; t < threads; t++)
            if (started[t])
                pthread_join(th[t], NULL);
        for (uint32_t t = 0; t < threads; t++)
            if (!w[t].ok) {
                fail_with(err, cap, "the CD image's files cannot be read");
                ok = 0;
            }
        for (uint32_t i = 0; ok && i < to - first; i++) {
            ok = write_out(out, packed + (size_t)i * TFX_CHD_HUNK, len[i]);
            tfx_chd_put(cw, raw + (size_t)i * TFX_CHD_HUNK, len[i], codec[i]);
        }
        if (!ok && w[0].ok)
            fail_with(err, cap, "the copy of the CD cannot be written");
        if (progress)
            progress(ctx, to, hunks);
    }
    const uint8_t *tail;
    size_t tn = 0;
    ok = ok && tfx_chd_finish(cw, &tail, &tn, header) && write_out(out, tail, tn);
    ok = ok && pwrite(out, header, sizeof header, 0) == (ssize_t)sizeof header && fsync(out) == 0;
    if (out >= 0)
        ok = close(out) == 0 && ok;
    if (ok && rename(tmp, chd) != 0) {
        fail_with(err, cap, "the copy of the CD cannot be written");
        ok = 0;
    }
    if (!ok)
        unlink(tmp);
    for (uint32_t t = 0; t < threads; t++) {
        tfx_deflater_free(w[t].z);
        for (int f = 0; f < TFX_CD_FILES; f++)
            if (w[t].files[f] > 0)
                close(w[t].files[f] - 1);
    }
    tfx_chd_writer_free(cw);
    free(raw);
    free(packed);
    free(len);
    free(codec);
    return ok;
}
