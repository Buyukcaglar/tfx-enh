/* The frame's place in the window (M03): the original's 320 x 200 frame
 * shown at its 4:3 display aspect (each pixel 1.2 times as tall as wide),
 * as large as fits and centred, or with integer scaling the largest
 * whole multiple k of 320 x 240 that fits: every column k pixels wide,
 * rows 1.2 k tall (all alike when k is a multiple of 5, else differing by
 * one pixel). Sizes in the view's backing pixels. Portable C. */
#ifndef TFX_MAC_PRESENT_H
#define TFX_MAC_PRESENT_H

typedef struct tfx_mac_rect {
    double x, y, w, h;
} tfx_mac_rect;

/* the frame's rectangle in a w x h view */
tfx_mac_rect tfx_mac_fit(double w, double h, int integer);

/* a 6-bit DAC value widened to 8 bits (0 -> 0, 63 -> 255, as a VGA's DAC
 * spans its range) */
static inline unsigned char tfx_mac_dac8(unsigned char v) {
    v &= 63;
    return (unsigned char)(v << 2 | v >> 4);
}

#endif
