/* The settings (M06): TFX.CFG through the recovered CONFIG (src/config:
 * its defaults and parser, System Check's rules, its writer, the joystick
 * calibration flows) over the host's files, for the application's
 * settings view. Portable C.
 *
 * - Load: TFX.CFG as the game would find it (the user's store first, then
 *   the game folder); with none this is CONFIG's first run, which starts
 *   in System Check: its rules applied to native measurements (the
 *   machine's memory, a connected controller, and a processor and video
 *   as fast as the rules' tops: a modern Mac outruns every 1995 PC).
 * - Save: CONFIG's writer (the same calibration precondition) into the
 *   store, never the game folder; the game reads it at its next start.
 * - The joystick: CONFIG's port read (_ReadJoystick, the game's 0x1601c8
 *   at CONFIG's addresses) and loop count (_CalcJoystickTimeout) over the
 *   controller's counts, as the calibration screens read it. */
#ifndef TFX_MAC_SETTINGS_H
#define TFX_MAC_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "config/cfg.h"
#include "config/joycal.h"
#include "core/stick.h"
#include "files.h"

enum { TFX_MAC_CFG_NONE = 0, TFX_MAC_CFG_STORE = 1, TFX_MAC_CFG_GAME = 2 };

typedef struct tfx_mac_settings {
    tfx_config_settings s;     /* the working copy */
    int from;                  /* TFX_MAC_CFG_*: where TFX.CFG was read */
    tfx_examine_report report; /* System Check's (first run, or asked for) */
    int examined;
} tfx_mac_settings;

/* TFX.CFG loaded (CONFIG's SetDefaults_), System Check run when there is none */
void tfx_mac_settings_load(tfx_mac_settings *m, const tfx_mac_files *f, uint64_t memory_bytes, int joystick);
/* System Check's rules on native measurements (applies, as the original) */
void tfx_mac_settings_examine(tfx_mac_settings *m, uint64_t memory_bytes, int joystick);
/* CONFIG's SaveConfig_ into the store; TFX_CONFIG_NEEDS_CALIBRATION
 * (nothing written) when the joystick mode is uncalibrated */
tfx_config_save_result tfx_mac_settings_save(tfx_mac_settings *m, const tfx_mac_files *f, int joystick);

/* CONFIG's About text (AboutTFX_, 0x40138) from the game folder's
 * CONFIG.EXE when it is the supported release's (its digest checked);
 * 0 when not (the text is the user's, not shipped) */
int tfx_mac_settings_about(const tfx_mac_files *f, char *out, size_t cap);
/* how the sound devices selected play here (the session's description
 * and the settings view's notes) */
void tfx_mac_sound_describe(int soundfx, int music, int cd, char *out, size_t cap);

/* CONFIG's joystick as the calibration screens read it */
typedef struct tfx_mac_cal_port {
    tfx_stick_state st;        /* CONFIG's port state (0x42178 ..) */
    tfx_stick_device device;   /* the controller now */
} tfx_mac_cal_port;

/* CONFIG's state as loaded: loop limit 4000, the axes mask given
 * (tfx_joystick_ui.axis_mask) */
void tfx_mac_cal_port_init(tfx_mac_cal_port *p, uint32_t axis_mask);
/* _CalcJoystickTimeout with the positions and buttons it leaves (a
 * tfx_joy_read_fn over a tfx_mac_cal_port) */
tfx_joy_read tfx_mac_cal_read(void *port);
/* _JoystickPresent: a read gives a count */
int tfx_mac_cal_present(tfx_mac_cal_port *p);

#endif
