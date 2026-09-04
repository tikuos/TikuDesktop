/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_select.c - which poses are selected, and the anchor a
 * range extends from
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/**
 * @brief Select or deselect one pose, stamping when it joined.
 *
 * The stamp is what the slow-second-click rename reads: the click that
 * selects a pose must not also arm its rename, and only the moment
 * selection happened can tell those two clicks apart.
 */
void
pv_set_selected(tiku_trk_pose_t *p, int on)
{
    if (p == NULL) {
        return;
    }
    if (on && !p->selected) {
        p->sel_time = tiku_trk_now();
    }
    p->selected = on ? 1 : 0;
}

void
pv_clear_selection(tiku_trk_poseview_t *v)
{
    int i;

    for (i = 0; i < v->count; i++) {
        v->poses[i].selected = 0;
    }
}

void
tiku_trk_poseview_select_none(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        pv_clear_selection(v);
        v->pivot = -1;              /* PVS-008: empty selection, no pivot */
        v->real_pivot = -1;
        v->last_added = -1;
    }
}

int
tiku_trk_poseview_select_after_removal(tiku_trk_poseview_t *v, int index)
{
    int n;

    if (v == NULL || tiku_trk_poseview_mode(v) != TIKU_TRK_MODE_LIST) {
        /* Icon modes have no "next one down": the poses are where the user
         * put them, and picking a neighbour by list order would select
         * something on the other side of the window. */
        return -1;
    }
    n = tiku_trk_poseview_count(v);
    if (n == 0) {
        tiku_trk_poseview_select_none(v);
        return -1;
    }
    /* The row that has TAKEN the removed one's index -- the next one down
     * before the removal.  Past the end means the last item went, and the
     * new last row is the one that moved up into the gap. */
    if (index < 0) {
        index = 0;
    }
    if (index >= n) {
        index = n - 1;
    }
    tiku_trk_poseview_click(v, index, 0u);
    return index;
}

void
tiku_trk_poseview_select_all(tiku_trk_poseview_t *v)
{
    int i, n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    if (v->single_only) {
        /* Nothing to select ALL of.  The command stays reachable and does
         * the only thing it can mean here (PVS-009). */
        n = (n > 0) ? 1 : 0;
    }
    for (i = 0; i < n; i++) {
        pv_set_selected(tiku_trk_poseview_at(v, i), 1);
    }
    if (n > 0) {
        /* Always the first row, not only when there is no anchor: a pivot
         * left over from an earlier click would make the next shift-click
         * extend from a pose that has nothing to do with this selection. */
        v->pivot = tiku_trk_poseview_at(v, 0)->index;
        v->last_added = tiku_trk_poseview_at(v, n - 1)->index;
    }
}

/** @brief Where pose @p pi appears in the list being shown, or -1. */
int
pv_display_of(const tiku_trk_poseview_t *v, int pi)
{
    int i;

    if (pi < 0) {
        return -1;
    }
    if (!v->filtering) {
        return (pi < v->count) ? pi : -1;
    }
    for (i = 0; i < v->filtered_count; i++) {
        if (v->filtered[i] == pi) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Select the range between two POSE indices.
 *
 * The anchors are pose-array indices, because that is the one space the view
 * keeps: a display index means nothing once a filter changes or a row is
 * removed, and mixing the two is why a pivot could end up pointing at
 * whatever slid into the gap.  The walk is over the DISPLAYED list, so a
 * range never selects a row the user cannot see.
 */
static void
select_range(tiku_trk_poseview_t *v, int from_pose, int to_pose, int on)
{
    int a = pv_display_of(v, from_pose), b = pv_display_of(v, to_pose);
    int i, lo, hi;

    if (a < 0 || b < 0) {
        return;
    }
    lo = (a < b) ? a : b;
    hi = (a < b) ? b : a;
    for (i = lo; i <= hi; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        pv_set_selected(p, on);
    }
}

/** @brief Select an icon-mode rectangle from a point to a pose's location. */
static void
select_icon_range(tiku_trk_poseview_t *v, int from_pose, int x, int y, int on)
{
    tiku_trk_pose_t *anchor;
    int ax = 0, ay = 0, i, n;
    int lox, loy, hix, hiy;

    if (from_pose < 0 || from_pose >= v->count) {
        return;
    }
    anchor = &v->poses[from_pose];
    tiku_trk_poseview_pose_location(v, anchor, &ax, &ay);
    lox = (x < ax) ? x : ax;
    loy = (y < ay) ? y : ay;
    hix = (x < ax) ? ax : x;
    hiy = (y < ay) ? ay : y;
    /* A zero-width or zero-height BRect still catches the poses it touches.
     * Express that explicitly because the integer intersection below uses a
     * half-open far edge (PVS-005). */
    if (hix - lox < 1) { hix = lox + 1; }
    if (hiy - loy < 1) { hiy = loy + 1; }

    n = tiku_trk_poseview_count(v);
    for (i = n - 1; i >= 0; i--) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        if (px + pw > lox && px < hix && py + ph > loy && py < hiy) {
            pv_set_selected(p, on);
        }
    }
}

/** @brief Restore the selection/pivot invariant after every click branch. */
static void
repair_click_pivot(tiku_trk_poseview_t *v, const tiku_trk_pose_t *clicked)
{
    if (tiku_trk_poseview_selection_count(v) == 0) {
        v->pivot = -1;
        v->real_pivot = -1;
        v->last_added = -1;
    } else if (v->pivot < 0 && clicked != NULL) {
        /* Tracker re-anchors on the pose that caused the operation, including
         * when a Shift+Command range removed that pose but left others. */
        v->pivot = clicked->index;
    }
}

void
tiku_trk_poseview_set_multiple(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->single_only = !on;
        if (!on) {
            /* Whatever was already gathered goes down to one: the view is
             * about to be shown as a picker, and leaving three rows lit
             * would offer a choice it will not honour. */
            int i, n = tiku_trk_poseview_count(v), kept = 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

                if (p != NULL && p->selected) {
                    if (kept) {
                        p->selected = 0;
                    } else {
                        kept = 1;
                    }
                }
            }
        }
    }
}

int
tiku_trk_poseview_multiple(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? !v->single_only : 1;
}

void
tiku_trk_poseview_click(tiku_trk_poseview_t *v, int index, unsigned mods)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(v, index);
    int x = 0, y = 0;

    if (p != NULL) {
        int w = 0, h = 0;

        /* Put a coordinate-free click just inside what is actually hit.  An
         * icon-mode pose location is the cell's top-left, which may be empty
         * margin to the left of centred art and would select nothing. */
        tiku_trk_poseview_pose_rect(v, p, &x, &y, &w, &h);
        x++;
        y++;
    }
    tiku_trk_poseview_click_at(v, index, x, y, mods);
}

void
tiku_trk_poseview_click_at(tiku_trk_poseview_t *v, int index, int x, int y,
                           unsigned mods)
{
    tiku_trk_pose_t *p;

    if (v == NULL) {
        return;
    }
    if (v->single_only) {
        /* Every extending modifier is dropped rather than handled, so a
         * modifier click is a plain click (PVS-009). */
        mods &= ~(TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
    }
    if (index < 0) {
        /* PVS-001: clicking empty space clears the whole selection, and with
         * it any rename waiting to start (PVS-015). */
        tiku_trk_poseview_cancel_edit_wait(v);
        tiku_trk_poseview_select_none(v);
        v->active = -1;
        return;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL) {
        return;
    }
    /* PVS-015: a click on a DIFFERENT pose disarms a pending rename; the
     * pose it was armed on is no longer the one being worked with. */
    if (v->edit_clicked_at != 0 && v->edit_pose != p->index) {
        tiku_trk_poseview_cancel_edit_wait(v);
    }
    v->active = p->index;

    if ((mods & TIKU_TRK_MOD_CMD) && (mods & TIKU_TRK_MOD_SHIFT)) {
        /* PVS-007: the range takes the state the CLICKED pose does not
         * already have.  Deselecting unconditionally would make a
         * shift+cmd click on an unselected pose clear a range the user was
         * reaching out to add. */
        int on = !p->selected;

        if (v->pivot >= 0) {
            if (v->mode == TIKU_TRK_MODE_LIST) {
                select_range(v, v->pivot, p->index, on);
            } else {
                select_icon_range(v, v->pivot, x, y, on);
            }
        } else {
            pv_set_selected(p, on);
        }
        if (!on && v->pivot >= 0 && !v->poses[v->pivot].selected) {
            v->pivot = -1;
        }
        if (!on && v->real_pivot >= 0 &&
            !v->poses[v->real_pivot].selected) {
            v->real_pivot = -1;
        }
        repair_click_pivot(v, p);
        return;
    }
    if (mods & TIKU_TRK_MOD_CMD) {
        /* PVS-004: toggle one, leave the rest alone. */
        pv_set_selected(p, !p->selected);
        if (p->selected) {
            v->pivot = p->index;
            v->last_added = p->index;
        } else if (tiku_trk_poseview_selection_count(v) == 0) {
            v->pivot = -1;          /* PVS-008 */
        }
        repair_click_pivot(v, p);
        return;
    }
    if (mods & TIKU_TRK_MOD_SHIFT) {
        /* PVS-005/006: clear first but KEEP the pivot, so successive
         * shift-clicks re-anchor rather than growing monotonically. */
        int pivot = (v->pivot >= 0) ? v->pivot : p->index;
        pv_clear_selection(v);
        v->pivot = pivot;
        if (v->mode == TIKU_TRK_MODE_LIST) {
            select_range(v, pivot, p->index, 1);
        } else {
            select_icon_range(v, pivot, x, y, 1);
        }
        v->last_added = p->index;
        repair_click_pivot(v, p);
        return;
    }
    /* PVS-003/PVS-014: a selected pose stays selected without being stamped
     * again.  For a group that preserves the drag payload; for a single pose
     * it preserves the age that distinguishes a slow second click from the
     * click that originally selected it and lets inline rename arm. */
    if (p->selected && (tiku_trk_poseview_selection_count(v) > 1 ||
                        v->pivot == p->index)) {
        return;
    }
    /* PVS-002: otherwise select just this one and make it the pivot. */
    pv_clear_selection(v);
    pv_set_selected(p, 1);
    v->pivot = p->index;
    v->last_added = p->index;
    repair_click_pivot(v, p);
}

void
tiku_trk_poseview_invert_selection(tiku_trk_poseview_t *v)
{
    int i, n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    /* Both anchors go first.  An inverted selection has no click behind it,
     * so a pivot left over from before would anchor the next shift-click on
     * a row the user never touched. */
    v->pivot = -1;
    v->real_pivot = -1;
    v->last_added = -1;
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

        if (p == NULL) {
            continue;
        }
        pv_set_selected(p, !p->selected);
        if (i == 0 && p->selected) {
            v->pivot = p->index;
        }
    }
}

int
tiku_trk_poseview_select_matching(tiku_trk_poseview_t *v, const char *expr,
                                  tiku_trk_match_t how, int ignore_case,
                                  int invert, char *err, size_t errmax)
{
    int i, n, hits = 0;

    if (v == NULL || expr == NULL) {
        return -1;
    }
    /* Check the expression BEFORE clearing anything: a typo that matched
     * nothing and a typo that was rejected look identical on screen once the
     * selection is already gone. */
    if (how == TIKU_TRK_MATCH_REGEX &&
        !tiku_trk_regex_check(expr, err, errmax)) {
        return -1;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    tiku_trk_poseview_select_none(v);
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int m;

        if (p == NULL) {
            continue;
        }
        m = tiku_trk_match(p->model.name, expr, !ignore_case, how);
        if (m ^ (invert ? 1 : 0)) {
            pv_set_selected(p, 1);
            if (hits == 0) {
                v->pivot = p->index;
                v->active = p->index;
            }
            v->last_added = p->index;
            hits++;
        }
    }
    if (hits > 0) {
        int i, n = tiku_trk_poseview_count(v);

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

            if (p != NULL && p->selected) {
                /* The first thing it found, for the same reason: a window
                 * that selected six rows off screen looks like a window
                 * that did nothing (PVL-054). */
                tiku_trk_poseview_scroll_into_view(v, i, v->visible_rows);
                break;
            }
        }
    }
    return hits;
}

int
tiku_trk_poseview_context_click(tiku_trk_poseview_t *v, int index)
{
    tiku_trk_pose_t *p;

    if (v == NULL) {
        return 0;
    }
    if (index < 0) {
        /* Empty space: the menu is about the FOLDER, so a selection left
         * standing would make its items apply to something off-screen. */
        if (tiku_trk_poseview_selection_count(v) == 0) {
            return 0;
        }
        tiku_trk_poseview_select_none(v);
        v->active = -1;
        return 1;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL) {
        return 0;
    }
    v->active = p->index;
    if (p->selected) {
        return 0;               /* a group keeps its group                */
    }
    pv_clear_selection(v);
    pv_set_selected(p, 1);
    v->pivot = p->index;
    v->last_added = p->index;
    return 1;
}

int
tiku_trk_poseview_draws_selected(const tiku_trk_poseview_t *v,
                                 const tiku_trk_pose_t *p)
{
    if (v == NULL || p == NULL || !p->selected) {
        return 0;
    }
    /* A band in progress paints its sweep even in an inactive window: it is
     * the only feedback the sweep has (PVS-031). */
    return v->selection_visible || v->show_sel_inactive || v->band;
}

void
tiku_trk_poseview_window_activated(tiku_trk_poseview_t *v, int active)
{
    if (v == NULL) {
        return;
    }
    if (v->show_sel_inactive) {
        v->selection_visible = 1;
        return;
    }
    if (active == v->selection_visible) {
        return;
    }
    v->selection_visible = active ? 1 : 0;
    if (!active) {
        /* Parked, not dropped: coming back to a window and shift-clicking
         * must extend from the anchor the user left, not from scratch. */
        v->real_pivot = v->pivot;
        v->pivot = -1;
    } else if (v->real_pivot >= 0) {
        v->pivot = v->real_pivot;
        v->real_pivot = -1;
    }
}

void
tiku_trk_poseview_show_selection_when_inactive(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->show_sel_inactive = on ? 1 : 0;
        if (on) {
            v->selection_visible = 1;
            if (v->real_pivot >= 0) {
                v->pivot = v->real_pivot;
                v->real_pivot = -1;
            }
        }
    }
}

void
tiku_trk_poseview_set_band(tiku_trk_poseview_t *v, int on)
{
    if (v == NULL) {
        return;
    }
    if (!on) {
        if (v->band_seen) {
            /* The sweep is over and the band is not drawn again, so where
             * it was lying is what needs painting.  The frame that ends a
             * sweep usually has nothing else to go on and paints the whole
             * listing regardless; this is for the one that does -- a value
             * arriving mid-sweep leaves that frame partial, and the band
             * would stay on the ground it was over.  No test can force
             * that race, which is why it is written down here. */
            tiku_trk_repaint_add(&v->repaint, v->band_rect);
        }
        v->band_seen = 0;       /* the next sweep is a new one */
    }
    if (on && v->band_was != NULL) {
        int i;

        /* The band toggles against the selection as it was when the sweep
         * began, so the snapshot has to be taken here and not recomputed
         * from the live flags the sweep is busy changing. */
        for (i = 0; i < v->count; i++) {
            v->band_was[i] = (unsigned char)(v->poses[i].selected ? 1 : 0);
        }
    }
    if (!on && v->pivot >= 0 && v->pivot < v->count &&
        !v->poses[v->pivot].selected) {
        /* A pivot the sweep left UNSELECTED is dropped: the next
         * shift-click would otherwise extend from a pose that is no longer
         * part of anything (PVS-029). */
        v->pivot = -1;
    }
    v->band = on ? 1 : 0;
}

int
tiku_trk_poseview_band(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->band : 0;
}

void
tiku_trk_poseview_damage_row(tiku_trk_poseview_t *v, int index)
{
    int rh;

    if (v == NULL || index < 0 || index >= v->count) {
        return;
    }
    rh = (v->row_h > 0) ? v->row_h : 1;
    tiku_trk_repaint_add(&v->repaint,
        (tiku_trk_repaint_rect_t){ 0, (index - v->scroll) * rh,
                                   (v->view_w > 0) ? v->view_w : 1, rh });
}

int
tiku_trk_poseview_repaint_take(tiku_trk_poseview_t *v,
                               tiku_trk_repaint_rect_t *out, int max)
{
    return v != NULL ? tiku_trk_repaint_take(&v->repaint, out, max) : 0;
}

int
tiku_trk_poseview_repaint_take_slide(tiku_trk_poseview_t *v, int *y)
{
    return v != NULL ? tiku_trk_repaint_take_slide(&v->repaint, y) : 0;
}
