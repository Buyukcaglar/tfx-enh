#include "host/dospath.h"

#include <string.h>

static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }

static int is_sep(char c) { return c == '\\' || c == '/'; }

int tfx_dos_relative(const char *dos_path, char *out, size_t cap) {
    if (!dos_path || !cap)
        return 0;
    const char *p = dos_path;
    const char *root = TFX_DOS_ROOT;
    size_t rn = strlen(root), i = 0;
    while (i < rn && p[i] && up(p[i]) == root[i])
        i++;
    if (i == rn && (!p[i] || is_sep(p[i])))
        p += rn; /* the folder by DID's name */
    else if ((p[0] && p[1] == ':') || is_sep(p[0]))
        return 0; /* another drive or the drive's root */
    size_t n = 0;
    while (*p) {
        while (is_sep(*p))
            p++;
        if (!*p)
            break;
        const char *end = p;
        while (*end && !is_sep(*end))
            end++;
        size_t len = (size_t)(end - p);
        if (len == 1 && p[0] == '.') {
            p = end;
            continue;
        }
        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (n + (n ? 1 : 0) + len + 1 > cap)
            return 0;
        if (n)
            out[n++] = '\\';
        for (size_t k = 0; k < len; k++) {
            if (p[k] == ':')
                return 0;
            out[n++] = up(p[k]);
        }
        p = end;
    }
    out[n] = 0;
    return 1;
}

static int dos_char(char c) {
    if ((unsigned char)c < 0x21 || (unsigned char)c > 0x7e)
        return 0;
    return !strchr("\\/:*?\"<>|+=;,[].", c);
}

int tfx_dos_83(const char *name) {
    size_t base = 0, ext = 0;
    const char *p = name;
    while (*p && *p != '.') {
        if (!dos_char(*p))
            return 0;
        base++, p++;
    }
    if (*p == '.') {
        p++;
        while (*p) {
            if (!dos_char(*p))
                return 0;
            ext++, p++;
        }
        if (!ext)
            return 0;
    }
    return base >= 1 && base <= 8 && ext <= 3;
}

/* the FCB form: 8 + 3 characters, blank padded, '*' as '?' to the end */
static void fcb(const char *s, char out[11], int pattern) {
    memset(out, ' ', 11);
    int i = 0;
    for (; *s && *s != '.'; s++) {
        if (pattern && *s == '*') {
            while (i < 8)
                out[i++] = '?';
            while (*s && *s != '.')
                s++;
            break;
        }
        if (i < 8)
            out[i++] = up(*s);
    }
    if (*s == '.')
        s++;
    for (i = 8; *s; s++) {
        if (pattern && *s == '*') {
            while (i < 11)
                out[i++] = '?';
            break;
        }
        if (i < 11)
            out[i++] = up(*s);
    }
}

int tfx_dos_match(const char *pattern, const char *name) {
    char p[11], n[11];
    fcb(pattern, p, 1);
    fcb(name, n, 0);
    for (int i = 0; i < 11; i++)
        if (p[i] != '?' && p[i] != n[i])
            return 0;
    return 1;
}

int tfx_dos_split(const char *rel, char *dir, size_t dir_cap, char *last, size_t last_cap) {
    const char *slash = strrchr(rel, '\\');
    size_t dn = slash ? (size_t)(slash - rel) : 0;
    const char *l = slash ? slash + 1 : rel;
    size_t ln = strlen(l);
    if (dn + 1 > dir_cap || ln + 1 > last_cap)
        return 0;
    memcpy(dir, rel, dn);
    dir[dn] = 0;
    memcpy(last, l, ln + 1);
    return 1;
}
