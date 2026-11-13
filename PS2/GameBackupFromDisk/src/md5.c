#include "md5.h"
#include <string.h>

#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

static const u8 PADDING[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u32 rotate_left(u32 x, u32 n)
{
    return (x << n) | (x >> (32 - n));
}

static u32 F(u32 x, u32 y, u32 z) { return (x & y) | ((~x) & z); }
static u32 G(u32 x, u32 y, u32 z) { return (x & z) | (y & (~z)); }
static u32 H(u32 x, u32 y, u32 z) { return x ^ y ^ z; }
static u32 I(u32 x, u32 y, u32 z) { return y ^ (x | (~z)); }

static void md5_transform(u32 state[4], const u8 block[64])
{
    u32 a = state[0], b = state[1], c = state[2], d = state[3];
    u32 x[16];

    for (int i = 0; i < 16; i++) {
        x[i] = (u32)block[i * 4] |
               ((u32)block[i * 4 + 1] << 8) |
               ((u32)block[i * 4 + 2] << 16) |
               ((u32)block[i * 4 + 3] << 24);
    }

#define FF(a, b, c, d, x, s, ac) { \
    a += F(b, c, d) + x + (u32)(ac); a = rotate_left(a, s); a += b; }
#define GG(a, b, c, d, x, s, ac) { \
    a += G(b, c, d) + x + (u32)(ac); a = rotate_left(a, s); a += b; }
#define HH(a, b, c, d, x, s, ac) { \
    a += H(b, c, d) + x + (u32)(ac); a = rotate_left(a, s); a += b; }
#define II(a, b, c, d, x, s, ac) { \
    a += I(b, c, d) + x + (u32)(ac); a = rotate_left(a, s); a += b; }

    FF(a, b, c, d, x[ 0], S11, 0xd76aa478); FF(d, a, b, c, x[ 1], S12, 0xe8c7b756);
    FF(c, d, a, b, x[ 2], S13, 0x242070db); FF(b, c, d, a, x[ 3], S14, 0xc1bdceee);
    FF(a, b, c, d, x[ 4], S11, 0xf57c0faf); FF(d, a, b, c, x[ 5], S12, 0x4787c62a);
    FF(c, d, a, b, x[ 6], S13, 0xa8304613); FF(b, c, d, a, x[ 7], S14, 0xfd469501);
    FF(a, b, c, d, x[ 8], S11, 0x698098d8); FF(d, a, b, c, x[ 9], S12, 0x8b44f7af);
    FF(c, d, a, b, x[10], S13, 0xffff5bb1); FF(b, c, d, a, x[11], S14, 0x895cd7be);
    FF(a, b, c, d, x[12], S11, 0x6b901122); FF(d, a, b, c, x[13], S12, 0xfd987193);
    FF(c, d, a, b, x[14], S13, 0xa679438e); FF(b, c, d, a, x[15], S14, 0x49b40821);

    GG(a, b, c, d, x[ 1], S21, 0xf61e2562); GG(d, a, b, c, x[ 6], S22, 0xc040b340);
    GG(c, d, a, b, x[11], S23, 0x265e5a51); GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa);
    GG(a, b, c, d, x[ 5], S21, 0xd62f105d); GG(d, a, b, c, x[10], S22, 0x2441453);
    GG(c, d, a, b, x[15], S23, 0xd8a1e681); GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8);
    GG(a, b, c, d, x[ 9], S21, 0x21e1cde6); GG(d, a, b, c, x[14], S22, 0xc33707d6);
    GG(c, d, a, b, x[ 3], S23, 0xf4d50d87); GG(b, c, d, a, x[ 8], S24, 0x455a14ed);
    GG(a, b, c, d, x[13], S21, 0xa9e3e905); GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8);
    GG(c, d, a, b, x[ 7], S23, 0x676f02d9); GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

    HH(a, b, c, d, x[ 5], S31, 0xfffa3942); HH(d, a, b, c, x[ 8], S32, 0x8771f681);
    HH(c, d, a, b, x[11], S33, 0x6d9d6122); HH(b, c, d, a, x[14], S34, 0xfde5380c);
    HH(a, b, c, d, x[ 1], S31, 0xa4beea44); HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9);
    HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60); HH(b, c, d, a, x[10], S34, 0xbebfbc70);
    HH(a, b, c, d, x[13], S31, 0x289b7ec6); HH(d, a, b, c, x[ 0], S32, 0xeaa127fa);
    HH(c, d, a, b, x[ 3], S33, 0xd4ef3085); HH(b, c, d, a, x[ 6], S34, 0x4881d05);
    HH(a, b, c, d, x[ 9], S31, 0xd9d4d039); HH(d, a, b, c, x[12], S32, 0xe6db99e5);
    HH(c, d, a, b, x[15], S33, 0x1fa27cf8); HH(b, c, d, a, x[ 2], S34, 0xc4ac5665);

    II(a, b, c, d, x[ 0], S41, 0xf4292244); II(d, a, b, c, x[ 7], S42, 0x432aff97);
    II(c, d, a, b, x[14], S43, 0xab9423a7); II(b, c, d, a, x[ 5], S44, 0xfc93a039);
    II(a, b, c, d, x[12], S41, 0x655b59c3); II(d, a, b, c, x[ 3], S42, 0x8f0ccc92);
    II(c, d, a, b, x[10], S43, 0xffeff47d); II(b, c, d, a, x[ 1], S44, 0x85845dd1);
    II(a, b, c, d, x[ 8], S41, 0x6fa87e4f); II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
    II(c, d, a, b, x[ 6], S43, 0xa3014314); II(b, c, d, a, x[13], S44, 0x4e0811a1);
    II(a, b, c, d, x[ 4], S41, 0xf7537e82); II(d, a, b, c, x[11], S42, 0xbd3af235);
    II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb); II(b, c, d, a, x[ 9], S44, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_init(md5_ctx_t *ctx)
{
    ctx->count[0] = 0;
    ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void md5_update(md5_ctx_t *ctx, const void *data, size_t len)
{
    const u8 *ptr = (const u8 *)data;
    u32 i, index, partLen;

    index = (u32)((ctx->count[0] >> 3) & 0x3F);
    if ((ctx->count[0] += ((u32)len << 3)) < ((u32)len << 3)) {
        ctx->count[1]++;
    }
    ctx->count[1] += ((u32)len >> 29);
    partLen = 64 - index;

    if (len >= partLen) {
        memcpy(&ctx->buffer[index], ptr, partLen);
        md5_transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64) {
            md5_transform(ctx->state, &ptr[i]);
        }
        index = 0;
    } else {
        i = 0;
    }

    memcpy(&ctx->buffer[index], &ptr[i], len - i);
}

void md5_final(md5_ctx_t *ctx, u8 digest[16])
{
    u32 count[2];
    u8 bits[8];
    u32 index, padLen;

    count[0] = ctx->count[0];
    count[1] = ctx->count[1];

    bits[0] = (u8)(count[0]);
    bits[1] = (u8)(count[0] >> 8);
    bits[2] = (u8)(count[0] >> 16);
    bits[3] = (u8)(count[0] >> 24);
    bits[4] = (u8)(count[1]);
    bits[5] = (u8)(count[1] >> 8);
    bits[6] = (u8)(count[1] >> 16);
    bits[7] = (u8)(count[1] >> 24);

    index = (u32)((count[0] >> 3) & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, PADDING, padLen);
    md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i * 4]     = (u8)(ctx->state[i]);
        digest[i * 4 + 1] = (u8)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (u8)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (u8)(ctx->state[i] >> 24);
    }

    memset(ctx, 0, sizeof(*ctx));
}

void md5_hash(const void *data, size_t len, u8 digest[16])
{
    md5_ctx_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
}

void md5_hex(const u8 digest[16], char hex[33])
{
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hex[i * 2]     = hex_chars[(digest[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
    }
    hex[32] = '\0';
}
