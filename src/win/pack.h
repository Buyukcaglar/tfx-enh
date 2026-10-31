/* The program's own copies of the user's content, beside the program in
 * GAME (else %LOCALAPPDATA%\TFX\GAME, when the program's folder cannot be
 * written): the install folder's files the game uses (the content
 * profile's, host/profile.h) as TFX.ZIP (host/zip.h), and the CD image
 * (a CUE sheet and its BINARY files) as TFX.CHD (host/chd.h). Each is
 * written to NAME.new and moved over NAME in one step, so a failed or
 * stopped copy leaves no half file under the name. The user's originals
 * are only read. */
#ifndef TFX_WIN_PACK_H
#define TFX_WIN_PACK_H

#include <stddef.h>
#include <wchar.h>

/* progress: done of total, from the working thread */
typedef void (*tfx_win_pack_progress)(void *ctx, unsigned done, unsigned total);

/* the folder's profile files into the zip; 0 and why in err */
int tfx_win_pack_zip(const wchar_t *game_dir, const wchar_t *zip, char *err, size_t cap);
/* whether the zip holds the folder's profile files at their sizes (the
 * same folder packed already) */
int tfx_win_pack_zip_matches(const wchar_t *game_dir, const wchar_t *zip);

/* the sheet's disc into the CHD, its hunks compressed on every processor;
 * 0 and why in err */
int tfx_win_pack_chd(const wchar_t *cue, const wchar_t *chd, tfx_win_pack_progress progress, void *ctx, char *err,
                     size_t cap);
/* whether the CHD holds the sheet's disc (its tracks where the sheet has
 * them) */
int tfx_win_pack_chd_matches(const wchar_t *cue, const wchar_t *chd);

/* work(arg, progress, ctx) on a thread while a small window shows text
 * and the progress (no window when hidden); work's result */
int tfx_win_pack_run(void *instance, const wchar_t *text, int hidden,
                     int (*work)(void *arg, tfx_win_pack_progress progress, void *ctx), void *arg);

#endif
