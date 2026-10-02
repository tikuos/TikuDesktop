/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_lines.c - edits a code view makes a line at a time: a new
 * line keeping its indentation and opening a level after an opener, a
 * closer taking its opener's indentation, indenting and unindenting,
 * comments in C and BASIC, copying, moving and deleting lines, deleting
 * words, the keys for each, and the bracket by the caret with its match.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
#include "tiku_ui.h"

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

#define W 360
#define H 200
#define PAPER TIKU_RGB(255, 0, 255)

static const tiku_rect_t box = { 10, 10, 320, 160 };

/** @brief A view of a new document holding @p s, in @p lang, in the box;
 *         the view holds the only reference. */
static tiku_codeview_t *
view_in(const char *s, tiku_syntax_lang_t lang)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_language(v, lang);
    return v;
}

/** @brief Whether the view's document holds exactly @p s. */
static int
holds(tiku_codeview_t *v, const char *s)
{
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };
    char got[512];
    size_t n = 0;

    return tiku_editdoc_read(doc, all, got, sizeof got, &n) ==
               TIKU_EDIT_OK && n == strlen(s) && memcmp(got, s, n) == 0;
}

/** @brief Whether the selection is @p anchor to @p caret. */
static int
is(tiku_codeview_t *v, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == anchor && c == caret;
}

/** @brief Press key @p k with @p mods, bringing @p text; what the view
 *         answered. */
static int
key(tiku_codeview_t *v, unsigned k, unsigned mods, const char *text)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = k;
    e.modifiers = mods;
    if (text != NULL)
        memcpy(e.text, text, strnlen(text, sizeof e.text - 1));
    return tiku_codeview_event(v, &e);
}

/** @brief Type the printable key @p c. */
static void
type(tiku_codeview_t *v, char c)
{
    char t[2] = { c, '\0' };

    (void)key(v, (unsigned char)c, 0, t);
}

/** @brief Select from @p anchor to @p caret. */
static void
sel(tiku_codeview_t *v, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    (void)tiku_codeview_select(v, anchor, caret);
}

/** @brief Return copies the line's indentation up to the caret, joins
 *         the typing before it, replaces a selection and types the view's
 *         line break. */
static void
newlines(void)
{
    tiku_codeview_t *v = view_in("\tint x;", TIKU_SYNTAX_NONE);

    sel(v, 7, 7);
    check(key(v, TIKU_KEY_RETURN, 0, NULL) == 1 &&
          holds(v, "\tint x;\n\t") && is(v, 9, 9), "LNC-001",
          "Return keeps the indentation and puts the caret after it");
    tiku_codeview_destroy(v);

    v = view_in("ab", TIKU_SYNTAX_NONE);
    sel(v, 2, 2);
    type(v, 'c');
    (void)key(v, TIKU_KEY_RETURN, 0, NULL);
    type(v, 'd');
    check(holds(v, "abc\nd") &&
          tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, "ab"),
          "LNC-001", "typing, Return and typing are one undo step");
    tiku_codeview_destroy(v);

    v = view_in("\t\tfoo", TIKU_SYNTAX_NONE);
    sel(v, 1, 1);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "\t\n\t\tfoo") && is(v, 3, 3), "LNC-001",
          "a caret inside the indentation keeps only what is before it");
    tiku_codeview_destroy(v);

    v = view_in("  ab", TIKU_SYNTAX_NONE);
    sel(v, 3, 4);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK && holds(v, "  a\n  ") &&
          is(v, 6, 6), "LNC-001", "Return replaces a selection");
    tiku_codeview_destroy(v);

    v = view_in("\tx", TIKU_SYNTAX_NONE);
    (void)tiku_codeview_set_newline(v, "\r\n");
    sel(v, 2, 2);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "\tx\r\n\t") && is(v, 5, 5), "LNC-001",
          "the view's CRLF goes before the indentation");
    tiku_codeview_destroy(v);
}

/** @brief After a line whose C code ends with "{" the new line opens a
 *         level; a remark after it is passed over, a "{" in quotes is
 *         not an opener, a closer after the caret goes to its own line,
 *         and BASIC and prose only keep the indentation. */
static void
levels(void)
{
    tiku_codeview_t *v = view_in("\tif (x) {", TIKU_SYNTAX_C);

    sel(v, 9, 9);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "\tif (x) {\n\t\t") && is(v, 12, 12), "LNC-002",
          "a C line ending with { opens a level");
    tiku_codeview_destroy(v);

    v = view_in("if (x) { // go", TIKU_SYNTAX_C);
    sel(v, 14, 14);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "if (x) { // go\n\t"), "LNC-002",
          "a remark after the opener is passed over");
    tiku_codeview_destroy(v);

    v = view_in("s = \"{\";", TIKU_SYNTAX_C);
    sel(v, 8, 8);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "s = \"{\";\n"), "LNC-002",
          "a { in quotes opens nothing");
    tiku_codeview_destroy(v);

    v = view_in("s = \"{\"", TIKU_SYNTAX_C);
    sel(v, 7, 7);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "s = \"{\"\n"), "LNC-002",
          "a line ending in quotes holding { opens nothing");
    tiku_codeview_destroy(v);

    v = view_in("s = \"{", TIKU_SYNTAX_C);
    sel(v, 6, 6);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "s = \"{\n"), "LNC-002",
          "a { that ends a line inside open quotes opens nothing");
    tiku_codeview_destroy(v);

    v = view_in("f() {}", TIKU_SYNTAX_C);
    sel(v, 5, 5);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "f() {\n\t\n}") && is(v, 7, 7), "LNC-002",
          "between { and } the closer goes to its own line");
    tiku_codeview_destroy(v);

    v = view_in("\tf() {  }", TIKU_SYNTAX_C);
    sel(v, 6, 6);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "\tf() {\n\t\t\n\t}") && is(v, 9, 9), "LNC-002",
          "blanks before that closer go, and it keeps the old indentation");
    tiku_codeview_destroy(v);

    v = view_in("x {", TIKU_SYNTAX_C);
    (void)tiku_codeview_set_indent(v, 4);
    sel(v, 3, 3);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "x {\n    "), "LNC-002", "a level of four spaces");
    tiku_codeview_destroy(v);

    v = view_in("  10 FOR I = 1 TO 3 {", TIKU_SYNTAX_BASIC);
    sel(v, 21, 21);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "  10 FOR I = 1 TO 3 {\n  "), "LNC-002",
          "BASIC keeps the indentation and opens no level");
    tiku_codeview_destroy(v);

    v = view_in("  note {", TIKU_SYNTAX_NONE);
    sel(v, 8, 8);
    check(tiku_codeview_newline(v) == TIKU_EDIT_OK &&
          holds(v, "  note {\n  "), "LNC-002", "prose opens no level");
    tiku_codeview_destroy(v);
}

/** @brief A closer typed first on its line takes the indentation of the
 *         line holding its opener; elsewhere it is typed as it is. */
static void
closers(void)
{
    tiku_codeview_t *v = view_in("if (x) {\n\t\t", TIKU_SYNTAX_C);

    sel(v, 11, 11);
    type(v, '}');
    check(holds(v, "if (x) {\n}") && is(v, 10, 10), "LNC-003",
          "} takes the indentation of its opener's line");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK &&
          holds(v, "if (x) {\n\t\t"), "LNC-003", "and undo puts it back");
    tiku_codeview_destroy(v);

    v = view_in("\tif (a) {\n\t\tif (b) {\n\t\t\t", TIKU_SYNTAX_C);
    sel(v, 24, 24);
    type(v, '}');
    check(holds(v, "\tif (a) {\n\t\tif (b) {\n\t\t}"), "LNC-003",
          "the innermost open { decides");
    tiku_codeview_destroy(v);

    v = view_in("{\n\t\tx }\n\t\t", TIKU_SYNTAX_C);
    sel(v, 10, 10);
    type(v, '}');
    check(holds(v, "{\n\t\tx }\n\t\t}"), "LNC-003",
          "a { already closed is passed over, so nothing changes here");
    tiku_codeview_destroy(v);

    v = view_in("{\n\tx", TIKU_SYNTAX_C);
    sel(v, 4, 4);
    type(v, '}');
    check(holds(v, "{\n\tx}"), "LNC-003",
          "a closer after text is typed, open { or not");
    tiku_codeview_destroy(v);

    v = view_in("\t\t", TIKU_SYNTAX_C);
    sel(v, 2, 2);
    type(v, '}');
    check(holds(v, "\t\t}"), "LNC-003", "with no opener it is typed");
    tiku_codeview_destroy(v);

    v = view_in("{\n\t", TIKU_SYNTAX_NONE);
    sel(v, 3, 3);
    type(v, '}');
    check(holds(v, "{\n\t}"), "LNC-003", "prose has no closers");
    tiku_codeview_destroy(v);
}

/** @brief Tab over lines indents them, skipping empty ones, and selects
 *         them whole; Shift+Tab takes a tab or up to a level of spaces off
 *         each; Tab at a caret types a level. */
static void
indenting(void)
{
    tiku_codeview_t *v = view_in("a\nb\n\nc", TIKU_SYNTAX_NONE);

    sel(v, 0, 5);
    check(key(v, TIKU_KEY_TAB, 0, NULL) == 1 &&
          holds(v, "\ta\n\tb\n\nc") && is(v, 0, 7), "LNC-004",
          "Tab over lines indents them; the empty line and the line the "
          "selection ends at the start of are left");
    check(key(v, TIKU_KEY_TAB, TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "a\nb\n\nc") && is(v, 0, 5), "LNC-005",
          "Shift+Tab takes the level off again");
    sel(v, 5, 0);
    check(tiku_codeview_indent(v, 0) == TIKU_EDIT_OK && is(v, 7, 0),
          "LNC-004", "a backward selection stays backward");
    tiku_codeview_destroy(v);

    v = view_in("      x", TIKU_SYNTAX_NONE);
    sel(v, 6, 6);
    check(key(v, TIKU_KEY_TAB, TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "  x") && is(v, 2, 2), "LNC-005",
          "Shift+Tab takes a tab stop's worth of spaces");
    check(tiku_codeview_indent(v, 1) == TIKU_EDIT_OK && holds(v, "x") &&
          is(v, 0, 0), "LNC-005", "and fewer when fewer are there");
    check(tiku_codeview_indent(v, 1) == TIKU_EDIT_NOTHING &&
          key(v, TIKU_KEY_TAB, TIKU_MOD_SHIFT, NULL) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_OK && holds(v, "x"),
          "LNC-005", "with no indentation nothing changes");
    tiku_codeview_destroy(v);

    v = view_in("\t  x", TIKU_SYNTAX_NONE);
    check(tiku_codeview_indent(v, 1) == TIKU_EDIT_OK && holds(v, "  x"),
          "LNC-005", "a leading tab goes alone");
    tiku_codeview_destroy(v);

    v = view_in("ab", TIKU_SYNTAX_NONE);
    sel(v, 1, 1);
    check(key(v, TIKU_KEY_TAB, 0, NULL) == 1 && holds(v, "a\tb") &&
          is(v, 2, 2), "LNC-004", "Tab at a caret types a tab");
    (void)tiku_codeview_set_indent(v, 2);
    check(key(v, TIKU_KEY_TAB, 0, NULL) == 1 && holds(v, "a\t  b"),
          "LNC-004", "or a level of spaces");
    sel(v, 0, 0);
    check(tiku_codeview_indent(v, 0) == TIKU_EDIT_OK &&
          holds(v, "  a\t  b") && is(v, 2, 2), "LNC-004",
          "Indent at a caret indents its line and moves it along");
    check(tiku_codeview_indent(v, 1) == TIKU_EDIT_OK &&
          holds(v, "a\t  b"), "LNC-005", "two spaces are a level");
    check(tiku_codeview_set_indent(v, 33) == TIKU_EDIT_LIMIT &&
          tiku_codeview_set_indent(v, -1) == TIKU_EDIT_LIMIT &&
          tiku_codeview_indent_spaces(v) == 2, "LNC-004",
          "a level past 32 spaces is refused");
    tiku_codeview_destroy(v);
}

/** @brief Comments in C: added at the least indentation of the lines
 *         holding text, removed with one space after them when all hold
 *         one, and added to all when only some do. */
static void
comments_c(void)
{
    tiku_codeview_t *v = view_in("\tint a;\n\n\t\tint b;", TIKU_SYNTAX_C);

    sel(v, 0, 16);
    check(key(v, '/', TIKU_MOD_CMD, NULL) == 1 &&
          holds(v, "\t// int a;\n\n\t// \tint b;"), "LNC-006",
          "comments go at the least indentation, blank lines left");
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "\tint a;\n\n\t\tint b;"), "LNC-006",
          "and come off with one space after them");
    tiku_codeview_destroy(v);

    v = view_in("// a\nb", TIKU_SYNTAX_C);
    sel(v, 0, 6);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "// // a\n// b"), "LNC-006",
          "when some lines are not commented every line is");
    tiku_codeview_destroy(v);

    v = view_in("//x", TIKU_SYNTAX_C);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK && holds(v, "x"),
          "LNC-006", "a comment with no space after it comes off");
    tiku_codeview_destroy(v);

    v = view_in("int a;", TIKU_SYNTAX_C);
    sel(v, 4, 4);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "// int a;") && is(v, 7, 7), "LNC-006",
          "the caret stays on its text");
    tiku_codeview_destroy(v);

    v = view_in("\n  \n", TIKU_SYNTAX_C);
    sel(v, 0, 4);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_NOTHING &&
          holds(v, "\n  \n"), "LNC-006", "blank lines alone are left");
    tiku_codeview_destroy(v);
}

/** @brief Comments in BASIC go after a line's number as REM; REM in any
 *         case and an apostrophe come off; prose has no comments. */
static void
comments_basic(void)
{
    tiku_codeview_t *v = view_in("10 PRINT 1\n  20 X = 1\n30",
                                 TIKU_SYNTAX_BASIC);

    sel(v, 0, 24);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "10 REM PRINT 1\n  20 REM X = 1\n30"), "LNC-007",
          "REM goes after each line's number; a bare number is left");
    sel(v, 0, 32);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "10 PRINT 1\n  20 X = 1\n30"), "LNC-007",
          "and comes off");
    tiku_codeview_destroy(v);

    v = view_in("10 ' hi\n20 rem x", TIKU_SYNTAX_BASIC);
    sel(v, 0, 16);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "10 hi\n20 x"), "LNC-007",
          "an apostrophe and a lower-case rem come off");
    tiku_codeview_destroy(v);

    v = view_in("PRINT 1", TIKU_SYNTAX_BASIC);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_OK &&
          holds(v, "REM PRINT 1"), "LNC-007",
          "a line without a number takes REM at its start");
    tiku_codeview_destroy(v);

    v = view_in("plain", TIKU_SYNTAX_NONE);
    check(tiku_codeview_toggle_comment(v) == TIKU_EDIT_NOTHING &&
          key(v, '/', TIKU_MOD_CMD, NULL) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_OK && holds(v, "plain"),
          "LNC-007", "prose has no comments");
    tiku_codeview_destroy(v);
}

/** @brief Duplicating copies the touched lines below them and selects
 *         the copy; the last line gets the view's line break first. */
static void
duplicating(void)
{
    tiku_codeview_t *v = view_in("a\nb\nc", TIKU_SYNTAX_NONE);

    sel(v, 3, 3);
    check(key(v, 'd', TIKU_MOD_CMD, NULL) == 1 && holds(v, "a\nb\nb\nc") &&
          is(v, 5, 5), "LNC-008", "the caret's line is copied below it");
    sel(v, 0, 3);
    check(tiku_codeview_duplicate_lines(v) == TIKU_EDIT_OK &&
          holds(v, "a\nb\na\nb\nb\nc") && is(v, 4, 7), "LNC-008",
          "a selection's lines are copied, and the copy selected");
    tiku_codeview_destroy(v);

    v = view_in("a\r\nb", TIKU_SYNTAX_NONE);
    (void)tiku_codeview_set_newline(v, "\r\n");
    sel(v, 4, 4);
    check(tiku_codeview_duplicate_lines(v) == TIKU_EDIT_OK &&
          holds(v, "a\r\nb\r\nb") && is(v, 7, 7), "LNC-008",
          "the last line is copied after the view's line break");
    tiku_codeview_destroy(v);
}

/** @brief Moving swaps the touched lines with the line above or below;
 *         the last line keeps no break, the selection moves with the
 *         text, and undo puts the lines back. */
static void
moving(void)
{
    tiku_codeview_t *v = view_in("a\nb\nc", TIKU_SYNTAX_NONE);

    sel(v, 2, 2);
    check(key(v, TIKU_KEY_DOWN, TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "a\nc\nb") && is(v, 4, 4), "LNC-009",
          "down past the last line: its break goes between the two");
    check(key(v, TIKU_KEY_UP, TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "a\nb\nc") && is(v, 2, 2), "LNC-009",
          "up from the last line puts it back");
    sel(v, 0, 0);
    check(tiku_codeview_move_lines(v, 0) == TIKU_EDIT_NOTHING &&
          key(v, TIKU_KEY_UP, TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "a\nb\nc"), "LNC-009", "the first line goes no higher");
    sel(v, 4, 4);
    check(tiku_codeview_move_lines(v, 1) == TIKU_EDIT_NOTHING,
          "LNC-009", "the last line goes no lower");
    tiku_codeview_destroy(v);

    v = view_in("1\n2\n3\n4", TIKU_SYNTAX_NONE);
    sel(v, 2, 6);
    check(tiku_codeview_move_lines(v, 0) == TIKU_EDIT_OK &&
          holds(v, "2\n3\n1\n4") && is(v, 0, 4), "LNC-009",
          "two lines move up together, selected");
    check(tiku_codeview_move_lines(v, 1) == TIKU_EDIT_OK &&
          tiku_codeview_move_lines(v, 1) == TIKU_EDIT_OK &&
          holds(v, "1\n4\n2\n3") && is(v, 4, 7), "LNC-009",
          "and down to the end, the selection at the moved text's end");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK &&
          tiku_codeview_undo(v) == TIKU_EDIT_OK &&
          tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, "1\n2\n3\n4"),
          "LNC-009", "each move is one undo step");
    tiku_codeview_destroy(v);

    v = view_in("a\r\nb", TIKU_SYNTAX_NONE);
    sel(v, 3, 3);
    check(tiku_codeview_move_lines(v, 0) == TIKU_EDIT_OK &&
          holds(v, "b\r\na"), "LNC-009", "a CRLF moves as one break");
    tiku_codeview_destroy(v);
}

/** @brief Deleting lines takes them with their breaks, or the last ones
 *         with the break before them. */
static void
deleting(void)
{
    tiku_codeview_t *v = view_in("a\nb\nc", TIKU_SYNTAX_NONE);

    sel(v, 2, 2);
    check(key(v, 'k', TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "a\nc") && is(v, 2, 2), "LNC-010",
          "a line goes with its break");
    sel(v, 3, 3);
    check(tiku_codeview_delete_lines(v) == TIKU_EDIT_OK && holds(v, "a") &&
          is(v, 1, 1), "LNC-010", "the last line goes with the break "
          "before it");
    check(tiku_codeview_delete_lines(v) == TIKU_EDIT_OK && holds(v, "") &&
          tiku_codeview_delete_lines(v) == TIKU_EDIT_NOTHING, "LNC-010",
          "the only line empties the document, then nothing is left");
    check(key(v, 'k', TIKU_MOD_CMD, NULL) == 0, "LNC-010",
          "Cmd+K without Shift is not taken");
    tiku_codeview_destroy(v);
}

/** @brief Cmd+Backspace and Cmd+Delete delete to where a word move goes,
 *         each press an undo step of its own. */
static void
words(void)
{
    tiku_codeview_t *v = view_in("foo bar", TIKU_SYNTAX_NONE);

    sel(v, 7, 7);
    check(key(v, TIKU_KEY_BACKSPACE, TIKU_MOD_CMD, NULL) == 1 &&
          holds(v, "foo ") && is(v, 4, 4), "LNC-011",
          "back over a word");
    check(key(v, TIKU_KEY_BACKSPACE, TIKU_MOD_CMD, NULL) == 1 &&
          holds(v, ""), "LNC-011", "and over blanks and the next word");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, "foo ") &&
          tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, "foo bar"),
          "LNC-011", "each press is an undo step");
    sel(v, 0, 0);
    check(key(v, TIKU_KEY_DELETE, TIKU_MOD_CMD, NULL) == 1 &&
          holds(v, "bar"), "LNC-011", "on over a word and its blanks");
    sel(v, 0, 2);
    check(tiku_codeview_delete_word(v, 0) == TIKU_EDIT_OK &&
          holds(v, "r"), "LNC-011", "a selection is deleted instead");
    sel(v, 0, 0);
    check(tiku_codeview_delete_word(v, 0) == TIKU_EDIT_NOTHING,
          "LNC-011", "nothing before the caret: nothing");
    check(key(v, TIKU_KEY_DELETE, TIKU_MOD_CMD | TIKU_MOD_OPTION, NULL) ==
              0 && holds(v, "r"), "LNC-011",
          "Cmd+Option+Delete is not the view's");
    tiku_codeview_destroy(v);
}

/** @brief A document that refuses changes refuses every line command. */
static void
read_only(void)
{
    tiku_codeview_t *v = view_in("{\n\tx\n}", TIKU_SYNTAX_C);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);

    tiku_editdoc_set_read_only(doc, 1);
    sel(v, 3, 3);
    check(tiku_codeview_newline(v) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_indent(v, 0) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_indent(v, 1) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_toggle_comment(v) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_duplicate_lines(v) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_move_lines(v, 0) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_delete_lines(v) == TIKU_EDIT_READ_ONLY &&
          tiku_codeview_delete_word(v, 0) == TIKU_EDIT_READ_ONLY &&
          holds(v, "{\n\tx\n}"), "LNC-012", "every command is refused");
    tiku_codeview_destroy(v);
}

/** @brief The bracket before the caret, else after it, and its match by
 *         kind, nesting counted; quotes and remarks are passed over. */
static void
brackets(void)
{
    tiku_codeview_t *v = view_in("f(a[1], (b))", TIKU_SYNTAX_NONE);
    tiku_edit_pos_t m = 0;

    sel(v, 2, 2);
    check(tiku_codeview_bracket(v, &m) == 1 && m == 11, "BRC-001",
          "the opener before the caret matches its closer");
    sel(v, 1, 1);
    check(tiku_codeview_bracket(v, &m) == 1 && m == 11, "BRC-001",
          "the opener after the caret when none is before it");
    sel(v, 6, 6);
    check(tiku_codeview_bracket(v, &m) == 5 && m == 3, "BRC-001",
          "a closer matches back to its opener");
    sel(v, 12, 12);
    check(tiku_codeview_bracket(v, &m) == 11 && m == 1, "BRC-001",
          "nesting is counted");
    sel(v, 0, 2);
    check(tiku_codeview_bracket(v, &m) == -1 && m == -1, "BRC-003",
          "a selection has none");
    tiku_codeview_destroy(v);

    v = view_in("(a)(b)", TIKU_SYNTAX_NONE);
    sel(v, 3, 3);
    check(tiku_codeview_bracket(v, &m) == 2 && m == 0, "BRC-001",
          "with brackets on both sides, the one before the caret");
    tiku_codeview_destroy(v);

    v = view_in("(]", TIKU_SYNTAX_NONE);
    sel(v, 1, 1);
    check(tiku_codeview_bracket(v, &m) == 0 && m == -1, "BRC-003",
          "another kind does not pair");
    tiku_codeview_destroy(v);

    v = view_in("(\")\" /* ) */ )", TIKU_SYNTAX_C);
    sel(v, 0, 0);
    check(tiku_codeview_bracket(v, &m) == 0 && m == 13, "BRC-002",
          "C quotes and remarks are passed over");
    sel(v, 3, 3);
    check(tiku_codeview_bracket(v, &m) == -1, "BRC-002",
          "a bracket in quotes is none");
    tiku_codeview_destroy(v);

    v = view_in("10 PRINT (\"(\") : REM (", TIKU_SYNTAX_BASIC);
    sel(v, 10, 10);
    check(tiku_codeview_bracket(v, &m) == 9 && m == 13, "BRC-002",
          "BASIC quotes are passed over");
    sel(v, 22, 22);
    check(tiku_codeview_bracket(v, &m) == -1, "BRC-002",
          "a bracket in a remark is none");
    tiku_codeview_destroy(v);

    v = view_in("(a)", TIKU_SYNTAX_NONE);
    sel(v, 3, 3);
    check(tiku_codeview_bracket(v, &m) == 2 && m == 0, "BRC-001",
          "after the closer, its opener");
    (void)tiku_codeview_insert(v, ")", 1, 0);
    check(tiku_codeview_bracket(v, &m) == 3 && m == -1, "BRC-003",
          "an edit finds the match again: the new ) has none");
    tiku_codeview_destroy(v);

    v = view_in("(a)", TIKU_SYNTAX_NONE);
    sel(v, 1, 1);
    check(tiku_codeview_bracket(v, &m) == 0 && m == 2, "BRC-001",
          "the pair before an edit behind the caret");
    {
        tiku_edit_replace_t cut = { { 2, 3 }, NULL, 0 };
        tiku_edit_transaction_t t;

        memset(&t, 0, sizeof t);
        t.edits = &cut;
        t.count = 1;
        (void)tiku_editdoc_apply(tiku_codeview_doc(v), &t);
    }
    check(is(v, 1, 1) && tiku_codeview_bracket(v, &m) == 0 && m == -1,
          "BRC-003", "an edit that leaves the caret where it was finds "
          "the match again");
    tiku_codeview_destroy(v);

    {
        size_t n = (size_t)TIKU_CODEVIEW_BRACE_SCAN + 16;
        char *big = malloc(n + 2);
        tiku_editdoc_t *doc = NULL;

        if (big == NULL)
            exit(1);
        big[0] = '(';
        memset(big + 1, 'x', n);
        big[n + 1] = '\0';
        if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
            tiku_editdoc_load(doc, big, n + 1) != TIKU_EDIT_OK ||
            tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
            exit(1);
        tiku_editdoc_release(doc);
        free(big);
        sel(v, 1, 1);
        check(tiku_codeview_bracket(v, &m) == 0 && m == -2, "BRC-003",
              "the search gives up past its bound");
        tiku_codeview_destroy(v);
    }
}

/** @brief The colour bits of the pixel at @p x, @p y. */
static tiku_rgb_t
at(const tiku_surface_t *s, int x, int y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief The ground painted under the bracket at @p pos: its cell's
 *         left column on its row's last pixel line, below any glyph. */
static tiku_rgb_t
ground_at(tiku_codeview_t *v, tiku_surface_t *s, tiku_edit_pos_t pos)
{
    int64_t x = 0, y = 0;

    tiku_codeview_point(v, pos, &x, &y);
    return at(s, (int)x, (int)y + tiku_codeview_row_height(v) - 1);
}

/** @brief With the bracket option both brackets of a pair are filled
 *         behind the text, an unmatched one in the wrong colour; without
 *         it nothing is. */
static void
bracket_paint(void)
{
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);
    tiku_codeview_t *v = view_in("x(ab)y", TIKU_SYNTAX_NONE);
    tiku_rgb_t pair = tiku_dim(TIKU_C_SELECT, TIKU_C_DOC) & 0xffffffu;
    tiku_rgb_t wrong = tiku_dim(tiku_ink(TIKU_INK_WRONG), TIKU_C_DOC) &
                       0xffffffu;
    tiku_rgb_t doc = TIKU_C_DOC & 0xffffffu;

    if (s == NULL)
        exit(1);
    tiku_codeview_set_focus(v, 0);
    sel(v, 2, 2);
    tiku_codeview_paint(v, s);
    check(ground_at(v, s, 1) == doc && ground_at(v, s, 4) == doc,
          "BRC-004", "without the option nothing is marked");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_BRACES);
    tiku_codeview_paint(v, s);
    check(ground_at(v, s, 1) == pair && ground_at(v, s, 4) == pair &&
          ground_at(v, s, 2) == doc, "BRC-004",
          "both brackets of the pair are filled, nothing between");
    (void)tiku_codeview_insert(v, "(", 1, 0);
    tiku_codeview_paint(v, s);
    check(ground_at(v, s, 2) == pair && ground_at(v, s, 5) == pair &&
          ground_at(v, s, 1) == doc, "BRC-004",
          "after an edit the pair by the caret is the one marked");
    tiku_codeview_destroy(v);
    v = view_in("x(ab", TIKU_SYNTAX_NONE);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_BRACES);
    sel(v, 2, 2);
    tiku_codeview_paint(v, s);
    check(ground_at(v, s, 1) == wrong && ground_at(v, s, 2) == doc,
          "BRC-004", "an unmatched bracket is filled in the wrong colour");
    tiku_codeview_destroy(v);
    tiku_surface_free(s);
}

/** @brief Cmd+M puts the caret past the matching bracket, Cmd+Shift+M
 *         selects the pair. */
static void
goto_bracket(void)
{
    tiku_codeview_t *v = view_in("x(ab)y", TIKU_SYNTAX_NONE);

    sel(v, 2, 2);
    check(key(v, 'm', TIKU_MOD_CMD, NULL) == 1 && is(v, 5, 5), "BRC-005",
          "from an opener to past its closer");
    check(tiku_codeview_goto_bracket(v, 0) == TIKU_EDIT_OK && is(v, 1, 1),
          "BRC-005", "and back to before the opener");
    check(key(v, 'm', TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 1 &&
          is(v, 1, 5), "BRC-005", "Shift selects the pair");
    sel(v, 0, 0);
    check(tiku_codeview_goto_bracket(v, 0) == TIKU_EDIT_NOTHING &&
          is(v, 0, 0), "BRC-005", "no bracket by the caret: nothing");
    tiku_codeview_destroy(v);
}

/** @brief Keys the view leaves alone: Option chords of the new commands,
 *         Cmd+Shift+D, and Return with Shift. */
static void
keys_left(void)
{
    tiku_codeview_t *v = view_in("a\nb", TIKU_SYNTAX_C);

    sel(v, 0, 0);
    check(key(v, '/', TIKU_MOD_CMD | TIKU_MOD_OPTION, NULL) == 0 &&
          key(v, 'd', TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 0 &&
          key(v, TIKU_KEY_DOWN, TIKU_MOD_CMD, NULL) == 0 &&
          key(v, TIKU_KEY_RETURN, TIKU_MOD_SHIFT, NULL) == 0 &&
          holds(v, "a\nb"), "LNC-013",
          "chords the view has no command for are not taken");
    check(key(v, 'c', TIKU_MOD_CMD | TIKU_MOD_OPTION, NULL) == 1,
          "LNC-013", "Cmd+Option+C still copies as before");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    newlines();
    levels();
    closers();
    indenting();
    comments_c();
    comments_basic();
    duplicating();
    moving();
    deleting();
    words();
    read_only();
    brackets();
    bracket_paint();
    goto_bracket();
    keys_left();
    if (fails) {
        printf("FAIL: %d checks\n", fails);
        return 1;
    }
    printf("PASS: code view lines -- new lines, levels, closers, indenting,"
           " comments, duplicating, moving and deleting lines, words, "
           "brackets\n");
    return 0;
}
