/* The MPU-401's data port (0x330) as MIDI messages (M05): the bytes the
 * game sends in UART mode become whole messages, with running status,
 * system exclusive messages up to 512 bytes and real-time bytes passed
 * through as they come. Portable C. */
#ifndef TFX_MAC_MIDI_H
#define TFX_MAC_MIDI_H

#include <stdint.h>

#define TFX_MIDI_SYSEX 512

typedef struct tfx_midi {
    uint8_t status;              /* running status (0: none) */
    uint8_t data[2];
    int have, need;
    uint8_t sysex[TFX_MIDI_SYSEX];
    int sysex_n, in_sysex, sysex_overflow;
} tfx_midi;

/* feeds a byte; returns the length of a completed message copied to msg
 * (cap at least TFX_MIDI_SYSEX), else 0 */
int tfx_midi_byte(tfx_midi *m, uint8_t b, uint8_t *msg);

#endif
