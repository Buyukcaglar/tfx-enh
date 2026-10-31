/* The macOS host's headless runner (M01): the session (session.c) on a
 * virtual clock (1 ms a wait, so a run is the same on every machine and
 * architecture), keys typed at given frames, a hash of every frame shown
 * (FNV-1a over the pixels and the DAC). Two slices of the universal build
 * giving the same hashes run the game the same.
 *
 *     tfx_headless <game folder> <store> <frames> [frame:scancode[,scancode..] ...]
 *
 * checks the folder's content profile first (profile.h: the supported
 * release, else the reason and exit status 3), prints "frame <n> <hash>"
 * every 100 frames and at the end, then the counts; the store is written
 * as the game writes (use a scratch one; one that cannot be written: the
 * reason and exit status 3, M08).
 * With TFX_HEADLESS_DUMP set to a folder, those frames are written there
 * too (frame_<n>.ppm). TFX_HEADLESS_PRESENT=n converts every n-th frame
 * for display as the app does, and TFX_HEADLESS_PUMP=1 steps through the
 * app's pump (returning at each frame): neither may change a hash (M03).
 * TFX_HEADLESS_STICK="x y z w buttons" connects a joystick reading those
 * counts and buttons throughout (M04). TFX_HEADLESS_AUDIO=1 renders the
 * mixer (the samples, the music devices, the CD) on the virtual clock,
 * 48 frames at 48 kHz a wait, and hashes it as 16-bit samples
 * (TFX_HEADLESS_WAV=<file> writes it); TFX_HEADLESS_CUE=<cue sheet> gives
 * the machine a CD (the image profile-checked: exit status 3) (M05).
 * Input on the virtual clock (M08: a screen waiting for input shows no
 * frames, so frame numbers cannot reach it): "@<ms>:scancode[,..]" types
 * at that many milliseconds after the start, "m<ms>:x,y,buttons" moves the
 * mouse (x 0 .. 639, y 0 .. 199), "v<ms>:dx,dy" moves it by that many
 * mickeys (tfx_mouse_motion, M09), "d<ms>:" only writes the screen;
 * TFX_HEADLESS_MS=<ms> ends the run there;
 * with TFX_HEADLESS_DUMP the screen is also written before each timed
 * input (at_<ms>.ppm) and at the end (last.ppm).
 * TFX_HEADLESS_ENHANCED=<w>x<h> turns the enhanced 3D mode on at that size
 * (render/hires.h; the app's Ctrl+Shift+G): each frame's enhanced picture
 * is hashed apart ("enhanced" line) and dumped beside the frame
 * (<name>_hi.ppm); the frames' hashes must stay those of a run without it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile.h"
#include "session.h"

static uint64_t clock_us = 1000000;
static uint64_t now(void *ctx) { return (void)ctx, clock_us; }
static void say(void *ctx, const char *m) { (void)ctx, fprintf(stderr, "%s\n", m); }

/* the enhanced mode's size and picture (TFX_HEADLESS_ENHANCED; 0: off) */
static int enh_w, enh_h;
static uint8_t *enh_rgba;

static void write_ppm(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6 %d %d 255\n", w, h);
        for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; i++)
            fwrite(rgba + 4 * i, 1, 3, f);
        fclose(f);
    }
}

static void dump(tfx_mac_session *s, const char *name) {
    const char *dir = getenv("TFX_HEADLESS_DUMP");
    if (!dir)
        return;
    static uint8_t rgba[64000 * 4];
    char path[1100];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    tfx_mac_session_rgba(s, rgba);
    write_ppm(path, rgba, 320, 200);
    if (enh_rgba && tfx_mac_session_enhanced_rgba(s, enh_w, enh_h, enh_rgba)) {
        snprintf(path, sizeof path, "%s/%s_hi.ppm", dir, name);
        write_ppm(path, enh_rgba, enh_w, enh_h);
    }
}

/* a scan code list typed; 0 when it is not one */
static int type_keys(tfx *core, const char *list) {
    for (const char *p = list; *p;) {
        char *end;
        unsigned long b = strtoul(p, &end, 16);
        if (end == p || (*end && *end != ','))
            return 0;
        tfx_key(core, (uint8_t)b);
        p = *end ? end + 1 : end;
    }
    return 1;
}

/* the timed inputs due by ms (each once: done[a]) */
static int timed(tfx_mac_session *s, int argc, char **argv, char *done, uint64_t ms) {
    for (int a = 4; a < argc; a++) {
        char *colon = strchr(argv[a], ':');
        if (done[a] || !colon || !strchr("@mvd", argv[a][0]) || strtoull(argv[a] + 1, NULL, 10) > ms)
            continue;
        done[a] = 1;
        char name[32];
        snprintf(name, sizeof name, "at_%s", argv[a] + 1);
        *strchr(name, ':') = 0;
        dump(s, name);
        if (argv[a][0] == 'd')
            continue;
        unsigned x, y, b;
        int dx, dy;
        int ok = argv[a][0] == 'm'   ? sscanf(colon + 1, "%u,%u,%u", &x, &y, &b) == 3
                 : argv[a][0] == 'v' ? sscanf(colon + 1, "%d,%d", &dx, &dy) == 2
                                     : type_keys(s->core, colon + 1);
        if (!ok) {
            fprintf(stderr, "bad timed input: %s\n", argv[a]);
            return 0;
        }
        if (argv[a][0] == 'm')
            tfx_mouse(s->core, (uint16_t)x, (uint16_t)y, (uint16_t)b);
        else if (argv[a][0] == 'v')
            tfx_mouse_motion(s->core, dx, dy);
    }
    return 1;
}

static uint64_t fnv(uint64_t h, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: tfx_headless <game folder> <store> <frames> [frame:scan,..]\n");
        return 2;
    }
    long limit = atol(argv[3]);
    tfx_mac_profile prof;
    if (!tfx_mac_profile_check(argv[1], &prof)) {
        fprintf(stderr, "%s\n", prof.message);
        return 3;
    }
    fprintf(stderr, "profile: %s\n", prof.edition);
    tfx_mac_session s = {.now_us = now, .log = say};
    static tfx_mac_mixer mix;
    static tfx_mac_cd cd;
    const char *av = getenv("TFX_HEADLESS_AUDIO"), *cue = getenv("TFX_HEADLESS_CUE"), *wav_path = getenv("TFX_HEADLESS_WAV");
    int audio = av && atoi(av);
    if (audio) {
        tfx_mac_mixer_init(&mix, 48000, 20000);
        s.mixer = &mix;
    }
    if (cue) {
        if (!tfx_mac_cd_open(&cd, cue) || !tfx_mac_cd_profile(&cd)) {
            fprintf(stderr, "%s\n", cd.error);
            return 3;
        }
        s.cd = &cd;
    }
    FILE *wav = audio && wav_path ? fopen(wav_path, "wb") : NULL;
    if (wav)
        fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, wav);
    uint64_t sound = 0xcbf29ce484222325ull, sound_frames = 0;
    int32_t sound_peak = 0;
    if (!tfx_mac_session_start(&s, argv[1], argv[2])) {
        fprintf(stderr, "%s\n", s.error[0] ? s.error : "no core");
        return s.error[0] ? 3 : 1;
    }
    const char *sv = getenv("TFX_HEADLESS_STICK");
    if (sv) {
        unsigned c[4], b;
        if (sscanf(sv, "%u %u %u %u %u", &c[0], &c[1], &c[2], &c[3], &b) != 5) {
            fprintf(stderr, "TFX_HEADLESS_STICK: x y z w buttons\n");
            return 2;
        }
        uint16_t count[4] = {(uint16_t)c[0], (uint16_t)c[1], (uint16_t)c[2], (uint16_t)c[3]};
        tfx_stick(s.core, 1, count, (uint8_t)b);
    }
    uint64_t all = 0xcbf29ce484222325ull;
    long waits = 0, shown = 0;
    const char *pv = getenv("TFX_HEADLESS_PRESENT"), *pu = getenv("TFX_HEADLESS_PUMP");
    long present_every = pv ? atol(pv) : 0;
    int pump = pu && atoi(pu);
    static uint8_t shown_rgba[64000 * 4];
    static uint64_t palette[4096];
    int palettes = 0;
    const char *mv = getenv("TFX_HEADLESS_MS");
    uint64_t ms_limit = mv ? strtoull(mv, NULL, 10) : UINT64_MAX;
    const char *ev = getenv("TFX_HEADLESS_ENHANCED");
    if (ev) {
        if (sscanf(ev, "%dx%d", &enh_w, &enh_h) != 2 || enh_w <= 0 || enh_h <= 0 || enh_w > 8192 || enh_h > 8192 ||
            !(enh_rgba = malloc((size_t)enh_w * (size_t)enh_h * 4))) {
            fprintf(stderr, "TFX_HEADLESS_ENHANCED: <w>x<h>\n");
            return 2;
        }
    }
    uint64_t enh_all = 0xcbf29ce484222325ull;
    long enh_frames = 0;
    char *done = calloc((size_t)argc, 1);
    while (shown < limit) {
        uint64_t ms = (clock_us - 1000000) / 1000;
        if (ms >= ms_limit)
            break;
        if (!timed(&s, argc, argv, done, ms))
            return 2;
        tfx_step_result r;
        if (pump) { /* the app's way: a pump to the first frame or wait */
            uint64_t before = s.frames;
            int got = tfx_mac_session_pump(&s, 0);
            r = got & TFX_MAC_ENDED ? TFX_STEP_ENDED : s.frames != before ? TFX_STEP_FRAME : TFX_STEP_WAIT;
            if (s.frames > before + 1) {
                fprintf(stderr, "the pump showed %llu frames at once\n", (unsigned long long)(s.frames - before));
                return 4;
            }
        } else {
            r = tfx_step(s.core);
        }
        if (r == TFX_STEP_ENDED)
            break;
        if (r == TFX_STEP_WAIT) {
            clock_us += 1000;
            if (audio) {
                float buf[2 * 48];
                int16_t pcm[2 * 48];
                tfx_mac_mixer_render(&mix, buf, 48, clock_us);
                for (int i = 0; i < 2 * 48; i++) {
                    pcm[i] = (int16_t)lrintf(buf[i] * 32767.0f);
                    int32_t a = pcm[i] < 0 ? -pcm[i] : pcm[i];
                    sound_peak = a > sound_peak ? a : sound_peak;
                }
                sound = fnv(sound, (const uint8_t *)pcm, sizeof pcm);
                sound_frames += 48;
                if (wav)
                    fwrite(pcm, sizeof pcm, 1, wav);
            }
            if (++waits > 100000000L)
                break;
            continue;
        }
        shown++;
        if (present_every && shown % present_every == 0)
            tfx_mac_session_rgba(&s, shown_rgba);
        const uint8_t *dac, *px = tfx_frame(s.core, &dac);
        uint64_t h = fnv(fnv(0xcbf29ce484222325ull, px, 64000), dac, 768);
        uint64_t dh = fnv(0xcbf29ce484222325ull, dac, 768); /* the palettes shown (fades count each step) */
        int seen = 0;
        for (int i = 0; i < palettes && !seen; i++)
            seen = palette[i] == dh;
        if (!seen && palettes < 4096)
            palette[palettes++] = dh;
        all = fnv(all, (const uint8_t *)&h, sizeof h);
        if (enh_rgba && tfx_mac_session_enhanced_rgba(&s, enh_w, enh_h, enh_rgba)) {
            uint64_t eh = fnv(0xcbf29ce484222325ull, enh_rgba, (size_t)enh_w * (size_t)enh_h * 4);
            enh_all = fnv(enh_all, (const uint8_t *)&eh, sizeof eh);
            enh_frames++;
        }
        if (shown % 100 == 0 || shown == limit) {
            printf("frame %ld %016llx\n", shown, (unsigned long long)h);
            char name[32];
            snprintf(name, sizeof name, "frame_%05ld", shown);
            dump(&s, name);
        }
        for (int a = 4; a < argc; a++) {
            char *colon = strchr(argv[a], ':');
            if (!colon || strchr("@mvd", argv[a][0]) || atol(argv[a]) != shown)
                continue;
            if (!type_keys(s.core, colon + 1)) { /* (not a scan code list: stop) */
                fprintf(stderr, "bad key list: %s\n", argv[a]);
                return 2;
            }
        }
    }
    dump(&s, "last");
    free(done);
    const char *msg;
    tfx_end e = tfx_ended(s.core, &msg);
    printf("frames %ld waits %ld clock %llu palettes %d samples %llu music %llu end %d%s%s all %016llx\n", shown, waits,
           (unsigned long long)clock_us, palettes, (unsigned long long)s.samples, (unsigned long long)s.music_writes, (int)e,
           msg ? " " : "", msg ? msg : "", (unsigned long long)all);
    if (enh_rgba)
        printf("enhanced %dx%d frames %ld all %016llx\n", enh_w, enh_h, enh_frames, (unsigned long long)enh_all);
    if (audio)
        printf("audio frames %llu peak %d opl %llu midi %llu cd %llu dropped %llu sound %016llx\n",
               (unsigned long long)sound_frames, (int)sound_peak, (unsigned long long)mix.music.opl_writes,
               (unsigned long long)mix.music.midi_messages, (unsigned long long)s.cd_plays,
               (unsigned long long)mix.music.dropped, (unsigned long long)sound);
    if (wav) { /* the sizes */
        uint32_t data = (uint32_t)(sound_frames * 4), riff = data + 36;
        fseek(wav, 4, SEEK_SET);
        fwrite(&riff, 4, 1, wav);
        fseek(wav, 40, SEEK_SET);
        fwrite(&data, 4, 1, wav);
        fclose(wav);
    }
    tfx_mac_session_stop(&s);
    free(enh_rgba);
    if (s.cd)
        tfx_mac_cd_close(s.cd);
    return 0;
}
