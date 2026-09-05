/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_measure_font.c - the desktop answers from its baked faces.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_measure.h"

#include "tiku_font.h"
#include "tiku_ui.h"

/** @brief The face numbered @p face. */
static const tiku_font_t *
face_of(int face)
{
    switch (face) {
    case TIKU_MEASURE_BOLD: return tiku_font_bold();
    case TIKU_MEASURE_MONO: return tiku_font_mono(0);
    default:                return tiku_font_plain();
    }
}

int
tiku_measure_width(int face, const char *text)
{
    return tiku_text_width(face_of(face), text);
}

int
tiku_measure_height(int face)
{
    return face_of(face)->height;
}

int
tiku_measure_ascent(int face)
{
    return face_of(face)->ascent;
}

int
tiku_measure_row_h(void)
{
    return tiku_ui_row_h();
}

int
tiku_measure_step_w(void)
{
    return tiku_ui_step_w();
}
