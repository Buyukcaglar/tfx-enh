/* The user's files (W07): the files the game writes (platform/storage.h),
 * recognised by their DOS names and checked by the recovered readers
 * before an import puts them in the user store, so a file the game would
 * refuse, reset or misread is not imported over a good one.
 *
 *   tfx.cfg       the game's reader 0x135b68 reads it only under 1024
 *                 bytes (else its built-in text); CONFIG's reader
 *                 accepts its keys without case, the game's only in
 *                 capitals: such keys are named (R09's import rule)
 *   tfx.hs        674 bytes whose checksum holds (else the game restores
 *                 its default table and rewrites the file)
 *   tfxsav.<n>    the pilot's save, and tfxsav.<n>l its log: decoded with
 *                 the variables' types of the flow script the game runs
 *                 (cd_demo.par), the whole file consumed, every box-stack
 *                 entry and the resume node inside the script's flows
 *   video.rec     playable as the player checks it (0x15564c: frames and
 *                 the stored size), the shape table decoding at its
 *                 offset */
#ifndef TFX_HOST_USERFILES_H
#define TFX_HOST_USERFILES_H

#include <stddef.h>
#include <stdint.h>

#include "formats/par.h"
#include "platform/storage.h"

/* the kind of a file by its name (no path), -1 for a name the game does
 * not write; *slot the pilot's number */
int tfx_userfile_kind(const char *name, int *slot);

/* 1 when the file is one the game reads as its own; why: the reason, or
 * on success the notes (keys the game would miss) */
int tfx_userfile_check(tfx_user_file kind, const uint8_t *data, size_t len, const tfx_par *script, char *why,
                       size_t cap);

#endif
