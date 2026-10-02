/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_folds.c - regions told by C's braces, BASIC's blocks and
 * prose's indentation; folding and unfolding them in one view, nested
 * ones kept; the caret kept out of what is folded and what it goes to
 * shown; edits, two views, the fold column, the keys, and folds with
 * wrapped lines and annotations.
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
#define H 220
#define PAPER TIKU_RGB(255, 0, 255)
#define X0 15                   /* the text's left edge: 10 + 5     */
#define Y0 13                   /* the first row's top: 10 + 3      */

static const tiku_rect_t box = { 10, 10, 320, 200 };

static const char c_text[] =
    "int f(void) {\n"           /* 0 */
    "    if (x) {\n"            /* 1 */
    "        a();\n"            /* 2 */
    "    } else {\n"            /* 3 */
    "        b();\n"            /* 4 */
    "    }\n"                   /* 5 */
    "    s = \"{\"; // {\n"     /* 6 */
    "}\n"                       /* 7 */
    "int g;";                   /* 8 */

/** @brief A view of a new document holding @p s in @p lang; the view
 *         holds the only reference. */
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

/** @brief The colour bits of the pixel at @p x, @p y. */
static tiku_rgb_t
pixel(const tiku_surface_t *s, int x, int y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief Rows in all, as the scroll range counts them. */
static int64_t
rows_of(tiku_codeview_t *v)
{
    int64_t total = -1;

    tiku_codeview_extent(v, 0, &total, NULL, NULL);
    return total;
}

/** @brief The row line @p line's start is drawn on, from the first. */
static int64_t
row_of_line(tiku_codeview_t *v, int64_t line)
{
    int64_t y = 0;

    (void)tiku_codeview_point(v, tiku_editdoc_line_start(
                                  tiku_codeview_doc(v), line), NULL, &y);
    return (y - Y0) / tiku_codeview_row_height(v);
}

/** @brief The caret's line. */
static int64_t
caret_line(tiku_codeview_t *v)
{
    tiku_edit_pos_t a = 0, c = 0;

    (void)tiku_codeview_selection(v, &a, &c);
    return tiku_editdoc_line_of(tiku_codeview_doc(v), c);
}

/** @brief Put the caret at the start of @p line. */
static void
put_line(tiku_codeview_t *v, int64_t line)
{
    tiku_edit_pos_t p = tiku_editdoc_line_start(tiku_codeview_doc(v), line);

    (void)tiku_codeview_select(v, p, p);
}

/** @brief Replace [s, e) with @p text as one edit. */
static void
replace(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    (void)tiku_editdoc_apply(doc, &t);
}

/** @brief C's regions: a line leaving a brace open heads the lines up to
 *         the one before its closer's, braces in quotes and remarks do
 *         not count, and "} else {" ends one region and heads the next. */
static void
regions(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);

    check(tiku_codeview_fold_end(v, 0) == 6 &&
          tiku_codeview_fold_end(v, 1) == 2 &&
          tiku_codeview_fold_end(v, 3) == 4, "FLD-001",
          "a brace left open heads the lines before its closer's line");
    check(tiku_codeview_fold_end(v, 6) == -1 &&
          tiku_codeview_fold_end(v, 7) == -1 &&
          tiku_codeview_fold_end(v, 8) == -1 &&
          tiku_codeview_fold_end(v, 9) == -1, "FLD-001",
          "braces in quotes or remarks, a closer and plain code head none");
    tiku_codeview_destroy(v);
    v = view_in("x {\n  a\n  b", TIKU_SYNTAX_C);
    check(tiku_codeview_fold_end(v, 0) == -1, "FLD-001",
          "a brace never closed heads none");
    tiku_codeview_destroy(v);
    v = view_in("10 FOR I = 1 TO 3\n20   PRINT I\n30 NEXT I\n"
                "40 IF X THEN\n50   PRINT \"FOR\"\n60 ENDIF\n"
                "70 FOR J = 1 TO 2: PRINT J: NEXT J\n80 REM FOR\n"
                "90 WHILE A: A = A - 1\n100 WEND", TIKU_SYNTAX_BASIC);
    check(tiku_codeview_fold_end(v, 0) == 1 &&
          tiku_codeview_fold_end(v, 3) == 4, "FLD-002",
          "a BASIC block word starting a statement, or THEN ending a line, "
          "heads the lines before its closer");
    check(tiku_codeview_fold_end(v, 6) == -1 &&
          tiku_codeview_fold_end(v, 7) == -1 &&
          tiku_codeview_fold_end(v, 8) == -1, "FLD-002",
          "a block closed on its own line, a remark, or one with nothing "
          "between heads none");
    tiku_codeview_destroy(v);
    v = view_in("Heading\n  one\n\n  two\n\nNext\n  three",
                TIKU_SYNTAX_NONE);
    check(tiku_codeview_fold_end(v, 0) == 3 &&
          tiku_codeview_fold_end(v, 1) == -1 &&
          tiku_codeview_fold_end(v, 5) == 6, "FLD-003",
          "prose heads the lines indented further after it, blank lines "
          "at the end left out");
    tiku_codeview_destroy(v);
}

/** @brief Folding takes a region's lines out of the rows, the caret
 *         moving past them; unfolding shows them but a folded region's
 *         inside it. */
static void
folding(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);

    check(tiku_codeview_fold(v, 2, 0) == TIKU_EDIT_NOTHING &&
          tiku_codeview_fold(v, 99, 0) == TIKU_EDIT_INVALID_RANGE &&
          tiku_codeview_fold(v, 1, 1) == TIKU_EDIT_NOTHING, "FLD-004",
          "a line heading nothing, or not folded, has nothing to do");
    check(tiku_codeview_fold(v, 1, 0) == TIKU_EDIT_OK &&
          tiku_codeview_folded(v, 1) && !tiku_codeview_shown(v, 2) &&
          tiku_codeview_shown(v, 3) && rows_of(v) == 8 &&
          row_of_line(v, 3) == 2, "FLD-004",
          "a folded region's lines take no rows");
    check(tiku_codeview_fold(v, 1, 0) == TIKU_EDIT_NOTHING, "FLD-004",
          "folding it again does nothing");
    put_line(v, 1);
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_LINE_DOWN, 0);
    check(caret_line(v) == 3, "FLD-005",
          "Down from a folded line goes past what is folded");
    check(tiku_codeview_fold(v, 0, 0) == TIKU_EDIT_OK && rows_of(v) == 3 &&
          row_of_line(v, 7) == 1 && caret_line(v) == 0, "FLD-005",
          "folding over the caret puts it at the end of the folded line");
    check(tiku_codeview_fold(v, 0, 1) == TIKU_EDIT_OK && rows_of(v) == 8 &&
          !tiku_codeview_shown(v, 2) && tiku_codeview_folded(v, 1),
          "FLD-004", "unfolding shows the lines but a folded region's "
          "inside it");
    check(tiku_codeview_fold(v, 1, 1) == TIKU_EDIT_OK && rows_of(v) == 9,
          "FLD-004", "and unfolding that shows the rest");
    (void)tiku_codeview_fold(v, 1, 0);
    (void)tiku_codeview_fold(v, 0, 0);
    check(tiku_codeview_fold(v, 1, 1) == TIKU_EDIT_OK &&
          !tiku_codeview_folded(v, 1) && !tiku_codeview_shown(v, 2) &&
          rows_of(v) == 3, "FLD-004",
          "unfolding a line in a folded region only marks it unfolded");
    check(tiku_codeview_fold(v, 0, 1) == TIKU_EDIT_OK && rows_of(v) == 9,
          "FLD-004", "and its lines show with that region's");
    tiku_codeview_destroy(v);
}

/** @brief Whatever puts the caret in folded text unfolds it: a search,
 *         or a selection shown; until then it is not drawn. */
static void
revealing(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);
    tiku_rgb_t text = TIKU_C_TEXT & 0xffffffu;
    int step = tiku_codeview_row_height(v);
    tiku_edit_query_t q;

    if (s == NULL)
        exit(1);
    (void)tiku_codeview_fold(v, 0, 0);
    memset(&q, 0, sizeof q);
    q.text = "b()";
    q.length = 3;
    check(tiku_codeview_find(v, &q, 0, NULL) == TIKU_EDIT_OK &&
          caret_line(v) == 4 && tiku_codeview_shown(v, 4) &&
          !tiku_codeview_folded(v, 0), "FLD-006",
          "a search that finds folded text unfolds it");
    (void)tiku_codeview_fold(v, 1, 0);
    tiku_codeview_set_focus(v, 1);
    put_line(v, 3);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0, Y0 + 2 * step + 2) == text, "FLD-006",
          "a caret on a shown line is drawn on its row");
    put_line(v, 2);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0, Y0 + 2 * step + 2) != text, "FLD-006",
          "a caret put in folded lines is not drawn");
    tiku_codeview_reveal(v);
    check(tiku_codeview_shown(v, 2) && !tiku_codeview_folded(v, 1),
          "FLD-006", "and showing it unfolds them");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief Fold All folds every region, inner ones first, and Unfold All
 *         shows every line. */
static void
all(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);

    check(tiku_codeview_fold_all(v, 0) == TIKU_EDIT_OK && rows_of(v) == 3 &&
          tiku_codeview_folded(v, 0) && tiku_codeview_folded(v, 1) &&
          tiku_codeview_folded(v, 3), "FLD-007",
          "Fold All folds every region");
    check(tiku_codeview_fold_all(v, 1) == TIKU_EDIT_OK && rows_of(v) == 9 &&
          !tiku_codeview_folded(v, 1), "FLD-007",
          "Unfold All shows every line");
    check(tiku_codeview_fold_all(v, 1) == TIKU_EDIT_NOTHING, "FLD-007",
          "with nothing folded there is nothing to unfold");
    tiku_codeview_destroy(v);
}

/** @brief Put 12000 lines at the start of @p doc, more than a view counts
 *         the rows of as it lays out. */
static void
put_many(tiku_editdoc_t *doc)
{
    size_t n = 12000 * 32, i;
    char *text = malloc(n + 1);

    if (text == NULL)
        exit(1);
    for (i = 0; i < n; i++)
        text[i] = i % 32 == 31 ? '\n' : 'a';
    text[n] = '\0';
    replace(doc, 0, 0, text);
    free(text);
}

/** @brief An edit shows what it touched and the folded lines next to it;
 *         edits elsewhere move folds with their lines. */
static void
edits(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);

    (void)tiku_codeview_fold(v, 1, 0);
    replace(doc, 0, 0, "// top\n");
    check(tiku_codeview_folded(v, 2) && !tiku_codeview_shown(v, 3) &&
          !tiku_codeview_folded(v, 1) && rows_of(v) == 9, "FLD-008",
          "lines put in above move a fold with its lines");
    replace(doc, tiku_editdoc_line_start(doc, 2),
            tiku_editdoc_line_start(doc, 2), " ");
    check(!tiku_codeview_folded(v, 2) && tiku_codeview_shown(v, 3) &&
          rows_of(v) == 10, "FLD-008",
          "an edit to a folded line unfolds it");
    (void)tiku_codeview_fold(v, 2, 0);
    replace(doc, tiku_editdoc_line_start(doc, 3),
            tiku_editdoc_line_start(doc, 3) + 4, "");
    check(tiku_codeview_shown(v, 3) && !tiku_codeview_folded(v, 2) &&
          rows_of(v) == 10, "FLD-008",
          "an edit inside folded lines, from another view, shows them");
    (void)tiku_codeview_fold(v, 2, 0);
    (void)tiku_codeview_fold(v, 4, 0);
    replace(doc, tiku_editdoc_line_start(doc, 2),
            tiku_editdoc_line_start(doc, 2), " ");
    check(!tiku_codeview_folded(v, 2) && tiku_codeview_shown(v, 3) &&
          tiku_codeview_folded(v, 4) && !tiku_codeview_shown(v, 5) &&
          rows_of(v) == 9, "FLD-008",
          "an edit to one folded line leaves another folded");
    (void)tiku_codeview_fold(v, 4, 1);
    (void)tiku_codeview_fold(v, 1, 0);
    replace(doc, tiku_editdoc_line_start(doc, 5),
            tiku_editdoc_line_start(doc, 5), " ");
    check(tiku_codeview_shown(v, 2) && tiku_codeview_shown(v, 7) &&
          !tiku_codeview_folded(v, 1) && rows_of(v) == 10, "FLD-008",
          "an edit in the middle of folded lines shows them all");
    put_many(doc);
    check(!tiku_codeview_wrapping(v), "FLD-008",
          "with nothing folded, many lines put in leave no rows to count");
    tiku_codeview_destroy(v);
    v = view_in(c_text, TIKU_SYNTAX_C);
    doc = tiku_codeview_doc(v);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    (void)tiku_codeview_fold(v, 1, 0);
    replace(doc, tiku_editdoc_line_start(doc, 1),
            tiku_editdoc_line_start(doc, 1), " ");
    check(!tiku_codeview_folded(v, 1) && tiku_codeview_shown(v, 2) &&
          rows_of(v) == 9, "FLD-008",
          "so does an edit to the only folded line while lines wrap");
    tiku_codeview_destroy(v);
}

/** @brief Each view folds on its own. */
static void
two_views(void)
{
    tiku_codeview_t *a = view_in(c_text, TIKU_SYNTAX_C), *b = NULL;

    if (tiku_codeview_create(tiku_codeview_doc(a), &b) != TIKU_EDIT_OK)
        exit(1);
    tiku_codeview_set_bounds(b, box);
    tiku_codeview_set_language(b, TIKU_SYNTAX_C);
    (void)tiku_codeview_fold(a, 0, 0);
    check(rows_of(a) == 3 && rows_of(b) == 9 && tiku_codeview_shown(b, 2),
          "FLD-009", "a fold in one view leaves the other alone");
    tiku_codeview_destroy(b);
    tiku_codeview_destroy(a);
}

/** @brief The fold column: a box on each line heading a region, a plus
 *         in a folded one's, the mark after a folded line's text, and a
 *         press on a box folding or unfolding it. */
static void
column(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);
    tiku_rgb_t faint = tiku_ink(TIKU_INK_FAINT) & 0xffffffu;
    int step = tiku_codeview_row_height(v), bx = box.x + (12 - 9) / 2;
    int by = Y0 + (step - 9) / 2;
    int64_t x0 = 0, x1 = 0;
    tiku_event_t e;

    if (s == NULL)
        exit(1);
    tiku_codeview_set_focus(v, 0);
    (void)tiku_codeview_point(v, 0, &x0, NULL);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_FOLDS);
    (void)tiku_codeview_point(v, 0, &x1, NULL);
    check(x1 - x0 == 12 + 4 - 5, "FLD-010",
          "the fold column is 12 pixels at the gutter's edge");
    tiku_codeview_paint(v, s);
    check(pixel(s, bx, by + 4) == faint && pixel(s, bx + 4, by + 4) == faint &&
          pixel(s, bx + 4, by + 2) != faint &&
          pixel(s, bx, by + 2 * step + 4) != faint, "FLD-010",
          "a heading line has a box with a minus, other lines none");
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.button = 1;
    e.x = bx + 4;
    e.y = Y0 + step + step / 2;
    check(tiku_codeview_event(v, &e) == 1 && tiku_codeview_folded(v, 1),
          "FLD-010", "a press on the box folds the region");
    tiku_codeview_paint(v, s);
    {
        int64_t xe = 0;

        (void)tiku_codeview_point(v, tiku_editdoc_line_end(
                                      tiku_codeview_doc(v), 1), &xe, NULL);
        check(pixel(s, bx + 4, by + step + 2) == faint &&
              pixel(s, (int)xe + 4, Y0 + step + step / 2) == faint,
              "FLD-010", "a folded line's box has a plus, and a mark "
              "follows its text");
    }
    check(tiku_codeview_event(v, &e) == 1 && !tiku_codeview_folded(v, 1),
          "FLD-010", "and another press unfolds it");
    e.y = Y0 + 2 * step + step / 2;
    check(tiku_codeview_event(v, &e) == 1 && caret_line(v) == 0,
          "FLD-010", "a press on a line heading nothing is taken and does "
          "nothing");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief Command with Shift and [ folds the region the caret is in,
 *         with ] unfolds the caret's line; without Shift they are left. */
static void
keys(void)
{
    tiku_codeview_t *v = view_in(c_text, TIKU_SYNTAX_C);
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.modifiers = TIKU_MOD_CMD;
    e.key = '[';
    put_line(v, 2);
    check(tiku_codeview_event(v, &e) == 0 && !tiku_codeview_folded(v, 1),
          "FLD-011", "Command with [ is left to the application");
    e.modifiers |= TIKU_MOD_SHIFT;
    check(tiku_codeview_event(v, &e) == 1 && tiku_codeview_folded(v, 1) &&
          caret_line(v) == 1, "FLD-011",
          "with Shift it folds the innermost region the caret is in");
    e.key = ']';
    check(tiku_codeview_event(v, &e) == 1 && !tiku_codeview_folded(v, 1),
          "FLD-011", "and ] unfolds the caret's line");
    tiku_codeview_destroy(v);
}

/** @brief Folded lines take none of their rows, wrapped or annotated. */
static void
with_rows(void)
{
    tiku_codeview_t *v = view_in("a {\n  bbbb bbbb bbbb bbbb\n}",
                                 TIKU_SYNTAX_C);
    tiku_rect_t narrow = { 10, 10, 10 + 10 * tiku_font_mono_cell(0) + 2,
                           200 };

    tiku_codeview_set_bounds(v, narrow);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP |
                              TIKU_CODEVIEW_ANNOTATIONS);
    (void)tiku_editdoc_annotation_add(tiku_codeview_doc(v), 1, "note", 0);
    check(rows_of(v) == 1 + 3 + 1 + 1, "FLD-012",
          "unfolded: the wrapped line's rows and its annotation's");
    (void)tiku_codeview_fold(v, 0, 0);
    check(rows_of(v) == 2 && row_of_line(v, 2) == 1, "FLD-012",
          "folded, none of them");
    (void)tiku_codeview_fold(v, 0, 1);
    check(rows_of(v) == 6, "FLD-012", "unfolded, all of them again");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    regions();
    folding();
    revealing();
    all();
    edits();
    two_views();
    column();
    keys();
    with_rows();
    if (fails) {
        printf("FAIL: %d checks\n", fails);
        return 1;
    }
    printf("PASS: code view folds -- regions by braces, blocks and "
           "indentation, folding, revealing, fold all, edits, two views, "
           "the fold column, keys, with wrapped and annotated rows\n");
    return 0;
}
