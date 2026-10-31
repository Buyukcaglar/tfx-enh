#include "win/pack.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/cdaudio.h"
#include "host/chd.h"
#include "host/profile.h"
#include "host/zip.h"
#include "win/content.h"
#include "win/theme.h"

static void fail_with(char *err, size_t cap, const char *why) { snprintf(err, cap, "%s", why); }

/* the folder's file by its profile path (DOS separators) */
static void file_path(const wchar_t *dir, const char *rel, wchar_t *out) {
    swprintf(out, 520, L"%ls\\", dir);
    size_t n = wcslen(out);
    for (const char *p = rel; *p && n < 519; p++)
        out[n++] = (wchar_t)(unsigned char)*p;
    out[n] = 0;
}

/* NAME.new written whole, then moved over NAME */
static int write_whole(const wchar_t *path, const uint8_t *data, size_t n) {
    wchar_t tmp[540];
    swprintf(tmp, 540, L"%ls.new", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int ok = 1;
    for (size_t at = 0; ok && at < n;) {
        DWORD k = 0, want = n - at > (1u << 30) ? 1u << 30 : (DWORD)(n - at);
        ok = WriteFile(h, data + at, want, &k, NULL) && k > 0;
        at += k;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    ok = ok && MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok)
        DeleteFileW(tmp);
    return ok;
}

int tfx_win_pack_zip(const wchar_t *game_dir, const wchar_t *zip, char *err, size_t cap) {
    int count;
    const tfx_profile_file *f = tfx_profile_baseline(&count);
    tfx_zip_out *z = tfx_zip_out_new();
    if (!z) {
        fail_with(err, cap, "out of memory");
        return 0;
    }
    int ok = 1, added = 0;
    for (int i = 0; ok && i < count; i++) {
        wchar_t path[520];
        file_path(game_dir, f[i].path, path);
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (h == INVALID_HANDLE_VALUE)
            continue; /* (a file the folder lacks: the profile check has said so) */
        LARGE_INTEGER size;
        FILETIME t, local;
        WORD date = 0x21, time = 0;
        uint8_t *data = NULL;
        DWORD got = 0;
        ok = GetFileSizeEx(h, &size) && size.QuadPart < (1 << 30) && (data = malloc((size_t)size.QuadPart + 1)) &&
             (size.QuadPart == 0 || (ReadFile(h, data, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart));
        if (GetFileTime(h, NULL, NULL, &t) && FileTimeToLocalFileTime(&t, &local))
            FileTimeToDosDateTime(&local, &date, &time);
        CloseHandle(h);
        char name[260];
        snprintf(name, sizeof name, "%s", f[i].path);
        for (char *p = name; *p; p++)
            if (*p == '\\')
                *p = '/';
        if (ok && !tfx_zip_out_add(z, name, data, (uint32_t)size.QuadPart, time, date))
            ok = 0;
        else
            added++;
        free(data);
        if (!ok)
            snprintf(err, cap, "%s cannot be read", f[i].path);
    }
    size_t n = 0;
    const uint8_t *bytes = ok ? tfx_zip_out_finish(z, &n) : NULL;
    if (ok && (!bytes || !added)) {
        fail_with(err, cap, added ? "out of memory" : "the folder holds none of the game's files");
        ok = 0;
    }
    if (ok && !write_whole(zip, bytes, n)) {
        fail_with(err, cap, "the zip cannot be written");
        ok = 0;
    }
    tfx_zip_out_free(z);
    return ok;
}

int tfx_win_pack_zip_matches(const wchar_t *game_dir, const wchar_t *zip) {
    tfx_win_content c = {0};
    if (!tfx_win_content_open_zip(&c, zip, game_dir)) /* (the store unused: the folder, never written) */
        return 0;
    int count, same = 1;
    const tfx_profile_file *f = tfx_profile_baseline(&count);
    for (int i = 0; i < count && same; i++) {
        wchar_t path[520];
        file_path(game_dir, f[i].path, path);
        same = tfx_win_file_size(path) == tfx_win_content_game_size(&c, f[i].path);
    }
    tfx_win_content_close(&c);
    return same;
}

/* --- the CD ------------------------------------------------------------------------ */

typedef struct disc {
    tfx_cd_image img;
    tfx_chd_layout layout;
    wchar_t dir[260];
} disc;

static void sheet_file(const disc *d, const char *name, wchar_t *out) {
    swprintf(out, 520, L"%ls\\", d->dir);
    size_t n = wcslen(out);
    MultiByteToWideChar(CP_ACP, 0, name, -1, out + n, (int)(520 - n));
}

static long long sheet_size(void *ctx, const char *name) {
    wchar_t p[520];
    sheet_file(ctx, name, p);
    return tfx_win_file_size(p);
}

typedef struct worker {
    const disc *d;
    HANDLE files[TFX_CD_FILES];
    tfx_deflater *z;
    uint32_t from, to; /* hunks [from, to) of the batch, every step-th */
    uint32_t step, first;
    uint8_t *raw, *packed; /* the batch's, TFX_CHD_HUNK a hunk */
    uint32_t *len;
    int *codec;
    int ok;
} worker;

static size_t worker_read(void *ctx, int file, uint64_t offset, uint8_t *buf, size_t n) {
    worker *w = ctx;
    if (!w->files[file]) {
        wchar_t p[520];
        sheet_file(w->d, w->d->img.file[file], p);
        w->files[file] = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    }
    OVERLAPPED o = {0};
    o.Offset = (DWORD)offset, o.OffsetHigh = (DWORD)(offset >> 32);
    DWORD got = 0;
    return w->files[file] != INVALID_HANDLE_VALUE && ReadFile(w->files[file], buf, (DWORD)n, &got, &o) ? got : 0;
}

static DWORD WINAPI work_hunks(void *arg) {
    worker *w = arg;
    for (uint32_t h = w->from; h < w->to && w->ok; h += w->step) {
        uint32_t k = h - w->first;
        uint8_t *raw = w->raw + (size_t)k * TFX_CHD_HUNK;
        int kind = tfx_chd_raw_hunk(&w->d->layout, &w->d->img, h, raw, worker_read, w);
        if (!kind)
            w->ok = 0;
        else
            w->len[k] = (uint32_t)tfx_chd_compress(w->z, raw, kind, w->packed + (size_t)k * TFX_CHD_HUNK, &w->codec[k]);
    }
    return 0;
}

static int load_sheet(disc *d, const wchar_t *cue, char *err, size_t cap) {
    memset(d, 0, sizeof *d);
    char text[16384];
    DWORD n = 0;
    HANDLE h = CreateFileW(cue, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BOOL read_ok = h != INVALID_HANDLE_VALUE && ReadFile(h, text, sizeof text, &n, NULL);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (!read_ok || n == sizeof text) {
        fail_with(err, cap, "the CD sheet cannot be read");
        return 0;
    }
    wcsncpy(d->dir, cue, 259);
    wchar_t *slash = wcsrchr(d->dir, L'\\'), *s2 = wcsrchr(d->dir, L'/');
    if (s2 > slash)
        slash = s2;
    if (slash)
        *slash = 0;
    else
        wcscpy(d->dir, L".");
    if (!tfx_cdi_parse(&d->img, text, n, sheet_size, d, err, cap))
        return 0;
    tfx_chd_layout_cd(&d->layout, &d->img);
    return 1;
}

static int handle_read(void *ctx, uint64_t offset, void *buf, size_t n) {
    OVERLAPPED o = {0};
    o.Offset = (DWORD)offset, o.OffsetHigh = (DWORD)(offset >> 32);
    DWORD got = 0;
    return ReadFile(ctx, buf, (DWORD)n, &got, &o) && got == n;
}

int tfx_win_pack_chd_matches(const wchar_t *cue, const wchar_t *chd) {
    static disc d;
    static tfx_chd c;
    tfx_cd_image back;
    char err[200];
    HANDLE h = CreateFileW(chd, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    int same = h != INVALID_HANDLE_VALUE && load_sheet(&d, cue, err, sizeof err) &&
               tfx_chd_open(&c, handle_read, h, err, sizeof err);
    if (same) {
        tfx_chd_cd_image(&c, &back);
        same = back.n_tracks == d.img.n_tracks && back.end_lba == d.img.end_lba;
        for (int i = 0; same && i < back.n_tracks; i++)
            same = back.track[i].lba == d.img.track[i].lba && back.track[i].audio == d.img.track[i].audio;
        tfx_chd_close(&c);
    }
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    return same;
}

int tfx_win_pack_chd(const wchar_t *cue, const wchar_t *chd, tfx_win_pack_progress progress, void *ctx, char *err,
                     size_t cap) {
    static disc d;
    if (!load_sheet(&d, cue, err, cap))
        return 0;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint32_t threads = si.dwNumberOfProcessors < 1 ? 1 : si.dwNumberOfProcessors > 32 ? 32 : si.dwNumberOfProcessors;
    uint32_t batch = 64 * threads, hunks = d.layout.hunks;
    worker w[32] = {0};
    uint8_t *raw = malloc((size_t)batch * TFX_CHD_HUNK), *packed = malloc((size_t)batch * TFX_CHD_HUNK);
    uint32_t *len = malloc(sizeof *len * batch);
    int *codec = malloc(sizeof *codec * batch);
    tfx_chd_writer *cw = tfx_chd_writer_new(&d.layout);
    int ok = raw && packed && len && codec && cw;
    for (uint32_t t = 0; ok && t < threads; t++)
        ok = (w[t].z = tfx_deflater_new()) != NULL;
    wchar_t tmp[540];
    swprintf(tmp, 540, L"%ls.new", chd);
    HANDLE out = ok ? CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)
                    : INVALID_HANDLE_VALUE;
    if (!ok)
        fail_with(err, cap, "out of memory");
    else if (out == INVALID_HANDLE_VALUE) {
        fail_with(err, cap, "the CHD cannot be written");
        ok = 0;
    }
    uint8_t header[TFX_CHD_HEADER] = {0};
    DWORD k;
    ok = ok && WriteFile(out, header, sizeof header, &k, NULL);
    for (uint32_t first = 0; ok && first < hunks; first += batch) {
        uint32_t to = first + batch < hunks ? first + batch : hunks;
        HANDLE th[32];
        for (uint32_t t = 0; t < threads; t++) {
            w[t].d = &d, w[t].from = first + t, w[t].to = to, w[t].step = threads, w[t].first = first;
            w[t].raw = raw, w[t].packed = packed, w[t].len = len, w[t].codec = codec, w[t].ok = 1;
            th[t] = CreateThread(NULL, 0, work_hunks, &w[t], 0, NULL);
            if (!th[t])
                work_hunks(&w[t]), th[t] = NULL;
        }
        for (uint32_t t = 0; t < threads; t++)
            if (th[t]) {
                WaitForSingleObject(th[t], INFINITE);
                CloseHandle(th[t]);
            }
        for (uint32_t t = 0; t < threads; t++)
            if (!w[t].ok) {
                fail_with(err, cap, "the CD image's files cannot be read");
                ok = 0;
            }
        for (uint32_t i = 0; ok && i < to - first; i++) {
            ok = WriteFile(out, packed + (size_t)i * TFX_CHD_HUNK, len[i], &k, NULL) && k == len[i];
            tfx_chd_put(cw, raw + (size_t)i * TFX_CHD_HUNK, len[i], codec[i]);
        }
        if (progress)
            progress(ctx, to, hunks);
    }
    const uint8_t *tail;
    size_t tn = 0;
    ok = ok && tfx_chd_finish(cw, &tail, &tn, header) && WriteFile(out, tail, (DWORD)tn, &k, NULL) && k == tn;
    LARGE_INTEGER zero = {0};
    ok = ok && SetFilePointerEx(out, zero, NULL, FILE_BEGIN) && WriteFile(out, header, sizeof header, &k, NULL) &&
         FlushFileBuffers(out);
    if (out != INVALID_HANDLE_VALUE)
        CloseHandle(out);
    if (ok && !MoveFileExW(tmp, chd, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail_with(err, cap, "the CHD cannot be written");
        ok = 0;
    }
    if (!ok)
        DeleteFileW(tmp);
    for (uint32_t t = 0; t < threads; t++) {
        tfx_deflater_free(w[t].z);
        for (int f = 0; f < TFX_CD_FILES; f++)
            if (w[t].files[f] && w[t].files[f] != INVALID_HANDLE_VALUE)
                CloseHandle(w[t].files[f]);
    }
    tfx_chd_writer_free(cw);
    free(raw);
    free(packed);
    free(len);
    free(codec);
    return ok;
}

/* --- the window while it works ----------------------------------------------------- */

typedef struct run {
    int (*work)(void *arg, tfx_win_pack_progress progress, void *ctx);
    void *arg;
    volatile LONG done, total;
    int result;
    const wchar_t *text;
} run;

static void on_progress(void *ctx, unsigned done, unsigned total) {
    run *r = ctx;
    InterlockedExchange(&r->total, (LONG)total);
    InterlockedExchange(&r->done, (LONG)done);
}

static DWORD WINAPI run_thread(void *arg) {
    run *r = arg;
    r->result = r->work(r->arg, on_progress, r);
    return 0;
}

static LRESULT CALLBACK pack_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        run *r = (run *)GetWindowLongPtrW(w, GWLP_USERDATA);
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT c;
        GetClientRect(w, &c);
        int dark = tfx_win_theme_dark(); /* (the system's mode, win/theme.h) */
        FillRect(dc, &c, dark ? tfx_win_theme_color(0, NULL, NULL) : GetSysColorBrush(COLOR_BTNFACE));
        NONCLIENTMETRICSW m = {.cbSize = sizeof m};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
        HFONT font = CreateFontIndirectW(&m.lfMessageFont), old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark ? TFX_DARK_TEXT : GetSysColor(COLOR_BTNTEXT));
        RECT t = {16, 14, c.right - 16, 60};
        if (r)
            DrawTextW(dc, r->text, -1, &t, DT_WORDBREAK);
        RECT bar = {16, c.bottom - 34, c.right - 16, c.bottom - 18};
        HBRUSH edge = CreateSolidBrush(dark ? RGB(100, 100, 100) : GetSysColor(COLOR_BTNSHADOW));
        FrameRect(dc, &bar, edge);
        DeleteObject(edge);
        if (r && r->total > 0) {
            RECT fill = {bar.left + 1, bar.top + 1, bar.left + 1 + (LONG)((bar.right - bar.left - 2) * (double)r->done / r->total), bar.bottom - 1};
            FillRect(dc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
        }
        SelectObject(dc, old);
        DeleteObject(font);
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_CLOSE)
        return 0; /* (the copy finishes: a half one is never left) */
    if (msg == WM_SETTINGCHANGE && tfx_win_theme_changed(lp))
        tfx_win_theme_refresh(), tfx_win_theme_window(w);
    return DefWindowProcW(w, msg, wp, lp);
}

int tfx_win_pack_run(void *instance, const wchar_t *text, int hidden,
                     int (*work)(void *arg, tfx_win_pack_progress progress, void *ctx), void *arg) {
    run r = {work, arg, 0, 0, 0, text};
    if (hidden)
        return work(arg, NULL, NULL);
    WNDCLASSEXW wc = {.cbSize = sizeof wc};
    wc.lpfnWndProc = pack_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"TFXPack";
    RegisterClassExW(&wc);
    int width = 460, height = 130;
    HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TFXPack", L"TFX", WS_CAPTION | WS_SYSMENU,
                             (GetSystemMetrics(SM_CXSCREEN) - width) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                             width, height, NULL, NULL, instance, NULL);
    if (w) {
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)&r);
        tfx_win_theme_window(w);
        ShowWindow(w, SW_SHOW);
        UpdateWindow(w);
    }
    HANDLE th = CreateThread(NULL, 0, run_thread, &r, 0, NULL);
    if (!th)
        run_thread(&r);
    LONG shown = -1;
    while (th && MsgWaitForMultipleObjects(1, &th, FALSE, 100, QS_ALLINPUT) != WAIT_OBJECT_0) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (w && r.done != shown)
            shown = r.done, InvalidateRect(w, NULL, FALSE);
    }
    if (th)
        CloseHandle(th);
    if (w)
        DestroyWindow(w);
    return r.result;
}
