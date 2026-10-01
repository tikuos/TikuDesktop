/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_find.c - literal search in an editor document and from a
 * code view: queries refused, matches within a line and never
 * overlapping, case and whole words, searching both ways with wrapping,
 * Replace and Replace All, and every answer compared with a plain model
 * of the same rules over random texts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
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

/** @brief A new document holding @p n bytes of @p s. */
static tiku_editdoc_t *
doc_of_n(const char *s, size_t n)
{
    tiku_editdoc_t *doc = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, n) != TIKU_EDIT_OK)
        exit(1);
    return doc;
}

static tiku_editdoc_t *
doc_of(const char *s)
{
    return doc_of_n(s, strlen(s));
}

/** @brief A query for the string @p s. */
static tiku_edit_query_t
query(const char *s, unsigned flags)
{
    tiku_edit_query_t q;

    q.text = s;
    q.length = strlen(s);
    q.flags = flags;
    return q;
}

/** @brief The match found from @p from, as its start, or -1. */
static tiku_edit_pos_t
find(tiku_editdoc_t *doc, const char *s, unsigned flags,
     tiku_edit_pos_t from, int backward, int *wrapped)
{
    tiku_edit_query_t q = query(s, flags);
    tiku_edit_range_t m;

    if (tiku_edit_find(doc, &q, from, backward, &m, wrapped) != TIKU_EDIT_OK)
        return -1;
    return m.start;
}

/** @brief Whether the document holds exactly @p s. */
static int
holds(tiku_editdoc_t *doc, const char *s)
{
    char buf[4096];
    size_t n = 0;
    tiku_edit_range_t all = { 0, 0 };

    all.end = tiku_editdoc_length(doc);
    return tiku_editdoc_read(doc, all, buf, sizeof buf, &n) == TIKU_EDIT_OK &&
           n == strlen(s) && memcmp(buf, s, n) == 0;
}

/** @brief Queries that cannot be looked for are refused. */
static void
refusals(void)
{
    tiku_editdoc_t *doc = doc_of("ab\ncd");
    tiku_edit_query_t q;
    tiku_edit_range_t m;

    q = query("", 0);
    check(tiku_edit_find(doc, &q, 0, 0, &m, NULL) == TIKU_EDIT_INVALID_TEXT,
          "FND-001", "an empty query is refused");
    q = query("b\nc", 0);
    check(tiku_edit_find(doc, &q, 0, 0, &m, NULL) == TIKU_EDIT_INVALID_TEXT,
          "FND-001", "and one holding a line break");
    q = query("b\rc", 0);
    check(tiku_edit_find(doc, &q, 0, 0, &m, NULL) == TIKU_EDIT_INVALID_TEXT,
          "FND-001", "a CR among them");
    q.text = "a\xff";
    q.length = 2;
    check(tiku_edit_find(doc, &q, 0, 0, &m, NULL) == TIKU_EDIT_INVALID_TEXT,
          "FND-001", "and malformed UTF-8");
    q.text = "a\0b";
    q.length = 3;
    check(tiku_edit_find(doc, &q, 0, 0, &m, NULL) == TIKU_EDIT_INVALID_TEXT,
          "FND-001", "and a NUL");
    q = query("cd", 0);
    check(tiku_edit_find(doc, &q, 9, 0, &m, NULL) == TIKU_EDIT_INVALID_RANGE,
          "FND-001", "a search from outside the text is refused");
    check(find(doc, "bc", 0, 0, 0, NULL) == -1, "FND-002",
          "a match never runs across a line break");
    tiku_editdoc_release(doc);
}

/** @brief Case, whole words, overlap and wrapping. */
static void
rules(void)
{
    tiku_editdoc_t *doc = doc_of("Alpha alphabet ALPHA_1 caf\xc3\xa9 caf\n"
                                 "aaaa \xc3\x89t\xc3\xa9 \xc3\xa9t\xc3\xa9");
    int wrapped = -1;

    check(find(doc, "alpha", 0, 0, 0, NULL) == 0 &&
          find(doc, "alpha", TIKU_EDIT_MATCH_CASE, 0, 0, NULL) == 6,
          "FND-003", "without Match case ASCII letters fold; with it they "
          "do not");
    check(find(doc, "\xc3\xa9t\xc3\xa9", 0, 0, 0, NULL) == 44, "FND-003",
          "letters past ASCII never fold");
    check(find(doc, "alpha", TIKU_EDIT_WHOLE_WORD, 1, 0, NULL) == 0 &&
          find(doc, "caf", TIKU_EDIT_WHOLE_WORD, 0, 0, NULL) == 29,
          "FND-004", "a whole word touches no letter, digit, '_' or byte "
          "past ASCII, so ALPHA_1 and caf\xc3\xa9 do not hold one");
    check(find(doc, "aa", 0, 34, 0, NULL) == 35 &&
          find(doc, "aa", 0, 37, 1, NULL) == 35 &&
          find(doc, "aa", 0, 35, 1, NULL) == 33, "FND-005",
          "a line is read from its start, so aa in aaaa is found at 0 and "
          "2 and not at 1, whichever way the search goes");
    check(find(doc, "Alpha", TIKU_EDIT_MATCH_CASE, 5, 0, &wrapped) == 0 &&
          wrapped == 1, "FND-006", "past the last match the search starts "
          "again from the top, and says so");
    check(find(doc, "caf", TIKU_EDIT_WHOLE_WORD, 2, 1, &wrapped) == 29 &&
          wrapped == 1, "FND-006", "and before the first, from the end");
    check(find(doc, "ALPHA", TIKU_EDIT_MATCH_CASE, 0, 0, &wrapped) == 15 &&
          wrapped == 0, "FND-006", "without wrapping it says it did not");
    check(find(doc, "zebra", 0, 0, 0, NULL) == -1, "FND-006",
          "with no match anywhere there is nothing");
    tiku_editdoc_release(doc);
}

/** @brief A match the buffer's gap runs through is found like any. */
static void
gap(void)
{
    tiku_editdoc_t *doc = doc_of("hello world, hello");
    tiku_edit_replace_t r = { { 5, 5 }, "X", 1 };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    tiku_editdoc_apply(doc, &t);
    r.range.start = 5;
    r.range.end = 6;
    r.text = NULL;
    r.length = 0;
    tiku_editdoc_apply(doc, &t);
    check(find(doc, "lo wo", 0, 0, 0, NULL) == 3 &&
          find(doc, "lo wo", 0, 18, 1, NULL) == 3, "FND-007",
          "a match across the place the last edit left the gap is found");
    tiku_editdoc_release(doc);
}

/* The plain model: the same rules on a C string. */
static int
m_fold(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int
m_word(int c)
{
    return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') ||
           (m_fold(c) >= 'a' && m_fold(c) <= 'z');
}

/** @brief Every match, as starts, by the plain rules. */
static size_t
m_all(const unsigned char *s, size_t n, const unsigned char *q, size_t m,
      unsigned flags, size_t *out)
{
    size_t count = 0, start = 0, i;

    while (start <= n) {
        size_t end = start;

        while (end < n && s[end] != '\n' && s[end] != '\r')
            end++;
        for (i = start; i + m <= end;) {
            size_t k;
            int ok = 1;

            for (k = 0; k < m && ok; k++) {
                ok = (flags & TIKU_EDIT_MATCH_CASE) ? s[i + k] == q[k]
                                                    : m_fold(s[i + k]) ==
                                                      m_fold(q[k]);
            }
            if (ok && (flags & TIKU_EDIT_WHOLE_WORD))
                ok = !(i > 0 && m_word(s[i - 1])) &&
                     !(i + m < n && m_word(s[i + m]));
            if (ok) {
                out[count++] = i;
                i += m;
            } else {
                i++;
            }
        }
        if (end >= n)
            break;
        start = end + (s[end] == '\r' && end + 1 < n && s[end + 1] == '\n'
                       ? 2 : 1);
    }
    return count;
}

/** @brief A random text of @p n bytes from a small alphabet that makes
 *         matches, words and every kind of line break likely. */
static size_t
m_text(unsigned *seed, unsigned char *s, size_t n)
{
    static const char *const bits[] = {
        "a", "b", "A", "_", " ", "\xc3\xa9", "\n", "\r\n", "\r", "ab", "aa"
    };
    size_t len = 0;

    while (len < n) {
        const char *b;

        *seed = *seed * 1103515245u + 12345u;
        b = bits[(*seed >> 8) % 11];
        if (len + strlen(b) > n)
            break;
        memcpy(s + len, b, strlen(b));
        len += strlen(b);
    }
    return len;
}

/** @brief Find both ways and Replace All agree with the model on random
 *         texts, queries, options and starting points. */
static void
model(void)
{
    static const char *const queries[] = {
        "a", "aa", "ab", "A", "b a", "\xc3\xa9", "a_", "ba", "aab", " "
    };
    unsigned seed = 7;
    int round, agree = 1, replaced_ok = 1;

    for (round = 0; round < 3000 && agree; round++) {
        unsigned char s[64];
        size_t n, starts[64], count, i;
        const char *q;
        unsigned flags;
        tiku_editdoc_t *doc;
        tiku_edit_pos_t from;
        int back;

        n = m_text(&seed, s, sizeof s - 1);
        seed = seed * 1103515245u + 12345u;
        q = queries[(seed >> 8) % 10];
        flags = (seed >> 16) % 4;
        count = m_all(s, n, (const unsigned char *)q, strlen(q), flags,
                      starts);
        doc = doc_of_n((const char *)s, n);
        seed = seed * 1103515245u + 12345u;
        from = (tiku_edit_pos_t)((seed >> 8) % (n + 1));
        while (!tiku_editdoc_is_boundary(doc, from))
            from--;
        back = (seed >> 20) & 1;
        {
            tiku_edit_pos_t want = -1;
            int want_wrap = 0, got_wrap = 0;

            if (!back) {
                for (i = 0; i < count && want < 0; i++)
                    if ((tiku_edit_pos_t)starts[i] >= from)
                        want = (tiku_edit_pos_t)starts[i];
                if (want < 0 && count > 0) {
                    want = (tiku_edit_pos_t)starts[0];
                    want_wrap = 1;
                }
            } else {
                for (i = 0; i < count; i++)
                    if ((tiku_edit_pos_t)starts[i] < from)
                        want = (tiku_edit_pos_t)starts[i];
                if (want < 0 && count > 0) {
                    want = (tiku_edit_pos_t)starts[count - 1];
                    want_wrap = 1;
                }
            }
            if (find(doc, q, flags, from, back, &got_wrap) != want ||
                (want >= 0 && got_wrap != want_wrap)) {
                agree = 0;
                printf("         %s from %lld %s: want %lld\n", q,
                       (long long)from, back ? "back" : "on",
                       (long long)want);
            }
        }
        if (round % 4 == 0) {
            tiku_edit_query_t tq = query(q, flags);
            tiku_edit_view_t none = { 0, 0 };
            unsigned char want[256];
            size_t done = 0, wn = 0, at = 0;

            for (i = 0; i < count; i++) {
                memcpy(want + wn, s + at, starts[i] - at);
                wn += starts[i] - at;
                memcpy(want + wn, "<>", 2);
                wn += 2;
                at = starts[i] + strlen(q);
            }
            memcpy(want + wn, s + at, n - at);
            wn += n - at;
            want[wn] = '\0';
            if (tiku_edit_replace_all(doc, &tq, "<>", 2, none, &done) !=
                (count ? TIKU_EDIT_OK : TIKU_EDIT_NOTHING) || done != count ||
                !holds(doc, (const char *)want))
                replaced_ok = 0;
        }
        tiku_editdoc_release(doc);
    }
    check(agree, "FND-008", "Find both ways agrees with the plain model of "
          "the rules on 3,000 random texts, queries and options");
    check(replaced_ok, "FND-008", "and Replace All on every fourth of them");
}

/** @brief A view of @p s in a box. */
static tiku_codeview_t *
view_of(const char *s)
{
    tiku_editdoc_t *doc = doc_of(s);
    tiku_codeview_t *v = NULL;
    tiku_rect_t box = { 0, 0, 300, 100 };

    if (tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    return v;
}

/** @brief Whether the selection is @p anchor to @p caret. */
static int
is(tiku_codeview_t *v, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == anchor && c == caret;
}

/** @brief Find from a view selects the match and shows it; the next
 *         search starts past it, a backward one before it. */
static void
view_find(void)
{
    char text[4000];
    tiku_codeview_t *v;
    tiku_edit_query_t q = query("needle", 0);
    tiku_codeview_found_t f;
    int i;

    text[0] = '\0';
    for (i = 0; i < 60; i++)
        strcat(text, i == 50 ? "a needle here\n" : "hay\n");
    strcat(text, "needle again");
    v = view_of(text);
    check(tiku_codeview_find(v, &q, 0, &f) == TIKU_EDIT_OK && f.found &&
          !f.wrapped && is(v, 202, 208) && tiku_codeview_top(v) > 0,
          "FND-009", "Find selects the match from its start to its end and "
          "scrolls to it");
    check(tiku_codeview_find(v, &q, 0, &f) == TIKU_EDIT_OK &&
          is(v, 250, 256) && tiku_codeview_find(v, &q, 0, &f) ==
          TIKU_EDIT_OK && f.wrapped && is(v, 202, 208), "FND-009",
          "the next starts after it, and wraps");
    check(tiku_codeview_find(v, &q, 1, &f) == TIKU_EDIT_OK && f.wrapped &&
          is(v, 250, 256) && tiku_codeview_find(v, &q, 1, &f) ==
          TIKU_EDIT_OK && !f.wrapped && is(v, 202, 208), "FND-009",
          "backward, from the selection's start");
    q = query("nothing like it", 0);
    check(tiku_codeview_find(v, &q, 0, &f) == TIKU_EDIT_NOTHING && !f.found &&
          is(v, 202, 208), "FND-009", "with no match the selection stays");
    tiku_codeview_destroy(v);
}

/** @brief Replace changes only a selection that is a match, then moves
 *         on; Replace All changes every match as one step. */
static void
view_replace(void)
{
    tiku_codeview_t *v = view_of("one two one\ntwo one");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_codeview_t *other = NULL;
    tiku_edit_query_t q = query("one", TIKU_EDIT_WHOLE_WORD);
    tiku_codeview_found_t f;

    tiku_codeview_select(v, 4, 7);
    check(tiku_codeview_replace(v, &q, "1", 1, &f) == TIKU_EDIT_OK &&
          f.replaced == 0 && holds(doc, "one two one\ntwo one") &&
          is(v, 8, 11), "FND-010",
          "Replace with a selection that is not a match only finds the "
          "next");
    check(tiku_codeview_replace(v, &q, "1", 1, &f) == TIKU_EDIT_OK &&
          f.replaced == 1 && holds(doc, "one two 1\ntwo one") &&
          is(v, 14, 17), "FND-010",
          "with one that is, it replaces it and selects the next match");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(doc, "one two one\ntwo one"), "FND-010",
          "and that is one undo step");

    tiku_codeview_create(doc, &other);
    tiku_codeview_select(other, 16, 16);
    tiku_codeview_select(v, 0, 0);
    check(tiku_codeview_replace_all(v, &q, "uno", 3, &f) == TIKU_EDIT_OK &&
          f.replaced == 3 && holds(doc, "uno two uno\ntwo uno") &&
          is(other, 16, 16), "FND-011",
          "Replace All replaces every match, and another view's caret "
          "moves with its text");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(doc, "one two one\ntwo one") && !tiku_editdoc_can_undo(doc),
          "FND-011", "as one undo step");
    q = query("absent", 0);
    check(tiku_codeview_replace_all(v, &q, "x", 1, &f) == TIKU_EDIT_NOTHING &&
          f.replaced == 0, "FND-011", "with no match it changes nothing");
    q = query("one", 0);
    tiku_editdoc_set_read_only(doc, 1);
    check(tiku_codeview_replace_all(v, &q, "x", 1, &f) ==
          TIKU_EDIT_READ_ONLY && holds(doc, "one two one\ntwo one"),
          "FND-011", "a read-only document refuses it whole");
    tiku_editdoc_set_read_only(doc, 0);
    tiku_codeview_destroy(other);
    tiku_codeview_destroy(v);
}

/* An allocator that fails the n-th attempt from now. */
static long attempts, fail_at;

static void *
t_alloc(size_t n)
{
    return ++attempts == fail_at ? NULL : malloc(n);
}

static void *
t_realloc(void *p, size_t n)
{
    return ++attempts == fail_at ? NULL : realloc(p, n);
}

/** @brief Replace All that cannot allocate changes nothing, and works
 *         once it can. */
static void
replace_all_faults(void)
{
    tiku_ed_allocator_t counted = { t_alloc, t_realloc, free };
    char text[8000];
    tiku_editdoc_t *doc;
    tiku_edit_query_t q = query("ab", 0);
    tiku_edit_view_t none = { 0, 0 };
    size_t count = 0;
    int n, intact = 1;

    text[0] = '\0';
    for (n = 0; n < 500; n++)
        strcat(text, "ab cd\n");
    doc = doc_of(text);
    tiku_ed_test_allocator(&counted);
    for (n = 1; n < 400; n++) {
        tiku_edit_status_t st;

        attempts = 0;
        fail_at = n;
        st = tiku_edit_replace_all(doc, &q, "x", 1, none, &count);
        fail_at = 0;
        if (st == TIKU_EDIT_OK)
            break;
        if (st != TIKU_EDIT_NO_MEMORY || !holds(doc, text) || count != 0)
            intact = 0;
    }
    tiku_ed_test_allocator(NULL);
    check(intact && n > 1 && count == 500, "FND-012",
          "Replace All that cannot allocate changes nothing, and succeeds "
          "once it can");
    tiku_editdoc_release(doc);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    refusals();
    rules();
    gap();
    model();
    view_find();
    view_replace();
    replace_all_faults();
    printf("%s\n", fails ? "FAIL: code view search" :
           "PASS: code view search -- refusals, case, whole words, no "
           "overlap, wrapping, the gap, the model, Replace, Replace All");
    return fails != 0;
}
