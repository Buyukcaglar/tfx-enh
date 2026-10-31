/* A game controller as the game port's joystick (M04): the controller's
 * sticks, trigger, d-pad and face buttons as the one-shots' counts and
 * the buttons tfx_stick takes (include/tfx/core.h).
 *
 *   axis 1, 2   the left stick (x right, y down): 400 at the centre,
 *               50 .. 750 across, as a typical PC stick reads
 *   axis 3      the right stick's x (a rudder: ThrustMaster's)
 *   axis 4      the right trigger as a throttle (750 released, 50 full:
 *               a FlightStick Pro's throttle reads lower forward), or
 *               while the d-pad is held a ThrustMaster's hat (up 0,
 *               left 1/4, down 1/2, right 3/4 of the centre's 750)
 *   buttons     A, B, X, Y as A1 (fire), A2, B1, B2
 *
 * The game's CONFIG calibrates whatever the ranges are (M06). */
#ifndef TFX_MAC_GAMEPAD_H
#define TFX_MAC_GAMEPAD_H

#include <stdint.h>

enum { TFX_PAD_UP = 1, TFX_PAD_DOWN = 2, TFX_PAD_LEFT = 4, TFX_PAD_RIGHT = 8 };

typedef struct tfx_mac_pad {
    float lx, ly, rx; /* -1 .. 1, y up */
    float trigger;    /* 0 .. 1 */
    unsigned dpad;    /* TFX_PAD_* */
    unsigned buttons; /* bit 0 A, 1 B, 2 X, 3 Y */
} tfx_mac_pad;

void tfx_mac_pad_counts(const tfx_mac_pad *p, uint16_t count[4], uint8_t *buttons);

#endif
