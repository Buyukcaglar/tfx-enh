/* An OPL2 (YM3812) synthesizer for the game's AdLib music (M05): the
 * port writes the core reports (0x388 the register index, 0x389 its
 * value) drive nine two-operator channels or six channels and five
 * rhythm instruments, rendered at the chip's rate (3.579545 MHz / 72,
 * about 49716 Hz). Log-sine and exponent tables, the four waveforms, the
 * envelope generator's rates with key scaling, feedback, FM and additive
 * connection, tremolo and vibrato, and the rhythm section's noise are
 * modelled after the chip's documented behaviour. Portable C, no state
 * outside the struct. */
#ifndef TFX_MAC_OPL2_H
#define TFX_MAC_OPL2_H

#include <stdint.h>

#define TFX_OPL2_RATE 49716

typedef struct tfx_opl2_op {
    uint32_t phase;     /* 10.10 fixed point */
    uint32_t inc;       /* phase increment per sample */
    uint16_t env;       /* attenuation, 0 (loud) .. 511 (silent) */
    uint8_t stage;      /* attack, decay, sustain, release */
    uint8_t key;        /* key on (bit 0 by the channel, bit 1 by rhythm) */
    uint8_t am, vib, egt, ksr, mult, ksl, tl, ar, dr, sl, rr, wave;
    uint8_t ksr_shift;  /* rate offset from key scaling */
    uint16_t ksl_att;   /* attenuation from key scale level */
    int16_t out, prev;  /* the last two outputs (feedback) */
} tfx_opl2_op;

typedef struct tfx_opl2_ch {
    uint16_t fnum;
    uint8_t block, fb, con;
} tfx_opl2_ch;

typedef struct tfx_opl2 {
    tfx_opl2_op op[18];
    tfx_opl2_ch ch[9];
    uint8_t reg[256];
    uint8_t index;
    uint8_t wave_enable, am_depth, vib_depth, rhythm, note_sel;
    uint32_t eg_counter, lfo_counter, noise;
    uint8_t tremolo, vib_pos;
} tfx_opl2;

void tfx_opl2_init(tfx_opl2 *o);
/* a port write: 0x388 (index) or 0x389 (data) */
void tfx_opl2_port(tfx_opl2 *o, uint16_t port, uint8_t value);
/* a register write */
void tfx_opl2_write(tfx_opl2 *o, uint8_t reg, uint8_t value);
/* n samples at TFX_OPL2_RATE, mono, about -1 .. 1 */
void tfx_opl2_render(tfx_opl2 *o, float *out, int n);

#endif
