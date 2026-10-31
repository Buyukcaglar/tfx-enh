/* The native settings (W06): CONFIG.EXE's recovered controller and rules
 * (config/cfg.c, joycal.c, menu.c; EX10, EX11) over the native host.
 *
 * - The file: the user store's TFX.CFG (else the game folder's, read
 *   only), loaded by SetDefaults_'s rules; no file starts CONFIG's first
 *   run (state 5, System Check). Saved by SaveConfig_'s writer (CRLF) to
 *   the user store only, which the game then reads as its own.
 * - System Check: CONFIG's recommendation rules (tfx_config_examine) on
 *   the native machine's measures. The game runs at the host's full
 *   speed whatever the PC, so the measures are those of a machine past
 *   every threshold: memory 16 MB, the benchmarks at their maxima
 *   (MODEL MILITARY SPEC, DETAIL HIGH), the joystick as detected and the
 *   host's BLASTER string.
 * - The stick: CONFIG's reader _ReadJoystick 0x18214, the game's same
 *   algorithm (core/stick.c's 0x1601c8, with CONFIG's statics: the count
 *   4000, the mask by STICK), over the host's game port; a read as
 *   _CalcJoystickTimeout 0x18313 leaves it: the largest count in the X
 *   and Y rings, the X and Y readings, buttons 1 and 2, and stick B's
 *   positions as CONFIG takes them (the rings' first entries, axes 3
 *   and 4 halved).
 * - The labels: the value names of CONFIG's switch rows (EX10's matrix). */
#ifndef TFX_HOST_SETTINGS_H
#define TFX_HOST_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "config/cfg.h"
#include "config/joycal.h"
#include "core/stick.h"

/* the switch rows: key, the field, the values and their names */
typedef struct tfx_settings_row {
    const char *key;
    size_t offset; /* of the int32_t in tfx_config_settings */
    int first, count;
    const char *names[5];
} tfx_settings_row;

#define TFX_SETTINGS_ROWS 7
extern const tfx_settings_row tfx_settings_rows[TFX_SETTINGS_ROWS];
int32_t *tfx_settings_field(tfx_config_settings *s, const tfx_settings_row *r);

/* SetDefaults_ over the file's bytes (len < 0: no file) */
void tfx_settings_load(tfx_config_settings *s, const char *file, long len, tfx_getenv_fn env, void *env_ctx);

/* System Check with the native measures */
void tfx_settings_examine(tfx_config_settings *s, int joystick_present, tfx_getenv_fn env, void *env_ctx,
                          tfx_examine_report *report);

/* SaveConfig_ to the file's bytes (CRLF) */
tfx_config_save_result tfx_settings_save(tfx_config_settings *s, int joystick_present, char *out, size_t cap,
                                         size_t *len);

/* the stick's reads */
typedef int (*tfx_settings_device_fn)(void *ctx, tfx_stick_device *d); /* the controller now: d->present */
typedef struct tfx_settings_stick {
    tfx_settings_device_fn device;
    void *ctx;
    tfx_stick_state st;
    unsigned long reads;
} tfx_settings_stick;

/* mask by STICK: 0xb000 for STICK 1 (axis 3 too), else 0x3000 */
void tfx_settings_stick_begin(tfx_settings_stick *k, int stick, tfx_settings_device_fn device, void *ctx);
tfx_joy_read tfx_settings_stick_read(void *k); /* a tfx_joy_read_fn */

/* the game's values from the saved file (its own reader), for the view's
 * check that a save reaches the game */
void tfx_settings_game_view(const char *file, size_t len, tfx_game_cfg *g);

#endif
