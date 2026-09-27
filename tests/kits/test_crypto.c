/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_crypto.c - the digests and ciphers against the vectors their
 * standards publish: RFC 1321's for MD5, FIPS 180-4's examples for
 * SHA-2, FIPS 197 appendix C for AES, and RC4's well-known three.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_crypto.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief Whether @p n bytes at @p p read as the hex @p want. */
static int
is_hex(const unsigned char *p, size_t n, const char *want)
{
    char got[300];
    size_t i;

    for (i = 0; i < n && 2 * i + 2 < sizeof got; i++) {
        snprintf(got + 2 * i, 3, "%02x", p[i]);
    }
    got[2 * n] = '\0';
    return strcmp(got, want) == 0;
}

/** @brief @p hex into bytes at @p out.  @return how many. */
static size_t
from_hex(const char *hex, unsigned char *out)
{
    size_t n = 0;
    unsigned v;

    while (hex[0] && hex[1] && sscanf(hex, "%2x", &v) == 1) {
        out[n++] = (unsigned char)v;
        hex += 2;
    }
    return n;
}

/** @brief MD5 of @p s, whole or fed a byte at a time. */
static int
md5_is(const char *s, const char *want, int bytewise)
{
    tiku_md5_t c;
    unsigned char out[16];
    size_t i, n = strlen(s);

    tiku_md5_init(&c);
    if (bytewise) {
        for (i = 0; i < n; i++) tiku_md5_update(&c, s + i, 1);
    } else {
        tiku_md5_update(&c, s, n);
    }
    tiku_md5_final(&c, out);
    return is_hex(out, 16, want);
}

static void
digest_tests(void)
{
    static const char two[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const char four[] =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    unsigned char out[64];

    check(md5_is("", "d41d8cd98f00b204e9800998ecf8427e", 0) &&
          md5_is("abc", "900150983cd24fb0d6963f7d28e17f72", 0) &&
          md5_is("message digest", "f96b697d7cb7938d525a2f31aaf161d0", 1),
          "CR-001", "MD5: RFC 1321's vectors, whole and a byte at a time");
    check(md5_is("12345678901234567890123456789012345678901234567890123456"
                 "789012345678901234567890",
                 "57edf4a22be3c955ac49da2e2107b67a", 0),
          "CR-002", "MD5: eighty digits, over a block's edge");
    tiku_sha256("abc", 3, out);
    check(is_hex(out, 32, "ba7816bf8f01cfea414140de5dae2223"
                          "b00361a396177a9cb410ff61f20015ad"),
          "CR-003", "SHA-256 of abc");
    tiku_sha256(two, strlen(two), out);
    check(is_hex(out, 32, "248d6a61d20638b8e5c026930c3e6039"
                          "a33ce45964ff2167f6ecedd419db06c1"),
          "CR-004", "SHA-256 of two blocks");
    tiku_sha256("", 0, out);
    check(is_hex(out, 32, "e3b0c44298fc1c149afbf4c8996fb924"
                          "27ae41e4649b934ca495991b7852b855"),
          "CR-005", "SHA-256 of nothing");
    tiku_sha384("abc", 3, out);
    check(is_hex(out, 48, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded163"
                          "1a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"),
          "CR-006", "SHA-384 of abc");
    tiku_sha512("abc", 3, out);
    check(is_hex(out, 64, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea2"
                          "0a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
                          "454d4423643ce80e2a9ac94fa54ca49f"),
          "CR-007", "SHA-512 of abc");
    tiku_sha512(four, strlen(four), out);
    check(is_hex(out, 64, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa1"
                          "7299aeadb6889018501d289e4900f7e4331b99dec4b5433a"
                          "c7d329eeb6dd26545e96e55b874be909"),
          "CR-008", "SHA-512 of two blocks");
}

static void
cipher_tests(void)
{
    static const struct { const char *key, *text, *want; } rc4[] = {
        {"Key", "Plaintext", "bbf316e8d940af0ad3"},
        {"Wiki", "pedia", "1021bf0420"},
        {"Secret", "Attack at dawn", "45a01f645fc35b383552544b9bf5"},
    };
    unsigned char key[32], block[16], data[64], iv[16], iv2[16];
    tiku_rc4_t r;
    tiku_aes_t a;
    int i, ok = 1;

    for (i = 0; i < 3; i++) {
        size_t n = strlen(rc4[i].text);

        memcpy(data, rc4[i].text, n);
        tiku_rc4_init(&r, (const unsigned char *)rc4[i].key,
                      strlen(rc4[i].key));
        tiku_rc4_crypt(&r, data, n);
        ok &= is_hex(data, n, rc4[i].want);
    }
    check(ok, "CR-009", "RC4: Key, Wiki and Secret");
    (void)from_hex("000102030405060708090a0b0c0d0e0f"
                   "101112131415161718191a1b1c1d1e1f", key);
    (void)from_hex("00112233445566778899aabbccddeeff", block);
    check(tiku_aes_init(&a, key, 128) == 0, "CR-010", "AES-128 keys");
    tiku_aes_encrypt(&a, block, data);
    check(is_hex(data, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"), "CR-011",
          "AES-128 enciphers FIPS 197 C.1");
    tiku_aes_decrypt(&a, data, data);
    check(memcmp(data, block, 16) == 0, "CR-012", "and deciphers it back");
    check(tiku_aes_init(&a, key, 256) == 0, "CR-013", "AES-256 keys");
    tiku_aes_encrypt(&a, block, data);
    check(is_hex(data, 16, "8ea2b7ca516745bfeafc49904b496089"), "CR-014",
          "AES-256 enciphers FIPS 197 C.3");
    tiku_aes_decrypt(&a, data, data);
    check(memcmp(data, block, 16) == 0, "CR-015", "and deciphers it back");
    check(tiku_aes_init(&a, key, 192) == -1, "CR-016",
          "a 192-bit key is refused, as no PDF uses one");
    for (i = 0; i < 64; i++) data[i] = (unsigned char)(i * 7);
    memset(iv, 0x5a, 16);
    memcpy(iv2, iv, 16);
    (void)tiku_aes_init(&a, key, 256);
    tiku_aes_cbc_encrypt(&a, iv, data, 64);
    tiku_aes_cbc_decrypt(&a, iv2, data, 32);
    tiku_aes_cbc_decrypt(&a, iv2, data + 32, 32);
    for (i = 0, ok = 1; i < 64; i++) ok &= data[i] == (unsigned char)(i * 7);
    check(ok, "CR-017", "CBC round trips, the chain carried across calls");
}

int
main(void)
{
    printf("Digests and ciphers against their standards' vectors\n");
    digest_tests();
    cipher_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: MD5, SHA-2, RC4 and AES");
    return fails ? 1 : 0;
}
