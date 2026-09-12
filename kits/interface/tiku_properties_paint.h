/* TikuOS interface kit. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_PROPERTIES_PAINT_H_
#define TIKU_PROPERTIES_PAINT_H_
#include "tiku_properties.h"
#include "tiku_gfx.h"
/** @brief A two-column table; focused is a row index or -1.
 * Selection is independent from keyboard focus. Headings are not selectable.
 * Long labels and values elide; unavailable values use subdued ink.
 */
void tiku_properties_draw(const tiku_properties_t *p, tiku_surface_t *s,
                           tiku_rect_t body, int focused);
#endif
