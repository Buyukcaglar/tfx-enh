/* FLAC frames (xiph.org's format: no stream header; the frame header,
 * the two channels' subframes, the CRCs), as the CHD's "cdfl" codec keeps
 * a hunk's CD audio (host/chd.h): 16-bit stereo at 44100 Hz, blocks of a
 * fixed size. The encoder writes constant, verbatim and fixed-predictor
 * subframes with Rice-coded residuals and picks the stereo decorrelation
 * per frame; the decoder reads what it writes (not LPC subframes: those
 * are other encoders'). Written for this program; no library. */
#ifndef TFX_HOST_FLAC_H
#define TFX_HOST_FLAC_H

#include <stddef.h>
#include <stdint.h>

/* n stereo samples (interleaved L R) as frames of block samples (the last
 * frame shorter); the bytes written, 0 when they do not fit in cap */
size_t tfx_flac_encode(const int16_t *pcm, int n, int block, uint8_t *out, size_t cap);

/* frames from in until n stereo samples are made; the bytes the frames
 * took, 0 on frames that are not valid */
size_t tfx_flac_decode(const uint8_t *in, size_t len, int16_t *pcm, int n);

#endif
