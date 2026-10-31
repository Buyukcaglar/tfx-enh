/* SHA-1 (FIPS 180-4), which the CHD format keeps for its data and its
 * metadata (host/chd.h); not used to identify the user's files (those
 * are SHA-256: host/profile.h). */
#ifndef TFX_HOST_SHA1_H
#define TFX_HOST_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct tfx_sha1 {
    uint32_t h[5];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} tfx_sha1;

void tfx_sha1_init(tfx_sha1 *s);
void tfx_sha1_add(tfx_sha1 *s, const void *data, size_t n);
void tfx_sha1_done(tfx_sha1 *s, uint8_t digest[20]);

#endif
