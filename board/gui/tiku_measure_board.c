/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_measure_board.c - the board answers from the baked metrics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_measure.h"

#include "tiku_draw.h"

int
tiku_measure_width(int face, const char *text)
{
    return tiku_draw_text_width((tiku_draw_face_t)face, text);
}

int
tiku_measure_height(int face)
{
    return tiku_draw_text_height((tiku_draw_face_t)face);
}

int
tiku_measure_ascent(int face)
{
    return tiku_draw_text_ascent((tiku_draw_face_t)face);
}

int
tiku_measure_row_h(void)
{
    return tiku_draw_text_height(TIKU_DRAW_PLAIN) + 7;
}

int
tiku_measure_step_w(void)
{
    return tiku_draw_text_height(TIKU_DRAW_PLAIN) + 11;
}
