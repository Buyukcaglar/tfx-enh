#include "mixer.h"

#include <stdlib.h>
#include <string.h>

void tfx_mac_mixer_init(tfx_mac_mixer *m, uint32_t rate, uint32_t latency_us) {
    memset(m, 0, sizeof *m);
    m->rate = rate;
    m->latency = (int64_t)((uint64_t)latency_us * rate / 1000000u);
    m->gain = 0.5f;
    m->cd_gain = 0.8f;
    tfx_mac_music_init(&m->music, rate);
    pthread_mutex_init(&m->lock, NULL);
}

void tfx_mac_mixer_free(tfx_mac_mixer *m) {
    for (int v = 0; v < TFX_MAC_VOICES; v++)
        for (int k = 0; k < m->voice[v].n; k++)
            free(m->voice[v].s[k].data);
    pthread_mutex_destroy(&m->lock);
}

void tfx_mac_mixer_reset(tfx_mac_mixer *m) {
    pthread_mutex_lock(&m->lock);
    for (int v = 0; v < TFX_MAC_VOICES; v++) {
        for (int k = 0; k < m->voice[v].n; k++)
            free(m->voice[v].s[k].data);
        m->voice[v].n = 0;
    }
    tfx_mac_synth synth = m->music.synth;
    for (uint8_t ch = 0; synth.midi && ch < 16; ch++) {
        synth.midi(synth.ctx, (const uint8_t[]){(uint8_t)(0xb0 | ch), 123, 0}, 3, 0);
        synth.midi(synth.ctx, (const uint8_t[]){(uint8_t)(0xb0 | ch), 121, 0}, 3, 0);
    }
    tfx_mac_music_init(&m->music, m->rate);
    m->music.synth = synth;
    pthread_mutex_unlock(&m->lock);
}

/* the voice's passed segments freed and removed */
static void compact(tfx_mac_mixer *m, int v) {
    int w = 0;
    for (int k = 0; k < m->voice[v].n; k++) {
        tfx_mac_segment *s = &m->voice[v].s[k];
        if (s->done)
            free(s->data);
        else
            m->voice[v].s[w++] = *s;
    }
    m->voice[v].n = w;
}

/* the output frame a stamp plays at (the lock held) */
static int64_t frame_at(const tfx_mac_mixer *m, uint64_t at_us) {
    if (!m->frame_us)
        return m->frame;
    int64_t d = (int64_t)(at_us - m->frame_us);
    int64_t f = m->frame + d * (int64_t)m->rate / 1000000 + m->latency;
    return f < m->frame ? m->frame : f;
}

int tfx_mac_mixer_port(tfx_mac_mixer *m, uint64_t at_us, uint16_t port, uint8_t value) {
    pthread_mutex_lock(&m->lock);
    int ok = tfx_mac_music_port(&m->music, frame_at(m, at_us), port, value);
    pthread_mutex_unlock(&m->lock);
    return ok;
}

int tfx_mac_mixer_pcm(tfx_mac_mixer *m, uint64_t at_us, int voice, const uint8_t *data, uint32_t n, uint32_t rate) {
    if (voice < 0 || voice >= TFX_MAC_VOICES || (n && (!data || !rate)))
        return 0;
    uint8_t *copy = NULL;
    if (n) {
        copy = malloc(n);
        if (!copy)
            return 0;
        memcpy(copy, data, n);
    }
    pthread_mutex_lock(&m->lock);
    compact(m, voice);
    int64_t start = frame_at(m, at_us);
    /* a segment takes over from its start: the queued ones starting later are dropped */
    int k = m->voice[voice].n;
    while (k > 0 && m->voice[voice].s[k - 1].start > start)
        free(m->voice[voice].s[--k].data);
    m->voice[voice].n = k;
    int ok = k < TFX_MAC_SEGMENTS;
    if (ok)
        m->voice[voice].s[m->voice[voice].n++] = (tfx_mac_segment){start, n, rate, copy, 0};
    pthread_mutex_unlock(&m->lock);
    if (!ok)
        free(copy);
    return ok;
}

void tfx_mac_mixer_render(tfx_mac_mixer *m, float *out, uint32_t frames, uint64_t now_us) {
    memset(out, 0, sizeof *out * 2 * frames);
    pthread_mutex_lock(&m->lock);
    int64_t f0 = m->frame;
    for (int v = 0; v < TFX_MAC_VOICES; v++) {
        int n = m->voice[v].n;
        for (uint32_t i = 0; i < frames; i++) {
            int64_t g = f0 + i;
            /* the latest segment started by g; the earlier ones are passed */
            int cur = -1;
            for (int k = 0; k < n; k++)
                if (!m->voice[v].s[k].done && m->voice[v].s[k].start <= g)
                    cur = k;
            if (cur < 0)
                continue;
            for (int k = 0; k < cur; k++)
                m->voice[v].s[k].done = 1;
            tfx_mac_segment *s = &m->voice[v].s[cur];
            uint64_t idx = (uint64_t)(g - s->start) * s->rate / m->rate;
            if (idx >= s->n) { /* ended (or a stop): nothing until the next segment */
                s->done = 1;
                continue;
            }
            float x = ((float)s->data[idx] - 128.0f) / 128.0f * m->gain;
            out[2 * i] += x;
            out[2 * i + 1] += x;
        }
    }
    tfx_mac_music_render(&m->music, out, frames, f0);
    if (m->cd)
        tfx_mac_cd_render(m->cd, out, (int)frames, (int)m->rate, m->cd_gain);
    for (uint32_t i = 0; i < 2 * frames; i++)
        out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
    m->frame = f0 + frames;
    m->frame_us = now_us;
    pthread_mutex_unlock(&m->lock);
}
