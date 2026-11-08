/* The program's own copies of the user's content (an addition, at the
 * user's request, as the Windows host's GAME folder: src/win/pack.h), in
 * ~/Library/Application Support/TFX/Content: the install folder's files
 * the game uses (the content profile's, host/profile.h) as TFX.ZIP
 * (host/zip.h), and the CD image (a CUE sheet and its BINARY files) as
 * TFX.CHD (host/chd.h). Each is written to NAME.new and renamed over NAME
 * in one step, so a failed or stopped copy leaves no half file under the
 * name. The user's originals are only read. POSIX (pthreads). */
#ifndef TFX_MAC_PACK_H
#define TFX_MAC_PACK_H

#include <stddef.h>
#include <stdint.h>

/* The zip's files, all inflated into memory when it is opened (14 MB),
 * looked up by their folder-relative path without case. */
typedef struct tfx_mac_pack {
    int count;
    char (*name)[260]; /* as the zip keeps them ('/' between folders) */
    uint8_t **data;
    uint32_t *size;
} tfx_mac_pack;

/* the zip read and every entry inflated (their CRCs checked); 0 and why
 * in err */
int tfx_mac_pack_load(tfx_mac_pack *p, const char *zip, char *err, size_t cap);
void tfx_mac_pack_free(tfx_mac_pack *p);
/* the entry for a path below the game folder ('\' or '/', "." parts
 * skipped, no case), -1 none */
int tfx_mac_pack_find(const tfx_mac_pack *p, const char *rel);

/* the folder's profile files into the zip; 0 and why in err */
int tfx_mac_pack_zip(const char *game_dir, const char *zip, char *err, size_t cap);
/* whether the zip holds the folder's profile files at their sizes (the
 * same folder packed already) */
int tfx_mac_pack_zip_matches(const char *game_dir, const char *zip);

/* progress: done of total, from the working thread */
typedef void (*tfx_mac_pack_progress)(void *ctx, unsigned done, unsigned total);

/* the sheet's disc into the CHD, its hunks compressed on every processor;
 * 0 and why in err */
int tfx_mac_pack_chd(const char *cue, const char *chd, tfx_mac_pack_progress progress, void *ctx, char *err,
                     size_t cap);
/* whether the CHD holds the sheet's disc (its tracks where the sheet has
 * them) */
int tfx_mac_pack_chd_matches(const char *cue, const char *chd);

#endif
