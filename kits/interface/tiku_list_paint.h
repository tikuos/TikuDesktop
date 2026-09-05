/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_list_paint.h - the list's rows, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LIST_PAINT_H_
#define TIKU_LIST_PAINT_H_

#include "tiku_list.h"
#include "tiku_gfx.h"

/**
 * @brief Draw the rows in @p body as plain named rows.
 *
 * The simple look: one line of text a row, selected rows in the
 * selection colours, through the kit's row painter, so each row crosses
 * the wire as the row it is and a reader is told its name and whether
 * it is picked.  A caller that wants columns, art or a second line draws
 * from tiku_list_row_rect() instead.  A heading crosses as words rather
 * than as a row: a reader counting rows is not told about furniture.
 */
void tiku_list_draw(const tiku_list_t *l, tiku_surface_t *s,
                    tiku_rect_t body, tiku_list_name_fn name, void *ctx);

#endif /* TIKU_LIST_PAINT_H_ */
