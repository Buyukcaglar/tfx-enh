#include "win/userio.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/did.h"
#include "formats/par.h"
#include "formats/resource.h"
#include "host/userfiles.h"

#define SCRIPT_PATH "C:\\TFX\\cd_demo.par"

/* a whole file by its Windows path; NULL missing or unreadable */
static uint8_t *slurp(const wchar_t *path, size_t *len) {
    long long n = tfx_win_file_size(path);
    if (n < 0 || n > 64 << 20)
        return NULL;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    uint8_t *b = malloc(n ? (size_t)n : 1);
    size_t got = 0;
    int ok = b != NULL;
    while (ok && got < (size_t)n) {
        DWORD k = 0;
        ok = ReadFile(h, b + got, (DWORD)((size_t)n - got), &k, NULL) && k > 0;
        got += k;
    }
    CloseHandle(h);
    if (!ok) {
        free(b);
        return NULL;
    }
    *len = (size_t)n;
    return b;
}

/* the temporary file, flushed, moved over the old one */
static int safe_write(const wchar_t *path, const uint8_t *buf, size_t n) {
    wchar_t tmp[280];
    if (wcslen(path) + 5 > 280)
        return 0;
    swprintf(tmp, 280, L"%ls.new", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    size_t put = 0;
    int ok = 1;
    while (ok && put < n) {
        DWORD k = 0;
        ok = WriteFile(h, buf + put, (DWORD)(n - put), &k, NULL) && k > 0;
        put += k;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    ok = ok && MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok)
        DeleteFileW(tmp);
    return ok;
}

static int no_loose(void *ctx, const char *name, const uint8_t **data, size_t *len) {
    (void)ctx, (void)name, (void)data, (void)len;
    return 0;
}

/* the flow script as the game reads it (tfx_core_content): DID.DAT's
 * record, else the loose file */
static tfx_par *load_script(const tfx_win_content *c, uint8_t **bytes) {
    uint32_t n = 0;
    *bytes = NULL;
    size_t did_len = 0;
    uint8_t *did_bytes = tfx_win_content_game_load(c, "C:\\TFX\\DID.DAT", &did_len);
    tfx_did *did = malloc(sizeof *did);
    tfx_did_record rec;
    if (did_bytes && did && tfx_did_open(did, did_bytes, did_len) == TFX_DID_OK &&
        tfx_did_lookup(did, SCRIPT_PATH, "C:\\TFX", &rec) == TFX_DID_OK) {
        tfx_resources r = {did, "C:\\TFX", {no_loose, NULL}, 0, 0};
        uint32_t size = tfx_res_size(&r, SCRIPT_PATH);
        size_t got;
        if (size && (*bytes = malloc(size)) && tfx_res_load(&r, SCRIPT_PATH, *bytes, size, &got) == TFX_RES_OK)
            n = (uint32_t)got;
    } else if ((n = tfx_win_content_size((void *)c, SCRIPT_PATH)) && (*bytes = malloc(n)) &&
               !tfx_win_content_read((void *)c, SCRIPT_PATH, *bytes, n))
        n = 0;
    free(did);
    free(did_bytes);
    tfx_par *p = malloc(sizeof *p);
    if (!n || !p || tfx_par_parse(p, *bytes, n) != TFX_PAR_OK) {
        free(p);
        free(*bytes);
        *bytes = NULL;
        return NULL;
    }
    return p;
}

int tfx_win_import(const tfx_win_content *c, const wchar_t *file, char *why, size_t cap) {
    const wchar_t *base = file;
    for (const wchar_t *s = file; *s; s++)
        if (*s == L'\\' || *s == L'/')
            base = s + 1;
    char name[16];
    size_t k = 0;
    for (; base[k] && k < sizeof name - 1; k++) {
        if (base[k] > 0x7e) {
            snprintf(why, cap, "not a name the game writes");
            return 0;
        }
        name[k] = (char)base[k];
    }
    name[k] = 0;
    int slot = 0;
    int kind = base[k] ? -1 : tfx_userfile_kind(name, &slot);
    if (kind < 0) {
        snprintf(why, cap, "not a name the game writes (TFX.CFG, TFX.HS, TFXSAV.<n>, TFXSAV.<n>L, VIDEO.REC)");
        return 0;
    }
    size_t len = 0;
    uint8_t *data = slurp(file, &len);
    if (!data) {
        snprintf(why, cap, "cannot be read");
        return 0;
    }
    uint8_t *script_bytes = NULL;
    tfx_par *script = kind == TFX_USER_PILOT || kind == TFX_USER_PILOT_LOG ? load_script(c, &script_bytes) : NULL;
    int ok = tfx_userfile_check((tfx_user_file)kind, data, len, script, why, cap);
    free(script);
    free(script_bytes);
    if (ok) {
        char store_name[16], dos[32];
        tfx_user_file_name((tfx_user_file)kind, slot, store_name, sizeof store_name);
        snprintf(dos, sizeof dos, "C:\\TFX\\%s", store_name);
        wchar_t w[260];
        size_t had_len = 0;
        uint8_t *had = tfx_win_content_locate(c, dos, 1, w, 260) ? slurp(w, &had_len) : NULL;
        if (had && had_len == len && !memcmp(had, data, len))
            snprintf(why + strlen(why), cap - strlen(why), "; already the store's %s", store_name);
        else if (!tfx_win_content_write((void *)c, dos, data, (uint32_t)len)) {
            snprintf(why, cap, "the store's %s cannot be written (the old file is kept)", store_name);
            ok = 0;
        } else
            snprintf(why + strlen(why), cap - strlen(why), "; the store's %s %s", store_name,
                     had ? "replaced" : "written");
        free(had);
    }
    free(data);
    return ok;
}

/* the full path, without case, and a separator after the prefix */
static int inside(const wchar_t *path, const wchar_t *dir) {
    size_t n = wcslen(dir);
    return !_wcsnicmp(path, dir, n) && (path[n] == 0 || path[n] == L'\\' || path[n] == L'/');
}

static int ends_new(const wchar_t *name) {
    size_t n = wcslen(name);
    return n > 4 && !_wcsicmp(name + n - 4, L".new");
}

typedef int (*visit_fn)(const wchar_t *path, const wchar_t *rel, void *ctx);

/* every file under root, depth first; returns the sum of the visits, -1 on a failed visit */
static int walk(const wchar_t *root, const wchar_t *rel, visit_fn visit, void *ctx) {
    wchar_t pattern[300];
    swprintf(pattern, 300, rel[0] ? L"%ls\\%ls\\*" : L"%ls%ls\\*", root, rel);
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(pattern, &d);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int sum = 0;
    do {
        if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L".."))
            continue;
        wchar_t r[260], p[300];
        swprintf(r, 260, rel[0] ? L"%ls\\%ls" : L"%ls%ls", rel, d.cFileName);
        swprintf(p, 300, L"%ls\\%ls", root, r);
        int got = d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? walk(root, r, visit, ctx) : visit(p, r, ctx);
        if (got < 0) {
            sum = -1;
            break;
        }
        sum += got;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return sum;
}

typedef struct export_ctx {
    const wchar_t *dir;
    tfx_win_userio_log log;
} export_ctx;

static void logw(tfx_win_userio_log log, const char *what, const wchar_t *rel) {
    char line[400], r[260];
    WideCharToMultiByte(CP_UTF8, 0, rel, -1, r, sizeof r, NULL, NULL);
    snprintf(line, sizeof line, "%s %s", what, r);
    log(line);
}

static int export_one(const wchar_t *path, const wchar_t *rel, void *ctx) {
    export_ctx *e = ctx;
    if (ends_new(rel))
        return 0;
    wchar_t out[300];
    swprintf(out, 300, L"%ls\\%ls", e->dir, rel);
    for (wchar_t *s = out + wcslen(e->dir) + 1; *s; s++)
        if (*s == L'\\') {
            *s = 0;
            CreateDirectoryW(out, NULL);
            *s = L'\\';
        }
    size_t n;
    uint8_t *b = slurp(path, &n);
    int ok = b && safe_write(out, b, n);
    free(b);
    logw(e->log, ok ? "exported" : "export failed:", rel);
    return ok ? 1 : -1;
}

int tfx_win_export(const tfx_win_content *c, const wchar_t *dir, tfx_win_userio_log log) {
    wchar_t full[260];
    DWORD n = GetFullPathNameW(dir, 260, full, NULL);
    if (!n || n >= 260) {
        log("export refused: the folder's name is too long");
        return -1;
    }
    for (wchar_t *d = full + wcslen(full) - 1; d > full + 2 && (*d == L'\\' || *d == L'/'); d--)
        *d = 0;
    if (inside(full, c->game) || inside(c->game, full) || inside(full, c->store) || inside(c->store, full)) {
        log("export refused: the folder is the game folder or the user store, or holds or is inside one");
        return -1;
    }
    CreateDirectoryW(full, NULL);
    DWORD a = GetFileAttributesW(full);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        log("export refused: the folder cannot be created");
        return -1;
    }
    export_ctx e = {full, log};
    return walk(c->store, L"", export_one, &e);
}

static int recover_one(const wchar_t *path, const wchar_t *rel, void *ctx) {
    tfx_win_userio_log log = *(tfx_win_userio_log *)ctx;
    if (!ends_new(rel))
        return 0;
    wchar_t target[300];
    wcscpy(target, path);
    target[wcslen(target) - 4] = 0;
    int had = tfx_win_file_size(target) >= 0;
    int ok = DeleteFileW(path) != 0;
    logw(log, !ok ? "recovery: cannot remove" : had ? "recovery: removed an unfinished write (the last good file kept):"
                                                    : "recovery: removed an unfinished write (no earlier file):",
         rel);
    return ok;
}

int tfx_win_recover(const tfx_win_content *c, tfx_win_userio_log log) {
    return walk(c->store, L"", recover_one, &log);
}
