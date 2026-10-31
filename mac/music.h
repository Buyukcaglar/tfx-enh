/* The music devices (M05): the port writes the core reports
 * (tfx_host.audio_port) played at their output frames.
 *
 * - 0x388 / 0x389, the AdLib's OPL2 (opl2.h): synthesized at the chip's
 *   rate (49716 Hz) and resampled to the output's, each register write
 *   taking effect at its frame (the chip's sample nearest it).
 * - 0x330 / 0x331, the MPU-401 in UART mode: the data bytes become MIDI
 *   messages (midi.h) handed to a synthesizer (the host's: Apple's DLS
 *   synthesizer in the application) with their frame's offset in the
 *   buffer being rendered; a reset (0xff at 0x331) silences it (all
 *   notes off, the controllers reset on every channel).
 *
 * The writes are queued by their frame (the mixer converts the stamps)
 * and consumed by the render. Portable C. */
#ifndef TFX_MAC_MUSIC_H
#define TFX_MAC_MUSIC_H

#include <stdint.h>

#include "midi.h"
#include "opl2.h"

#define TFX_MAC_MUSIC_EVENTS 16384

typedef struct tfx_mac_synth {
    void *ctx;
    /* a whole message at a frame of the buffer about to be rendered */
    void (*midi)(void *ctx, const uint8_t *msg, int n, uint32_t offset);
    /* the buffer's frames (interleaved stereo) added into out */
    void (*render)(void *ctx, float *out, uint32_t frames);
} tfx_mac_synth;

typedef struct tfx_mac_music {
    uint32_t rate;
    float gain;
    struct {
        int64_t frame;
        uint16_t port;
        uint8_t value;
    } ev[TFX_MAC_MUSIC_EVENTS];
    uint32_t head, tail;
    int64_t last;      /* the latest queued frame (they stay in order) */
    uint64_t dropped;  /* writes lost to a full queue */
    tfx_opl2 opl;
    int opl_on;        /* written since the start: rendered */
    double opl_step, opl_at; /* the chip's samples per output frame; the position between prev and cur */
    float opl_prev, opl_cur;
    tfx_midi midi;
    tfx_mac_synth synth;
    uint8_t msg[TFX_MIDI_SYSEX];
    uint64_t opl_writes, midi_messages;
} tfx_mac_music;

void tfx_mac_music_init(tfx_mac_music *m, uint32_t rate);
/* a port write played at an output frame; 0 when dropped */
int tfx_mac_music_port(tfx_mac_music *m, int64_t frame, uint16_t port, uint8_t value);
/* the frames from output frame f0 added into out (interleaved stereo),
 * the writes due by then applied at their frames */
void tfx_mac_music_render(tfx_mac_music *m, float *out, uint32_t frames, int64_t f0);

#endif
