/* The macOS host's files (M01): the user's original game folder, read
 * only, and the user store the game's writes go to (platform/storage.h),
 * both behind the DOS paths the core asks for (include/tfx/core.h).
 *
 * A DOS path is taken below the game's folder: "C:\TFX\" (the DID
 * variable the host gives) or a drive is cut, leading backslashes too, and
 * the rest is matched component by component, case-insensitively, as DOS
 * would. A read looks in the store first (the game's own writes: tfx.cfg,
 * tfx.hs, the pilots, video.rec), then in the originals. A write goes to
 * the store only, by the path's last component, written to a temporary
 * file and renamed over the old one, so a failed write keeps the previous
 * contents. A search lists the store's matches, then the originals' not
 * already listed. The originals can instead be the program's own copy of
 * them (pack.h: TFX.ZIP, inflated in memory). POSIX only: the Windows host has its own. */
#ifndef TFX_MAC_FILES_H
#define TFX_MAC_FILES_H

#include <stddef.h>
#include <stdint.h>

#define TFX_MAC_PATH 1024

struct tfx_mac_pack;

typedef struct tfx_mac_files {
    char root[TFX_MAC_PATH];  /* the original game folder (never written) */
    char store[TFX_MAC_PATH]; /* the user store (created when missing) */
    /* the program's copy of the game's files (pack.h), read in the
     * originals' place when root is empty; NULL: none */
    const struct tfx_mac_pack *pack;
} tfx_mac_files;

/* the DID variable the game's paths start from */
#define TFX_MAC_DID "C:\\TFX"

/* the DOS path's file under dir (out: the host path); 1 found, else 0 (with
 * last_may_miss, a missing last component is kept as spelled) */
int tfx_mac_resolve(const char *dir, const char *dos, char *out, size_t n, int last_may_miss);
/* the DOS pattern (* and ?, case-insensitive) against a name */
int tfx_mac_match(const char *pattern, const char *name);

uint32_t tfx_mac_size(const tfx_mac_files *f, const char *dos);
int tfx_mac_read(const tfx_mac_files *f, const char *dos, uint8_t *buf, uint32_t cap);
int tfx_mac_write(const tfx_mac_files *f, const char *dos, const uint8_t *buf, uint32_t n);
/* the same, the version replaced kept in the store's .history folder (M07,
 * userdata.h): the game's writes, the settings', imports */
#define TFX_MAC_KEPT 3
int tfx_mac_write_kept(const tfx_mac_files *f, const char *dos, const uint8_t *buf, uint32_t n);
int tfx_mac_find(const tfx_mac_files *f, const char *pattern, uint16_t attr, uint32_t k, char name[13]);
/* the store created and written to (a hidden probe, removed) before the
 * game runs (M08: the game finds a store it cannot write only when it
 * next saves, and stops there); 1 ready, else 0 and why */
int tfx_mac_store_ready(const tfx_mac_files *f, char *why, size_t cap);

#endif
