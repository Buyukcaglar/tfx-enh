#include "host/userfiles.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/hiscore.h"
#include "formats/pilot.h"
#include "formats/rec.h"

static int same_ascii(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int digits(const char *s, size_t n) {
    if (!n)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

int tfx_userfile_kind(const char *name, int *slot) {
    char n[16];
    size_t len = strlen(name);
    if (len >= sizeof n)
        return -1;
    for (size_t i = 0; i <= len; i++)
        n[i] = (char)tolower((unsigned char)name[i]);
    *slot = 0;
    if (!strcmp(n, "tfx.cfg"))
        return TFX_USER_CONFIG;
    if (!strcmp(n, "tfx.hs"))
        return TFX_USER_HISCORE;
    if (!strcmp(n, "video.rec"))
        return TFX_USER_RECORDING;
    if (!strncmp(n, "tfxsav.", 7)) { /* the extension: up to three characters */
        const char *e = n + 7;
        size_t k = strlen(e);
        if (k >= 1 && k <= 3 && digits(e, k)) {
            *slot = atoi(e);
            return TFX_USER_PILOT;
        }
        if (k >= 2 && k <= 3 && e[k - 1] == 'l' && digits(e, k - 1)) {
            *slot = atoi(e);
            return TFX_USER_PILOT_LOG;
        }
    }
    return -1;
}

/* the game's keys (0x20a454), matched there in capitals only */
static const char *const GAME_KEYS[] = {"CONTROL", "DETAIL",  "MODEL",   "SOUNDFX", "MUSIC",    "STICK",
                                        "JOYSTICK", "LANG",   "LOWMEM",  "BLASTER", "ULTRASND", "IRQBASE",
                                        "REALISM", "JOYSCALE", "HUDCOL"};

static int check_config(const uint8_t *data, size_t len, char *why, size_t cap) {
    if (len >= 1024) {
        snprintf(why, cap, "%zu bytes: the game reads TFX.CFG only under 1024 bytes (else its built-in settings)",
                 len);
        return 0;
    }
    /* each line's key: one the game knows only in another case is named */
    char notes[400] = "";
    size_t at = 0;
    while (at < len) {
        size_t e = at;
        while (e < len && data[e] != '\n')
            e++;
        char key[32];
        size_t k = 0;
        while (at + k < e && data[at + k] != '=' && k < sizeof key - 1)
            key[k] = (char)data[at + k], k++;
        while (k && (key[k - 1] == '\r' || key[k - 1] == ' '))
            k--;
        key[k] = 0;
        for (size_t i = 0; i < sizeof GAME_KEYS / sizeof GAME_KEYS[0]; i++)
            if (strcmp(key, GAME_KEYS[i]) && same_ascii(key, GAME_KEYS[i]) &&
                strlen(notes) + strlen(key) + 4 < sizeof notes) {
                strcat(notes, notes[0] ? ", " : "");
                strcat(notes, key);
            }
        at = e + 1;
    }
    if (notes[0])
        snprintf(why, cap, "CONFIG reads it; the game misses these keys (it matches capitals only): %s", notes);
    else
        snprintf(why, cap, "both CONFIG and the game read it");
    return 1;
}

static int check_pilot(const uint8_t *data, size_t len, const tfx_par *script, char *why, size_t cap) {
    if (!script) {
        snprintf(why, cap, "the game's flow script (cd_demo.par) could not be read to check it");
        return 0;
    }
    tfx_pilot_layout l;
    for (int i = 0; i < TFX_PILOT_VARS; i++)
        l.is_string[i] = i < script->vars && script->var_type[i] == 1;
    tfx_pilot *p = malloc(sizeof *p);
    if (!p) {
        snprintf(why, cap, "no memory");
        return 0;
    }
    size_t used = tfx_pilot_decode(&l, data, len, p);
    int ok = 0;
    if (!used)
        snprintf(why, cap, "%zu bytes: too short for the script's variables and the box stack it names, or a "
                 "stack deeper than the game's %d", len, TFX_PILOT_STACK);
    else if (used != len)
        snprintf(why, cap, "%zu bytes: the save ends at %zu (another script's variables, or not a pilot)", len, used);
    else {
        ok = 1;
        for (int i = 0; i <= p->stack_count && ok; i++) {
            tfx_pilot_pos q = i < p->stack_count ? p->stack[i] : p->resume;
            if (i == p->stack_count && q.flow == -1 && q.node == -1)
                break;
            const char *what = i < p->stack_count ? "a box-stack entry" : "the resume node";
            if (q.flow < 0 || q.flow >= script->flows) {
                snprintf(why, cap, "%s names flow %d: the script has %d", what, q.flow, script->flows);
                ok = 0;
            } else if (q.node < 0 || q.node >= script->flow[q.flow].count) {
                snprintf(why, cap, "%s names node %d of flow %d: the flow has %d", what, q.node, q.flow,
                         script->flow[q.flow].count);
                ok = 0;
            }
        }
        if (ok)
            snprintf(why, cap, "a pilot of the game's script: box stack %d deep, resume node %s", p->stack_count,
                     p->resume.flow < 0 ? "none" : "set");
    }
    free(p);
    return ok;
}

static int check_recording(const uint8_t *data, size_t len, char *why, size_t cap) {
    uint16_t frames;
    uint32_t size, shapes;
    if (!tfx_rec_header_decode(data, len, &frames, &size, &shapes) || !tfx_rec_playable(data, len)) {
        snprintf(why, cap, "%zu bytes: not playable (no frames, or the stored size is not the file's)", len);
        return 0;
    }
    if (shapes < TFX_REC_HEADER || shapes >= len) {
        snprintf(why, cap, "the shape table's offset %u is outside the file", (unsigned)shapes);
        return 0;
    }
    tfx_rec_shapes *s = malloc(sizeof *s);
    int ok = s && tfx_rec_shapes_decode(data + shapes, len - shapes, s) != 0;
    if (ok)
        snprintf(why, cap, "a recording: %u frames, %u shapes", (unsigned)frames, (unsigned)s->count);
    else
        snprintf(why, cap, "the shape table at %u does not decode", (unsigned)shapes);
    free(s);
    return ok;
}

int tfx_userfile_check(tfx_user_file kind, const uint8_t *data, size_t len, const tfx_par *script, char *why,
                       size_t cap) {
    switch (kind) {
    case TFX_USER_CONFIG: return check_config(data, len, why, cap);
    case TFX_USER_HISCORE: {
        if (len != TFX_HS_FILE_SIZE) {
            snprintf(why, cap, "%zu bytes, not %d", len, TFX_HS_FILE_SIZE);
            return 0;
        }
        uint8_t table[TFX_HS_TABLE_SIZE];
        tfx_rng rng;
        tfx_rng_reset(&rng);
        if (!tfx_hs_decode(data, table, &rng)) {
            snprintf(why, cap, "the checksum fails: the game would restore its default table");
            return 0;
        }
        tfx_hs_entry e = tfx_hs_entry_at(table, 0);
        snprintf(why, cap, "a score table: first %.20s, %ld", e.callsign, (long)e.score);
        return 1;
    }
    case TFX_USER_PILOT:
    case TFX_USER_PILOT_LOG: return check_pilot(data, len, script, why, cap);
    case TFX_USER_RECORDING: return check_recording(data, len, why, cap);
    }
    snprintf(why, cap, "not a file the game writes");
    return 0;
}
