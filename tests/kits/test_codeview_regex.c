/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_regex.c - regular expressions from a code view: groups
 * put into replacements, empty matches found once and replaced only when
 * found, UTF-8 kept whole, refusals, and Replace All that cannot allocate
 * changing nothing at every point it can fail.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview_private.h"
#include "tiku_sys.h"

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

/** @brief A view of a new document holding @p text; the view holds the
 *         only reference. */
static tiku_codeview_t *
page(const char *text)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, text, strlen(text)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_editdoc_release(doc);
    return v;
}

/** @brief Whether the view's document holds exactly @p text. */
static int
holds(tiku_codeview_t *v, const char *text)
{
    tiku_edit_range_t all = { 0, 0 };
    char result[512];
    size_t n = 0;

    all.end = tiku_editdoc_length(v->doc);
    return tiku_editdoc_read(v->doc, all, result, sizeof result, &n) ==
               TIKU_EDIT_OK &&
           n == strlen(text) && memcmp(result, text, n) == 0;
}

/** @brief A case-sensitive regex query for @p s. */
static tiku_edit_query_t
query(const char *s)
{
    tiku_edit_query_t q;

    q.text = s;
    q.length = strlen(s);
    q.flags = TIKU_EDIT_REGEX | TIKU_EDIT_MATCH_CASE;
    return q;
}

/** @brief The view's caret. */
static tiku_edit_pos_t
caret_of(tiku_codeview_t *v)
{
    tiku_edit_pos_t anchor = -1, caret = -1;

    (void)tiku_codeview_selection(v, &anchor, &caret);
    return caret;
}

/* Allocations left before the next one fails. */
static int allocations;

/** @brief An allocation that fails once the count runs out. */
static void *
fail_after_alloc(size_t n)
{
    return allocations-- == 0 ? NULL : malloc(n);
}

/** @brief The same for growing. */
static void *
fail_after_realloc(void *p, size_t n)
{
    return allocations-- == 0 ? NULL : realloc(p, n);
}

/** @brief An allocation that always fails. */
static void *
no_alloc(size_t n)
{
    (void)n;
    return NULL;
}

/** @brief The same for growing. */
static void *
no_realloc(void *p, size_t n)
{
    (void)p;
    (void)n;
    return NULL;
}

/** @brief Groups go into replacements; a pattern or replacement that
 *         cannot be used is refused, the text as it was. */
static void
captures(void)
{
    tiku_codeview_t *v = page("a=12\r\nb=34\r\n");
    tiku_edit_query_t q = query("([a-z])=([0-9]+)");
    tiku_codeview_found_t found;
    tiku_edit_range_t m;

    check(tiku_codeview_replace_all(v, &q, "$2:${1}", 7, &found) ==
              TIKU_EDIT_OK &&
          found.replaced == 2 && holds(v, "12:a\r\n34:b\r\n"), "REX-002",
          "groups go into every replacement");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK &&
          holds(v, "a=12\r\nb=34\r\n"), "REX-002",
          "Replace All is one undo step");
    q = query("B=3");
    q.flags = TIKU_EDIT_REGEX;
    check(tiku_edit_find(v->doc, &q, 0, 0, &m, NULL) == TIKU_EDIT_OK &&
          m.start == 6, "FND-003",
          "a regular expression without Match case folds case");
    q = query("([a-z])=([0-9]+)");
    check(tiku_codeview_find(v, &q, 1, &found) == TIKU_EDIT_OK &&
          found.wrapped, "REX-001", "a search back from the start wraps");
    check(tiku_codeview_replace(v, &q, "$2", 2, &found) == TIKU_EDIT_OK &&
          holds(v, "a=12\r\n34\r\n"), "REX-002",
          "Replace puts the found match's group in");
    q = query("(");
    check(tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_BAD_PATTERN,
          "REX-002", "a pattern that does not compile is refused");
    q = query("([a-z])");
    check(tiku_codeview_replace_all(v, &q, "$2", 2, &found) ==
              TIKU_EDIT_BAD_REPLACEMENT &&
          holds(v, "a=12\r\n34\r\n"), "REX-002",
          "a group the pattern does not have is refused");
    tiku_codeview_destroy(v);
}

/** @brief An empty match is found once and the next search goes on past
 *         it, at line starts, line ends and in an empty document. */
static void
empty_matches(void)
{
    tiku_codeview_t *v = page("ab\ncd");
    tiku_edit_query_t q = query("^");
    tiku_codeview_found_t found;
    const tiku_edit_pos_t ends[] = { 2, 6, 8 };
    int i, stops = 1;

    check(tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_OK &&
          caret_of(v) == 0, "REX-001", "a line's start is found");
    check(tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_OK &&
          caret_of(v) == 3, "REX-001", "and then the next line's");
    check(tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_OK &&
          found.wrapped, "REX-001", "and then the first again, wrapping");
    check(tiku_codeview_replace_all(v, &q, ">", 1, &found) ==
              TIKU_EDIT_OK &&
          found.replaced == 2 && holds(v, ">ab\n>cd"), "REX-001",
          "Replace All replaces each empty match once");
    (void)tiku_codeview_undo(v);
    q = query("$");
    check(tiku_codeview_replace_all(v, &q, "!", 1, &found) ==
          TIKU_EDIT_OK && holds(v, "ab!\ncd!"), "REX-001",
          "and each line's end once");
    tiku_codeview_destroy(v);

    v = page("ab\r\ncd\r\n");
    for (i = 0; i < 3; i++)
        stops &= tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_OK &&
                 caret_of(v) == ends[i];
    check(stops, "REX-001", "a line's end is before its CRLF");
    check(tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_OK &&
          found.wrapped, "REX-001", "and the last one is followed by the "
          "first");
    tiku_codeview_destroy(v);

    v = page("");
    q = query("^");
    check(tiku_codeview_find(v, &q, 0, &found) == TIKU_EDIT_OK &&
          tiku_codeview_replace(v, &q, "x", 1, &found) == TIKU_EDIT_OK &&
          holds(v, "x") && found.replaced == 1, "FND-010",
          "the empty match Find selected in an empty document is "
          "replaced");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, ""),
          "FND-010", "and the replacement undone");
    q = query("nomatch");
    check(tiku_codeview_replace_all(v, &q, "$1", 2, &found) ==
          TIKU_EDIT_BAD_REPLACEMENT, "REX-002",
          "a replacement is checked even with no match");
    tiku_codeview_destroy(v);
}

/** @brief Replace acts on an empty selection only when Find selected it
 *         as an empty match, wherever else a pattern matches nothing. */
static void
found_only(void)
{
    tiku_codeview_t *v = page("ab\ncd");
    tiku_edit_query_t q = query("^");
    tiku_codeview_found_t found;

    (void)tiku_codeview_select(v, 0, 0);
    check(tiku_codeview_replace(v, &q, ">", 1, &found) == TIKU_EDIT_OK &&
          found.replaced == 0 && found.found && holds(v, "ab\ncd"),
          "FND-010", "a caret Find did not put on an empty match is not "
          "replaced; the match is found instead");
    check(tiku_codeview_replace(v, &q, ">", 1, &found) == TIKU_EDIT_OK &&
          found.replaced == 1 && holds(v, ">ab\ncd"), "FND-010",
          "the empty match found is replaced");
    tiku_codeview_destroy(v);

    v = page("ab");
    q = query("x*");
    (void)tiku_codeview_select(v, 1, 1);
    check(tiku_codeview_replace(v, &q, "-", 1, &found) == TIKU_EDIT_OK &&
          found.replaced == 0 && holds(v, "ab"), "FND-010",
          "nor is a caret where a pattern matches nothing");
    tiku_codeview_destroy(v);
}

/** @brief Matches keep whole UTF-8 characters; an unmatched group puts
 *         in nothing; a refused Replace All changes nothing. */
static void
utf8(void)
{
    const tiku_ed_allocator_t fail = { no_alloc, no_realloc, free };
    tiku_codeview_t *v = page("\xc3\xa9\xf0\x9f\x98\x80");
    tiku_edit_query_t q = query(".");
    tiku_codeview_found_t found;
    tiku_edit_status_t st;

    check(tiku_codeview_replace_all(v, &q, "[$0]", 4, &found) ==
              TIKU_EDIT_OK &&
          found.replaced == 2 &&
          holds(v, "[\xc3\xa9][\xf0\x9f\x98\x80]"), "REX-001",
          "a character is matched whole, however many bytes it has");
    tiku_codeview_destroy(v);

    v = page("ab b");
    q = query("(a)?b");
    check(tiku_codeview_replace_all(v, &q, "$1$$\\t", 6, &found) ==
          TIKU_EDIT_OK && holds(v, "a$\t $\t"), "REX-002",
          "an unmatched group puts in nothing, $$ a dollar and \\t a tab");
    (void)tiku_codeview_undo(v);
    tiku_ed_test_allocator(&fail);
    st = tiku_codeview_replace_all(v, &q, "x", 1, &found);
    tiku_ed_test_allocator(NULL);
    check(st == TIKU_EDIT_NO_MEMORY && holds(v, "ab b"), "REX-002",
          "Replace All with no room changes nothing");
    tiku_editdoc_set_read_only(v->doc, 1);
    check(tiku_codeview_replace_all(v, &q, "x", 1, &found) ==
          TIKU_EDIT_READ_ONLY && holds(v, "ab b"), "REX-002",
          "nor does Replace All in a document refusing changes");
    tiku_codeview_destroy(v);
}

/** @brief Replace All failing at each allocation in turn changes neither
 *         the text nor the history, until it can allocate and succeeds. */
static void
faults(void)
{
    const tiku_ed_allocator_t fail = { fail_after_alloc, fail_after_realloc,
                                       free };
    int point, succeeded = 0, intact = 1;

    for (point = 0; point < 160 && !succeeded; point++) {
        tiku_codeview_t *v = page("a=1\nb=22\nc=333\n");
        tiku_edit_query_t q = query("([a-z])=([0-9]+)");
        uint64_t state = tiku_editdoc_state(v->doc);
        uint64_t generation = tiku_editdoc_generation(v->doc);
        tiku_codeview_found_t found;
        tiku_edit_status_t st;

        allocations = point;
        tiku_ed_test_allocator(&fail);
        st = tiku_codeview_replace_all(v, &q, "$2:$1", 5, &found);
        tiku_ed_test_allocator(NULL);
        if (st == TIKU_EDIT_OK) {
            intact &= holds(v, "1:a\n22:b\n333:c\n") &&
                      found.replaced == 3 &&
                      tiku_codeview_undo(v) == TIKU_EDIT_OK;
            succeeded = 1;
        } else {
            intact &= st == TIKU_EDIT_NO_MEMORY && found.replaced == 0 &&
                      tiku_editdoc_state(v->doc) == state &&
                      tiku_editdoc_generation(v->doc) == generation;
        }
        intact &= holds(v, "a=1\nb=22\nc=333\n");
        tiku_codeview_destroy(v);
    }
    check(intact && succeeded, "REX-002", "Replace All that cannot "
          "allocate changes nothing, and succeeds once it can");
}

int
main(void)
{
    void *locale = tiku_sys_locale_use(NULL);

    captures();
    empty_matches();
    found_only();
    utf8();
    faults();
    check(tiku_sys_locale_use(NULL) == locale, "REX-001",
          "the thread's locale is as it was");
    printf("%s\n", fails ? "FAIL: code view regex" :
           "PASS: code view regex -- groups, empty matches, Replace on "
           "found matches only, UTF-8, refusals, allocation failures");
    return fails != 0;
}
