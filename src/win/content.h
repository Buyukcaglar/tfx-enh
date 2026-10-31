/* The Windows host's files (W01, W02): the game folder the user chose,
 * only read, under a writable user store (%LOCALAPPDATA%\TFX\store by
 * default) that holds every file the game writes. A DOS path the core
 * asks for (host/dospath.h) is looked up in the store first, then in the
 * game folder, without case (the file systems Windows uses compare names
 * without case). A write goes to a temporary file in the store, flushed,
 * then moved over the old one in one step (MoveFileEx, replace
 * existing), so a failed write leaves the last good file (the temporary,
 * NAME.new, is never an 8.3 name, so no find lists it). A find lists
 * both folders' 8.3 names (a store file hiding the game folder's of the
 * same name), "." and ".." first for a directory search (the game folder
 * is C:\TFX, a subdirectory), the rest sorted by name.
 *
 * The game's files can instead come from the program's own copy of the
 * folder, GAME\TFX.ZIP (host/zip.h): read whole into memory, each file
 * inflated when first asked for and kept; its folders are listed as the
 * folder's are. The store is the same either way. */
#ifndef TFX_WIN_CONTENT_H
#define TFX_WIN_CONTENT_H

#include <stdint.h>
#include <wchar.h>

#include "host/zip.h"

typedef struct tfx_win_content {
    wchar_t game[260];  /* the original folder, no trailing separator (or the zip) */
    wchar_t store[260]; /* the user store */
    /* the zip (tfx_win_content_open_zip): its bytes, its entries, each
     * entry's bytes once inflated */
    uint8_t *zip_data;
    tfx_zip zip;
    uint8_t **zip_files;
} tfx_win_content;

/* 0 when either folder is missing (the store is created) */
int tfx_win_content_open(tfx_win_content *c, const wchar_t *game, const wchar_t *store);
/* the game's files from the zip; 0 when it cannot be read or is not one */
int tfx_win_content_open_zip(tfx_win_content *c, const wchar_t *zip, const wchar_t *store);
void tfx_win_content_close(tfx_win_content *c);

/* A game file (not the store's) by its DOS path: its size (-1 missing),
 * its bytes (malloc'ed; NULL missing), from the folder or the zip. */
long long tfx_win_content_game_size(const tfx_win_content *c, const char *dos_path);
uint8_t *tfx_win_content_game_load(const tfx_win_content *c, const char *dos_path, size_t *n);

/* A DOS path's file: its Windows path in the store (store 1) or the game
 * folder (not with a zip: tfx_win_content_game_load); 0 when the path is
 * outside the game's drive or missing. */
int tfx_win_content_locate(const tfx_win_content *c, const char *dos_path, int store, wchar_t *out, size_t cap);

uint32_t tfx_win_content_size(void *ctx, const char *path);
int tfx_win_content_read(void *ctx, const char *path, uint8_t *buf, uint32_t cap);
int tfx_win_content_write(void *ctx, const char *path, const uint8_t *buf, uint32_t n);
int tfx_win_content_find(void *ctx, const char *pattern, uint16_t attr, uint32_t k, char name[13]);

/* A file's size by its Windows path; -1 missing or a folder */
long long tfx_win_file_size(const wchar_t *path);

#endif
