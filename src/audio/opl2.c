#include "audio/opl2.h"

#include <math.h>
#include <string.h>

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

/* the chip's ROMs: -log2 of a quarter sine and 2^x, both in 1/256 steps */
static uint16_t logsin[256], expo[256];
static int tables;

static void build_tables(void) {
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < 256; i++) {
        logsin[i] = (uint16_t)floor(-log2(sin((i + 0.5) * pi / 512.0)) * 256.0 + 0.5);
        expo[i] = (uint16_t)floor(pow(2.0, (255 - i) / 256.0) * 1024.0 + 0.5);
    }
    tables = 1;
}

static const uint8_t MULT_X2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
static const uint8_t KSL_ROM[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
static const uint8_t KSL_SHIFT[4] = {8, 1, 2, 0};
static const uint8_t EG_INCSTEP[4][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 1, 0}, {1, 1, 1, 0}};
/* register offset (0 .. 0x15) to operator, -1 none */
static const int8_t SLOT_OF[0x16] = {0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17};
/* operator to channel, and the channel's two operators */
static const uint8_t CH_OF[18] = {0, 1, 2, 0, 1, 2, 3, 4, 5, 3, 4, 5, 6, 7, 8, 6, 7, 8};
static const uint8_t CH_MOD[9] = {0, 1, 2, 6, 7, 8, 12, 13, 14};

static int32_t exp_level(uint32_t level) {
    if (level > 0x1fff)
        level = 0x1fff;
    return (expo[level & 0xff] << 1) >> (level >> 8);
}

static int32_t wave(uint8_t ws, uint16_t phase, uint16_t env) {
    phase &= 0x3ff;
    uint32_t out;
    int neg = 0;
    switch (ws & 3) {
    case 0: /* sine */
        neg = (phase & 0x200) != 0;
        out = phase & 0x100 ? logsin[(phase & 0xff) ^ 0xff] : logsin[phase & 0xff];
        break;
    case 1: /* half sine */
        out = phase & 0x200 ? 0x1000 : phase & 0x100 ? logsin[(phase & 0xff) ^ 0xff] : logsin[phase & 0xff];
        break;
    case 2: /* absolute sine */
        out = phase & 0x100 ? logsin[(phase & 0xff) ^ 0xff] : logsin[phase & 0xff];
        break;
    default: /* quarter pulses */
        out = phase & 0x100 ? 0x1000 : logsin[phase & 0xff];
        break;
    }
    int32_t v = exp_level(out + ((uint32_t)env << 3));
    return neg ? ~v : v;
}

static void update_ksl(tfx_opl2 *o, tfx_opl2_slot *s) {
    tfx_opl2_channel *c = &o->ch[s->channel];
    int ksl = (KSL_ROM[c->fnum >> 6] << 2) - ((8 - c->block) << 5);
    s->eg_ksl = (uint8_t)(ksl < 0 ? 0 : ksl);
}

static void update_channel_freq(tfx_opl2 *o, int ch) {
    tfx_opl2_channel *c = &o->ch[ch];
    c->ksv = (uint8_t)(c->block << 1 | ((c->fnum >> (9 - o->nts)) & 1));
    update_ksl(o, &o->slot[CH_MOD[ch]]);
    update_ksl(o, &o->slot[CH_MOD[ch] + 3]);
}

static void connect(tfx_opl2 *o, int ch) {
    tfx_opl2_slot *m = &o->slot[CH_MOD[ch]], *c = &o->slot[CH_MOD[ch] + 3];
    m->mod = &m->fbmod;
    c->mod = o->ch[ch].cnt ? &o->zero : &m->out;
    if (o->rhy && ch >= 7) /* HH and SD, TOM and CY: each operator alone */
        m->mod = c->mod = &o->zero;
}

static void key(tfx_opl2_slot *s, uint8_t bit, int on) {
    if (on)
        s->key |= bit;
    else
        s->key &= (uint8_t)~bit;
}

void tfx_opl2_reset(tfx_opl2 *o) {
    if (!tables)
        build_tables();
    memset(o, 0, sizeof *o);
    for (int i = 0; i < 18; i++) {
        o->slot[i].channel = CH_OF[i];
        o->slot[i].eg_rout = 0x1ff;
        o->slot[i].eg_out = 0x1ff;
        o->slot[i].eg_gen = EG_RELEASE;
    }
    for (int ch = 0; ch < 9; ch++)
        connect(o, ch);
    o->noise = 1;
}

void tfx_opl2_port(tfx_opl2 *o, uint16_t port, uint8_t value) {
    if (port == 0x388)
        o->index = value;
    else if (port == 0x389)
        tfx_opl2_write(o, o->index, value);
}

void tfx_opl2_write(tfx_opl2 *o, uint8_t reg, uint8_t v) {
    int hi = reg & 0xe0, lo = reg & 0x1f;
    if (reg == 0x01) {
        o->wse = (v >> 5) & 1;
        return;
    }
    if (reg == 0x08) {
        o->nts = (v >> 6) & 1;
        for (int ch = 0; ch < 9; ch++)
            update_channel_freq(o, ch);
        return;
    }
    if (reg == 0xbd) {
        o->am_depth = (v >> 7) & 1;
        o->vib_depth = (v >> 6) & 1;
        int rhy = (v >> 5) & 1;
        if (rhy != o->rhy) {
            o->rhy = (uint8_t)rhy;
            for (int ch = 6; ch < 9; ch++)
                connect(o, ch);
        }
        /* BD: operators 12 and 15; SD 16; TOM 14; CY 17; HH 13 */
        key(&o->slot[12], 2, rhy && (v & 0x10));
        key(&o->slot[15], 2, rhy && (v & 0x10));
        key(&o->slot[16], 2, rhy && (v & 0x08));
        key(&o->slot[14], 2, rhy && (v & 0x04));
        key(&o->slot[17], 2, rhy && (v & 0x02));
        key(&o->slot[13], 2, rhy && (v & 0x01));
        return;
    }
    if (hi >= 0x20 && hi <= 0x80 && lo < 0x16) {
        int si = SLOT_OF[lo];
        if (si < 0)
            return;
        tfx_opl2_slot *s = &o->slot[si];
        switch (hi) {
        case 0x20:
            s->am = (v >> 7) & 1, s->vib = (v >> 6) & 1, s->egt = (v >> 5) & 1, s->ksr = (v >> 4) & 1;
            s->mult = v & 15;
            break;
        case 0x40:
            s->ksl = (v >> 6) & 3, s->tl = v & 0x3f;
            update_ksl(o, s);
            break;
        case 0x60:
            s->ar = (v >> 4) & 15, s->dr = v & 15;
            break;
        case 0x80:
            s->sl = (v >> 4) & 15, s->rr = v & 15;
            if (s->sl == 15)
                s->sl = 0x1f;
            break;
        }
        return;
    }
    if (hi == 0xe0 && lo < 0x16) {
        int si = SLOT_OF[lo];
        if (si >= 0)
            o->slot[si].ws = o->wse ? v & 3 : 0;
        return;
    }
    if (reg >= 0xa0 && reg <= 0xa8) {
        int ch = reg - 0xa0;
        o->ch[ch].fnum = (uint16_t)((o->ch[ch].fnum & 0x300) | v);
        update_channel_freq(o, ch);
        return;
    }
    if (reg >= 0xb0 && reg <= 0xb8) {
        int ch = reg - 0xb0;
        o->ch[ch].fnum = (uint16_t)((o->ch[ch].fnum & 0xff) | (v & 3) << 8);
        o->ch[ch].block = (v >> 2) & 7;
        update_channel_freq(o, ch);
        int on = (v >> 5) & 1;
        key(&o->slot[CH_MOD[ch]], 1, on);
        key(&o->slot[CH_MOD[ch] + 3], 1, on);
        return;
    }
    if (reg >= 0xc0 && reg <= 0xc8) {
        int ch = reg - 0xc0;
        o->ch[ch].fb = (v >> 1) & 7;
        o->ch[ch].cnt = v & 1;
        connect(o, ch);
        return;
    }
}

static void envelope(tfx_opl2 *o, tfx_opl2_slot *s) {
    uint32_t out = s->eg_rout + ((uint32_t)s->tl << 2) + (s->eg_ksl >> KSL_SHIFT[s->ksl]) + (s->am ? o->tremolo : 0);
    s->eg_out = (uint16_t)(out > 0x1ff ? 0x1ff : out);
    int reset = 0;
    uint8_t reg_rate = 0;
    if (s->key && s->eg_gen == EG_RELEASE) {
        reset = 1;
        reg_rate = s->ar;
    } else {
        switch (s->eg_gen) {
        case EG_ATTACK: reg_rate = s->ar; break;
        case EG_DECAY: reg_rate = s->dr; break;
        case EG_SUSTAIN: reg_rate = s->egt ? 0 : s->rr; break;
        default: reg_rate = s->rr; break;
        }
    }
    s->pg_reset = (uint8_t)reset;
    uint8_t ks = (uint8_t)(o->ch[s->channel].ksv >> ((s->ksr ^ 1) << 1));
    int nonzero = reg_rate != 0;
    int rate = ks + (reg_rate << 2), rate_hi = rate >> 2, rate_lo = rate & 3;
    if (rate_hi & 0x10)
        rate_hi = 15;
    int eg_shift = rate_hi + o->eg_add, shift = 0;
    if (nonzero) {
        if (rate_hi < 12) {
            if (o->eg_state) {
                if (eg_shift == 12)
                    shift = 1;
                else if (eg_shift == 13)
                    shift = (rate_lo >> 1) & 1;
                else if (eg_shift == 14)
                    shift = rate_lo & 1;
            }
        } else {
            shift = (rate_hi & 3) + EG_INCSTEP[rate_lo][o->eg_timer_lo];
            if (shift & 4)
                shift = 3;
            if (!shift)
                shift = o->eg_state;
        }
    }
    uint16_t rout = s->eg_rout;
    int inc = 0, off = (s->eg_rout & 0x1f8) == 0x1f8;
    if (reset && rate_hi == 15)
        rout = 0; /* the attack at the top rate is instant */
    if (s->eg_gen != EG_ATTACK && !reset && off)
        rout = 0x1ff;
    switch (s->eg_gen) {
    case EG_ATTACK:
        if (!s->eg_rout)
            s->eg_gen = EG_DECAY;
        else if (s->key && shift > 0 && rate_hi != 15)
            inc = (-(int)s->eg_rout - 1) >> (4 - shift); /* towards 0 by a fraction of the distance */
        break;
    case EG_DECAY:
        if ((s->eg_rout >> 4) == s->sl)
            s->eg_gen = EG_SUSTAIN;
        else if (!off && !reset && shift > 0)
            inc = 1 << (shift - 1);
        break;
    default:
        if (!off && !reset && shift > 0)
            inc = 1 << (shift - 1);
        break;
    }
    s->eg_rout = (uint16_t)((rout + inc) & 0x1ff);
    if (reset)
        s->eg_gen = EG_ATTACK;
    if (!s->key)
        s->eg_gen = EG_RELEASE;
}

static void phase(tfx_opl2 *o, tfx_opl2_slot *s, int num) {
    tfx_opl2_channel *c = &o->ch[s->channel];
    int fnum = c->fnum;
    if (s->vib) {
        int range = (fnum >> 7) & 7;
        uint8_t vp = o->vibpos;
        if (!(vp & 3))
            range = 0;
        else if (vp & 1)
            range >>= 1;
        range >>= o->vib_depth ? 0 : 1;
        if (vp & 4)
            range = -range;
        fnum += range;
    }
    uint32_t base = ((uint32_t)fnum << c->block) >> 1;
    uint16_t ph = (uint16_t)(s->pg_phase >> 9);
    if (s->pg_reset)
        s->pg_phase = 0;
    s->pg_phase += (base * MULT_X2[s->mult]) >> 1;
    uint32_t noise = o->noise;
    s->pg_out = ph;
    if (num == 13) { /* HH's phase bits feed the rhythm's */
        o->rm_hh2 = (ph >> 2) & 1, o->rm_hh3 = (ph >> 3) & 1;
        o->rm_hh7 = (ph >> 7) & 1, o->rm_hh8 = (ph >> 8) & 1;
    }
    if (num == 17 && o->rhy) {
        o->rm_tc3 = (ph >> 3) & 1, o->rm_tc5 = (ph >> 5) & 1;
    }
    if (o->rhy) {
        uint16_t x = (uint16_t)((o->rm_hh2 ^ o->rm_hh7) | (o->rm_hh3 ^ o->rm_tc5) | (o->rm_tc3 ^ o->rm_tc5));
        if (num == 13)
            s->pg_out = (uint16_t)(x << 9 | ((x ^ (noise & 1)) ? 0xd0 : 0x34));
        else if (num == 16)
            s->pg_out = (uint16_t)((uint32_t)o->rm_hh8 << 9 | (uint32_t)(o->rm_hh8 ^ (noise & 1)) << 8);
        else if (num == 17)
            s->pg_out = (uint16_t)(x << 9 | 0x80);
    }
}

int16_t tfx_opl2_sample(tfx_opl2 *o) {
    for (int i = 0; i < 18; i++) {
        tfx_opl2_slot *s = &o->slot[i];
        tfx_opl2_channel *c = &o->ch[s->channel];
        /* feedback (the modulators only: a carrier's fb never reaches it) */
        if (i == CH_MOD[s->channel] && c->fb)
            s->fbmod = (s->prout + s->out) >> (9 - c->fb);
        else
            s->fbmod = 0;
        s->prout = s->out;
        envelope(o, s);
        phase(o, s, i);
        s->out = wave(s->ws, (uint16_t)(s->pg_out + *s->mod), s->eg_out);
    }
    int32_t mix = 0;
    for (int ch = 0; ch < 9; ch++) {
        tfx_opl2_slot *m = &o->slot[CH_MOD[ch]], *c = &o->slot[CH_MOD[ch] + 3];
        if (o->rhy && ch == 6)
            mix += 2 * c->out; /* BD */
        else if (o->rhy && ch >= 7)
            mix += 2 * (m->out + c->out); /* HH + SD, TOM + CY */
        else
            mix += o->ch[ch].cnt ? m->out + c->out : c->out;
    }
    /* the chip's counters */
    if ((o->timer & 0x3f) == 0x3f)
        o->tremolopos = (uint8_t)((o->tremolopos + 1) % 210);
    {
        int sh = o->am_depth ? 2 : 4;
        o->tremolo = (uint8_t)((o->tremolopos < 105 ? o->tremolopos : 210 - o->tremolopos) >> sh);
    }
    if ((o->timer & 0x3ff) == 0x3ff)
        o->vibpos = (uint8_t)((o->vibpos + 1) & 7);
    o->timer++;
    if (o->eg_state) {
        int shift = 0;
        while (shift < 36 && ((o->eg_timer >> shift) & 1) == 0)
            shift++;
        o->eg_add = (uint8_t)(shift > 12 ? 0 : shift + 1);
        o->eg_timer_lo = (uint8_t)(o->eg_timer & 3);
    }
    if (o->eg_timerrem || o->eg_state) {
        if (o->eg_timer == 0xfffffffffull) {
            o->eg_timer = 0;
            o->eg_timerrem = 1;
        } else {
            o->eg_timer++;
            o->eg_timerrem = 0;
        }
    }
    o->eg_state ^= 1;
    uint32_t n = o->noise, bit = ((n >> 14) ^ n) & 1;
    o->noise = (n >> 1) | (bit << 22);
    if (mix > 32767)
        mix = 32767;
    if (mix < -32768)
        mix = -32768;
    return (int16_t)mix;
}
