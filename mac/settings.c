#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile.h"

/* the Mac has no DOS environment: BLASTER and ULTRASND are unset */
static const char *no_env(void *ctx, const char *name) { return (void)ctx, (void)name, NULL; }

void tfx_mac_settings_examine(tfx_mac_settings *m, uint64_t memory_bytes, int joystick) {
    /* the measurements' native stand-ins: the memory DPMI would report
     * (capped to 32 bits, as its page count), the controller, and the
     * benchmarks at the rules' tops (MODEL 3 needs 140, DETAIL 2 5000) */
    tfx_examine_inputs in = {memory_bytes > 0xffffffffu ? 0xffffffffu : (uint32_t)memory_bytes, joystick, 140, 5000};
    tfx_config_examine(&m->s, &in, no_env, NULL, &m->report);
    m->examined = 1;
}

void tfx_mac_settings_load(tfx_mac_settings *m, const tfx_mac_files *f, uint64_t memory_bytes, int joystick) {
    memset(m, 0, sizeof *m);
    tfx_config_static_init(&m->s);
    char *text = NULL;
    uint32_t n = tfx_mac_size(f, "TFX.CFG");
    if (n && (text = malloc(n)) != NULL && tfx_mac_read(f, "TFX.CFG", (uint8_t *)text, n)) {
        /* (where it came from: the store's copy wins, as the files read) */
        tfx_mac_files store_only = *f;
        memcpy(store_only.root, f->store, sizeof store_only.root);
        m->from = tfx_mac_size(&store_only, "TFX.CFG") ? TFX_MAC_CFG_STORE : TFX_MAC_CFG_GAME;
        /* text mode: CRLF read as LF */
        uint32_t w = 0;
        for (uint32_t i = 0; i < n; i++)
            if (!(text[i] == '\r' && i + 1 < n && text[i + 1] == '\n'))
                text[w++] = text[i];
        tfx_config_load(&m->s, text, w, no_env, NULL);
    } else {
        tfx_config_load(&m->s, NULL, 0, no_env, NULL);
    }
    free(text);
    if (m->s.first == 5) /* CONFIG's first run starts in System Check */
        tfx_mac_settings_examine(m, memory_bytes, joystick);
}

tfx_config_save_result tfx_mac_settings_save(tfx_mac_settings *m, const tfx_mac_files *f, int joystick) {
    char out[2048];
    size_t len;
    tfx_config_settings s = m->s; /* (the writer derives fields; kept only when saved) */
    tfx_config_save_result r = tfx_config_save(&s, joystick, 1, out, sizeof out, &len);
    if (r != TFX_CONFIG_SAVED)
        return r;
    if (len > sizeof out || !tfx_mac_write_kept(f, "TFX.CFG", (const uint8_t *)out, (uint32_t)len))
        return TFX_CONFIG_WRITE_FAILED;
    m->s = s;
    m->from = TFX_MAC_CFG_STORE;
    return r;
}

void tfx_mac_cal_port_init(tfx_mac_cal_port *p, uint32_t axis_mask) {
    memset(p, 0, sizeof *p);
    p->st.loops = 4000, p->st.mask = axis_mask;
}

tfx_joy_read tfx_mac_cal_read(void *port) {
    tfx_mac_cal_port *p = port;
    tfx_joy_read r;
    r.loops = tfx_stick_loops(&p->st, &p->device);
    r.x = (int32_t)p->st.x, r.y = (int32_t)p->st.y;
    r.left = p->st.button[0], r.right = p->st.button[1];
    r.bx = (int32_t)p->st.ring_3[0], r.by = (int32_t)p->st.ring_4[0];
    return r;
}

int tfx_mac_cal_present(tfx_mac_cal_port *p) { return tfx_stick_probe(&p->st, &p->device) != 0; }

/* the text's place in the release's CONFIG.EXE (341897 bytes): the LE
 * image's third object (base 0x40000) starts at file offset 0x42614 */
#define ABOUT_AT 0x4274c
#define ABOUT_MAX 1024

int tfx_mac_settings_about(const tfx_mac_files *f, char *out, size_t cap) {
    char path[TFX_MAC_PATH + 16], digest[65];
    snprintf(path, sizeof path, "%s/CONFIG.EXE", f->root);
    if (cap == 0 || !tfx_mac_sha256_file(path, digest) ||
        strcmp(digest, "b97c4f32a7cf7183a8978fbb0717f95cbb71fe803cf34362150e8fa020006504"))
        return 0;
    FILE *in = fopen(path, "rb");
    char buf[ABOUT_MAX];
    size_t n = 0;
    if (in && !fseek(in, ABOUT_AT, SEEK_SET))
        n = fread(buf, 1, sizeof buf, in);
    if (in)
        fclose(in);
    char *end = memchr(buf, 0, n);
    if (!end)
        return 0;
    size_t k = (size_t)(end - buf) < cap - 1 ? (size_t)(end - buf) : cap - 1;
    memcpy(out, buf, k);
    out[k] = 0;
    return 1;
}

void tfx_mac_sound_describe(int soundfx, int music, int cd, char *out, size_t cap) {
    static const char *const SFX[] = {"no sound effects", "Sound Blaster effects",
                                      "LAPC-I effects: on the General MIDI synthesizer, so not with the MT-32's sounds",
                                      "Sound Blaster 16 effects, without its CSP chip's QSound processing (Q2.COM)",
                                      "UltraSound effects"};
    int fm = !music && soundfx == 1; /* (the game then sets up the AdLib's driver, silent: 0x135c0d) */
    if (music == 3)
        music = 1; /* (the game folds ADLIB to 1) */
    const char *m = fm           ? "no music (the game sets up the AdLib's driver, silent)"
                    : music == 0 ? "no music"
                    : music == 1 ? (cd ? "AdLib music (OPL2, synthesized), in flight the CD's tracks"
                                       : "AdLib music (OPL2, synthesized)")
                    : music == 2 ? "Roland LAPC-I / MT-32 music: played on the General MIDI synthesizer, so not with "
                                   "the MT-32's own instruments"
                    : music == 4 ? "Roland SCC-1 music (General MIDI / GS) on the General MIDI synthesizer"
                                 : "a music device the game does not have (none plays)";
    snprintf(out, cap, "%s; %s; %s",
             soundfx >= 0 && soundfx < 5 ? SFX[soundfx] : "an effects device the game does not have (none plays)", m,
             cd ? "CD audio from the image" : "no CD (as without a CD-ROM drive)");
}
