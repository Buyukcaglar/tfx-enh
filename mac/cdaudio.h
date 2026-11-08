/* The CD's audio from the user's own CD image (M05): a CUE sheet with one
 * BINARY file of raw 2352-byte sectors (the original release's layout:
 * a data track, then audio tracks). The core asks for a track's first
 * sector, plays n sectors from one, stops, and asks which track is heard
 * (include/tfx/core.h); after n sectors the position rests at the next
 * track's start, as a drive's would, which is how the game loops its
 * music. The audio (44.1 kHz, 16-bit stereo) is read through a memory
 * map of the image, or from the program's copy of it (TFX.CHD, read a
 * sector at a time). POSIX. */
#ifndef TFX_MAC_CDAUDIO_H
#define TFX_MAC_CDAUDIO_H

#include <stdint.h>

#define TFX_CD_TRACKS 99

typedef struct tfx_mac_cd {
    int tracks;                      /* the last track's number (0: no image) */
    int32_t start[TFX_CD_TRACKS + 2]; /* first sector by track (1-based); start[tracks + 1] the end */
    uint8_t audio[TFX_CD_TRACKS + 1]; /* 1 for an audio track */
    const uint8_t *image;            /* the map */
    long long size;
    int32_t pos, end;                /* the sector played and the one to stop before */
    uint32_t sample;                 /* the 44.1 kHz frame within pos's sector */
    double frac;                     /* the resampler's position between frames */
    int playing;
    char error[256];
    /* the program's copy of the disc (TFX.CHD, pack.h) in the map's place */
    struct tfx_chd *chd;
    int chd_fd;
    int32_t sector_at;            /* the sector in sector (-1 none) */
    uint8_t sector[2352];
} tfx_mac_cd;

/* opens the image a CUE sheet names; 1 on success (else error says why) */
int tfx_mac_cd_open(tfx_mac_cd *cd, const char *cue_path);
/* opens the program's copy of the disc (host/chd.h); 1 on success */
int tfx_mac_cd_open_chd(tfx_mac_cd *cd, const char *chd_path);
void tfx_mac_cd_close(tfx_mac_cd *cd);
/* 1 when the image is the supported release's disc (TFX, Ocean CD, 1995:
 * a data track and 14 audio tracks at its table of contents' sectors,
 * 257010 sectors in all); else 0 and error says how it differs */
int tfx_mac_cd_profile(tfx_mac_cd *cd);

int32_t tfx_mac_cd_track(const tfx_mac_cd *cd, int track);
void tfx_mac_cd_play(tfx_mac_cd *cd, int32_t from, int32_t n);
void tfx_mac_cd_stop(tfx_mac_cd *cd);
int tfx_mac_cd_heard(const tfx_mac_cd *cd);
/* adds frames of stereo audio at rate Hz into out (interleaved), scaled by gain */
void tfx_mac_cd_render(tfx_mac_cd *cd, float *out, int frames, int rate, float gain);

#endif
