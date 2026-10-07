/* Key-reader limits, wrong passwords and partially decoded key storage. */
#include "../../kits/support/tiku_keyfile.c"
#include <assert.h>

/* A PFX around MacData @p mac: version 3, an authSafe of one empty
 * SafeContents, then the MacData.  @return pfx_read()'s result. */
static int
pfx_with_mac(const unsigned char *mac, size_t maclen, const char *password)
{
    static const unsigned char head[] = {
        2, 1, 3,
        0x30, 0x11, 6, 9, 0x2a, 0x86, 0x48, 0x86, 0xf7, 13, 1, 7, 1,
        0xa0, 4, 4, 2, 0x30, 0 };
    unsigned char pfx[128];
    gather_t g = {.password = password};
    tiku_der_t e;

    assert(sizeof head + maclen + 2 <= sizeof pfx);
    pfx[0] = 0x30;
    pfx[1] = (unsigned char)(sizeof head + maclen);
    memcpy(pfx + 2, head, sizeof head);
    memcpy(pfx + 2 + sizeof head, mac, maclen);
    assert(tiku_der_read(pfx, 2 + sizeof head + maclen, &e) == 0);
    return pfx_read(&e, &g);
}

int main(void)
{
    int fails = 0;
    unsigned char truncated[] = {0x30, 6, 2, 1, 0, 2, 1, 42};
    gather_t g = {.key = truncated, .key_len = sizeof truncated, .rsa1 = 1};
    tiku_key_t *key = calloc(1, sizeof *key); assert(key);
    assert(key_numbers(&g, key) == TIKU_KEY_BAD);
    int bad = key->num == NULL || key->num_len != 6;
    puts(bad ? "FAIL D-161" : "PASS D-161"); fails += bad;
    tiku_key_free(key);
    /* SHA-1 MacData: a zero digest, an empty salt, and 10000001 rounds
     * (the INTEGER's leading 0x00 keeps 0x989681 positive). */
    unsigned char mac[] = {
        0x30, 0x2b, 0x30, 0x21, 0x30, 9, 6, 5, 0x2b, 14, 3, 2, 26, 5, 0,
        4, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        4, 0, 2, 4, 0, 0x98, 0x96, 0x81};
    tiku_der_t e; assert(tiku_der_read(mac, sizeof mac, &e) == 0);
    g = (gather_t){.password = "wrong"};
    bad = mac_holds(&e, &g, (unsigned char *)"", 0) != MAC_UNSUPPORTED ||
          pfx_with_mac(mac, sizeof mac, "wrong") != TIKU_KEY_UNSUPPORTED;
    puts(bad ? "FAIL D-153 iterations past the limit are unsupported"
             : "PASS D-153 iterations past the limit are unsupported");
    fails += bad;
    /* Without the leading 0x00 the count is negative: a malformed file. */
    mac[40] = 3; mac[41] = 0x98; mac[42] = 0x96; mac[43] = 0x81; mac[1]--;
    bad = pfx_with_mac(mac, 44, "wrong") != TIKU_KEY_BAD;
    puts(bad ? "FAIL D-153 a count below 1 is a bad file"
             : "PASS D-153 a count below 1 is a bad file");
    fails += bad;
    /* A count of 1 runs, and does not hold for this password. */
    mac[40] = 1; mac[41] = 1; mac[1] -= 2;
    bad = pfx_with_mac(mac, 42, "wrong") != TIKU_KEY_PASSWORD;
    puts(bad ? "FAIL D-153 a MAC that does not hold is a wrong password"
             : "PASS D-153 a MAC that does not hold is a wrong password");
    fails += bad;
    /* A digest algorithm the reader does not name (1.3.14.3.2.27, DSA
     * with SHA-1) leaves the MAC unread and the safe opened unchecked. */
    mac[12] = 27;
    bad = pfx_with_mac(mac, 42, "wrong") != TIKU_KEY_OK;
    puts(bad ? "FAIL D-153 an unknown digest opens unchecked"
             : "PASS D-153 an unknown digest opens unchecked");
    fails += bad;
    mac[12] = 26;
    /* A digest shorter than SHA-1's is a malformed MacData. */
    mac[16] = 19; mac[3]--; mac[1]--;
    memmove(mac + 36, mac + 37, 5);
    bad = pfx_with_mac(mac, 41, "wrong") != TIKU_KEY_BAD;
    puts(bad ? "FAIL D-153 a digest of the wrong length is a bad file"
             : "PASS D-153 a digest of the wrong length is a bad file");
    fails += bad;
    unsigned char encrypted[] = {
        0x30, 0x21, 0x30, 0x15, 6, 10, 0x2a, 0x86, 0x48, 0x86, 0xf7,
        13, 1, 12, 1, 1, 0x30, 7, 4, 2, 1, 2, 2, 1, 1,
        4, 8, 0,0,0,0,0,0,0,0};
    assert(tiku_der_read(encrypted, sizeof encrypted, &e) == 0);
    shrouded(&g, &e);
    bad = !g.wrong || g.key != NULL;
    puts(bad ? "FAIL D-154" : "PASS D-154"); fails += bad;
    free(g.key);
    return !!fails;
}
