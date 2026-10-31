/* The macOS host's sample mixer (M01): the core's samples (tfx_host.pcm:
 * 8-bit unsigned mono at a rate, on a voice, stamped on the host's clock)
 * mixed into the output device's float stereo frames.
 *
 * Each voice holds a short queue of segments by their start; a segment
 * takes the voice over at its start (a Sound Blaster's next block, a new
 * sample on an UltraSound voice, a stop) and plays at its rate held
 * sample by sample (as the cards' DACs: no interpolation) until it ends.
 * A stamp becomes an output frame through the last render's clock reading
 * plus a fixed latency, so the samples keep the spacing the game gave
 * them; a stamp already past starts at once. The render side (the audio
 * device's thread) only reads and marks; the copies are allocated and
 * freed on the game's side. The music devices' port writes (M05,
 * music.h) are stamped the same way, and the CD's audio (cdaudio.h, its
 * calls made with the lock held) is added as it plays. Portable C: no
 * Core Audio here. */
#ifndef TFX_MAC_MIXER_H
#define TFX_MAC_MIXER_H

#include <pthread.h>
#include <stdint.h>

#include "cdaudio.h"
#include "music.h"

#define TFX_MAC_VOICES 8
#define TFX_MAC_SEGMENTS 16

typedef struct tfx_mac_segment {
    int64_t start;  /* the output frame it takes the voice over at */
    uint32_t n;     /* its samples (0: a stop) */
    uint32_t rate;
    uint8_t *data;  /* the copy (malloc'd) */
    int done;       /* passed: freed by the next pcm call */
} tfx_mac_segment;

typedef struct tfx_mac_mixer {
    uint32_t rate;      /* the output's frames a second */
    int64_t latency;    /* frames a stamp is played late */
    float gain;
    pthread_mutex_t lock;
    int64_t frame;      /* the next frame to render */
    uint64_t frame_us;  /* the host clock when it was rendered (0: no render yet) */
    struct {
        tfx_mac_segment s[TFX_MAC_SEGMENTS];
        int n;
    } voice[TFX_MAC_VOICES];
    tfx_mac_music music; /* the music devices (M05) */
    tfx_mac_cd *cd;      /* the CD's audio, NULL without an image */
    float cd_gain;
} tfx_mac_mixer;

void tfx_mac_mixer_init(tfx_mac_mixer *m, uint32_t rate, uint32_t latency_us);
void tfx_mac_mixer_free(tfx_mac_mixer *m);
/* the core's sample (n 0: the voice stops at the stamp); 0 when dropped */
int tfx_mac_mixer_pcm(tfx_mac_mixer *m, uint64_t at_us, int voice, const uint8_t *data, uint32_t n, uint32_t rate);
/* everything queued and playing dropped, the music devices reset (their
 * synthesizer kept, its notes silenced): a new run starts clean */
void tfx_mac_mixer_reset(tfx_mac_mixer *m);
/* a music device's port write (0x388 / 0x389, 0x330 / 0x331); 0 when dropped */
int tfx_mac_mixer_port(tfx_mac_mixer *m, uint64_t at_us, uint16_t port, uint8_t value);
/* frames of interleaved stereo float, the host clock read now */
void tfx_mac_mixer_render(tfx_mac_mixer *m, float *out, uint32_t frames, uint64_t now_us);

#endif
