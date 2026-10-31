#include "music.h"

#include <string.h>

void tfx_mac_music_init(tfx_mac_music *m, uint32_t rate) {
    memset(m, 0, sizeof *m);
    m->rate = rate;
    m->gain = 0.5f;
    m->opl_step = (double)TFX_OPL2_RATE / rate;
    m->opl_at = 1.0; /* (the first frame takes the chip's first sample) */
    tfx_opl2_init(&m->opl);
}

int tfx_mac_music_port(tfx_mac_music *m, int64_t frame, uint16_t port, uint8_t value) {
    if (m->head - m->tail >= TFX_MAC_MUSIC_EVENTS) {
        m->dropped++;
        return 0;
    }
    if (frame < m->last)
        frame = m->last;
    m->last = frame;
    uint32_t i = m->head++ % TFX_MAC_MUSIC_EVENTS;
    m->ev[i].frame = frame, m->ev[i].port = port, m->ev[i].value = value;
    return 1;
}

static void send(tfx_mac_music *m, const uint8_t *msg, int n, uint32_t offset) {
    m->midi_messages++;
    if (m->synth.midi)
        m->synth.midi(m->synth.ctx, msg, n, offset);
}

static void apply(tfx_mac_music *m, uint16_t port, uint8_t v, uint32_t offset) {
    switch (port) {
    case 0x388:
    case 0x389:
        tfx_opl2_port(&m->opl, port, v);
        m->opl_on = 1;
        m->opl_writes += port == 0x389;
        break;
    case 0x330: {
        int n = tfx_midi_byte(&m->midi, v, m->msg);
        if (n)
            send(m, m->msg, n, offset);
        break;
    }
    case 0x331:
        if (v == 0xff) { /* reset: all notes off, controllers reset */
            memset(&m->midi, 0, sizeof m->midi);
            for (uint8_t ch = 0; ch < 16; ch++) {
                send(m, (const uint8_t[]){(uint8_t)(0xb0 | ch), 123, 0}, 3, offset);
                send(m, (const uint8_t[]){(uint8_t)(0xb0 | ch), 121, 0}, 3, offset);
            }
        }
        break;
    }
}

/* the chip's output at the next output frame: linear between its samples */
static float opl_frame(tfx_mac_music *m) {
    m->opl_at += m->opl_step;
    while (m->opl_at >= 1.0) {
        m->opl_prev = m->opl_cur;
        tfx_opl2_render(&m->opl, &m->opl_cur, 1);
        m->opl_at -= 1.0;
    }
    return m->opl_prev + (m->opl_cur - m->opl_prev) * (float)m->opl_at;
}

void tfx_mac_music_render(tfx_mac_music *m, float *out, uint32_t frames, int64_t f0) {
    for (uint32_t i = 0; i < frames; i++) {
        while (m->tail != m->head && m->ev[m->tail % TFX_MAC_MUSIC_EVENTS].frame <= f0 + (int64_t)i) {
            uint32_t k = m->tail++ % TFX_MAC_MUSIC_EVENTS;
            apply(m, m->ev[k].port, m->ev[k].value, i);
        }
        if (m->opl_on) {
            float x = opl_frame(m) * m->gain;
            out[2 * i] += x;
            out[2 * i + 1] += x;
        }
    }
    if (m->synth.render)
        m->synth.render(m->synth.ctx, out, frames);
}
