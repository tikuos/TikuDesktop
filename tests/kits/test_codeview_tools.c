/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_tools.c - changes made to whole lines or the selection at
 * once, their line breaks kept and each one undo step, changing nothing
 * when they cannot allocate; and matches gathered in a range, literal or
 * regex, up to a capacity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview_private.h"

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

/** @brief A view of a new document holding @p text, tab stops every 4
 *         columns; the view holds the only reference. */
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
    (void)tiku_codeview_set_tab_width(v, 4);
    return v;
}

/** @brief Whether the view's document holds exactly @p text. */
static int
holds(tiku_codeview_t *v, const char *text)
{
    tiku_edit_range_t all = { 0, 0 };
    char bytes[512];
    size_t n = 0;

    all.end = tiku_editdoc_length(v->doc);
    return tiku_editdoc_read(v->doc, all, bytes, sizeof bytes, &n) ==
               TIKU_EDIT_OK &&
           n == strlen(text) && memcmp(bytes, text, n) == 0;
}

/** @brief Whether @p op turns @p from into @p to, a change of case acting
 *         on the whole text selected, undone and redone as one step. */
static int
transforms(const char *from, const char *to, tiku_codeview_transform_t op)
{
    tiku_codeview_t *v = page(from);
    int ok;

    if (op == TIKU_CODEVIEW_UPPER_ASCII || op == TIKU_CODEVIEW_LOWER_ASCII)
        (void)tiku_codeview_select(v, 0, (tiku_edit_pos_t)strlen(from));
    ok = tiku_codeview_transform(v, op) == TIKU_EDIT_OK && holds(v, to) &&
         tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, from) &&
         tiku_codeview_redo(v) == TIKU_EDIT_OK && holds(v, to);
    tiku_codeview_destroy(v);
    return ok;
}

/** @brief Each change keeps the line breaks as they are, and is one undo
 *         step. */
static void
changes(void)
{
    check(transforms("b  \r\na\t\nc \r", "b\r\na\nc\r",
                     TIKU_CODEVIEW_TRIM_TRAILING), "LNC-014",
          "blanks at lines' ends are taken out");
    check(transforms(" \t a\r\n\tb", "     a\r\n    b",
                     TIKU_CODEVIEW_INDENT_TO_SPACES), "LNC-014",
          "indentation is made of spaces to the tab stops");
    check(transforms("     a\r\n    b", "\t a\r\n\tb",
                     TIKU_CODEVIEW_INDENT_TO_TABS), "LNC-014",
          "and of tabs, then spaces");
    check(transforms("z\r\na\nb", "a\r\nb\nz", TIKU_CODEVIEW_SORT_LINES),
          "LNC-014", "lines sort, each place's line break staying");
    check(transforms("z\r\na\n", "a\r\nz\n", TIKU_CODEVIEW_SORT_LINES),
          "LNC-014", "with the last line break kept");
    check(transforms("aZ \xc3\xa9\xc3\x9f", "AZ \xc3\xa9\xc3\x9f",
                     TIKU_CODEVIEW_UPPER_ASCII), "LNC-014",
          "upper case changes ASCII letters only");
    check(transforms("aZ \xc3\xa9\xc3\x9f", "az \xc3\xa9\xc3\x9f",
                     TIKU_CODEVIEW_LOWER_ASCII), "LNC-014",
          "and so does lower case");
}

/** @brief The lines a selection touches are changed, one it ends at the
 *         start of left out; nothing to change is TIKU_EDIT_NOTHING. */
static void
ranges(void)
{
    tiku_codeview_t *v = page("c \nb \na ");

    (void)tiku_codeview_select(v, 1, 3);
    check(tiku_codeview_transform(v, TIKU_CODEVIEW_TRIM_TRAILING) ==
          TIKU_EDIT_OK && holds(v, "c\nb \na "), "LNC-014",
          "a selection ending at a line's start leaves that line out");
    (void)tiku_codeview_undo(v);
    (void)tiku_codeview_select(v, 0, 0);
    check(tiku_codeview_transform(v, TIKU_CODEVIEW_UPPER_ASCII) ==
          TIKU_EDIT_NOTHING, "LNC-014",
          "a change of case with nothing selected does nothing");
    tiku_editdoc_set_read_only(v->doc, 1);
    check(tiku_codeview_transform(v, TIKU_CODEVIEW_SORT_LINES) ==
          TIKU_EDIT_READ_ONLY && holds(v, "c \nb \na "), "LNC-014",
          "a document refusing changes is not changed");
    tiku_codeview_destroy(v);
    v = page("");
    check(tiku_codeview_transform(v, TIKU_CODEVIEW_SORT_LINES) ==
          TIKU_EDIT_NOTHING, "LNC-014", "an empty document has nothing to "
          "sort");
    tiku_codeview_destroy(v);
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

static const tiku_ed_allocator_t failure = { fail_after_alloc,
                                             fail_after_realloc, free };

/** @brief Whether @p op, failing at each allocation in turn, changes
 *         neither the text nor the selection until it succeeds. */
static int
atomic(tiku_codeview_transform_t op)
{
    const char *original = "     zebra \t\r\n\tAlpha  \n";
    tiku_edit_pos_t length = (tiku_edit_pos_t)strlen(original);
    int point, succeeded = 0, intact = 1;

    for (point = 0; point < 160 && !succeeded; point++) {
        tiku_codeview_t *v = page(original);
        uint64_t generation = tiku_editdoc_generation(v->doc);
        tiku_edit_pos_t a = -1, b = -1;
        tiku_edit_status_t st;

        (void)tiku_codeview_select(v, 0, length);
        allocations = point;
        tiku_ed_test_allocator(&failure);
        st = tiku_codeview_transform(v, op);
        tiku_ed_test_allocator(NULL);
        if (st == TIKU_EDIT_OK) {
            succeeded = 1;
            intact &= tiku_codeview_undo(v) == TIKU_EDIT_OK;
        } else {
            intact &= st == TIKU_EDIT_NO_MEMORY &&
                      tiku_editdoc_generation(v->doc) == generation;
        }
        intact &= holds(v, original) &&
                  tiku_codeview_selection(v, &a, &b) == TIKU_EDIT_OK &&
                  a == 0 && b == length;
        tiku_codeview_destroy(v);
    }
    return intact && succeeded;
}

/** @brief Every change that cannot allocate changes nothing, and is
 *         undone whole once it can. */
static void
faults(void)
{
    int op, ok = 1;

    for (op = TIKU_CODEVIEW_TRIM_TRAILING; op <= TIKU_CODEVIEW_SORT_LINES;
         op++)
        ok &= atomic((tiku_codeview_transform_t)op);
    check(ok, "LNC-015", "a change that cannot allocate leaves the text and "
          "the selection, and its undo gives both back");
}

/** @brief Matches are gathered inside a range up to a capacity, with
 *         word and case rules, as regexes too, and never on an error. */
static void
collect(void)
{
    tiku_codeview_t *v = page("aa AA aaa\r\naa\n\xc3\xa9");
    tiku_edit_range_t all = { 0, 0 }, found[10];
    tiku_edit_range_t scope = { 1, 0 }, char_scope = { 14, 16 };
    tiku_edit_range_t split = { 15, 16 };
    tiku_edit_query_t q = { "aa", 2, 0 };
    uint64_t generation = tiku_editdoc_generation(v->doc);
    size_t n = 99;
    int truncated = 99, point, succeeded = 0, clean = 1;

    all.end = tiku_editdoc_length(v->doc);
    scope.end = all.end;
    check(tiku_edit_collect(v->doc, &q, all, found, 10, &n, &truncated) ==
              TIKU_EDIT_OK &&
          n == 4 && !truncated && found[0].start == 0 &&
          found[1].start == 3 && found[3].start == 11, "FND-014",
          "every match is gathered, in order, without overlaps");
    check(tiku_edit_collect(v->doc, &q, all, found, 2, &n, &truncated) ==
          TIKU_EDIT_OK && n == 2 && truncated, "FND-014",
          "a full capacity says there are more");
    check(tiku_edit_collect(v->doc, &q, all, NULL, 0, &n, &truncated) ==
          TIKU_EDIT_OK && n == 0 && truncated, "FND-014",
          "and so does no capacity");
    q.flags = TIKU_EDIT_MATCH_CASE | TIKU_EDIT_WHOLE_WORD;
    check(tiku_edit_collect(v->doc, &q, all, found, 10, &n, &truncated) ==
          TIKU_EDIT_OK && n == 2, "FND-014", "the case and word rules hold");
    check(tiku_edit_collect(v->doc, &q, scope, found, 10, &n, &truncated) ==
          TIKU_EDIT_OK && n == 1 && found[0].start == 11, "FND-014",
          "only matches wholly inside the range are gathered");
    q.text = "^|$";
    q.length = 3;
    q.flags = TIKU_EDIT_REGEX;
    check(tiku_edit_collect(v->doc, &q, all, found, 10, &n, &truncated) ==
          TIKU_EDIT_OK && n == 6, "FND-014",
          "a regex's empty matches are gathered once each");
    q.text = ".";
    q.length = 1;
    check(tiku_edit_collect(v->doc, &q, char_scope, found, 10, &n,
                            &truncated) == TIKU_EDIT_OK &&
          n == 1 && found[0].end == 16, "FND-014",
          "a character is matched whole");
    check(tiku_edit_collect(v->doc, &q, split, found, 10, &n, &truncated) ==
          TIKU_EDIT_INVALID_RANGE && n == 0, "FND-014",
          "a range splitting a character is refused");
    q.text = "[";
    check(tiku_edit_collect(v->doc, &q, all, found, 10, &n, &truncated) ==
          TIKU_EDIT_BAD_PATTERN && n == 0 && !truncated, "FND-014",
          "a pattern that does not compile is refused, nothing gathered");
    q.text = "a";
    for (point = 0; point < 20 && !succeeded; point++) {
        tiku_edit_status_t st;

        allocations = point;
        tiku_ed_test_allocator(&failure);
        st = tiku_edit_collect(v->doc, &q, all, found, 10, &n, &truncated);
        tiku_ed_test_allocator(NULL);
        if (st == TIKU_EDIT_OK)
            succeeded = 1;
        else
            clean &= st == TIKU_EDIT_NO_MEMORY && n == 0 && !truncated;
    }
    check(clean && succeeded && tiku_editdoc_generation(v->doc) ==
          generation, "FND-014", "with no room nothing is gathered, and "
          "the document is never changed");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    changes();
    ranges();
    faults();
    collect();
    printf("%s\n", fails ? "FAIL: code view tools" :
           "PASS: code view tools -- trimming, indentation, case, sorting, "
           "undo and allocation failure, matches gathered in a range");
    return fails != 0;
}
