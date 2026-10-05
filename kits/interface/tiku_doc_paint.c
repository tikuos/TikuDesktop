/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc_paint.c - what tiku_doc.h laid out, drawn.
 *
 * Every rectangle here is read from the piece and line tables; nothing
 * is measured a second time.  A link is underlined as well as tinted,
 * because under a theme with no hue the line under it is all that tells
 * a link from a word.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_doc_paint.h"

#include <string.h>

#include "tiku_font.h"
#include "tiku_text_layout.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

/** @brief The face a piece is drawn in. */
static const tiku_font_t *face_of(int face)
{
    switch (face) {
    case TIKU_MEASURE_BOLD: return tiku_font_bold();
    case TIKU_MEASURE_MONO: return tiku_font_mono(0);
    default:
#ifndef TIKU_DISPLAYLESS
        if (face >= 10) return tiku_font_at(face);
#endif
        return tiku_font_plain();
    }
}

tiku_rgb_t tiku_doc_ink(int doc_ink)
{
    switch (doc_ink) {
    case TIKU_DOC_INK_FAINT:    return tiku_ink(TIKU_INK_FAINT);
    /* Code in a sentence takes the reserved-word ink. */
    case TIKU_DOC_INK_CODE:     return tiku_ink(TIKU_INK_WORD);
    /* The proportional faces have no italic cut, so emphasis takes the
     * path ink. */
    case TIKU_DOC_INK_EMPHASIS: return tiku_ink(TIKU_INK_PATH);
    case TIKU_DOC_INK_LINK:     return tiku_ink(TIKU_INK_LINK);
    default:                    return tiku_ink(TIKU_INK_PLAIN);
    }
}

/** @brief One piece's bytes, terminated, for the text call. */
static const char *piece_text(const tiku_doc_t *d, const tiku_doc_piece_t *pc)
{
    static char out[TIKU_TEXT_LINE_MAX];
    int n = pc->len < (int)sizeof out - 1 ? pc->len : (int)sizeof out - 1;

    while (n > 1 && ((unsigned char)tiku_doc_text(d, pc->off)[n] & 0xc0) == 0x80) {
        n--;
    }
    memcpy(out, tiku_doc_text(d, pc->off), (size_t)n);
    out[n] = 0;
    return out;
}

/** @brief The ground, rule or frame a block stands on. */
static void block_art(tiku_surface_t *s, const tiku_doc_t *d, int i,
                      tiku_rect_t r, int scroll,
                      const tiku_doc_paint_t *how)
{
    const tiku_doc_block_t *b = tiku_doc_block(d, i);
    tiku_rect_t box = tiku_doc_block_rect(d, i);

    box.x += r.x;
    box.y += r.y - scroll;
    switch (b->kind) {
    case TIKU_DOC_BLOCK_PRE:
        box.x -= 6;
        box.w += 12;
        tiku_ui_sunken(s, box, TIKU_C_DOC);
        break;
    case TIKU_DOC_BLOCK_QUOTE:
        tiku_vline(s, box.x - 6, box.y, box.h,
                   tiku_ink(TIKU_INK_FAINT));
        break;
    case TIKU_DOC_BLOCK_RULE:
        tiku_hline(s, r.x, box.y + box.h / 2, r.w,
                   tiku_ink(TIKU_INK_FAINT));
        break;
    case TIKU_DOC_BLOCK_NOTICE:
        tiku_hline(s, r.x, box.y, r.w, tiku_ink(TIKU_INK_FAINT));
        if (how && how->cut) {
            tiku_text_line_draw(s, (tiku_rect_t){r.x, box.y + 2, r.w, box.h},
                                TIKU_MEASURE_PLAIN, how->cut,
                                tiku_ink(TIKU_INK_FAINT));
        }
        break;
    case TIKU_DOC_BLOCK_CELL:
        /* One rule under the whole row, drawn by the row's first cell;
         * every cell of a row is as tall as the row. */
        if (b->col == 0) {
            tiku_hline(s, r.x, box.y + box.h - 1, r.w,
                       tiku_ink(TIKU_INK_FAINT));
        }
        break;
    case TIKU_DOC_BLOCK_IMAGE: {
        const tiku_doc_image_t *im = NULL;

        if (b->image == TIKU_DOC_NONE) break;
        im = &d->image[b->image];
        if (im->w > 0 && im->h > 0) {
            tiku_rect_t where = {box.x, box.y, im->w, im->h};

            if (how && how->image && im->src != TIKU_DOC_NONE) {
                how->image(how->image_ctx, tiku_doc_text(d, im->src), where);
            } else {
                tiku_frame(s, where, tiku_ink(TIKU_INK_FAINT));
            }
        } else {
            /* No pixels: the alt text is laid out as words, and framed
             * here. */
            tiku_frame(s, (tiku_rect_t){box.x - 4, box.y - 2, box.w + 8,
                                        box.h + 4},
                       tiku_ink(TIKU_INK_FAINT));
        }
        break;
    }
    default:
        break;
    }
}

void tiku_doc_draw_marked(tiku_surface_t *s, const tiku_doc_t *d,
                         tiku_rect_t r, const tiku_doc_paint_t *how,
                         tiku_doc_mark_fn mark, void *ctx)
{
    int scroll = how ? how->scroll : 0;
    int focus = how ? how->focus_link : TIKU_DOC_NONE;
    int drawn_block = TIKU_DOC_NONE;

    if (!s || !d) return;
    tiku_fill(s, r, TIKU_C_DOC);
    if (!tiku_doc_laid(d)) return;
    tiku_clip_set(s, r);
    for (int i = 0; i < tiku_doc_line_count(d); i++) {
        const tiku_doc_line_t *ln = tiku_doc_line(d, i);
        int y = r.y + (int)ln->y - scroll;

        if (y + ln->h <= r.y) continue;
        /* Not a break: a table lays its cells side by side, so a line
         * past the frame may be followed by one back at the top of the
         * row and still to be drawn. */
        if (y >= r.y + r.h) continue;
        if (ln->block != drawn_block) {
            drawn_block = ln->block;
            block_art(s, d, ln->block, r, scroll, how);
        }
        for (int k = ln->piece0; k < ln->piece0 + ln->npiece; k++) {
            const tiku_doc_piece_t *pc = tiku_doc_piece(d, k);
            const tiku_doc_run_t *run = tiku_doc_run(d, pc->run);
            int x = r.x + ln->x + pc->x;
            tiku_rgb_t ink;

            if (!run) continue;
            if (mark) mark(ctx, s, k, (tiku_rect_t){x, y, pc->w, ln->h});
            ink = tiku_doc_ink(run->ink);
            tiku_text(s, face_of(run->face), x, y + ln->ascent,
                      piece_text(d, pc), ink);
            if (run->link != TIKU_DOC_NONE) {
                /* Underlined as well as tinted: under a theme with no
                 * hue the line is all that tells a link from a word. */
                tiku_hline(s, x, y + ln->ascent + 2, pc->w, ink);
                if (run->link == focus) {
                    tiku_frame(s, (tiku_rect_t){x - 2, y, pc->w + 4, ln->h},
                               TIKU_C_FOCUS);
                }
            }
        }
    }
    tiku_clip_reset(s);
}

void tiku_doc_draw(tiku_surface_t *s, const tiku_doc_t *d, tiku_rect_t r,
                   const tiku_doc_paint_t *how)
{
    tiku_doc_draw_marked(s, d, r, how, NULL, NULL);
}
