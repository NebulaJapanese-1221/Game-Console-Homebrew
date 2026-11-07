#ifndef DUCO_S1_H
#define DUCO_S1_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
void sha1_final(SHA1_CTX *ctx, uint8_t digest[20]);

void duco_s1_hash(const char *last_hash, uint32_t nonce, uint8_t digest[20]);

int duco_s1_mine(const char *last_hash, const char *expected_hash,
                 uint32_t *nonce_out, uint8_t digest[20], uint32_t max_iter);

#endif
