#include "gamepad.h"

static float unit(float v, float lo) { return v != v ? 0 : v < lo ? lo : v > 1 ? 1 : v; }

static uint16_t across(float v) { return (uint16_t)(400 + (int)(v * 350 + (v < 0 ? -0.5f : 0.5f))); }

void tfx_mac_pad_counts(const tfx_mac_pad *p, uint16_t count[4], uint8_t *buttons) {
    count[0] = across(unit(p->lx, -1));
    count[1] = across(-unit(p->ly, -1));
    count[2] = across(unit(p->rx, -1));
    if (p->dpad & TFX_PAD_UP)
        count[3] = 0;
    else if (p->dpad & TFX_PAD_DOWN)
        count[3] = 750 / 2;
    else if (p->dpad & TFX_PAD_LEFT)
        count[3] = 750 / 4;
    else if (p->dpad & TFX_PAD_RIGHT)
        count[3] = 750 * 3 / 4;
    else
        count[3] = (uint16_t)(750 - (int)(unit(p->trigger, 0) * 700 + 0.5f));
    *buttons = (uint8_t)(p->buttons & 15);
}
