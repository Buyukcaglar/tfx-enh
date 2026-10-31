#include "midi.h"

#include <string.h>

/* data bytes after a channel status */
static int data_bytes(uint8_t s) { return (s & 0xf0) == 0xc0 || (s & 0xf0) == 0xd0 ? 1 : 2; }

/* data bytes after a system common status (0xf1 .. 0xf7) */
static int common_bytes(uint8_t s) { return s == 0xf1 || s == 0xf3 ? 1 : s == 0xf2 ? 2 : 0; }

int tfx_midi_byte(tfx_midi *m, uint8_t b, uint8_t *msg) {
    if (b >= 0xf8) { /* real time: anywhere, alone */
        msg[0] = b;
        return 1;
    }
    if (m->in_sysex && b != 0xf7 && b < 0x80) {
        if (m->sysex_n < TFX_MIDI_SYSEX - 1)
            m->sysex[m->sysex_n++] = b;
        else
            m->sysex_overflow = 1;
        return 0;
    }
    if (b == 0xf0) {
        m->in_sysex = 1, m->sysex_overflow = 0, m->sysex_n = 0;
        m->sysex[m->sysex_n++] = b;
        m->status = 0;
        return 0;
    }
    if (b == 0xf7 || (m->in_sysex && b >= 0x80)) { /* the end (or a status cutting it short) */
        int n = 0;
        if (m->in_sysex && !m->sysex_overflow) {
            m->sysex[m->sysex_n++] = 0xf7;
            memcpy(msg, m->sysex, (size_t)m->sysex_n);
            n = m->sysex_n;
        }
        m->in_sysex = 0;
        if (b == 0xf7)
            return n;
        if (n) /* the status that cut it is lost with the message returned; rare */
            return n;
    }
    if (b >= 0xf0) { /* system common: clears running status */
        m->status = 0;
        int k = common_bytes(b);
        if (!k) {
            msg[0] = b;
            return 1;
        }
        m->data[0] = b, m->have = 0, m->need = k;
        m->status = b;
        return 0;
    }
    if (b >= 0x80) {
        m->status = b, m->have = 0, m->need = data_bytes(b);
        return 0;
    }
    if (!m->status) /* data with no status: dropped */
        return 0;
    m->data[m->have++] = b;
    if (m->have < m->need)
        return 0;
    msg[0] = m->status;
    memcpy(msg + 1, m->data, (size_t)m->need);
    int n = 1 + m->need;
    m->have = 0;
    if (m->status >= 0xf0) /* system common does not run */
        m->status = 0;
    return n;
}
