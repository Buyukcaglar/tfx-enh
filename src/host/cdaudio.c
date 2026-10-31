#include "host/cdaudio.h"

#include <stdio.h>
#include <string.h>

static int upcase(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }

/* the next word of a line (a quoted one whole), lower bound: end */
static const char *word(const char *p, const char *end, char *out, size_t cap) {
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    size_t n = 0;
    if (p < end && *p == '"') {
        p++;
        while (p < end && *p != '"') {
            if (n + 1 < cap)
                out[n++] = *p;
            p++;
        }
        if (p < end)
            p++;
    } else {
        while (p < end && *p != ' ' && *p != '\t') {
            if (n + 1 < cap)
                out[n++] = *p;
            p++;
        }
    }
    out[n] = 0;
    return p;
}

static int keyword(const char *w, const char *k) {
    while (*w && *k && upcase((unsigned char)*w) == *k)
        w++, k++;
    return !*w && !*k;
}

static int msf(const char *s, int32_t *frames) {
    unsigned m, sec, f;
    if (sscanf(s, "%u:%u:%u", &m, &sec, &f) != 3 || sec >= 60 || f >= 75)
        return 0;
    *frames = (int32_t)((m * 60 + sec) * 75 + f);
    return 1;
}

int tfx_cdi_parse(tfx_cd_image *img, const char *text, size_t n,
                 long long (*file_size)(void *ctx, const char *name), void *ctx, char *err, size_t err_cap) {
    memset(img, 0, sizeof *img);
    const char *p = text, *end = text + n;
    int cur = -1, file = -1;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r')
            eol++;
        char k[32], a[260], b[64];
        const char *q = word(p, eol, k, sizeof k);
        if (keyword(k, "FILE")) {
            q = word(q, eol, a, sizeof a);
            word(q, eol, b, sizeof b);
            if (img->n_files == TFX_CD_FILES) {
                snprintf(err, err_cap, "the sheet names more than %d files", TFX_CD_FILES);
                return 0;
            }
            if (!keyword(b, "BINARY")) {
                snprintf(err, err_cap, "file \"%s\" is %s, not BINARY", a, b);
                return 0;
            }
            long long size = file_size(ctx, a);
            if (size < 0) {
                snprintf(err, err_cap, "the image file \"%s\" is missing", a);
                return 0;
            }
            file = img->n_files++;
            snprintf(img->file[file], sizeof img->file[file], "%s", a);
            img->file_size[file] = (uint64_t)size;
            img->file_lba[file] = file ? img->file_lba[file - 1] + (int32_t)(img->file_size[file - 1] / TFX_CD_SECTOR) : 0;
        } else if (keyword(k, "TRACK")) {
            q = word(q, eol, a, sizeof a);
            word(q, eol, b, sizeof b);
            int num = 0;
            if (file < 0 || sscanf(a, "%d", &num) != 1 || num < 1 || num > 99 || img->n_tracks == TFX_CD_TRACKS) {
                snprintf(err, err_cap, "a TRACK line the player cannot use (\"%s %s\")", a, b);
                return 0;
            }
            int audio = keyword(b, "AUDIO");
            if (!audio && !keyword(b, "MODE1/2352") && !keyword(b, "MODE2/2352")) {
                snprintf(err, err_cap, "track %d is %s (only 2352-byte sectors are supported)", num, b);
                return 0;
            }
            cur = img->n_tracks++;
            img->track[cur] = (tfx_cd_track){num, audio, file, 0, -1};
        } else if (keyword(k, "INDEX") && cur >= 0) {
            q = word(q, eol, a, sizeof a);
            word(q, eol, b, sizeof b);
            int32_t f;
            if (!strcmp(a, "01") || !strcmp(a, "1")) {
                if (!msf(b, &f)) {
                    snprintf(err, err_cap, "track %d's INDEX 01 \"%s\" is not mm:ss:ff", img->track[cur].number, b);
                    return 0;
                }
                img->track[cur].offset = (uint64_t)f * TFX_CD_SECTOR;
                img->track[cur].lba = img->file_lba[img->track[cur].file] + f;
            }
        }
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r'))
            p++;
    }
    if (!img->n_tracks) {
        snprintf(err, err_cap, "the sheet lists no tracks");
        return 0;
    }
    for (int i = 0; i < img->n_tracks; i++)
        if (img->track[i].lba < 0) {
            snprintf(err, err_cap, "track %d has no INDEX 01", img->track[i].number);
            return 0;
        }
    int last = img->n_files - 1;
    img->end_lba = img->file_lba[last] + (int32_t)(img->file_size[last] / TFX_CD_SECTOR);
    return 1;
}

int32_t tfx_cdi_track_start(const tfx_cd_image *img, int number) {
    for (int i = 0; i < img->n_tracks; i++)
        if (img->track[i].number == number)
            return img->track[i].lba;
    return -1;
}

static int index_at(const tfx_cd_image *img, int32_t lba) {
    int found = -1;
    for (int i = 0; i < img->n_tracks; i++)
        if (img->track[i].lba <= lba && lba < img->end_lba)
            found = i;
    return found;
}

int tfx_cdi_track_at(const tfx_cd_image *img, int32_t lba) {
    int i = index_at(img, lba);
    return i < 0 ? 0 : img->track[i].number;
}

void tfx_cdi_play(tfx_cd_player *p, const tfx_cd_image *img, int32_t from, int32_t n) {
    p->img = img;
    if (from < 0)
        from = 0;
    if (n < 0)
        n = 0;
    p->at = from;
    p->end = from + n > img->end_lba ? img->end_lba : from + n;
    if (p->end < p->at)
        p->end = p->at;
    p->frame = 0;
}

void tfx_cdi_stop(tfx_cd_player *p) { p->end = p->at; }

int tfx_cdi_heard(const tfx_cd_player *p) { return p->img && p->at < p->end ? tfx_cdi_track_at(p->img, p->at) : 0; }

int tfx_cdi_render(tfx_cd_player *p, int16_t *out, int frames,
                  size_t (*read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n), void *ctx) {
    int done = 0;
    uint8_t sector[TFX_CD_SECTOR];
    while (done < frames && p->img && p->at < p->end) {
        int i = index_at(p->img, p->at);
        const tfx_cd_track *t = i >= 0 ? &p->img->track[i] : NULL;
        int take = 588 - p->frame;
        if (take > frames - done)
            take = frames - done;
        size_t got = 0;
        if (t && t->audio) {
            int file = t->file;
            uint64_t off = (uint64_t)(p->at - p->img->file_lba[file]) * TFX_CD_SECTOR + (uint64_t)p->frame * 4;
            got = read(ctx, file, off, sector, (size_t)take * 4);
        }
        for (int k = 0; k < take; k++) {
            int16_t l = 0, r = 0;
            if ((size_t)k * 4 + 4 <= got) {
                l = (int16_t)(sector[4 * k] | sector[4 * k + 1] << 8);
                r = (int16_t)(sector[4 * k + 2] | sector[4 * k + 3] << 8);
            }
            out[2 * (done + k)] = l, out[2 * (done + k) + 1] = r;
        }
        done += take;
        p->frame += take;
        if (p->frame == 588)
            p->frame = 0, p->at++;
    }
    return done;
}
