/* TFX for Windows (W01 .. W05): the recovered game (include/tfx/core.h) in
 * a native window. The original folder is only read; what the game writes
 * goes to the user store (win/content.h). The frame, the original's 320 x
 * 200 picture and its palette, is shown by GDI as an 8-bit DIB stretched
 * to the 4:3 picture a VGA monitor showed (host/present.h); Alt+Enter
 * toggles fullscreen (a borderless window over the monitor). The keyboard
 * reaches the game as the AT controller's scan codes (host/scancode.h),
 * every held key released when the window loses the focus; the mouse as
 * the driver's 640 x 200 position; the first joystick (winmm) as the game
 * port's one-shot counts. Sound: win/audio.h.
 *
 *   tfx.exe [--game DIR] [--store DIR] [--cd SHEET.CUE] [--scale aspect|integer|stretch|square]
 *           [--fullscreen] [--midi N] [--no-audio] [--check] [--choose] [--auto SCRIPT] [--hidden] [--log FILE] [--frame-log FILE] [--virtual-clock] [--trace-files] [--wav FILE] [--sound-log FILE] [--settings] [--import FILE].. [--export DIR]
 *           [--enhanced] [--no-pack] [--pack] [--theme system|light|dark]
 *
 * The program's own folders, beside it (else in %LOCALAPPDATA%\TFX when
 * its folder cannot be written): SAVE, the user store (what the game
 * writes; --store names another), and GAME, its copies of the user's
 * content (win/pack.h). Started without a script, the first time from
 * a folder the program packs the folder's game files into GAME\TFX.ZIP
 * and from then on reads them from it (--game DIR again: packed again
 * when the folder's files differ); the CD image (--cd, a sheet beside
 * the folder, or the one the user picks after the folder) it converts
 * into GAME\TFX.CHD and plays the CD's music from that. --no-pack, and
 * every run with --auto, --check, --import or --export, use the folder
 * and the sheet as given and leave GAME alone (--pack: a script's run
 * uses GAME as the user's start does, for its validation).
 *
 * The program's windows and boxes follow the system's light or dark app
 * mode (win/theme.h; --theme light or dark sets one). The window menu
 * holds About... above Close: the About box, over the game held and muted.
 *
 * Ctrl+Shift+C opens the settings view (--settings, CONFIG.EXE's) over
 * the game, held and muted; saved, the game starts again with them.
 * Ctrl+Shift+G switches the 3D between the original's 320 x 200 and the
 * enhanced mode (render/hires.h: the world at the picture's own size;
 * --enhanced starts in it); a note names the mode for 2 s at the top right
 * (0.4 s fading in, 0.4 s out).
 *
 * Without --game: the folder last used (tfx.ini in the store's parent),
 * the program's own folder when it holds TFX.DAT, else the user picks one
 * (--choose: picks one now). A folder that is not the supported baseline
 * shows its content profile: incomplete, the user picks another or stops;
 * another edition, the user starts anyway, picks another or stops.
 * --check prints the folder's content profile and exits (status 0
 * supported, 1 another edition, 2 incomplete). --auto runs a script, one
 * command a line ('#' comments), each when the frames shown reach its
 * time:
 *   wait N            N frames on
 *   ms N              N milliseconds on (the host clock)
 *   key CODE          the key made (CODE: set-1 hex, "e0" prefix for the
 *   up CODE           grey keys: e048; "pause", "print"), released
 *   tap CODE          made, released 100 ms later
 *   mouse X Y B       the driver's position (0 .. 639, 0 .. 199), buttons
 *   shot FILE         the frame as a 24-bit BMP
 *   hshot FILE        the enhanced mode's picture as a 24-bit BMP
 *   enhanced 0|1      the enhanced 3D mode off, on (as Ctrl+Shift+G)
 *   settings FILE     the settings view (as Ctrl+Shift+C), run by the
 *                     settings script FILE (as --settings --auto FILE)
 *   dump FILE         the frame's 64000 indices, then its 768 DAC values
 *   peek ADDR N FILE  N bytes of the game image at ADDR (hex), for diagnosis
 *   size W H          the window's client area W x H (windowed)
 *   full 0|1          fullscreen off, on
 *   scale MODE        aspect, integer, stretch, square
 *   stick X Y B       a stick in place of the device's: X, Y 0 .. 65535, buttons
 *   focus             the focus lost: every held key released, the buttons up
 *   quit              the game asked to end (as closing the window)
 * --frame-log writes each frame's number, host time, SHA-256 (the
 * 64000 pixels, then the 768 DAC values) and CRTC start. --virtual-clock makes the host
 * clock the steps' (a wait 1 ms, any other step 0.1 ms) and runs without
 * sleeping: the same script gives the same frames on any machine,
 * window or display (W03, W04). --wav (with it) renders the sound through
 * the same mixer to a WAV file instead of the device (W05). --settings
 * opens the settings view in CONFIG.EXE's place (W06). --import puts a
 * file the game writes into the store when the game would read it as its
 * own, --export copies the store to a folder; neither starts the game
 * (exit 5 when one was refused). Every start removes an unfinished write
 * the store kept (win/userio.h, W07).
 */
/* (before windows.h, whose near and far macros the core's names would meet) */
#include "core/api.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/present.h"
#include "host/sha256.h"
#include "host/profile.h"
#include "host/scancode.h"
#include "tfx/core.h"
#include "win/audio.h"
#include "win/content.h"
#include "win/pack.h"
#include "win/settings.h"
#include "win/theme.h"
#include "win/userio.h"

typedef struct app {
    HWND wnd;
    tfx *game;
    tfx_win_content content;
    tfx_win_audio *audio;
    LARGE_INTEGER freq, base;
    tfx_scale_mode scale;
    int fullscreen;
    WINDOWPLACEMENT placement;
    tfx_keys_held keys;
    uint16_t mouse_x, mouse_y, mouse_b;
    struct {
        BITMAPINFOHEADER h;
        RGBQUAD colors[256];
    } dib;
    uint8_t frame[320 * 200];
    int have_frame;
    /* the enhanced 3D mode: its picture (bottom-up, hw x hh), the note
     * naming the mode (shown from note_at, real time) */
    int enhanced;
    struct {
        BITMAPINFOHEADER h;
        RGBQUAD colors[256];
    } hdib;
    uint8_t *hframe;
    int hw, hh;
    uint64_t note_at;
    int note_on;
    uint64_t note_shown;
    uint64_t frames;
    FILE *log;
    int joy; /* the joystick's id, -1 none */
    JOYCAPSW joy_caps;
    /* automation */
    char **script;
    int script_n, script_at;
    uint64_t script_frame, script_ms;
    int pending_up[16], pending_n;
    uint64_t pending_ms[16]; /* (a tap's release: by the clock, as a key is let go, not by frames) */
    int quit_asked;
    int want_settings, in_settings; /* Ctrl+Shift+C: the settings view, from the main loop */
    int want_about;                 /* the window menu's About...: the About box, from the main loop */
    char settings_script[400]; /* (the script's settings FILE: the view's own script) */
    int exit_code;
    /* the virtual clock (--virtual-clock): advanced by the steps only, so
     * a run is the same whatever the machine, the window or the display */
    int vstick; /* the script's stick in place of the device's */
    uint32_t vstick_xy[4]; /* axes 3 and 4: 0xffffffff when not connected */
    uint8_t vstick_b;
    int virtual_clock;
    uint64_t vclock;
    FILE *frame_log;
    FILE *sound_log; /* --sound-log: every sound event with its time */
} app;

static app A;

/* --- the host's services ------------------------------------------------- */

static uint64_t h_now(void *ctx) {
    (void)ctx;
    if (A.virtual_clock)
        return A.vclock;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    uint64_t d = (uint64_t)(t.QuadPart - A.base.QuadPart);
    return d / (uint64_t)A.freq.QuadPart * 1000000 + d % (uint64_t)A.freq.QuadPart * 1000000 / (uint64_t)A.freq.QuadPart;
}

static void logf_line(const char *kind, const char *message) {
    char line[600];
    snprintf(line, sizeof line, "%10.3f %s: %s\n", (double)h_now(NULL) / 1e6, kind, message);
    OutputDebugStringA(line);
    if (A.log) {
        fputs(line, A.log);
        fflush(A.log);
    }
}

static void h_print(void *ctx, const char *message) { (void)ctx, logf_line("print", message); }
static void h_diag(void *ctx, const char *message) { (void)ctx, logf_line("diag", message); }

static const char *h_getenv(void *ctx, const char *name) {
    (void)ctx;
    if (!_stricmp(name, "DID"))
        return "C:\\TFX";
    if (!_stricmp(name, "BLASTER")) /* a Sound Blaster 16 at the usual settings */
        return "A220 I5 D1 H5 P330 T6";
    return NULL;
}

static int trace_files; /* --trace-files: each file asked for, logged */
static uint32_t h_size(void *ctx, const char *path) {
    uint32_t n = tfx_win_content_size(&A.content, path);
    if (trace_files) {
        char line[300];
        snprintf(line, sizeof line, "size %s: %u", path, n);
        h_diag(NULL, line);
    }
    return (void)ctx, n;
}
static int h_read(void *ctx, const char *path, uint8_t *buf, uint32_t cap) {
    return (void)ctx, tfx_win_content_read(&A.content, path, buf, cap);
}
static int h_write(void *ctx, const char *path, const uint8_t *buf, uint32_t n) {
    int ok = tfx_win_content_write(&A.content, path, buf, n);
    char line[300];
    snprintf(line, sizeof line, "%s %s (%u bytes)", ok ? "wrote" : "could not write", path, n);
    return (void)ctx, h_diag(NULL, line), ok;
}
static int h_find(void *ctx, const char *pattern, uint16_t attr, uint32_t k, char name[13]) {
    return (void)ctx, tfx_win_content_find(&A.content, pattern, attr, k, name);
}
static unsigned long n_opl, n_mpu, n_pcm, n_cd; /* (the run's account, logged at the end) */
static void h_port(void *ctx, uint64_t at, uint16_t port, uint8_t value) {
    if (A.sound_log)
        fprintf(A.sound_log, "%llu port %x %02x\n", (unsigned long long)at, port, value);
    if (port == 0x388 || port == 0x389)
        n_opl++;
    else if (port == 0x330 || port == 0x331)
        n_mpu++;
    (void)ctx, tfx_win_audio_port(A.audio, at, port, value);
}
static void h_pcm(void *ctx, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate) {
    n_pcm += n != 0;
    if (A.sound_log)
        fprintf(A.sound_log, "%llu pcm %d %lu %lu\n", (unsigned long long)at, voice, (unsigned long)n, (unsigned long)rate);
    (void)ctx, tfx_win_audio_pcm(A.audio, at, voice, data, n, rate);
}
static int32_t h_cd_track(void *ctx, int track) { return (void)ctx, tfx_win_audio_cd_track(A.audio, track); }
static void h_cd_play(void *ctx, int32_t from, int32_t n) {
    if (A.sound_log)
        fprintf(A.sound_log, "%llu cd_play %ld %ld\n", (unsigned long long)h_now(NULL), (long)from, (long)n);
    n_cd++, (void)ctx, tfx_win_audio_cd_play(A.audio, from, n);
}
static void h_cd_stop(void *ctx) {
    if (A.sound_log)
        fprintf(A.sound_log, "%llu cd_stop\n", (unsigned long long)h_now(NULL));
    (void)ctx, tfx_win_audio_cd_stop(A.audio);
}
static int h_cd_heard(void *ctx) { return (void)ctx, tfx_win_audio_cd_heard(A.audio); }

/* the first joystick as the game port: an axis 0 .. 65535 as the
 * one-shot's count, 200 .. 3800 reads (the game's averages, a quarter
 * of the counts, about 50 .. 950 around 500: a common stick's) */
/* the first joystick winmm answers for; looked for again (at most every
 * two seconds) while there is none, so one plugged in later is found */
static void find_joystick(void) {
    static uint64_t last;
    uint64_t now = h_now(NULL);
    if (A.joy >= 0 || (last && now - last < 2000000))
        return;
    last = now;
    for (UINT id = 0; id < joyGetNumDevs() && A.joy < 0; id++) {
        JOYINFOEX j = {.dwSize = sizeof j, .dwFlags = JOY_RETURNALL};
        if (joyGetPosEx(id, &j) == JOYERR_NOERROR && joyGetDevCapsW(id, &A.joy_caps, sizeof A.joy_caps) == JOYERR_NOERROR) {
            A.joy = (int)id;
            h_diag(NULL, "joystick found");
        }
    }
}

/* an axis at p of 0 .. span as the one-shot's count */
static uint16_t stick_count(uint64_t p, uint64_t span) { return (uint16_t)(200 + p * 3600 / span); }

static unsigned long n_stick; /* (the device's reads, in the run's account) */
/* the script's stick: "X Y B [Z [R]]", axes 0 .. 65535, the buttons' bits;
 * axes 3 and 4 not connected unless given */
static int set_vstick(const char *arg) {
    int v[5] = {0, 0, 0, -1, -1};
    if (sscanf(arg, "%d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) < 3)
        return 0;
    A.vstick = 1, A.vstick_b = (uint8_t)(v[2] & 15);
    A.vstick_xy[0] = (uint32_t)v[0] & 0xffff, A.vstick_xy[1] = (uint32_t)v[1] & 0xffff;
    A.vstick_xy[2] = v[3] < 0 ? 0xffffffffu : (uint32_t)v[3] & 0xffff;
    A.vstick_xy[3] = v[4] < 0 ? 0xffffffffu : (uint32_t)v[4] & 0xffff;
    return 1;
}

/* the controller now as the game port's device (core/stick.h: each axis's
 * count, 0xffff an axis not connected, which never times out) */
static int h_stick(void *ctx, tfx_stick_device *d) {
    (void)ctx;
    memset(d, 0, sizeof *d);
    if (A.vstick) { /* the script's stick (W04's validation): x, y of 0 .. 65535 */
        for (int i = 0; i < 4; i++)
            d->count[i] = i < 2 || A.vstick_xy[i] <= 0xffff ? stick_count(A.vstick_xy[i], 65535) : 0xffff;
        d->buttons = A.vstick_b;
        return d->present = 1;
    }
    find_joystick();
    if (A.joy < 0)
        return 0;
    n_stick++;
    JOYINFOEX j = {.dwSize = sizeof j, .dwFlags = JOY_RETURNALL};
    if (joyGetPosEx((UINT)A.joy, &j) != JOYERR_NOERROR) { /* unplugged: no stick until one answers */
        A.joy = -1;
        h_diag(NULL, "joystick lost");
        return 0;
    }
    DWORD v[4] = {j.dwXpos, j.dwYpos, j.dwZpos, j.dwRpos};
    UINT lo[4] = {A.joy_caps.wXmin, A.joy_caps.wYmin, A.joy_caps.wZmin, A.joy_caps.wRmin};
    UINT hi[4] = {A.joy_caps.wXmax, A.joy_caps.wYmax, A.joy_caps.wZmax, A.joy_caps.wRmax};
    int axes = (int)A.joy_caps.wNumAxes;
    for (int i = 0; i < 4; i++) {
        if (i >= axes || hi[i] <= lo[i]) {
            d->count[i] = 0xffff;
            continue;
        }
        d->count[i] = stick_count(v[i] < lo[i] ? 0 : v[i] > hi[i] ? hi[i] - lo[i] : v[i] - lo[i], hi[i] - lo[i]);
    }
    d->buttons = (uint8_t)(j.dwButtons & 15);
    return d->present = 1;
}

/* the device given to the game before each step (a real one read at most
 * every 2 ms of the host's clock) */
static void push_stick(void) {
    static tfx_stick_device d;
    static uint64_t last;
    uint64_t now = h_now(NULL);
    if (A.vstick || !last || now - last >= 2000 || now < last) {
        h_stick(NULL, &d);
        last = now ? now : 1;
    }
    tfx_stick(A.game, d.present, d.count, d.buttons);
}

/* --- presentation ---------------------------------------------------------- */

/* the real time (the note's fades: not the virtual clock) */
static uint64_t real_us(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    uint64_t d = (uint64_t)(t.QuadPart - A.base.QuadPart);
    return d / (uint64_t)A.freq.QuadPart * 1000000 + d % (uint64_t)A.freq.QuadPart * 1000000 / (uint64_t)A.freq.QuadPart;
}

/* the note's opacity now: 2 s, fading in and out over 0.4 s each */
static double note_alpha(void) {
    if (!A.note_on)
        return 0;
    double e = (double)(real_us() - A.note_at) / 1e6;
    return e >= 2.0 ? 0 : e < 0.4 ? e / 0.4 : e > 1.6 ? (2.0 - e) / 0.4 : 1;
}

/* the note at the picture's top right (w x h), white on a dark box */
static void draw_note(HDC dc, int w, int h, double a) {
    const wchar_t *text = A.enhanced ? L"Enhanced 3D" : L"Original 3D";
    int len = (int)wcslen(text), fh = h / 24 < 14 ? 14 : h / 24, pad = fh / 2;
    HDC tdc = CreateCompatibleDC(dc);
    HFONT font = CreateFontW(-fh, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (!tdc || !font) {
        if (font)
            DeleteObject(font);
        if (tdc)
            DeleteDC(tdc);
        return;
    }
    HGDIOBJ old_font = SelectObject(tdc, font);
    SIZE sz;
    GetTextExtentPoint32W(tdc, text, len, &sz);
    int bw = sz.cx + 2 * pad, bh = sz.cy + pad;
    BITMAPINFO bi = {.bmiHeader = {sizeof(BITMAPINFOHEADER), bw, -bh, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
    void *bits = NULL;
    HBITMAP box = CreateDIBSection(tdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (box && bits) {
        HGDIOBJ old_box = SelectObject(tdc, box);
        memset(bits, 0, (size_t)bw * (size_t)bh * 4);
        SetBkMode(tdc, TRANSPARENT);
        SetTextColor(tdc, RGB(255, 255, 255));
        TextOutW(tdc, pad, pad / 2, text, len);
        GdiFlush();
        uint32_t *px = bits; /* the text's coverage to premultiplied white over the box's 63 % black */
        for (int i = 0; i < bw * bh; i++) {
            uint32_t t = px[i] & 0xff;
            px[i] = (160 + t * 95 / 255) << 24 | t << 16 | t << 8 | t;
        }
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)(a * 255 + 0.5), AC_SRC_ALPHA};
        AlphaBlend(dc, w - bw - pad, pad, bw, bh, tdc, 0, 0, bw, bh, bf);
        SelectObject(tdc, old_box);
    }
    if (box)
        DeleteObject(box);
    SelectObject(tdc, old_font);
    DeleteObject(font);
    DeleteDC(tdc);
}


static void show(HDC dc) {
    RECT cr;
    GetClientRect(A.wnd, &cr);
    tfx_rect r = tfx_present_fit(cr.right, cr.bottom, A.scale);
    HBRUSH black = GetStockObject(BLACK_BRUSH);
    RECT bars[4] = {{0, 0, cr.right, r.y}, {0, r.y + r.h, cr.right, cr.bottom},
                    {0, r.y, r.x, r.y + r.h}, {r.x + r.w, r.y, cr.right, r.y + r.h}};
    for (int i = 0; i < 4; i++)
        if (bars[i].right > bars[i].left && bars[i].bottom > bars[i].top)
            FillRect(dc, &bars[i], black);
    if (!A.have_frame) {
        RECT all = {r.x, r.y, r.x + r.w, r.y + r.h};
        FillRect(dc, &all, black);
        return;
    }
    double a = note_alpha();
    HDC to = dc, mem = NULL;
    HBITMAP bmp = NULL, old = NULL;
    int x = r.x, y = r.y;
    if (a > 0 && (mem = CreateCompatibleDC(dc)) && (bmp = CreateCompatibleBitmap(dc, r.w, r.h))) {
        old = SelectObject(mem, bmp);
        to = mem, x = y = 0;
    }
    SetStretchBltMode(to, COLORONCOLOR);
    if (A.enhanced && A.hframe && A.hw == r.w && A.hh == r.h)
        StretchDIBits(to, x, y, r.w, r.h, 0, 0, A.hw, A.hh, A.hframe, (BITMAPINFO *)&A.hdib, DIB_RGB_COLORS, SRCCOPY);
    else
        StretchDIBits(to, x, y, r.w, r.h, 0, 0, 320, 200, A.frame, (BITMAPINFO *)&A.dib, DIB_RGB_COLORS, SRCCOPY);
    if (to == mem) {
        draw_note(mem, r.w, r.h, a);
        BitBlt(dc, r.x, r.y, r.w, r.h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
    }
    if (bmp)
        DeleteObject(bmp);
    if (mem)
        DeleteDC(mem);
    A.note_shown = real_us();
}

/* the enhanced mode's picture of the frame shown, at the picture's size
 * (the core's layer made or resized to it) */
static void take_enhanced(void) {
    RECT cr;
    GetClientRect(A.wnd, &cr);
    tfx_rect r = tfx_present_fit(cr.right, cr.bottom, A.scale);
    int w, h;
    const uint8_t *p;
    if (r.w <= 0 || r.h <= 0 || !tfx_enhanced(A.game, r.w, r.h) || !(p = tfx_frame_enhanced(A.game, &w, &h)))
        return;
    int stride = (w + 3) & ~3;
    if (w != A.hw || h != A.hh) {
        free(A.hframe);
        A.hw = A.hh = 0;
        if (!(A.hframe = malloc((size_t)stride * (size_t)h)))
            return;
        A.hw = w, A.hh = h;
        A.hdib.h = (BITMAPINFOHEADER){sizeof A.hdib.h, w, h, 1, 8, BI_RGB, 0, 0, 0, 256, 0};
    }
    memcpy(A.hdib.colors, A.dib.colors, sizeof A.hdib.colors);
    for (int y = 0; y < h; y++) /* (bottom-up) */
        memcpy(A.hframe + (size_t)(h - 1 - y) * (size_t)stride, p + (size_t)y * (size_t)w, (size_t)w);
}

static void take_frame(void) {
    const uint8_t *dac;
    const uint8_t *f = tfx_frame(A.game, &dac);
    uint8_t rgb[768];
    tfx_present_palette(dac, rgb);
    for (int i = 0; i < 256; i++)
        A.dib.colors[i] = (RGBQUAD){rgb[3 * i + 2], rgb[3 * i + 1], rgb[3 * i], 0};
    /* the DIB bottom-up: rows reversed */
    for (int y = 0; y < 200; y++)
        memcpy(A.frame + (199 - y) * 320, f + y * 320, 320);
    if (A.enhanced)
        take_enhanced();
    A.have_frame = 1;
    A.frames++;
    if (A.frame_log) { /* the frame and its DAC as the game left them */
        tfx_sha256 h;
        uint8_t d[32];
        char hex[65];
        tfx_sha256_init(&h);
        tfx_sha256_add(&h, f, 64000);
        tfx_sha256_add(&h, dac, 768);
        tfx_sha256_done(&h, d);
        tfx_sha256_hex(d, hex);
        const tfx_core *core = tfx_api_core(A.game);
        fprintf(A.frame_log, "%llu %llu %s %04x\n", (unsigned long long)A.frames, (unsigned long long)h_now(NULL), hex,
                (unsigned)(core->vga.crtc[0x0c] << 8 | core->vga.crtc[0x0d]));
    }
    HDC dc = GetDC(A.wnd);
    show(dc);
    ReleaseDC(A.wnd, dc);
}

static void repaint(void) {
    HDC dc = GetDC(A.wnd);
    show(dc);
    ReleaseDC(A.wnd, dc);
}

/* the enhanced 3D mode on or off (Ctrl+Shift+G), its note shown */
static void set_enhanced(int on) {
    A.enhanced = on;
    if (on && A.have_frame)
        take_enhanced();
    else if (!on)
        tfx_enhanced(A.game, 0, 0);
    A.note_at = real_us(), A.note_on = 1;
    logf_line("host", on ? "3D: enhanced" : "3D: original");
    repaint();
}

/* the enhanced mode's picture as a 24-bit BMP */
static int save_hbmp(const char *path) {
    if (!A.enhanced || !A.hframe)
        return 0;
    FILE *o = fopen(path, "wb");
    if (!o)
        return 0;
    int w = A.hw, h = A.hh, stride = (w + 3) & ~3, row = (3 * w + 3) & ~3;
    uint8_t hd[54] = {'B', 'M'};
    uint32_t size = 54 + (uint32_t)row * (uint32_t)h;
    memcpy(hd + 2, &size, 4);
    hd[10] = 54, hd[14] = 40;
    memcpy(hd + 18, &w, 4), memcpy(hd + 22, &h, 4);
    hd[26] = 1, hd[28] = 24;
    fwrite(hd, 1, 54, o);
    uint8_t *line = calloc((size_t)row, 1);
    for (int y = 0; y < h && line; y++) { /* (both bottom-up) */
        const uint8_t *s = A.hframe + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; x++) {
            RGBQUAD c = A.hdib.colors[s[x]];
            line[3 * x] = c.rgbBlue, line[3 * x + 1] = c.rgbGreen, line[3 * x + 2] = c.rgbRed;
        }
        fwrite(line, 1, (size_t)row, o);
    }
    free(line);
    return fclose(o) == 0 && line;
}

static int save_bmp(const char *path) {
    const uint8_t *dac;
    const uint8_t *f = tfx_frame(A.game, &dac);
    static uint32_t px[320 * 200];
    tfx_present_expand(f, dac, px);
    FILE *o = fopen(path, "wb");
    if (!o)
        return 0;
    uint8_t h[54] = {'B', 'M'};
    uint32_t size = 54 + 320 * 200 * 3;
    memcpy(h + 2, &size, 4);
    h[10] = 54, h[14] = 40;
    int32_t w = 320, hh = 200;
    memcpy(h + 18, &w, 4), memcpy(h + 22, &hh, 4);
    h[26] = 1, h[28] = 24;
    fwrite(h, 1, 54, o);
    for (int y = 199; y >= 0; y--)
        for (int x = 0; x < 320; x++) {
            uint32_t p = px[y * 320 + x];
            uint8_t b[3] = {(uint8_t)p, (uint8_t)(p >> 8), (uint8_t)(p >> 16)};
            fwrite(b, 1, 3, o);
        }
    return fclose(o) == 0;
}

static void set_fullscreen(int on) {
    DWORD style = (DWORD)GetWindowLongW(A.wnd, GWL_STYLE);
    if (on && !A.fullscreen) {
        MONITORINFO mi = {.cbSize = sizeof mi};
        A.placement.length = sizeof A.placement;
        if (GetWindowPlacement(A.wnd, &A.placement) && GetMonitorInfoW(MonitorFromWindow(A.wnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(A.wnd, GWL_STYLE, (LONG)(style & ~(DWORD)WS_OVERLAPPEDWINDOW));
            SetWindowPos(A.wnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            A.fullscreen = 1;
        }
    } else if (!on && A.fullscreen) {
        SetWindowLongW(A.wnd, GWL_STYLE, (LONG)(style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(A.wnd, &A.placement);
        SetWindowPos(A.wnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        A.fullscreen = 0;
    }
    ShowCursor(!A.fullscreen);
}

/* --- input ----------------------------------------------------------------- */

static void send_key(int key, int released) {
    uint8_t b[6];
    if (!tfx_keys_note(&A.keys, key, released))
        return;
    int n = tfx_key_bytes(key, released, b);
    for (int i = 0; i < n; i++)
        tfx_key(A.game, b[i]);
}

static void release_all(void) {
    uint8_t b[512];
    int n = tfx_keys_release_all(&A.keys, b, sizeof b);
    for (int i = 0; i < n; i++)
        tfx_key(A.game, b[i]);
    A.mouse_b = 0;
    tfx_mouse(A.game, A.mouse_x, A.mouse_y, 0);
}

static void mouse_at(LPARAM lp, WPARAM wp) {
    RECT cr;
    GetClientRect(A.wnd, &cr);
    tfx_rect r = tfx_present_fit(cr.right, cr.bottom, A.scale);
    tfx_present_mouse(r, (short)LOWORD(lp), (short)HIWORD(lp), &A.mouse_x, &A.mouse_y);
    A.mouse_b = (uint16_t)((wp & MK_LBUTTON ? 1 : 0) | (wp & MK_RBUTTON ? 2 : 0) | (wp & MK_MBUTTON ? 4 : 0));
    tfx_mouse(A.game, A.mouse_x, A.mouse_y, A.mouse_b);
}

/* the window menu's own command (a multiple of 16 below 0xF000, as
 * WM_SYSCOMMAND requires) */
#define SC_TFX_ABOUT 0x1080u

/* the window menu: the system's, with About... and a separator above Close */
static void add_menu(HWND w) {
    HMENU m = GetSystemMenu(w, FALSE);
    if (!m)
        return;
    int n = GetMenuItemCount(m), close = -1;
    for (int i = 0; i < n; i++)
        if (GetMenuItemID(m, i) == SC_CLOSE)
            close = i;
    if (close < 0)
        AppendMenuW(m, MF_SEPARATOR, 0, NULL), AppendMenuW(m, MF_STRING, SC_TFX_ABOUT, L"&About\u2026");
    else {
        InsertMenuW(m, (UINT)close, MF_BYPOSITION | MF_STRING, SC_TFX_ABOUT, L"&About\u2026");
        InsertMenuW(m, (UINT)close + 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    }
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        show(dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
        if (wp == VK_RETURN && (lp & (1 << 29))) { /* Alt+Enter: the host's */
            if (!up && !(lp & (1 << 30)))
                set_fullscreen(!A.fullscreen);
            return 0;
        }
        if (wp == VK_F4 && (lp & (1 << 29)))
            break; /* Alt+F4: close */
        if (wp == 'G' && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0) { /* Ctrl+Shift+G: the host's */
            if (!up && !(lp & (1 << 30)) && A.game)
                set_enhanced(!A.enhanced);
            return 0;
        }
        if (wp == 'C' && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0) { /* Ctrl+Shift+C: the host's */
            if (!up && !(lp & (1 << 30)) && A.game)
                A.want_settings = 1;
            return 0;
        }
        if (!A.game || A.in_settings)
            return 0;
        if (wp == VK_SNAPSHOT) { /* Windows sends only its release */
            if (up)
                send_key(TFX_KEY_PRINT, 0), send_key(TFX_KEY_PRINT, 1);
            return 0;
        }
        int key = tfx_key_from_windows((unsigned)(lp >> 16) & 0xff, (lp >> 24) & 1, wp == VK_PAUSE, 0);
        if (key) {
            if (!up && (lp & (1 << 30)) && key == TFX_KEY_PAUSE)
                return 0;
            send_key(key, up);
        }
        return 0;
    }
    case WM_SYSCHAR:
    case WM_CHAR:
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) /* Alt and F10 are the game's */
            return 0;
        if ((wp & 0xfff0) == SC_TFX_ABOUT) {
            A.want_about = 1;
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
        if (tfx_win_theme_changed(lp))
            tfx_win_theme_refresh(), tfx_win_theme_window(w);
        break;
    case WM_KILLFOCUS:
        if (A.game)
            release_all();
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        if (A.game)
            mouse_at(lp, wp);
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
            SetCapture(w);
        else if (msg != WM_MOUSEMOVE && !(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
            ReleaseCapture();
        return 0;
    case WM_CLOSE:
        if (A.game && !A.quit_asked) {
            A.quit_asked = 1;
            tfx_quit(A.game);
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

/* --- automation -------------------------------------------------------------- */

static int parse_key(const char *s) {
    if (!_stricmp(s, "pause"))
        return TFX_KEY_PAUSE;
    if (!_stricmp(s, "print"))
        return TFX_KEY_PRINT;
    int ext = 0;
    if (strlen(s) == 4 && !_strnicmp(s, "e0", 2))
        ext = TFX_KEY_EXTENDED, s += 2;
    unsigned v = (unsigned)strtoul(s, NULL, 16);
    return v >= 1 && v < 0x80 ? (int)v | ext : 0;
}

static int load_script(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n#")] = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            continue;
        char **grown = realloc(A.script, (size_t)(A.script_n + 1) * sizeof *A.script);
        if (!grown)
            break;
        A.script = grown;
        A.script[A.script_n++] = _strdup(s);
    }
    fclose(f);
    return 1;
}

static void run_script(void) {
    for (int i = 0; i < A.pending_n;) /* taps' releases */
        if (h_now(NULL) / 1000 >= A.pending_ms[i]) {
            send_key(A.pending_up[i], 1);
            A.pending_up[i] = A.pending_up[--A.pending_n];
            A.pending_ms[i] = A.pending_ms[A.pending_n];
        } else
            i++;
    while (A.script_at < A.script_n) {
        if (A.frames < A.script_frame || h_now(NULL) / 1000 < A.script_ms)
            return;
        char cmd[16] = "", arg[400] = "", path[400];
        int a1 = 0, a2 = 0, a3 = 0;
        const char *line = A.script[A.script_at++];
        sscanf(line, "%15s %399[^\n]", cmd, arg);
        logf_line("auto", line);
        if (!strcmp(cmd, "wait"))
            A.script_frame = A.frames + strtoull(arg, NULL, 10);
        else if (!strcmp(cmd, "ms"))
            A.script_ms = h_now(NULL) / 1000 + strtoull(arg, NULL, 10);
        else if (!strcmp(cmd, "key") || !strcmp(cmd, "up") || !strcmp(cmd, "tap")) {
            int key = parse_key(arg);
            if (!key) {
                logf_line("auto", "bad key");
                continue;
            }
            send_key(key, cmd[0] == 'u');
            if (cmd[0] == 't' && A.pending_n < 16) {
                A.pending_up[A.pending_n] = key;
                A.pending_ms[A.pending_n++] = h_now(NULL) / 1000 + 100;
            }
        } else if (!strcmp(cmd, "mouse") && sscanf(arg, "%d %d %d", &a1, &a2, &a3) == 3) {
            A.mouse_x = (uint16_t)a1, A.mouse_y = (uint16_t)a2, A.mouse_b = (uint16_t)a3;
            tfx_mouse(A.game, A.mouse_x, A.mouse_y, A.mouse_b);
        } else if (!strcmp(cmd, "shot")) {
            if (!save_bmp(arg))
                logf_line("auto", "shot failed");
        } else if (!strcmp(cmd, "hshot")) {
            if (!save_hbmp(arg))
                logf_line("auto", "hshot failed");
        } else if (!strcmp(cmd, "settings")) { /* as Ctrl+Shift+C, the view run by the script FILE */
            snprintf(A.settings_script, sizeof A.settings_script, "%s", arg);
            A.want_settings = 1;
            return;
        } else if (!strcmp(cmd, "enhanced")) {
            set_enhanced(atoi(arg) != 0);
        } else if (!strcmp(cmd, "dump")) { /* the frame's 64000 indices, then the 768 DAC values */
            const uint8_t *dac;
            const uint8_t *f = tfx_frame(A.game, &dac);
            FILE *o = fopen(arg, "wb");
            if (!o || fwrite(f, 1, 64000, o) != 64000 || fwrite(dac, 1, 768, o) != 768)
                logf_line("auto", "dump failed");
            if (o)
                fclose(o);
        } else if (!strcmp(cmd, "peek") && sscanf(arg, "%x %x %399s", &a1, &a2, path) == 3) {
            /* (the game image's bytes, for diagnosis: core/api.h) */
            FILE *o = fopen(path, "wb");
            if (o) {
                fwrite(tfx_core_at(tfx_api_core(A.game), (uint32_t)a1), 1, (size_t)a2, o);
                fclose(o);
            }
        } else if (!strcmp(cmd, "size") && sscanf(arg, "%d %d", &a1, &a2) == 2) {
            RECT r = {0, 0, a1, a2};
            set_fullscreen(0);
            AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
            SetWindowPos(A.wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
        } else if (!strcmp(cmd, "stick") && set_vstick(arg)) {
        } else if (!strcmp(cmd, "cd") && sscanf(arg, "%d %d", &a1, &a2) == 2) {
            /* a disc track for a2 sectors from its start, as MSCDEX's play (W05) */
            int32_t from = h_cd_track(NULL, a1);
            if (from >= 0)
                h_cd_play(NULL, from, a2);
            else
                logf_line("auto", "no such track");
        } else if (!strcmp(cmd, "cdstop")) {
            h_cd_stop(NULL);
        } else if (!strcmp(cmd, "focus")) { /* the focus lost (as WM_KILLFOCUS) */
            release_all();
        } else if (!strcmp(cmd, "full")) {
            set_fullscreen(atoi(arg) != 0);
        } else if (!strcmp(cmd, "scale")) {
            A.scale = !strcmp(arg, "integer") ? TFX_SCALE_INTEGER : !strcmp(arg, "stretch") ? TFX_SCALE_STRETCH
                      : !strcmp(arg, "square") ? TFX_SCALE_SQUARE : TFX_SCALE_ASPECT;
            InvalidateRect(A.wnd, NULL, FALSE);
        } else if (!strcmp(cmd, "quit")) {
            A.quit_asked = 1;
            tfx_quit(A.game);
        } else
            logf_line("auto", "unknown command");
    }
}

/* --- the folder and its check ------------------------------------------------ */

static long long p_size(void *ctx, const char *path) { return tfx_win_content_game_size(ctx, path); }

static int p_stream(void *ctx, const char *path, void (*add)(void *sink, const void *data, uint32_t n), void *sink) {
    size_t n = 0;
    uint8_t *p = tfx_win_content_game_load(ctx, path, &n);
    if (!p)
        return 0;
    for (size_t at = 0; at < n; at += 1 << 16)
        add(sink, p + at, (uint32_t)(n - at < (1 << 16) ? n - at : 1 << 16));
    free(p);
    return 1;
}

static tfx_profile_verdict check_folder(char *text, size_t cap) {
    tfx_profile_io io = {&A.content, p_size, p_stream};
    tfx_profile_report r;
    tfx_profile_check(&io, &r);
    size_t at = (size_t)snprintf(text, cap, "%s\r\n\r\n", tfx_profile_verdict_text(r.verdict));
    for (int i = 0; i < r.n && at < cap; i++) {
        char line[200];
        tfx_profile_line(&r, i, line, sizeof line);
        at += (size_t)snprintf(text + at, cap - at, "%s\r\n", line);
    }
    return r.verdict;
}

static int has_game(const wchar_t *dir) {
    wchar_t p[280];
    if (wcslen(dir) > 260)
        return 0;
    swprintf(p, 280, L"%ls\\TFX.DAT", dir);
    return tfx_win_file_size(p) > 0;
}

static int pick_folder(wchar_t *out) {
    BROWSEINFOW bi = {0};
    bi.lpszTitle = L"Choose the folder of your installed TFX (it holds TFX.DAT and DID.DAT). It is only read.";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id)
        return 0;
    int ok = SHGetPathFromIDListW(id, out);
    CoTaskMemFree(id);
    return ok;
}

static void settings_path(const wchar_t *store, wchar_t *out) {
    wcscpy(out, store);
    wchar_t *s = wcsrchr(out, L'\\');
    if (s)
        *s = 0;
    wcscat(out, L"\\tfx.ini");
}

static void fail(const wchar_t *text, int console) {
    if (console)
        fwprintf(stderr, L"%ls\n", text);
    else
        tfx_win_message_box(NULL, text, L"TFX", MB_OK | MB_ICONERROR);
}

/* the import's and export's lines: the log and the console */
static void userio_log(const char *line) {
    logf_line("host", line);
    printf("%s\n", line);
    fflush(stdout);
}

/* the settings view's log, and its automation lines the view does not take */
static void settings_log(const char *message) { logf_line("host", message); }
static int settings_command(const char *line) {
    if (!strncmp(line, "stick ", 6) && set_vstick(line + 6))
        return 1;
    if (!strcmp(line, "nostick")) {
        A.vstick = 0;
        return 1;
    }
    return 0;
}

/* --- the sound devices and the game: opened at the start, and again when
 * the settings view (Ctrl+Shift+C) has saved new settings, which the game
 * reads only as it starts (TFX.CFG) ------------------------------------------ */

static struct {
    const wchar_t *wavp, *cue;
    int no_audio, midi;
    wchar_t found[300];
    wchar_t chd[280]; /* GAME\TFX.CHD when the CD is played from it */
} S;

static int open_audio(void) {
    const wchar_t *cue = S.cue;
    if (S.wavp) {
        if (!(A.audio = tfx_win_audio_open_wav(h_now, NULL, S.wavp))) {
            fail(L"The WAV file cannot be written.", 1);
            return 0;
        }
    } else if (!S.no_audio && !(A.audio = tfx_win_audio_open(h_now, NULL, S.midi))) {
        char why[160];
        snprintf(why, sizeof why,
                 "no sound output (%u wave output devices; none opens): the game runs as on a PC with no sound card",
                 waveOutGetNumDevs());
        logf_line("host", why);
    }
    if (A.audio && !S.wavp && !tfx_win_audio_has_midi(A.audio))
        logf_line("host", "no MIDI output opens: the MT-32 and GS music (MUSIC 2, 4) is not heard");
    if (A.audio && S.chd[0]) {
        char err[200];
        if (!tfx_win_audio_cd_chd(A.audio, S.chd, err, sizeof err))
            logf_line("host", err);
    } else if (A.audio) {
        if (!cue) { /* a sheet beside the game folder's files */
            wchar_t pat[280];
            WIN32_FIND_DATAW d;
            swprintf(pat, 280, L"%ls\\*.cue", A.content.game);
            HANDLE h = FindFirstFileW(pat, &d);
            if (h != INVALID_HANDLE_VALUE) {
                swprintf(S.found, 300, L"%ls\\%ls", A.content.game, d.cFileName);
                FindClose(h);
                cue = S.found;
            }
        }
        char err[200];
        if (cue && !tfx_win_audio_cd(A.audio, cue, err, sizeof err))
            logf_line("host", err);
        else if (!cue)
            logf_line("host", "no CD image: the CD's music is not played");
    }
    return 1;
}

static tfx *start_game(void) {
    tfx_host host = {
        .now_us = h_now, .size = h_size, .read = h_read, .write = h_write, .find = h_find, .getenv = h_getenv,
        .print = h_print, .diag = h_diag,
    };
    if (A.audio) {
        host.audio_port = h_port, host.pcm = h_pcm;
        if (tfx_win_audio_cd_image(A.audio))
            host.cd_track = h_cd_track, host.cd_play = h_cd_play, host.cd_stop = h_cd_stop, host.cd_heard = h_cd_heard;
    }
    static const char *game_argv[] = {"TFX.EXE", "cd_demo"};
    tfx_config cfg = {2, game_argv};
    return tfx_create(&host, &cfg);
}

/* Ctrl+Shift+C: the game held, its sound muted, while the settings view
 * (CONFIG.EXE's) is open; saved, the game starts again with them (as
 * CONFIG.EXE run before TFX.BAT), else it goes on. 0 when the game cannot
 * start again. */
static int settings_in_game(HINSTANCE inst) {
    A.want_settings = 0, A.in_settings = 1;
    release_all();
    if (A.audio)
        tfx_win_audio_volume(A.audio, 0, 0, 0);
    logf_line("host", "settings: opened from the game (Ctrl+Shift+C)");
    FILE *f = A.settings_script[0] ? fopen(A.settings_script, "r") : NULL;
    if (A.settings_script[0] && !f)
        logf_line("auto", "settings: the script cannot be read");
    A.settings_script[0] = 0;
    tfx_win_settings_host sh = {&A.content, h_stick, h_getenv, settings_log, settings_command, f};
    int saved = tfx_win_settings(inst, &sh);
    if (f)
        fclose(f);
    A.in_settings = 0;
    if (IsWindow(A.wnd))
        SetForegroundWindow(A.wnd);
    if (!saved || A.quit_asked) {
        if (A.audio)
            tfx_win_audio_volume(A.audio, 256, 256, 256);
        return 1;
    }
    logf_line("host", "settings: saved; the game starts again with them");
    tfx_destroy(A.game);
    A.game = NULL;
    tfx_win_audio_close(A.audio);
    A.audio = NULL;
    A.have_frame = 0;
    return open_audio() && (A.game = start_game()) != NULL; /* (the enhanced mode kept: take_frame) */
}

/* The window menu's About...: the game held, its sound muted, while the
 * About box is open (as the settings view) */
static void about_in_game(void) {
    A.want_about = 0, A.in_settings = 1;
    if (A.game)
        release_all();
    if (A.audio)
        tfx_win_audio_volume(A.audio, 0, 0, 0);
    logf_line("host", "about: opened");
    tfx_win_about(A.wnd);
    A.in_settings = 0;
    if (A.audio)
        tfx_win_audio_volume(A.audio, 256, 256, 256);
}

/* --- the program's own folders (SAVE, GAME) and copies ---------------------------- */

static int writable(const wchar_t *dir) {
    wchar_t probe[300];
    CreateDirectoryW(dir, NULL);
    swprintf(probe, 300, L"%ls\\tfx-write.tmp", dir);
    HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

/* the program's folder's sub (created), else %LOCALAPPDATA%\TFX\fallback;
 * 0 when neither can be written */
static int own_dir(const wchar_t *sub, const wchar_t *fallback, wchar_t *out) {
    wchar_t exe[260], base[200];
    DWORD n = GetModuleFileNameW(NULL, exe, 260);
    wchar_t *slash = n && n < 260 ? wcsrchr(exe, L'\\') : NULL;
    if (slash) {
        *slash = 0;
        swprintf(out, 260, L"%ls\\%ls", exe, sub);
        if (writable(out))
            return 1;
    }
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, 200);
    if (!n || n >= 200)
        return 0;
    swprintf(out, 260, L"%ls\\TFX", base);
    CreateDirectoryW(out, NULL);
    swprintf(out, 260, L"%ls\\TFX\\%ls", base, fallback);
    return writable(out);
}

/* the files of from (its folders too) into to, none replaced (the saves
 * of the store the program used before SAVE) */
static int copy_tree(const wchar_t *from, const wchar_t *to) {
    wchar_t pat[300], a[300], b[300];
    WIN32_FIND_DATAW d;
    int n = 0;
    swprintf(pat, 300, L"%ls\\*", from);
    HANDLE h = FindFirstFileW(pat, &d);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CreateDirectoryW(to, NULL);
    do {
        if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L".."))
            continue;
        swprintf(a, 300, L"%ls\\%ls", from, d.cFileName);
        swprintf(b, 300, L"%ls\\%ls", to, d.cFileName);
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            n += copy_tree(a, b);
        else
            n += CopyFileW(a, b, TRUE) != 0;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return n;
}

typedef struct pack_job {
    const wchar_t *from, *to;
    char err[200];
} pack_job;

static int zip_work(void *arg, tfx_win_pack_progress progress, void *ctx) {
    pack_job *j = arg;
    (void)progress, (void)ctx;
    return tfx_win_pack_zip(j->from, j->to, j->err, sizeof j->err);
}

static int chd_work(void *arg, tfx_win_pack_progress progress, void *ctx) {
    pack_job *j = arg;
    return tfx_win_pack_chd(j->from, j->to, progress, ctx, j->err, sizeof j->err);
}

static void log_pack(int ok, const char *what, const pack_job *j, uint64_t us) {
    char line[400], to[300];
    WideCharToMultiByte(CP_UTF8, 0, j->to, -1, to, sizeof to, NULL, NULL);
    if (ok)
        snprintf(line, sizeof line, "%s packed into %s (%.1f s)", what, to, (double)us / 1e6);
    else
        snprintf(line, sizeof line, "%s not packed into %s: %s", what, to, j->err);
    logf_line("host", line);
}

/* the user's CD image, after the folder they picked (GetOpenFileName) */
static int pick_cue(wchar_t *out) {
    OPENFILENAMEW o = {.lStructSize = sizeof o};
    out[0] = 0;
    o.lpstrFilter = L"CD image (*.cue)\0*.cue\0";
    o.lpstrFile = out;
    o.nMaxFile = 280;
    o.lpstrTitle = L"Choose your TFX CD's image (its .cue sheet) for the CD's music; Cancel: play without it";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o) != 0;
}

/* --- main ---------------------------------------------------------------------- */

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show_cmd) {
    (void)prev, (void)cmdline;
    QueryPerformanceFrequency(&A.freq);
    QueryPerformanceCounter(&A.base);
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *game = NULL, *store = NULL, *cue = NULL, *script = NULL, *logp = NULL, *framep = NULL, *wavp = NULL, *soundp = NULL;
    int check = 0, hidden = 0, no_audio = 0, midi = -1, full = 0, choose = 0, settings = 0, imports = 0, no_pack = 0, pack = 0;
    const wchar_t *import[64], *exportp = NULL;
    A.scale = TFX_SCALE_ASPECT;
    A.joy = -1;
    for (int i = 1; i < argc; i++) {
        const wchar_t *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!wcscmp(a, L"--game") && v)
            game = v, i++;
        else if (!wcscmp(a, L"--store") && v)
            store = v, i++;
        else if (!wcscmp(a, L"--cd") && v)
            cue = v, i++;
        else if (!wcscmp(a, L"--auto") && v)
            script = v, i++;
        else if (!wcscmp(a, L"--log") && v)
            logp = v, i++;
        else if (!wcscmp(a, L"--frame-log") && v)
            framep = v, i++;
        else if (!wcscmp(a, L"--sound-log") && v)
            soundp = v, i++;
        else if (!wcscmp(a, L"--wav") && v)
            wavp = v, i++;
        else if (!wcscmp(a, L"--trace-files"))
            trace_files = 1;
        else if (!wcscmp(a, L"--virtual-clock"))
            A.virtual_clock = 1;
        else if (!wcscmp(a, L"--midi") && v)
            midi = _wtoi(v), i++;
        else if (!wcscmp(a, L"--scale") && v) {
            A.scale = !wcscmp(v, L"integer") ? TFX_SCALE_INTEGER : !wcscmp(v, L"stretch") ? TFX_SCALE_STRETCH
                      : !wcscmp(v, L"square") ? TFX_SCALE_SQUARE : TFX_SCALE_ASPECT;
            i++;
        } else if (!wcscmp(a, L"--check"))
            check = 1;
        else if (!wcscmp(a, L"--settings"))
            settings = 1;
        else if (!wcscmp(a, L"--import") && v && imports < 64)
            import[imports++] = v, i++;
        else if (!wcscmp(a, L"--export") && v)
            exportp = v, i++;
        else if (!wcscmp(a, L"--choose"))
            choose = 1;
        else if (!wcscmp(a, L"--hidden"))
            hidden = 1;
        else if (!wcscmp(a, L"--enhanced"))
            A.enhanced = 1;
        else if (!wcscmp(a, L"--no-pack"))
            no_pack = 1;
        else if (!wcscmp(a, L"--pack"))
            pack = 1;
        else if (!wcscmp(a, L"--theme") && v)
            tfx_win_theme_force(!wcscmp(v, L"light") ? 0 : !wcscmp(v, L"dark") ? 1 : -1), i++;
        else if (!wcscmp(a, L"--no-audio"))
            no_audio = 1;
        else if (!wcscmp(a, L"--fullscreen"))
            full = 1;
        else {
            fail(L"Usage: tfx [--game DIR] [--store DIR] [--cd SHEET.CUE] [--scale aspect|integer|stretch|square] "
                 L"[--fullscreen] [--midi N] [--no-audio] [--check] [--choose] [--auto SCRIPT] [--hidden] [--log FILE] [--frame-log FILE] [--virtual-clock] [--trace-files] [--wav FILE] [--sound-log FILE] [--settings] [--import FILE].. [--export DIR] [--enhanced] [--no-pack] [--pack] [--theme system|light|dark]", check);
            return 64;
        }
    }
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES}; /* (Common Controls 6, src/win/tfx.manifest) */
    InitCommonControlsEx(&icc);
    tfx_win_theme_refresh(); /* the menus' mode before any window */
    int console = check || script || imports || exportp;
    /* (a GUI program: its output a pipe or file it was given, else the
     * console it was started from) */
    if (console && GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    /* the store, the settings, the folder */
    wchar_t store_dir[260], ini[260], game_dir[260] = L"";
    if (store)
        wcsncpy(store_dir, store, 259), store_dir[259] = 0;
    else {
        wchar_t base[200], old[260];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, 200);
        swprintf(old, 260, L"%ls\\TFX\\store", n && n < 200 ? base : L"");
        wchar_t probe[260];
        if (!own_dir(L"SAVE", L"store", store_dir)) {
            fail(L"No folder for the saves: neither the program's folder nor %LOCALAPPDATA% can be written; give one with --store.", console);
            return 3;
        }
        /* (the first SAVE: the saves of the store used before it, copied) */
        swprintf(probe, 260, L"%ls\\*", store_dir);
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileW(probe, &d);
        int empty = 1;
        if (h != INVALID_HANDLE_VALUE) {
            do
                empty &= !wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..");
            while (FindNextFileW(h, &d));
            FindClose(h);
        }
        DWORD at = GetFileAttributesW(old);
        if (empty && _wcsicmp(old, store_dir) && at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY))
            copy_tree(old, store_dir);
    }
    { /* (absolute: the profile API reads a relative name in the Windows folder) */
        wchar_t abs_store[260];
        DWORD n = GetFullPathNameW(store_dir, 260, abs_store, NULL);
        if (n && n < 260)
            wcscpy(store_dir, abs_store);
    }
    settings_path(store_dir, ini);
    /* the folder: --game, else the last one used, else the program's own,
     * else the user's choice; checked before the start (W02): an
     * incomplete folder or another edition offers another choice */
    int picked = 0;
    if (game) {
        DWORD n = GetFullPathNameW(game, 260, game_dir, NULL);
        if (!n || n >= 260)
            wcsncpy(game_dir, game, 259);
    } else if (!choose) {
        GetPrivateProfileStringW(L"tfx", L"game", L"", game_dir, 260, ini);
        if (!game_dir[0] || !has_game(game_dir)) {
            wchar_t exe[260];
            GetModuleFileNameW(NULL, exe, 260);
            wchar_t *s = wcsrchr(exe, L'\\');
            if (s)
                *s = 0;
            wcscpy(game_dir, has_game(exe) ? exe : L"");
        }
    }
    /* the program's copies (GAME): only when it is the user's own start */
    wchar_t game_home[260], zip_path[280], chd_path[280];
    int packing = (pack || !console) && !no_pack && !check && own_dir(L"GAME", L"GAME", game_home), zipped = 0;
    if (packing) {
        swprintf(zip_path, 280, L"%ls\\TFX.ZIP", game_home);
        swprintf(chd_path, 280, L"%ls\\TFX.CHD", game_home);
        zipped = !game && !choose && tfx_win_file_size(zip_path) > 0 &&
                 tfx_win_content_open_zip(&A.content, zip_path, store_dir);
    }
    char report[4096];
    tfx_profile_verdict verdict;
    for (;;) {
        if (zipped) { /* (checked as a folder is; refused, the user picks a folder) */
            verdict = check_folder(report, sizeof report);
            if (verdict == TFX_PROFILE_SUPPORTED)
                break;
            tfx_win_content_close(&A.content);
            zipped = 0, game_dir[0] = 0;
            logf_line("host", "GAME\\TFX.ZIP is not the supported game: a folder is asked for");
        }
        if (!game_dir[0]) {
            if (console || !pick_folder(game_dir)) {
                fail(L"No TFX folder: give it with --game.", console);
                return 3;
            }
            picked = 1;
        }
        if (!tfx_win_content_open(&A.content, game_dir, store_dir)) {
            if (console || game) {
                wchar_t w[700];
                DWORD at = GetFileAttributesW(game_dir);
                if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY))
                    swprintf(w, 700, L"The game folder %ls does not exist.", game_dir);
                else
                    swprintf(w, 700, L"The user store %ls cannot be created.", store_dir);
                fail(w, console);
                return 3;
            }
            game_dir[0] = 0;
            continue;
        }
        verdict = check_folder(report, sizeof report);
        if (check) {
            fputs(report, stdout);
            return (int)verdict;
        }
        if (verdict == TFX_PROFILE_SUPPORTED || console || game) {
            if (verdict == TFX_PROFILE_INCOMPLETE) {
                wchar_t w[4096];
                MultiByteToWideChar(CP_UTF8, 0, report, -1, w, 4096);
                fail(w, console);
                return 2;
            }
            break;
        }
        /* the user's choice: another folder, or (another edition) start anyway */
        wchar_t w[4200];
        MultiByteToWideChar(CP_UTF8, 0, report, -1, w, 4096);
        if (verdict == TFX_PROFILE_INCOMPLETE) {
            wcscat(w, L"\r\nChoose another folder?");
            if (tfx_win_message_box(NULL, w, L"TFX", MB_RETRYCANCEL | MB_ICONERROR) != IDRETRY)
                return 2;
        } else {
            wcscat(w, L"\r\nStart with this folder anyway? (No: choose another.)");
            int r = tfx_win_message_box(NULL, w, L"TFX", MB_YESNOCANCEL | MB_ICONWARNING);
            if (r == IDCANCEL)
                return 1;
            if (r == IDYES)
                break;
        }
        game_dir[0] = 0;
    }
    if (!zipped && !game && (picked || !choose))
        WritePrivateProfileStringW(L"tfx", L"game", A.content.game, ini);
    wchar_t folder[260] = L"";
    if (!zipped)
        wcscpy(folder, A.content.game);
    if (logp)
        A.log = _wfopen(logp, L"w");
    if (framep)
        A.frame_log = _wfopen(framep, L"w");
    if (soundp)
        A.sound_log = _wfopen(soundp, L"w");
    logf_line("host", tfx_profile_verdict_text(verdict));
    if (packing && !zipped) { /* the folder's game files into GAME\TFX.ZIP (once; again when they differ) */
        pack_job j = {folder, zip_path, ""};
        int ok = tfx_win_file_size(zip_path) > 0 && tfx_win_pack_zip_matches(folder, zip_path);
        if (!ok) {
            uint64_t t0 = real_us();
            ok = tfx_win_pack_run(inst, L"Copying the game's files into GAME\\TFX.ZIP ...", hidden, zip_work, &j);
            log_pack(ok, "the game folder", &j, real_us() - t0);
        }
        if (ok && tfx_win_content_open_zip(&A.content, zip_path, store_dir))
            zipped = 1;
        else if (!tfx_win_content_open(&A.content, folder, store_dir)) {
            fail(L"The game folder cannot be read again.", console);
            return 3;
        }
    }
    if (zipped) {
        char z[300];
        WideCharToMultiByte(CP_UTF8, 0, zip_path, -1, z, sizeof z, NULL, NULL);
        char line[400];
        snprintf(line, sizeof line, "the game's files from %s", z);
        logf_line("host", line);
    }
    if (packing) { /* the CD image into GAME\TFX.CHD (once; again when --cd names another disc) */
        wchar_t found[280] = L"", picked_cue[280];
        const wchar_t *sheet = cue;
        int have = tfx_win_file_size(chd_path) > 0;
        if (!sheet && !have && folder[0]) {
            wchar_t pat[280];
            WIN32_FIND_DATAW d;
            swprintf(pat, 280, L"%ls\\*.cue", folder);
            HANDLE h = FindFirstFileW(pat, &d);
            if (h != INVALID_HANDLE_VALUE) {
                swprintf(found, 280, L"%ls\\%ls", folder, d.cFileName);
                FindClose(h);
                sheet = found;
            }
        }
        if (!sheet && !have && picked && pick_cue(picked_cue))
            sheet = picked_cue;
        if (sheet && (!have || (cue && !tfx_win_pack_chd_matches(sheet, chd_path)))) {
            pack_job j = {sheet, chd_path, ""};
            uint64_t t0 = real_us();
            int ok = tfx_win_pack_run(inst, L"Converting the CD image into GAME\\TFX.CHD (once; a minute or so) ...",
                                      hidden, chd_work, &j);
            log_pack(ok, "the CD image", &j, real_us() - t0);
            have = ok;
        }
        if (have)
            wcscpy(S.chd, chd_path);
        cue = have ? NULL : sheet;
    }
    tfx_win_recover(&A.content, userio_log);
    if (imports || exportp) { /* the user's files (W07), no game */
        int bad = 0;
        for (int k = 0; k < imports; k++) {
            char why[600], line[1000], name[300];
            int ok = tfx_win_import(&A.content, import[k], why, sizeof why);
            WideCharToMultiByte(CP_UTF8, 0, import[k], -1, name, sizeof name, NULL, NULL);
            snprintf(line, sizeof line, "%s %s: %s", ok ? "imported" : "refused", name, why);
            userio_log(line);
            bad |= !ok;
        }
        if (exportp)
            bad |= tfx_win_export(&A.content, exportp, userio_log) < 0;
        if (A.log)
            fclose(A.log);
        return bad ? 5 : 0;
    }
    if (settings) { /* CONFIG.EXE's place: the settings view, no game */
        FILE *f = script ? _wfopen(script, L"r") : NULL;
        if (script && !f) {
            fail(L"The script cannot be read.", 1);
            return 3;
        }
        find_joystick();
        tfx_win_settings_host sh = {&A.content, h_stick, h_getenv, settings_log, settings_command, f};
        int saved = tfx_win_settings(inst, &sh);
        if (f)
            fclose(f);
        if (A.log)
            fclose(A.log);
        return saved ? 0 : 4;
    }
    if (script && !load_script(script)) {
        fail(L"The script cannot be read.", 1);
        return 3;
    }

    /* the devices */
    timeBeginPeriod(1);
    S.wavp = wavp, S.no_audio = no_audio, S.midi = midi, S.cue = cue;
    if (!open_audio())
        return 3;
    find_joystick();

    /* the window */
    WNDCLASSEXW wc = {.cbSize = sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"TFX";
    RegisterClassExW(&wc);
    RECT r = {0, 0, 960, 720};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    A.wnd = CreateWindowExW(0, L"TFX", L"TFX", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                            r.bottom - r.top, NULL, NULL, inst, NULL);
    add_menu(A.wnd);
    tfx_win_theme_window(A.wnd);
    A.dib.h = (BITMAPINFOHEADER){sizeof A.dib.h, 320, 200, 1, 8, BI_RGB, 0, 0, 0, 256, 0};
    if (!hidden) {
        ShowWindow(A.wnd, show_cmd);
        if (full)
            set_fullscreen(1);
    }

    /* the game: TFX.BAT's "start cd_demo" in C:\TFX */
    A.game = start_game();
    if (!A.game) {
        fail(L"The game could not start (no memory).", console);
        return 3;
    }
    int running = 1;
    while (running) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                running = 0;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!running)
            break;
        if (A.want_about)
            about_in_game();
        if (A.want_settings && !settings_in_game(inst)) {
            fail(L"The game could not start again (no memory).", console);
            A.exit_code = 3;
            break;
        }
        if (A.script)
            run_script();
        push_stick();
        tfx_step_result s = tfx_step(A.game);
        if (A.virtual_clock) /* (each step a little time; a wait a millisecond) */
            A.vclock += s == TFX_STEP_WAIT ? 1000 : 100;
        if (wavp)
            tfx_win_audio_pump(A.audio);
        if (s == TFX_STEP_FRAME)
            take_frame();
        else if (s == TFX_STEP_WAIT && !A.virtual_clock)
            MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
        else if (s == TFX_STEP_ENDED) {
            const char *message = NULL;
            tfx_end e = tfx_ended(A.game, &message);
            if (e == TFX_END_FATAL) {
                logf_line("fatal", message ? message : "");
                wchar_t w[300];
                MultiByteToWideChar(CP_UTF8, 0, message ? message : "", -1, w, 300);
                if (!script)
                    tfx_win_message_box(A.wnd, w, L"TFX", MB_OK | MB_ICONERROR);
                A.exit_code = 1;
            }
            logf_line("host", e == TFX_END_FATAL ? "ended: fatal" : e == TFX_END_REQUEST ? "ended: closed" : "ended: quit");
            running = 0;
        }
        if (A.note_on) { /* the note's fades between the game's frames; gone after its 2 s */
            uint64_t now = real_us();
            if (now - A.note_at >= 2000000)
                A.note_on = 0, repaint();
            else if (now - A.note_shown >= 15000)
                repaint();
        }
    }
    char account[200];
    snprintf(account, sizeof account, "%llu frames; OPL writes %lu, MPU-401 bytes %lu, sample blocks %lu, CD plays %lu, stick reads %lu",
             (unsigned long long)A.frames, n_opl, n_mpu, n_pcm, n_cd, n_stick);
    logf_line("host", account);
    if (wavp) {
        uint64_t frames, clipped;
        int32_t peak;
        tfx_win_audio_wav_stats(A.audio, &frames, &peak, &clipped);
        snprintf(account, sizeof account, "WAV: %llu frames (%.2f s), peak %d, %llu samples at full scale",
                 (unsigned long long)frames, (double)frames / 44100, (int)peak, (unsigned long long)clipped);
        logf_line("host", account);
    }
    tfx_destroy(A.game);
    A.game = NULL;
    tfx_win_audio_close(A.audio);
    if (IsWindow(A.wnd))
        DestroyWindow(A.wnd);
    timeEndPeriod(1);
    if (A.log)
        fclose(A.log);
    if (A.frame_log)
        fclose(A.frame_log);
    if (A.sound_log)
        fclose(A.sound_log);
    LocalFree(argv);
    return A.exit_code;
}
