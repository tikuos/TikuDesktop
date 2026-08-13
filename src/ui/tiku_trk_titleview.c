/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_titleview.c - the column title strip's tracking state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_titleview.h"

#include <string.h>

void
tiku_trk_titleview_init(tiku_trk_titleview_t *t, tiku_trk_poseview_t *v,
                        int height)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof *t);
    t->view = v;
    t->height = height;
    t->field = -1;
    t->line_x = -1;
    t->prev_field = -1;
}

int
tiku_trk_titleview_bounds(const tiku_trk_titleview_t *t, int field, int *left,
                          int *right)
{
    const tiku_trk_column_t *cols;
    int n, i;

    if (t == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_columns(t->view, &cols);
    for (i = 0; i < n; i++) {
        if (cols[i].field != field) {
            continue;
        }
        if (left != NULL) {
            *left = cols[i].offset - TIKU_TRK_COL_LEFT_MARGIN;
        }
        if (right != NULL) {
            *right = cols[i].offset + cols[i].width +
                     TIKU_TRK_COL_RIGHT_MARGIN;
        }
        return 1;
    }
    return 0;
}

int
tiku_trk_titleview_title_at(const tiku_trk_titleview_t *t, int x)
{
    const tiku_trk_column_t *cols;
    int n, i;

    if (t == NULL) {
        return -1;
    }
    n = tiku_trk_poseview_columns(t->view, &cols);
    for (i = 0; i < n; i++) {
        int l = cols[i].offset - TIKU_TRK_COL_LEFT_MARGIN;
        int r = cols[i].offset + cols[i].width + TIKU_TRK_COL_RIGHT_MARGIN;

        if (x >= l && x <= r) {
            return cols[i].field;
        }
    }
    return -1;
}

int
tiku_trk_titleview_edge_at(const tiku_trk_titleview_t *t, int x)
{
    const tiku_trk_column_t *cols;
    int n, i;

    if (t == NULL) {
        return -1;
    }
    n = tiku_trk_poseview_columns(t->view, &cols);
    /* First match in list order, as Tracker's aggregate does: where two
     * bands overlap the LEFT column wins, so the divider always resizes the
     * column that owns it. */
    for (i = 0; i < n; i++) {
        int r = cols[i].offset + cols[i].width + TIKU_TRK_COL_RIGHT_MARGIN;

        if (x >= r - TIKU_TRK_EDGE_SIZE && x <= r + TIKU_TRK_EDGE_SIZE) {
            return cols[i].field;
        }
    }
    return -1;
}

tiku_trk_track_t
tiku_trk_titleview_tracking(const tiku_trk_titleview_t *t)
{
    return (t != NULL) ? t->track : TIKU_TRK_TRACK_NONE;
}

int
tiku_trk_titleview_dragged(const tiku_trk_titleview_t *t, int *offset)
{
    if (t == NULL || t->track != TIKU_TRK_TRACK_DRAG) {
        return -1;
    }
    if (offset != NULL) {
        *offset = t->last_x - t->drag_base;
    }
    return t->field;
}

unsigned
tiku_trk_titleview_mouse_down(tiku_trk_titleview_t *t, int x, int y,
                              int buttons, int active, int64_t now)
{
    int edge, title;

    if (t == NULL || t->view == NULL) {
        return TIKU_TRK_TT_NOTHING;
    }
    if (!active) {
        /* TS-015: the first click on an inactive window is spent waking it.
         * Sorting as well would make a window the user was only bringing
         * forward re-order itself under the pointer. */
        return TIKU_TRK_TT_ACTIVATE | TIKU_TRK_TT_REDRAW;
    }
    edge = tiku_trk_titleview_edge_at(t, x);
    title = tiku_trk_titleview_title_at(t, x);
    if (buttons == 2) {
        /* The secondary button asks for the attributes menu wherever it
         * lands, so a right-click on a divider never starts a resize. */
        return TIKU_TRK_TT_MENU;
    }
    if (edge >= 0) {
        /* PVL-037: a second press on the same divider inside the
         * double-click window fits the column to its widest value, and the
         * middle button forces the same with no prior click and no timing. */
        /* Any previous divider click inside the window, not necessarily
         * this divider's: what the gesture means is "that was a double
         * click", and the pointer has to be on a divider now either way. */
        int fit = (buttons == 3) ||
                  (t->prev_field >= 0 && t->prev_click_at != 0 &&
                   now - t->prev_click_at < TIKU_TRK_CLICK_SPEED_US);

        t->prev_field = edge;
        t->prev_click_at = now;
        if (fit && tiku_trk_poseview_fit_column(t->view, edge)) {
            /* It fitted, so the press is spent: no drag follows.  A fit
             * that changed nothing falls through into one instead, which is
             * what stops a double-click on an already-fitted column from
             * doing nothing at all. */
            return TIKU_TRK_TT_REDRAW;
        }
        t->track = TIKU_TRK_TRACK_RESIZE;
        t->field = edge;
        {
            const tiku_trk_column_t *cols;
            int n, i;

            n = tiku_trk_poseview_columns(t->view, &cols);
            for (i = 0; i < n; i++) {
                if (cols[i].field == edge) {
                    /* The grabbed edge stays pinned under the pointer for
                     * the whole drag; without this the column jumps by
                     * however far into the band the press landed. */
                    t->grab = (cols[i].offset + cols[i].width) - x;
                    t->line_x = cols[i].offset + cols[i].width +
                                TIKU_TRK_COL_RIGHT_MARGIN;
                    break;
                }
            }
        }
    } else if (title >= 0) {
        t->track = TIKU_TRK_TRACK_DRAG;
        t->field = title;
        t->drag_base = x;
        t->removed = 0;
        t->line_x = -1;
    } else {
        return TIKU_TRK_TT_NOTHING;    /* neither: nothing to track       */
    }
    t->first_x = x;
    t->first_y = y;
    t->last_x = x;
    /* The deadline is captured HERE, not measured at the release: the same
     * value gates the dead zone during motion, and two clocks would let a
     * gesture be a click by one test and a drag by the other. */
    t->past_click = now + TIKU_TRK_CLICK_SPEED_US;
    t->has_moved = 0;
    return TIKU_TRK_TT_REDRAW |
           ((t->track == TIKU_TRK_TRACK_RESIZE) ? TIKU_TRK_TT_LINE : 0u);
}

/** @brief The resize half of a tracked move. */
static unsigned
resize_moved(tiku_trk_titleview_t *t, int x)
{
    const tiku_trk_column_t *cols;
    int n, i, width = 0, off = 0;

    n = tiku_trk_poseview_columns(t->view, &cols);
    for (i = 0; i < n; i++) {
        if (cols[i].field == t->field) {
            off = cols[i].offset;
            break;
        }
    }
    width = x + t->grab - off;
    if (width < TIKU_TRK_COL_MIN_WIDTH) {
        width = TIKU_TRK_COL_MIN_WIDTH;
    }
    (void)tiku_trk_poseview_resize_column(t->view, t->field, width);
    t->line_x = off + width + TIKU_TRK_COL_RIGHT_MARGIN;
    return TIKU_TRK_TT_REDRAW | TIKU_TRK_TT_LINE;
}

/** @brief The drag half: reorder inside the strip, removal outside it. */
static unsigned
drag_moved(tiku_trk_titleview_t *t, int x, int y)
{
    int inside;

    /* The band is grown VERTICALLY only, so a title dragged along the strip
     * is being re-ordered however far it goes; it takes ten pixels of
     * up-or-down to mean "take this column away". */
    inside = (y >= -TIKU_TRK_REMOVE_MARGIN &&
              y <= t->height + TIKU_TRK_REMOVE_MARGIN);
    if (!inside) {
        if (!t->removed) {
            const tiku_trk_column_t *cols;
            int n, i, old_l = 0;

            n = tiku_trk_poseview_columns(t->view, &cols);
            for (i = 0; i < n; i++) {
                if (cols[i].field == t->field) {
                    t->removed_field = cols[i].field;
                    t->removed_width = cols[i].width;
                    t->removed_align = cols[i].alignment;
                    break;
                }
            }
            (void)tiku_trk_titleview_bounds(t, t->field, &old_l, NULL);
            if (tiku_trk_poseview_toggle_column(t->view, t->field)) {
                /* Kept, not thrown: dragging back in has to restore the
                 * column that left, not a fresh one with default width.
                 * The origin is rebased by the title's old left edge, so
                 * the outline stays under the pointer that is carrying it. */
                t->removed = 1;
                t->drag_base -= old_l;
            }
        }
        return TIKU_TRK_TT_REDRAW;
    }
    if (t->removed) {
        /* Re-entry is tested against the UNGROWN strip: leaving at ten
         * pixels and returning at zero is the hysteresis, and testing both
         * against the same edge would make the column flicker in and out
         * along it. */
        if (y < 0 || y > t->height) {
            return TIKU_TRK_TT_NOTHING;
        }
        if (tiku_trk_poseview_toggle_column(t->view, t->removed_field)) {
            int over = tiku_trk_titleview_title_at(t, x);
            int new_l = 0;

            (void)tiku_trk_poseview_resize_column(t->view, t->removed_field,
                                                  t->removed_width);
            if (over >= 0 && over != t->removed_field) {
                /* AFTER the header it was dropped on: landing before it
                 * would make the last position unreachable and the first
                 * reachable only by accident. */
                (void)tiku_trk_poseview_column_after(t->view,
                                                     t->removed_field, over);
            }
            t->removed = 0;
            t->field = t->removed_field;
            if (tiku_trk_titleview_bounds(t, t->field, &new_l, NULL)) {
                /* The outline is drawn relative to the press point, so the
                 * origin follows the title wherever re-insertion put it --
                 * otherwise the restored title is painted a column width
                 * away from the pointer holding it. */
                t->drag_base += new_l;
            }
        }
        return TIKU_TRK_TT_REDRAW;
    }
    {
        int over, self_l, self_r, over_l, over_r;

        /* Reordering needs the pointer ON the strip.  Between the strip and
         * the removal band it is neither: the outline follows the pointer
         * and the columns stay put, which is what stops a drag that has
         * slipped into the list from silently re-ordering. */
        if (y < 0 || y > t->height) {
            return TIKU_TRK_TT_REDRAW;
        }
        over = tiku_trk_titleview_title_at(t, x);
        if (over < 0 || over == t->field) {
            return TIKU_TRK_TT_REDRAW;
        }
        if (!tiku_trk_titleview_bounds(t, t->field, &self_l, &self_r) ||
            !tiku_trk_titleview_bounds(t, over, &over_l, &over_r)) {
            return TIKU_TRK_TT_REDRAW;
        }
        /* Moving right: the neighbour has to be clear of the dragged title.
         * Moving left: the pointer must still be within one dragged-title
         * width of the neighbour's left edge -- that second term is the
         * only thing stopping the pair from swapping back and forth on
         * every motion event once they have swapped once. */
        if (over_l >= self_r || x < over_l + (self_r - self_l)) {
            if (tiku_trk_poseview_move_column(t->view, t->field, over)) {
                int new_l;

                if (tiku_trk_titleview_bounds(t, t->field, &new_l, NULL)) {
                    /* The outline is drawn from the press point, so the
                     * base is rebased by however far the title itself
                     * moved; otherwise it jumps by a column width. */
                    t->drag_base += new_l - self_l;
                }
                return TIKU_TRK_TT_REDRAW;
            }
        }
    }
    return TIKU_TRK_TT_REDRAW;
}

unsigned
tiku_trk_titleview_mouse_moved(tiku_trk_titleview_t *t, int x, int y,
                               int64_t now)
{
    if (t == NULL || t->view == NULL) {
        return TIKU_TRK_TT_NOTHING;
    }
    if (t->track == TIKU_TRK_TRACK_NONE) {
        /* TS-013: the resize cursor is a hint about where the pointer is,
         * and it is only offered while nothing is being tracked -- during a
         * drag the cursor stays as it was. */
        return (tiku_trk_titleview_edge_at(t, x) >= 0) ? TIKU_TRK_TT_CURSOR
                                                       : TIKU_TRK_TT_NOTHING;
    }
    if (!t->has_moved && now < t->past_click) {
        /* One pixel of slop, inclusive, and only until the deadline: a hand
         * that shakes during a click must not turn it into a drag, and a
         * pointer that has not moved at all after the deadline must not
         * stay a click for ever. */
        if (x >= t->first_x - 1 && x <= t->first_x + 1 &&
            y >= t->first_y - 1 && y <= t->first_y + 1) {
            return TIKU_TRK_TT_NOTHING;
        }
    }
    t->has_moved = 1;
    t->last_x = x;
    if (t->track == TIKU_TRK_TRACK_RESIZE) {
        return resize_moved(t, x);
    }
    return drag_moved(t, x, y);
}

unsigned
tiku_trk_titleview_mouse_up(tiku_trk_titleview_t *t, int x, int y, int shift,
                            int64_t now)
{
    unsigned flags = TIKU_TRK_TT_REDRAW;
    int clicked;

    (void)x;
    (void)y;
    if (t == NULL || t->track == TIKU_TRK_TRACK_NONE) {
        return TIKU_TRK_TT_NOTHING;
    }
    /* A click is short AND still.  Either test alone lets a slow press that
     * never moved sort the column, or a fast flick reorder it. */
    clicked = (now <= t->past_click && !t->has_moved);
    if (clicked && t->track == TIKU_TRK_TRACK_DRAG) {
        tiku_trk_poseview_sort_click(t->view, t->field, shift);
        flags |= TIKU_TRK_TT_SORTED;
    }
    if (t->track == TIKU_TRK_TRACK_RESIZE) {
        flags |= TIKU_TRK_TT_NO_LINE;
    }
    /* A column dragged out and released outside the strip stays out: the
     * release is not an undo, and restoring it would make the gesture
     * impossible to complete. */
    t->track = TIKU_TRK_TRACK_NONE;
    t->field = -1;
    t->line_x = -1;
    t->removed = 0;
    t->has_moved = 0;
    return flags;
}
