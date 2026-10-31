#include "session.h"

#include "config/cfg.h"
#include "present.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

static uint64_t h_now(void *ctx) {
    tfx_mac_session *s = ctx;
    return s->now_us(s->clock_ctx);
}
static uint32_t h_size(void *ctx, const char *path) { return tfx_mac_size(&((tfx_mac_session *)ctx)->files, path); }
static int h_read(void *ctx, const char *path, uint8_t *buf, uint32_t cap) {
    return tfx_mac_read(&((tfx_mac_session *)ctx)->files, path, buf, cap);
}
static int h_write(void *ctx, const char *path, const uint8_t *buf, uint32_t n) {
    return tfx_mac_write_kept(&((tfx_mac_session *)ctx)->files, path, buf, n);
}
static int h_find(void *ctx, const char *pattern, uint16_t attr, uint32_t k, char name[13]) {
    return tfx_mac_find(&((tfx_mac_session *)ctx)->files, pattern, attr, k, name);
}
static const char *h_getenv(void *ctx, const char *name) {
    (void)ctx;
    return strcmp(name, "DID") ? NULL : TFX_MAC_DID;
}
static void h_audio_port(void *ctx, uint64_t at, uint16_t port, uint8_t value) {
    tfx_mac_session *s = ctx;
    s->music_writes++;
    if (s->mixer)
        tfx_mac_mixer_port(s->mixer, at, port, value);
}
/* the CD (the mixer's lock held: its render moves the position) */
static int32_t h_cd_track(void *ctx, int track) { return tfx_mac_cd_track(((tfx_mac_session *)ctx)->cd, track); }
static void h_cd_play(void *ctx, int32_t from, int32_t n) {
    tfx_mac_session *s = ctx;
    s->cd_plays++;
    pthread_mutex_lock(&s->mixer->lock);
    tfx_mac_cd_play(s->cd, from, n);
    pthread_mutex_unlock(&s->mixer->lock);
}
static void h_cd_stop(void *ctx) {
    tfx_mac_session *s = ctx;
    pthread_mutex_lock(&s->mixer->lock);
    tfx_mac_cd_stop(s->cd);
    pthread_mutex_unlock(&s->mixer->lock);
}
static int h_cd_heard(void *ctx) {
    tfx_mac_session *s = ctx;
    pthread_mutex_lock(&s->mixer->lock);
    int t = tfx_mac_cd_heard(s->cd);
    pthread_mutex_unlock(&s->mixer->lock);
    return t;
}
static void h_pcm(void *ctx, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate) {
    tfx_mac_session *s = ctx;
    if (n)
        s->samples++;
    if (s->mixer)
        tfx_mac_mixer_pcm(s->mixer, at, voice, data, n, rate);
}
static void say(tfx_mac_session *s, const char *kind, const char *message) {
    char line[512];
    snprintf(line, sizeof line, "%s: %s", kind, message);
    if (s->log)
        s->log(s->log_ctx, line);
}
static void h_print(void *ctx, const char *message) { say(ctx, "print", message); }
static void h_diag(void *ctx, const char *message) { say(ctx, "diag", message); }

/* the devices TFX.CFG selects (read as START.EXE does: SOUNDFX 1 without
 * music gives the AdLib's) and how they are played here */
static void describe(tfx_mac_session *s) {
    static tfx_game_cfg g;
    char text[4096];
    uint32_t n = tfx_mac_size(&s->files, "TFX.CFG");
    int ok = n && n < sizeof text && tfx_mac_read(&s->files, "TFX.CFG", (uint8_t *)text, n);
    memset(&g, 0, sizeof g);
    tfx_game_cfg_parse(&g, ok ? text : NULL, ok ? n : 0);
    char d[400];
    /* (the parse forces MUSIC 1, its state 0, for SOUNDFX 1 without music) */
    tfx_mac_sound_describe(g.soundfx, g.music_state == 0 ? 0 : g.music, s->cd != NULL, d, sizeof d);
    snprintf(s->audio, sizeof s->audio, "%s%s", ok ? "" : "TFX.CFG absent: ", d);
}

int tfx_mac_session_start(tfx_mac_session *s, const char *root, const char *store) {
    snprintf(s->files.root, sizeof s->files.root, "%s", root);
    snprintf(s->files.store, sizeof s->files.store, "%s", store);
    s->error[0] = 0;
    if (!tfx_mac_store_ready(&s->files, s->error, sizeof s->error)) {
        say(s, "store", s->error);
        return 0;
    }
    if (!s->mixer)
        s->cd = NULL;
    describe(s);
    say(s, "audio", s->audio);
    tfx_host h = {.ctx = s, .now_us = h_now, .size = h_size, .read = h_read, .write = h_write, .find = h_find,
                  .getenv = h_getenv, .audio_port = h_audio_port, .pcm = h_pcm, .print = h_print, .diag = h_diag};
    if (s->cd)
        h.cd_track = h_cd_track, h.cd_play = h_cd_play, h.cd_stop = h_cd_stop, h.cd_heard = h_cd_heard;
    static const char *argv[] = {"TFX.EXE", "cd_demo"};
    tfx_config cfg = {2, argv};
    s->core = tfx_create(&h, &cfg);
    return s->core != NULL;
}

int tfx_mac_session_pump(tfx_mac_session *s, uint64_t budget_us) {
    int out = 0;
    uint64_t t0 = s->now_us(s->clock_ctx);
    for (;;) {
        tfx_step_result r = tfx_step(s->core);
        if (r == TFX_STEP_ENDED)
            return out | TFX_MAC_ENDED;
        if (r == TFX_STEP_WAIT)
            return out;
        out |= TFX_MAC_SHOWN;
        s->frames++;
        if (s->now_us(s->clock_ctx) - t0 >= budget_us)
            return out;
    }
}

void tfx_mac_session_rgba(const tfx_mac_session *s, uint8_t *rgba) {
    const uint8_t *dac, *px = tfx_frame(s->core, &dac);
    uint8_t pal[256][4];
    for (int c = 0; c < 256; c++) {
        for (int k = 0; k < 3; k++)
            pal[c][k] = tfx_mac_dac8(dac[3 * c + k]);
        pal[c][3] = 255;
    }
    for (int i = 0; i < 64000; i++)
        memcpy(rgba + 4 * i, pal[px[i]], 4);
}

int tfx_mac_session_enhanced_rgba(tfx_mac_session *s, int w, int h, uint8_t *rgba) {
    int pw, ph;
    const uint8_t *dac, *px;
    if (!tfx_enhanced(s->core, w, h) || w <= 0 || h <= 0)
        return 0;
    tfx_frame(s->core, &dac);
    if (!(px = tfx_frame_enhanced(s->core, &pw, &ph)) || pw != w || ph != h)
        return 0;
    uint8_t pal[256][4];
    for (int c = 0; c < 256; c++) {
        for (int k = 0; k < 3; k++)
            pal[c][k] = tfx_mac_dac8(dac[3 * c + k]);
        pal[c][3] = 255;
    }
    for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; i++)
        memcpy(rgba + 4 * i, pal[px[i]], 4);
    return 1;
}

void tfx_mac_session_stop(tfx_mac_session *s) {
    tfx_destroy(s->core); /* (a run under way ended first, through the game's exit path) */
    s->core = NULL;
}
