/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richtext.c - Unicode 17 segmentation conformance and invalid UTF-8.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richtext:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Encode a conformance-fixture scalar independently of kit decoding. */
static size_t
encode(unsigned cp, char *out)
{
    if (cp < 0x80) { out[0] = cp; return 1; }
    if (cp < 0x800) {
        out[0] = 0xc0 | (cp >> 6); out[1] = 0x80 | (cp & 63);
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = 0xe0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63);
        out[2] = 0x80 | (cp & 63);
        return 3;
    }
    out[0] = 0xf0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63);
    out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63);
    return 4;
}

/** @brief Run each official vector, including boundaries at continuation bytes. */
static size_t
conformance(const char *path, unsigned bit)
{
    FILE *file = fopen(path, "r");
    char line[16384], text[4096], *token, *comment;
    uint8_t expected[4096], actual[4096];
    size_t length, number = 0, count = 0, i;
    unsigned cp;

    CHECK(file);
    while (fgets(line, sizeof line, file)) {
        number++;
        comment = strchr(line, '#');
        if (comment) *comment = '\0';
        token = strtok(line, " \t\r\n");
        if (!token) continue;
        memset(expected, 0, sizeof expected);
        length = 0;
        do {
            if (!strcmp(token, "\xc3\xb7"))
                expected[length] = bit;
            else if (strcmp(token, "\xc3\x97")) {
                CHECK(sscanf(token, "%x", &cp) == 1 && cp <= 0x10ffff);
                CHECK(length + 4 < sizeof text);
                length += encode(cp, text + length);
            }
        } while ((token = strtok(NULL, " \t\r\n")) != NULL);
        CHECK(tiku_richtext_boundaries(text, length, actual, sizeof actual) ==
              TIKU_RICH_OK);
        for (i = 0; i <= length; i++) {
            if ((actual[i] & bit) != expected[i]) {
                fprintf(stderr, "%s:%zu byte %zu expected %u got %u\n",
                    path, number, i, expected[i], actual[i] & bit);
                CHECK(0);
            }
        }
        count++;
    }
    CHECK(!ferror(file) && fclose(file) == 0 && count > 500);
    return count;
}

/** @brief Malformed UTF-8 and small outputs are rejected without partial data. */
static void
invalid(void)
{
    const struct { const char *text; size_t length; } cases[] = {
        {"\x80", 1}, {"\xc0\x80", 2}, {"\xc2", 1}, {"\xe0\x80\x80", 3},
        {"\xed\xa0\x80", 3}, {"\xf4\x90\x80\x80", 4}, {"a\xff", 2},
        {"\xf0\x9f\x8e", 3}, {"\xe2\x80!", 3}
    };
    uint8_t flags[32], sentinel[32];
    size_t i;

    memset(sentinel, 0xa5, sizeof sentinel);
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        memcpy(flags, sentinel, sizeof flags);
        CHECK(tiku_richtext_boundaries(cases[i].text, cases[i].length,
            flags, sizeof flags) == TIKU_RICH_INVALID_TEXT);
        CHECK(!memcmp(flags, sentinel, sizeof flags));
    }
    CHECK(tiku_richtext_boundaries(NULL, 0, flags, sizeof flags) ==
          TIKU_RICH_OK && flags[0] == 3);
    CHECK(tiku_richtext_boundaries(NULL, 1, flags, sizeof flags) ==
          TIKU_RICH_INVALID);
    CHECK(tiku_richtext_boundaries("a", 1, flags, 1) == TIKU_RICH_LIMIT);
    CHECK(tiku_richtext_boundaries("", SIZE_MAX, flags, SIZE_MAX) ==
          TIKU_RICH_LIMIT);
    CHECK(tiku_richtext_boundaries("", 0, NULL, 1) == TIKU_RICH_INVALID);
}

/** @brief Official pinned conformance fixtures are required, not optional. */
int
main(void)
{
    size_t graphemes = conformance(
        "tests/fixtures/document/unicode17/GraphemeBreakTest.txt",
        TIKU_RICH_GRAPHEME);
    size_t words = conformance(
        "tests/fixtures/document/unicode17/WordBreakTest.txt", TIKU_RICH_WORD);

    invalid();
    printf("PASS: Unicode 17, %zu grapheme and %zu word conformance cases\n",
           graphemes, words);
    return 0;
}
