/* The macOS host's audio output (M01): the default output device through
 * Core Audio's default output unit, pulling the mixer's frames (48 kHz
 * float stereo; the unit converts to the device). The MPU-401's music
 * (M05) plays on Apple's DLS synthesizer (General MIDI, with GS bank
 * selects), given each message at its frame of the buffer the output
 * renders and rendered into it. */
#ifndef TFX_MAC_OUTPUT_H
#define TFX_MAC_OUTPUT_H

#include <stdint.h>

#include "mixer.h"

#define TFX_MAC_OUTPUT_RATE 48000

typedef struct tfx_mac_output tfx_mac_output;

/* started on the mixer, now the host's clock in microseconds; NULL when no
 * device opens (the game runs silent) */
tfx_mac_output *tfx_mac_output_open(tfx_mac_mixer *m, uint64_t (*now_us)(void));
void tfx_mac_output_close(tfx_mac_output *o);
/* 1 when the MIDI synthesizer runs (else the MPU-401's music is silent) */
int tfx_mac_output_synth(const tfx_mac_output *o);

#endif
