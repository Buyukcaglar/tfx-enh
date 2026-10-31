/* The user's files on Windows (W07): import into the user store, export
 * from it, and recovery of an interrupted write.
 *
 * An import takes a file the game writes, by its DOS name (host/
 * userfiles.h: TFX.CFG, TFX.HS, TFXSAV.<n>, TFXSAV.<n>L, VIDEO.REC), checks
 * it with the recovered readers (a pilot against the flow script the game
 * runs, C:\TFX\cd_demo.par) and writes it to the store by the store's safe
 * write (win/content.h): a file that fails the check, or a write that
 * fails, leaves the store's file as it was. The game folder is never
 * written.
 *
 * An export copies every file of the store (the game's subfolders kept)
 * to a folder outside both the game folder and the store, each by a
 * temporary file moved over the old one.
 *
 * The recovery runs before the game or the settings view: a NAME.new the
 * store's write left (the process ended between the write and the move)
 * is removed. The move is the commit, so NAME, when there is one, is the
 * last good file. */
#ifndef TFX_WIN_USERIO_H
#define TFX_WIN_USERIO_H

#include <stddef.h>
#include <wchar.h>

#include "win/content.h"

typedef void (*tfx_win_userio_log)(const char *line);

/* 1 imported (or already the store's), 0 refused; why: the reason or the
 * check's notes */
int tfx_win_import(const tfx_win_content *c, const wchar_t *file, char *why, size_t cap);

/* the files exported, -1 refused or failed (logged) */
int tfx_win_export(const tfx_win_content *c, const wchar_t *dir, tfx_win_userio_log log);

/* the leftovers removed */
int tfx_win_recover(const tfx_win_content *c, tfx_win_userio_log log);

#endif
