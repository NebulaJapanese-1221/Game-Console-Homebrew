#include "duco-s1.h"
#include <string.h>
#include <stdio.h>

static const uint32_t sha1_k[4] = {
    0x5a827999u,
    0x6ed9eba1u,
    0x8f1bbcdcu,
    0xca62c1d6u
};

static uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_transform(SHA1_CTX *ctx, const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = sha1_k[0];
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = sha1_k[1];
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = sha1_k[2];
        } else {
            f = b ^ c ^ d;
            k = sha1_k[3];
        }
        uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xc3d2e1f0u;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    size_t index = (size_t)((ctx->count[0] >> 3) & 0x3f);
    ctx->count[0] += (uint32_t)(len * 8);
    if (ctx->count[0] < (uint32_t)(len * 8)) {
        ctx->count[1]++;
    }
    ctx->count[1] += (uint32_t)(((uint64_t)len * 8) >> 32);
    size_t part = 64 - index;
    size_t i = 0;
    if (len >= part) {
        memcpy(&ctx->buffer[index], data, part);
        sha1_transform(ctx, ctx->buffer);
        i = part;
        while (i + 64 <= len) {
            sha1_transform(ctx, &data[i]);
            i += 64;
        }
        index = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

void sha1_final(SHA1_CTX *ctx, uint8_t digest[20]) {
    uint64_t bits = ((uint64_t)ctx->count[1] << 32) | ctx->count[0];
    size_t index = (size_t)((ctx->count[0] >> 3) & 0x3f);
    uint8_t marker = 0x80;
    uint8_t zeros[64] = {0};
    uint8_t bits_be[8];

    sha1_update(ctx, &marker, 1);
    size_t zero_len = index < 56 ? 55 - index : 119 - index;
    if (zero_len) {
        sha1_update(ctx, zeros, zero_len);
    }
    for (int i = 0; i < 8; i++) {
        bits_be[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha1_update(ctx, bits_be, sizeof(bits_be));
    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)(ctx->state[i >> 2] >> (24 - 8 * (i & 3)));
    }
}

void duco_s1_hash(const char *last_hash, uint32_t nonce, uint8_t digest[20]) {
    if (!last_hash || !digest) return;
    char nonce_text[11];
    int n = snprintf(nonce_text, sizeof(nonce_text), "%u", nonce);
    if (n < 0 || (size_t)n >= sizeof(nonce_text)) return;

    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)last_hash, strlen(last_hash));
    sha1_update(&ctx, (const uint8_t *)nonce_text, (size_t)n);
    sha1_final(&ctx, digest);
}

static int hash_matches(const uint8_t digest[20], const char *expected_hash) {
    static const char hex[] = "0123456789abcdef";
    if (strlen(expected_hash) != 40) return 0;
    for (size_t i = 0; i < 20; i++) {
        char high = expected_hash[i * 2];
        char low = expected_hash[i * 2 + 1];
        if (high >= 'A' && high <= 'F') high = (char)(high - 'A' + 'a');
        if (low >= 'A' && low <= 'F') low = (char)(low - 'A' + 'a');
        if (high != hex[digest[i] >> 4] || low != hex[digest[i] & 0x0f]) return 0;
    }
    return 1;
}

int duco_s1_mine(const char *last_hash, const char *expected_hash,
                 uint32_t *nonce_out, uint8_t digest[20], uint32_t max_iter) {
    if (!last_hash || !expected_hash || strlen(expected_hash) != 40 ||
        !nonce_out || !digest) return 0;
    for (uint32_t nonce = 0; nonce < max_iter; nonce++) {
        duco_s1_hash(last_hash, nonce, digest);
        if (hash_matches(digest, expected_hash)) {
            *nonce_out = nonce;
            return 1;
        }
    }
    return 0;
}
