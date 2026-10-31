#include "win/audio.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/chd.h"
#include "host/mixer.h"
#include "win/content.h"

#define RATE 44100
#define BLOCK 882 /* 20 ms */
#define BUFFERS 4
#define DELAY_US 120000 /* more than the queue's 80 ms */

struct tfx_win_audio {
    CRITICAL_SECTION lock;
    tfx_mixer *mixer;
    uint64_t (*now_us)(void *ctx);
    void *clock_ctx;
    HWAVEOUT wave;
    WAVEHDR hdr[BUFFERS];
    int16_t buf[BUFFERS][BLOCK * 2];
    HANDLE event, thread;
    volatile LONG stop;
    uint64_t t0;      /* the host time of the next block's first sample */
    uint64_t written; /* frames handed to the device */
    HMIDIOUT midi;
    MIDIHDR sysex;
    uint8_t sysex_buf[512];
    /* the CD */
    tfx_cd_image cd;
    int cd_ok;
    wchar_t cd_dir[260];
    HANDLE cd_file[TFX_CD_FILES];
    tfx_chd chd; /* the CD from GAME\TFX.CHD (tfx_win_audio_cd_chd) */
    HANDLE chd_file;
    /* the offline output (tfx_win_audio_open_wav): the stream to a file,
     * rendered as the clock passes (tfx_win_audio_pump) */
    FILE *wav;
    uint64_t wav_frames, wav_base;
    int32_t wav_peak;
    uint64_t wav_clipped;
};

static void midi_out(void *ctx, const uint8_t *msg, size_t n) {
    tfx_win_audio *a = ctx;
    if (!a->midi || !n)
        return;
    if (msg[0] == 0xf0) {
        if (n > sizeof a->sysex_buf)
            return;
        if (a->sysex.dwFlags & MHDR_PREPARED) { /* the last one done first */
            while (!(a->sysex.dwFlags & MHDR_DONE))
                Sleep(1);
            midiOutUnprepareHeader(a->midi, &a->sysex, sizeof a->sysex);
        }
        memcpy(a->sysex_buf, msg, n);
        memset(&a->sysex, 0, sizeof a->sysex);
        a->sysex.lpData = (LPSTR)a->sysex_buf;
        a->sysex.dwBufferLength = a->sysex.dwBytesRecorded = (DWORD)n;
        if (midiOutPrepareHeader(a->midi, &a->sysex, sizeof a->sysex) == MMSYSERR_NOERROR)
            midiOutLongMsg(a->midi, &a->sysex, sizeof a->sysex);
        return;
    }
    DWORD m = 0;
    for (size_t i = 0; i < n && i < 3; i++)
        m |= (DWORD)msg[i] << (8 * i);
    midiOutShortMsg(a->midi, m);
}

static size_t cd_read(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n) {
    tfx_win_audio *a = ctx;
    if (file < 0 || file >= a->cd.n_files)
        return 0;
    if (!a->cd_file[file]) {
        wchar_t path[520];
        int k = MultiByteToWideChar(CP_ACP, 0, a->cd.file[file], -1, NULL, 0);
        if (k <= 0 || wcslen(a->cd_dir) + 1 + (size_t)k > 520)
            return 0;
        wcscpy(path, a->cd_dir);
        wcscat(path, L"\\");
        MultiByteToWideChar(CP_ACP, 0, a->cd.file[file], -1, path + wcslen(path), k);
        a->cd_file[file] = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }
    HANDLE h = a->cd_file[file];
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    OVERLAPPED o = {0};
    o.Offset = (DWORD)offset, o.OffsetHigh = (DWORD)(offset >> 32);
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)n, &got, &o))
        return 0;
    return got;
}

static int chd_file_read(void *ctx, uint64_t offset, void *buf, size_t n) {
    OVERLAPPED o = {0};
    o.Offset = (DWORD)offset, o.OffsetHigh = (DWORD)(offset >> 32);
    DWORD got = 0;
    return ReadFile(ctx, buf, (DWORD)n, &got, &o) && got == n;
}

static size_t chd_read(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n) {
    tfx_win_audio *a = ctx;
    return file ? 0 : tfx_chd_cd_read(&a->chd, offset, buf, n);
}

int tfx_win_audio_cd_chd(tfx_win_audio *a, const wchar_t *path, char *err, size_t cap) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, cap, "the CD's CHD cannot be opened");
        return 0;
    }
    EnterCriticalSection(&a->lock);
    a->cd_ok = tfx_chd_open(&a->chd, chd_file_read, h, err, cap);
    if (a->cd_ok) {
        a->chd_file = h;
        tfx_chd_cd_image(&a->chd, &a->cd);
        tfx_mixer_set_cd(a->mixer, &a->cd, chd_read, a);
    } else
        CloseHandle(h);
    LeaveCriticalSection(&a->lock);
    return a->cd_ok;
}

static void fill(tfx_win_audio *a, int i) {
    EnterCriticalSection(&a->lock);
    tfx_mixer_render(a->mixer, a->buf[i], BLOCK, a->t0);
    LeaveCriticalSection(&a->lock);
    a->t0 += (uint64_t)BLOCK * 1000000 / RATE;
    a->written += BLOCK;
    a->hdr[i].dwFlags &= ~(DWORD)WHDR_DONE;
    waveOutWrite(a->wave, &a->hdr[i], sizeof a->hdr[i]);
}

/* the stream's time: DELAY_US behind the clock, from 0 (the clock starts
 * at 0, so the first DELAY_US are silence rather than a wrapped time) */
static uint64_t behind(const tfx_win_audio *a) {
    uint64_t now = a->now_us(a->clock_ctx);
    return now > DELAY_US ? now - DELAY_US : 0;
}

static DWORD WINAPI run(LPVOID arg) {
    tfx_win_audio *a = arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (!a->stop) {
        WaitForSingleObject(a->event, 50);
        /* the device's clock against the host's: a block behind by more
         * than one (a stall) or ahead skips the time to now - DELAY */
        uint64_t want = behind(a) + (uint64_t)BUFFERS * BLOCK * 1000000 / RATE;
        if (a->t0 + 2 * (uint64_t)BLOCK * 1000000 / RATE < want || a->t0 > want + 2 * (uint64_t)BLOCK * 1000000 / RATE)
            a->t0 = want;
        for (int i = 0; i < BUFFERS && !a->stop; i++)
            if (a->hdr[i].dwFlags & WHDR_DONE)
                fill(a, i);
    }
    return 0;
}

tfx_win_audio *tfx_win_audio_open(uint64_t (*now_us)(void *ctx), void *clock_ctx, int midi_device) {
    tfx_win_audio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    InitializeCriticalSection(&a->lock);
    a->now_us = now_us, a->clock_ctx = clock_ctx;
    a->mixer = tfx_mixer_new(RATE);
    a->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX f = {WAVE_FORMAT_PCM, 2, RATE, RATE * 4, 4, 16, 0};
    if (!a->mixer || !a->event ||
        waveOutOpen(&a->wave, WAVE_MAPPER, &f, (DWORD_PTR)a->event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        tfx_win_audio_close(a);
        return NULL;
    }
    if (midiOutOpen(&a->midi, midi_device < 0 ? MIDI_MAPPER : (UINT)midi_device, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        a->midi = NULL;
    tfx_mixer_set_midi(a->mixer, midi_out, a);
    a->t0 = behind(a);
    for (int i = 0; i < BUFFERS; i++) {
        a->hdr[i].lpData = (LPSTR)a->buf[i];
        a->hdr[i].dwBufferLength = sizeof a->buf[i];
        waveOutPrepareHeader(a->wave, &a->hdr[i], sizeof a->hdr[i]);
        fill(a, i);
    }
    a->thread = CreateThread(NULL, 0, run, a, 0, NULL);
    if (!a->thread) {
        tfx_win_audio_close(a);
        return NULL;
    }
    return a;
}

static void wav_header(tfx_win_audio *a) {
    uint32_t data = (uint32_t)(a->wav_frames * 4), riff = 36 + data;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
    uint32_t rate = RATE, bps = RATE * 4;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &bps, 4);
    h[32] = 4, h[34] = 16;
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fseek(a->wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, a->wav);
    fseek(a->wav, 0, SEEK_END);
}

tfx_win_audio *tfx_win_audio_open_wav(uint64_t (*now_us)(void *ctx), void *clock_ctx, const wchar_t *path) {
    tfx_win_audio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    InitializeCriticalSection(&a->lock);
    a->now_us = now_us, a->clock_ctx = clock_ctx;
    a->mixer = tfx_mixer_new(RATE);
    a->wav = _wfopen(path, L"wb");
    if (!a->mixer || !a->wav) {
        tfx_win_audio_close(a);
        return NULL;
    }
    wav_header(a);
    a->t0 = a->wav_base = behind(a);
    return a;
}

void tfx_win_audio_pump(tfx_win_audio *a) {
    if (!a || !a->wav)
        return;
    uint64_t until = behind(a);
    int16_t buf[BLOCK * 2];
    while (a->t0 + (uint64_t)BLOCK * 1000000 / RATE <= until) {
        EnterCriticalSection(&a->lock);
        /* (each block's time from the frames so far: no rounding drift) */
        tfx_mixer_render(a->mixer, buf, BLOCK, a->t0);
        LeaveCriticalSection(&a->lock);
        for (int i = 0; i < BLOCK * 2; i++) {
            int32_t v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > a->wav_peak)
                a->wav_peak = v;
            a->wav_clipped += v >= 32767;
        }
        fwrite(buf, sizeof buf[0], BLOCK * 2, a->wav);
        a->wav_frames += BLOCK;
        a->t0 = a->wav_base + a->wav_frames * 1000000 / RATE;
    }
}

void tfx_win_audio_wav_stats(const tfx_win_audio *a, uint64_t *frames, int32_t *peak, uint64_t *clipped) {
    *frames = a->wav_frames, *peak = a->wav_peak, *clipped = a->wav_clipped;
}

int tfx_win_audio_has_midi(const tfx_win_audio *a) { return a->midi != NULL; }

void tfx_win_audio_close(tfx_win_audio *a) {
    if (!a)
        return;
    if (a->wav) {
        wav_header(a);
        fclose(a->wav);
    }
    if (a->thread) {
        InterlockedExchange(&a->stop, 1);
        SetEvent(a->event);
        WaitForSingleObject(a->thread, INFINITE);
        CloseHandle(a->thread);
    }
    if (a->wave) {
        waveOutReset(a->wave);
        for (int i = 0; i < BUFFERS; i++)
            if (a->hdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(a->wave, &a->hdr[i], sizeof a->hdr[i]);
        waveOutClose(a->wave);
    }
    if (a->midi) {
        midiOutReset(a->midi);
        if (a->sysex.dwFlags & MHDR_PREPARED)
            midiOutUnprepareHeader(a->midi, &a->sysex, sizeof a->sysex);
        midiOutClose(a->midi);
    }
    for (int i = 0; i < TFX_CD_FILES; i++)
        if (a->cd_file[i] && a->cd_file[i] != INVALID_HANDLE_VALUE)
            CloseHandle(a->cd_file[i]);
    if (a->chd_file) {
        tfx_chd_close(&a->chd);
        CloseHandle(a->chd_file);
    }
    if (a->event)
        CloseHandle(a->event);
    tfx_mixer_free(a->mixer);
    DeleteCriticalSection(&a->lock);
    free(a);
}

static long long cue_file_size(void *ctx, const char *name) {
    tfx_win_audio *a = ctx;
    wchar_t path[520];
    int k = MultiByteToWideChar(CP_ACP, 0, name, -1, NULL, 0);
    if (k <= 0 || wcslen(a->cd_dir) + 1 + (size_t)k > 520)
        return -1;
    wcscpy(path, a->cd_dir);
    wcscat(path, L"\\");
    MultiByteToWideChar(CP_ACP, 0, name, -1, path + wcslen(path), k);
    return tfx_win_file_size(path);
}

int tfx_win_audio_cd(tfx_win_audio *a, const wchar_t *cue, char *err, size_t cap) {
    HANDLE h = CreateFileW(cue, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        strncpy(err, "the CD sheet cannot be opened", cap - 1), err[cap - 1] = 0;
        return 0;
    }
    char text[16384];
    DWORD n = 0;
    BOOL ok = ReadFile(h, text, sizeof text, &n, NULL);
    CloseHandle(h);
    if (!ok || n == sizeof text) {
        strncpy(err, "the CD sheet cannot be read", cap - 1), err[cap - 1] = 0;
        return 0;
    }
    wcsncpy(a->cd_dir, cue, 259);
    wchar_t *slash = wcsrchr(a->cd_dir, L'\\');
    if (!slash)
        slash = wcsrchr(a->cd_dir, L'/');
    if (slash)
        *slash = 0;
    else
        wcscpy(a->cd_dir, L".");
    EnterCriticalSection(&a->lock);
    a->cd_ok = tfx_cdi_parse(&a->cd, text, n, cue_file_size, a, err, cap);
    if (a->cd_ok)
        tfx_mixer_set_cd(a->mixer, &a->cd, cd_read, a);
    LeaveCriticalSection(&a->lock);
    return a->cd_ok;
}

const tfx_cd_image *tfx_win_audio_cd_image(const tfx_win_audio *a) { return a->cd_ok ? &a->cd : NULL; }

void tfx_win_audio_port(void *ctx, uint64_t at, uint16_t port, uint8_t value) {
    tfx_win_audio *a = ctx;
    EnterCriticalSection(&a->lock);
    tfx_mixer_port(a->mixer, at, port, value);
    LeaveCriticalSection(&a->lock);
}

void tfx_win_audio_pcm(void *ctx, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate) {
    tfx_win_audio *a = ctx;
    EnterCriticalSection(&a->lock);
    tfx_mixer_pcm(a->mixer, at, voice, data, n, rate);
    LeaveCriticalSection(&a->lock);
}

int32_t tfx_win_audio_cd_track(void *ctx, int track) {
    tfx_win_audio *a = ctx;
    return a->cd_ok ? tfx_cdi_track_start(&a->cd, track) : -1;
}

void tfx_win_audio_cd_play(void *ctx, int32_t from, int32_t n) {
    tfx_win_audio *a = ctx;
    if (!a->cd_ok)
        return;
    EnterCriticalSection(&a->lock);
    tfx_mixer_cd_play(a->mixer, a->now_us(a->clock_ctx), from, n);
    LeaveCriticalSection(&a->lock);
}

void tfx_win_audio_cd_stop(void *ctx) {
    tfx_win_audio *a = ctx;
    EnterCriticalSection(&a->lock);
    tfx_mixer_cd_stop(a->mixer, a->now_us(a->clock_ctx));
    LeaveCriticalSection(&a->lock);
}

int tfx_win_audio_cd_heard(void *ctx) {
    tfx_win_audio *a = ctx;
    EnterCriticalSection(&a->lock);
    int t = tfx_mixer_cd_heard(a->mixer);
    LeaveCriticalSection(&a->lock);
    return t;
}

void tfx_win_audio_volume(tfx_win_audio *a, int music, int fx, int cd) {
    EnterCriticalSection(&a->lock);
    a->mixer->vol_music = music, a->mixer->vol_fx = fx, a->mixer->vol_cd = cd;
    LeaveCriticalSection(&a->lock);
}
