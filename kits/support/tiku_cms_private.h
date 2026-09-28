/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cms_private.h - what the key files and CMS share: a certificate as
 * read, a key as held, and the digests and block ciphers by the names
 * (object identifiers) the containers give them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CMS_PRIVATE_H_
#define TIKU_CMS_PRIVATE_H_

#include "tiku_cms.h"
#include "tiku_der.h"
#include "tiku_pubkey.h"

/** @brief A certificate as read: its parts, pointing into its DER. */
typedef struct {
    tiku_der_t           whole, tbs;    /* all of it; what the issuer signed */
    tiku_der_t           serial;        /* the INTEGER                   */
    tiku_der_t           issuer, subject;   /* the Names, whole          */
    tiku_der_t           key_alg;       /* the key's algorithm's OID     */
    tiku_der_t           key_param;     /* its parameters (a curve's OID) */
    const unsigned char *key;           /* the public key's bits         */
    size_t               key_len;
    const unsigned char *ski;           /* its subject key identifier    */
    size_t               ski_len;
    int64_t              not_before, not_after;
} cms_cert_t;

/** @brief A public key made ready to check with. */
typedef struct {
    int            curve;           /* TIKU_EC_*, or 0 for RSA           */
    tiku_rsa_key_t rsa;
    tiku_num_t     point;           /* an EC key's point                 */
} cms_pub_t;

/** @brief A key file's private key, and its certificate. */
struct tiku_key {
    unsigned char *num;             /* the RSA numbers, owned            */
    size_t         num_len;
    tiku_rsa_key_t rsa;             /* pointing into num                 */
    unsigned char *cert;            /* its certificate's DER, or NULL    */
    size_t         cert_len;
    char           name[160];       /* its certificate's subject, or "" */
};

/** @brief Read the DER certificate @p der.  @return 0, or -1. */
int cms_cert_read(const unsigned char *der, size_t n, cms_cert_t *c);

/** @brief The public key @p c holds.  @return 0, or -1 for one this does
 *         not read. */
int cms_cert_key(const cms_cert_t *c, cms_pub_t *k);

/** @brief The digest an AlgorithmIdentifier's OID names: TIKU_HASH_*,
 *         or 0. */
int cms_digest(const tiku_der_t *oid);

/** @brief A block cipher, as a container names it. */
enum { CMS_DES3 = 1, CMS_DES, CMS_RC2, CMS_AES, CMS_RC4 };

/**
 * @brief Decipher @p n bytes of @p data in place: cipher @p kind in CBC
 *        with @p iv (RC4: none), keyed with @p key (RC2's effective
 *        @p bits), and the padding taken off.
 * @return the plaintext's length, or -1: a wrong key most often shows
 *         as padding that is not.
 */
long cms_cbc(int kind, const unsigned char *key, size_t klen, int bits,
             const unsigned char *iv, unsigned char *data, size_t n);

/**
 * @brief The cipher an AlgorithmIdentifier @p alg names -- AES, triple
 *        DES, DES or RC2 in CBC, the IV its parameters -- deciphering
 *        @p data in place with @p key.
 * @return the plaintext's length; -1 for a wrong key; -2 for a cipher
 *         this does not read.
 */
long cms_decipher(const tiku_der_t *alg, const unsigned char *key,
                  size_t klen, unsigned char *data, size_t n);

/** @brief How long a key cipher @p alg takes, 0 when it can be any
 *         length (RC2) or is unknown. */
size_t cms_cipher_key_len(const tiku_der_t *alg);

#endif /* TIKU_CMS_PRIVATE_H_ */
