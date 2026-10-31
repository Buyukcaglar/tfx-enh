/* The content profile (W02): which original files a folder holds, and
 * whether they are the supported baseline (TFX version 2.0 of 21 February
 * 1994, installed and patched: plan/evidence/reference-manifest.json),
 * identified by size and SHA-256 only; no original byte is part of the
 * program.
 *
 * Kinds: REQUIRED files the game cannot start without (a missing or
 * changed one stops the start with its name and what was found);
 * OPTIONAL ones with a native default (TFX.CFG); EXPECTED ones the game
 * reads at some point (a missing one is reported and the game goes on,
 * as the original would have); REPORTED ones no native code reads. */
#ifndef TFX_HOST_PROFILE_H
#define TFX_HOST_PROFILE_H

#include <stdint.h>

typedef enum tfx_profile_kind {
    TFX_PROFILE_REQUIRED,
    TFX_PROFILE_OPTIONAL,
    TFX_PROFILE_EXPECTED,
    TFX_PROFILE_REPORTED
} tfx_profile_kind;

typedef struct tfx_profile_file {
    const char *path; /* folder-relative, DOS separators, upper case */
    tfx_profile_kind kind;
    uint32_t size;
    const char *sha256;
    const char *role;
} tfx_profile_file;

typedef enum tfx_file_state {
    TFX_FILE_MATCH,     /* size and hash as the baseline's */
    TFX_FILE_MISSING,
    TFX_FILE_DIFFERENT, /* present, another size or hash */
    TFX_FILE_UNREADABLE
} tfx_file_state;

typedef enum tfx_profile_verdict {
    TFX_PROFILE_SUPPORTED = 0, /* every required file the baseline's */
    TFX_PROFILE_UNVERIFIED = 1, /* required files present, one or more differ (another edition) */
    TFX_PROFILE_INCOMPLETE = 2  /* a required file missing or unreadable */
} tfx_profile_verdict;

#define TFX_PROFILE_MAX 32

typedef struct tfx_profile_report {
    int n;
    tfx_file_state state[TFX_PROFILE_MAX];
    uint32_t size[TFX_PROFILE_MAX];
    char sha256[TFX_PROFILE_MAX][65];
    tfx_profile_verdict verdict;
} tfx_profile_report;

/* The folder's files to the checker: a file's size (-1 missing) and its
 * bytes streamed to add (0 on a read failure). */
typedef struct tfx_profile_io {
    void *ctx;
    long long (*size)(void *ctx, const char *path);
    int (*stream)(void *ctx, const char *path, void (*add)(void *sink, const void *data, uint32_t n), void *sink);
} tfx_profile_io;

const tfx_profile_file *tfx_profile_baseline(int *n);
const char *tfx_profile_baseline_name(void);

void tfx_profile_check(const tfx_profile_io *io, tfx_profile_report *out);

/* A one-line account of file i ("DID.DAT: matches", "TFX.DAT: missing
 * (required: the game program ...)", ...). */
void tfx_profile_line(const tfx_profile_report *r, int i, char *out, unsigned cap);
/* The verdict as a sentence for the user. */
const char *tfx_profile_verdict_text(tfx_profile_verdict v);

#endif
