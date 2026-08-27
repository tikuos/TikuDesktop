/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_COLOR_H_
#define TIKU_TRK_COLOR_H_

#include <stdint.h>

typedef struct { uint8_t r, g, b, a; } tiku_trk_color_t;
int tiku_trk_color_parse(const void *data, unsigned size, tiku_trk_color_t *out);
void tiku_trk_color_set(tiku_trk_color_t *desktop, const tiku_trk_color_t *color);

#endif
