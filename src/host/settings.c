#include "host/settings.h"

#include <string.h>

#define ROW(k, f, first, n, ...) {k, offsetof(tfx_config_settings, f), first, n, {__VA_ARGS__}}

const tfx_settings_row tfx_settings_rows[TFX_SETTINGS_ROWS] = {
    ROW("CONTROL", control_mode, 0, 2, "KEYBOARD", "JOYSTICK"),
    ROW("DETAIL", w_detail, 0, 3, "LOW", "MEDIUM", "HIGH"),
    ROW("MODEL", this_model, 0, 4, "ARCADE", "SIMPLE", "REALISTIC", "MILITARY SPEC"),
    ROW("SOUNDFX", gsound, 0, 5, "OFF", "SOUND BLASTER", "LAPC1", "SB16 (QSOUND/CSP)", "GUS"),
    ROW("MUSIC", music, 0, 5, "OFF", "SB/CD", "LAPC1", "ADLIB", "SCC1"),
    ROW("STICK", stick, 0, 3, "NORMAL", "FLIGHTSTICK PRO", "THRUSTMASTER"),
    ROW("LANG", lang, 1, 5, "ENGLISH", "FRANCAIS", "DEUTSCH", "ITALIA", "ESPANA"),
};

int32_t *tfx_settings_field(tfx_config_settings *s, const tfx_settings_row *r) {
    return (int32_t *)((char *)s + r->offset);
}

void tfx_settings_load(tfx_config_settings *s, const char *file, long len, tfx_getenv_fn env, void *env_ctx) {
    memset(s, 0, sizeof *s);
    tfx_config_static_init(s);
    tfx_config_load(s, len < 0 ? NULL : file, len < 0 ? 0 : (size_t)len, env, env_ctx);
}

void tfx_settings_examine(tfx_config_settings *s, int joystick_present, tfx_getenv_fn env, void *env_ctx,
                          tfx_examine_report *report) {
    const tfx_examine_inputs in = {16u << 20, joystick_present, 140, 5000};
    tfx_config_examine(s, &in, env, env_ctx, report);
}

tfx_config_save_result tfx_settings_save(tfx_config_settings *s, int joystick_present, char *out, size_t cap,
                                         size_t *len) {
    return tfx_config_save(s, joystick_present, 1, out, cap, len);
}

void tfx_settings_stick_begin(tfx_settings_stick *k, int stick, tfx_settings_device_fn device, void *ctx) {
    memset(k, 0, sizeof *k);
    k->device = device, k->ctx = ctx;
    k->st.loops = 4000;                        /* [0x421d0] */
    k->st.mask = stick == 1 ? 0xb000 : 0x3000; /* [0x421d4], _joysticks_2_read */
}

tfx_joy_read tfx_settings_stick_read(void *ctx) {
    tfx_settings_stick *k = ctx;
    tfx_stick_device d = {0};
    if (k->device)
        k->device(k->ctx, &d);
    k->reads++;
    tfx_joy_read r = {0};
    r.loops = tfx_stick_loops(&k->st, &d);
    r.x = (int32_t)k->st.x, r.y = (int32_t)k->st.y;
    r.left = k->st.button[0] != 0, r.right = k->st.button[1] != 0;
    r.bx = (int32_t)k->st.ring_3[0], r.by = (int32_t)k->st.ring_4[0];
    return r;
}

void tfx_settings_game_view(const char *file, size_t len, tfx_game_cfg *g) {
    memset(g, 0, sizeof *g);
    tfx_game_cfg_parse(g, file, len);
}
