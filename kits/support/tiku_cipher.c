/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cipher.c - RC4, and AES with 128 and 256-bit keys (FIPS 197),
 * a byte at a time with no tables but the S-box, which each key
 * computes for itself: nothing shared between the threads that use it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_crypto.h"

#include <string.h>

/* -------------------------------------------------------------- RC4  */

void
tiku_rc4_init(tiku_rc4_t *c, const unsigned char *key, size_t n)
{
    unsigned j = 0, i;

    for (i = 0; i < 256; i++) {
        c->s[i] = (unsigned char)i;
    }
    for (i = 0; n > 0 && i < 256; i++) {
        unsigned char t = c->s[i];

        j = (j + t + key[i % n]) & 255;
        c->s[i] = c->s[j];
        c->s[j] = t;
    }
    c->i = 0;
    c->j = 0;
}

void
tiku_rc4_crypt(tiku_rc4_t *c, unsigned char *data, size_t n)
{
    size_t k;

    for (k = 0; k < n; k++) {
        unsigned char t;

        c->i = (unsigned char)(c->i + 1);
        c->j = (unsigned char)(c->j + c->s[c->i]);
        t = c->s[c->i];
        c->s[c->i] = c->s[c->j];
        c->s[c->j] = t;
        data[k] ^= c->s[(unsigned char)(c->s[c->i] + c->s[c->j])];
    }
}

/* -------------------------------------------------------------- AES  */

/** @brief @p x times two in GF(2^8). */
static unsigned char
xtime(unsigned char x)
{
    return (unsigned char)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

/** @brief @p a times @p b in GF(2^8). */
static unsigned char
gmul(unsigned char a, unsigned char b)
{
    unsigned char p = 0;

    while (b) {
        if (b & 1) p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}

/** @brief The S-box and its inverse, from the field: 3 generates it, the
 *         inverse is found alongside, and the affine map is applied. */
static void
sboxes(tiku_aes_t *c)
{
    unsigned char p = 1, q = 1;
    int i;

    do {
        unsigned char x;

        p = (unsigned char)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
        q ^= (unsigned char)(q << 1);
        q ^= (unsigned char)(q << 2);
        q ^= (unsigned char)(q << 4);
        if (q & 0x80) q ^= 0x09;
        x = (unsigned char)(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^
                            (q << 3 | q >> 5) ^ (q << 4 | q >> 4));
        c->sbox[p] = (unsigned char)(x ^ 0x63);
    } while (p != 1);
    c->sbox[0] = 0x63;
    for (i = 0; i < 256; i++) {
        c->inv[c->sbox[i]] = (unsigned char)i;
    }
}

int
tiku_aes_init(tiku_aes_t *c, const unsigned char *key, int bits)
{
    int nk = bits / 32, words, i;
    unsigned char rcon = 1;

    if (bits != 128 && bits != 256) {
        return -1;
    }
    sboxes(c);
    c->rounds = nk + 6;
    words = 4 * (c->rounds + 1);
    memcpy(c->rk, key, (size_t)nk * 4);
    for (i = nk; i < words; i++) {
        unsigned char t[4];
        int k;

        memcpy(t, c->rk + 4 * (i - 1), 4);
        if (i % nk == 0) {
            unsigned char first = t[0];

            t[0] = (unsigned char)(c->sbox[t[1]] ^ rcon);
            t[1] = c->sbox[t[2]];
            t[2] = c->sbox[t[3]];
            t[3] = c->sbox[first];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (k = 0; k < 4; k++) t[k] = c->sbox[t[k]];
        }
        for (k = 0; k < 4; k++) {
            c->rk[4 * i + k] = (unsigned char)(c->rk[4 * (i - nk) + k] ^ t[k]);
        }
    }
    return 0;
}

/** @brief The state XORed with round key @p r. */
static void
add_key(unsigned char *s, const tiku_aes_t *c, int r)
{
    int i;

    for (i = 0; i < 16; i++) {
        s[i] ^= c->rk[16 * r + i];
    }
}

/** @brief Row @p r of the state turned left by @p by columns. */
static void
turn_row(unsigned char *s, int r, int by)
{
    unsigned char t[4];
    int k;

    for (k = 0; k < 4; k++) {
        t[k] = s[r + 4 * ((k + by) & 3)];
    }
    for (k = 0; k < 4; k++) {
        s[r + 4 * k] = t[k];
    }
}

void
tiku_aes_encrypt(const tiku_aes_t *c, const unsigned char in[16],
                 unsigned char out[16])
{
    unsigned char s[16];
    int round, i;

    memcpy(s, in, 16);
    add_key(s, c, 0);
    for (round = 1; round <= c->rounds; round++) {
        for (i = 0; i < 16; i++) s[i] = c->sbox[s[i]];
        for (i = 1; i < 4; i++) turn_row(s, i, i);
        if (round < c->rounds) {
            for (i = 0; i < 16; i += 4) {
                unsigned char a0 = s[i], a1 = s[i + 1], a2 = s[i + 2];
                unsigned char a3 = s[i + 3], all = a0 ^ a1 ^ a2 ^ a3;

                s[i] ^= all ^ xtime(a0 ^ a1);
                s[i + 1] ^= all ^ xtime(a1 ^ a2);
                s[i + 2] ^= all ^ xtime(a2 ^ a3);
                s[i + 3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        add_key(s, c, round);
    }
    memcpy(out, s, 16);
}

void
tiku_aes_decrypt(const tiku_aes_t *c, const unsigned char in[16],
                 unsigned char out[16])
{
    unsigned char s[16];
    int round, i;

    memcpy(s, in, 16);
    add_key(s, c, c->rounds);
    for (round = c->rounds - 1; round >= 0; round--) {
        for (i = 1; i < 4; i++) turn_row(s, i, 4 - i);
        for (i = 0; i < 16; i++) s[i] = c->inv[s[i]];
        add_key(s, c, round);
        if (round > 0) {
            for (i = 0; i < 16; i += 4) {
                unsigned char a0 = s[i], a1 = s[i + 1], a2 = s[i + 2];
                unsigned char a3 = s[i + 3];

                s[i] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^
                       gmul(a3, 9);
                s[i + 1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^
                           gmul(a3, 13);
                s[i + 2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^
                           gmul(a3, 11);
                s[i + 3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^
                           gmul(a3, 14);
            }
        }
    }
    memcpy(out, s, 16);
}

void
tiku_aes_cbc_encrypt(const tiku_aes_t *c, unsigned char iv[16],
                     unsigned char *data, size_t n)
{
    size_t at;
    int i;

    for (at = 0; at + 16 <= n; at += 16) {
        for (i = 0; i < 16; i++) data[at + i] ^= iv[i];
        tiku_aes_encrypt(c, data + at, data + at);
        memcpy(iv, data + at, 16);
    }
}

void
tiku_aes_cbc_decrypt(const tiku_aes_t *c, unsigned char iv[16],
                     unsigned char *data, size_t n)
{
    unsigned char next[16];
    size_t at;
    int i;

    for (at = 0; at + 16 <= n; at += 16) {
        memcpy(next, data + at, 16);
        tiku_aes_decrypt(c, data + at, data + at);
        for (i = 0; i < 16; i++) data[at + i] ^= iv[i];
        memcpy(iv, next, 16);
    }
}
