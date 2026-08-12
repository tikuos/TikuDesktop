/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag.c - drag arming, payload and drop rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_drag.h"

#include <stdio.h>
#include <string.h>

/** @brief The parent directory of @p path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= max) {
        n = max - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

void
tiku_trk_drag_reset(tiku_trk_drag_t *d)
{
    if (d != NULL) {
        memset(d, 0, sizeof *d);
    }
}

void
tiku_trk_drag_arm(tiku_trk_drag_t *d, int x, int y, unsigned buttons,
                  unsigned mods, int clicks, void *src_win,
                  const char *src_dir)
{
    if (d == NULL) {
        return;
    }
    tiku_trk_drag_reset(d);
    /* A second click is the start of a double-click, and a double-click
     * opens.  Arming on it would make every impatient open a drag. */
    if (clicks != 1) {
        return;
    }
    d->armed = 1;
    d->x0 = x;
    d->y0 = y;
    d->x = x;
    d->y = y;
    d->buttons = buttons;
    d->mods = mods;
    d->src_win = src_win;
    if (src_dir != NULL) {
        snprintf(d->src_dir, sizeof d->src_dir, "%s", src_dir);
    }
}

int
tiku_trk_drag_add(tiku_trk_drag_t *d, const char *path)
{
    if (d == NULL || path == NULL || path[0] == '\0' ||
        d->count >= TIKU_TRK_DRAG_MAX) {
        return -1;
    }
    snprintf(d->path[d->count], sizeof d->path[0], "%s", path);
    d->count++;
    return 0;
}

int
tiku_trk_drag_moved(tiku_trk_drag_t *d, int x, int y)
{
    int dx, dy;

    if (d == NULL || !d->armed) {
        return 0;
    }
    d->x = x;
    d->y = y;
    if (d->active) {
        return 0;                   /* already started; just tracking */
    }
    /* Squared, against the squared threshold: the comparison is the same
     * one and costs no square root. */
    dx = x - d->x0;
    dy = y - d->y0;
    if (dx * dx + dy * dy < TIKU_TRK_DRAG_SLOP * TIKU_TRK_DRAG_SLOP) {
        return 0;
    }
    /* Nothing to carry is not a drag.  A press on empty space arms the
     * rubber band, which is a different gesture with the same opening. */
    if (d->count == 0) {
        return 0;
    }
    d->active = 1;
    return 1;
}

int
tiku_trk_drag_active(const tiku_trk_drag_t *d)
{
    return (d != NULL && d->active);
}

int
tiku_trk_drag_carries(const tiku_trk_drag_t *d, const char *path)
{
    int i;

    if (d == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < d->count; i++) {
        if (strcmp(d->path[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_drag_can_drop(const tiku_trk_drag_t *d, const tiku_trk_model_t *over)
{
    if (d == NULL || over == NULL || !d->active) {
        return 0;
    }
    /* A pose that is itself in the drag is not a target: dropping the
     * selection on one of its own members means the window behind it
     * (PVS-042). */
    if (tiku_trk_drag_carries(d, over->path)) {
        return 0;
    }
    if (!tiku_trk_model_is_container(over)) {
        return 0;
    }
    /* A container that will not accept new entries -- a read-only store, a
     * device node's virtual directory -- is not a target either, and saying
     * so during the hover is better than refusing after the drop. */
    return tiku_trk_model_is_mutable_container(over);
}

tiku_trk_drop_t
tiku_trk_drag_action(const tiku_trk_drag_t *d, const char *dst_dir,
                     unsigned mods, int same_volume)
{
    char from[TIKU_TRK_PATH_MAX];

    if (d == NULL || dst_dir == NULL || d->count == 0) {
        return TIKU_TRK_DROP_NOTHING;
    }
    /* Control asks for a link whatever else is true, and Option forces a
     * copy: both are read at the DROP, not at the press, because the user
     * decides what the gesture means while it is in the air. */
    if ((mods & TIKU_TRK_DRAG_MOD_LINK) != 0u) {
        return TIKU_TRK_DROP_LINK;
    }
    /* Back where it started is not an operation.  Checked against the
     * item's own parent rather than the drag's source folder, so a drag of
     * items gathered from several places still does the right thing for
     * each. */
    parent_of(d->path[0], from, sizeof from);
    if (strcmp(from, dst_dir) == 0) {
        return ((mods & TIKU_TRK_DRAG_MOD_COPY) != 0u) ? TIKU_TRK_DROP_COPY
                                                       : TIKU_TRK_DROP_NOTHING;
    }
    if ((mods & TIKU_TRK_DRAG_MOD_COPY) != 0u) {
        return TIKU_TRK_DROP_COPY;
    }
    /* Same volume moves, different volume copies: the bytes only travel
     * when they have to. */
    return same_volume ? TIKU_TRK_DROP_MOVE : TIKU_TRK_DROP_COPY;
}
