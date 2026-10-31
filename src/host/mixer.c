#include "host/mixer.h"

#include <stdlib.h>
#include <string.h>

tfx_mixer *tfx_mixer_new(int rate) {
    tfx_mixer *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->ev = calloc(TFX_MIXER_EVENTS, sizeof *m->ev);
    if (!m->ev) {
        free(m);
        return NULL;
    }
    m->rate = rate;
    tfx_opl2_reset(&m->opl);
    m->vol_music = m->vol_fx = m->vol_cd = 256;
    return m;
}

static void drop_voice(tfx_mixer_voice *v) {
    free(v->data);
    memset(v, 0, sizeof *v);
}

void tfx_mixer_clear(tfx_mixer *m) {
    while (m->tail != m->head) {
        free(m->ev[m->tail % TFX_MIXER_EVENTS].data);
        m->tail++;
    }
    for (int i = 0; i < TFX_MIXER_VOICES; i++)
        drop_voice(&m->voice[i]);
    tfx_opl2_reset(&m->opl);
    m->opl_on = 0;
    tfx_cdi_stop(&m->cd);
    m->cd_buf_at = m->cd_buf_n = 0;
    m->midi_status = 0, m->midi_have = 0, m->in_sysex = 0;
}

void tfx_mixer_free(tfx_mixer *m) {
    if (!m)
        return;
    tfx_mixer_clear(m);
    free(m->ev);
    free(m);
}

void tfx_mixer_set_midi(tfx_mixer *m, void (*midi)(void *ctx, const uint8_t *msg, size_t n), void *ctx) {
    m->midi = midi, m->midi_ctx = ctx;
}

void tfx_mixer_set_cd(tfx_mixer *m, const tfx_cd_image *img,
                      size_t (*read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n), void *ctx) {
    m->cd_img = img, m->cd_read = read, m->cd_ctx = ctx;
    tfx_cdi_stop(&m->cd);
}

static tfx_mixer_event *push(tfx_mixer *m) {
    if (m->head - m->tail >= TFX_MIXER_EVENTS) {
        m->dropped++;
        return NULL;
    }
    tfx_mixer_event *e = &m->ev[m->head % TFX_MIXER_EVENTS];
    memset(e, 0, sizeof *e);
    return e;
}

void tfx_mixer_port(tfx_mixer *m, uint64_t at, uint16_t port, uint8_t value) {
    tfx_mixer_event *e = push(m);
    if (!e)
        return;
    e->at = at, e->kind = 0, e->port = port, e->value = value;
    m->head++;
}

void tfx_mixer_pcm(tfx_mixer *m, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate) {
    if (voice < 0 || voice >= TFX_MIXER_VOICES)
        return;
    tfx_mixer_event *e = push(m);
    if (!e)
        return;
    e->at = at, e->kind = 1, e->port = (uint16_t)voice, e->n = n, e->rate = rate;
    if (n) {
        e->data = malloc(n);
        if (!e->data)
            return;
        memcpy(e->data, data, n);
    }
    m->head++;
}

void tfx_mixer_cd_play(tfx_mixer *m, uint64_t at, int32_t from, int32_t n) {
    tfx_mixer_event *e = push(m);
    if (!e)
        return;
    e->at = at, e->kind = 2, e->from = from, e->n = (uint32_t)n;
    m->head++;
}

void tfx_mixer_cd_stop(tfx_mixer *m, uint64_t at) {
    tfx_mixer_event *e = push(m);
    if (!e)
        return;
    e->at = at, e->kind = 3;
    m->head++;
}

int tfx_mixer_cd_heard(const tfx_mixer *m) {
    /* a play or stop still queued decides what the drive reports */
    for (unsigned i = m->head; i != m->tail; i--) {
        const tfx_mixer_event *e = &m->ev[(i - 1) % TFX_MIXER_EVENTS];
        if (e->kind == 2)
            return m->cd_img && e->n ? tfx_cdi_track_at(m->cd_img, e->from) : 0;
        if (e->kind == 3)
            return 0;
    }
    return tfx_cdi_heard(&m->cd);
}

static void midi_flush(tfx_mixer *m, const uint8_t *msg, size_t n) {
    if (m->midi)
        m->midi(m->midi_ctx, msg, n);
}

static uint8_t midi_length(uint8_t status) {
    switch (status & 0xf0) {
    case 0xc0:
    case 0xd0:
        return 2;
    case 0xf0:
        return status == 0xf1 || status == 0xf3 ? 2 : status == 0xf2 ? 3 : 1;
    default:
        return 3;
    }
}

static void midi_byte(tfx_mixer *m, uint8_t b) {
    if (b >= 0xf8) { /* real time: alone, anywhere */
        midi_flush(m, &b, 1);
        return;
    }
    if (m->in_sysex) {
        if (b == 0xf7) {
            if (m->sysex_n < sizeof m->sysex)
                m->sysex[m->sysex_n++] = b;
            midi_flush(m, m->sysex, m->sysex_n);
            m->in_sysex = 0;
            return;
        }
        if (!(b & 0x80)) {
            if (m->sysex_n < sizeof m->sysex)
                m->sysex[m->sysex_n++] = b;
            return;
        }
        m->in_sysex = 0; /* a status byte ends it unfinished: dropped */
    }
    if (b == 0xf0) {
        m->in_sysex = 1;
        m->sysex[0] = b;
        m->sysex_n = 1;
        m->midi_status = 0;
        return;
    }
    if (b & 0x80) {
        m->midi_status = b < 0xf0 ? b : 0;
        m->midi_msg[0] = b;
        m->midi_have = 1;
        m->midi_need = midi_length(b);
        if (m->midi_need == 1) {
            midi_flush(m, m->midi_msg, 1);
            m->midi_have = 0;
        }
        return;
    }
    if (!m->midi_have) { /* running status */
        if (!m->midi_status)
            return;
        m->midi_msg[0] = m->midi_status;
        m->midi_have = 1;
        m->midi_need = midi_length(m->midi_status);
    }
    m->midi_msg[m->midi_have++] = b;
    if (m->midi_have == m->midi_need) {
        midi_flush(m, m->midi_msg, m->midi_need);
        m->midi_have = 0;
    }
}

static void midi_reset(tfx_mixer *m) {
    for (uint8_t ch = 0; ch < 16; ch++) {
        uint8_t off[3] = {(uint8_t)(0xb0 | ch), 123, 0}, sound[3] = {(uint8_t)(0xb0 | ch), 120, 0};
        midi_flush(m, off, 3);
        midi_flush(m, sound, 3);
    }
    m->midi_status = 0, m->midi_have = 0, m->in_sysex = 0;
}

static void apply(tfx_mixer *m, tfx_mixer_event *e) {
    switch (e->kind) {
    case 0:
        if (e->port == 0x388 || e->port == 0x389) {
            tfx_opl2_port(&m->opl, e->port, e->value);
            m->opl_on = 1;
        } else if (e->port == 0x330) {
            midi_byte(m, e->value);
        } else if (e->port == 0x331 && e->value == 0xff) {
            midi_reset(m);
        }
        break;
    case 1: {
        tfx_mixer_voice *v = &m->voice[e->port];
        drop_voice(v);
        if (e->n && e->rate) {
            v->data = e->data, v->n = e->n, v->rate = e->rate;
            e->data = NULL;
        }
        break;
    }
    case 2:
        if (m->cd_img)
            tfx_cdi_play(&m->cd, m->cd_img, e->from, (int32_t)e->n);
        /* one frame pending: the first output frame is the track's first
         * (prev, at fraction 0), at the event's own output sample */
        m->cd_pos = 1ull << 32, m->cd_buf_at = m->cd_buf_n = 0, m->cd_tail = 0;
        m->cd_next[0] = m->cd_next[1] = 0;
        break;
    case 3:
        tfx_cdi_stop(&m->cd);
        m->cd_buf_at = m->cd_buf_n = 0, m->cd_tail = 0;
        break;
    }
    free(e->data);
    e->data = NULL;
}

static int16_t clamp16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void tfx_mixer_render(tfx_mixer *m, int16_t *out, int frames, uint64_t t0) {
    const uint64_t opl_step = ((uint64_t)TFX_OPL2_RATE << 32) / (uint64_t)m->rate;
    const uint64_t cd_step = ((uint64_t)44100 << 32) / (uint64_t)m->rate;
    for (int i = 0; i < frames; i++) {
        uint64_t t = t0 + (uint64_t)i * 1000000u / (uint64_t)m->rate;
        while (m->tail != m->head && m->ev[m->tail % TFX_MIXER_EVENTS].at <= t) {
            apply(m, &m->ev[m->tail % TFX_MIXER_EVENTS]);
            m->tail++;
        }
        int32_t l = 0, r = 0;
        /* the OPL2, mono: its own rate, interpolated */
        if (m->opl_on) {
            m->opl_pos += opl_step;
            while (m->opl_pos >= (1ull << 32)) {
                m->opl_pos -= 1ull << 32;
                m->opl_prev = m->opl_next;
                m->opl_next = tfx_opl2_sample(&m->opl);
            }
            uint32_t frac = (uint32_t)(m->opl_pos >> 16);
            int32_t s = m->opl_prev + (int32_t)(((int64_t)(m->opl_next - m->opl_prev) * frac) >> 16);
            s = s * m->vol_music / 256;
            l += s, r += s;
        }
        /* the samples */
        for (int k = 0; k < TFX_MIXER_VOICES; k++) {
            tfx_mixer_voice *v = &m->voice[k];
            if (!v->data)
                continue;
            uint32_t idx = (uint32_t)(v->pos >> 32);
            if (idx >= v->n) {
                drop_voice(v);
                continue;
            }
            int32_t a = (int32_t)v->data[idx] - 128;
            int32_t b = idx + 1 < v->n ? (int32_t)v->data[idx + 1] - 128 : a;
            uint32_t frac = (uint32_t)(v->pos >> 16) & 0xffff;
            int32_t s = (a * 256 + (((b - a) * 256 * (int32_t)frac) >> 16)) * m->vol_fx / 256;
            l += s, r += s;
            v->pos += ((uint64_t)v->rate << 32) / (uint64_t)m->rate;
        }
        /* the CD: 44100 Hz stereo, interpolated */
        if (m->cd_img && (m->cd.at < m->cd.end || m->cd_buf_at < m->cd_buf_n || m->cd_tail)) {
            m->cd_pos += cd_step;
            while (m->cd_pos >= (1ull << 32)) {
                m->cd_pos -= 1ull << 32;
                m->cd_prev[0] = m->cd_next[0], m->cd_prev[1] = m->cd_next[1];
                if (m->cd_buf_at == m->cd_buf_n) { /* a sector at a time */
                    m->cd_buf_n = tfx_cdi_render(&m->cd, m->cd_buf, 588, m->cd_read, m->cd_ctx);
                    m->cd_buf_at = 0;
                }
                if (m->cd_buf_at < m->cd_buf_n) {
                    m->cd_next[0] = m->cd_buf[2 * m->cd_buf_at], m->cd_next[1] = m->cd_buf[2 * m->cd_buf_at + 1];
                    m->cd_buf_at++;
                    m->cd_tail = 1;
                } else
                    m->cd_next[0] = m->cd_next[1] = 0, m->cd_tail = 0;
            }
            uint32_t frac = (uint32_t)(m->cd_pos >> 16);
            for (int c = 0; c < 2; c++) {
                int32_t s = m->cd_prev[c] + (int32_t)(((int64_t)(m->cd_next[c] - m->cd_prev[c]) * frac) >> 16);
                s = s * m->vol_cd / 256;
                if (c)
                    r += s;
                else
                    l += s;
            }
        }
        out[2 * i] = clamp16(l);
        out[2 * i + 1] = clamp16(r);
    }
}
