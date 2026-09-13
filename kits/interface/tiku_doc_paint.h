/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc_paint.h - a page laid out by tiku_doc, drawn.
 *
 * The painter measures nothing.  Every rectangle it draws in comes from
 * the piece table tiku_doc_layout filled, which is the same table the
 * hit test searches, so a link is where it was drawn and a word is
 * where it was measured.  A picture is put down by the caller, since
 * the interface kit cannot decode one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DOC_PAINT_H_
#define TIKU_DOC_PAINT_H_

#include "tiku_doc.h"
#include "tiku_gfx.h"

/** @brief Put a picture down in @p where.  The caller owns the pixels. */
typedef void (*tiku_doc_draw_image_fn)(void *ctx, const char *src,
                                       tiku_rect_t where);

/** @brief What one drawing of a page is told. */
typedef struct {
    int scroll;                     /* the first row of the page shown  */
    int focus_link;                 /* a link to ring, or TIKU_DOC_NONE */
    /*
     * What to say where the page was cut.  The kit owns no words, so a
     * caller that has none says nothing and the notice is an empty rule
     * rather than a sentence in a language nobody chose.
     */
    const char *cut;
    tiku_doc_draw_image_fn image;
    void *image_ctx;
} tiku_doc_paint_t;

/**
 * @brief Draw the part of @p d that falls inside @p r.
 *
 * Only the lines the scroll shows are drawn, and only the blocks they
 * belong to are decorated.  @p how may be NULL, which draws the top of
 * the page with nothing focused.
 */
void tiku_doc_draw(tiku_surface_t *s, const tiku_doc_t *d, tiku_rect_t r,
                   const tiku_doc_paint_t *how);

/** @brief Optional decoration of a piece, before its text is painted. */
typedef void (*tiku_doc_mark_fn)(void *ctx, tiku_surface_t *s,
                               int piece, tiku_rect_t where);
void tiku_doc_draw_marked(tiku_surface_t *s, const tiku_doc_t *d,
                         tiku_rect_t r, const tiku_doc_paint_t *how,
                         tiku_doc_mark_fn mark, void *ctx);

/** @brief The shade a run of a page means, under the live theme. */
tiku_rgb_t tiku_doc_ink(int doc_ink);

#endif /* TIKU_DOC_PAINT_H_ */
