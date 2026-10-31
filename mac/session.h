/* The macOS host's session (M01): the core (include/tfx/core.h) over the
 * host's files, clock and mixer, and the stepping a front end does in
 * turn. Portable C, so the app (app.m) and the headless runner
 * (mac/headless.c) run the game the same way.
 *
 * The game is run the way TFX.BAT starts it ("start cd_demo": TFX.EXE
 * with the flow script cd_demo); the DID variable is C:\TFX, which the
 * files map onto the original folder. The music devices' port writes go
 * to the mixer's music (M05: the OPL2, the MPU-401's MIDI); with a CD
 * image the machine has a CD-ROM drive and its audio plays through the
 * mixer (without one, MSCDEX is absent, as on a machine without a drive).
 * At the start the devices TFX.CFG selects are described (`audio`):
 * those played as the original's and those that are not. */
#ifndef TFX_MAC_SESSION_H
#define TFX_MAC_SESSION_H

#include <stdint.h>

#include "cdaudio.h"
#include "files.h"
#include "mixer.h"
#include "tfx/core.h"

typedef struct tfx_mac_session {
    tfx_mac_files files;
    tfx_mac_mixer *mixer;            /* NULL: the samples dropped */
    uint64_t (*now_us)(void *ctx);   /* the host's monotonic clock */
    void *clock_ctx;
    void (*log)(void *ctx, const char *message); /* the original's prints and the core's reports */
    void *log_ctx;
    tfx_mac_cd *cd;                  /* the CD image (NULL: no drive); used with a mixer */
    tfx *core;
    char audio[512];                 /* the devices TFX.CFG selects, described at the start */
    uint64_t frames;                 /* frames shown */
    uint64_t music_writes;           /* the music devices' port writes */
    uint64_t cd_plays;               /* the CD's plays */
    uint64_t samples;                /* samples started */
    char error[1200];                /* why the session could not start */
} tfx_mac_session;

enum { TFX_MAC_SHOWN = 1, TFX_MAC_ENDED = 2 };

/* the core created over the folders (the session's fields set first);
 * 0 when it cannot be (error: why; the store must be writable) */
int tfx_mac_session_start(tfx_mac_session *s, const char *root, const char *store);
/* the game stepped until it waits, ends or the budget (on the session's
 * clock) runs out: TFX_MAC_SHOWN when a frame was shown, TFX_MAC_ENDED */
int tfx_mac_session_pump(tfx_mac_session *s, uint64_t budget_us);
/* the 320 x 200 frame as 32-bit pixels (bytes R G B A), the DAC's 6-bit
 * values widened as the VGA's DAC does */
void tfx_mac_session_rgba(const tfx_mac_session *s, uint8_t *rgba);
/* the enhanced 3D mode (render/hires.h, not the original's; the Windows
 * host's Ctrl+Shift+G): the mode set to w x h and the frame shown last at
 * that size written as 32-bit pixels (w * h * 4 bytes, the same DAC as
 * tfx_mac_session_rgba); 0 when there is no picture at that size (no
 * memory). w or h 0: the mode off */
int tfx_mac_session_enhanced_rgba(tfx_mac_session *s, int w, int h, uint8_t *rgba);
/* the run ended through the game's exit path (its open files written),
 * then freed */
void tfx_mac_session_stop(tfx_mac_session *s);

#endif
