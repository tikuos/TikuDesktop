/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_inkstyles.c - a code view's text styles by ink: bold and
 * italic cuts drawn where the view's face puts each character, lines
 * under the text, a face with no such cut, the selection, the block
 * caret, and a recording, each compared with the same calls made
 * directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
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

#define W 320
#define H 140
#define PAPER TIKU_RGB(255, 0, 255)

static const tiku_rect_t box = { 10, 10, 300, 110 };
/* Line 0: a faint number, a blank, a remark whose letters reach below the
 * baseline.  Line 1: a faint number, a reserved word and a number. */
static const char *const program = "10 REM py\n20 PRINT 1";

static int x0, y0, cell, step, asc;

static void
metrics(void)
{
    const tiku_font_t *f = tiku_font_mono(0);

    x0 = box.x + 5;
    y0 = box.y + 3;
    cell = tiku_font_mono_cell(0);
    step = f->height + 2;
    asc = f->ascent;
}

static tiku_surface_t *
surface(void)
{
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);

    if (s == NULL)
        exit(1);
    return s;
}

static int
same(const tiku_surface_t *a, const tiku_surface_t *b)
{
    return memcmp(a->px, b->px, (size_t)W * H * sizeof *a->px) == 0;
}

/** @brief A view of @p s, coloured as BASIC when @p basic, unfocused so
 *         no caret is drawn. */
static tiku_codeview_t *
view_of(const char *s, int basic)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_focus(v, 0);
    if (basic)
        tiku_codeview_set_language(v, TIKU_SYNTAX_BASIC);
    return v;
}

/** @brief The cut of the mono face in @p style. */
static const tiku_font_t *
mono(unsigned style)
{
    return tiku_font_styled(tiku_font_mono(0), style);
}

/** @brief The ground and the text's clip, as a reference starts. */
static void
ground(tiku_surface_t *s)
{
    tiku_rect_t clip = { x0, box.y, box.x + box.w - x0, box.h };

    tiku_fill(s, box, TIKU_C_DOC);
    tiku_clip_set(s, clip);
}

/** @brief @p text in @p f on row @p row from column @p col, in @p ink. */
static void
at(tiku_surface_t *s, const tiku_font_t *f, int row, int col,
   const char *text, tiku_ink_t ink)
{
    tiku_text(s, f, x0 + col * cell, y0 + row * step + asc, text,
              tiku_ink(ink));
}

/** @brief A line under @p n columns of row @p row from column @p col. */
static void
under(tiku_surface_t *s, int row, int col, int n, tiku_rgb_t c)
{
    tiku_hline(s, x0 + col * cell, y0 + row * step + asc + 1, n * cell, c);
}

/** @brief Setting and reading a style, and what is refused. */
static void
setting(void)
{
    tiku_codeview_t *v = view_of(program, 1);

    check(tiku_codeview_ink_style(v, TIKU_INK_WORD) == 0 &&
          tiku_codeview_ink_style(v, TIKU_INK_PLAIN) == 0, "INK-001",
          "a new view draws every ink plain");
    check(tiku_codeview_set_ink_style(v, TIKU_INK_WORD,
                                      TIKU_CODEVIEW_BOLD |
                                      TIKU_CODEVIEW_UNDERLINE) ==
          TIKU_EDIT_OK &&
          tiku_codeview_ink_style(v, TIKU_INK_WORD) ==
          (TIKU_CODEVIEW_BOLD | TIKU_CODEVIEW_UNDERLINE), "INK-001",
          "a style set is the style read");
    check(tiku_codeview_set_ink_style(v, (tiku_ink_t)(TIKU_INK_LINK + 1),
                                      TIKU_CODEVIEW_BOLD) ==
          TIKU_EDIT_INVALID &&
          tiku_codeview_set_ink_style(v, TIKU_INK_WORD, 0x80u) ==
          TIKU_EDIT_INVALID &&
          tiku_codeview_set_ink_style(NULL, TIKU_INK_WORD, 0) ==
          TIKU_EDIT_INVALID &&
          tiku_codeview_ink_style(v, TIKU_INK_WORD) ==
          (TIKU_CODEVIEW_BOLD | TIKU_CODEVIEW_UNDERLINE) &&
          tiku_codeview_ink_style(v, (tiku_ink_t)(TIKU_INK_LINK + 1)) == 0,
          "INK-001", "an ink past the last, or a flag that is none of "
          "them, is refused and the style kept");
    tiku_codeview_destroy(v);
}

/** @brief Bold and italic runs are drawn in their cuts at the places the
 *         view's face gives them; the rest as it was. */
static void
cuts(void)
{
    tiku_codeview_t *v = view_of(program, 1);
    tiku_surface_t *got = surface(), *want = surface();

    (void)tiku_codeview_set_ink_style(v, TIKU_INK_WORD, TIKU_CODEVIEW_BOLD);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_ITALIC);
    tiku_codeview_paint(v, got);
    ground(want);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_ITALIC), 0, 3, "REM py", TIKU_INK_REMARK);
    at(want, mono(0), 1, 0, "20", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_BOLD), 1, 3, "PRINT", TIKU_INK_WORD);
    at(want, mono(0), 1, 9, "1", TIKU_INK_NUM);
    check(same(got, want), "INK-002",
          "reserved words in the bold cut and the remark in the italic "
          "one, each where the face puts it, the rest upright");
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_BOLD |
                                      TIKU_CODEVIEW_ITALIC);
    tiku_codeview_paint(v, got);
    ground(want);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_BOLD | TIKU_FONT_ITALIC), 0, 3, "REM py",
       TIKU_INK_REMARK);
    at(want, mono(0), 1, 0, "20", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_BOLD), 1, 3, "PRINT", TIKU_INK_WORD);
    at(want, mono(0), 1, 9, "1", TIKU_INK_NUM);
    check(same(got, want), "INK-002", "and both, in the bold italic cut");
    tiku_codeview_destroy(v);

    v = view_of("plain words", 0);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_PLAIN, TIKU_CODEVIEW_BOLD);
    tiku_codeview_paint(v, got);
    ground(want);
    at(want, mono(TIKU_FONT_BOLD), 0, 0, "plain words", TIKU_INK_PLAIN);
    check(same(got, want), "INK-002",
          "text no language colours takes the plain ink's style");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief An underline runs under every character of its ink, blanks
 *         too, one pixel under the baseline in the text's colour. */
static void
underlines(void)
{
    tiku_codeview_t *v = view_of(program, 1);
    tiku_surface_t *got = surface(), *want = surface();

    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_UNDERLINE);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_NUM,
                                      TIKU_CODEVIEW_UNDERLINE |
                                      TIKU_CODEVIEW_BOLD);
    tiku_codeview_paint(v, got);
    ground(want);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(0), 0, 3, "REM py", TIKU_INK_REMARK);
    under(want, 0, 3, 6, tiku_ink(TIKU_INK_REMARK));
    at(want, mono(0), 1, 0, "20", TIKU_INK_FAINT);
    at(want, mono(0), 1, 3, "PRINT", TIKU_INK_WORD);
    at(want, mono(TIKU_FONT_BOLD), 1, 9, "1", TIKU_INK_NUM);
    under(want, 1, 9, 1, tiku_ink(TIKU_INK_NUM));
    check(same(got, want), "INK-003",
          "a line under the remark, its blank included, and under the "
          "number, each in its text's colour");
    (void)tiku_codeview_select(v, 5, 8);
    tiku_codeview_paint(v, got);
    ground(want);
    tiku_fill(want, (tiku_rect_t){ x0 + 5 * cell, y0, 3 * cell, step },
              TIKU_C_SELECT);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(0), 0, 3, "RE", TIKU_INK_REMARK);
    under(want, 0, 3, 2, tiku_ink(TIKU_INK_REMARK));
    tiku_text(want, mono(0), x0 + 5 * cell, y0 + asc, "M p", TIKU_C_SELTEXT);
    under(want, 0, 5, 3, TIKU_C_SELTEXT);
    at(want, mono(0), 0, 8, "y", TIKU_INK_REMARK);
    under(want, 0, 8, 1, tiku_ink(TIKU_INK_REMARK));
    at(want, mono(0), 1, 0, "20", TIKU_INK_FAINT);
    at(want, mono(0), 1, 3, "PRINT", TIKU_INK_WORD);
    at(want, mono(TIKU_FONT_BOLD), 1, 9, "1", TIKU_INK_NUM);
    under(want, 1, 9, 1, tiku_ink(TIKU_INK_NUM));
    check(same(got, want), "INK-003",
          "selected, the line under the selected part takes the "
          "selection's ink");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief Selected text keeps its cut, and so does the character a block
 *         caret stands over. */
static void
selected_and_block(void)
{
    tiku_codeview_t *v = view_of(program, 1);
    tiku_surface_t *got = surface(), *want = surface();

    (void)tiku_codeview_set_ink_style(v, TIKU_INK_WORD, TIKU_CODEVIEW_BOLD);
    (void)tiku_codeview_select(v, 13, 18);
    tiku_codeview_paint(v, got);
    ground(want);
    tiku_fill(want, (tiku_rect_t){ x0 + 3 * cell, y0 + step, 5 * cell, step },
              TIKU_C_SELECT);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(0), 0, 3, "REM py", TIKU_INK_REMARK);
    at(want, mono(0), 1, 0, "20", TIKU_INK_FAINT);
    tiku_text(want, mono(TIKU_FONT_BOLD), x0 + 3 * cell, y0 + step + asc,
              "PRINT", TIKU_C_SELTEXT);
    at(want, mono(0), 1, 9, "1", TIKU_INK_NUM);
    check(same(got, want), "INK-004",
          "selected text is drawn in its cut, in the selection's ink");
    (void)tiku_codeview_select(v, 10, 18);
    tiku_codeview_paint(v, got);
    ground(want);
    tiku_fill(want, (tiku_rect_t){ x0, y0 + step, 8 * cell, step },
              TIKU_C_SELECT);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(0), 0, 3, "REM py", TIKU_INK_REMARK);
    tiku_text(want, mono(0), x0, y0 + step + asc, "20 ", TIKU_C_SELTEXT);
    tiku_text(want, mono(TIKU_FONT_BOLD), x0 + 3 * cell, y0 + step + asc,
              "PRINT", TIKU_C_SELTEXT);
    at(want, mono(0), 1, 9, "1", TIKU_INK_NUM);
    check(same(got, want), "INK-004",
          "and where one selected ink meets another, each keeps its own "
          "cut");
    (void)tiku_codeview_select(v, 13, 13);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_BLOCK_CARET);
    tiku_codeview_set_focus(v, 1);
    tiku_codeview_paint(v, got);
    ground(want);
    at(want, mono(0), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(0), 0, 3, "REM py", TIKU_INK_REMARK);
    at(want, mono(0), 1, 0, "20", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_BOLD), 1, 3, "PRINT", TIKU_INK_WORD);
    at(want, mono(0), 1, 9, "1", TIKU_INK_NUM);
    tiku_fill(want, (tiku_rect_t){ x0 + 3 * cell, y0 + step, cell, step },
              TIKU_C_TEXT);
    tiku_text(want, mono(TIKU_FONT_BOLD), x0 + 3 * cell, y0 + step + asc,
              "P", TIKU_C_DOC);
    check(same(got, want), "INK-004",
          "and a block caret's character is drawn in its cut");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief A face whose bold cut is wider draws each bold letter alone at
 *         the view's own place for it, and a face with no italic cut
 *         draws italic text upright. */
static void
proportional(void)
{
    const tiku_font_t *plain = tiku_font_plain(), *bold = tiku_font_bold();
    tiku_codeview_t *v = view_of(program, 1);
    tiku_surface_t *got = surface(), *want = surface();
    const char *word = "PRINT";
    int x = x0, i;
    char one[2] = { 0, 0 };

    tiku_codeview_set_font(v, plain);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_WORD, TIKU_CODEVIEW_BOLD);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_ITALIC);
    tiku_codeview_paint(v, got);
    ground(want);
    tiku_text(want, plain, x0, y0 + plain->ascent, "10",
              tiku_ink(TIKU_INK_FAINT));
    x = x0 + tiku_text_width(plain, "10 ");
    tiku_text(want, plain, x, y0 + plain->ascent, "REM py",
              tiku_ink(TIKU_INK_REMARK));
    tiku_text(want, plain, x0, y0 + plain->height + 2 + plain->ascent,
              "20", tiku_ink(TIKU_INK_FAINT));
    x = x0 + tiku_text_width(plain, "20 ");
    for (i = 0; word[i] != '\0'; i++) {
        one[0] = word[i];
        tiku_text(want, bold, x, y0 + plain->height + 2 + plain->ascent, one,
                  tiku_ink(TIKU_INK_WORD));
        x += tiku_text_advance(plain, (unsigned char)word[i]);
    }
    tiku_text(want, plain, x0 + tiku_text_width(plain, "20 PRINT "),
              y0 + plain->height + 2 + plain->ascent, "1",
              tiku_ink(TIKU_INK_NUM));
    check(tiku_text_width(bold, word) != tiku_text_width(plain, word) &&
          same(got, want), "INK-005",
          "bold letters wider than the face's stand one by one at the "
          "face's places, and italic with no italic cut is upright");
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_BOLD |
                                      TIKU_CODEVIEW_ITALIC);
    tiku_codeview_paint(v, got);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK, TIKU_CODEVIEW_BOLD);
    tiku_codeview_paint(v, want);
    check(same(got, want), "INK-005",
          "bold italic with no italic cut is bold");
    tiku_codeview_destroy(v);

    v = view_of(program, 1);
    tiku_codeview_set_font(v, tiku_font_mono(1));
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_ITALIC);
    tiku_codeview_paint(v, got);
    ground(want);
    at(want, mono(TIKU_FONT_BOLD), 0, 0, "10", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_BOLD | TIKU_FONT_ITALIC), 0, 3, "REM py",
       TIKU_INK_REMARK);
    at(want, mono(TIKU_FONT_BOLD), 1, 0, "20", TIKU_INK_FAINT);
    at(want, mono(TIKU_FONT_BOLD), 1, 3, "PRINT", TIKU_INK_WORD);
    at(want, mono(TIKU_FONT_BOLD), 1, 9, "1", TIKU_INK_NUM);
    check(same(got, want), "INK-005",
          "a view in a bold face draws italic in the bold italic cut");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief Everything a styled view paints is carried by a recording, and
 *         the recording plays back to the pixels painted. */
static void
recorded(void)
{
    tiku_codeview_t *v = view_of(program, 1);
    tiku_surface_t *got = surface(), *played = surface();
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    size_t size, wrote = 0, read = 0;
    void *flat;

    if (dl == NULL)
        exit(1);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_WORD, TIKU_CODEVIEW_BOLD);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_REMARK,
                                      TIKU_CODEVIEW_ITALIC |
                                      TIKU_CODEVIEW_UNDERLINE);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_NUM,
                                      TIKU_CODEVIEW_BOLD |
                                      TIKU_CODEVIEW_ITALIC);
    got->record = dl;
    tiku_codeview_paint(v, got);
    got->record = NULL;
    size = tiku_dl_flat_size(dl);
    flat = malloc(size);
    if (flat != NULL && tiku_dl_flatten(dl, flat, size, &wrote))
        back = tiku_dl_unflatten(flat, wrote, &read);
    check(tiku_dl_misses(dl) == 0 && back != NULL && read == wrote &&
          tiku_dl_play(back, played) > 0 && same(got, played), "INK-006",
          "a recording carries every cut and line, and plays back to the "
          "pixels painted");
    free(flat);
    tiku_dl_free(dl);
    tiku_dl_free(back);
    tiku_surface_free(got);
    tiku_surface_free(played);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    metrics();
    setting();
    cuts();
    underlines();
    selected_and_block();
    proportional();
    recorded();
    printf("%s\n", fails ? "FAIL: code view ink styles" :
           "PASS: code view ink styles -- bold and italic cuts in place, "
           "underlines, selections, block carets, wider faces, recordings");
    return fails != 0;
}
