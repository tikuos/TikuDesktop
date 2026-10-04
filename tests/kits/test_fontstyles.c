/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fontstyles.c - the cuts of a face: each mono face's bold, italic
 * and bold italic on the same advance and metrics as its upright cut,
 * the bold of the proportional faces, none where there is none, and a
 * recording that names the italic faces and plays them back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_gfx.h"

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

#define BI (TIKU_FONT_BOLD | TIKU_FONT_ITALIC)

/** @brief Each mono face, at both sizes, reaches its four cuts, and each
 *         cut reaches the others. */
static void
mono_cuts(void)
{
    static const int sizes[] = { 12, 14 };
    size_t k;

    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const tiku_font_t *up, *bold, *it, *bit;

        (void)tiku_font_set_size(sizes[k]);
        up = tiku_font_mono(0);
        bold = tiku_font_mono(1);
        it = tiku_font_styled(up, TIKU_FONT_ITALIC);
        bit = tiku_font_styled(up, BI);
        check(it != NULL && bit != NULL && it != up && it != bold &&
              bit != up && bit != bold && it != bit, "FNT-001",
              "a mono face has an italic and a bold italic cut, each a "
              "face of its own");
        check(tiku_font_styled(up, 0) == up &&
              tiku_font_styled(up, TIKU_FONT_BOLD) == bold &&
              tiku_font_styled(bold, 0) == up &&
              tiku_font_styled(it, TIKU_FONT_BOLD) == bold &&
              tiku_font_styled(bit, TIKU_FONT_ITALIC) == it &&
              tiku_font_styled(bold, BI) == bit, "FNT-001",
              "and every cut reaches every other");
    }
    (void)tiku_font_set_size(12);
}

/** @brief The italic cuts keep the upright cut's height, ascent and every
 *         advance, so a style never moves a character. */
static void
mono_metrics(void)
{
    static const int sizes[] = { 12, 14 };
    size_t k;

    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const tiku_font_t *up, *cut[3];
        unsigned cp;
        int i, same = 1;

        (void)tiku_font_set_size(sizes[k]);
        up = tiku_font_mono(0);
        cut[0] = tiku_font_mono(1);
        cut[1] = tiku_font_styled(up, TIKU_FONT_ITALIC);
        cut[2] = tiku_font_styled(up, BI);
        for (i = 0; i < 3; i++) {
            if (cut[i] == NULL || cut[i]->height != up->height ||
                cut[i]->ascent != up->ascent)
                same = 0;
            for (cp = 32; cut[i] != NULL && cp <= 0xFF; cp++) {
                if (tiku_text_advance(cut[i], cp) !=
                    tiku_text_advance(up, cp))
                    same = 0;
            }
        }
        check(same, "FNT-002",
              "each cut has the upright cut's height, ascent and the "
              "advance of every Latin-1 character");
    }
    (void)tiku_font_set_size(12);
}

/** @brief The proportional faces have a bold cut and no italic one, and a
 *         face or a style the kit does not know has none. */
static void
proportional_cuts(void)
{
    const tiku_font_t *plain = tiku_font_plain(), *bold = tiku_font_bold();
    const tiku_font_t *small = tiku_font_at(10);
    const tiku_font_t *small_bold = tiku_font_styled(small, TIKU_FONT_BOLD);
    tiku_font_t stranger = *plain;

    check(tiku_font_styled(plain, TIKU_FONT_BOLD) == bold &&
          tiku_font_styled(bold, 0) == plain &&
          tiku_font_styled(plain, 0) == plain, "FNT-003",
          "the face in force reaches its bold cut and back");
    check(small_bold != NULL && small_bold != small &&
          tiku_font_styled(small_bold, 0) == small &&
          small_bold->height <= small->height + 2, "FNT-003",
          "a face at another size reaches the bold cut of that size");
    check(tiku_font_styled(plain, TIKU_FONT_ITALIC) == NULL &&
          tiku_font_styled(bold, BI) == NULL &&
          tiku_font_styled(small, TIKU_FONT_ITALIC) == NULL, "FNT-003",
          "and none of them has an italic cut");
    check(tiku_font_styled(NULL, 0) == NULL &&
          tiku_font_styled(&stranger, TIKU_FONT_BOLD) == NULL &&
          tiku_font_styled(tiku_font_mono(0), 0x4u) == NULL, "FNT-003",
          "a face the kit does not know, or a style it does not, has none");
}

/** @brief Italic ink is not the upright ink, it is recorded by the name
 *         the far end resolves, and it plays back to the same pixels. */
static void
italic_drawn(void)
{
    const tiku_font_t *up = tiku_font_mono(0);
    const tiku_font_t *it = tiku_font_styled(up, TIKU_FONT_ITALIC);
    const tiku_font_t *bit = tiku_font_styled(up, BI);
    tiku_surface_t *a = tiku_surface_new(120, 30, TIKU_RGB(255, 255, 255));
    tiku_surface_t *b = tiku_surface_new(120, 30, TIKU_RGB(255, 255, 255));
    tiku_surface_t *c = tiku_surface_new(120, 30, TIKU_RGB(255, 255, 255));
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    size_t size, wrote = 0, read = 0;
    void *flat;

    if (a == NULL || b == NULL || c == NULL || dl == NULL || it == NULL ||
        bit == NULL)
        exit(1);
    tiku_text(a, up, 4, 20, "Hello", TIKU_RGB(0, 0, 0));
    tiku_text(b, it, 4, 20, "Hello", TIKU_RGB(0, 0, 0));
    check(memcmp(a->px, b->px, (size_t)120 * 30 * sizeof *a->px) != 0,
          "FNT-004", "italic letters are not the upright ones");
    b->record = dl;
    tiku_text(b, it, 4, 20, "Hello", TIKU_RGB(0, 0, 0));
    tiku_text(b, bit, 50, 20, "World", TIKU_RGB(0, 0, 0));
    b->record = NULL;
    size = tiku_dl_flat_size(dl);
    flat = malloc(size);
    if (flat != NULL && tiku_dl_flatten(dl, flat, size, &wrote))
        back = tiku_dl_unflatten(flat, wrote, &read);
    tiku_fill(b, (tiku_rect_t){ 0, 0, 120, 30 }, TIKU_RGB(255, 255, 255));
    tiku_text(b, it, 4, 20, "Hello", TIKU_RGB(0, 0, 0));
    tiku_text(b, bit, 50, 20, "World", TIKU_RGB(0, 0, 0));
    check(tiku_dl_misses(dl) == 0 && tiku_dl_count(dl) == 2, "FNT-004",
          "the italic cuts are named in a recording, not missed");
    check(back != NULL && tiku_dl_play(back, c) > 0 &&
          memcmp(b->px, c->px, (size_t)120 * 30 * sizeof *b->px) == 0,
          "FNT-004", "and the recording plays back to the same pixels");
    free(flat);
    tiku_dl_free(dl);
    tiku_dl_free(back);
    tiku_surface_free(a);
    tiku_surface_free(b);
    tiku_surface_free(c);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    mono_cuts();
    mono_metrics();
    proportional_cuts();
    italic_drawn();
    printf("%s\n", fails ? "FAIL: font styles" :
           "PASS: font styles -- mono cuts on one grid, bold proportional "
           "cuts, none where there is none, italic recorded and played");
    return fails != 0;
}
