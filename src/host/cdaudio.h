/* The CD's audio from the user's disc image (W05): a CUE sheet with its
 * BINARY files of raw 2352-byte sectors, played as the drive would play
 * Red Book audio (44100 Hz, 16-bit, stereo, little-endian: 588 frames a
 * sector). The core's MSCDEX model (core/bind_sound.c) asks a track's
 * start (sectors from 00:02:00, i.e. the LBA; it adds the 150 itself),
 * plays n sectors from a sector, stops, and asks the track heard now.
 *
 * The supplied disc (reference/_cd) has a data track and 14 audio tracks;
 * the game plays tracks 3 .. 9 (R08). Only the image's track table and the
 * sectors played are read; nothing is copied out. */
#ifndef TFX_HOST_CDAUDIO_H
#define TFX_HOST_CDAUDIO_H

#include <stddef.h>
#include <stdint.h>

#define TFX_CD_TRACKS 99
#define TFX_CD_FILES 8
#define TFX_CD_SECTOR 2352

typedef struct tfx_cd_track {
    int number; /* 1 .. 99 */
    int audio;  /* AUDIO (else a data track) */
    int file;   /* index into the files */
    uint64_t offset; /* INDEX 01's byte offset in its file */
    int32_t lba;     /* INDEX 01 on the disc */
} tfx_cd_track;

typedef struct tfx_cd_image {
    int n_tracks, n_files;
    tfx_cd_track track[TFX_CD_TRACKS];
    char file[TFX_CD_FILES][260]; /* as the sheet names them */
    uint64_t file_size[TFX_CD_FILES];
    int32_t file_lba[TFX_CD_FILES]; /* each file's first sector on the disc */
    int32_t end_lba;                /* the disc's last sector + 1 */
} tfx_cd_image;

/* Parses a CUE sheet (text, n bytes). Each FILE's size comes from
 * file_size(ctx, name) (-1: missing). Returns 0 and a message in err on a
 * sheet it cannot use (no tracks, a file missing, a mode other than
 * 2352-byte sectors). */
int tfx_cdi_parse(tfx_cd_image *img, const char *text, size_t n,
                 long long (*file_size)(void *ctx, const char *name), void *ctx, char *err, size_t err_cap);

/* The track's start (LBA), -1 when there is none. */
int32_t tfx_cdi_track_start(const tfx_cd_image *img, int number);
/* The track holding a sector (0 none). */
int tfx_cdi_track_at(const tfx_cd_image *img, int32_t lba);

/* The player: plays sectors of audio tracks (a data track's sectors are
 * silence, as a drive plays none) until n are played. */
typedef struct tfx_cd_player {
    const tfx_cd_image *img;
    int32_t at, end; /* the next sector, the last + 1; at == end: stopped */
    int frame;       /* the next frame (0 .. 587) of the sector at */
} tfx_cd_player;

void tfx_cdi_play(tfx_cd_player *p, const tfx_cd_image *img, int32_t from, int32_t n);
void tfx_cdi_stop(tfx_cd_player *p);
/* The track heard now (0: stopped). */
int tfx_cdi_heard(const tfx_cd_player *p);

/* Renders up to frames stereo frames at 44100 Hz into out (interleaved
 * L R), reading sectors through read(ctx, file, offset, buf, n) (returns
 * bytes read); returns the frames written (fewer: the play ended, the
 * rest left alone). */
int tfx_cdi_render(tfx_cd_player *p, int16_t *out, int frames,
                  size_t (*read)(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n), void *ctx);

#endif
