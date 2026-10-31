/* The Windows host's sound (W01, W05): the mixer (host/mixer.h) on a
 * thread feeding waveOut (16-bit stereo at 44100 Hz, BUFFERS blocks of
 * BLOCK frames queued), its MIDI to midiOut's default device (the
 * Microsoft GS synthesizer, or the device the user names), the CD's audio
 * from a CUE sheet's BINARY files or from the game's CHD (host/chd.h). Every event carries the host clock's
 * time; the stream plays DELAY_US behind it, so each block is rendered
 * with the time of its first sample and every event before it has
 * arrived. One lock guards the mixer (the game's calls come from the
 * main thread, the rendering from the audio thread). */
#ifndef TFX_WIN_AUDIO_H
#define TFX_WIN_AUDIO_H

#include <stdint.h>
#include <wchar.h>

#include "host/cdaudio.h"

typedef struct tfx_win_audio tfx_win_audio;

/* now_us: the host's clock; NULL when no output device opens (the game
 * then runs silent; its port writes are dropped) */
tfx_win_audio *tfx_win_audio_open(uint64_t (*now_us)(void *ctx), void *clock_ctx, int midi_device);
void tfx_win_audio_close(tfx_win_audio *a);
/* whether a MIDI output opened (without one the MPU-401's music is not
 * heard; the game still finds the interface) */
int tfx_win_audio_has_midi(const tfx_win_audio *a);

/* the offline output (validation, with the virtual clock): the same mixer
 * and delay, the stream written to a 16-bit stereo WAV file; pump after
 * each step renders every block the clock has passed; the stats: frames
 * written, the largest sample magnitude, samples at full scale */
tfx_win_audio *tfx_win_audio_open_wav(uint64_t (*now_us)(void *ctx), void *clock_ctx, const wchar_t *path);
void tfx_win_audio_pump(tfx_win_audio *a);
void tfx_win_audio_wav_stats(const tfx_win_audio *a, uint64_t *frames, int32_t *peak, uint64_t *clipped);

/* the CD image (a CUE sheet): 0 and a message when it cannot be used */
int tfx_win_audio_cd(tfx_win_audio *a, const wchar_t *cue, char *err, size_t cap);
/* the CD from the CHD the program made of the user's image (GAME\TFX.CHD) */
int tfx_win_audio_cd_chd(tfx_win_audio *a, const wchar_t *path, char *err, size_t cap);
const tfx_cd_image *tfx_win_audio_cd_image(const tfx_win_audio *a);

/* tfx_host's sound entries (ctx: the tfx_win_audio) */
void tfx_win_audio_port(void *ctx, uint64_t at, uint16_t port, uint8_t value);
void tfx_win_audio_pcm(void *ctx, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate);
int32_t tfx_win_audio_cd_track(void *ctx, int track);
void tfx_win_audio_cd_play(void *ctx, int32_t from, int32_t n);
void tfx_win_audio_cd_stop(void *ctx);
int tfx_win_audio_cd_heard(void *ctx);

/* levels 0 .. 256 */
void tfx_win_audio_volume(tfx_win_audio *a, int music, int fx, int cd);

#endif
