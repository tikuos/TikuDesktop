/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto.h - the digests and ciphers an encrypted document needs:
 * MD5, SHA-256, SHA-384 and SHA-512, RC4, and AES with 128 or 256-bit
 * keys, written here from their standards.
 *
 * For READING what others encrypted -- a PDF's standard security
 * handler -- not for keeping secrets: nothing here is constant-time,
 * and MD5 and RC4 are broken for any other use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CRYPTO_H_
#define TIKU_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

/** @brief MD5 (RFC 1321), fed in pieces. */
typedef struct {
    uint32_t      s[4];
    uint64_t      n;                /* bytes fed                       */
    unsigned char buf[64];
} tiku_md5_t;

/** @brief Start a digest, feed it @p n bytes, and finish it into @p out. */
void tiku_md5_init(tiku_md5_t *c);
void tiku_md5_update(tiku_md5_t *c, const void *data, size_t n);
void tiku_md5_final(tiku_md5_t *c, unsigned char out[16]);

/** @brief SHA-2 (FIPS 180-4) of @p n bytes at @p data, all at once. */
void tiku_sha256(const void *data, size_t n, unsigned char out[32]);
void tiku_sha384(const void *data, size_t n, unsigned char out[48]);
void tiku_sha512(const void *data, size_t n, unsigned char out[64]);

/** @brief RC4: the key schedule, then bytes XORed in place. */
typedef struct {
    unsigned char s[256];
    unsigned char i, j;
} tiku_rc4_t;

/** @brief The key schedule for @p n bytes of @p key; then @p n bytes of
 *         @p data XORed with the stream, in place. */
void tiku_rc4_init(tiku_rc4_t *c, const unsigned char *key, size_t n);
void tiku_rc4_crypt(tiku_rc4_t *c, unsigned char *data, size_t n);

/** @brief AES (FIPS 197): a key's round keys and the tables they use. */
typedef struct {
    unsigned char rk[240];          /* 15 round keys of 16 bytes       */
    int           rounds;
    unsigned char sbox[256], inv[256];
} tiku_aes_t;

/** @brief Round keys for a @p bits-bit key (128 or 256).  @return 0,
 *         or -1 for any other length. */
int tiku_aes_init(tiku_aes_t *c, const unsigned char *key, int bits);
/** @brief One 16-byte block enciphered; in and out may be the same. */
void tiku_aes_encrypt(const tiku_aes_t *c, const unsigned char in[16],
                      unsigned char out[16]);
/** @brief One 16-byte block deciphered; in and out may be the same. */
void tiku_aes_decrypt(const tiku_aes_t *c, const unsigned char in[16],
                      unsigned char out[16]);
/** @brief CBC encryption over @p n bytes in place, a whole number of
 *         blocks, the chain carried in @p iv from one call to the next. */
void tiku_aes_cbc_encrypt(const tiku_aes_t *c, unsigned char iv[16],
                          unsigned char *data, size_t n);
/** @brief CBC decryption likewise. */
void tiku_aes_cbc_decrypt(const tiku_aes_t *c, unsigned char iv[16],
                          unsigned char *data, size_t n);

#endif /* TIKU_CRYPTO_H_ */
