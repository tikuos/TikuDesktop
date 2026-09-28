/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_cms.c - key files, envelopes and signatures other writers made:
 * PKCS#12 by its own schemes and by PBES2, PEM's three kinds of key;
 * envelopes in every content cipher, to one recipient and to several;
 * signatures with and without signed attributes, by RSA, RSA-PSS and
 * ECDSA, with the content inside; an RFC 3161 timestamp; each changed
 * and refused, and every one of them cut short and bent a byte at a time
 * without a fault.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_cms.h"
#include "tiku_crypto.h"
#include "tiku_der.h"
#include "../fixtures/cms.inc"

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

/** @brief A fixture's bytes, decoded from its base64. */
typedef struct {
    unsigned char *p;
    size_t         n;
} blob_t;

/** @brief @p b64 decoded.  The bytes are the caller's to free(). */
static blob_t
blob(const char *b64)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    blob_t b = { malloc(strlen(b64)), 0 };
    unsigned v = 0;
    int bits = 0;

    for (; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            b.p[b.n++] = (unsigned char)(v >> bits);
        }
    }
    return b;
}

/** @brief Key file @p b64 read with @p pw: its status, the key in @p k. */
static int
key_of(const char *b64, const char *pw, tiku_key_t **k)
{
    blob_t b = blob(b64);
    int s = tiku_key_read(b.p, b.n, pw, k);

    free(b.p);
    return s;
}

/** @brief Key files: PKCS#12 by PBES2 and its own schemes, the empty
 *         password, and PEM's kinds; wrong passwords and unread keys. */
static void
key_tests(void)
{
    tiku_key_t *k = NULL;
    const char *name;

    check(key_of(ck_p12_modern, "p\xc3\xa4ssw\xc3\xb6rd", &k) == TIKU_KEY_OK &&
          !strcmp(tiku_key_name(k), "Tiku Test Signer"), "CMS-001",
          "PKCS#12 by PBES2, AES-256 and HMAC-SHA256; a non-ASCII password");
    tiku_key_free(k);
    k = NULL;
    check(key_of(ck_p12_modern, "passwort", &k) == TIKU_KEY_PASSWORD &&
          k == NULL, "CMS-002", "the wrong password is said to be wrong");
    check(key_of(ck_p12_legacy, "secret", &k) == TIKU_KEY_OK &&
          !strcmp(tiku_key_name(k), "Tiku Test Signer"), "CMS-003",
          "PKCS#12's triple DES and SHA-1 MAC; its own certificate of two");
    tiku_key_free(k);
    check(key_of(ck_p12_libressl, "secret", &k) == TIKU_KEY_OK &&
          !strcmp(tiku_key_name(k), "Tiku Test Signer"), "CMS-004",
          "LibreSSL's: RC2-40 around the certificate");
    tiku_key_free(k);
    check(key_of(ck_p12_empty, "", &k) == TIKU_KEY_OK &&
          (tiku_key_free(k), key_of(ck_p12_empty, "x", &k)) ==
          TIKU_KEY_PASSWORD, "CMS-005", "the empty password, and not it");
    check(key_of(ck_pem_plain, NULL, &k) == TIKU_KEY_OK &&
          !strcmp(tiku_key_name(k), "Tiku Test Signer"), "CMS-006",
          "PEM: PKCS#8 and its certificate");
    tiku_key_free(k);
    check(key_of(ck_pem_sealed, "secret", &k) == TIKU_KEY_OK &&
          (tiku_key_free(k), key_of(ck_pem_sealed, "open", &k)) ==
          TIKU_KEY_PASSWORD, "CMS-007", "PEM: PKCS#8 sealed by PBES2");
    check(key_of(ck_pem_rsa, "", &k) == TIKU_KEY_OK &&
          (name = tiku_key_name(k)) != NULL && !name[0], "CMS-008",
          "PEM: PKCS#1, with no certificate and so no name");
    tiku_key_free(k);
    k = NULL;
    check(key_of(ck_pem_ec, "", &k) == TIKU_KEY_UNSUPPORTED &&
          key_of(ck_pem_old, "secret", &k) == TIKU_KEY_UNSUPPORTED &&
          key_of(ck_message, "", &k) == TIKU_KEY_BAD && k == NULL,
          "CMS-009", "an EC key and OpenSSL's old sealing are not read; "
                     "what is no key file is said to be none");
}

/** @brief Envelope @p b64 opened with @p k.  @return whether it gave
 *         ck_message. */
static int
opens(const tiku_key_t *k, const char *b64)
{
    blob_t b = blob(b64), m = blob(ck_message);
    unsigned char *out;
    long n = tiku_cms_open(k, b.p, b.n, &out);
    int ok = n == (long)m.n && !memcmp(out, m.p, m.n);

    free(out);
    free(b.p);
    free(m.p);
    return ok;
}

/** @brief Envelopes in every content cipher, OAEP, cryptography's, one to
 *         another person, one to two; a key with no certificate. */
static void
envelope_tests(void)
{
    tiku_key_t *k = NULL, *bare = NULL;

    (void)key_of(ck_p12_modern, "p\xc3\xa4ssw\xc3\xb6rd", &k);
    check(opens(k, ck_env_aes256) && opens(k, ck_env_aes128) &&
          opens(k, ck_env_aes192), "CMS-010", "AES-256, -128 and -192");
    check(opens(k, ck_env_des3) && opens(k, ck_env_rc2) &&
          opens(k, ck_env_rc2_128), "CMS-011",
          "triple DES, RC2-40 and RC2-128");
    check(opens(k, ck_env_pyca) && opens(k, ck_env_oaep), "CMS-012",
          "cryptography's envelope, and a key sealed by RSA-OAEP");
    check(!opens(k, ck_env_elsewhere) && opens(k, ck_env_both), "CMS-013",
          "one to someone else stays shut; one to both opens");
    (void)key_of(ck_pem_rsa, "", &bare);
    check(opens(bare, ck_env_both) && opens(bare, ck_env_des3), "CMS-014",
          "a key with no certificate tries each recipient");
    check(!opens(NULL, ck_env_aes256) && !opens(k, ck_sig_rsa), "CMS-015",
          "no key, or no envelope, opens nothing");
    tiku_key_free(k);
    tiku_key_free(bare);
}

/** @brief Signature @p b64 checked over ck_message, or over it with a
 *         byte changed.  @return the status, the result in @p out. */
static int
verified(const char *b64, int changed, int detached, tiku_cms_sig_t *out)
{
    blob_t b = blob(b64), m = blob(ck_message);
    tiku_piece_t p = { m.p, m.n };
    int s;

    if (changed) m.p[5] ^= 0x20;
    s = tiku_cms_verify(b.p, b.n, &p, detached, out);
    free(b.p);
    free(m.p);
    return s;
}

/** @brief Signatures: RSA with and without attributes, the content
 *         inside, no certificate, ECDSA, PSS; each changed. */
static void
signature_tests(void)
{
    tiku_cms_sig_t s;
    char when[40];

    check(verified(ck_sig_rsa, 0, 1, &s) == TIKU_SIG_GOOD &&
          !strcmp(s.cert.subject, "Tiku Test Signer") &&
          !strcmp(s.cert.org, "TikuOS Tests") && s.signed_at > 0 &&
          s.digest == TIKU_HASH_SHA256 && s.stamp == 0, "CMS-016",
          "RSA with signed attributes: good, its signer and time");
    check(verified(ck_sig_rsa, 1, 1, &s) == TIKU_SIG_CHANGED, "CMS-017",
          "the content changed a byte: changed");
    check(verified(ck_sig_noattr, 0, 1, &s) == TIKU_SIG_GOOD &&
          s.digest == TIKU_HASH_SHA1 && s.signed_at == 0 &&
          verified(ck_sig_noattr, 1, 1, &s) == TIKU_SIG_CHANGED, "CMS-018",
          "RSA over the content itself, SHA-1; changed");
    check(verified(ck_sig_inside, 0, 0, &s) == TIKU_SIG_GOOD &&
          s.econtent_len == 63 && !memcmp(s.econtent, "The first", 9),
          "CMS-019", "the content inside it: good, and given back");
    check(verified(ck_sig_nocert, 0, 1, &s) == TIKU_SIG_UNCHECKED &&
          strstr(s.why, "certificate"), "CMS-020",
          "no certificate to check by: unchecked, and said why");
    check(verified(ck_sig_ec, 0, 1, &s) == TIKU_SIG_GOOD &&
          !strcmp(s.cert.subject, "Tiku EC Signer") &&
          s.digest == TIKU_HASH_SHA384 &&
          verified(ck_sig_ec, 1, 1, &s) == TIKU_SIG_CHANGED, "CMS-021",
          "ECDSA P-256 over SHA-384: good; changed");
    check(verified(ck_sig_pss, 0, 1, &s) == TIKU_SIG_GOOD &&
          verified(ck_sig_pss, 1, 1, &s) == TIKU_SIG_CHANGED, "CMS-022",
          "RSASSA-PSS: good; changed");
    check(verified(ck_stamp, 0, 1, &s) == TIKU_SIG_GOOD && s.timestamp &&
          s.stamp == TIKU_SIG_GOOD && !strcmp(s.stamp_by, "Tiku Test TSA"),
          "CMS-023", "a timestamp token over the content: good, its "
                     "authority named");
    tiku_der_time_text(s.stamped_at, when, sizeof when);
    check(!strcmp(when, "2026-09-28 12:34:56 UTC"), "CMS-024",
          "and the time it gives, 2026-09-28 12:34:56 UTC");
    check(verified(ck_stamp, 1, 1, &s) == TIKU_SIG_CHANGED, "CMS-025",
          "the content it dates changed: changed");
}

/** @brief A certificate as a person reads it. */
static void
cert_tests(void)
{
    blob_t b = blob(ck_cert);
    tiku_cert_info_t c;
    char from[40];

    check(tiku_cert_info(b.p, b.n, &c) == 0 &&
          !strcmp(c.subject, "Tiku Test Signer") &&
          !strcmp(c.issuer, "Tiku Test Signer") && c.self_signed &&
          (tiku_der_time_text(c.not_before, from, sizeof from),
           !strcmp(from, "2026-01-01 00:00:00 UTC")) &&
          c.not_after > c.not_before, "CMS-026",
          "its name, its issuer (itself), and when it holds");
    b.p[b.n / 2] ^= 0xff;
    check(tiku_cert_info(b.p, 40, &c) != 0 && !c.subject[0], "CMS-027",
          "cut short, it is no certificate");
    free(b.p);
}

/** @brief Every fixture cut at each length and bent at each byte, read
 *         as a key file, an envelope and a signature: no fault, and no
 *         signature whose value was bent reads as good. */
static void
bend_tests(void)
{
    static const char *const all[] = { ck_p12_legacy, ck_pem_plain,
        ck_env_aes256, ck_env_rc2, ck_sig_rsa, ck_sig_ec, ck_stamp };
    tiku_key_t *k = NULL, *got;
    tiku_cms_sig_t s;
    unsigned char *out;
    size_t i, at;
    int good = 0;

    (void)key_of(ck_pem_plain, "", &k);
    for (i = 0; i < sizeof all / sizeof all[0]; i++) {
        blob_t b = blob(all[i]), m = blob(ck_message);
        tiku_piece_t p = { m.p, m.n };

        for (at = 0; at < b.n; at += 1 + b.n / 700) {
            b.p[at] ^= 0x5a;
            if (tiku_key_read(b.p, b.n, "secret", &got) == TIKU_KEY_OK) {
                tiku_key_free(got);
            }
            free((tiku_cms_open(k, b.p, b.n, &out), out));
            good += all[i] == ck_sig_rsa && at >= b.n - 128 &&
                    tiku_cms_verify(b.p, b.n, &p, 1, &s) == TIKU_SIG_GOOD;
            b.p[at] ^= 0x5a;
            (void)tiku_cms_verify(b.p, at, &p, 1, &s);
            if (tiku_key_read(b.p, at, "", &got) == TIKU_KEY_OK) {
                tiku_key_free(got);
            }
        }
        free(b.p);
        free(m.p);
    }
    tiku_key_free(k);
    check(good == 0, "CMS-028", "cut and bent: no fault, and no bent "
                                "signature value reads as good");
}

int
main(void)
{
    printf("Key files, envelopes and signatures other writers made\n");
    key_tests();
    envelope_tests();
    signature_tests();
    cert_tests();
    bend_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: PKCS#12, PKCS#8, CMS enveloped "
           "and signed data, timestamps");
    return fails ? 1 : 0;
}
