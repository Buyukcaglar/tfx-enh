/* The CD image as one compressed file, GAME\TFX.CHD beside the program:
 * MAME's CHD format, version 5, as chdman makes a CD's (hunks of 8
 * frames, a frame the 2352-byte sector and 96 bytes of subcode, here
 * none; the audio's samples big-endian; each track's frames padded to a
 * multiple of 4; a CHT2 metadata line a track) and its "cdzl" codec (each
 * hunk's sectors and subcode deflated apart: host/deflate.h) and "cdfl"
 * codec (the sectors as FLAC frames, the subcode deflated: host/flac.h)
 * for the hunks of audio. Made once from the user's CUE sheet; the CD's
 * music is then played from it. Other CHD readers (chdman, libchdr) read
 * what this writes; this reads what it writes (cdzl and cdfl hunks,
 * stored hunks, a hunk the same as an earlier one), not chdman's cdlz
 * hunks or its FLAC's LPC subframes.
 *
 * The writer is portable and has no threads: the host compresses hunks on
 * as many threads as it likes (tfx_chd_compress), then gives them to the
 * writer in order, writing what it returns one after the other. */
#ifndef TFX_HOST_CHD_H
#define TFX_HOST_CHD_H

#include <stddef.h>
#include <stdint.h>

#include "host/cdaudio.h"
#include "host/deflate.h"

#define TFX_CHD_FRAME 2448 /* a sector and its subcode */
#define TFX_CHD_HUNK (8 * TFX_CHD_FRAME)
#define TFX_CHD_HEADER 124

/* the disc as the CHD lays it out */
typedef struct tfx_chd_layout {
    int n_tracks;
    struct {
        int number, audio;
        int32_t lba, frames; /* on the disc */
        uint32_t chd_frame;  /* its first frame in the CHD */
    } track[TFX_CD_TRACKS];
    uint32_t frames; /* the CHD's (the padding's in) */
    uint32_t hunks;
} tfx_chd_layout;

/* from a parsed sheet */
void tfx_chd_layout_cd(tfx_chd_layout *l, const tfx_cd_image *img);

/* hunk h's raw bytes (TFX_CHD_HUNK) from the sheet's files, read through
 * read(ctx, file, offset, buf, n) (bytes read): 0 on a short read, else 1
 * (data only) or 2 (an audio frame in it) */
int tfx_chd_raw_hunk(const tfx_chd_layout *l, const tfx_cd_image *img, uint32_t h, uint8_t *raw,
                     size_t (*read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n), void *ctx);

/* a raw hunk as the file keeps it into out (TFX_CHD_HUNK bytes at most),
 * kind as tfx_chd_raw_hunk returned: its length; *codec 0 (cdzl), 1
 * (cdfl) or 4 (stored, when neither gains) */
size_t tfx_chd_compress(tfx_deflater *d, const uint8_t *raw, int kind, uint8_t *out, int *codec);

typedef struct tfx_chd_writer tfx_chd_writer;

/* The file: TFX_CHD_HEADER bytes (any: the header is written last), then
 * each hunk as tfx_chd_compress made it (and given to tfx_chd_put with
 * its raw bytes, in order), then tfx_chd_finish's tail; then the header
 * over the first bytes. */
tfx_chd_writer *tfx_chd_writer_new(const tfx_chd_layout *l);
void tfx_chd_put(tfx_chd_writer *w, const uint8_t *raw, size_t n, int codec);
/* the map and the metadata (*tail, *n: valid until freed) and the header;
 * 0 out of memory */
int tfx_chd_finish(tfx_chd_writer *w, const uint8_t **tail, size_t *n, uint8_t header[TFX_CHD_HEADER]);
void tfx_chd_writer_free(tfx_chd_writer *w);

/* The reader: read(ctx, offset, buf, n) reads the file's bytes (1 all
 * read). */
typedef struct tfx_chd {
    int (*read)(void *ctx, uint64_t offset, void *buf, size_t n);
    void *ctx;
    uint32_t hunks;
    uint8_t *map; /* 12 bytes a hunk: codec, length (3), offset (6), CRC-16 (2) */
    tfx_chd_layout layout;
    int32_t end_lba;
    uint8_t *packed, *hunk; /* the hunk last read */
    uint32_t hunk_at;       /* its number, ~0 none */
} tfx_chd;

/* 0 and why in err when the file is not a CD CHD this reads */
int tfx_chd_open(tfx_chd *c, int (*read)(void *ctx, uint64_t offset, void *buf, size_t n), void *ctx, char *err,
                 size_t cap);
void tfx_chd_close(tfx_chd *c);
/* hunk h's raw bytes into out; 0 on a damaged hunk (its CRC checked) */
int tfx_chd_read_hunk(tfx_chd *c, uint32_t h, uint8_t *out);
/* The disc as the CD player takes it (host/cdaudio.h): one file, the
 * disc's sectors in order (a sector's offset is its LBA x 2352). */
void tfx_chd_cd_image(const tfx_chd *c, tfx_cd_image *img);
/* that file's bytes (the audio's samples little-endian again), as the
 * player's read; the bytes read */
size_t tfx_chd_cd_read(tfx_chd *c, uint64_t offset, uint8_t *buf, size_t n);

#endif
