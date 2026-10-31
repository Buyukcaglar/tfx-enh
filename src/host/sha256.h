/* SHA-256 (FIPS 180-4), for the content profile's identification of the
 * user's original files (W02). */
#ifndef TFX_HOST_SHA256_H
#define TFX_HOST_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct tfx_sha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} tfx_sha256;

void tfx_sha256_init(tfx_sha256 *s);
void tfx_sha256_add(tfx_sha256 *s, const void *data, size_t n);
void tfx_sha256_done(tfx_sha256 *s, uint8_t digest[32]);
/* digest as 64 lower-case hex digits and a NUL */
void tfx_sha256_hex(const uint8_t digest[32], char out[65]);

#endif
