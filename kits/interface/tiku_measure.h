/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_measure.h - how wide text is, asked without a font in hand.
 *
 * A control's behaviour lays rows and tabs out by the size of text and
 * does not know how text is drawn: on the desktop the answer comes from
 * the baked faces (tiku_measure_font.c), on a board from a table of their
 * metrics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEASURE_H_
#define TIKU_MEASURE_H_

/*
 * Face numbers, as the display list numbers them (tiku_dl_face_t).  On the
 * desktop a number of 10 or more is the plain face nearest that many
 * pixels.
 */
#define TIKU_MEASURE_PLAIN 0
#define TIKU_MEASURE_BOLD  1
#define TIKU_MEASURE_MONO  2

/** @brief How wide @p text is in @p face, in pixels. */
int tiku_measure_width(int face, const char *text);

/** @brief The line height of @p face. */
int tiku_measure_height(int face);

/** @brief The ascent of @p face. */
int tiku_measure_ascent(int face);

/**
 * @brief The face for a heading of @p level.
 *
 * On the desktop, levels 1 and 2 are the plain face 4 and 2 pixels over
 * the body size, at most 16; a heading that would be no bigger than the
 * body, and every heading on a board, is the bold face.
 */
int tiku_measure_heading(int level);

/** @brief The height of a list row. */
int tiku_measure_row_h(void);

/** @brief The width of a stepper's square half, the - or the +. */
int tiku_measure_step_w(void);

#endif /* TIKU_MEASURE_H_ */
