/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskicons.c - Desktop mode, placement and the union source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskicons.h"
#include "tiku_trk_poseview.h"

#include <stdio.h>
#include <string.h>

int
tiku_trk_deskicons_mode(int saved)
{
    /* Coerced rather than refused at save time: a state file written by
     * another build is not ours to reject, and the Desktop has to come up
     * either way. */
    return (saved == TIKU_TRK_MODE_LIST) ? TIKU_TRK_MODE_ICON : saved;
}

/** @brief Whether two rectangles touch at all. */
static int
overlaps(tiku_trk_rect_t a, tiku_trk_rect_t b)
{
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

/** @brief Grow @p r by @p n on every side. */
static tiku_trk_rect_t
grow(tiku_trk_rect_t r, int n)
{
    r.x -= n;
    r.y -= n;
    r.w += 2 * n;
    r.h += 2 * n;
    return r;
}

int
tiku_trk_deskicons_valid(tiku_trk_rect_t r, tiku_trk_rect_t screen,
                         tiku_trk_rect_t deskbar,
                         const tiku_trk_rect_t *others, int nothers)
{
    int i;

    /* Wholly on screen: an icon half off the edge is an icon the user
     * cannot pick up, which is worse than one in an awkward place. */
    if (r.x < screen.x || r.y < screen.y ||
        r.x + r.w > screen.x + screen.w ||
        r.y + r.h > screen.y + screen.h) {
        return 0;
    }
    if (deskbar.w > 0 && deskbar.h > 0 &&
        overlaps(r, grow(deskbar, TIKU_TRK_DESK_BAR_MARGIN))) {
        return 0;
    }
    for (i = 0; i < nothers; i++) {
        if (overlaps(r, grow(others[i], TIKU_TRK_DESK_OTHER_MARGIN))) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_deskicons_pull_back(tiku_trk_rect_t *r, tiku_trk_rect_t screen)
{
    int ox, oy;

    if (r == NULL) {
        return 0;
    }
    ox = r->x;
    oy = r->y;
    if (r->x + r->w > screen.x + screen.w) {
        r->x = screen.x + screen.w - r->w;
    }
    if (r->y + r->h > screen.y + screen.h) {
        r->y = screen.y + screen.h - r->h;
    }
    /* The near edges last, so a rectangle wider than the screen ends up
     * against the top-left rather than off it on the other side. */
    if (r->x < screen.x) { r->x = screen.x; }
    if (r->y < screen.y) { r->y = screen.y; }
    return (r->x != ox || r->y != oy);
}

void
tiku_trk_deskicons_remember(tiku_trk_deskpos_set_t *s, int sw, int sh,
                            int x, int y)
{
    int i;

    if (s == NULL) {
        return;
    }
    /* One position per screen SIZE: plugging a monitor back in should put
     * the icons where they were on that monitor, not where they drifted
     * to on the other one. */
    for (i = 0; i < s->n; i++) {
        if (s->at[i].sw == sw && s->at[i].sh == sh) {
            s->at[i].x = x;
            s->at[i].y = y;
            return;
        }
    }
    if (s->n >= TIKU_TRK_DESK_POS_MAX) {
        return;
    }
    s->at[s->n].sw = sw;
    s->at[s->n].sh = sh;
    s->at[s->n].x = x;
    s->at[s->n].y = y;
    s->n++;
}

int
tiku_trk_deskicons_recall(const tiku_trk_deskpos_set_t *s, int sw, int sh,
                          int *x, int *y)
{
    int i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < s->n; i++) {
        if (s->at[i].sw == sw && s->at[i].sh == sh) {
            if (x != NULL) { *x = s->at[i].x; }
            if (y != NULL) { *y = s->at[i].y; }
            return 1;
        }
    }
    return 0;
}

void
tiku_trk_deskicons_sources(const tiku_trk_volumes_t *vs,
                           tiku_trk_backend_t *b, const char *home,
                           tiku_trk_vdir_t *out)
{
    int i;

    if (out == NULL) {
        return;
    }
    tiku_trk_vdir_init(out);
    if (vs == NULL || b == NULL) {
        return;
    }
    if (home != NULL && home[0] != '\0') {
        tiku_trk_model_t hm;

        /* First, so what the user put on their own Desktop is what shows
         * when another volume happens to hold the same name. */
        if (b->ops->stat(b, home, &hm) == 0 &&
            tiku_trk_model_is_container(&hm)) {
            (void)tiku_trk_vdir_add(out, home);
        }
    }
    for (i = 0; i < vs->n; i++) {
        char path[TIKU_TRK_PATH_MAX];
        tiku_trk_model_t m;

        /* Only volumes that show an icon contribute: one mounted inside
         * another is a detail of it, and its Desktop folder is the same
         * folder seen twice (PVN-041, PVN-051). */
        if (!tiku_trk_volume_shows_icon(vs, &vs->v[i])) {
            continue;
        }
        if (snprintf(path, sizeof path, "%s%sDesktop", vs->v[i].mount,
                     (vs->v[i].mount[strlen(vs->v[i].mount) - 1u] == '/')
                         ? "" : "/") < 0) {
            continue;
        }
        if (b->ops->stat(b, path, &m) == 0 &&
            tiku_trk_model_is_container(&m)) {
            (void)tiku_trk_vdir_add(out, path);
        }
    }
}

int
tiku_trk_deskicons_show_disks(int show_disks_icon, int volumes_on_desktop)
{
    /* Either alone puts the root on the Desktop; the preferences make them
     * exclusive, and this must not depend on that holding. */
    return (show_disks_icon || volumes_on_desktop) ? 1 : 0;
}
