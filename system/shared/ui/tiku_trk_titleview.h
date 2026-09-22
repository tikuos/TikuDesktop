/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_titleview.h - the column title strip, and what a drag on it means.
 *
 * Tracker puts every column gesture behind one tracking state: a press picks
 * either a resize or a drag, and from then on the same three calls
 * (moved / up) decide whether what happened was a click, a reorder, a
 * resize or a removal.  The decisions are all thresholds -- a deadline
 * captured at press time, a one-pixel dead zone, a ten-pixel vertical
 * hysteresis, a swap condition with a term whose only job is to stop the
 * columns oscillating -- so they live here as logic rather than inside a
 * mouse handler where they cannot be checked.
 *
 * Nothing here draws.  The caller is told WHAT changed and paints it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TITLEVIEW_H_
#define TIKU_TRK_TITLEVIEW_H_

#include <stdint.h>

#include "tiku_trk_poseview.h"

/** @brief Half-width of the band around a column's right edge (kEdgeSize). */
#define TIKU_TRK_EDGE_SIZE 6

/** @brief How far out of the strip a drag must go to tear a column out. */
#define TIKU_TRK_REMOVE_MARGIN 10

/** @brief What the tracker is doing. */
typedef enum {
    TIKU_TRK_TRACK_NONE = 0,
    TIKU_TRK_TRACK_DRAG,
    TIKU_TRK_TRACK_RESIZE
} tiku_trk_track_t;

/** @brief What the caller has to do about the last event. */
#define TIKU_TRK_TT_NOTHING   0x00u
#define TIKU_TRK_TT_REDRAW    0x01u
#define TIKU_TRK_TT_ACTIVATE  0x02u   /* the click only woke the window   */
#define TIKU_TRK_TT_MENU      0x04u   /* the attributes menu was asked for */
#define TIKU_TRK_TT_SORTED    0x08u   /* the sort keys moved              */
#define TIKU_TRK_TT_LINE      0x10u   /* the guide line is on, at line_x  */
#define TIKU_TRK_TT_NO_LINE   0x20u   /* and now it is off                */
#define TIKU_TRK_TT_CURSOR    0x40u   /* the pointer is over a resize edge */

typedef struct tiku_trk_titleview {
    tiku_trk_poseview_t *view;        /* whose columns these are         */
    int              height;          /* the strip's height              */
    tiku_trk_track_t track;           /* what the press is doing: none,  */
                                      /* a resize or a drag              */
    int              field;           /* the column being tracked        */
    int              first_x, first_y;
    int64_t          past_click;      /* the absolute click deadline     */
    int              has_moved;       /* the pointer left the press point, */
                                      /* so a release is not a click     */
    int              grab;            /* resize: right edge minus mouse  */
    int              drag_base;       /* drag: the raw x at press        */
    int              last_x;          /* where the pointer is now        */
    int              line_x;          /* guide line position, or -1      */
    /* A column dragged out of the strip is REMOVED, and what it was is
     * kept here -- it is the only way back if the drag returns. */
    int              removed;         /* a column is out of the strip    */
    int              removed_field, removed_width, removed_align;
    /* The previous divider click, for the double-click auto-fit. */
    int              prev_field;      /* the divider it landed on, or -1 */
    int64_t          prev_click_at;   /* when, in microseconds; 0 never  */
} tiku_trk_titleview_t;

/** @brief A strip over @p v, @p height tall, tracking nothing yet. */
void tiku_trk_titleview_init(tiku_trk_titleview_t *t,
                             tiku_trk_poseview_t *v, int height);

/**
 * @brief The title rectangle of @p field, in content coordinates.
 *
 * Wider than the column: it starts TIKU_TRK_COL_LEFT_MARGIN before the data
 * (the gap the sort mark lives in) and ends TIKU_TRK_COL_RIGHT_MARGIN past
 * it, which is why hit tests cannot be done against the column's own width.
 */
int tiku_trk_titleview_bounds(const tiku_trk_titleview_t *t, int field,
                              int *left, int *right);

/** @brief The first title containing @p x, or -1. */
int tiku_trk_titleview_title_at(const tiku_trk_titleview_t *t, int x);

/**
 * @brief The first title whose resize edge contains @p x, or -1.
 *
 * The band straddles the right edge by TIKU_TRK_EDGE_SIZE on BOTH sides, so
 * it reaches into the next title -- grabbing just inside a column's left
 * edge resizes the one before it, which is what makes a divider feel like a
 * divider rather than like two adjacent columns.
 */
int tiku_trk_titleview_edge_at(const tiku_trk_titleview_t *t, int x);

/**
 * @brief A press on the strip.
 *
 * @param buttons 1 primary, 2 secondary, 3 tertiary.
 * @param active  Whether the window is active; an inactive one only wakes.
 * @return TIKU_TRK_TT_* flags.
 */
unsigned tiku_trk_titleview_mouse_down(tiku_trk_titleview_t *t, int x, int y,
                                       int buttons, int active, int64_t now);

/** @brief Motion, tracking or not. */
unsigned tiku_trk_titleview_mouse_moved(tiku_trk_titleview_t *t, int x, int y,
                                        int64_t now);

/**
 * @brief Release: the click-or-drag decision, and what it means.
 *
 * @param shift Whether Shift was held AT THE RELEASE, which is when Tracker
 *              reads it -- a modifier taken at press time would make the
 *              sort depend on a key the user has since let go of.
 */
unsigned tiku_trk_titleview_mouse_up(tiku_trk_titleview_t *t, int x, int y,
                                     int shift, int64_t now);

/** @brief Whether a gesture is in progress, and which. */
tiku_trk_track_t tiku_trk_titleview_tracking(const tiku_trk_titleview_t *t);

/**
 * @brief The column being dragged, for the pressed and outlined look.
 *
 * @param offset Receives how far right of its own place the outline sits.
 */
int tiku_trk_titleview_dragged(const tiku_trk_titleview_t *t, int *offset);

#endif /* TIKU_TRK_TITLEVIEW_H_ */
