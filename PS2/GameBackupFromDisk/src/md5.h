#ifndef MD5_H
#define MD5_H

#include <tamtypes.h>
#include <stddef.h>

typedef struct {
    u32 state[4];
    u32 count[2];
    u8  buffer[64];
} md5_ctx_t;

void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const void *data, size_t len);
void md5_final(md5_ctx_t *ctx, u8 digest[16]);
void md5_hash(const void *data, size_t len, u8 digest[16]);
void md5_hex(const u8 digest[16], char hex[33]);

#endif
