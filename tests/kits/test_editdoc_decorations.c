/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_decorations.c - line markers, which follow their lines
 * through edits, undo and redo, and the execution line, which a change
 * makes stale and never moves.
 *
 * One function adapts Scintilla 5.6.7 test/unit/testPerLine.cxx,
 * LineMarkers, Copyright Neil Hodgson; see kits/editor/LICENSE.Scintilla
 * and kits/editor/UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
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

/** @brief A marker of kind 0 on @p line. */
static tiku_edit_marker_t
mark(tiku_editdoc_t *doc, int64_t line)
{
    tiku_edit_marker_t m = { 0, 0 };

    if (tiku_editdoc_marker_add(doc, line, 0, &m) != TIKU_EDIT_OK)
        exit(1);
    return m;
}

/** @brief Where line @p line starts. */
static int64_t
start(tiku_editdoc_t *doc, int64_t line)
{
    return tiku_editdoc_line_start(doc, line);
}

/** @brief Adding, finding, removing and clearing markers, and their
 *         handles. */
static void
handles(void)
{
    tiku_editdoc_t *doc = doc_of("zero\none\ntwo\nthree\n");
    tiku_edit_marker_t a, b, c, old;
    uint32_t kinds;

    check(tiku_editdoc_marker_add(doc, 1, 3, &a) == TIKU_EDIT_OK &&
          tiku_editdoc_marker_line(doc, a) == 1, "LMK-001",
          "a marker is added to a line and reports it");
    check(tiku_editdoc_marker_add(doc, 4, 0, NULL) == TIKU_EDIT_OK,
          "LMK-001", "the empty line after a final break can be marked");
    check(tiku_editdoc_marker_add(doc, 5, 0, &b) ==
              TIKU_EDIT_INVALID_RANGE &&
          tiku_editdoc_marker_add(doc, -1, 0, &b) ==
              TIKU_EDIT_INVALID_RANGE, "LMK-001",
          "a line outside the text is refused");
    check(tiku_editdoc_marker_add(doc, 1, TIKU_EDIT_MARKER_KINDS, &b) ==
              TIKU_EDIT_INVALID &&
          tiku_editdoc_marker_add(NULL, 1, 0, &b) == TIKU_EDIT_INVALID,
          "LMK-001", "a kind past the last, or no document, is refused");
    check(tiku_editdoc_marker_add(doc, 1, 31, &b) == TIKU_EDIT_OK &&
          tiku_editdoc_marker_add(doc, 3, 3, &c) == TIKU_EDIT_OK,
          "LMK-001", "kinds 0 to 31 are taken");
    kinds = tiku_editdoc_markers_on(doc, 1);
    check(kinds == ((1u << 3) | (1u << 31)) &&
          tiku_editdoc_markers_on(doc, 2) == 0 &&
          tiku_editdoc_markers_on(doc, 9) == 0, "LMK-002",
          "the kinds on a line come back as a set");
    check(tiku_editdoc_markers_next(doc, 0, 1u << 3) == 1 &&
          tiku_editdoc_markers_next(doc, 2, 1u << 3) == 3 &&
          tiku_editdoc_markers_next(doc, 4, 1u << 3) == -1 &&
          tiku_editdoc_markers_next(doc, 0, 1u << 7) == -1, "LMK-002",
          "the next marked line among some kinds is found, or none");
    tiku_editdoc_marker_remove(doc, a);
    check(tiku_editdoc_marker_line(doc, a) == -1 &&
          tiku_editdoc_markers_on(doc, 1) == 1u << 31, "LMK-001",
          "a removed marker's handle is dead and its kind is gone");
    old = a;
    check(tiku_editdoc_marker_add(doc, 2, 3, &a) == TIKU_EDIT_OK &&
          a.slot == old.slot && tiku_editdoc_marker_line(doc, old) == -1,
          "LMK-001", "a reused slot does not revive the old handle");
    tiku_editdoc_marker_remove(doc, old);
    check(tiku_editdoc_marker_line(doc, a) == 2, "LMK-001",
          "and removing through the dead handle removes nothing");
    check(tiku_editdoc_markers_clear(doc, 3, 1u << 3) == 1 &&
          tiku_editdoc_marker_line(doc, c) == -1 &&
          tiku_editdoc_marker_line(doc, a) == 2, "LMK-002",
          "clearing a line removes only its markers of those kinds");
    check(tiku_editdoc_markers_clear(doc, -1, 1u << 3) == 1 &&
          tiku_editdoc_markers_clear(doc, -1, 1u << 3) == 0 &&
          tiku_editdoc_marker_line(doc, b) == 1, "LMK-002",
          "and clearing every line counts what went");
    tiku_editdoc_set_read_only(doc, 1);
    check(tiku_editdoc_marker_add(doc, 0, 1, NULL) == TIKU_EDIT_OK &&
          tiku_editdoc_markers_clear(doc, 0, 1u << 1) == 1, "LMK-001",
          "markers are kept on a read-only document too");
    tiku_editdoc_release(doc);
}

/** @brief A marker stays with its line's text through edits inside it,
 *         at its end and at its start. */
static void
inserts(void)
{
    tiku_editdoc_t *doc = doc_of("zero\none\ntwo\n");
    tiku_edit_marker_t m = mark(doc, 1);

    put(doc, start(doc, 1) + 2, start(doc, 1) + 2, "xx");
    put(doc, tiku_editdoc_line_end(doc, 1), tiku_editdoc_line_end(doc, 1),
        "yy");
    check(tiku_editdoc_marker_line(doc, m) == 1 &&
          tiku_editdoc_markers_on(doc, 1) == 1, "LMK-003",
          "text inside a marked line, or at its end, leaves it there");
    put(doc, start(doc, 1) + 3, start(doc, 1) + 3, "\n");
    check(tiku_editdoc_marker_line(doc, m) == 1, "LMK-003",
          "splitting the line keeps the marker on the first part");
    put(doc, tiku_editdoc_line_end(doc, 0), tiku_editdoc_line_end(doc, 0),
        "\nnew");
    check(tiku_editdoc_marker_line(doc, m) == 2, "LMK-003",
          "a line added above moves it down with its line");
    put(doc, start(doc, 2), start(doc, 2), "lead ");
    check(tiku_editdoc_marker_line(doc, m) == 2, "LMK-004",
          "text without a line break typed at its line's start keeps it");
    put(doc, start(doc, 2), start(doc, 2), "a\nb\n");
    check(tiku_editdoc_marker_line(doc, m) == 4, "LMK-004",
          "text bringing two line breaks to its start moves it two down");
    put(doc, start(doc, 4), start(doc, 4) + 2, "c\nd");
    check(tiku_editdoc_marker_line(doc, m) == 5, "LMK-004",
          "a replacement starting there moves it to where the text ends");
    tiku_editdoc_release(doc);

    doc = doc_of("a\r\nb\r\nc\rd");
    m = mark(doc, 2);
    put(doc, start(doc, 2), start(doc, 2), "x\r\n");
    check(tiku_editdoc_marker_line(doc, m) == 3, "LMK-004",
          "CRLF counts as one line break when it is inserted");
    put(doc, start(doc, 3) - 2, start(doc, 3) - 1, "");
    check(tiku_editdoc_marker_line(doc, m) == 3 &&
          tiku_editdoc_markers_on(doc, 3) == 1, "LMK-004",
          "and taking the CR of a CRLF leaves an LF that still ends the "
          "line");
    tiku_editdoc_release(doc);
}

/** @brief Removing the line break before a marked line merges its
 *         marker into the line the edit starts on. */
static void
removals(void)
{
    tiku_editdoc_t *doc = doc_of("zero\none\ntwo\nthree\nfour\n");
    tiku_edit_marker_t one = mark(doc, 1), two = mark(doc, 2);
    tiku_edit_marker_t three = mark(doc, 3), four = mark(doc, 4);

    put(doc, start(doc, 1) - 1, start(doc, 1), "");
    check(tiku_editdoc_marker_line(doc, one) == 0, "LMK-005",
          "Backspace at a marked line's start joins its marker to the "
          "line above");
    check(tiku_editdoc_marker_line(doc, two) == 1, "LMK-005",
          "and the lines below move up with their markers");
    put(doc, start(doc, 1), start(doc, 3), "");
    check(tiku_editdoc_marker_line(doc, two) == 1 &&
          tiku_editdoc_marker_line(doc, three) == 1, "LMK-005",
          "deleting whole lines keeps the first line's marker and merges "
          "the next one's into it");
    check(tiku_editdoc_marker_line(doc, four) == 1 &&
          tiku_editdoc_markers_on(doc, 1) == 1, "LMK-005",
          "the line whose start the deletion ended at merges too");
    put(doc, 2, start(doc, 1) + 1, "Q");
    check(tiku_editdoc_marker_line(doc, four) == 0 &&
          tiku_editdoc_marker_line(doc, one) == 0, "LMK-005",
          "a replacement from inside a line merges into that line");
    tiku_editdoc_release(doc);

    doc = doc_of("zero\none\ntwo\n");
    one = mark(doc, 1);
    put(doc, 2, start(doc, 1), "X\nY");
    check(tiku_editdoc_marker_line(doc, one) == 0, "LMK-005",
          "a replacement ending at the marked line's start goes to the "
          "line it starts on, whatever line breaks it brings");
    tiku_editdoc_release(doc);
    doc = doc_of("zero\none\ntwo\n");
    one = mark(doc, 1);
    put(doc, 2, start(doc, 1) + 2, "X\nY");
    check(tiku_editdoc_marker_line(doc, one) == 0, "LMK-005",
          "and so does one running past it");
    tiku_editdoc_release(doc);
}

/** @brief Several replacements at once, and undo and redo. */
static void
history(void)
{
    tiku_editdoc_t *doc = doc_of("a\nb\nc\nd\ne\n");
    tiku_edit_marker_t b = mark(doc, 1), d = mark(doc, 3), e = mark(doc, 4);
    tiku_edit_replace_t r[3] = {
        { { 0, 0 }, "x\n", 2 },
        { { 4, 6 }, "", 0 },
        { { 8, 8 }, "y\nz\n", 4 }
    };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = 3;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK, "LMK-006", "apply");
    /* The lines are now x, a, b, d, y, z, e: c's line went. */
    check(tiku_editdoc_marker_line(doc, b) == 2 &&
          tiku_editdoc_marker_line(doc, d) == 3 &&
          tiku_editdoc_marker_line(doc, e) == 6, "LMK-006",
          "a transaction moves markers as its replacements would");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_marker_line(doc, b) == 1 &&
          tiku_editdoc_marker_line(doc, d) == 3 &&
          tiku_editdoc_marker_line(doc, e) == 4, "LMK-006",
          "undo moves them back through the inverse replacements");
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_marker_line(doc, e) == 6, "LMK-006",
          "and redo forward again");
    tiku_editdoc_release(doc);

    doc = doc_of("a\nb\nc\n");
    b = mark(doc, 1);
    put(doc, 1, 2, "");
    check(tiku_editdoc_marker_line(doc, b) == 0, "LMK-006", "merged");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_marker_line(doc, b) == 0, "LMK-006",
          "undoing the join does not put a merged marker back");
    tiku_editdoc_release(doc);
}

/** @brief Upstream LineMarkers, each section on a fresh document: its
 *         lines are inserted and removed as edits of the text, and its
 *         marker numbers are kinds. */
static void
upstream_line_markers(void)
{
    tiku_editdoc_t *doc = doc_of("");
    tiku_edit_marker_t h1, h2;

    check(tiku_editdoc_markers_on(doc, 0) == 0, "LMK-002",
          "PerLine/LineMarkers/Initial");
    tiku_editdoc_release(doc);

    doc = doc_of("\n\n\n\n\n");
    check(tiku_editdoc_marker_add(doc, 0, 1, &h1) == TIKU_EDIT_OK &&
          tiku_editdoc_markers_on(doc, 0) == 2 &&
          tiku_editdoc_marker_line(doc, h1) == 0, "LMK-001",
          "PerLine/LineMarkers/AddDelete: add");
    check(tiku_editdoc_markers_clear(doc, 0, 1u << 1) == 1 &&
          tiku_editdoc_markers_on(doc, 0) == 0, "LMK-002",
          "PerLine/LineMarkers/AddDelete: delete");
    check(tiku_editdoc_marker_add(doc, 0, 2, &h2) == TIKU_EDIT_OK &&
          tiku_editdoc_markers_on(doc, 0) == 4, "LMK-001",
          "PerLine/LineMarkers/AddDelete: add another");
    tiku_editdoc_marker_remove(doc, h2);
    check(tiku_editdoc_markers_on(doc, 0) == 0, "LMK-001",
          "PerLine/LineMarkers/AddDelete: delete from handle");
    tiku_editdoc_release(doc);

    doc = doc_of("\n\n\n\n\n");
    tiku_editdoc_marker_add(doc, 1, 1, NULL);
    tiku_editdoc_marker_add(doc, 2, 2, NULL);
    check(tiku_editdoc_markers_next(doc, 0, 6) == 1 &&
          tiku_editdoc_markers_next(doc, 2, 6) == 2 &&
          tiku_editdoc_markers_next(doc, 3, 6) == -1, "LMK-002",
          "PerLine/LineMarkers/MarkerNext");
    tiku_editdoc_release(doc);

    doc = doc_of("a\nb\nc\nd\n");
    tiku_editdoc_marker_add(doc, 1, 1, NULL);
    tiku_editdoc_marker_add(doc, 2, 2, NULL);
    put(doc, start(doc, 2) - 1, start(doc, 2), "");
    check(tiku_editdoc_markers_on(doc, 1) == 6 &&
          tiku_editdoc_markers_on(doc, 2) == 0, "LMK-005",
          "PerLine/LineMarkers/MergeMarkers");
    tiku_editdoc_release(doc);

    doc = doc_of("a\nb\nc\nd\n");
    tiku_editdoc_marker_add(doc, 1, 1, &h1);
    tiku_editdoc_marker_add(doc, 2, 2, &h2);
    put(doc, start(doc, 2), start(doc, 2), "\n");
    check(tiku_editdoc_markers_on(doc, 0) == 0 &&
          tiku_editdoc_markers_on(doc, 1) == 2 &&
          tiku_editdoc_markers_on(doc, 2) == 0 &&
          tiku_editdoc_markers_on(doc, 3) == 4 &&
          tiku_editdoc_markers_on(doc, 4) == 0, "LMK-004",
          "PerLine/LineMarkers/InsertRemoveLine: insert a line");
    put(doc, start(doc, 2), start(doc, 3), "");
    check(tiku_editdoc_markers_on(doc, 1) == 2 &&
          tiku_editdoc_markers_on(doc, 2) == 4 &&
          tiku_editdoc_markers_on(doc, 3) == 0, "LMK-005",
          "PerLine/LineMarkers/InsertRemoveLine: remove it");
    put(doc, start(doc, 2), start(doc, 2), "\n\n");
    check(tiku_editdoc_markers_on(doc, 1) == 2 &&
          tiku_editdoc_markers_on(doc, 2) == 0 &&
          tiku_editdoc_markers_on(doc, 3) == 0 &&
          tiku_editdoc_markers_on(doc, 4) == 4 &&
          tiku_editdoc_marker_line(doc, h1) == 1 &&
          tiku_editdoc_marker_line(doc, h2) == 4, "LMK-004",
          "PerLine/LineMarkers/InsertRemoveLine: insert two");
    tiku_editdoc_release(doc);
}

/** @brief Random edits, undos and redos never lose a marker or leave one
 *         anywhere but at the start of a line the document has. */
static void
random_edits(void)
{
    static const char *const pieces[] = {
        "", "x", "\n", "a\nb", "\r\n", "\r", "yy\n\nzz", "\n\n\n"
    };
    char text[400] = "";
    tiku_editdoc_t *doc;
    tiku_edit_marker_t m[12];
    unsigned seed = 2024, kind[12];
    int i, k, ok = 1, found = 1;

    for (i = 0; i < 30; i++)
        sprintf(text + strlen(text), "line %d\n", i);
    doc = doc_of(text);
    for (k = 0; k < 12; k++) {
        kind[k] = (unsigned)k % 4;
        if (tiku_editdoc_marker_add(doc, (k * 7) % 30, kind[k], &m[k]) !=
            TIKU_EDIT_OK)
            exit(1);
    }
    for (i = 0; i < 3000; i++) {
        int64_t len = tiku_editdoc_length(doc), s, e;

        seed = seed * 1103515245u + 12345u;
        s = (int64_t)(seed >> 8) % (len + 1);
        seed = seed * 1103515245u + 12345u;
        e = s + (int64_t)(seed >> 8) % 20;
        if (e > len)
            e = len;
        while (!tiku_editdoc_is_boundary(doc, s))
            s--;
        while (!tiku_editdoc_is_boundary(doc, e))
            e--;
        if (e < s)
            e = s;
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 9) % 9 == 0)
            (void)tiku_editdoc_undo(doc);
        else if ((seed >> 9) % 9 == 1)
            (void)tiku_editdoc_redo(doc);
        else
            (void)put(doc, s, e, pieces[(seed >> 13) % 8]);
        for (k = 0; k < 12; k++) {
            int64_t line = tiku_editdoc_marker_line(doc, m[k]);

            if (line < 0 || line >= tiku_editdoc_lines(doc) ||
                !(tiku_editdoc_markers_on(doc, line) & (1u << kind[k])))
                ok = 0;
            if (tiku_editdoc_markers_next(doc, 0, 1u << kind[k]) > line)
                found = 0;
        }
    }
    check(ok, "LMK-006", "after each of 3000 random edits, undos and "
          "redos every marker is on a line it is reported on");
    check(found, "LMK-002", "and the first marked line of each kind is "
          "never past a marker of that kind");
    tiku_editdoc_release(doc);
}

/* An allocator that refuses while refuse is set. */
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

/** @brief A load removes every marker; a table that cannot grow refuses
 *         an add and keeps what it had. */
static void
loads_and_memory(void)
{
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    tiku_editdoc_t *doc = doc_of("a\nb\nc\nd\ne\n");
    tiku_edit_marker_t m[5], extra;
    int i, ok = 1;

    for (i = 0; i < 4; i++)
        m[i] = mark(doc, i);
    tiku_ed_test_allocator(&failing);
    refuse = 1;
    check(tiku_editdoc_marker_add(doc, 4, 2, &extra) == TIKU_EDIT_NO_MEMORY &&
          tiku_editdoc_markers_on(doc, 4) == 0, "LMK-007",
          "an add the table cannot grow for is refused and adds nothing");
    refuse = 0;
    tiku_ed_test_allocator(NULL);
    for (i = 0; i < 4; i++)
        ok &= tiku_editdoc_marker_line(doc, m[i]) == i;
    check(ok, "LMK-007", "and the markers it had are kept");
    m[4] = mark(doc, 4);
    check(tiku_editdoc_load(doc, "new\ntext\n", 9) == TIKU_EDIT_OK &&
          tiku_editdoc_markers_next(doc, 0, ~0u) == -1 &&
          tiku_editdoc_marker_line(doc, m[0]) == -1 &&
          tiku_editdoc_marker_line(doc, m[4]) == -1, "LMK-007",
          "a load removes every marker");
    tiku_editdoc_release(doc);
}

/** @brief The execution line: set for the text as it is, stale once it
 *         changes, current again when undo brings that text back. */
static void
execution(void)
{
    tiku_editdoc_t *doc = doc_of("10 A=1\n20 B=2\n30 END\n");
    int64_t line = 7;

    check(tiku_editdoc_execution(doc, &line) == TIKU_EDIT_NOTHING &&
          line == -1, "EXE-001", "a new document has none");
    check(tiku_editdoc_execution_set(doc, 4) == TIKU_EDIT_INVALID_RANGE &&
          tiku_editdoc_execution_set(doc, -1) == TIKU_EDIT_INVALID_RANGE &&
          tiku_editdoc_execution(doc, &line) == TIKU_EDIT_NOTHING,
          "EXE-001", "a line outside the text is refused");
    check(tiku_editdoc_execution_set(doc, 1) == TIKU_EDIT_OK &&
          tiku_editdoc_execution(doc, &line) == TIKU_EDIT_OK && line == 1,
          "EXE-001", "a line set is current");
    put(doc, 0, 0, "5 REM\n");
    check(tiku_editdoc_execution(doc, &line) == TIKU_EDIT_STALE &&
          line == 1, "EXE-002",
          "an edit makes it stale and does not move it to the text it was "
          "on");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_execution(doc, &line) == TIKU_EDIT_OK && line == 1,
          "EXE-002", "undo back to that text makes it current again");
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_execution(doc, &line) == TIKU_EDIT_STALE, "EXE-002",
          "and redo stale again");
    tiku_editdoc_undo(doc);
    tiku_editdoc_set_read_only(doc, 1);
    check(tiku_editdoc_execution_set(doc, 2) == TIKU_EDIT_OK &&
          tiku_editdoc_execution(doc, &line) == TIKU_EDIT_OK && line == 2,
          "EXE-003", "a read-only document takes the mark");
    tiku_editdoc_set_read_only(doc, 0);
    tiku_editdoc_execution_clear(doc);
    check(tiku_editdoc_execution(doc, &line) == TIKU_EDIT_NOTHING,
          "EXE-003", "clearing removes it");
    tiku_editdoc_execution_set(doc, 0);
    check(tiku_editdoc_load(doc, "10 A=1\n20 B=2\n30 END\n", 21) ==
              TIKU_EDIT_OK &&
          tiku_editdoc_execution(doc, &line) == TIKU_EDIT_STALE, "EXE-002",
          "a load makes it stale, even of the same text");
    check(tiku_editdoc_execution(NULL, &line) == TIKU_EDIT_INVALID &&
          tiku_editdoc_execution(doc, NULL) == TIKU_EDIT_INVALID, "EXE-001",
          "no document or no place for the line is refused");
    tiku_editdoc_release(doc);
}

int
main(void)
{
    handles();
    inserts();
    removals();
    history();
    upstream_line_markers();
    random_edits();
    loads_and_memory();
    execution();
    printf("%s\n", fails ? "FAIL: editdoc decorations" :
           "PASS: editdoc decorations -- line markers through edits, "
           "undo and loads; the execution line made stale, not moved");
    return fails != 0;
}
