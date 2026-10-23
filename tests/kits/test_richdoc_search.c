/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_search.c - incremental literal matching and position mapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc search:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Locate a position in the independent separator-inclusive string. */
static tiku_rich_pos_t
at(const tiku_richsnapshot_t *s, size_t offset)
{
    size_t i;
    tiku_rich_paragraph_t p;

    for (i = 0; i < tiku_richsnapshot_paragraphs(s); i++) {
        CHECK(tiku_richsnapshot_paragraph(s, i, &p) == TIKU_RICH_OK);
        if (offset <= p.length)
            return (tiku_rich_pos_t){tiku_richsnapshot_body(s), p.id,
                                    offset, TIKU_RICH_AFTER};
        offset -= p.length + 1;
    }
    CHECK(0);
    return (tiku_rich_pos_t){0};
}

/** @brief Compare result IDs and byte positions with an independent offset. */
static void
expect(const tiku_richsnapshot_t *s, tiku_rich_pos_t p, size_t offset,
         tiku_rich_affinity_t affinity)
{
    tiku_rich_pos_t wanted = at(s, offset);

    CHECK(p.body == wanted.body && p.paragraph == wanted.paragraph);
    CHECK(p.byte == wanted.byte && p.affinity == affinity);
    CHECK(tiku_richsnapshot_position(s, p) == TIKU_RICH_OK);
}

/** @brief Create a source with alternating structural and soft line breaks. */
static tiku_richdoc_t *
load(const char *text)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t p[257] = {0};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 1};
    size_t i, count = 0;
    unsigned breaks = 0;

    p[0].text = text;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n' && ++breaks % 2) {
            count++;
            CHECK(count < 257);
            p[count].text = text + i + 1;
            source.paragraph_count++;
        } else
            p[count].length++;
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Compare each budgeted result with bounded, nonoverlapping strstr. */
static void
oracle(const char *text, const char *needle, size_t first, size_t last,
         size_t work)
{
    tiku_richdoc_t *doc = load(text);
    tiku_richsnapshot_t *s;
    tiku_richsearch_t *search;
    tiku_rich_match_t match;
    tiku_rich_status_t status;
    const char *found;
    size_t length = strlen(needle), cursor = first, calls = 0;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsearch_create(s, at(s, first), at(s, last), needle,
                                  length, &search) == TIKU_RICH_OK);
    do {
        status = tiku_richsearch_next(search, work, &match);
        CHECK(++calls < 4 * strlen(text) + 20);
        if (status == TIKU_RICH_PENDING) {
            CHECK(!match.generation && !match.first.body && !match.last.body);
            continue;
        }
        found = strstr(text + cursor, needle);
        if (found && ((size_t)(found - text) > last ||
                      length > last - (size_t)(found - text)))
            found = NULL;
        if (status == TIKU_RICH_OK) {
            CHECK(found);
            CHECK(match.generation == tiku_richsnapshot_generation(s));
            expect(s, match.first, (size_t)(found - text), TIKU_RICH_AFTER);
            expect(s, match.last, (size_t)(found - text) + length,
                   TIKU_RICH_BEFORE);
            cursor = (size_t)(found - text) + length;
        } else {
            CHECK(status == TIKU_RICH_NOT_FOUND && !found);
            CHECK(!match.generation && !match.first.body && !match.last.body);
        }
    } while (status != TIKU_RICH_NOT_FOUND);
    CHECK(tiku_richsearch_next(search, work, &match) == TIKU_RICH_NOT_FOUND);
    tiku_richsearch_release(search);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Deterministic local generator, independent of library state. */
static uint32_t
random_value(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/** @brief Generated Unicode/paragraph cases against a flat-string oracle. */
static void
generated(void)
{
    uint32_t state = 193927;
    char text[257], needle[257];
    size_t case_no, length, n, i, first, last, start, end;

    for (case_no = 0; case_no < 2000; case_no++) {
        length = 1 + random_value(&state) % 100;
        n = 0;
        for (i = 0; i < length; i++) {
            unsigned value = (random_value(&state) >> 16) % 6;

            if (value == 5) {
                text[n++] = (char)0xc3;
                text[n++] = (char)0xa9;
            } else
                text[n++] = "abAB\n"[value];
        }
        text[n] = 0;
        start = random_value(&state) % n;
        while (((unsigned char)text[start] & 0xc0) == 0x80)
            start--;
        end = start + 1 + random_value(&state) % (n - start);
        while (((unsigned char)text[end] & 0xc0) == 0x80)
            end++;
        memcpy(needle, text + start, end - start);
        needle[end - start] = 0;
        first = random_value(&state) % (n + 1);
        while (((unsigned char)text[first] & 0xc0) == 0x80)
            first--;
        last = first + random_value(&state) % (n - first + 1);
        while (((unsigned char)text[last] & 0xc0) == 0x80)
            last++;
        oracle(text, needle, 0, n, 1 + case_no % 7);
        oracle(text, needle, first, last, 1 + case_no % 11);
    }
}

/** @brief Invalid requests do not advance a search or retain an output. */
static void
invalid(void)
{
    tiku_richdoc_t *doc = load("a\xc3\xa9");
    tiku_richsnapshot_t *s;
    tiku_richsearch_t *search = (void *)(uintptr_t)1;
    tiku_rich_match_t match;
    tiku_rich_pos_t first, last, bad;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    first = at(s, 0);
    last = at(s, 3);
    CHECK(tiku_richsearch_create(s, first, last, "", 0, &search) ==
          TIKU_RICH_INVALID && !search);
    CHECK(tiku_richsearch_create(s, last, first, "a", 1, &search) ==
          TIKU_RICH_INVALID_RANGE && !search);
    bad = first;
    bad.byte = 2;
    CHECK(tiku_richsearch_create(s, bad, last, "a", 1, &search) ==
          TIKU_RICH_INVALID_RANGE && !search);
    CHECK(tiku_richsearch_create(s, first, last, "\r", 1, &search) ==
          TIKU_RICH_INVALID_TEXT && !search);
    CHECK(tiku_richsearch_create(s, first, last, "\xc0\x80", 2, &search) ==
          TIKU_RICH_INVALID_TEXT && !search);
    CHECK(tiku_richsearch_create(s, first, last, "a", SIZE_MAX, &search) ==
          TIKU_RICH_LIMIT && !search);
    CHECK(tiku_richsearch_create(s, first, last, "a", 1, &search) ==
          TIKU_RICH_OK);
    memset(&match, 0xff, sizeof match);
    CHECK(tiku_richsearch_next(search, 0, &match) == TIKU_RICH_INVALID);
    CHECK(!match.generation && !match.first.body);
    CHECK(tiku_richsearch_next(search, 1, NULL) == TIKU_RICH_INVALID);
    CHECK(tiku_richsearch_next(search, 1, &match) == TIKU_RICH_OK);
    expect(s, match.first, 0, TIKU_RICH_AFTER);
    expect(s, match.last, 1, TIKU_RICH_BEFORE);
    tiku_richsearch_release(search);
    tiku_richsearch_release(NULL);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Searches own the captured state and generation across edits/close. */
static void
lifetime(void)
{
    tiku_richdoc_t *doc = load("aba\naba");
    tiku_richsnapshot_t *s;
    tiku_richsearch_t *search;
    tiku_rich_match_t match;
    tiku_rich_pos_t first, last;
    char needle[] = "aba";

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    first = at(s, 0);
    last = at(s, 7);
    CHECK(tiku_richsearch_create(s, first, last, needle, 3, &search) ==
          TIKU_RICH_OK);
    strcpy(needle, "XYZ");
    CHECK(tiku_richdoc_replace_range(doc, 0, first, first, "!", 1) ==
          TIKU_RICH_OK);
    CHECK(tiku_richsearch_next(search, 100, &match) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_replace_range(doc, match.generation, match.first,
          match.last, "q", 1) == TIKU_RICH_STALE);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
    CHECK(tiku_richsearch_next(search, 100, &match) == TIKU_RICH_OK);
    CHECK(match.first.byte == 0 && match.last.byte == 3);
    CHECK(tiku_richsearch_next(search, 100, &match) == TIKU_RICH_NOT_FOUND);
    tiku_richsearch_release(search);
}

/** @brief Compare word filtering, combining marks and overlapping candidates. */
static void
whole_words(void)
{
    static const struct {
        const char *text, *needle;
        size_t expected;
    } cases[] = {
        {"scatter cat", "cat", 8},
        {"can't can", "can", 6},
        {"e\xcc\x81 e", "e", 4},
        {"aba ba ba", "ba ba", 4},
        {"dog\ncat catty", "cat", 4}
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        tiku_richdoc_t *doc = load(cases[i].text);
        tiku_richsnapshot_t *snapshot;
        tiku_richsearch_t *search;
        tiku_rich_match_t match;
        tiku_rich_status_t status;
        unsigned work = 0;

        CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        CHECK(tiku_richsearch_create_options(snapshot, at(snapshot, 0),
            at(snapshot, strlen(cases[i].text)), cases[i].needle,
            strlen(cases[i].needle), TIKU_RICH_SEARCH_WHOLE_WORD, &search) ==
            TIKU_RICH_OK);
        do {
            status = tiku_richsearch_next(search, 1, &match);
            CHECK(++work < 1000);
        } while (status == TIKU_RICH_PENDING);
        CHECK(status == TIKU_RICH_OK);
        expect(snapshot, match.first, cases[i].expected, TIKU_RICH_AFTER);
        do { status = tiku_richsearch_next(search, 1, &match); }
        while (status == TIKU_RICH_PENDING);
        CHECK(status == TIKU_RICH_NOT_FOUND);
        tiku_richsearch_release(search);
        tiku_richsnapshot_release(snapshot);
        tiku_richdoc_release(doc);
    }
}

/** @brief Run fixed cases, lifetime checks and generated search comparisons. */
int
main(void)
{
    char repetitive[4097], needle[129];

    oracle("", "x", 0, 0, 1);
    oracle("aaa", "aa", 0, 3, 1);
    oracle("\n\n\na\n", "\n\n", 0, 5, 1);
    oracle("abc\ndef", "c\nd", 0, 7, 1);
    oracle("abc\ndef", "c\nd", 0, 4, 1);
    oracle("abc\ndef", "\n", 3, 4, 1);
    oracle("aA\nAaa", "A", 0, 6, 2);
    oracle("\xc3\xa9\n\xc3\xa9", "\xc3\xa9", 0, 5, 1);
    memset(repetitive, 'a', sizeof repetitive - 1);
    repetitive[4096] = 0;
    memset(needle, 'a', sizeof needle - 2);
    needle[127] = 'b';
    needle[128] = 0;
    oracle(repetitive, needle, 0, 4096, 1);
    invalid();
    lifetime();
    generated();
    whole_words();
    puts("PASS: rich document search, 4000 generated scope comparisons");
    return 0;
}
