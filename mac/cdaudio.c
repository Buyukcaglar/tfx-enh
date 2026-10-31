#include "cdaudio.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR 2352

static int fail(tfx_mac_cd *cd, const char *what, const char *detail) {
    snprintf(cd->error, sizeof cd->error, "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
    return 0;
}

int tfx_mac_cd_open(tfx_mac_cd *cd, const char *cue_path) {
    memset(cd, 0, sizeof *cd);
    FILE *f = fopen(cue_path, "r");
    if (!f)
        return fail(cd, "The CUE sheet cannot be opened", cue_path);
    char line[512], bin[1024] = "";
    int track = 0, files = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (isspace((unsigned char)*s))
            s++;
        if (!strncmp(s, "FILE", 4)) {
            char *q = strchr(s, '"'), *r = q ? strchr(q + 1, '"') : NULL;
            if (!q || !r || ++files > 1) {
                fclose(f);
                return fail(cd, files > 1 ? "Only CUE sheets with one image file are supported" : "A FILE line has no quoted name", NULL);
            }
            const char *slash = strrchr(cue_path, '/');
            int dir = slash ? (int)(slash - cue_path + 1) : 0;
            snprintf(bin, sizeof bin, "%.*s%.*s", dir, cue_path, (int)(r - q - 1), q + 1);
            if (!strstr(r, "BINARY")) {
                fclose(f);
                return fail(cd, "The image file must be BINARY", NULL);
            }
        } else if (!strncmp(s, "TRACK", 5)) {
            track = atoi(s + 5);
            if (track < 1 || track > TFX_CD_TRACKS || track != cd->tracks + 1) {
                fclose(f);
                return fail(cd, "The CUE sheet's tracks are not numbered 1, 2, 3 ...", NULL);
            }
            cd->tracks = track;
            cd->audio[track] = strstr(s, "AUDIO") != NULL;
            if (!cd->audio[track] && !strstr(s, "/2352")) {
                fclose(f);
                return fail(cd, "Only raw 2352-byte sectors are supported", NULL);
            }
            cd->start[track] = -1;
        } else if (!strncmp(s, "INDEX 01", 8) && track) {
            int m, sec, fr;
            if (sscanf(s + 8, "%d:%d:%d", &m, &sec, &fr) != 3) {
                fclose(f);
                return fail(cd, "An INDEX line cannot be read", NULL);
            }
            cd->start[track] = (m * 60 + sec) * 75 + fr;
        }
    }
    fclose(f);
    if (!cd->tracks || !bin[0])
        return fail(cd, "The CUE sheet names no tracks", NULL);
    for (int t = 1; t <= cd->tracks; t++)
        if (cd->start[t] < 0)
            return fail(cd, "A track has no INDEX 01", NULL);
    int fd = open(bin, O_RDONLY);
    if (fd < 0)
        return fail(cd, "The CD image cannot be opened", bin);
    struct stat st;
    if (fstat(fd, &st) || st.st_size < SECTOR) {
        close(fd);
        return fail(cd, "The CD image is empty", bin);
    }
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return fail(cd, "The CD image cannot be mapped", bin);
    cd->image = map;
    cd->size = st.st_size;
    cd->start[cd->tracks + 1] = (int32_t)(st.st_size / SECTOR);
    return 1;
}

/* the release's table of contents (the user's image, M05): each track's
 * first sector, then the disc's length */
static const int32_t OCEAN[] = {0,      19205,  41213,  62109,  85514,  105553, 127010, 148667,
                                171628, 192812, 200404, 207996, 225937, 230921, 252026, 257010};

int tfx_mac_cd_profile(tfx_mac_cd *cd) {
    int n = (int)(sizeof OCEAN / sizeof OCEAN[0]) - 1;
    if (cd->tracks != n) {
        snprintf(cd->error, sizeof cd->error,
                 "This disc has %d tracks; TFX's CD (Ocean, 1995) has %d, so its music would not match", cd->tracks, n);
        return 0;
    }
    for (int t = 1; t <= n + 1; t++)
        if (cd->start[t] != OCEAN[t - 1] || (t <= n && cd->audio[t] != (t > 1))) {
            snprintf(cd->error, sizeof cd->error,
                     "This disc's %s differs from TFX's CD (Ocean, 1995): sector %d, expected %d",
                     t <= n ? "track layout" : "length", cd->start[t], OCEAN[t - 1]);
            return 0;
        }
    return 1;
}

void tfx_mac_cd_close(tfx_mac_cd *cd) {
    if (cd->image)
        munmap((void *)cd->image, (size_t)cd->size);
    memset(cd, 0, sizeof *cd);
}

int32_t tfx_mac_cd_track(const tfx_mac_cd *cd, int track) {
    return track >= 1 && track <= cd->tracks ? cd->start[track] : -1;
}

void tfx_mac_cd_play(tfx_mac_cd *cd, int32_t from, int32_t n) {
    int32_t last = cd->start[cd->tracks + 1];
    if (!cd->tracks || from < 0 || from >= last || n <= 0)
        return;
    cd->pos = from, cd->end = from + n > last ? last : from + n;
    cd->sample = 0, cd->frac = 0.0;
    cd->playing = 1;
    madvise((void *)(cd->image + (long long)from * SECTOR - ((long long)from * SECTOR) % 16384),
            (size_t)(cd->end - from) * SECTOR, MADV_WILLNEED);
}

void tfx_mac_cd_stop(tfx_mac_cd *cd) { cd->playing = 0; }

int tfx_mac_cd_heard(const tfx_mac_cd *cd) {
    for (int t = cd->tracks; t >= 1; t--)
        if (cd->pos >= cd->start[t])
            return t;
    return 0;
}

/* the 44.1 kHz frame k after the current one, as floats (silence past the end) */
static void frame(const tfx_mac_cd *cd, int k, float *l, float *r) {
    long long at = (long long)cd->pos * SECTOR + (long long)(cd->sample + (uint32_t)k) * 4;
    if (at + 4 > (long long)cd->end * SECTOR || cd->audio[tfx_mac_cd_heard(cd)] == 0) {
        *l = *r = 0.0f;
        return;
    }
    const uint8_t *p = cd->image + at;
    *l = (float)(int16_t)(p[0] | p[1] << 8) / 32768.0f;
    *r = (float)(int16_t)(p[2] | p[3] << 8) / 32768.0f;
}

void tfx_mac_cd_render(tfx_mac_cd *cd, float *out, int frames, int rate, float gain) {
    double step = 44100.0 / rate;
    for (int i = 0; i < frames && cd->playing; i++) {
        float l0, r0, l1, r1;
        frame(cd, 0, &l0, &r0);
        frame(cd, 1, &l1, &r1);
        float t = (float)cd->frac;
        out[2 * i] += gain * (l0 + (l1 - l0) * t);
        out[2 * i + 1] += gain * (r0 + (r1 - r0) * t);
        cd->frac += step;
        while (cd->frac >= 1.0) {
            cd->frac -= 1.0;
            if (++cd->sample == SECTOR / 4) {
                cd->sample = 0;
                if (++cd->pos >= cd->end) { /* rests at the next sector */
                    cd->playing = 0;
                    break;
                }
            }
        }
    }
}
