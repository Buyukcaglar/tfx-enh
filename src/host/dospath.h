/* DOS paths for a native host (W02): what the game asks for, by the path
 * DOS would have opened, mapped onto a folder of the original's files.
 *
 * The game runs with the game folder as the current directory and the
 * DID variable naming it (TFX.BAT's `start cd_demo`; the core asks for
 * "%DID%\tfx.hs" and the like); the host sets DID to TFX_DOS_ROOT. A
 * path is the folder's when it is relative (".\" and repeated
 * separators dropped) or names TFX_DOS_ROOT (any case); anything else
 * (another drive, "..") is outside and missing. Names are compared
 * without case and kept upper-case, the way DOS stored them. */
#ifndef TFX_HOST_DOSPATH_H
#define TFX_HOST_DOSPATH_H

#include <stddef.h>

#define TFX_DOS_ROOT "C:\\TFX"

/* The folder-relative path ("MISSIONS\MISS830.MIS", "" for the folder
 * itself) into out; 0 when the path is outside or does not fit. */
int tfx_dos_relative(const char *dos_path, char *out, size_t cap);

/* A name DOS could hold: 1 .. 8 characters, optionally '.' and 1 .. 3, of
 * the characters DOS allows (no blanks, wildcards or separators). */
int tfx_dos_83(const char *name);

/* DOS's FindFirst match of an 8.3 name against a pattern ('?' any one
 * character or none at the end of a part, '*' the rest of the part;
 * "*.*" everything), without case. */
int tfx_dos_match(const char *pattern, const char *name);

/* Splits "DIR\SUB\*.SSS" into the folder part ("DIR\SUB", "" for none)
 * and the last component ("*.SSS"); 0 when either does not fit. */
int tfx_dos_split(const char *rel, char *dir, size_t dir_cap, char *last, size_t last_cap);

#endif
