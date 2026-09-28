/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pubkey.c - RSA and ECDSA against what another implementation made:
 * signatures by PKCS#1 v1.5 and PSS, envelopes by v1.5 and OAEP, opened
 * by the primes and without them, and an ECDSA signature on each of the
 * six curves, its key whole and compressed; each also broken a bit, and
 * refused.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tiku_crypto.h"
#include "tiku_pubkey.h"
#include "../fixtures/pubkey.inc"

static int fails;

/** @brief One check's line, and its failure counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

#define NUM(a) ((tiku_num_t){ (a), sizeof (a) })

/** @brief The 2048-bit key: whole, or public only. */
static tiku_rsa_key_t
key2k(int whole)
{
    tiku_rsa_key_t k;

    memset(&k, 0, sizeof k);
    k.n = NUM(rsa2k_n);
    k.e = NUM(rsa2k_e);
    if (whole) {
        k.d = NUM(rsa2k_d);
        k.p = NUM(rsa2k_p);
        k.q = NUM(rsa2k_q);
        k.dp = NUM(rsa2k_dp);
        k.dq = NUM(rsa2k_dq);
        k.qi = NUM(rsa2k_qi);
    }
    return k;
}

/** @brief Signatures by the 2048-bit key: v1.5 over three digests, PSS
 *         with a fixed and the longest salt; each refused once broken. */
static void
rsa_sign_tests(void)
{
    tiku_rsa_key_t k = key2k(0);
    unsigned char d1[20], d256[32], d512[64], bent[256];

    (void)tiku_hash(TIKU_HASH_SHA1, pk_message, sizeof pk_message, d1);
    (void)tiku_hash(TIKU_HASH_SHA256, pk_message, sizeof pk_message, d256);
    (void)tiku_hash(TIKU_HASH_SHA512, pk_message, sizeof pk_message, d512);
    check(tiku_rsa_verify(&k, TIKU_HASH_SHA1, d1, rsa2k_v15_sha1, 256) &&
          tiku_rsa_verify(&k, TIKU_HASH_SHA256, d256, rsa2k_v15_sha256, 256) &&
          tiku_rsa_verify(&k, TIKU_HASH_SHA512, d512, rsa2k_v15_sha512, 256),
          "PK-001", "RSA v1.5 signatures over SHA-1, SHA-256 and SHA-512");
    memcpy(bent, rsa2k_v15_sha256, 256);
    bent[100] ^= 1;
    check(!tiku_rsa_verify(&k, TIKU_HASH_SHA256, d256, bent, 256) &&
          !tiku_rsa_verify(&k, TIKU_HASH_SHA512, d512, rsa2k_v15_sha256,
                           256), "PK-002",
          "a bit changed, or the digest named another, is refused");
    d256[7] ^= 1;
    check(!tiku_rsa_verify(&k, TIKU_HASH_SHA256, d256, rsa2k_v15_sha256, 256),
          "PK-019", "and so is another message's digest");
    d256[7] ^= 1;
    check(tiku_rsa_verify_pss(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256, 32,
                              d256, rsa2k_pss_sha256, 256) &&
          tiku_rsa_verify_pss(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256, -1,
                              d256, rsa2k_pss_sha256, 256) &&
          tiku_rsa_verify_pss(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256, -1,
                              d256, rsa2k_pss_max, 256) &&
          tiku_rsa_verify_pss(&k, TIKU_HASH_SHA512, TIKU_HASH_SHA1, 20,
                              d512, rsa2k_pss_sha512, 256),
          "PK-003", "RSA-PSS: a 32-byte salt, the longest, and SHA-512 "
                    "masked by SHA-1");
    memcpy(bent, rsa2k_pss_sha256, 256);
    bent[5] ^= 0x80;
    check(!tiku_rsa_verify_pss(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256, 32,
                               d256, bent, 256) &&
          !tiku_rsa_verify_pss(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256, 20,
                               d256, rsa2k_pss_sha256, 256) &&
          !tiku_rsa_verify_pss(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256, -1,
                               d256, rsa2k_v15_sha256, 256),
          "PK-004", "PSS broken, with the wrong salt, or v1.5 read as PSS "
                    "is refused");
}

/** @brief Envelopes to the 2048-bit key (v1.5, OAEP over SHA-1 and
 *         SHA-256) opened by its primes; the 1024-bit key's by its private
 *         exponent alone; each refused once broken. */
static void
rsa_seal_tests(void)
{
    tiku_rsa_key_t k = key2k(1), one;
    unsigned char out[256], bent[256];
    long n;
    clock_t t0;

    n = tiku_rsa_decrypt(&k, 0, 0, rsa2k_env_v15, 256, out, sizeof out);
    check(n == 24 && !memcmp(out, pk_secret, 24), "PK-005",
          "a v1.5 envelope opened by the primes");
    n = tiku_rsa_decrypt(&k, TIKU_HASH_SHA1, TIKU_HASH_SHA1, rsa2k_env_oaep1,
                         256, out, sizeof out);
    check(n == 24 && !memcmp(out, pk_secret, 24), "PK-006",
          "an OAEP envelope, SHA-1");
    n = tiku_rsa_decrypt(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256,
                         rsa2k_env_oaep256, 256, out, sizeof out);
    check(n == 24 && !memcmp(out, pk_secret, 24), "PK-007",
          "an OAEP envelope, SHA-256");
    memcpy(bent, rsa2k_env_v15, 256);
    bent[200] ^= 4;
    check(tiku_rsa_decrypt(&k, 0, 0, bent, 256, out, sizeof out) != 24 ||
          memcmp(out, pk_secret, 24) != 0, "PK-008",
          "a broken envelope does not give the secret");
    check(tiku_rsa_decrypt(&k, TIKU_HASH_SHA256, TIKU_HASH_SHA256,
                           rsa2k_env_oaep1, 256, out, sizeof out) < 0 &&
          tiku_rsa_decrypt(&k, 0, 0, rsa2k_env_oaep256, 256, out,
                           sizeof out) < 0 &&
          tiku_rsa_decrypt(&k, 0, 0, rsa2k_env_v15, 256, out, 8) < 0,
          "PK-009", "OAEP read with the wrong digest, or as v1.5, or into "
                    "too little room, is refused");
    memset(&one, 0, sizeof one);
    one.n = NUM(rsa1k_n);
    one.e = NUM(rsa1k_e);
    one.d = NUM(rsa1k_d);
    n = tiku_rsa_decrypt(&one, 0, 0, rsa1k_env_v15, 128, out, sizeof out);
    check(n == 24 && !memcmp(out, pk_secret, 24), "PK-010",
          "a key without its primes opens by its private exponent");
    (void)tiku_hash(TIKU_HASH_SHA256, pk_message, sizeof pk_message, out);
    check(tiku_rsa_verify(&one, TIKU_HASH_SHA256, out, rsa1k_v15_sha256, 128),
          "PK-011", "and a 1024-bit signature checks");
    t0 = clock();
    for (int i = 0; i < 10; i++) {
        (void)tiku_rsa_decrypt(&k, 0, 0, rsa2k_env_v15, 256, out, sizeof out);
    }
    printf("    RSA-2048 by the primes: %.1f ms an envelope\n",
           (double)(clock() - t0) * 100.0 / CLOCKS_PER_SEC);
}

/** @brief One curve's signature, by its whole and compressed key, and
 *         over digests shorter and longer than the order (@p r1, @p r5);
 *         then broken three ways. */
static int
ec_case(int curve, const unsigned char *q, size_t qlen,
        const unsigned char *qc, size_t qclen, int alg, tiku_num_t r,
        tiku_num_t s, const tiku_num_t *r1, const tiku_num_t *r5)
{
    unsigned char d[64], d1[20], d5[64], sb[80];
    size_t dl = tiku_hash(alg, pk_message, sizeof pk_message, d);
    tiku_num_t bent = { sb, s.n };
    int ok;

    (void)tiku_hash(TIKU_HASH_SHA1, pk_message, sizeof pk_message, d1);
    (void)tiku_hash(TIKU_HASH_SHA512, pk_message, sizeof pk_message, d5);
    ok = tiku_ecdsa_verify(curve, q, qlen, d, dl, &r, &s) &&
         tiku_ecdsa_verify(curve, qc, qclen, d, dl, &r, &s) &&
         tiku_ecdsa_verify(curve, q, qlen, d1, 20, &r1[0], &r1[1]) &&
         tiku_ecdsa_verify(curve, q, qlen, d5, 64, &r5[0], &r5[1]);
    memcpy(sb, s.b, s.n);
    sb[s.n / 2] ^= 1;
    d[3] ^= 1;
    ok &= !tiku_ecdsa_verify(curve, q, qlen, d, dl, &r, &s);
    d[3] ^= 1;
    ok &= !tiku_ecdsa_verify(curve, q, qlen, d, dl, &r, &bent) &&
          !tiku_ecdsa_verify(curve, q, qlen, d, dl, &s, &r);
    return ok;
}

#define EC_CASE(c, tag, alg)                                             \
    ec_case(c, ec_##tag##_q, sizeof ec_##tag##_q, ec_##tag##_qc,        \
            sizeof ec_##tag##_qc, alg, NUM(ec_##tag##_r),                \
            NUM(ec_##tag##_s),                                           \
            (tiku_num_t[]){ NUM(ec_##tag##_r1), NUM(ec_##tag##_s1) },    \
            (tiku_num_t[]){ NUM(ec_##tag##_r5), NUM(ec_##tag##_s5) })

/** @brief ECDSA on the NIST and Brainpool curves; and what is not a
 *         point, not a curve, or not in range is refused. */
static void
ec_tests(void)
{
    unsigned char d[32], q[65], zero = 0;
    tiku_num_t r = NUM(ec_p256_r), s = NUM(ec_p256_s), none = { &zero, 1 };
    clock_t t0;

    check(EC_CASE(TIKU_EC_P256, p256, TIKU_HASH_SHA256), "PK-012",
          "P-256: its signature, compressed key, SHA-1 and SHA-512; "
          "broken, refused");
    check(EC_CASE(TIKU_EC_P384, p384, TIKU_HASH_SHA384), "PK-013",
          "P-384 likewise");
    t0 = clock();
    check(EC_CASE(TIKU_EC_P521, p521, TIKU_HASH_SHA512), "PK-014",
          "P-521 likewise");
    printf("    P-521: %.1f ms for its six checks\n",
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    check(EC_CASE(TIKU_EC_BP256, bp256, TIKU_HASH_SHA256), "PK-015",
          "brainpoolP256r1 likewise");
    check(EC_CASE(TIKU_EC_BP384, bp384, TIKU_HASH_SHA384), "PK-016",
          "brainpoolP384r1 likewise");
    check(EC_CASE(TIKU_EC_BP512, bp512, TIKU_HASH_SHA512), "PK-017",
          "brainpoolP512r1 likewise");
    (void)tiku_hash(TIKU_HASH_SHA256, pk_message, sizeof pk_message, d);
    memcpy(q, ec_p256_q, 65);
    q[64] ^= 1;
    check(!tiku_ecdsa_verify(TIKU_EC_P256, q, 65, d, 32, &r, &none) &&
          !tiku_ecdsa_verify(TIKU_EC_P256, q, 65, d, 32, &r,
                             &s) &&
          !tiku_ecdsa_verify(TIKU_EC_P384, ec_p256_q, 65, d, 32, &r,
                             &s) &&
          !tiku_ecdsa_verify(99, ec_p256_q, 65, d, 32, &r,
                             &s),
          "PK-018", "s of 0, a point off the curve, another curve's key "
                    "and no curve are refused");
}

int
main(void)
{
    printf("RSA and ECDSA against another implementation's signatures\n");
    rsa_sign_tests();
    rsa_seal_tests();
    ec_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: RSA v1.5, PSS and OAEP; ECDSA "
           "on six curves");
    return fails ? 1 : 0;
}
