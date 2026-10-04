/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_advanced.c - style IDs an application defines, fold
 * levels a parser supplies and those read from JSON, kept through edits,
 * new tables and changes of language; rich lists of words, signature
 * tips, and the guides, ruler and zoom, painted and carried through a
 * drawing list.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview_private.h"
#include "tiku_dl.h"

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
#define H 180

/** @brief A view of a new document holding @p text, in bounds of W by H
 *         at the surface's corner; the view holds the only reference. */
static tiku_codeview_t *
page(const char *text)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;
    tiku_rect_t box = { 0, 0, W, H };

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, text, strlen(text)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
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

/** @brief Whether @p v painted while recorded gives a list with no misses
 *         that, flattened and read back, plays to the same pixels. */
static int
roundtrip(tiku_codeview_t *v)
{
    tiku_surface_t *a = tiku_surface_new(W, H, 0);
    tiku_surface_t *b = tiku_surface_new(W, H, 0);
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    size_t length, wrote = 0, read = 0;
    void *wire = NULL;
    int ok = 0;

    if (a != NULL && b != NULL && dl != NULL) {
        a->record = dl;
        tiku_codeview_paint(v, a);
        a->record = NULL;
        length = tiku_dl_flat_size(dl);
        wire = malloc(length);
        if (wire != NULL && !tiku_dl_misses(dl) &&
            tiku_dl_flatten(dl, wire, length, &wrote) && wrote == length)
            back = tiku_dl_unflatten(wire, wrote, &read);
        ok = back != NULL && read == wrote && tiku_dl_play(back, b) > 0 &&
             memcmp(a->px, b->px, (size_t)W * H * sizeof *a->px) == 0;
    }
    free(wire);
    tiku_dl_free(back);
    tiku_dl_free(dl);
    tiku_surface_free(a);
    tiku_surface_free(b);
    return ok;
}

/** @brief The colour bits of the pixel at @p x, @p y. */
static tiku_rgb_t
pixel(const tiku_surface_t *s, int64_t x, int64_t y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief A lexer giving every line one run in style ID 32. */
static tiku_edit_status_t
lexer(void *ctx, const char *text, size_t n, uint32_t in, uint32_t *out,
      tiku_syntax_run_fn emit, void *emit_ctx)
{
    (void)ctx;
    (void)text;
    (void)in;
    *out = 0;
    if (emit != NULL && n > 0)
        emit(emit_ctx, (tiku_ink_t)32, (int)n);
    return TIKU_EDIT_OK;
}

/** @brief An allocator with no room at all. */
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

static const tiku_ed_allocator_t fail = { no_alloc, no_realloc, free };

/** @brief A style ID a lexer uses is drawn as the application defines it:
 *         refused until defined, its role read by bracket matching, its
 *         ground under the indicators and the selection. */
static void
styles(void)
{
    tiku_codeview_t *v = page("( text )\nbody\nend");
    tiku_codeview_lexer_t provider = { NULL, 0, 0, lexer };
    tiku_codeview_style_t style, got;
    const tiku_rgb_t red = TIKU_RGB(220, 20, 40);
    tiku_edit_range_t marked = { 2, 6 };
    tiku_surface_t *s = tiku_surface_new(W, H, 0);
    tiku_cv_inks_t inks;
    int64_t x = 0, y = 0;
    size_t n = 0;

    memset(&style, 0, sizeof style);
    style.role = TIKU_INK_REMARK;
    style.flags = TIKU_CODEVIEW_ITALIC | TIKU_CODEVIEW_UNDERLINE;
    style.colors = TIKU_CODEVIEW_STYLE_FORE | TIKU_CODEVIEW_STYLE_BACK;
    style.foreground = TIKU_RGB(24, 60, 90);
    style.background = TIKU_RGB(238, 243, 249);
    check(tiku_codeview_set_lexer(v, &provider) == TIKU_EDIT_OK &&
          tiku_cv_style_line(v, 0, 8, &n) == NULL, "INK-007",
          "runs in a style ID not yet defined are refused");
    check(tiku_codeview_set_style(v, 32, &style) == TIKU_EDIT_OK &&
          tiku_codeview_get_style(v, 32, &got) == TIKU_EDIT_OK &&
          got.background == style.background, "INK-007",
          "a defined style reads back");
    check(tiku_cv_style_line(v, 0, 8, &n) != NULL && n == 1, "INK-007",
          "and its runs are used once it is defined");
    inks = tiku_cv_inks(v, 0, 8);
    check(!tiku_cv_code_at(&inks, 0), "INK-007",
          "a style with the remark role is not code to bracket matching");
    style.flags |= 128;
    check(tiku_codeview_set_style(v, 32, &style) == TIKU_EDIT_INVALID,
          "INK-007", "a flag there is not is refused");
    check(tiku_editdoc_indicator_add(v->doc, marked, 0, NULL) ==
              TIKU_EDIT_OK &&
          tiku_codeview_set_mark(v, 0, TIKU_CODEVIEW_MARK_BOX, red) ==
              TIKU_EDIT_OK && s != NULL, "INK-007", "an indicator is set");
    if (s != NULL) {
        tiku_codeview_set_focus(v, 0);
        tiku_codeview_paint(v, s);
        check(tiku_codeview_point(v, 2, &x, &y) == TIKU_EDIT_OK &&
              pixel(s, x, y) == red, "INK-007",
              "an indicator is drawn over a style's ground");
        (void)tiku_codeview_select(v, 0, 2);
        tiku_codeview_paint(v, s);
        check(tiku_codeview_point(v, 0, &x, &y) == TIKU_EDIT_OK &&
              pixel(s, x, y) == TIKU_C_SELECT, "INK-007",
              "and so is the selection");
        tiku_surface_free(s);
    }
    (void)tiku_codeview_select(v, 0, 0);
    check(roundtrip(v), "INK-007",
          "styled text plays back from a drawing list");
    tiku_codeview_destroy(v);
}

/** @brief A parser's table is copied for one generation, and a table that
 *         does not fit the document changes nothing. */
static void
fold_tables(void)
{
    tiku_codeview_t *v = page("( text )\nbody\nend");
    uint32_t levels[] = { TIKU_CODEVIEW_FOLD_HEADER, 1, 0 };
    uint64_t generation = tiku_editdoc_generation(v->doc);

    check(tiku_codeview_set_fold_levels(v, levels, 3, generation - 1) ==
          TIKU_EDIT_STALE, "FLD-013", "a table for another generation is "
          "refused");
    check(tiku_codeview_set_fold_levels(v, levels, 3, generation) ==
          TIKU_EDIT_OK, "FLD-013", "a table for this one is taken");
    levels[1] = 0;
    check(tiku_codeview_fold_end(v, 0) == 1, "FLD-013",
          "the table is copied");
    check(tiku_codeview_set_fold_levels(v, levels, 3, generation) ==
          TIKU_EDIT_INVALID_RANGE, "FLD-013",
          "a header no deeper line follows is refused");
    check(tiku_codeview_fold(v, 0, 0) == TIKU_EDIT_OK &&
          !tiku_codeview_shown(v, 1), "FLD-013", "a region folds by it");
    levels[1] = 2;
    tiku_ed_test_allocator(&fail);
    check(tiku_codeview_set_fold_levels(v, levels, 3, generation) ==
          TIKU_EDIT_NO_MEMORY, "FLD-013",
          "a table that cannot be copied is refused");
    tiku_ed_test_allocator(NULL);
    check(!tiku_codeview_shown(v, 1), "FLD-013",
          "and the region stays folded");
    check(tiku_codeview_insert(v, "x", 1, 0) == TIKU_EDIT_OK &&
          tiku_codeview_shown(v, 1) && tiku_codeview_fold_end(v, 0) == -1,
          "FLD-014", "an edit to the heading line unfolds its region, and "
          "the stale table heads nothing");
    tiku_codeview_destroy(v);
}

/* The lines wrapped_page() holds. */
#define WRAPPED_LINES 24

/** @brief A view whose first line wraps into several rows, then a line
 *         heading a short region, and short lines enough to scroll. */
static tiku_codeview_t *
wrapped_page(void)
{
    static char text[512];
    size_t i, n = 0;

    for (i = 0; i < 100; i++, n += 3)
        memcpy(text + n, "ab ", 3);
    memcpy(text + n, "\nb {\n  c\n}", 10);
    n += 10;
    for (i = 4; i < WRAPPED_LINES; i++, n += 2)
        memcpy(text + n, "\nz", 2);
    text[n] = '\0';
    return page(text);
}

/** @brief Fold levels for wrapped_page(): line 1 heading the lines after
 *         it through @p last, and none with @p last 1. */
static void
wrapped_levels(uint32_t *levels, int64_t last)
{
    int64_t i;

    for (i = 0; i < WRAPPED_LINES; i++)
        levels[i] = i >= 2 && i <= last ? 1 : 0;
    if (last >= 2)
        levels[1] = TIKU_CODEVIEW_FOLD_HEADER;
}

/** @brief Set @p levels for the document as it is now. */
static tiku_edit_status_t
set_levels(tiku_codeview_t *v, const uint32_t *levels)
{
    return tiku_codeview_set_fold_levels(v, levels, WRAPPED_LINES,
                                         tiku_editdoc_generation(v->doc));
}

/** @brief Folds made by a parser's table stay through an edit that leaves
 *         them alone and through the table that follows it, and so do the
 *         first row shown and the row of its line. */
static void
fold_edits(void)
{
    tiku_codeview_t *v = wrapped_page();
    uint32_t levels[WRAPPED_LINES];
    tiku_edit_pos_t at;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    tiku_codeview_scroll_to(v, 3, 0);
    check(tiku_codeview_top(v) == 3, "FLD-014",
          "the view is scrolled to a row inside its wrapped first line");
    wrapped_levels(levels, 2);
    check(set_levels(v, levels) == TIKU_EDIT_OK &&
          tiku_codeview_top(v) == 3, "FLD-014",
          "setting a table keeps the first row shown");
    check(tiku_codeview_fold(v, 1, 0) == TIKU_EDIT_OK &&
          !tiku_codeview_shown(v, 2), "FLD-014", "line 1's region folds");
    at = tiku_editdoc_line_end(v->doc, 5);
    (void)tiku_codeview_select(v, at, at);
    check(tiku_codeview_insert(v, "x", 1, 0) == TIKU_EDIT_OK &&
          tiku_codeview_top(v) == 3, "FLD-014",
          "typing on a line below keeps the first row shown");
    check(tiku_codeview_folded(v, 1) && !tiku_codeview_shown(v, 2) &&
          tiku_cv_fold_head(v, 1), "FLD-014", "while the table is stale "
          "the region the edit left alone stays folded, with its box");
    check(set_levels(v, levels) == TIKU_EDIT_OK &&
          tiku_codeview_folded(v, 1) && !tiku_codeview_shown(v, 2) &&
          tiku_codeview_top(v) == 3, "FLD-014",
          "a new table keeps a region folded while its line still heads "
          "one, and the first row shown");
    wrapped_levels(levels, 3);
    check(set_levels(v, levels) == TIKU_EDIT_OK &&
          tiku_codeview_folded(v, 1) && !tiku_codeview_shown(v, 3),
          "FLD-014", "over the lines the new table gives it");
    wrapped_levels(levels, 1);
    check(set_levels(v, levels) == TIKU_EDIT_OK &&
          !tiku_codeview_folded(v, 1) && tiku_codeview_shown(v, 2) &&
          tiku_codeview_shown(v, 3), "FLD-014",
          "a region whose line heads none in the new table unfolds");
    tiku_codeview_destroy(v);
}

/** @brief JSON's brackets fold outside strings, kept through an edit and
 *         read again only when the text has changed. */
static void
json_folds(void)
{
    tiku_codeview_t *v = page("{\n\"array\": [\n\"} ]\",\n1\n]\n}\n");
    tiku_edit_status_t st;

    check(tiku_codeview_json_folds(v) == TIKU_EDIT_OK &&
          tiku_codeview_fold_end(v, 0) == 4 &&
          tiku_codeview_fold_end(v, 1) == 3, "FLD-015",
          "brackets in a string do not count");
    check(tiku_codeview_fold(v, 1, 0) == TIKU_EDIT_OK, "FLD-015",
          "a region folds");
    tiku_ed_test_allocator(&fail);
    st = tiku_codeview_json_folds(v);
    tiku_ed_test_allocator(NULL);
    check(st == TIKU_EDIT_OK && tiku_codeview_folded(v, 1), "FLD-015",
          "an unchanged text is not read again");
    (void)tiku_codeview_select(v, 0, 0);
    check(tiku_codeview_insert(v, " ", 1, 0) == TIKU_EDIT_OK, "FLD-015",
          "the text changes");
    tiku_ed_test_allocator(&fail);
    st = tiku_codeview_json_folds(v);
    tiku_ed_test_allocator(NULL);
    check(st == TIKU_EDIT_NO_MEMORY, "FLD-015",
          "a changed text is read again");
    check(tiku_codeview_json_folds(v) == TIKU_EDIT_OK &&
          tiku_codeview_folded(v, 1) && !tiku_codeview_shown(v, 2),
          "FLD-014", "an edit on another line keeps a JSON region folded");
    tiku_codeview_destroy(v);
}

/** @brief A change of language or lexer clears a parser's table and
 *         nothing else: the rules' folds and the wrapped rows stay. */
static void
language_change(void)
{
    tiku_codeview_t *v = wrapped_page();
    uint32_t levels[WRAPPED_LINES];

    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    check(tiku_codeview_fold(v, 1, 0) == TIKU_EDIT_OK &&
          !tiku_codeview_shown(v, 2), "FLD-016", "a C region folds");
    tiku_codeview_scroll_to(v, 3, 0);
    check(tiku_codeview_set_lexer(v, tiku_codeview_json_lexer()) ==
              TIKU_EDIT_OK &&
          tiku_codeview_folded(v, 1) && tiku_codeview_top(v) == 3,
          "FLD-016", "a new lexer keeps the rules' folds and the rows");
    tiku_codeview_set_language(v, TIKU_SYNTAX_BASIC);
    check(tiku_codeview_folded(v, 1) && tiku_codeview_top(v) == 3,
          "FLD-016", "and so does a new language");
    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    wrapped_levels(levels, 3);
    check(set_levels(v, levels) == TIKU_EDIT_OK &&
          tiku_codeview_fold_end(v, 1) == 3, "FLD-016",
          "a table folds instead of the rules");
    tiku_codeview_set_language(v, TIKU_SYNTAX_BASIC);
    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    check(tiku_codeview_fold_end(v, 1) == 2 && tiku_codeview_top(v) == 3,
          "FLD-016", "until a change of language clears it");
    tiku_codeview_destroy(v);
}

/** @brief A rich list offers labels, puts in texts, keeps the host's
 *         indices, and is put in by an extra key, typed or committed. */
static void
completion(void)
{
    tiku_codeview_t *v = page("pr");
    tiku_codeview_completion_item_t items[] = {
        { "Println", "PRINT\n", "Write a line", TIKU_COMPLETION_FUNCTION },
        { "Print", "PRINT", "Write a value", TIKU_COMPLETION_FUNCTION },
        { "Value", NULL, "Integer", TIKU_COMPLETION_VARIABLE }
    };
    tiku_codeview_completion_t list;
    tiku_event_t key;
    tiku_text_input_t input;
    size_t chosen = 0;
    int taken = 1;

    memset(&list, 0, sizeof list);
    list.range.end = 2;
    list.items = items;
    list.count = 3;
    list.prefix = "pr";
    list.flags = TIKU_COMPLETION_SORT | TIKU_COMPLETION_IGNORE_CASE;
    list.rows = 8;
    list.accept = ";";
    list.current = SIZE_MAX;
    check(tiku_codeview_complete(v, &list) == TIKU_EDIT_OK &&
          v->list_count == 2 && tiku_codeview_completion_selected(v) == 1,
          "LST-006", "the labels starting with the prefix are offered, "
          "sorted, the host's index of the first chosen");
    items[1].label = NULL;
    check(tiku_codeview_complete(v, &list) == TIKU_EDIT_INVALID_TEXT &&
          tiku_codeview_completion_selected(v) == 1, "LST-006",
          "an item with no label is refused, the list shown staying");
    items[1].label = "Print";
    tiku_ed_test_allocator(&fail);
    check(tiku_codeview_complete(v, &list) == TIKU_EDIT_NO_MEMORY,
          "LST-006", "a list that cannot be copied is refused");
    tiku_ed_test_allocator(NULL);
    check(roundtrip(v), "LST-006", "the list plays back from a drawing "
          "list");
    memset(&key, 0, sizeof key);
    key.type = TIKU_EVENT_KEY_DOWN;
    key.key = ';';
    check(tiku_codeview_event(v, &key) && holds(v, "PRINT"), "LST-006",
          "an extra key puts the chosen text in and is not typed");
    check(tiku_codeview_completion_result(v, &chosen) && chosen == 1 &&
          !tiku_codeview_completion_result(v, &chosen), "LST-006",
          "the host's index of what went in is told once");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK && holds(v, "pr"),
          "LST-006", "putting it in is one undo step");
    check(tiku_codeview_complete(v, &list) == TIKU_EDIT_OK, "LST-006",
          "the list is offered again");
    memset(&input, 0, sizeof input);
    check(tiku_text_input_set(&input, ";", 1), "LST-006",
          "a committed key is staged");
    while (tiku_text_input_next(&input, &key))
        taken &= tiku_codeview_event(v, &key) != 0;
    check(taken && holds(v, "PRINT") &&
          tiku_codeview_completion_result(v, &chosen) && chosen == 1,
          "LST-006", "and the key committed as text puts it in too");
    tiku_codeview_destroy(v);
}

/** @brief A signature's tip highlights the argument the caret is in,
 *         counting commas outside brackets and strings, and goes when the
 *         caret leaves the call. */
static void
signatures(void)
{
    tiku_codeview_t *v = page("f(g(1,2), \"a,b\", 3)");
    const char *signature = "f(first, second, third)";
    tiku_edit_range_t args[] = { { 2, 7 }, { 9, 15 }, { 17, 22 } };
    tiku_edit_range_t bad = { 0, 100 };
    tiku_edit_pos_t end;

    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    (void)tiku_codeview_select(v, 2, 2);
    check(tiku_codeview_signature_show(v, 1, signature, args, 3) ==
              TIKU_EDIT_OK &&
          tiku_codeview_signature_argument(v) == 0, "TIP-003",
          "the tip starts at the first argument");
    tiku_ed_test_allocator(&fail);
    check(tiku_codeview_signature_show(v, 1, signature, args, 3) ==
          TIKU_EDIT_NO_MEMORY, "TIP-003",
          "a tip that cannot be copied is refused");
    tiku_ed_test_allocator(NULL);
    check(tiku_codeview_signature_argument(v) == 0, "TIP-003",
          "and the tip shown stays");
    args[1].start = 0;
    args[1].end = 0;
    (void)tiku_codeview_select(v, 10, 10);
    check(roundtrip(v) && tiku_codeview_signature_argument(v) == 1 &&
          v->tip_highlight.start == 9 && v->tip_highlight.end == 15,
          "TIP-003", "a comma inside brackets is not counted, and the "
          "tip keeps its own copy of the arguments");
    (void)tiku_codeview_select(v, 17, 17);
    check(roundtrip(v) && tiku_codeview_signature_argument(v) == 2,
          "TIP-003", "nor is a comma inside a string");
    check(tiku_codeview_tip_highlight(v, bad) == TIKU_EDIT_INVALID_RANGE,
          "TIP-003", "a highlight past the tip's text is refused");
    end = tiku_editdoc_length(v->doc);
    (void)tiku_codeview_select(v, end, end);
    check(roundtrip(v) && !tiku_codeview_tip_shown(v), "TIP-003",
          "the tip goes when the caret leaves the call");
    tiku_codeview_destroy(v);
}

/** @brief The guides and the ruler are drawn behind the text and change
 *         none of it. */
static void
guides(void)
{
    tiku_codeview_t *v = page("    first\n\tsecond\nthird\nfourth\nfifth\n"
                              "sixth");
    tiku_surface_t *s = tiku_surface_new(W, H, 0);
    tiku_rgb_t guide = tiku_dim(tiku_ink(TIKU_INK_FAINT), TIKU_C_DOC);
    int cell = tiku_font_mono_cell(0), x0, y0;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_INDENT_GUIDES |
                                 TIKU_CODEVIEW_LINE_NUMBERS |
                                 TIKU_CODEVIEW_WRAP);
    check(tiku_codeview_set_ruler(v, 20) == TIKU_EDIT_OK &&
          tiku_codeview_set_ruler(v, 10001) == TIKU_EDIT_LIMIT &&
          tiku_codeview_ruler(v) == 20, "DSP-009",
          "a ruler past column 10000 is refused, the ruler set staying");
    if (s == NULL) {
        check(0, "DSP-009", "a surface to paint on");
        tiku_codeview_destroy(v);
        return;
    }
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, s);
    x0 = tiku_cv_text_x(v);
    y0 = tiku_cv_text_y(v);
    check(pixel(s, x0 + 4 * cell, y0 + 1) == guide, "DSP-009",
          "a guide stands at the first level of indentation");
    check(pixel(s, x0 + 20 * cell, y0 + 1) == guide, "DSP-009",
          "and the ruler at its column");
    (void)tiku_codeview_set_ruler(v, 0);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS |
                                 TIKU_CODEVIEW_WRAP);
    tiku_codeview_paint(v, s);
    check(pixel(s, x0 + 4 * cell, y0 + 1) == TIKU_C_DOC &&
          pixel(s, x0 + 20 * cell, y0 + 1) == TIKU_C_DOC, "DSP-009",
          "without them neither is drawn");
    check(holds(v, "    first\n\tsecond\nthird\nfourth\nfifth\nsixth"),
          "DSP-009", "the text is as it was");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief Each zoom step draws and hits the text at the face's new size,
 *         recorded and played back, leaving the interface's size. */
static void
zoom(void)
{
    tiku_codeview_t *v = page("    first\n\tsecond\nthird\nfourth\nfifth\n"
                              "sixth");
    int ui, steps, hits = 1, plays = 1, sizes = 1;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_INDENT_GUIDES |
                                 TIKU_CODEVIEW_LINE_NUMBERS |
                                 TIKU_CODEVIEW_WRAP);
    (void)tiku_codeview_set_ruler(v, 20);
    for (ui = 12; ui <= 16; ui += 4) {
        (void)tiku_font_set_size(ui);
        for (steps = -2; steps <= 4; steps++) {
            int64_t x = 0, y = 0;

            hits &= tiku_codeview_set_zoom(v, steps) == TIKU_EDIT_OK &&
                    tiku_codeview_zoom(v) == steps &&
                    tiku_codeview_point(v, 6, &x, &y) == TIKU_EDIT_OK &&
                    tiku_codeview_hit(v, (int)x, (int)y) == 6;
            plays &= roundtrip(v);
        }
        sizes &= tiku_font_size() == ui;
    }
    check(hits, "DSP-010", "every step from -2 to 4 hits where it draws");
    check(plays, "DSP-010", "and plays back from a drawing list");
    check(sizes, "DSP-010", "the interface's size is left as it was");
    check(tiku_codeview_set_zoom(v, 5) == TIKU_EDIT_LIMIT, "DSP-010",
          "a step past 4 is refused");
    (void)tiku_font_set_size(12);
    tiku_codeview_set_font(v, tiku_font_plain());
    check(tiku_codeview_zoom(v) == 0 &&
          tiku_codeview_set_zoom(v, 1) == TIKU_EDIT_INVALID, "DSP-010",
          "a face of the view's own puts the zoom back and refuses it");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_size(12);
    styles();
    fold_tables();
    fold_edits();
    json_folds();
    language_change();
    completion();
    signatures();
    guides();
    zoom();
    printf("%s\n", fails ? "FAIL: code view advanced" :
           "PASS: code view advanced -- style IDs, parser and JSON folds "
           "through edits, rich lists, signature tips, guides and zoom");
    return fails != 0;
}
