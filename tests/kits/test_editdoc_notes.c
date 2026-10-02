/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_notes.c - indicators, which follow a range of text through
 * edits and go with the last of it, and annotations, which follow their
 * line as markers do; both found, visited, cleared and dropped by a load,
 * checked against a model through random edits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_edit_decorations.h"
#include "tiku_editdoc_private.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

/** @brief A new document holding @p s. */
static tiku_editdoc_t *
doc_of(const char *s)
{
    tiku_editdoc_t *doc = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK)
        exit(1);
    return doc;
}

/** @brief Replace [s, e) by @p text as one transaction. */
static tiku_edit_status_t
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief An indicator of @p kind over [@p s, @p e). */
static tiku_edit_indicator_t
ind(tiku_editdoc_t *doc, int64_t s, int64_t e, unsigned kind)
{
    tiku_edit_range_t r = { s, e };
    tiku_edit_indicator_t d = { 0, 0 };

    if (tiku_editdoc_indicator_add(doc, r, kind, &d) != TIKU_EDIT_OK)
        exit(1);
    return d;
}

/** @brief Whether @p d covers exactly [@p s, @p e). */
static int
covers(tiku_editdoc_t *doc, tiku_edit_indicator_t d, int64_t s, int64_t e)
{
    tiku_edit_range_t r = { -1, -1 };

    return tiku_editdoc_indicator_range(doc, d, &r) == TIKU_EDIT_OK &&
           r.start == s && r.end == e;
}

/** @brief Whether @p d is gone. */
static int
gone(tiku_editdoc_t *doc, tiku_edit_indicator_t d)
{
    return tiku_editdoc_indicator_range(doc, d, NULL) == TIKU_EDIT_NOTHING;
}

/** @brief Adding indicators: what is refused, and what a handle reads. */
static void
adding(void)
{
    tiku_editdoc_t *doc = doc_of("caf\xc3\xa9 au lait");
    tiku_edit_range_t r = { 0, 4 };
    tiku_edit_indicator_t d;

    check(tiku_editdoc_indicator_add(doc, r, TIKU_EDIT_INDICATOR_KINDS, &d) ==
          TIKU_EDIT_INVALID, "IND-001", "a kind past the last is refused");
    r.end = 0;
    check(tiku_editdoc_indicator_add(doc, r, 0, &d) ==
          TIKU_EDIT_INVALID_RANGE, "IND-001", "an empty range is refused");
    r.start = 4;
    r.end = 6;
    check(tiku_editdoc_indicator_add(doc, r, 0, &d) ==
          TIKU_EDIT_INVALID_RANGE, "IND-001",
          "a range starting inside a character is refused");
    r.start = 6;
    r.end = 99;
    check(tiku_editdoc_indicator_add(doc, r, 0, &d) ==
          TIKU_EDIT_INVALID_RANGE, "IND-001",
          "a range past the end is refused");
    d = ind(doc, 0, 5, 3);
    check(covers(doc, d, 0, 5), "IND-001", "a handle reads its range");
    tiku_editdoc_indicator_remove(doc, d);
    check(gone(doc, d), "IND-001", "a removed indicator's handle is dead");
    (void)ind(doc, 6, 8, 3);
    check(gone(doc, d), "IND-001",
          "and stays dead when its slot is taken again");
    check(tiku_editdoc_indicator_add(NULL, r, 0, &d) == TIKU_EDIT_INVALID &&
          tiku_editdoc_indicator_add(doc, r, 0, NULL) ==
              TIKU_EDIT_INVALID_RANGE, "IND-001",
          "no document is refused; a handle may be left out");
    tiku_editdoc_release(doc);
}

/** @brief A range follows its text: put in inside it widens it, at
 *         either end stays out, a cut shortens it, and taking all of its
 *         text removes it. */
static void
following(void)
{
    tiku_editdoc_t *doc = doc_of("one two three");
    tiku_edit_indicator_t d = ind(doc, 4, 7, 0);

    put(doc, 0, 0, "xx");
    check(covers(doc, d, 6, 9), "IND-002", "text before moves it");
    put(doc, 7, 7, "W");
    check(covers(doc, d, 6, 10), "IND-002", "text inside widens it");
    put(doc, 6, 6, "<");
    check(covers(doc, d, 7, 11), "IND-002", "text at its start stays out");
    put(doc, 11, 11, ">");
    check(covers(doc, d, 7, 11), "IND-002", "text at its end stays out");
    put(doc, 5, 9, "");
    check(covers(doc, d, 5, 7), "IND-002",
          "a cut over its start takes its head");
    put(doc, 6, 10, "zz");
    check(covers(doc, d, 5, 6), "IND-002",
          "a cut over its end takes its tail");
    put(doc, 4, 7, "abc");
    check(gone(doc, d), "IND-002", "a cut over all of it removes it");
    d = ind(doc, 0, 2, 0);
    put(doc, 0, 2, "");
    check(gone(doc, d), "IND-002", "and so does deleting just its text");
    tiku_editdoc_release(doc);
    doc = doc_of("a\rb");
    d = ind(doc, 0, 2, 0);
    put(doc, 2, 2, "\n");
    check(covers(doc, d, 0, 1), "IND-002",
          "an end never stands between a CR and its LF: the line break "
          "they make stays out");
    tiku_editdoc_release(doc);
    doc = doc_of("ab\ncd");
    d = ind(doc, 3, 5, 0);
    (void)tiku_editdoc_undo(doc);
    put(doc, 2, 3, "");
    (void)tiku_editdoc_undo(doc);
    check(covers(doc, d, 3, 5), "IND-002",
          "an undo moves it back with its text");
    tiku_editdoc_load(doc, "ab\ncd", 5);
    check(gone(doc, d), "IND-002", "a load removes it");
    tiku_editdoc_release(doc);
}

/** @brief Count the indicators visited, stopping at the second. */
static int
count_each(void *ctx, tiku_edit_range_t range, unsigned kind)
{
    int *n = ctx;

    (void)range;
    (void)kind;
    return ++*n == 2;
}

/** @brief The next indicator after a place, or back before it, the
 *         widest first at one start and only of the kinds asked; visits
 *         of those over a span, and clearing a kind. */
static void
searching(void)
{
    tiku_editdoc_t *doc = doc_of("0123456789abcdef");
    tiku_edit_range_t r, span = { 0, 16 };
    unsigned kind = 99;
    int n = 0;

    (void)ind(doc, 2, 4, 0);
    (void)ind(doc, 6, 8, 1);
    (void)ind(doc, 6, 12, 2);
    (void)ind(doc, 13, 14, 0);
    check(tiku_editdoc_indicator_next(doc, 2, 0x7, 0, &r, &kind) ==
          TIKU_EDIT_OK && r.start == 6 && r.end == 12 && kind == 2,
          "IND-003", "the next after a place is the nearest start, widest "
          "first");
    check(tiku_editdoc_indicator_next(doc, 6, 0x7, 0, &r, NULL) ==
          TIKU_EDIT_OK && r.start == 13, "IND-003",
          "a start at the place itself is not after it");
    check(tiku_editdoc_indicator_next(doc, 6, 0x7, 1, &r, &kind) ==
          TIKU_EDIT_OK && r.start == 2 && kind == 0, "IND-003",
          "backward takes the last start before the place");
    check(tiku_editdoc_indicator_next(doc, 2, 0x2, 0, &r, &kind) ==
          TIKU_EDIT_OK && r.start == 6 && r.end == 8 && kind == 1,
          "IND-003", "only the kinds asked for are found");
    check(tiku_editdoc_indicator_next(doc, 13, 0x7, 0, &r, NULL) ==
              TIKU_EDIT_NOTHING &&
          tiku_editdoc_indicator_next(doc, 2, 0x7, 1, &r, NULL) ==
              TIKU_EDIT_NOTHING, "IND-003", "none that way is nothing");
    tiku_editdoc_indicators_each(doc, span, 0x7, count_each, &n);
    check(n == 2, "IND-004", "a visit stops when it is told to");
    n = 0;
    span.start = 4;
    span.end = 6;
    tiku_editdoc_indicators_each(doc, span, 0x7, count_each, &n);
    check(n == 0, "IND-004",
          "a span ending where one starts, or starting where one ends, "
          "touches none");
    check(tiku_editdoc_indicators_clear(doc, 0x1) == 2 &&
          tiku_editdoc_indicator_next(doc, -1, 0x7, 0, &r, &kind) ==
              TIKU_EDIT_OK && kind != 0, "IND-004",
          "clearing a kind removes its indicators and no others");
    tiku_editdoc_release(doc);
}

/** @brief Annotations: added under lines, read back by index, following
 *         their lines, cleared and counted. */
static void
annotations(void)
{
    tiku_editdoc_t *doc = doc_of("a\nb\nc");
    unsigned kind = 99;
    uint64_t s0 = 0, s1 = 0, s2 = 0;

    check(tiku_editdoc_annotation_add(doc, 3, "x", 0) ==
              TIKU_EDIT_INVALID_RANGE &&
          tiku_editdoc_annotation_add(doc, 1, "x", 8) == TIKU_EDIT_INVALID &&
          tiku_editdoc_annotation_add(doc, 1, "", 0) == TIKU_EDIT_NOTHING &&
          tiku_editdoc_annotation_add(doc, 1, NULL, 0) == TIKU_EDIT_NOTHING &&
          tiku_editdoc_annotation_add(doc, 1, "\xff", 0) ==
              TIKU_EDIT_INVALID_TEXT, "ANN-001",
          "a line, kind or text that is not there is refused");
    (void)tiku_editdoc_annotations(doc, &s0);
    check(tiku_editdoc_annotation_add(doc, 1, "first\nsecond", 2) ==
              TIKU_EDIT_OK &&
          tiku_editdoc_annotation_add(doc, 1, "third", 1) == TIKU_EDIT_OK &&
          tiku_editdoc_annotations(doc, &s1) == 2 && s1 != s0, "ANN-001",
          "annotations are added, counted, and change the serial");
    check(!strcmp(tiku_editdoc_annotation(doc, 1, 0, &kind),
                  "first\nsecond") && kind == 2 &&
          !strcmp(tiku_editdoc_annotation(doc, 1, 1, &kind), "third") &&
          kind == 1 && tiku_editdoc_annotation(doc, 1, 2, NULL) == NULL &&
          tiku_editdoc_annotation(doc, 0, 0, NULL) == NULL, "ANN-001",
          "a line's annotations read back in order, with their kinds");
    put(doc, 0, 0, "x\ny\n");
    check(tiku_editdoc_annotation(doc, 3, 0, NULL) != NULL &&
          tiku_editdoc_annotation(doc, 1, 0, NULL) == NULL, "ANN-002",
          "lines put in above move them down with their line");
    put(doc, 6, 6, "q\n");
    check(tiku_editdoc_annotation(doc, 3, 0, NULL) == NULL &&
          tiku_editdoc_annotation(doc, 4, 0, NULL) != NULL, "ANN-002",
          "lines put in at their line's start push them along");
    put(doc, 7, 8, "");
    check(tiku_editdoc_annotation(doc, 3, 1, NULL) != NULL, "ANN-002",
          "removing the line break before their line moves them to the "
          "line the edit starts on");
    (void)tiku_editdoc_annotations(doc, &s2);
    check(s2 == s1, "ANN-003", "edits leave the serial alone");
    check(tiku_editdoc_annotations_clear(doc, 3, 0x2) == 1 &&
          tiku_editdoc_annotations(doc, &s2) == 1 && s2 != s1 &&
          tiku_editdoc_annotations_clear(doc, -1, 0xff) == 1 &&
          tiku_editdoc_annotations(doc, NULL) == 0, "ANN-003",
          "clearing by line and kind, or everywhere, removes them");
    (void)tiku_editdoc_annotation_add(doc, 0, "z", 0);
    tiku_editdoc_load(doc, "new", 3);
    check(tiku_editdoc_annotations(doc, NULL) == 0, "ANN-003",
          "a load removes them");
    tiku_editdoc_release(doc);
}

/** @brief Where a range end goes when [s, e) becomes n bytes: the model
 *         the random edits are checked against. */
static int64_t
model_end(int64_t p, int starts, int64_t s, int64_t e, int64_t n)
{
    if (p < s)
        return p;
    if (p > e)
        return p - (e - s) + n;
    return starts ? s + n : s;
}

/** @brief Random replacements and undos over many indicators, each range
 *         checked against the model after every step, and no allocation
 *         while a change is committed. */
static void
random_edits(void)
{
    static const char *const pieces[] = {
        "", "a", "bc", "def ghij", "klmnopqrstu", "v w"
    };
    tiku_editdoc_t *doc = doc_of("the quick brown fox jumps over the dog");
    tiku_edit_indicator_t d[12];
    int64_t s[12], e[12];
    unsigned long before = tiku_ed_test_commit_allocations();
    unsigned seed = 77;
    int i, k, kept = 1;

    for (k = 0; k < 12; k++) {
        s[k] = k * 3;
        e[k] = s[k] + 1 + k % 4;
        d[k] = ind(doc, s[k], e[k], (unsigned)k % 3);
    }
    for (i = 0; i < 500 && kept; i++) {
        int64_t len = tiku_editdoc_length(doc), a, b, n;
        const char *text;

        seed = seed * 1103515245u + 12345u;
        a = (int64_t)(seed >> 8) % (len + 1);
        seed = seed * 1103515245u + 12345u;
        b = a + (int64_t)(seed >> 8) % 6;
        if (b > len)
            b = len;
        text = pieces[(seed >> 16) % 6];
        n = (int64_t)strlen(text);
        if (put(doc, a, b, text) != TIKU_EDIT_OK)
            continue;
        for (k = 0; k < 12; k++) {
            if (s[k] < 0)
                continue;
            s[k] = model_end(s[k], 1, a, b, n);
            e[k] = model_end(e[k], 0, a, b, n);
            if (e[k] <= s[k])
                s[k] = e[k] = -1;
            if (s[k] < 0 ? !gone(doc, d[k]) : !covers(doc, d[k], s[k], e[k]))
                kept = 0;
        }
    }
    check(kept, "IND-002", "ranges follow 500 random replacements as the "
          "model says, removed when emptied");
    check(tiku_ed_test_commit_allocations() == before, "IND-002",
          "and no change allocates while it is committed");
    tiku_editdoc_release(doc);
}

/* An allocator whose every allocation can be made to fail. */
static int refuse;

static void *
t_alloc(size_t n)
{
    return refuse ? NULL : malloc(n);
}

static void *
t_realloc(void *p, size_t n)
{
    return refuse ? NULL : realloc(p, n);
}

/** @brief With no room, adding changes nothing and says so. */
static void
no_room(void)
{
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    tiku_editdoc_t *doc = doc_of("abc\ndef");
    tiku_edit_range_t r = { 0, 2 };
    tiku_edit_indicator_t d;
    int i;

    for (i = 0; i < 4; i++)
        (void)ind(doc, 0, 1, 0);
    tiku_ed_test_allocator(&failing);
    refuse = 1;
    check(tiku_editdoc_indicator_add(doc, r, 0, &d) == TIKU_EDIT_NO_MEMORY &&
          tiku_editdoc_annotation_add(doc, 0, "x", 0) == TIKU_EDIT_NO_MEMORY &&
          tiku_editdoc_annotations(doc, NULL) == 0, "IND-001",
          "with no room an indicator or annotation is refused, nothing "
          "kept");
    refuse = 0;
    tiku_ed_test_allocator(NULL);
    check(tiku_editdoc_annotation_add(doc, 0, "x", 0) == TIKU_EDIT_OK,
          "ANN-001", "and with room again it is added");
    tiku_editdoc_release(doc);
}

int
main(void)
{
    adding();
    following();
    searching();
    annotations();
    random_edits();
    no_room();
    if (fails) {
        printf("FAIL: %d checks\n", fails);
        return 1;
    }
    printf("PASS: editdoc notes -- indicators added, following their text, "
           "found and visited; annotations following their lines; random "
           "edits; no room\n");
    return 0;
}
