/* Deflate (RFC 1951, raw: no zlib or gzip wrapper), its inflate, and
 * CRC-32 (ISO 3309, as zip's), for the game's own copies of the user's
 * original content: the install folder's files packed as GAME\TFX.ZIP
 * (host/zip.h) and the CD image as GAME\TFX.CHD (host/chd.h). Written for
 * this program; no library. */
#ifndef TFX_HOST_DEFLATE_H
#define TFX_HOST_DEFLATE_H

#include <stddef.h>
#include <stdint.h>

/* A compressor's working memory (one a thread: the CD's hunks are
 * compressed by several at once). */
typedef struct tfx_deflater tfx_deflater;

tfx_deflater *tfx_deflater_new(void);
void tfx_deflater_free(tfx_deflater *d);

/* in (n bytes) as one raw deflate stream into out; the stream's length,
 * 0 when it does not fit in cap (the caller then keeps the bytes as
 * they are). */
size_t tfx_deflate(tfx_deflater *d, const uint8_t *in, size_t n, uint8_t *out, size_t cap);

/* A raw deflate stream (n bytes) into out; 1 and *got the bytes made,
 * 0 on a stream that is not valid or does not fit in cap. */
int tfx_inflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *got);

/* CRC-32: start at 0, then feed the running value back in. */
uint32_t tfx_crc32(uint32_t crc, const void *data, size_t n);

#endif
