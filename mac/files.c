#include "files.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *relative(const char *dos) {
    if (!strncasecmp(dos, TFX_MAC_DID "\\", sizeof TFX_MAC_DID))
        return dos + sizeof TFX_MAC_DID;
    if (!strcasecmp(dos, TFX_MAC_DID))
        return "";
    if (dos[0] && dos[1] == ':')
        dos += 2;
    while (*dos == '\\' || *dos == '/')
        dos++;
    return dos;
}

static void append(char *out, size_t n, const char *part) {
    size_t l = strlen(out);
    snprintf(out + l, n > l ? n - l : 0, "/%s", part);
}

int tfx_mac_resolve(const char *dir, const char *dos, char *out, size_t n, int last_may_miss) {
    char buf[TFX_MAC_PATH];
    snprintf(buf, sizeof buf, "%s", relative(dos));
    snprintf(out, n, "%s", dir);
    char *save, *part = strtok_r(buf, "\\/", &save);
    while (part) {
        char *next = strtok_r(NULL, "\\/", &save);
        int found = 0;
        if (strcmp(part, ".")) {
            DIR *d = opendir(out);
            struct dirent *e;
            while (d && (e = readdir(d)))
                if (!strcasecmp(e->d_name, part)) {
                    append(out, n, e->d_name);
                    found = 1;
                    break;
                }
            if (d)
                closedir(d);
        } else
            found = 1;
        if (!found) {
            if (next || !last_may_miss)
                return 0;
            append(out, n, part);
        }
        part = next;
    }
    return 1;
}

int tfx_mac_match(const char *pattern, const char *name) {
    for (; *pattern; pattern++, name++) {
        if (*pattern == '*') {
            for (const char *t = name;; t++) {
                if (tfx_mac_match(pattern + 1, t))
                    return 1;
                if (!*t)
                    return 0;
            }
        }
        if (!*name || (*pattern != '?' && toupper((unsigned char)*pattern) != toupper((unsigned char)*name)))
            return 0;
    }
    return !*name;
}

/* the file a read sees: the store's, else the originals' (regular files only) */
static FILE *open_read(const tfx_mac_files *f, const char *dos) {
    const char *dirs[2] = {f->store, f->root};
    for (int k = 0; k < 2; k++) {
        char full[TFX_MAC_PATH];
        struct stat st;
        if (dirs[k][0] && tfx_mac_resolve(dirs[k], dos, full, sizeof full, 0) && !stat(full, &st) &&
            S_ISREG(st.st_mode))
            return fopen(full, "rb");
    }
    return NULL;
}

uint32_t tfx_mac_size(const tfx_mac_files *f, const char *dos) {
    FILE *h = open_read(f, dos);
    if (!h)
        return 0;
    fseek(h, 0, SEEK_END);
    long n = ftell(h);
    fclose(h);
    return n > 0 && n <= 0x7fffffff ? (uint32_t)n : 0;
}

int tfx_mac_read(const tfx_mac_files *f, const char *dos, uint8_t *buf, uint32_t cap) {
    FILE *h = open_read(f, dos);
    if (!h)
        return 0;
    size_t n = fread(buf, 1, cap, h);
    int bad = ferror(h);
    fclose(h);
    return !bad && n > 0;
}

static const char *base_name(const char *dos) {
    const char *b = dos;
    for (const char *q = dos; *q; q++)
        if (*q == '\\' || *q == '/' || *q == ':')
            b = q + 1;
    return b;
}

int tfx_mac_write(const tfx_mac_files *f, const char *dos, const uint8_t *buf, uint32_t n) {
    const char *b = base_name(dos);
    if (!f->store[0] || !*b || !strcmp(b, ".") || !strcmp(b, ".."))
        return 0;
    mkdir(f->store, 0755);
    char full[TFX_MAC_PATH], tmp[TFX_MAC_PATH + 16];
    if (!tfx_mac_resolve(f->store, b, full, sizeof full, 1))
        return 0;
    snprintf(tmp, sizeof tmp, "%s.part", full);
    FILE *h = fopen(tmp, "wb");
    if (!h)
        return 0;
    int ok = fwrite(buf, 1, n, h) == n;
    ok &= fflush(h) == 0 && fsync(fileno(h)) == 0;
    ok &= fclose(h) == 0;
    if (ok && rename(tmp, full) == 0)
        return 1;
    unlink(tmp);
    return 0;
}

/* the store's file replaced, its version kept: .history/<name>~1 (the
 * newest) .. ~TFX_MAC_KEPT, older ones shifted down and the oldest dropped.
 * An empty file is not kept: the game creates a file (empty) and writes
 * its contents when it closes it, so one save is two writes. */
int tfx_mac_write_kept(const tfx_mac_files *f, const char *dos, const uint8_t *buf, uint32_t n) {
    const char *b = base_name(dos);
    char full[TFX_MAC_PATH];
    struct stat st;
    if (f->store[0] && *b && strcmp(b, ".") && strcmp(b, "..") && tfx_mac_resolve(f->store, b, full, sizeof full, 0) &&
        !stat(full, &st) && S_ISREG(st.st_mode) && st.st_size > 0) {
        char dir[TFX_MAC_PATH + 16], low[16], from[TFX_MAC_PATH + 48], to[TFX_MAC_PATH + 48];
        snprintf(dir, sizeof dir, "%s/.history", f->store);
        mkdir(dir, 0755);
        const char *name = base_name(full);
        size_t i = 0;
        for (; name[i] && i < sizeof low - 1; i++)
            low[i] = (char)tolower((unsigned char)name[i]);
        low[i] = 0;
        for (int k = TFX_MAC_KEPT; k > 1; k--) {
            snprintf(from, sizeof from, "%s/%s~%d", dir, low, k - 1);
            snprintf(to, sizeof to, "%s/%s~%d", dir, low, k);
            rename(from, to); /* (a missing version: nothing) */
        }
        snprintf(to, sizeof to, "%s/%s~1", dir, low);
        unlink(to);
        link(full, to); /* (the replaced file stays reachable there; failing, no version) */
    }
    return tfx_mac_write(f, dos, buf, n);
}

static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* the matches in one order whatever the file system's: the store's sorted,
 * then the originals' not in the store, sorted */
int tfx_mac_find(const tfx_mac_files *f, const char *pattern, uint16_t attr, uint32_t k, char name[13]) {
    const char *b = base_name(pattern);
    char dir[TFX_MAC_PATH];
    snprintf(dir, sizeof dir, "%.*s", (int)(b - pattern), pattern);
    const char *dirs[2] = {f->store, f->root};
    static char names[512][13];
    uint32_t n = 0;
    for (int r = 0; r < 2; r++) {
        char full[TFX_MAC_PATH];
        uint32_t first = n;
        if (!dirs[r][0] || !tfx_mac_resolve(dirs[r], dir, full, sizeof full, 0))
            continue;
        DIR *d = opendir(full);
        struct dirent *e;
        while (d && (e = readdir(d)) && n < 512) {
            size_t len = strlen(e->d_name);
            if (e->d_name[0] == '.' || len > 12 || !tfx_mac_match(b, e->d_name) || strstr(e->d_name, ".part"))
                continue;
            char path[TFX_MAC_PATH + 16];
            struct stat st;
            snprintf(path, sizeof path, "%s/%s", full, e->d_name);
            if (stat(path, &st) || (S_ISDIR(st.st_mode) && !(attr & 0x10)))
                continue;
            char up[13];
            for (size_t i = 0; i <= len; i++)
                up[i] = (char)toupper((unsigned char)e->d_name[i]);
            int dup = 0;
            for (uint32_t i = 0; i < first && !dup; i++)
                dup = !strcmp(names[i], up);
            if (!dup)
                memcpy(names[n++], up, 13);
        }
        if (d)
            closedir(d);
        qsort(names[first], n - first, sizeof names[0], by_name);
    }
    if (k >= n)
        return 0;
    memcpy(name, names[k], 13);
    return 1;
}

int tfx_mac_store_ready(const tfx_mac_files *f, char *why, size_t cap) {
    char probe[TFX_MAC_PATH + 16];
    snprintf(probe, sizeof probe, "%s/.probe", f->store);
    mkdir(f->store, 0755);
    FILE *h = fopen(probe, "wb");
    int ok = h && fputc(0, h) == 0 && fflush(h) == 0;
    int err = errno;
    if (h)
        fclose(h);
    unlink(probe);
    if (!ok)
        snprintf(why, cap, "The saved data folder cannot be written (%s): %s. TFX keeps its settings, pilots and "
                           "scores there.", strerror(err), f->store);
    return ok;
}
