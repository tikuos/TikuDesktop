/* Configuration client: strict wire parsing, frozen revisions, no downgrade.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int available, fault, reject, writes;
static const char *status_override;
static char sent[160];
static const char *epoch = "0102030405060708090a0b0c0d0e0f10";
static int stat_node(tiku_backend_t *b, const char *p, tiku_model_t *m)
{ (void)b; (void)p; (void)m; return available ? 0 : -1; }
static int read_node(tiku_backend_t *b, const char *p, void *out, size_t max)
{
    (void)b;
    if (fault) return -1;
    if (!strcmp(p, "/sys/config/status")) return status_override ?
        snprintf(out, max, "%s", status_override) : snprintf(out, max, "v1:ready:%s\n", epoch);
    return snprintf(out, max, "v1:%s:00000007:applied:426f617264\n", epoch);
}
static int write_node(tiku_backend_t *b, const char *p, const void *in, size_t n,
                        char *error, size_t max)
{
    (void)b;
    assert(!strcmp(p, "/sys/config/name"));
    writes++; snprintf(sent, sizeof sent, "%.*s", (int)n, (const char *)in);
    if (reject) { snprintf(error, max, "write: cannot write (ECONFLICT)"); return -1; }
    return 0;
}
int main(void)
{
    tiku_config_snapshot_t s;
    const tiku_backend_ops_t ops = {.stat=stat_node, .read=read_node, .write=write_node};
    tiku_backend_t b = {.ops=&ops};
    char wire[160], error[160];
    uint8_t token[16] = {1};
    assert(tiku_config_read(&b, "/sys/device/name", &s) == 0 && s.mode == 0);
    available = 1;
    assert(tiku_config_read(&b, "/sys/device/name", &s) == 1);
    assert(s.mode == 1 && s.revision == 7 && !strcmp(s.desired, "Board"));
    assert(tiku_config_request(&s, token, "Lab", 3, wire, sizeof wire) > 0);
    assert(strstr(wire, ":00000007:01000000000000000000000000000000:4c6162"));
    tiku_config_snapshot_t decoded;
    char token_hex[33], value[33], original[160];
    snprintf(original, sizeof original, "%s", wire);
    assert(!tiku_config_decode_request(wire, &decoded, token_hex, value));
    assert(decoded.revision == 7 && !strcmp(value, "Lab") &&
           !strcmp(token_hex, "01000000000000000000000000000000"));
    for (size_t cut = 0; cut < strlen(original); cut++) {
        snprintf(wire, sizeof wire, "%.*s", (int)cut, original);
        assert(tiku_config_decode_request(wire, &decoded, token_hex, value) == -1);
    }
    for (size_t at = 0; at < strlen(original); at++) {
        snprintf(wire, sizeof wire, "%s", original); wire[at] ^= 1;
        assert(tiku_config_decode_request(wire, &decoded, token_hex, value) == -1);
    }
    assert(tiku_config_request(&s, token, "Lab", 3, wire, 10) == -1);
    assert(tiku_config_request(&s, token, "L\nb", 3, wire, sizeof wire) == -1);
    memset(token, 0, sizeof token);
    assert(tiku_config_request(&s, token, "Lab", 3, wire, sizeof wire) == -1);
    assert(tiku_config_write(&b, "/sys/device/name", &s, "Lab", 3, error, sizeof error) == 0);
    assert(writes == 1 && strstr(sent, ":00000007:"));
    reject = 1;
    assert(tiku_config_write(&b, "/sys/device/name", &s, "Lab", 3, error, sizeof error) == -1);
    assert(writes == 2 && strstr(error, "Refresh"));
    fault = 1;
    assert(tiku_config_read(&b, "/sys/device/name", &s) == -1 && s.mode == -1);
    assert(tiku_config_write(&b, "/sys/device/name", &s, "Lab", 3, error, sizeof error) == -1);
    assert(writes == 2);
    const char *bad[] = {"", "v1:", "v2:123", "v1:0102030405060708090a0b0c0d0e0f10:00000001:unknown:41",
        "v1:0102030405060708090a0b0c0d0e0f10:00000000:applied:41",
        "v1:0102030405060708090a0b0c0d0e0f10:00000001:applied:0g",
        "v1:0102030405060708090a0b0c0d0e0f10:00000001:applied:00",
        "v1:0102030405060708090a0b0c0d0e0f10:00000001:applied:4",
        "v1:0102030405060708090a0b0c0d0e0f10:00000001:applied:41\nextra"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        assert(tiku_config_parse(bad[i], &s) == -1 && s.mode == -1);
    assert(tiku_config_parse("v1:0102030405060708090a0b0c0d0e0f10:00000000:unset:\n", &s) == 0);
    fault = 0;
    status_override = "v1:unenrolled:-\n";
    assert(!tiku_config_probe(&b, &s) && s.enrollable && !s.mode);
    status_override = "v1:unsupported:-\n";
    assert(!tiku_config_probe(&b, &s) && !s.enrollable);
    status_override = "v1:unenrolled:-\nextra";
    assert(tiku_config_probe(&b, &s) == -1 && s.mode == -1);
    status_override = "v1:ready:00000000000000000000000000000000\n";
    assert(tiku_config_probe(&b, &s) == -1 && s.mode == -1);
    puts("config: codecs, conflicts, immutable revision and no error downgrade PASS");
    return 0;
}
