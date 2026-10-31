/* The content profile (M02): is the selected folder the supported
 * original release? The installed files of the Ocean CD edition (1995)
 * are known by size and SHA-256 (analysis/env-macos/custody-after.json);
 * the three the core cannot start without (DID.DAT, TFX.DAT and
 * START.EXE, whose device blocks the music uses) must match, the others
 * are only counted. Files the game writes (TFX.CFG, the pilots, saves)
 * go to the user store and are not checked. Nothing is written. */
#ifndef TFX_MAC_PROFILE_H
#define TFX_MAC_PROFILE_H

typedef enum tfx_mac_profile_status {
    TFX_MAC_BASELINE = 0, /* the supported release */
    TFX_MAC_NOT_FOLDER,   /* not a folder, or not readable */
    TFX_MAC_NOT_GAME,     /* no DID.DAT: not a TFX installation */
    TFX_MAC_INCOMPLETE,   /* DID.DAT, but a required file missing */
    TFX_MAC_UNREADABLE,   /* a required file cannot be read */
    TFX_MAC_UNSUPPORTED,  /* a required file differs from the release */
} tfx_mac_profile_status; /* (in order: the first problem in this order is reported) */

typedef struct tfx_mac_profile {
    tfx_mac_profile_status status;
    char message[1024]; /* what is wrong and what to do (empty for the baseline) */
    char edition[64];   /* the baseline's name */
    int matched, differ, absent; /* the known files: same, different, absent */
} tfx_mac_profile;

/* checks root; returns 1 for the supported release */
int tfx_mac_profile_check(const char *root, tfx_mac_profile *p);

/* the SHA-256 of a file as 64 hex digits; 0 when it cannot be read */
int tfx_mac_sha256_file(const char *path, char hex[65]);

#endif
