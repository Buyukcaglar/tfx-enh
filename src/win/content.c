#include "win/content.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include "host/dospath.h"

static int join(wchar_t *out, size_t cap, const wchar_t *dir, const char *rel) {
    size_t n = wcslen(dir);
    if (n + 1 + strlen(rel) + 1 > cap)
        return 0;
    wcscpy(out, dir);
    if (*rel) {
        out[n++] = L'\\';
        for (const char *p = rel; *p; p++) /* (DOS names are ASCII) */
            out[n++] = (wchar_t)(unsigned char)*p;
        out[n] = 0;
    }
    return 1;
}

static int is_file(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int open_store(tfx_win_content *c, const wchar_t *store);

int tfx_win_content_open(tfx_win_content *c, const wchar_t *game, const wchar_t *store) {
    tfx_win_content_close(c);
    if (wcslen(game) >= 260 || wcslen(store) >= 260)
        return 0;
    wcscpy(c->game, game);
    for (wchar_t *d = c->game[0] ? c->game + wcslen(c->game) - 1 : c->game; d > c->game && (*d == L'\\' || *d == L'/'); d--)
        *d = 0;
    DWORD a = GetFileAttributesW(c->game);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    return open_store(c, store);
}

int tfx_win_content_open_zip(tfx_win_content *c, const wchar_t *zip, const wchar_t *store) {
    tfx_win_content_close(c);
    if (wcslen(zip) >= 260 || wcslen(store) >= 260)
        return 0;
    long long n = tfx_win_file_size(zip);
    HANDLE h = n > 22 && n < (1ll << 30) ? CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)
                                          : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    uint8_t *data = malloc((size_t)n);
    DWORD got = 0;
    int ok = data && ReadFile(h, data, (DWORD)n, &got, NULL) && got == (DWORD)n;
    CloseHandle(h);
    if (!ok || !tfx_zip_open(&c->zip, data, (size_t)n) ||
        !(c->zip_files = calloc((size_t)c->zip.count + 1, sizeof *c->zip_files))) {
        tfx_zip_close(&c->zip);
        free(data);
        return 0;
    }
    c->zip_data = data;
    wcscpy(c->game, zip);
    return open_store(c, store);
}

void tfx_win_content_close(tfx_win_content *c) {
    if (c->zip_files)
        for (int i = 0; i < c->zip.count; i++)
            free(c->zip_files[i]);
    free(c->zip_files);
    tfx_zip_close(&c->zip);
    free(c->zip_data);
    c->zip_files = NULL, c->zip_data = NULL;
}

static int open_store(tfx_win_content *c, const wchar_t *store) {
    DWORD a;
    wcscpy(c->store, store);
    /* the store and its parents */
    wchar_t p[260];
    wcscpy(p, c->store);
    for (wchar_t *s = p + 3; *s; s++)
        if (*s == L'\\' || *s == L'/') {
            wchar_t k = *s;
            *s = 0;
            CreateDirectoryW(p, NULL);
            *s = k;
        }
    CreateDirectoryW(p, NULL);
    a = GetFileAttributesW(c->store);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int tfx_win_content_locate(const tfx_win_content *c, const char *dos_path, int store, wchar_t *out, size_t cap) {
    char rel[260];
    if (!tfx_dos_relative(dos_path, rel, sizeof rel) || (!store && c->zip_data))
        return 0;
    return join(out, cap, store ? c->store : c->game, rel);
}

/* the zip's entry for a DOS path, -1 none (or no zip) */
static int zip_entry(const tfx_win_content *c, const char *dos_path) {
    char rel[260];
    return c->zip_data && tfx_dos_relative(dos_path, rel, sizeof rel) ? tfx_zip_find(&c->zip, rel) : -1;
}

/* the entry's bytes, inflated once (NULL damaged) */
static const uint8_t *zip_bytes(const tfx_win_content *c, int i) {
    if (!c->zip_files[i]) {
        uint8_t *p = malloc(c->zip.entry[i].size + 1);
        if (p && !tfx_zip_extract(&c->zip, i, p))
            free(p), p = NULL;
        c->zip_files[i] = p;
    }
    return c->zip_files[i];
}

long long tfx_win_content_game_size(const tfx_win_content *c, const char *dos_path) {
    wchar_t w[260];
    int i = zip_entry(c, dos_path);
    if (c->zip_data)
        return i < 0 ? -1 : (long long)c->zip.entry[i].size;
    return tfx_win_content_locate(c, dos_path, 0, w, 260) ? tfx_win_file_size(w) : -1;
}

uint8_t *tfx_win_content_game_load(const tfx_win_content *c, const char *dos_path, size_t *n) {
    long long size = tfx_win_content_game_size(c, dos_path);
    uint8_t *p = size >= 0 && size < (1ll << 30) ? malloc((size_t)size + 1) : NULL;
    if (!p)
        return NULL;
    int ok = 0;
    if (c->zip_data) {
        int i = zip_entry(c, dos_path);
        const uint8_t *z = i >= 0 ? zip_bytes(c, i) : NULL;
        if (z)
            memcpy(p, z, (size_t)size), ok = 1;
    } else {
        wchar_t w[260];
        HANDLE h = tfx_win_content_locate(c, dos_path, 0, w, 260)
                       ? CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)
                       : INVALID_HANDLE_VALUE;
        DWORD got = 0;
        ok = h != INVALID_HANDLE_VALUE && (size == 0 || (ReadFile(h, p, (DWORD)size, &got, NULL) && got == (DWORD)size));
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    if (!ok)
        free(p), p = NULL;
    else if (n)
        *n = (size_t)size;
    return p;
}

/* the file a read sees: the store's, else the game folder's */
static int resolve(const tfx_win_content *c, const char *path, wchar_t *out) {
    return (tfx_win_content_locate(c, path, 1, out, 260) && is_file(out)) ||
           (tfx_win_content_locate(c, path, 0, out, 260) && is_file(out));
}

long long tfx_win_file_size(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    return (long long)d.nFileSizeHigh << 32 | d.nFileSizeLow;
}

uint32_t tfx_win_content_size(void *ctx, const char *path) {
    wchar_t w[260];
    tfx_win_content *c = ctx;
    if (c->zip_data && !(tfx_win_content_locate(c, path, 1, w, 260) && is_file(w))) {
        int i = zip_entry(c, path);
        return i < 0 ? 0 : c->zip.entry[i].size;
    }
    if (!resolve(ctx, path, w))
        return 0;
    long long n = tfx_win_file_size(w);
    return n < 0 || n > 0xffffffffLL ? 0 : (uint32_t)n;
}

int tfx_win_content_read(void *ctx, const char *path, uint8_t *buf, uint32_t cap) {
    wchar_t w[260];
    tfx_win_content *c = ctx;
    if (c->zip_data && !(tfx_win_content_locate(c, path, 1, w, 260) && is_file(w))) {
        int i = zip_entry(c, path);
        const uint8_t *z = i >= 0 && c->zip.entry[i].size <= cap ? zip_bytes(c, i) : NULL;
        if (z)
            memcpy(buf, z, c->zip.entry[i].size);
        return z != NULL;
    }
    if (!resolve(ctx, path, w))
        return 0;
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    LARGE_INTEGER size;
    int ok = GetFileSizeEx(h, &size) && size.QuadPart <= cap;
    uint32_t got = 0;
    while (ok && got < (uint32_t)size.QuadPart) {
        DWORD n = 0;
        ok = ReadFile(h, buf + got, (DWORD)size.QuadPart - got, &n, NULL) && n > 0;
        got += n;
    }
    CloseHandle(h);
    return ok;
}

int tfx_win_content_write(void *ctx, const char *path, const uint8_t *buf, uint32_t n) {
    tfx_win_content *c = ctx;
    wchar_t w[260], tmp[270];
    if (!tfx_win_content_locate(c, path, 1, w, 260))
        return 0;
    /* a file in a folder of the game's: the store's copy of the folder */
    for (wchar_t *s = w + wcslen(c->store) + 1; *s; s++)
        if (*s == L'\\') {
            *s = 0;
            CreateDirectoryW(w, NULL);
            *s = L'\\';
        }
    if (wcslen(w) + 5 > 270)
        return 0;
    wcscpy(tmp, w);
    wcscat(tmp, L".new");
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    uint32_t put = 0;
    int ok = 1;
    while (ok && put < n) {
        DWORD k = 0;
        ok = WriteFile(h, buf + put, n - put, &k, NULL) && k > 0;
        put += k;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok)
        ok = MoveFileExW(tmp, w, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok)
        DeleteFileW(tmp);
    return ok;
}

/* --- find ---------------------------------------------------------------- */

typedef struct {
    char name[13];
    int dir;
} entry;

static int by_name(const void *a, const void *b) {
    const entry *x = a, *y = b;
    return strcmp(x->name, y->name);
}

static void upper(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 'a' + 'A');
}

/* the folder's 8.3 entries into list (deduplicated by name) */
static int list_dir(const wchar_t *dir, entry *list, int n, int cap) {
    wchar_t pat[270];
    if (wcslen(dir) + 3 > 270)
        return n;
    wcscpy(pat, dir);
    wcscat(pat, L"\\*");
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &d, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return n;
    do {
        char name[16];
        int len = WideCharToMultiByte(CP_ACP, 0, d.cFileName, -1, name, sizeof name, NULL, NULL);
        if (len <= 0 || !tfx_dos_83(name))
            continue;
        upper(name);
        int dup = 0;
        for (int i = 0; i < n && !dup; i++)
            dup = !strcmp(list[i].name, name);
        if (dup || n >= cap || (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || !strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        strcpy(list[n].name, name);
        list[n].dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        n++;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return n;
}

static char up(char a) {
    if (a == '/')
        a = '\\';
    return a >= 'a' && a <= 'z' ? (char)(a - 32) : a;
}

/* the zip's names in the folder dir (DOS separators, "" the top), files
 * and the folders their paths pass through */
static int list_zip(const tfx_win_content *c, const char *dir, entry *list, int n, int cap) {
    size_t dl = strlen(dir);
    for (int i = 0; i < c->zip.count; i++) {
        const char *p = c->zip.entry[i].name;
        size_t j = 0;
        while (j < dl && p[j] && up(p[j]) == up(dir[j]))
            j++;
        if (j < dl || (dl && up(p[dl]) != '\\'))
            continue;
        const char *rest = p + dl + (dl ? 1 : 0), *end = rest;
        while (*end && up(*end) != '\\')
            end++;
        char name[16];
        if (end == rest || end - rest > 12)
            continue;
        memcpy(name, rest, (size_t)(end - rest));
        name[end - rest] = 0;
        upper(name);
        int dup = 0;
        for (int k = 0; k < n && !dup; k++)
            dup = !strcmp(list[k].name, name);
        if (dup || n >= cap || !tfx_dos_83(name))
            continue;
        strcpy(list[n].name, name);
        list[n].dir = *end != 0;
        n++;
    }
    return n;
}

int tfx_win_content_find(void *ctx, const char *pattern, uint16_t attr, uint32_t k, char name[13]) {
    tfx_win_content *c = ctx;
    char rel[260], dir[260], last[16];
    if (!tfx_dos_relative(pattern, rel, sizeof rel) || !tfx_dos_split(rel, dir, sizeof dir, last, sizeof last))
        return 0;
    enum { CAP = 1024 };
    entry *list = malloc(CAP * sizeof *list);
    if (!list)
        return 0;
    wchar_t w[260];
    int n = 0;
    if (join(w, 260, c->store, dir))
        n = list_dir(w, list, n, CAP);
    if (c->zip_data)
        n = list_zip(c, dir, list, n, CAP);
    else if (join(w, 260, c->game, dir))
        n = list_dir(w, list, n, CAP);
    qsort(list, (size_t)n, sizeof *list, by_name);
    /* DOS: directories only when asked for (0x10), "." and ".." first in a
     * subdirectory (the game folder and every folder in it) */
    uint32_t i = 0;
    int found = 0;
    if (attr & 0x10)
        for (int d = 0; d < 2 && !found; d++) {
            const char *dot = d ? ".." : ".";
            if (tfx_dos_match(last, dot) && i++ == k)
                strcpy(name, dot), found = 1;
        }
    for (int j = 0; j < n && !found; j++)
        if ((!list[j].dir || (attr & 0x10)) && tfx_dos_match(last, list[j].name) && i++ == k)
            strcpy(name, list[j].name), found = 1;
    free(list);
    return found;
}
