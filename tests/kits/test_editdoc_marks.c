/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_marks.c - what follows the text: anchors by affinity,
 * each view's selection by the upstream selection rule, and listeners,
 * told after every commit, safe to add and remove while they are told.
 *
 * One case adapts Scintilla 5.6.7 test/unit/testSelection.cxx,
 * SelectionPosition/MoveForInsertDelete, Copyright Neil Hodgson; see
 * kits/editor/LICENSE.Scintilla and kits/editor/UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include <stdio.h>
#include <string.h>

#include "tiku_editdoc.h"

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

    check(tiku_editdoc_create(NULL, &doc) == TIKU_EDIT_OK &&
          tiku_editdoc_load(doc, s, strlen(s)) == TIKU_EDIT_OK, "DOC-002",
          "document");
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

/** @brief Where an anchor is. */
static tiku_edit_pos_t
where(tiku_editdoc_t *doc, tiku_edit_anchor_t a)
{
    tiku_edit_pos_t pos = -1;

    tiku_editdoc_anchor_pos(doc, a, &pos);
    return pos;
}

/** @brief Whether a view's selection is @p anchor .. @p caret. */
static int
selects(tiku_editdoc_t *doc, tiku_edit_view_t v, tiku_edit_pos_t anchor,
        tiku_edit_pos_t caret)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_editdoc_view_selection(doc, v, &a, &c) == TIKU_EDIT_OK &&
           a == anchor && c == caret;
}

/** @brief Anchors: each affinity at an insertion point, inside and at the
 *         ends of a replaced range, before and after it. */
static void
anchors(void)
{
    static const struct {
        int64_t at;                     /* where the anchor starts       */
        int64_t s, e;                   /* the range replaced            */
        const char *text;               /* by this                       */
        int64_t left, right;            /* where each affinity goes      */
        const char *why;
    } cases[] = {
        { 2, 2, 2, "XY", 2, 4, "insertion at the anchor" },
        { 1, 2, 2, "XY", 1, 1, "insertion after it" },
        { 3, 2, 2, "XY", 5, 5, "insertion before it" },
        { 3, 2, 5, "Q", 2, 3, "inside a replaced range" },
        { 2, 2, 5, "Q", 2, 3, "at a replaced range's start" },
        { 5, 2, 5, "Q", 2, 3, "at a replaced range's end" },
        { 6, 2, 5, "Q", 4, 4, "after a replaced range" },
        { 3, 2, 5, "", 2, 2, "inside a deletion, either way" }
    };
    size_t i;

    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        tiku_editdoc_t *doc = doc_of("abcdefgh");
        tiku_edit_anchor_t l, r;

        check(tiku_editdoc_anchor_create(doc, cases[i].at, TIKU_EDIT_LEFT,
                                         &l) == TIKU_EDIT_OK &&
              tiku_editdoc_anchor_create(doc, cases[i].at, TIKU_EDIT_RIGHT,
                                         &r) == TIKU_EDIT_OK,
              "MRK-001", "two anchors");
        check(put(doc, cases[i].s, cases[i].e, cases[i].text) == 0, "MRK-001",
              "the edit");
        check(where(doc, l) == cases[i].left, "MRK-002", cases[i].why);
        check(where(doc, r) == cases[i].right, "MRK-002", cases[i].why);
        tiku_editdoc_release(doc);
    }
}

/** @brief A transaction of several replacements moves anchors as the
 *         replacements would one by one from the last; undo maps back. */
static void
anchors_through_history(void)
{
    tiku_editdoc_t *doc = doc_of("0123456789");
    tiku_edit_anchor_t a[4];
    tiku_edit_replace_t r[] = {
        { { 1, 1 }, "AA", 2 }, { { 4, 6 }, "", 0 }, { { 8, 9 }, "BBB", 3 }
    };
    tiku_edit_transaction_t t;
    static const int64_t at[4] = { 1, 5, 7, 9 };
    static const int64_t after[4] = { 3, 6, 7, 11 };
    int i;

    for (i = 0; i < 4; i++)
        tiku_editdoc_anchor_create(doc, at[i], TIKU_EDIT_RIGHT, &a[i]);
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = 3;
    check(tiku_editdoc_apply(doc, &t) == 0, "MRK-003", "three replacements");
    for (i = 0; i < 4; i++)
        check(where(doc, a[i]) == after[i], "MRK-003",
              "each anchor where the replacements put it");
    check(tiku_editdoc_undo(doc) == 0 && where(doc, a[0]) == 1 &&
          where(doc, a[2]) == 7 && where(doc, a[3]) == 9, "MRK-004",
          "undo maps anchors back through the inverse edits");
    check(where(doc, a[1]) == 6, "MRK-004",
          "an anchor a deletion collapsed does not return: right of the "
          "restored text, not where it was");
    tiku_editdoc_release(doc);
}

/** @brief Handles: boundaries only, and a freed slot's handle dies. */
static void
anchor_handles(void)
{
    tiku_editdoc_t *doc = doc_of("a\xc3\xa9");
    tiku_edit_anchor_t a, b;
    tiku_edit_pos_t pos;

    check(tiku_editdoc_anchor_create(doc, 2, TIKU_EDIT_LEFT, &a) ==
          TIKU_EDIT_INVALID_RANGE, "MRK-005", "not inside a character");
    check(tiku_editdoc_anchor_create(doc, 1, TIKU_EDIT_LEFT, &a) == 0 &&
          tiku_editdoc_anchor_move(doc, a, 3) == 0 && where(doc, a) == 3 &&
          tiku_editdoc_anchor_move(doc, a, 2) == TIKU_EDIT_INVALID_RANGE,
          "MRK-005", "moved to boundaries only");
    tiku_editdoc_anchor_destroy(doc, a);
    check(tiku_editdoc_anchor_pos(doc, a, &pos) == TIKU_EDIT_INVALID &&
          tiku_editdoc_anchor_move(doc, a, 0) == TIKU_EDIT_INVALID,
          "MRK-005", "a destroyed anchor's handle is dead");
    check(tiku_editdoc_anchor_create(doc, 0, TIKU_EDIT_LEFT, &b) == 0 &&
          b.slot == a.slot && b.generation != a.generation &&
          tiku_editdoc_anchor_pos(doc, a, &pos) == TIKU_EDIT_INVALID,
          "MRK-005", "a reused slot does not revive the old handle");
    tiku_editdoc_release(doc);
}

/** @brief Selections move by the upstream rule: an empty caret stays
 *         before text inserted at it; a selection keeps what it selects. */
static void
selections(void)
{
    tiku_editdoc_t *doc = doc_of("abcdefgh");
    tiku_edit_view_t v, w;

    check(tiku_editdoc_view_attach(doc, &v) == 0 &&
          tiku_editdoc_view_attach(doc, &w) == 0 && selects(doc, v, 0, 0),
          "MRK-008", "views start at 0");
    tiku_editdoc_view_select(doc, v, 2, 2);
    put(doc, 2, 2, "XY");
    check(selects(doc, v, 2, 2), "MRK-006",
          "an empty caret stays before an insertion at it");
    tiku_editdoc_view_select(doc, v, 2, 4);
    tiku_editdoc_view_select(doc, w, 4, 2);
    put(doc, 2, 2, "!");
    check(selects(doc, v, 3, 5) && selects(doc, w, 5, 3), "MRK-006",
          "an insertion at a selection's start moves the whole selection");
    put(doc, 5, 5, "?");
    check(selects(doc, v, 3, 5) && selects(doc, w, 5, 3), "MRK-006",
          "an insertion at its end stays outside it");
    tiku_editdoc_view_select(doc, v, 1, 4);
    put(doc, 0, 3, "");
    check(selects(doc, v, 0, 1), "MRK-006",
          "a deletion over its start collapses that end to the deletion");
    tiku_editdoc_view_select(doc, v, 1, 3);
    put(doc, 0, 5, "ZZZZZZ");
    check(selects(doc, v, 0, 0), "MRK-006",
          "a replacement over the whole selection leaves a caret at its start");
    check(tiku_editdoc_view_select(doc, v, 99, 0) == TIKU_EDIT_INVALID_RANGE,
          "MRK-008", "a selection outside the text is refused");
    tiku_editdoc_view_detach(doc, w);
    check(tiku_editdoc_view_select(doc, w, 0, 0) == TIKU_EDIT_INVALID,
          "MRK-008", "a detached view's handle is dead");
    tiku_editdoc_release(doc);

    /* A caret an edit leaves between a CR and an LF steps back to the CR. */
    doc = doc_of("a\rXb\nc");
    tiku_editdoc_view_attach(doc, &v);
    check(tiku_editdoc_view_select(doc, v, 4, 4) == 0 &&
          put(doc, 2, 4, "") == 0, "MRK-007",
          "the edit joins a CR and an LF");
    check(selects(doc, v, 1, 1), "MRK-007",
          "the caret leaves the middle of the CRLF for the line's end");
    check(tiku_editdoc_view_select(doc, v, 2, 2) == TIKU_EDIT_INVALID_RANGE,
          "MRK-008", "no selection between a CR and its LF");
    tiku_editdoc_release(doc);
}

/** @brief The upstream steps with no virtual space, each on the position
 *         the step before left.  The virtual-space steps are excluded, so
 *         the sequence restarts at 4 where the first of them was. */
static void
selection_upstream(void)
{
    const char *name = "SelectionPosition/MoveForInsertDelete";
    tiku_editdoc_t *doc = doc_of("abcdefghij");
    tiku_edit_view_t v;

    tiku_editdoc_view_attach(doc, &v);
    tiku_editdoc_view_select(doc, v, 2, 2);
    put(doc, 0, 0, "x");
    check(selects(doc, v, 3, 3), name, "an insertion before it moves it");
    tiku_editdoc_view_select(doc, v, 4, 4);
    put(doc, 4, 5, "");
    check(selects(doc, v, 4, 4), name, "a deletion starting at it leaves it");
    put(doc, 3, 4, "");
    check(selects(doc, v, 3, 3), name, "a deletion ending at it moves it");
    put(doc, 3, 3, "x");
    check(selects(doc, v, 3, 3), name,
          "an insertion at it, not moving for equal, leaves it");
    tiku_editdoc_view_select(doc, v, 3, 5);
    put(doc, 3, 3, "x");
    check(selects(doc, v, 4, 6), name,
          "moving for equal, as a selection's start does, moves it");
    tiku_editdoc_view_select(doc, v, 3, 3);
    put(doc, 2, 7, "");
    check(selects(doc, v, 2, 2), name,
          "a deletion over it moves it to the deletion's start");
    tiku_editdoc_release(doc);
}

/** @brief What a listener heard, and what it tried while hearing it. */
typedef struct {
    int calls;                          /* notifications heard          */
    tiku_edit_change_t last;            /* the last one                 */
    tiku_edit_range_t before[4];        /* its spans, before            */
    tiku_edit_range_t after[4];         /* and after                    */
    tiku_edit_status_t edit, undo, load, select, anchor;
    int unlisten_self;                  /* remove itself when called    */
    tiku_edit_listener_t self;          /* its own handle               */
    tiku_edit_listener_t victim;        /* one to remove when called    */
    int add;                            /* add a listener when called   */
    tiku_edit_view_t view;              /* a view to select in          */
    int release;                        /* drop the last reference      */
} heard_t;

static heard_t *added_heard;

/** @brief A listener that records the change and tries things. */
static void
hear(void *ctx, tiku_editdoc_t *doc, const tiku_edit_change_t *change)
{
    heard_t *h = ctx;
    tiku_edit_listener_t more;
    tiku_edit_anchor_t a;
    size_t i;

    h->calls++;
    h->last = *change;
    for (i = 0; i < change->count && i < 4; i++)
        tiku_edit_change_span(change, i, &h->before[i], &h->after[i]);
    h->edit = put(doc, 0, 0, "x");
    h->undo = tiku_editdoc_undo(doc);
    h->load = tiku_editdoc_load(doc, "y", 1);
    h->select = tiku_editdoc_view_select(doc, h->view, 0, 0);
    h->anchor = tiku_editdoc_anchor_create(doc, 0, TIKU_EDIT_LEFT, &a);
    if (h->unlisten_self)
        tiku_editdoc_unlisten(doc, h->self);
    if (h->victim.slot)
        tiku_editdoc_unlisten(doc, h->victim);
    if (h->add && added_heard) {
        tiku_editdoc_listen(doc, hear, added_heard, &more);
        h->add = 0;
    }
    if (h->release)
        tiku_editdoc_release(doc);
}

/** @brief Notifications: after the commit, what changed and how; nothing
 *         may change the text while they run. */
static void
listeners(void)
{
    tiku_editdoc_t *doc = doc_of("abcdef");
    heard_t h, h2, h3;
    tiku_edit_replace_t r[] = { { { 1, 2 }, "XY", 2 }, { { 4, 6 }, "", 0 } };
    tiku_edit_transaction_t t;
    tiku_edit_listener_t l1, l2;

    memset(&h, 0, sizeof h);
    memset(&h2, 0, sizeof h2);
    memset(&h3, 0, sizeof h3);
    tiku_editdoc_view_attach(doc, &h.view);
    check(tiku_editdoc_listen(doc, hear, &h, &l1) == 0, "MRK-009", "listen");
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = 2;
    t.view = h.view;
    check(tiku_editdoc_apply(doc, &t) == 0 && h.calls == 1 &&
          h.last.cause == TIKU_EDIT_EDITED && h.last.count == 2 &&
          h.last.generation_after == h.last.generation_before + 1 &&
          h.last.view.slot == h.view.slot, "MRK-009",
          "one notification after the commit, two spans, the view");
    check(h.before[0].start == 1 && h.before[0].end == 2 &&
          h.after[0].start == 1 && h.after[0].end == 3 &&
          h.before[1].start == 4 && h.before[1].end == 6 &&
          h.after[1].start == 5 && h.after[1].end == 5, "MRK-009",
          "spans in the coordinates before and after");
    check(h.edit == TIKU_EDIT_BUSY && h.undo == TIKU_EDIT_BUSY &&
          h.load == TIKU_EDIT_BUSY && h.select == TIKU_EDIT_OK &&
          h.anchor == TIKU_EDIT_OK, "MRK-010",
          "no change of text inside a notification; selections and "
          "anchors may be set");
    check(tiku_editdoc_undo(doc) == 0 && h.last.cause == TIKU_EDIT_UNDONE &&
          h.before[0].start == 1 && h.before[0].end == 3 &&
          h.after[0].start == 1 && h.after[0].end == 2 &&
          h.before[1].start == 5 && h.before[1].end == 5 &&
          h.after[1].start == 4 && h.after[1].end == 6, "MRK-009",
          "undo is told with the spans reversed");
    check(tiku_editdoc_redo(doc) == 0 && h.last.cause == TIKU_EDIT_REDONE,
          "MRK-009", "redo is told");
    check(tiku_editdoc_load(doc, "new\n", 4) == 0 &&
          h.last.cause == TIKU_EDIT_LOADED && h.last.count == 1 &&
          h.before[0].end == 5 && h.after[0].end == 4 &&
          h.last.lines_after == 2, "MRK-009", "load is told as one span");

    /* Removing itself and the next one, and adding one, while told. */
    h.unlisten_self = 1;
    h.self = l1;
    check(tiku_editdoc_listen(doc, hear, &h2, &l2) == 0, "MRK-011", "second");
    h.victim = l2;
    h.add = 1;
    added_heard = &h3;
    h.calls = h2.calls = 0;
    check(put(doc, 0, 0, "z") == 0 && h.calls == 1 && h2.calls == 0 &&
          h3.calls == 0, "MRK-011",
          "a listener removed while told is not called; one added waits");
    check(put(doc, 0, 0, "z") == 0 && h.calls == 1 && h3.calls == 1,
          "MRK-011", "the removed stays removed; the added hears the next");
    tiku_editdoc_release(doc);

    /* The last reference dropped by a listener: the document lives until
     * the notification ends, and is freed then. */
    doc = doc_of("abc");
    memset(&h, 0, sizeof h);
    h.release = 1;
    tiku_editdoc_listen(doc, hear, &h, &l1);
    check(put(doc, 0, 0, "q") == 0 && h.calls == 1, "MRK-012",
          "released from inside, freed after");
}

/** @brief A listener added while told lands in a later slot of a table
 *         that has to grow: the walk reaches it, and it must still wait
 *         for the next change. */
static void
listener_added_later(void)
{
    tiku_editdoc_t *doc = doc_of("abc");
    heard_t more[5];
    tiku_edit_listener_t l;
    int i;

    memset(more, 0, sizeof more);
    more[0].add = 1;
    added_heard = &more[4];
    for (i = 0; i < 4; i++)
        tiku_editdoc_listen(doc, hear, &more[i], &l);
    check(put(doc, 0, 0, "q") == 0 && more[3].calls == 1 &&
          more[4].calls == 0, "MRK-011",
          "one added in a later slot waits, even when the table grows");
    check(put(doc, 0, 0, "q") == 0 && more[4].calls == 1, "MRK-011",
          "and hears the next");
    tiku_editdoc_release(doc);
}

int
main(void)
{
    anchors();
    anchors_through_history();
    anchor_handles();
    selections();
    selection_upstream();
    listeners();
    listener_added_later();
    printf("%s\n", fails ? "FAIL: editor marks" :
           "PASS: editor marks -- anchors by affinity, selections by the "
           "upstream rule, CRLF snap, listeners told after commits");
    return fails != 0;
}
