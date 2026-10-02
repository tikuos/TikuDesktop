/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_faults.c - every allocation an operation makes is failed
 * in turn: each failure must leave everything observable as it was, the
 * operation must succeed once allocation does, no commit may allocate,
 * and nothing may be left allocated at the end.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* The test allocator: every block carries its size ahead of it, so live
 * blocks can be counted, and the n-th attempt from now can be failed. */
static long live, attempts, fail_at;

/** @brief Whether this attempt is the one to fail. */
static int
failing(void)
{
    attempts++;
    return fail_at && attempts == fail_at;
}

/** @brief malloc through the counter. */
static void *
t_alloc(size_t n)
{
    size_t *p;

    if (failing() || n > SIZE_MAX - 16)
        return NULL;
    p = malloc(n + 16);
    if (!p)
        return NULL;
    *p = n;
    live++;
    return (char *)p + 16;
}

/** @brief realloc through the counter; a failure leaves the block. */
static void *
t_realloc(void *old, size_t n)
{
    char *p;

    if (!old)
        return t_alloc(n);
    if (failing() || n > SIZE_MAX - 16)
        return NULL;
    p = realloc((char *)old - 16, n + 16);
    if (!p)
        return NULL;
    *(size_t *)p = n;
    return p + 16;
}

/** @brief free through the counter. */
static void
t_free(void *p)
{
    if (p) {
        live--;
        free((char *)p - 16);
    }
}

/** @brief What a caller can see of a document, to compare across a
 *         failed operation. */
typedef struct {
    char text[65536];           /* the bytes                            */
    size_t n;                   /* how many                             */
    int64_t lines;              /* line count                           */
    int64_t starts[64];         /* the first line starts                */
    uint64_t generation;        /* the generation                       */
    uint64_t state;             /* the history state                    */
    int modified, undo, redo;   /* the flags                            */
    tiku_edit_pos_t anchor;     /* the watched anchor                   */
    tiku_edit_selection_t sel[8]; /* the watched view's selections      */
    size_t nsel, main;          /* how many, and the main one           */
    int heard;                  /* notifications so far                 */
} seen_t;

static int heard;
static tiku_edit_anchor_t watched_anchor;
static tiku_edit_view_t watched_view;

/** @brief A listener that only counts. */
static void
count_heard(void *ctx, tiku_editdoc_t *doc, const tiku_edit_change_t *change)
{
    (void)ctx;
    (void)doc;
    (void)change;
    heard++;
}

/** @brief Record what is visible of @p doc. */
static void
look(tiku_editdoc_t *doc, seen_t *s)
{
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };
    int64_t i;

    memset(s, 0, sizeof *s);
    tiku_editdoc_read(doc, all, s->text, sizeof s->text, &s->n);
    s->lines = tiku_editdoc_lines(doc);
    for (i = 0; i < 64 && i <= s->lines; i++)
        s->starts[i] = tiku_editdoc_line_start(doc, i);
    s->generation = tiku_editdoc_generation(doc);
    s->state = tiku_editdoc_state(doc);
    s->modified = tiku_editdoc_modified(doc);
    s->undo = tiku_editdoc_can_undo(doc);
    s->redo = tiku_editdoc_can_redo(doc);
    tiku_editdoc_anchor_pos(doc, watched_anchor, &s->anchor);
    s->nsel = tiku_editdoc_view_selections(doc, watched_view, s->sel, 8,
                                           &s->main);
    s->heard = heard;
}

/** @brief One operation under test. */
typedef tiku_edit_status_t (*op_fn)(tiku_editdoc_t *doc);

/** @brief Fail each allocation of @p op in turn on @p doc: every failure
 *         must be NO_MEMORY with nothing visible changed; then it must
 *         succeed.  @return the number of failures injected. */
static int
sweep(tiku_editdoc_t *doc, op_fn op, const char *row)
{
    seen_t before, after;
    tiku_edit_status_t st;
    int k;

    for (k = 1; k < 1000; k++) {
        look(doc, &before);
        fail_at = attempts + k;
        st = op(doc);
        fail_at = 0;
        if (st == TIKU_EDIT_OK)
            return k - 1;
        look(doc, &after);
        check(st == TIKU_EDIT_NO_MEMORY, row, "a failed allocation says so");
        check(!memcmp(&before, &after, sizeof before), row,
              "nothing visible changed, no listener told");
    }
    check(0, row, "never succeeded");
    return k;
}

/** @brief Build a document holding lines of text, with a watched anchor,
 *         view and listener. */
static tiku_editdoc_t *
prepared(const char *text)
{
    tiku_editdoc_t *doc = NULL;
    tiku_edit_listener_t l;

    check(tiku_editdoc_create(NULL, &doc) == TIKU_EDIT_OK &&
          tiku_editdoc_load(doc, text, strlen(text)) == TIKU_EDIT_OK &&
          tiku_editdoc_anchor_create(doc, 3, TIKU_EDIT_RIGHT,
                                     &watched_anchor) == TIKU_EDIT_OK &&
          tiku_editdoc_view_attach(doc, &watched_view) == TIKU_EDIT_OK &&
          tiku_editdoc_view_select(doc, watched_view, 2, 4) == TIKU_EDIT_OK &&
          tiku_editdoc_listen(doc, count_heard, NULL, &l) == TIKU_EDIT_OK,
          "FAULT", "setup");
    return doc;
}

/** @brief Replace [s, e) by @p text, flagged, from the watched view. */
static tiku_edit_status_t
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text,
    unsigned flags)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.flags = flags;
    t.view = watched_view;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief A multi-line paste that grows text, lines and history. */
static tiku_edit_status_t
op_paste(tiku_editdoc_t *doc)
{
    static char big[4000];
    int i;

    for (i = 0; i < (int)sizeof big - 1; i++)
        big[i] = i % 7 == 6 ? '\n' : (i % 13 == 12 ? '\r' : 'p');
    big[sizeof big - 1] = 0;
    return put(doc, 1, 2, big, 0);
}

static int64_t type_at;

/** @brief Typing at type_at: a new step, or one joining the last. */
static tiku_edit_status_t
op_type(tiku_editdoc_t *doc)
{
    return put(doc, type_at, type_at, "t", TIKU_EDIT_TYPING);
}

/** @brief A Replace All of many pieces. */
static tiku_edit_status_t
op_replace_all(tiku_editdoc_t *doc)
{
    tiku_edit_replace_t r[40];
    tiku_edit_transaction_t t;
    tiku_edit_pos_t pos = 0;
    int i;


    /* At forty caret positions in a row, whatever earlier passes added. */
    for (i = 0; i < 40; i++) {
        r[i].range.start = r[i].range.end = pos;
        r[i].text = "\xc3\xa9\r\n";
        r[i].length = 4;
        pos = tiku_editdoc_next(doc, pos);
    }
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = 40;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief Four selections at the first caret positions, the view's room
 *         for them grown. */
static tiku_edit_status_t
op_select_many(tiku_editdoc_t *doc)
{
    tiku_edit_selection_t s[4];
    tiku_edit_pos_t pos = 0;
    int i;

    for (i = 0; i < 4; i++) {
        s[i].anchor = pos;
        pos = tiku_editdoc_next(doc, tiku_editdoc_next(doc, pos));
        s[i].caret = i % 2 ? pos : s[i].anchor;
        pos = tiku_editdoc_next(doc, pos);
    }
    return tiku_editdoc_view_select_many(doc, watched_view, s, 4, 2);
}

/** @brief Typing at five carets a caret position apart, setting five
 *         after it. */
static tiku_edit_status_t
op_type_many(tiku_editdoc_t *doc)
{
    tiku_edit_replace_t r[5];
    tiku_edit_selection_t after[5];
    tiku_edit_transaction_t t;
    tiku_edit_pos_t pos = 0;
    int i;

    for (i = 0; i < 5; i++) {
        r[i].range.start = r[i].range.end = pos;
        r[i].text = "m";
        r[i].length = 1;
        after[i].anchor = after[i].caret = pos + i + 1;
        pos = tiku_editdoc_next(doc, pos);
    }
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = 5;
    t.view = watched_view;
    t.select = 1;
    t.selections = after;
    t.nselections = 5;
    t.main = 4;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief Undo and redo. */
static tiku_edit_status_t
op_undo(tiku_editdoc_t *doc)
{
    return tiku_editdoc_undo(doc);
}

static tiku_edit_status_t
op_redo(tiku_editdoc_t *doc)
{
    return tiku_editdoc_redo(doc);
}

/** @brief Load a new text. */
static tiku_edit_status_t
op_load(tiku_editdoc_t *doc)
{
    return tiku_editdoc_load(doc, "new\r\ntext\n", 10);
}

/** @brief Take and free a snapshot. */
static tiku_edit_status_t
op_snapshot(tiku_editdoc_t *doc)
{
    tiku_edit_snapshot_t snap;
    tiku_edit_status_t st = tiku_editdoc_snapshot(doc, &snap);

    if (st == TIKU_EDIT_OK)
        tiku_edit_snapshot_free(&snap);
    return st;
}

/** @brief Grow each registry past its first allocation. */
static tiku_edit_status_t
op_anchor(tiku_editdoc_t *doc)
{
    tiku_edit_anchor_t a;

    return tiku_editdoc_anchor_create(doc, 0, TIKU_EDIT_LEFT, &a);
}

static tiku_edit_status_t
op_view(tiku_editdoc_t *doc)
{
    tiku_edit_view_t v;

    return tiku_editdoc_view_attach(doc, &v);
}

static tiku_edit_status_t
op_listen(tiku_editdoc_t *doc)
{
    tiku_edit_listener_t l;

    return tiku_editdoc_listen(doc, count_heard, NULL, &l);
}

/** @brief Create a document, failing each of its allocations. */
static void
creation(void)
{
    long k;

    for (k = 1; k < 100; k++) {
        tiku_editdoc_t *doc = (tiku_editdoc_t *)&k;
        long before = live;
        tiku_edit_status_t st;

        fail_at = attempts + k;
        st = tiku_editdoc_create(NULL, &doc);
        fail_at = 0;
        if (st == TIKU_EDIT_OK) {
            tiku_editdoc_release(doc);
            check(live == before, "FAULT", "a released document frees all");
            return;
        }
        check(st == TIKU_EDIT_NO_MEMORY && doc == NULL && live == before,
              "FAULT", "a failed create holds nothing");
    }
    check(0, "FAULT", "create never succeeded");
}

int
main(void)
{
    static const tiku_ed_allocator_t counted = { t_alloc, t_realloc, t_free };
    unsigned long commits = tiku_ed_test_commit_allocations();
    tiku_editdoc_t *doc;
    int injected = 0, i;

    tiku_ed_test_allocator(&counted);
    creation();

    doc = prepared("line one\nline two\r\nline three");
    injected += sweep(doc, op_paste, "TXN-ATOMIC");
    type_at = 1;
    injected += sweep(doc, op_type, "TXN-ATOMIC");
    type_at = 2;
    injected += sweep(doc, op_type, "HIS-JOIN");      /* merges into it */
    injected += sweep(doc, op_replace_all, "TXN-ATOMIC");
    for (i = 0; i < 20; i++)                          /* past 16 records */
        injected += sweep(doc, op_replace_all, "HIS-GROW");
    injected += sweep(doc, op_undo, "HIS-UNDO");
    injected += sweep(doc, op_undo, "HIS-UNDO");
    injected += sweep(doc, op_redo, "HIS-REDO");
    injected += sweep(doc, op_snapshot, "DOC-011");
    injected += sweep(doc, op_select_many, "SEL-007");
    injected += sweep(doc, op_type_many, "SEL-007");
    type_at = 0;
    injected += sweep(doc, op_type, "SEL-007");
    injected += sweep(doc, op_undo, "SEL-007");
    injected += sweep(doc, op_undo, "SEL-007");
    injected += sweep(doc, op_redo, "SEL-007");
    for (i = 0; i < 6; i++) {
        injected += sweep(doc, op_anchor, "MRK-005");
        injected += sweep(doc, op_view, "MRK-008");
        injected += sweep(doc, op_listen, "MRK-009");
    }
    injected += sweep(doc, op_load, "DOC-012");
    tiku_editdoc_release(doc);

    check(injected > 30, "FAULT", "allocations were failed");
    check(tiku_ed_test_commit_allocations() == commits, "TXN-NOALLOC",
          "no commit allocated");
    check(live == 0, "FAULT", "nothing left allocated");
    tiku_ed_test_allocator(NULL);
    printf("%s (%d failures injected)\n", fails ? "FAIL: editor faults" :
           "PASS: editor faults -- every allocation failed in turn leaves "
           "the document as it was", injected);
    return fails != 0;
}
