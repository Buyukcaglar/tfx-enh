/* The native host's sound (W05): what the core hands the platform, on the
 * core's clock, mixed into one 16-bit stereo stream at the output's rate.
 *
 *   port writes   the OPL2's (0x388 / 0x389) to the synthesizer
 *                 (audio/opl2.h); the MPU-401's data (0x330) parsed into
 *                 MIDI messages for the host's MIDI output (running
 *                 status, system exclusive whole), its commands (0x331:
 *                 reset 0xFF, UART 0x3F) dropped after a reset's notes
 *                 off; others ignored
 *   samples       8-bit unsigned mono blocks on a voice (a new block cuts
 *                 the one playing; n 0 stops it), resampled linearly
 *   the CD        the CD player's Red Book audio (host/cdaudio.h)
 *
 * Every event carries the core's time (microseconds); the stream applies
 * it at the output sample for that time: the host renders a block with
 * the core time of its first sample, which runs a fixed delay behind the
 * clock so every event has arrived (the core stamps the music's writes
 * a little in the past). The mixer is not thread safe: the host locks
 * around every call. */
#ifndef TFX_HOST_MIXER_H
#define TFX_HOST_MIXER_H

#include <stddef.h>
#include <stdint.h>

#include "audio/opl2.h"
#include "host/cdaudio.h"

#define TFX_MIXER_VOICES 8
#define TFX_MIXER_EVENTS 65536

typedef struct tfx_mixer_event {
    uint64_t at;
    uint8_t kind; /* 0 port, 1 samples, 2 CD play, 3 CD stop */
    uint8_t value;
    uint16_t port; /* or the voice */
    uint32_t n, rate;
    int32_t from;
    uint8_t *data; /* the samples' copy (the mixer's) */
} tfx_mixer_event;

typedef struct tfx_mixer_voice {
    uint8_t *data;
    uint32_t n, rate;
    uint64_t pos; /* 32.32 */
} tfx_mixer_voice;

typedef struct tfx_mixer {
    int rate;
    tfx_mixer_event *ev;
    unsigned head, tail;
    unsigned dropped;
    tfx_opl2 opl;
    int opl_on;
    uint64_t opl_pos; /* 32.32 OPL samples per output sample */
    int16_t opl_prev, opl_next;
    tfx_mixer_voice voice[TFX_MIXER_VOICES];
    /* MIDI */
    void (*midi)(void *ctx, const uint8_t *msg, size_t n);
    void *midi_ctx;
    uint8_t midi_status, midi_msg[3], midi_have, midi_need;
    uint8_t sysex[512];
    size_t sysex_n;
    int in_sysex;
    /* the CD */
    tfx_cd_player cd;
    const tfx_cd_image *cd_img;
    size_t (*cd_read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n);
    void *cd_ctx;
    uint64_t cd_pos; /* 32.32 CD frames per output frame */
    int16_t cd_prev[2], cd_next[2];
    int cd_tail; /* cd_next holds the play's sample (its last, at the end) */
    int16_t cd_buf[588 * 2]; /* the sector being played */
    int cd_buf_at, cd_buf_n;
    /* levels, 0 .. 256 (256: unity) */
    int vol_music, vol_fx, vol_cd;
} tfx_mixer;

/* NULL: no memory */
tfx_mixer *tfx_mixer_new(int rate);
void tfx_mixer_free(tfx_mixer *m);

void tfx_mixer_set_midi(tfx_mixer *m, void (*midi)(void *ctx, const uint8_t *msg, size_t n), void *ctx);
void tfx_mixer_set_cd(tfx_mixer *m, const tfx_cd_image *img,
                      size_t (*read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n), void *ctx);

void tfx_mixer_port(tfx_mixer *m, uint64_t at, uint16_t port, uint8_t value);
void tfx_mixer_pcm(tfx_mixer *m, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate);
void tfx_mixer_cd_play(tfx_mixer *m, uint64_t at, int32_t from, int32_t n);
void tfx_mixer_cd_stop(tfx_mixer *m, uint64_t at);
/* the track heard now (0 none), as of the events rendered or queued */
int tfx_mixer_cd_heard(const tfx_mixer *m);

/* frames stereo frames, the first at core time t0 (microseconds); events
 * up to the last frame's time are applied in order */
void tfx_mixer_render(tfx_mixer *m, int16_t *out, int frames, uint64_t t0);

/* every event dropped and every voice stopped (a new run) */
void tfx_mixer_clear(tfx_mixer *m);

#endif
