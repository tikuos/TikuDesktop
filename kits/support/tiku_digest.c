/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_digest.c - MD5 (RFC 1321) and SHA-256, SHA-384 and SHA-512
 * (FIPS 180-4), from their standards' pseudocode and constants.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_crypto.h"

#include <string.h>

/* -------------------------------------------------------------- MD5  */

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const unsigned char md5_r[16] = {
    7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21
};

/** @brief @p x turned left by @p n bits. */
static uint32_t
rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

/** @brief One 64-byte block into the state. */
static void
md5_block(tiku_md5_t *c, const unsigned char *p)
{
    uint32_t m[16], a = c->s[0], b = c->s[1], cc = c->s[2], d = c->s[3];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 |
               (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
    }
    for (i = 0; i < 64; i++) {
        uint32_t f, t;
        int g;

        if (i < 16) {
            f = (b & cc) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & cc);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ cc ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = cc ^ (b | ~d);
            g = (7 * i) & 15;
        }
        t = d;
        d = cc;
        cc = b;
        b = b + rotl32(a + f + md5_k[i] + m[g],
                       md5_r[(i >> 4) * 4 + (i & 3)]);
        a = t;
    }
    c->s[0] += a;
    c->s[1] += b;
    c->s[2] += cc;
    c->s[3] += d;
}

void
tiku_md5_init(tiku_md5_t *c)
{
    c->s[0] = 0x67452301;
    c->s[1] = 0xefcdab89;
    c->s[2] = 0x98badcfe;
    c->s[3] = 0x10325476;
    c->n = 0;
}

void
tiku_md5_update(tiku_md5_t *c, const void *data, size_t n)
{
    const unsigned char *p = data;

    while (n > 0) {
        size_t at = (size_t)(c->n & 63), take = 64 - at;

        if (take > n) take = n;
        memcpy(c->buf + at, p, take);
        c->n += take;
        p += take;
        n -= take;
        if ((c->n & 63) == 0) {
            md5_block(c, c->buf);
        }
    }
}

void
tiku_md5_final(tiku_md5_t *c, unsigned char out[16])
{
    uint64_t bits = c->n * 8;
    unsigned char pad = 0x80, len[8];
    int i;

    tiku_md5_update(c, &pad, 1);
    pad = 0;
    while ((c->n & 63) != 56) {
        tiku_md5_update(c, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        len[i] = (unsigned char)(bits >> (8 * i));
    }
    tiku_md5_update(c, len, 8);
    for (i = 0; i < 16; i++) {
        out[i] = (unsigned char)(c->s[i / 4] >> (8 * (i % 4)));
    }
}

/* ----------------------------------------------------------- SHA-256  */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/** @brief @p x turned right by @p n bits. */
static uint32_t
rotr32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

/** @brief One 64-byte block into the eight words of @p h. */
static void
sha256_block(uint32_t *h, const unsigned char *p)
{
    uint32_t w[64], v[8];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (rotr32(v[4], 6) ^ rotr32(v[4], 11) ^
                              rotr32(v[4], 25)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha256_k[i] + w[i];
        uint32_t t2 = (rotr32(v[0], 2) ^ rotr32(v[0], 13) ^
                       rotr32(v[0], 22)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) {
        h[i] += v[i];
    }
}

void
tiku_sha256(const void *data, size_t n, unsigned char out[32])
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const unsigned char *p = data;
    unsigned char last[128];
    size_t rest = n % 64, tail;
    uint64_t bits = (uint64_t)n * 8;
    int i;

    for (; n >= 64; n -= 64, p += 64) {
        sha256_block(h, p);
    }
    memset(last, 0, sizeof last);
    memcpy(last, p, rest);
    last[rest] = 0x80;
    tail = rest < 56 ? 64 : 128;
    for (i = 0; i < 8; i++) {
        last[tail - 1 - i] = (unsigned char)(bits >> (8 * i));
    }
    sha256_block(h, last);
    if (tail == 128) {
        sha256_block(h, last + 64);
    }
    for (i = 0; i < 32; i++) {
        out[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

/* ----------------------------------------------------- SHA-384 / 512  */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/** @brief @p x turned right by @p n bits. */
static uint64_t
rotr64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

/** @brief One 128-byte block into the eight words of @p h. */
static void
sha512_block(uint64_t *h, const unsigned char *p)
{
    uint64_t w[80], v[8];
    int i, k;

    for (i = 0; i < 16; i++) {
        w[i] = 0;
        for (k = 0; k < 8; k++) {
            w[i] = w[i] << 8 | p[8 * i + k];
        }
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^
                      (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^
                      (w[i - 2] >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (i = 0; i < 80; i++) {
        uint64_t t1 = v[7] + (rotr64(v[4], 14) ^ rotr64(v[4], 18) ^
                              rotr64(v[4], 41)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha512_k[i] + w[i];
        uint64_t t2 = (rotr64(v[0], 28) ^ rotr64(v[0], 34) ^
                       rotr64(v[0], 39)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) {
        h[i] += v[i];
    }
}

/** @brief SHA-512's machine from start words @p h, @p outn bytes out. */
static void
sha512_run(uint64_t *h, const void *data, size_t n, unsigned char *out,
           int outn)
{
    const unsigned char *p = data;
    unsigned char last[256];
    size_t rest = n % 128, tail;
    uint64_t bits = (uint64_t)n * 8;
    int i;

    for (; n >= 128; n -= 128, p += 128) {
        sha512_block(h, p);
    }
    memset(last, 0, sizeof last);
    memcpy(last, p, rest);
    last[rest] = 0x80;
    tail = rest < 112 ? 128 : 256;
    for (i = 0; i < 8; i++) {
        last[tail - 1 - i] = (unsigned char)(bits >> (8 * i));
    }
    sha512_block(h, last);
    if (tail == 256) {
        sha512_block(h, last + 128);
    }
    for (i = 0; i < outn; i++) {
        out[i] = (unsigned char)(h[i / 8] >> (56 - 8 * (i % 8)));
    }
}

void
tiku_sha512(const void *data, size_t n, unsigned char out[64])
{
    uint64_t h[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };

    sha512_run(h, data, n, out, 64);
}

void
tiku_sha384(const void *data, size_t n, unsigned char out[48])
{
    uint64_t h[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL,
        0x152fecd8f70e5939ULL, 0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL
    };

    sha512_run(h, data, n, out, 48);
}
