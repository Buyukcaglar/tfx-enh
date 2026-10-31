#include "host/profile.h"

#include <stdio.h>
#include <string.h>

#include "host/sha256.h"

/* from plan/evidence/reference-manifest.json (reference/install/TFX) */
static const tfx_profile_file BASELINE[] = {
    {"DID.DAT", TFX_PROFILE_REQUIRED, 12681671u, "4ec6b35627b12dfe41b9da59d09816fdf105aebb61d662c80553b8241c85b085", "the resource archive"},
    {"TFX.DAT", TFX_PROFILE_REQUIRED, 833674u, "1df51f7dacb6d6598830820566aed840ef56445a7b4e76a80ecb0da318123aa5", "the game program (its data tables are read from it)"},
    {"START.EXE", TFX_PROFILE_REQUIRED, 36288u, "c5a60c7791c971a872aaed1e19ef2638aab0b21eb1e039907b8d64eb8f8980d6", "the loader (its tune tables and synthesizer messages are read from it)"},
    {"TFX.CFG", TFX_PROFILE_OPTIONAL, 131u, "99a5620fef17b37c867161ca382c552eb012206fe92e0fa5f332cc661e271f35", "the settings (defaults when absent; the user store keeps the edited copy)"},
    {"MISSIONS\\MISS830.MIS", TFX_PROFILE_EXPECTED, 696u, "98d26d4b42af77a6cb58c952a96b7f132c585c3ac484e98fd9d1142f20374173", "the version 2.0 mission update"},
    {"TUNES\\INTRO.ADL", TFX_PROFILE_EXPECTED, 8639u, "17c3c16fe2e84d2d5c660e7c598a48cb0f6c1b4655f9034f7bd25b09d8d8d92f", "the intro tune (AdLib)"},
    {"TUNES\\INTRO.RLD", TFX_PROFILE_EXPECTED, 11217u, "f8ce712588cb2e44b4f3c1241e32c274067a804774edd5e979543c03a1bedf17", "the intro tune (LAPC-1)"},
    {"TUNES\\INTRO.SCC", TFX_PROFILE_EXPECTED, 8515u, "31da35289ebf7b04ece0bc94cbcd650c148d8edbb7fb5e1f2bcf72b1ae79564d", "the intro tune (SCC-1)"},
    {"WAYBIN\\COL", TFX_PROFILE_EXPECTED, 2u, "7eb70257593da06f682a3ddda54a9d260d4fc514f645237f5ca74b08f8da61a6", "the planner's waypoint folder"},
    {"WAYTXT\\COL", TFX_PROFILE_EXPECTED, 2u, "7eb70257593da06f682a3ddda54a9d260d4fc514f645237f5ca74b08f8da61a6", "the planner's waypoint text folder"},
    {"CONFIG.EXE", TFX_PROFILE_REPORTED, 341897u, "b97c4f32a7cf7183a8978fbb0717f95cbb71fe803cf34362150e8fa020006504", "the DOS setup program (replaced by the native settings)"},
    {"GUS.COM", TFX_PROFILE_REPORTED, 23376u, "dc8abddfd7deee6e25f3e625708684209a408420c74e3ae1c403e0d4d47a5925", "the UltraSound loader (replaced by the native host)"},
    {"Q2.COM", TFX_PROFILE_REPORTED, 14750u, "24d626ce3a436296cd4cbfeb4123c8eb0b26765cb73f6678346512fb064edbbf", "the CSP loader (replaced by the native host)"},
    {"TFX.ADV", TFX_PROFILE_REPORTED, 258u, "6482c2a00ceec44eec11ad908421c22744278fdd23a2ba5aed825d5804849a16", "the advanced key list (R09: no program reads it)"},
    {"TFX.MK1", TFX_PROFILE_REPORTED, 924u, "b20e0124d3810827f77bf0648cd0926e80b2d87473c893db435b3a91fc1bb139", "the Mk 1 key list (R09: no program reads it)"},
    {"TFX.BAT", TFX_PROFILE_REPORTED, 15u, "5354baa2500f72e3f75383ce671a096756db5432b9d198381d0b9d84bed5f545", "the DOS launcher"},
    {"README.TXT", TFX_PROFILE_REPORTED, 2539u, "2a77149174235c2bd3962d4224c7d674354b2e0d11441e0a4a035b34abd96c96", "notes"},
    {"TFXMK1.TXT", TFX_PROFILE_REPORTED, 330u, "c153c249c0361ca37d089e4e0c528c1ba5bc0e6882cdcc6277c3b96d670dad89", "notes"},
    {"UPDATE.DOC", TFX_PROFILE_REPORTED, 1828u, "433927b002809ce28dd9ebe4c6af24762fa8911a04b04fefb10248a270d3f571", "the version 2.0 notes"},
};

const tfx_profile_file *tfx_profile_baseline(int *n) {
    *n = (int)(sizeof BASELINE / sizeof BASELINE[0]);
    return BASELINE;
}

const char *tfx_profile_baseline_name(void) { return "TFX version 2.0 (21 February 1994), installed"; }

static void sink_add(void *sink, const void *data, uint32_t n) { tfx_sha256_add(sink, data, n); }

void tfx_profile_check(const tfx_profile_io *io, tfx_profile_report *out) {
    int n;
    const tfx_profile_file *f = tfx_profile_baseline(&n);
    memset(out, 0, sizeof *out);
    out->n = n;
    int missing = 0, different = 0;
    for (int i = 0; i < n; i++) {
        long long size = io->size(io->ctx, f[i].path);
        if (size < 0) {
            out->state[i] = TFX_FILE_MISSING;
        } else {
            out->size[i] = (uint32_t)size;
            tfx_sha256 s;
            uint8_t d[32];
            tfx_sha256_init(&s);
            if (!io->stream(io->ctx, f[i].path, sink_add, &s)) {
                out->state[i] = TFX_FILE_UNREADABLE;
            } else {
                tfx_sha256_done(&s, d);
                tfx_sha256_hex(d, out->sha256[i]);
                out->state[i] = (uint32_t)size == f[i].size && !strcmp(out->sha256[i], f[i].sha256)
                                    ? TFX_FILE_MATCH
                                    : TFX_FILE_DIFFERENT;
            }
        }
        if (f[i].kind == TFX_PROFILE_REQUIRED) {
            missing += out->state[i] == TFX_FILE_MISSING || out->state[i] == TFX_FILE_UNREADABLE;
            different += out->state[i] == TFX_FILE_DIFFERENT;
        }
    }
    out->verdict = missing ? TFX_PROFILE_INCOMPLETE : different ? TFX_PROFILE_UNVERIFIED : TFX_PROFILE_SUPPORTED;
}

void tfx_profile_line(const tfx_profile_report *r, int i, char *out, unsigned cap) {
    int n;
    const tfx_profile_file *f = tfx_profile_baseline(&n);
    static const char *kind[] = {"required", "optional", "expected", "not read"};
    switch (r->state[i]) {
    case TFX_FILE_MATCH:
        snprintf(out, cap, "%s: matches the baseline", f[i].path);
        break;
    case TFX_FILE_MISSING:
        snprintf(out, cap, "%s: missing (%s: %s)", f[i].path, kind[f[i].kind], f[i].role);
        break;
    case TFX_FILE_UNREADABLE:
        snprintf(out, cap, "%s: cannot be read (%s: %s)", f[i].path, kind[f[i].kind], f[i].role);
        break;
    default:
        snprintf(out, cap, "%s: differs from the baseline (%u bytes, SHA-256 %.16s..., expected %u bytes, %.16s...)",
                 f[i].path, (unsigned)r->size[i], r->sha256[i], (unsigned)f[i].size, f[i].sha256);
        break;
    }
}

const char *tfx_profile_verdict_text(tfx_profile_verdict v) {
    switch (v) {
    case TFX_PROFILE_SUPPORTED:
        return "The folder holds the supported baseline.";
    case TFX_PROFILE_UNVERIFIED:
        return "The folder's game files differ from the supported baseline (another release or a modified copy); "
               "it may not run correctly.";
    default:
        return "The folder lacks files the game needs.";
    }
}
