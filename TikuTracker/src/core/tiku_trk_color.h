/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_COLOR_H_
#define TIKU_TRK_COLOR_H_

#include <stdint.h>

typedef struct { uint8_t r, g, b, a; } tiku_trk_color_t;
/**
 * @brief Read an RGBA colour from the first four bytes at @p data; an alpha
 * of 0 reads as opaque.  @return -1 when @p size is short of four.
 */
int tiku_trk_color_parse(const void *data, unsigned size, tiku_trk_color_t *out);
/** @brief Make @p desktop show @p color; either NULL leaves it alone. */
void tiku_trk_color_set(tiku_trk_color_t *desktop, const tiku_trk_color_t *color);

#endif
