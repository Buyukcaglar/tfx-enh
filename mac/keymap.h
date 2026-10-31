/* The macOS host's keyboard (M01): a Mac key (its virtual key code, the
 * kVK_ numbers of HIToolbox's Events.h, by position on the keyboard) as
 * the bytes the PC keyboard put at port 0x60 (scan code set 1: the make
 * code, 0x80 set on the release, 0xe0 before the extended keys), which is
 * what tfx_key takes. The keys the PC lacks (Command, Function, the JIS
 * keys) give nothing. */
#ifndef TFX_MAC_KEYMAP_H
#define TFX_MAC_KEYMAP_H

#include <stdint.h>

/* the bytes for the key pressed (down) or released into out; their count
 * (0: no PC key) */
int tfx_mac_scan(uint16_t keycode, int down, uint8_t out[2]);

/* the key is one of the modifiers (Shift, Control, Option, Caps Lock), whose
 * presses AppKit reports as flag changes */
int tfx_mac_modifier(uint16_t keycode);

#endif
