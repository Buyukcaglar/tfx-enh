/* The keyboard's bytes for a native host (W04): a key's make or break as
 * the AT keyboard controller gave them at port 0x60 (scan code set 1,
 * translated), which is what the game's INT 9 handler reads (EX18,
 * platform/keyboard.h) and tfx_key takes.
 *
 * A host names a key by its set-1 code (0x01 .. 0x7f) and whether the
 * keyboard sent it after 0xE0 (the grey keys, right Ctrl / Alt, the
 * keypad's Enter and /). Three keys are not one code: Pause (E1 1D 45 E1
 * 9D C5 on the press, nothing on the release), Print Screen (E0 2A E0 37,
 * released E0 B7 E0 AA) and Num Lock, which Windows reports with the
 * extended flag while the keyboard sends it plain (and Pause plain). */
#ifndef TFX_HOST_SCANCODE_H
#define TFX_HOST_SCANCODE_H

#include <stdint.h>

enum {
    TFX_KEY_EXTENDED = 0x100, /* after 0xE0 */
    TFX_KEY_PAUSE = 0x200,
    TFX_KEY_PRINT = 0x201
};

/* The bytes for key (a set-1 code, | TFX_KEY_EXTENDED, or a special)
 * made or released; returns how many (0 .. 6). */
int tfx_key_bytes(int key, int released, uint8_t out[6]);

/* Windows' scan code (lParam bits 16 .. 23) and extended flag (bit 24)
 * as a key above: Num Lock and Pause swapped back; 0 for none. */
int tfx_key_from_windows(unsigned scan, int extended, int is_pause, int is_print);

/* The keys held, so focus loss can release them all. */
typedef struct tfx_keys_held {
    uint8_t held[2][128]; /* [extended][code] */
} tfx_keys_held;

/* Records a make or break; returns 0 when a break is for a key not held
 * (dropped: the game saw no make) */
int tfx_keys_note(tfx_keys_held *k, int key, int released);

/* Every held key's break, appended to out (cap bytes); returns how many
 * bytes and clears the record. */
int tfx_keys_release_all(tfx_keys_held *k, uint8_t *out, int cap);

#endif
