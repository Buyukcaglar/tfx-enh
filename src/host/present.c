#include "host/present.h"

static tfx_rect centred(int w, int h, int rw, int rh) {
    tfx_rect r = {(w - rw) / 2, (h - rh) / 2, rw, rh};
    return r;
}

/* the largest rectangle of shape aw:ah inside w x h */
static tfx_rect shaped(int w, int h, int aw, int ah) {
    long long rw = w, rh = (long long)w * ah / aw;
    if (rh > h)
        rh = h, rw = (long long)h * aw / ah;
    return centred(w, h, (int)rw, (int)rh);
}

tfx_rect tfx_present_fit(int w, int h, tfx_scale_mode mode) {
    if (w <= 0 || h <= 0) {
        tfx_rect none = {0, 0, 0, 0};
        return none;
    }
    switch (mode) {
    case TFX_SCALE_STRETCH:
        return centred(w, h, w, h);
    case TFX_SCALE_SQUARE:
        return shaped(w, h, TFX_FRAME_W, TFX_FRAME_H);
    case TFX_SCALE_INTEGER: {
        int k = w / 320 < h / 240 ? w / 320 : h / 240;
        if (k >= 1)
            return centred(w, h, 320 * k, 240 * k);
        return shaped(w, h, 4, 3);
    }
    default:
        return shaped(w, h, 4, 3);
    }
}

void tfx_present_palette(const uint8_t dac[768], uint8_t rgb[768]) {
    for (int i = 0; i < 768; i++) {
        unsigned v = dac[i] & 0x3fu;
        rgb[i] = (uint8_t)(v << 2 | v >> 4);
    }
}

void tfx_present_mouse(tfx_rect r, int cx, int cy, uint16_t *x, uint16_t *y) {
    long long px = r.w > 0 ? (long long)(cx - r.x) * 640 / r.w : 0;
    long long py = r.h > 0 ? (long long)(cy - r.y) * 200 / r.h : 0;
    *x = (uint16_t)(px < 0 ? 0 : px > 639 ? 639 : px);
    *y = (uint16_t)(py < 0 ? 0 : py > 199 ? 199 : py);
}

void tfx_present_expand(const uint8_t *frame, const uint8_t dac[768], uint32_t *out) {
    uint8_t rgb[768];
    uint32_t lut[256];
    tfx_present_palette(dac, rgb);
    for (int i = 0; i < 256; i++)
        lut[i] = (uint32_t)rgb[3 * i] << 16 | (uint32_t)rgb[3 * i + 1] << 8 | rgb[3 * i + 2];
    for (int i = 0; i < TFX_FRAME_W * TFX_FRAME_H; i++)
        out[i] = lut[frame[i]];
}
