/* TikuOS interface kit. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TEXT_LAYOUT_PAINT_H_
#define TIKU_TEXT_LAYOUT_PAINT_H_
#include "tiku_text_layout.h"
#include "tiku_gfx.h"
/** @brief Paint a vertically centered, ellipsized line inside r. */
void tiku_text_line_draw(tiku_surface_t *s, tiku_rect_t r, int face,
                         const char *text, tiku_rgb_t ink);
/** @brief Paint top-aligned wrapped text; last visible line is ellipsized. */
void tiku_text_block_draw(tiku_surface_t *s, tiku_rect_t r, int face,
                          const char *text, tiku_rgb_t ink, int gap);
#endif
