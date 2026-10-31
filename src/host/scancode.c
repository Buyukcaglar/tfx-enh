#include "host/scancode.h"

#include <string.h>

int tfx_key_bytes(int key, int released, uint8_t out[6]) {
    if (key == TFX_KEY_PAUSE) {
        static const uint8_t pause[6] = {0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5};
        if (released)
            return 0;
        memcpy(out, pause, 6);
        return 6;
    }
    if (key == TFX_KEY_PRINT) {
        static const uint8_t make[4] = {0xe0, 0x2a, 0xe0, 0x37}, brk[4] = {0xe0, 0xb7, 0xe0, 0xaa};
        memcpy(out, released ? brk : make, 4);
        return 4;
    }
    int code = key & 0x7f, n = 0;
    if (!code)
        return 0;
    if (key & TFX_KEY_EXTENDED)
        out[n++] = 0xe0;
    out[n++] = (uint8_t)(code | (released ? 0x80 : 0));
    return n;
}

int tfx_key_from_windows(unsigned scan, int extended, int is_pause, int is_print) {
    if (is_pause)
        return TFX_KEY_PAUSE;
    if (is_print)
        return TFX_KEY_PRINT;
    scan &= 0xff;
    if (!scan || scan >= 0x80)
        return 0;
    if (scan == 0x45) /* Num Lock: Windows flags it extended, the keyboard does not */
        return 0x45;
    return (int)scan | (extended ? TFX_KEY_EXTENDED : 0);
}

int tfx_keys_note(tfx_keys_held *k, int key, int released) {
    if (key & 0x200)
        return 1;
    int e = (key & TFX_KEY_EXTENDED) != 0, code = key & 0x7f;
    if (released) {
        if (!k->held[e][code])
            return 0;
        k->held[e][code] = 0;
    } else
        k->held[e][code] = 1;
    return 1;
}

int tfx_keys_release_all(tfx_keys_held *k, uint8_t *out, int cap) {
    int n = 0;
    for (int e = 0; e < 2; e++)
        for (int code = 1; code < 128; code++)
            if (k->held[e][code]) {
                uint8_t b[6];
                int m = tfx_key_bytes(code | (e ? TFX_KEY_EXTENDED : 0), 1, b);
                if (n + m > cap)
                    return n;
                memcpy(out + n, b, (size_t)m);
                n += m;
                k->held[e][code] = 0;
            }
    return n;
}
