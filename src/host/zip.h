/* The install folder's files as one zip archive (PKWARE APPNOTE: the
 * local headers, the central directory, its end record; deflate or
 * stored; no zip64, no encryption), GAME\TFX.ZIP beside the program: made
 * once from the folder the user gave, then the game's files are read from
 * it. Names are kept as the archive keeps them ('/' between folders) and
 * looked up without case, '\' and '/' alike. The archive is made and read
 * in memory (the game's files are 14 MB). */
#ifndef TFX_HOST_ZIP_H
#define TFX_HOST_ZIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct tfx_zip_out tfx_zip_out;

tfx_zip_out *tfx_zip_out_new(void);
/* a file (deflated, or stored when that is no smaller), its DOS time and
 * date as the folder had them; 0 out of memory */
int tfx_zip_out_add(tfx_zip_out *z, const char *name, const uint8_t *data, uint32_t n, uint16_t dos_time,
                    uint16_t dos_date);
/* the archive's bytes (the central directory added), valid until the
 * writer is freed; NULL out of memory */
const uint8_t *tfx_zip_out_finish(tfx_zip_out *z, size_t *n);
void tfx_zip_out_free(tfx_zip_out *z);

typedef struct tfx_zip_entry {
    char name[260];
    uint32_t size, csize, crc;
    uint32_t offset; /* its local header's */
    uint16_t method; /* 0 stored, 8 deflated */
} tfx_zip_entry;

typedef struct tfx_zip {
    const uint8_t *data; /* the archive (the caller's, kept while open) */
    size_t n;
    int count;
    tfx_zip_entry *entry;
} tfx_zip;

/* 0 when data is not an archive this reads */
int tfx_zip_open(tfx_zip *z, const uint8_t *data, size_t n);
void tfx_zip_close(tfx_zip *z);
/* the entry by name (no case, '\' as '/'), -1 none */
int tfx_zip_find(const tfx_zip *z, const char *name);
/* entry i's bytes (entry[i].size) into out; 0 on a damaged entry (its
 * CRC checked) */
int tfx_zip_extract(const tfx_zip *z, int i, uint8_t *out);

#endif
