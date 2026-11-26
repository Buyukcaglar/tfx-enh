#include "present.h"

#include <math.h>

tfx_mac_rect tfx_mac_fit(double w, double h, int mode) {
    double fw, fh;
    if (mode == TFX_MAC_FIT_STRETCH) {
        fw = floor(w), fh = floor(h);
    } else if (mode == TFX_MAC_FIT_SQUARE) {
        if (w * 200 > h * 320)
            fh = floor(h), fw = floor(h * 320 / 200);
        else
            fw = floor(w), fh = floor(w * 200 / 320);
    } else if (mode == TFX_MAC_FIT_INTEGER && w >= 320 && h >= 240) {
        double k = floor(fmin(w / 320, h / 240));
        fw = 320 * k, fh = 240 * k;
    } else if (w * 3 > h * 4) {
        fh = floor(h), fw = floor(h * 4 / 3);
    } else {
        fw = floor(w), fh = floor(w * 3 / 4);
    }
    return (tfx_mac_rect){floor((w - fw) / 2), floor((h - fh) / 2), fw, fh};
}
