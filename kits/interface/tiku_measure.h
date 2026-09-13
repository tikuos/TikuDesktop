/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_measure.h - how wide words are, asked without a font in hand.
 *
 * A control's behaviour lays rows and tabs out by the size of text and
 * must not know how text is drawn: on the desktop the answer comes from
 * the baked faces, on a board from a table of their metrics.  The faces
 * are numbered as the display list numbers them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEASURE_H_
#define TIKU_MEASURE_H_

#define TIKU_MEASURE_PLAIN 0
#define TIKU_MEASURE_BOLD  1
#define TIKU_MEASURE_MONO  2

/** @brief How wide @p text is in @p face. */
int tiku_measure_width(int face, const char *text);
/** @brief The face's line height, and its ascent. */
int tiku_measure_height(int face);
int tiku_measure_ascent(int face);
/** @brief A heading face supported by this end; boards may return bold. */
int tiku_measure_heading(int level);
/** @brief A list row's height, and a stepper's square half. */
int tiku_measure_row_h(void);
int tiku_measure_step_w(void);

#endif /* TIKU_MEASURE_H_ */
