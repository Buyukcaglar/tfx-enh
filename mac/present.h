/* The frame's place in the window (M03): the original's 320 x 200 frame
 * shown at its 4:3 display aspect (each pixel 1.2 times as tall as wide),
 * as large as fits and centred, or with integer scaling the largest
 * whole multiple k of 320 x 240 that fits: every column k pixels wide,
 * rows 1.2 k tall (all alike when k is a multiple of 5, else differing by
 * one pixel). Sizes in the view's backing pixels. Portable C.
 * Stretch and square (the Windows host's --scale stretch and square,
 * launcher support, 2026-11-26): the whole view, or the frame at 320 x 200
 * (square pixels) as large as fits. */
#ifndef TFX_MAC_PRESENT_H
#define TFX_MAC_PRESENT_H

typedef struct tfx_mac_rect {
    double x, y, w, h;
} tfx_mac_rect;

enum { TFX_MAC_FIT_ASPECT = 0, TFX_MAC_FIT_INTEGER = 1, TFX_MAC_FIT_STRETCH = 2, TFX_MAC_FIT_SQUARE = 3 };

/* the frame's rectangle in a w x h view, mode one of TFX_MAC_FIT_* (0 and
 * 1: 4:3, integer scaling) */
tfx_mac_rect tfx_mac_fit(double w, double h, int mode);

/* a 6-bit DAC value widened to 8 bits (0 -> 0, 63 -> 255, as a VGA's DAC
 * spans its range) */
static inline unsigned char tfx_mac_dac8(unsigned char v) {
    v &= 63;
    return (unsigned char)(v << 2 | v >> 4);
}

#endif
