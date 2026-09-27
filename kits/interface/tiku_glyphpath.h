/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_glyphpath.h - one outline, however it was described.
 *
 * TrueType describes a glyph in quadratics and CFF in cubics, but a
 * rasteriser wants edges.  Both readers push their curves in here, in
 * device space (pixels, y downwards from the baseline), and this
 * flattens them by how big they are on the screen, so a curve is smooth
 * at forty pixels without being ten segments of nothing at ten.
 *
 * Coverage is signed area accumulated per cell and resolved with a
 * prefix sum, as the icon renderer does it: computed coverage has no
 * jaggies to average away, which matters most at interface sizes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GLYPHPATH_H_
#define TIKU_GLYPHPATH_H_

typedef struct tiku_path tiku_path_t;

/**
 * @brief How font units become device pixels, vertically grid-fitted.
 *
 * X is scaled and left alone, so advances and letter spacing stay as
 * smooth as the designer drew them.  Y is scaled and then nudged, so the
 * lines a reader's eye follows, the baseline and the tops of the
 * lowercase and the capitals, land on pixel boundaries instead of
 * straddling two rows at half strength: almost all of what hinting buys
 * at 10 and 12 px.  With @c zones at 0 this is a plain scale.
 */
#define TIKU_HINT_ZONES 3

typedef struct {
    float scale;
    int   zones;
    float from[TIKU_HINT_ZONES];   /* font units, ascending */
    float shift[TIKU_HINT_ZONES];  /* device pixels to nudge by */
} tiku_hint_t;

/** @brief The device y for font-unit @p y, sign flipped for the screen. */
float tiku_hint_y(const tiku_hint_t *hint, float y);

/** @brief The device x for font-unit @p x. */
float tiku_hint_x(const tiku_hint_t *hint, float x);

tiku_path_t *tiku_path_new(void);
void tiku_path_free(tiku_path_t *path);

/** @brief Forget the outline, keeping the room it was built in. */
void tiku_path_reset(tiku_path_t *path);

/**
 * @brief Whether anything has gone wrong since the last reset.
 *
 * Every builder call is allowed to fail silently -- a reader deep in a
 * charstring has nowhere useful to report to -- so failure is asked
 * about once, at the end.
 */
int tiku_path_failed(const tiku_path_t *path);

/**
 * @brief Apply @p m, a PDF-style matrix [a b c d e f], to every point
 *        pushed from now on: x' = a x + c y + e, y' = b x + d y + f.
 *
 * For an outline drawn at a size and slant the hint alone cannot say;
 * NULL takes it off again.  A reset keeps it.
 */
void tiku_path_transform(tiku_path_t *path, const float *m);

/** @brief The builders: a new contour, and a line, each to (x, y). */
void tiku_path_move(tiku_path_t *path, float x, float y);
void tiku_path_line(tiku_path_t *path, float x, float y);
/** @brief A quadratic through one control point to (x, y). */
void tiku_path_quad(tiku_path_t *path, float cx, float cy,
                         float x, float y);
/** @brief A cubic through two control points to (x, y). */
void tiku_path_cubic(tiku_path_t *path, float ax, float ay,
                          float bx, float by, float x, float y);

/** @brief Shut the contour back to where the last move put the pen. */
void tiku_path_close(tiku_path_t *path);

/**
 * @brief The whole outline's extent, in whole pixels, with a margin.
 *
 * The margin is the accumulator's: an edge writes into the cell past the
 * one it covers, and that cell has to be inside the map for the prefix
 * sum to resolve the last column of ink.
 */
void tiku_path_bounds(const tiku_path_t *path, int *x0, int *y0,
                           int *w, int *h);

/**
 * @brief Coverage for the box @p x0,@p y0,@p w,@p h.
 *
 * @param darken Nonzero to lift partial coverage toward ink, so a stem
 *               reads solid at small sizes rather than washed out -- the
 *               stem darkening a hinting library does.  Zero draws the
 *               honest, linear coverage.
 * @return w*h bytes the caller frees, or NULL.
 */
unsigned char *tiku_path_render(const tiku_path_t *path, int x0,
                                     int y0, int w, int h, int darken);

#endif /* TIKU_GLYPHPATH_H_ */
