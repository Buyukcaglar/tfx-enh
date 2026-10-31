/* The Windows settings view (W06): CONFIG.EXE's screens as native windows
 * over its recovered controller (config/menu.c) and the native settings
 * (host/settings.h). `tfx --settings` runs it in place of the game.
 *
 * Each of CONFIG's states is a page: the main settings (its switch rows,
 * the swap file kept as metadata), the joystick's calibration (stick A,
 * FlightStick Pro, ThrustMaster), save (to the user store's TFX.CFG,
 * never the game folder), System Check (CONFIG's rules on the native
 * machine), About (the credits read from the user's CONFIG.EXE), the
 * boot disk (unavailable: nothing is written) and the Sound Blaster's
 * fields (kept for CONFIG.EXE and DOS; no native effect). QUIT leaves
 * without saving, as CONFIG's.
 *
 * Automation (validation): a script's lines drive the pages through the
 * same controls (set KEY VALUE, swap 0|1, field NAME VALUE, check NAME
 * 0|1, click NAME, answer yes|no, pass N, and any other line handed to
 * the host, such as its virtual stick); the pages are then hidden. */
#ifndef TFX_WIN_SETTINGS_H
#define TFX_WIN_SETTINGS_H

#include <stdint.h>
#include <stdio.h>

#include "config/cfg.h"
#include "core/stick.h"
#include "win/content.h"

typedef struct tfx_win_settings_host {
    tfx_win_content *content;
    int (*stick)(void *ctx, tfx_stick_device *d); /* the controller now (d->present) */
    const char *(*getenv)(void *ctx, const char *name);
    void (*log)(const char *message);
    int (*command)(const char *line); /* an automation line the view does not know: 1 taken */
    FILE *script;                     /* NULL: interactive */
} tfx_win_settings_host;

/* 0 when the view ran (saved or not), 1 when the settings were saved */
int tfx_win_settings(void *instance, const tfx_win_settings_host *host);

/* The credits (AboutTFX_'s text) from CONFIG.EXE in the game folder, if
 * it is the baseline's; 0 and a reason otherwise */
int tfx_win_settings_credits(const tfx_win_content *c, char *out, size_t cap);

#endif
