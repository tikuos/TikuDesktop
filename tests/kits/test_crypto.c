/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_crypto.c - the digests and ciphers against the vectors their
 * standards publish: RFC 1321's for MD5, FIPS 180-4's examples for SHA-1
 * and SHA-2, RFC 2202 and 4231 for HMAC, RFC 6070 and 7914 for PBKDF2,
 * FIPS 197 appendix C for AES, FIPS 46's and SP 800-67's for DES and
 * triple DES, RFC 2268's for RC2, and RC4's well-known three.
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

/** @brief SHA-1 and SHA-224 by their examples; every digest fed in
 *         pieces of every awkward size is the digest taken whole; HMAC and
 *         PBKDF2 by their RFCs' vectors. */
static void
stream_tests(void)
{
    static const char two[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const size_t piece[] = { 1, 3, 55, 56, 63, 64, 65, 111, 112, 127,
                                    128, 129, 300 };
    unsigned char data[1000], whole[64], out[64], key[131];
    tiku_hash_t h;
    int alg, ok = 1;
    size_t i, at, n;

    (void)tiku_hash(TIKU_HASH_SHA1, "abc", 3, out);
    check(is_hex(out, 20, "a9993e364706816aba3e25717850c26c9cd0d89d"),
          "CR-018", "SHA-1 of abc");
    (void)tiku_hash(TIKU_HASH_SHA1, two, strlen(two), out);
    check(is_hex(out, 20, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"),
          "CR-019", "SHA-1 of two blocks");
    (void)tiku_hash_init(&h, TIKU_HASH_SHA1);
    memset(data, 'a', 1000);
    for (i = 0; i < 1000; i++) tiku_hash_update(&h, data, 1000);
    tiku_hash_final(&h, out);
    check(is_hex(out, 20, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"),
          "CR-020", "SHA-1 of a million a's, a thousand at a time");
    (void)tiku_hash(TIKU_HASH_SHA224, "abc", 3, out);
    check(is_hex(out, 28, "23097d223405d8228642a477bda255b3"
                          "2aadbce4bda0b3f7e36c9da7"),
          "CR-021", "SHA-224 of abc");
    for (i = 0; i < sizeof data; i++) data[i] = (unsigned char)(i * 37 + 11);
    for (alg = TIKU_HASH_MD5; alg <= TIKU_HASH_SHA512; alg++) {
        for (i = 0; i < sizeof piece / sizeof piece[0]; i++) {
            n = tiku_hash(alg, data, sizeof data, whole);
            (void)tiku_hash_init(&h, alg);
            for (at = 0; at < sizeof data; at += piece[i])
                tiku_hash_update(&h, data + at, sizeof data - at < piece[i]
                                                ? sizeof data - at : piece[i]);
            tiku_hash_final(&h, out);
            ok &= n > 0 && memcmp(out, whole, n) == 0;
        }
    }
    check(ok && tiku_hash(99, data, 1, out) == 0, "CR-022",
          "each digest fed in pieces is the digest whole; none is none");
    memset(key, 0x0b, 20);
    (void)tiku_hmac(TIKU_HASH_MD5, key, 16, "Hi There", 8, out);
    ok = is_hex(out, 16, "9294727a3638bb1c13f48ef8158bfc9d");
    (void)tiku_hmac(TIKU_HASH_SHA1, key, 20, "Hi There", 8, out);
    ok &= is_hex(out, 20, "b617318655057264e28bc0b6fb378c8ef146be00");
    (void)tiku_hmac(TIKU_HASH_SHA256, key, 20, "Hi There", 8, out);
    ok &= is_hex(out, 32, "b0344c61d8db38535ca8afceaf0bf12b"
                          "881dc200c9833da726e9376c2e32cff7");
    (void)tiku_hmac(TIKU_HASH_SHA512, key, 20, "Hi There", 8, out);
    ok &= is_hex(out, 64, "87aa7cdea5ef619d4ff0b4241a1d6cb0"
                          "2379f4e2ce4ec2787ad0b30545e17cde"
                          "daa833b7d6b8a702038b274eaea3f4e4"
                          "be9d914eeb61f1702e696c203a126854");
    check(ok, "CR-023", "HMAC over MD5, SHA-1, SHA-256, SHA-512: RFC 2202 "
                        "and 4231's first case");
    memset(key, 0xaa, 131);
    (void)tiku_hmac(TIKU_HASH_SHA256, key, 131, "Test Using Larger Than "
                    "Block-Size Key - Hash Key First", 54, out);
    check(is_hex(out, 32, "60e431591ee0b67f0d8a26aacbf5b77f"
                          "8e0bc6213728c5140546040f0ee37f54"),
          "CR-024", "HMAC with a key longer than a block: RFC 4231 case 6");
    ok = tiku_pbkdf2(TIKU_HASH_SHA1, "password", 8, "salt", 4, 1, out, 20)
         == 0 && is_hex(out, 20, "0c60c80f961f0e71f3a9b524af6012062fe037a6");
    ok &= tiku_pbkdf2(TIKU_HASH_SHA1, "password", 8, "salt", 4, 4096, out,
                      20) == 0 &&
          is_hex(out, 20, "4b007901b765489abead49d926f721d065a429c1");
    ok &= tiku_pbkdf2(TIKU_HASH_SHA1, "passwordPASSWORDpassword", 24,
                      "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096, out,
                      25) == 0 &&
          is_hex(out, 25, "3d2eec4fe41c849b80c8d83662c0e44a"
                          "8b291a964cf2f07038");
    check(ok, "CR-025", "PBKDF2 over HMAC-SHA1: RFC 6070's vectors, one "
                        "running past a block");
    check(tiku_pbkdf2(TIKU_HASH_SHA256, "passwd", 6, "salt", 4, 1, out,
                      64) == 0 &&
          is_hex(out, 64, "55ac046e56e3089fec1691c22544b605"
                          "f94185216dde0465e68b9d57c20dacbc"
                          "49ca9cccf179b645991664b39d77ef31"
                          "7c71b845b1e30bd509112041d3a19783") &&
          tiku_pbkdf2(TIKU_HASH_SHA256, "p", 1, "s", 1, 0, out, 8) == -1,
          "CR-026", "PBKDF2 over HMAC-SHA256: RFC 7914's; no rounds is "
                    "refused");
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
    stream_tests();
}

/** @brief DES by FIPS 46's worked example, triple DES by SP 800-67's,
 *         RC2 by RFC 2268's eight: each enciphered, and deciphered back. */
static void
old_cipher_tests(void)
{
    static const struct { const char *key; int bits; const char *want; }
        rc2[] = {
        {"0000000000000000", 63, "ebb773f993278eff"},
        {"ffffffffffffffff", 64, "278b27e42e2f0d49"},
        {"3000000000000000", 64, "30649edf9be7d2c2"},
        {"88", 64, "61a8a244adacccf0"},
        {"88bca90e90875a", 64, "6ccf4308974c267f"},
        {"88bca90e90875a7f0f79c384627bafb2", 64, "1a807d272bbe5db1"},
        {"88bca90e90875a7f0f79c384627bafb2", 128, "2269552ab0f85ca6"},
        {"88bca90e90875a7f0f79c384627bafb216f80a6f85920584c42fceb0be255daf1e",
         129, "5b78d3a43dfff1f1"},
    };
    static const char *const rc2_text[] = {
        "0000000000000000", "ffffffffffffffff", "1000000000000001",
        "0000000000000000", "0000000000000000", "0000000000000000",
        "0000000000000000", "0000000000000000",
    };
    unsigned char key[40], block[24], out[24], back[24];
    tiku_des3_t d;
    tiku_rc2_t r;
    size_t i, n;
    int ok = 1;

    (void)from_hex("133457799bbcdff1", key);
    (void)from_hex("0123456789abcdef", block);
    check(tiku_des3_init(&d, key, 8) == 0, "CR-028", "a DES key");
    tiku_des3_encrypt(&d, block, out);
    tiku_des3_decrypt(&d, out, back);
    check(is_hex(out, 8, "85e813540f0ab405") && !memcmp(back, block, 8),
          "CR-029", "DES enciphers FIPS 46's example, and back");
    (void)from_hex("0123456789abcdef23456789abcdef01456789abcdef0123", key);
    check(tiku_des3_init(&d, key, 24) == 0 &&
          tiku_des3_init(&d, key, 12) == -1, "CR-030",
          "three DES keys; twelve bytes are refused");
    (void)tiku_des3_init(&d, key, 24);
    for (i = 0; i < 24; i += 8) {
        tiku_des3_encrypt(&d, (const unsigned char *)"The qufck brown fox jump"
                          + i, out + i);
        tiku_des3_decrypt(&d, out + i, back + i);
    }
    check(is_hex(out, 24, "a826fd8ce53b855fcce21c8112256fe6"
                          "68d5c05dd9b6b900") &&
          !memcmp(back, "The qufck brown fox jump", 24),
          "CR-031", "triple DES enciphers SP 800-67's example, and back");
    for (i = 0; i < sizeof rc2 / sizeof rc2[0]; i++) {
        n = from_hex(rc2[i].key, key);
        (void)from_hex(rc2_text[i], block);
        ok &= tiku_rc2_init(&r, key, n, rc2[i].bits) == 0;
        tiku_rc2_encrypt(&r, block, out);
        tiku_rc2_decrypt(&r, out, back);
        ok &= is_hex(out, 8, rc2[i].want) && !memcmp(back, block, 8);
    }
    check(ok && tiku_rc2_init(&r, key, 0, 64) == -1 &&
          tiku_rc2_init(&r, key, 8, 1025) == -1, "CR-032",
          "RC2 enciphers RFC 2268's eight vectors, and back; no key or "
          "too many bits is refused");
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
    check(tiku_aes_init(&a, key, 192) == 0, "CR-016", "AES-192 keys");
    tiku_aes_encrypt(&a, block, data);
    check(is_hex(data, 16, "dda97ca4864cdfe06eaf70a0ec0d7191") &&
          tiku_aes_init(&a, key, 160) == -1, "CR-027",
          "AES-192 enciphers FIPS 197 C.2; a 160-bit key is refused");
    for (i = 0; i < 64; i++) data[i] = (unsigned char)(i * 7);
    memset(iv, 0x5a, 16);
    memcpy(iv2, iv, 16);
    (void)tiku_aes_init(&a, key, 256);
    tiku_aes_cbc_encrypt(&a, iv, data, 64);
    tiku_aes_cbc_decrypt(&a, iv2, data, 32);
    tiku_aes_cbc_decrypt(&a, iv2, data + 32, 32);
    for (i = 0, ok = 1; i < 64; i++) ok &= data[i] == (unsigned char)(i * 7);
    check(ok, "CR-017", "CBC round trips, the chain carried across calls");
    old_cipher_tests();
}

int
main(void)
{
    printf("Digests and ciphers against their standards' vectors\n");
    digest_tests();
    cipher_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: MD5, SHA-1, SHA-2, HMAC, "
           "PBKDF2, RC4, AES, DES and RC2");
    return fails ? 1 : 0;
}
