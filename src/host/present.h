/* The frame's presentation for a native host (W03): the original's
 * 320 x 200 indexed picture (mode 13h / mode X, shown by a VGA on a 4:3
 * monitor: each pixel 1.2 times as tall as wide) and its DAC (256 x 3
 * 6-bit values) as a native window shows them. Pure functions of the
 * frame; resizing or a display's refresh never reaches the game. */
#ifndef TFX_HOST_PRESENT_H
#define TFX_HOST_PRESENT_H

#include <stdint.h>

enum { TFX_FRAME_W = 320, TFX_FRAME_H = 200 };

typedef enum tfx_scale_mode {
    TFX_SCALE_ASPECT = 0,  /* the largest 4:3 rectangle (the monitor's shape) */
    TFX_SCALE_INTEGER = 1, /* whole multiples of 320 x 240 equivalents when they fit, else as ASPECT */
    TFX_SCALE_STRETCH = 2, /* the whole client area */
    TFX_SCALE_SQUARE = 3   /* square pixels: the largest 16:10 rectangle */
} tfx_scale_mode;

typedef struct tfx_rect {
    int x, y, w, h;
} tfx_rect;

/* Where the frame goes in a w x h client area (centred, the rest black). */
tfx_rect tfx_present_fit(int w, int h, tfx_scale_mode mode);

/* The DAC as 8-bit RGB (the VGA's 6-bit values scaled to 0 .. 255). */
void tfx_present_palette(const uint8_t dac[768], uint8_t rgb[768]);

/* A client-area position as the mouse driver's (x 0 .. 639, y 0 .. 199 in
 * mode 13h), clamped to the picture. */
void tfx_present_mouse(tfx_rect r, int cx, int cy, uint16_t *x, uint16_t *y);

/* The frame as 32-bit 0x00RRGGBB pixels (for a 32-bit surface or a
 * screenshot). */
void tfx_present_expand(const uint8_t *frame, const uint8_t dac[768], uint32_t *out);

#endif
