/* The OPL2 (Yamaha YM3812, the AdLib's and the Sound Blaster's FM chip)
 * as a native host's synthesizer (W05): the music devices' port writes
 * the core hands the platform (index at 0x388, data at 0x389) turned into
 * the chip's output.
 *
 * The model follows the chip's documented behaviour as the public
 * die-level analyses describe it: 18 operators (phase generator with the
 * frequency multipliers and vibrato, the envelope generator's attack /
 * decay / sustain / release on the chip's global rate counter, key scale
 * rate and level, tremolo), the log-sine and exponent tables (computed
 * here from their definitions), the four waveforms, two-operator FM and
 * additive connection with feedback, the rhythm mode's five instruments
 * with the noise generator. The timers and the status register (the
 * game's detection reads them) are the core's (EX18); this model only
 * sounds. One sample per 72 clocks of the 3.579545 MHz crystal
 * (49715.9 Hz), mono, 16-bit. */
#ifndef TFX_AUDIO_OPL2_H
#define TFX_AUDIO_OPL2_H

#include <stdint.h>

#define TFX_OPL2_RATE 49716 /* (3579545 / 72, rounded) */

typedef struct tfx_opl2_slot {
    int32_t out, prout, fbmod; /* outputs; this sample's, the last, feedback */
    int32_t *mod;              /* the modulation input */
    uint32_t pg_phase;
    uint16_t pg_out;
    uint16_t eg_rout, eg_out;
    uint8_t eg_gen; /* 0 attack, 1 decay, 2 sustain, 3 release */
    uint8_t key;    /* bit 0 the channel's, bit 1 the rhythm's */
    uint8_t am, vib, egt, ksr, mult, ksl, tl, ar, dr, sl, rr, ws;
    uint8_t eg_ksl, pg_reset;
    uint8_t channel;
} tfx_opl2_slot;

typedef struct tfx_opl2_channel {
    uint16_t fnum;
    uint8_t block, fb, cnt, ksv;
} tfx_opl2_channel;

typedef struct tfx_opl2 {
    tfx_opl2_slot slot[18];
    tfx_opl2_channel ch[9];
    uint8_t index;    /* the register selected at 0x388 */
    uint8_t wse, nts, rhy, am_depth, vib_depth;
    uint32_t timer;   /* samples (the vibrato's and tremolo's steps) */
    uint64_t eg_timer;
    uint8_t eg_state, eg_add, eg_timer_lo, eg_timerrem;
    uint8_t vibpos, tremolopos, tremolo;
    uint32_t noise;
    uint8_t rm_hh2, rm_hh3, rm_hh7, rm_hh8, rm_tc3, rm_tc5;
    int32_t zero;
} tfx_opl2;

void tfx_opl2_reset(tfx_opl2 *o);
/* A write to port 0x388 (index) or 0x389 (data); others ignored. */
void tfx_opl2_port(tfx_opl2 *o, uint16_t port, uint8_t value);
/* A register written directly. */
void tfx_opl2_write(tfx_opl2 *o, uint8_t reg, uint8_t value);
/* The next sample at TFX_OPL2_RATE. */
int16_t tfx_opl2_sample(tfx_opl2 *o);

#endif
