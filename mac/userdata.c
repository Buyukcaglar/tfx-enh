#include "userdata.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "formats/did.h"
#include "formats/hiscore.h"
#include "formats/par.h"
#include "formats/rec.h"
#include "formats/resource.h"

tfx_mac_user_kind tfx_mac_user_kind_of(const char *name) {
    if (!strcasecmp(name, "TFX.CFG"))
        return TFX_MAC_USER_CFG;
    if (!strcasecmp(name, "TFX.HS"))
        return TFX_MAC_USER_HS;
    if (!strcasecmp(name, "VIDEO.REC"))
        return TFX_MAC_USER_REC;
    /* "tfxsav.%i" (slots 0 .. 9; the pilot screen's temporary 99) and "%sl" */
    if (strncasecmp(name, "TFXSAV.", 7) || !isdigit((unsigned char)name[7]))
        return TFX_MAC_USER_NONE;
    const char *p = name + 8;
    if (isdigit((unsigned char)*p))
        p++;
    if (!*p)
        return TFX_MAC_USER_PILOT;
    return (*p == 'l' || *p == 'L') && !p[1] ? TFX_MAC_USER_LOG : TFX_MAC_USER_NONE;
}

/* ---- the flow script ---- */

/* the release's cd_demo.par: its three strings first (EX12: 177 + 4 x depth bytes) */
static void release_layout(tfx_pilot_layout *l) {
    memset(l, 0, sizeof *l);
    l->is_string[0] = l->is_string[1] = l->is_string[2] = 1;
}

static const tfx_mac_files *loose_files;
static uint8_t *loose_data;
static int loose_open(void *ctx, const char *name, const uint8_t **data, size_t *len) {
    (void)ctx;
    uint32_t n = tfx_mac_size(loose_files, name);
    free(loose_data);
    loose_data = n ? malloc(n) : NULL;
    if (!loose_data || !tfx_mac_read(loose_files, name, loose_data, n))
        return 0;
    *data = loose_data, *len = n;
    return 1;
}

int tfx_mac_user_flow_load(tfx_mac_user_flow *fl, const tfx_mac_files *f) {
    memset(fl, 0, sizeof *fl);
    release_layout(&fl->layout);
    /* as the game finds it: DID.DAT's record, else the loose file (only
     * the game folder: the store holds no scripts) */
    tfx_mac_files game = *f;
    game.store[0] = 0;
    static tfx_did did;
    uint32_t n = tfx_mac_size(&game, "DID.DAT");
    uint8_t *archive = n ? malloc(n) : NULL;
    int have = archive && tfx_mac_read(&game, "DID.DAT", archive, n) && tfx_did_open(&did, archive, n) == TFX_DID_OK;
    loose_files = &game;
    tfx_resources r = {have ? &did : NULL, TFX_MAC_DID, {loose_open, NULL}, 0, 0};
    static uint8_t script[1 << 20];
    size_t len = 0;
    static tfx_par par;
    if (tfx_res_load(&r, TFX_MAC_DID "\\cd_demo.par", script, sizeof script, &len) == TFX_RES_OK &&
        tfx_par_parse(&par, script, len) == TFX_PAR_OK && par.flows <= 64) {
        for (int i = 0; i < TFX_PILOT_VARS; i++) /* (slots past the variables: numbers) */
            fl->layout.is_string[i] = i < par.vars && par.var_type[i] != 0;
        fl->flows = par.flows;
        for (int i = 0; i < par.flows; i++)
            fl->nodes[i] = par.flow[i].count;
        fl->loaded = 1;
    }
    free(loose_data);
    loose_data = NULL;
    free(archive);
    return fl->loaded;
}

/* ---- the checks ---- */

static int in_script(const tfx_mac_user_flow *fl, tfx_pilot_pos p) {
    return p.flow >= 0 && p.flow < fl->flows && p.node >= 0 && p.node < fl->nodes[p.flow];
}

int tfx_mac_user_check(const tfx_mac_user_flow *fl, const char *name, const uint8_t *data, size_t len, char *why,
                       size_t cap) {
    snprintf(why, cap, "accepted");
    switch (tfx_mac_user_kind_of(name)) {
    case TFX_MAC_USER_CFG:
        if (len >= 1024) {
            snprintf(why, cap, "%zu bytes: the game reads only files under 1024 bytes (it would use its built-in "
                               "settings)", len);
            return 0;
        }
        if (memchr(data, 0, len)) {
            snprintf(why, cap, "not a text file");
            return 0;
        }
        return 1;
    case TFX_MAC_USER_HS: {
        uint8_t table[TFX_HS_TABLE_SIZE];
        tfx_rng rng;
        if (len != TFX_HS_FILE_SIZE) {
            snprintf(why, cap, "%zu bytes, not the scores' %d", len, TFX_HS_FILE_SIZE);
            return 0;
        }
        if (!tfx_hs_decode(data, table, &rng)) {
            snprintf(why, cap, "its checksum does not match (the game would restore its default scores over it)");
            return 0;
        }
        return 1;
    }
    case TFX_MAC_USER_PILOT:
    case TFX_MAC_USER_LOG: {
        static tfx_pilot p;
        size_t used = tfx_pilot_decode(&fl->layout, data, len, &p);
        if (!used || used != len) {
            snprintf(why, cap, used ? "%zu bytes after the pilot's state" : "not a pilot of this game's flow script",
                     len - used);
            return 0;
        }
        if (fl->loaded) {
            for (int i = 0; i < p.stack_count; i++)
                if (!in_script(fl, p.stack[i])) {
                    snprintf(why, cap, "a node not in the flow script (flow %d, node %d): the game would stop "
                                       "loading it", p.stack[i].flow, p.stack[i].node);
                    return 0;
                }
            if (!(p.resume.flow == -1 && p.resume.node == -1) && !in_script(fl, p.resume)) {
                snprintf(why, cap, "its resume node is not in the flow script (flow %d, node %d)", p.resume.flow,
                         p.resume.node);
                return 0;
            }
        }
        return 1;
    }
    case TFX_MAC_USER_REC:
        if (!tfx_rec_playable(data, len)) {
            snprintf(why, cap, "not a recording the game plays (no frames, or not its stored size)");
            return 0;
        }
        return 1;
    default:
        snprintf(why, cap, "not one of the game's files");
        return 0;
    }
}

/* ---- versions ---- */

static void version_path(const tfx_mac_files *f, const char *name, int k, char *out, size_t n) {
    char low[16];
    size_t i = 0;
    for (; name[i] && i < sizeof low - 1; i++)
        low[i] = (char)tolower((unsigned char)name[i]);
    low[i] = 0;
    snprintf(out, n, "%s/.history/%s~%d", f->store, low, k);
}

int tfx_mac_user_version(const tfx_mac_files *f, const char *name, int k, int64_t *when, uint32_t *size) {
    char path[TFX_MAC_PATH + 48];
    struct stat st;
    version_path(f, name, k, path, sizeof path);
    if (stat(path, &st) || !S_ISREG(st.st_mode))
        return 0;
    if (when)
        *when = (int64_t)st.st_mtime;
    if (size)
        *size = (uint32_t)st.st_size;
    return 1;
}

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *in = fopen(path, "rb");
    if (!in)
        return NULL;
    struct stat st;
    uint8_t *b = NULL;
    if (!fstat(fileno(in), &st) && S_ISREG(st.st_mode) && st.st_size < (64 << 20) &&
        (b = malloc((size_t)st.st_size + 1)) && fread(b, 1, (size_t)st.st_size, in) != (size_t)st.st_size) {
        free(b);
        b = NULL;
    }
    fclose(in);
    if (b)
        *len = (size_t)st.st_size;
    return b;
}

int tfx_mac_user_restore(const tfx_mac_files *f, const char *name, int k) {
    char path[TFX_MAC_PATH + 48];
    version_path(f, name, k, path, sizeof path);
    size_t len = 0;
    uint8_t *b = slurp(path, &len);
    int ok = b && tfx_mac_write_kept(f, name, b, (uint32_t)len);
    free(b);
    return ok;
}

/* ---- import and export ---- */

int tfx_mac_user_import(const tfx_mac_files *f, const tfx_mac_user_flow *fl, const char *dir, tfx_mac_user_report report,
                        void *ctx) {
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int written = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!tfx_mac_user_kind_of(e->d_name))
            continue;
        char path[TFX_MAC_PATH + 300], why[160];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        size_t len = 0;
        uint8_t *b = slurp(path, &len);
        int ok = 0;
        if (!b)
            snprintf(why, sizeof why, "could not be read");
        else if (tfx_mac_user_check(fl, e->d_name, b, len, why, sizeof why)) {
            ok = tfx_mac_write_kept(f, e->d_name, b, (uint32_t)len);
            if (!ok)
                snprintf(why, sizeof why, "could not be written to the store (the store's copy kept)");
        }
        written += ok;
        if (report)
            report(ctx, e->d_name, ok, why);
        free(b);
    }
    closedir(d);
    return written;
}

static int same_dir(const char *a, const char *b) {
    char ra[TFX_MAC_PATH], rb[TFX_MAC_PATH];
    return a[0] && b[0] && realpath(a, ra) && realpath(b, rb) && !strcmp(ra, rb);
}

int tfx_mac_user_export(const tfx_mac_files *f, const char *dir, tfx_mac_user_report report, void *ctx) {
    if (same_dir(dir, f->root) || same_dir(dir, f->store))
        return -1;
    DIR *d = opendir(f->store);
    if (!d)
        return 0;
    int written = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !tfx_mac_user_kind_of(e->d_name))
            continue;
        char from[TFX_MAC_PATH + 300], up[16];
        size_t i = 0;
        for (; e->d_name[i] && i < sizeof up - 1; i++)
            up[i] = (char)toupper((unsigned char)e->d_name[i]);
        up[i] = 0;
        snprintf(from, sizeof from, "%s/%s", f->store, e->d_name);
        size_t len = 0;
        uint8_t *b = slurp(from, &len);
        /* (written as the store's writes are: beside it, then renamed) */
        tfx_mac_files out = {{0}, {0}, NULL};
        snprintf(out.store, sizeof out.store, "%s", dir);
        int ok = b && tfx_mac_write(&out, up, b, (uint32_t)len);
        written += ok;
        if (report)
            report(ctx, up, ok, ok ? "exported" : "could not be copied");
        free(b);
    }
    closedir(d);
    return written;
}
