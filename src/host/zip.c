#include "host/zip.h"

#include <stdlib.h>
#include <string.h>

#include "host/deflate.h"

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

typedef struct buf {
    uint8_t *p;
    size_t n, cap;
} buf;

static uint8_t *grow(buf *b, size_t k) {
    if (b->n + k > b->cap) {
        size_t cap = b->cap ? b->cap : 1 << 16;
        while (cap < b->n + k)
            cap *= 2;
        uint8_t *p = realloc(b->p, cap);
        if (!p)
            return NULL;
        b->p = p, b->cap = cap;
    }
    uint8_t *at = b->p + b->n;
    b->n += k;
    return at;
}

struct tfx_zip_out {
    buf data, dir;
    uint32_t count;
    tfx_deflater *d;
};

tfx_zip_out *tfx_zip_out_new(void) {
    tfx_zip_out *z = calloc(1, sizeof *z);
    if (z && !(z->d = tfx_deflater_new()))
        free(z), z = NULL;
    return z;
}

void tfx_zip_out_free(tfx_zip_out *z) {
    if (!z)
        return;
    tfx_deflater_free(z->d);
    free(z->data.p);
    free(z->dir.p);
    free(z);
}

int tfx_zip_out_add(tfx_zip_out *z, const char *name, const uint8_t *data, uint32_t n, uint16_t dos_time,
                    uint16_t dos_date) {
    size_t len = strlen(name);
    if (len >= 260 || z->data.n > 0xffffffffu - 30 - len - (size_t)n)
        return 0;
    uint8_t *packed = malloc((size_t)n + 1);
    if (!packed)
        return 0;
    size_t csize = n ? tfx_deflate(z->d, data, n, packed, n) : 0;
    int method = csize && csize < n ? 8 : 0;
    if (!method)
        csize = n;
    uint32_t crc = tfx_crc32(0, data, n), offset = (uint32_t)z->data.n;
    uint8_t *h = grow(&z->data, 30 + len + csize), *c = h ? grow(&z->dir, 46 + len) : NULL;
    if (!c) {
        free(packed);
        return 0;
    }
    put32(h, 0x04034b50), put16(h + 4, 20), put16(h + 6, 0), put16(h + 8, (uint32_t)method);
    put16(h + 10, dos_time), put16(h + 12, dos_date), put32(h + 14, crc);
    put32(h + 18, (uint32_t)csize), put32(h + 22, n), put16(h + 26, (uint32_t)len), put16(h + 28, 0);
    memcpy(h + 30, name, len);
    memcpy(h + 30 + len, method ? packed : data, csize);
    put32(c, 0x02014b50), put16(c + 4, 20), put16(c + 6, 20), put16(c + 8, 0), put16(c + 10, (uint32_t)method);
    put16(c + 12, dos_time), put16(c + 14, dos_date), put32(c + 16, crc);
    put32(c + 20, (uint32_t)csize), put32(c + 24, n), put16(c + 28, (uint32_t)len);
    put16(c + 30, 0), put16(c + 32, 0), put16(c + 34, 0), put16(c + 36, 0), put32(c + 38, 0x20 /* archive */);
    put32(c + 42, offset);
    memcpy(c + 46, name, len);
    z->count++;
    free(packed);
    return 1;
}

const uint8_t *tfx_zip_out_finish(tfx_zip_out *z, size_t *n) {
    size_t at = z->data.n;
    uint8_t *d = grow(&z->data, z->dir.n + 22);
    if (!d || z->count > 0xffff)
        return NULL;
    memcpy(d, z->dir.p, z->dir.n);
    uint8_t *e = d + z->dir.n;
    put32(e, 0x06054b50), put16(e + 4, 0), put16(e + 6, 0), put16(e + 8, z->count), put16(e + 10, z->count);
    put32(e + 12, (uint32_t)z->dir.n), put32(e + 16, (uint32_t)at), put16(e + 20, 0);
    *n = z->data.n;
    return z->data.p;
}

int tfx_zip_open(tfx_zip *z, const uint8_t *data, size_t n) {
    memset(z, 0, sizeof *z);
    if (n < 22)
        return 0;
    size_t e = n - 22, stop = n - 22 > 0xffff ? n - 22 - 0xffff : 0;
    while (get32(data + e) != 0x06054b50) /* the end record, a comment after it */
        if (e-- == stop)
            return 0;
    uint32_t count = get16(data + e + 10), size = get32(data + e + 12), at = get32(data + e + 16);
    if ((size_t)at + size > e || !(z->entry = calloc(count ? count : 1, sizeof *z->entry)))
        return 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *c = data + at;
        if ((size_t)at + 46 > e || get32(c) != 0x02014b50)
            break;
        uint32_t len = get16(c + 28), skip = 46 + len + get16(c + 30) + get16(c + 32);
        tfx_zip_entry *t = &z->entry[i];
        if ((size_t)at + skip > e || len >= sizeof t->name || (get16(c + 8) & 1)) /* (encrypted: not read) */
            break;
        memcpy(t->name, c + 46, len);
        t->name[len] = 0;
        t->method = (uint16_t)get16(c + 10), t->crc = get32(c + 16);
        t->csize = get32(c + 20), t->size = get32(c + 24), t->offset = get32(c + 42);
        z->count++;
        at += skip;
    }
    if (z->count != (int)count) {
        tfx_zip_close(z);
        return 0;
    }
    z->data = data, z->n = n;
    return 1;
}

void tfx_zip_close(tfx_zip *z) {
    free(z->entry);
    memset(z, 0, sizeof *z);
}

static int same(char a, char b) {
    if (a == '\\')
        a = '/';
    if (b == '\\')
        b = '/';
    if (a >= 'a' && a <= 'z')
        a = (char)(a - 32);
    if (b >= 'a' && b <= 'z')
        b = (char)(b - 32);
    return a == b;
}

int tfx_zip_find(const tfx_zip *z, const char *name) {
    for (int i = 0; i < z->count; i++) {
        const char *a = z->entry[i].name, *b = name;
        while (*a && *b && same(*a, *b))
            a++, b++;
        if (!*a && !*b)
            return i;
    }
    return -1;
}

int tfx_zip_extract(const tfx_zip *z, int i, uint8_t *out) {
    if (i < 0 || i >= z->count)
        return 0;
    const tfx_zip_entry *t = &z->entry[i];
    if ((size_t)t->offset + 30 > z->n || get32(z->data + t->offset) != 0x04034b50)
        return 0;
    size_t at = (size_t)t->offset + 30 + get16(z->data + t->offset + 26) + get16(z->data + t->offset + 28);
    if (at + t->csize > z->n)
        return 0;
    size_t got = 0;
    if (t->method == 0) {
        if (t->csize != t->size)
            return 0;
        memcpy(out, z->data + at, t->size);
    } else if (t->method != 8 || !tfx_inflate(z->data + at, t->csize, out, t->size, &got) || got != t->size)
        return 0;
    return tfx_crc32(0, out, t->size) == t->crc;
}
