#include "opl2.h"

#include <math.h>
#include <string.h>

enum { ATTACK, DECAY, SUSTAIN, RELEASE };

static uint16_t logsin[256], exptab[256];
static int tables;

/* the operator behind register offsets 0x00 .. 0x15 (-1: none) */
static const int8_t slot_of[22] = {0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17};
/* each channel's modulator; its carrier is 3 further */
static const uint8_t ch_mod[9] = {0, 1, 2, 6, 7, 8, 12, 13, 14};
/* twice the frequency multiplier (0 is a half) */
static const uint8_t mult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
/* key scale level by the frequency's top four bits */
static const uint8_t ksl_rom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
static const uint8_t ksl_shift[4] = {8, 1, 2, 0};
/* envelope increments: rows by rate & 3 (and the fast groups), eight steps */
static const uint8_t eg_inc[13][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4},
};

static void build_tables(void) {
    for (int i = 0; i < 256; i++) {
        double s = sin((i + 0.5) * 3.14159265358979323846 / 512.0);
        logsin[i] = (uint16_t)lround(-log2(s) * 256.0);
        exptab[i] = (uint16_t)lround((pow(2.0, i / 256.0) - 1.0) * 1024.0);
    }
    tables = 1;
}

void tfx_opl2_init(tfx_opl2 *o) {
    if (!tables)
        build_tables();
    memset(o, 0, sizeof *o);
    for (int i = 0; i < 18; i++) {
        o->op[i].env = 511;
        o->op[i].stage = RELEASE;
    }
    o->noise = 1;
}

/* the operator's level for phase p (10 bits) at attenuation att (log units) */
static int16_t wave(uint8_t w, uint32_t p, uint32_t att) {
    p &= 1023;
    uint32_t l;
    int neg = 0;
    switch (w) {
    case 1: /* half sine */
        if (p & 0x200)
            return 0;
        l = logsin[p & 0x100 ? 255 - (p & 255) : p & 255];
        break;
    case 2: /* absolute sine */
        l = logsin[p & 0x100 ? 255 - (p & 255) : p & 255];
        break;
    case 3: /* quarter sine pulses */
        if (p & 0x100)
            return 0;
        l = logsin[p & 255];
        break;
    default:
        l = logsin[p & 0x100 ? 255 - (p & 255) : p & 255];
        neg = (p & 0x200) != 0;
    }
    l += att;
    if (l >= 13 * 256)
        return 0;
    int v = ((exptab[255 - (l & 255)] + 1024) << 1) >> (l >> 8);
    return (int16_t)(neg ? -v : v);
}

static void key(tfx_opl2_op *op, uint8_t bit, int on) {
    uint8_t was = op->key;
    op->key = (uint8_t)(on ? op->key | bit : op->key & ~bit);
    if (!was && op->key) {
        op->stage = ATTACK;
        op->phase = 0;
    } else if (was && !op->key) {
        op->stage = RELEASE;
    }
}

static void channel_keys(tfx_opl2 *o, int c, int on) {
    key(&o->op[ch_mod[c]], 1, on);
    key(&o->op[ch_mod[c] + 3], 1, on);
}

void tfx_opl2_write(tfx_opl2 *o, uint8_t r, uint8_t v) {
    o->reg[r] = v;
    if (r == 0x01) {
        o->wave_enable = (v & 0x20) != 0;
    } else if (r == 0x08) {
        o->note_sel = (v >> 6) & 1;
    } else if (r >= 0x20 && r < 0xa0) {
        int s = slot_of[(r & 0x1f) < 22 ? r & 0x1f : 6];
        if ((r & 0x1f) >= 22 || s < 0)
            return;
        tfx_opl2_op *op = &o->op[s];
        switch (r & 0xe0) {
        case 0x20:
            op->am = v >> 7, op->vib = (v >> 6) & 1, op->egt = (v >> 5) & 1, op->ksr = (v >> 4) & 1, op->mult = v & 15;
            break;
        case 0x40:
            op->ksl = v >> 6, op->tl = v & 63;
            break;
        case 0x60:
            op->ar = v >> 4, op->dr = v & 15;
            break;
        case 0x80:
            op->sl = v >> 4, op->rr = v & 15;
            break;
        }
    } else if (r >= 0xe0 && (r & 0x1f) < 22 && slot_of[r & 0x1f] >= 0) {
        o->op[slot_of[r & 0x1f]].wave = v & 3;
    } else if (r >= 0xa0 && r <= 0xa8) {
        o->ch[r - 0xa0].fnum = (uint16_t)((o->ch[r - 0xa0].fnum & 0x300) | v);
    } else if (r >= 0xb0 && r <= 0xb8) {
        tfx_opl2_ch *ch = &o->ch[r - 0xb0];
        ch->fnum = (uint16_t)((ch->fnum & 0xff) | (v & 3) << 8);
        ch->block = (v >> 2) & 7;
        channel_keys(o, r - 0xb0, (v >> 5) & 1);
    } else if (r == 0xbd) {
        o->am_depth = v >> 7, o->vib_depth = (v >> 6) & 1, o->rhythm = (v >> 5) & 1;
        int on = o->rhythm;
        key(&o->op[12], 2, on && (v & 0x10)); /* bass drum: both operators of channel 6 */
        key(&o->op[15], 2, on && (v & 0x10));
        key(&o->op[16], 2, on && (v & 0x08)); /* snare: channel 7's carrier */
        key(&o->op[14], 2, on && (v & 0x04)); /* tom: channel 8's modulator */
        key(&o->op[17], 2, on && (v & 0x02)); /* cymbal: channel 8's carrier */
        key(&o->op[13], 2, on && (v & 0x01)); /* hi-hat: channel 7's modulator */
    } else if (r >= 0xc0 && r <= 0xc8) {
        o->ch[r - 0xc0].fb = (v >> 1) & 7, o->ch[r - 0xc0].con = v & 1;
    }
}

void tfx_opl2_port(tfx_opl2 *o, uint16_t port, uint8_t value) {
    if (port == 0x388)
        o->index = value;
    else if (port == 0x389)
        tfx_opl2_write(o, o->index, value);
}

static void envelope(tfx_opl2 *o, tfx_opl2_op *op) {
    uint8_t r4;
    switch (op->stage) {
    case ATTACK:
        r4 = op->ar;
        break;
    case DECAY:
        r4 = op->dr;
        break;
    case SUSTAIN:
        return;
    default:
        r4 = op->rr;
    }
    if (!r4)
        return;
    int rate = r4 * 4 + op->ksr_shift;
    if (rate > 63)
        rate = 63;
    int g = rate >> 2, inc;
    if (g <= 12) {
        int shift = 12 - g;
        if (o->eg_counter & ((1u << shift) - 1))
            return;
        inc = eg_inc[rate & 3][(o->eg_counter >> shift) & 7];
    } else if (g < 15) {
        inc = eg_inc[4 * (g - 12) + (rate & 3)][o->eg_counter & 7];
    } else {
        inc = 4;
    }
    int env = op->env;
    if (op->stage == ATTACK) {
        if (rate >= 60)
            env = 0;
        else if (inc)
            env += (~env * inc) >> 3;
        if (env <= 0) {
            env = 0;
            op->stage = DECAY;
        }
    } else {
        env += inc;
        int sl = op->sl == 15 ? 31 << 4 : op->sl << 4;
        if (op->stage == DECAY && env >= sl) {
            env = sl;
            op->stage = op->egt ? SUSTAIN : RELEASE;
        }
        if (env > 511)
            env = 511;
    }
    op->env = (uint16_t)env;
}

/* the operator's phase increment and key scaling for its channel */
static void frequency(tfx_opl2 *o, tfx_opl2_op *op, const tfx_opl2_ch *ch) {
    int fnum = ch->fnum;
    if (op->vib) {
        int range = (fnum >> 7) & 7;
        if (!o->vib_depth)
            range >>= 1;
        static const int8_t shape[8] = {0, 1, 2, 1, 0, -1, -2, -1};
        int s = shape[o->vib_pos];
        fnum += s == 1 ? range >> 1 : s == 2 ? range : s == -1 ? -(range >> 1) : s == -2 ? -range : 0;
    }
    op->inc = (uint32_t)(((fnum << ch->block) >> 1) * mult2[op->mult]);
    int kso = ch->block << 1 | ((o->note_sel ? ch->fnum >> 8 : ch->fnum >> 9) & 1);
    op->ksr_shift = (uint8_t)(op->ksr ? kso : kso >> 2);
    int ksl = (ksl_rom[ch->fnum >> 6] << 2) - ((8 - ch->block) << 5);
    op->ksl_att = (uint16_t)(ksl > 0 ? ksl >> ksl_shift[op->ksl] : 0);
}

static uint32_t attenuation(const tfx_opl2 *o, const tfx_opl2_op *op) {
    uint32_t a = op->env + (uint32_t)(op->tl << 2) + op->ksl_att;
    if (op->am)
        a += o->am_depth ? o->tremolo >> 2 : o->tremolo >> 4;
    return a << 3;
}

static int16_t operate(const tfx_opl2 *o, tfx_opl2_op *op, uint32_t phase, int32_t pm) {
    int16_t v = wave(o->wave_enable ? op->wave : 0, (phase >> 10) + (uint32_t)pm, attenuation(o, op));
    op->prev = op->out;
    op->out = v;
    return v;
}

void tfx_opl2_render(tfx_opl2 *o, float *out, int n) {
    for (int k = 0; k < n; k++) {
        /* the LFOs: tremolo over 210 steps of 64 samples, vibrato over 8 of 1024 */
        o->lfo_counter++;
        if (!(o->lfo_counter & 63)) {
            uint32_t pos = (o->lfo_counter >> 6) % 210;
            o->tremolo = (uint8_t)(pos < 105 ? pos : 209 - pos);
        }
        o->vib_pos = (uint8_t)((o->lfo_counter >> 10) & 7);
        o->eg_counter++;
        uint32_t nbit = ((o->noise >> 14) ^ o->noise) & 1;
        o->noise = (o->noise >> 1) | (nbit << 22);
        for (int c = 0; c < 9; c++) {
            for (int j = 0; j < 2; j++) {
                tfx_opl2_op *op = &o->op[ch_mod[c] + 3 * j];
                frequency(o, op, &o->ch[c]);
                envelope(o, op);
                op->phase = (op->phase + op->inc) & 0xfffff;
            }
        }
        int32_t sum = 0;
        int melodic = o->rhythm ? 6 : 9;
        for (int c = 0; c < melodic; c++) {
            tfx_opl2_op *m = &o->op[ch_mod[c]], *car = &o->op[ch_mod[c] + 3];
            const tfx_opl2_ch *ch = &o->ch[c];
            int32_t fb = ch->fb ? (m->out + m->prev) >> (9 - ch->fb) : 0;
            int16_t mv = operate(o, m, m->phase, fb);
            int16_t cv = operate(o, car, car->phase, ch->con ? 0 : mv);
            int32_t v = ch->con ? mv + cv : cv;
            sum += c == 6 && o->rhythm ? 2 * v : v;
        }
        if (o->rhythm) {
            /* the bass drum (channel 6, both operators, doubled) */
            tfx_opl2_op *m = &o->op[12], *car = &o->op[15];
            int32_t fb = o->ch[6].fb ? (m->out + m->prev) >> (9 - o->ch[6].fb) : 0;
            int16_t mv = operate(o, m, m->phase, fb);
            sum += 2 * operate(o, car, car->phase, o->ch[6].con ? 0 : mv);
            /* the others from the hi-hat's and cymbal's phase bits and the noise */
            uint32_t hh = o->op[13].phase >> 10, tc = o->op[17].phase >> 10, nb = o->noise & 1;
            uint32_t b2 = (hh >> 2) & 1, b3 = (hh >> 3) & 1, b7 = (hh >> 7) & 1, b8 = (hh >> 8) & 1;
            uint32_t t3 = (tc >> 3) & 1, t5 = (tc >> 5) & 1;
            uint32_t x = (b2 ^ b7) | (b3 ^ t5) | (t3 ^ t5);
            uint32_t p_hh = x << 9 | ((x ^ nb) ? 0xd0 : 0x34);
            uint32_t p_sd = b8 << 9 | (b8 ^ nb) << 8;
            uint32_t p_cy = x << 9 | 0x80;
            sum += 2 * operate(o, &o->op[13], p_hh << 10, 0);
            sum += 2 * operate(o, &o->op[16], p_sd << 10, 0);
            sum += 2 * operate(o, &o->op[14], o->op[14].phase, 0);
            sum += 2 * operate(o, &o->op[17], p_cy << 10, 0);
        }
        out[k] = (float)sum / 16384.0f;
    }
}
