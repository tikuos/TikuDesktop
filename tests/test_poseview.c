/*
 The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_poseview.c - the engine's rules, checked against the FEATURES rows
 * that specified them.
 *
 * Each check names its row, so a failure says which promise broke.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_poseview.h"
#include "tiku_trk_task.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Display row of the active pose, or -1. */
static int
active_row_of(tiku_trk_poseview_t *v)
{
    tiku_trk_pose_t *a = tiku_trk_poseview_active(v);
    int i, n = tiku_trk_poseview_count(v);

    for (i = 0; a != NULL && i < n; i++) {
        if (tiku_trk_poseview_at(v, i) == a) { return i; }
    }
    return -1;
}

static int
sel_at(tiku_trk_poseview_t *v, int i)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
    return (p != NULL) ? p->selected : 0;
}

/** @brief A measure that makes every label far wider than any icon. */
static int
wide_measure(const char *text, void *ctx)
{
    (void)ctx;
    return 20 * (int)strlen(text);
}

static void
make_tree(const char *dir)
{
    char p[256];
    int i;
    static const char *names[] = { "alpha", "beta", "delta", "gamma",
                                   "omega", "sigma" };

    (void)mkdir(dir, 0755);
    for (i = 0; i < 6; i++) {
        FILE *f;
        snprintf(p, sizeof p, "%s/%s.txt", dir, names[i]);
        f = fopen(p, "w");
        if (f != NULL) { fprintf(f, "%d", i); (void)fclose(f); }
    }
}

static void
rm_tree(const char *dir)
{
    char p[256];
    int i;
    static const char *names[] = { "alpha", "beta", "delta", "gamma",
                                   "omega", "sigma" };

    for (i = 0; i < 6; i++) {
        snprintf(p, sizeof p, "%s/%s.txt", dir, names[i]);
        (void)unlink(p);
    }
    (void)rmdir(dir);
}

int
main(void)
{
    const char *dir = "/tmp/trk_pv_test";
    tiku_trk_backend_t *b;
    tiku_trk_poseview_t *v;

    make_tree(dir);
    b = tiku_trk_backend_posix_open();
    v = tiku_trk_poseview_new(b, dir);
    if (v == NULL) { printf("cannot open view\n"); return 1; }

    printf("poseview rules (6 entries, sorted by name)\n");
    check(tiku_trk_poseview_count(v) == 6, "-", "the six entries loaded");

    /* --- selection ------------------------------------------------------ */
    tiku_trk_poseview_click(v, 2, 0);
    check(sel_at(v, 2) && tiku_trk_poseview_selection_count(v) == 1,
          "PVS-002", "click selects just that pose");

    tiku_trk_poseview_click(v, 4, 0);
    check(sel_at(v, 4) && !sel_at(v, 2), "PVS-002",
          "clicking another deselects the first");

    tiku_trk_poseview_click(v, -1, 0);
    check(tiku_trk_poseview_selection_count(v) == 0, "PVS-001",
          "clicking empty space clears the selection");

    tiku_trk_poseview_click(v, 1, 0);
    tiku_trk_poseview_click(v, 3, TIKU_TRK_MOD_CMD);
    check(sel_at(v, 1) && sel_at(v, 3) &&
          tiku_trk_poseview_selection_count(v) == 2, "PVS-004",
          "cmd-click adds without disturbing the rest");

    tiku_trk_poseview_click(v, 3, TIKU_TRK_MOD_CMD);
    check(!sel_at(v, 3) && sel_at(v, 1), "PVS-004",
          "cmd-click again removes just that one");

    tiku_trk_poseview_click(v, 1, 0);
    tiku_trk_poseview_click(v, 4, TIKU_TRK_MOD_SHIFT);
    check(sel_at(v, 1) && sel_at(v, 2) && sel_at(v, 3) && sel_at(v, 4) &&
          tiku_trk_poseview_selection_count(v) == 4, "PVS-005",
          "shift-click selects the whole range from the pivot");

    tiku_trk_poseview_click(v, 2, TIKU_TRK_MOD_SHIFT);
    check(sel_at(v, 1) && sel_at(v, 2) && !sel_at(v, 3) && !sel_at(v, 4),
          "PVS-006", "a second shift-click re-anchors on the SAME pivot");

    tiku_trk_poseview_click(v, 0, 0);
    tiku_trk_poseview_click(v, 4, TIKU_TRK_MOD_SHIFT);
    tiku_trk_poseview_click(v, 2, TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
    check(!sel_at(v, 1) && !sel_at(v, 2) && sel_at(v, 3) && sel_at(v, 4),
          "PVS-007", "shift+cmd DESELECTS the range from the pivot");

    /* a click inside a multi-selection must not collapse it (drag support) */
    tiku_trk_poseview_click(v, 0, 0);
    tiku_trk_poseview_click(v, 3, TIKU_TRK_MOD_SHIFT);
    tiku_trk_poseview_click(v, 2, 0);
    check(tiku_trk_poseview_selection_count(v) == 4, "PVS-003",
          "clicking within a multi-selection keeps it whole");

    tiku_trk_poseview_select_none(v);
    check(tiku_trk_poseview_selection_count(v) == 0, "PVS-008",
          "an empty selection has no pivot");

    /* --- keyboard ------------------------------------------------------- */
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0);
    check(sel_at(v, 0), "PVK-003", "Down with nothing selected seeds the first");

    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_UP, 0);
    check(sel_at(v, 5), "PVK-003", "Up with nothing selected seeds the last");

    tiku_trk_poseview_click(v, 2, 0);
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0);
    check(sel_at(v, 3) && tiku_trk_poseview_selection_count(v) == 1,
          "PVK-004", "a plain arrow replaces the whole selection");

    tiku_trk_poseview_click(v, 0, 0);
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_UP, 0);
    check(sel_at(v, 0), "PVK-002", "Up does NOT wrap at the top");

    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_LEFT, 0);
    check(sel_at(v, 5), "PVK-002", "Left DOES wrap to the last");

    tiku_trk_poseview_click(v, 5, 0);
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0);
    check(sel_at(v, 5), "PVK-002", "Down does NOT wrap at the bottom");

    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_RIGHT, 0);
    check(sel_at(v, 0), "PVK-002", "Right DOES wrap to the first");

    tiku_trk_poseview_click(v, 1, 0);
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, TIKU_TRK_MOD_SHIFT);
    check(sel_at(v, 1) && sel_at(v, 2), "PVK-005",
          "shift+arrow grows the selection by one");
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_UP, TIKU_TRK_MOD_SHIFT);
    check(sel_at(v, 1) && !sel_at(v, 2), "PVK-005",
          "shift+arrow back walks the selection in again");

    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_HOME, 0);
    check(sel_at(v, 0), "PVK-010", "Home selects the first entry in list mode");
    tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_END, 0);
    check(sel_at(v, 5), "PVK-010", "End selects the last");

    /* --- type-ahead ----------------------------------------------------- */
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_type_ahead(v, 'd');
    check(sel_at(v, 2), "PVK-t", "typing jumps to the best match (delta)");
    check(strcmp(tiku_trk_poseview_type_string(v), "d") == 0, "PVK-t",
          "the typed string is exposed for the count view");

    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_type_ahead(v, 'g');
    tiku_trk_poseview_type_ahead(v, 'a');
    check(sel_at(v, 3), "PVK-t",
          "'ga' prefix-matches gamma, which wins on the starts-with rule");
    tiku_trk_poseview_type_backspace(v);
    check(strcmp(tiku_trk_poseview_type_string(v), "g") == 0, "PVK-t",
          "backspace shortens the string");

    /* starts-with beats contains, whatever the order in the list */
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_type_ahead(v, 'a');
    check(sel_at(v, 0), "PVK-t",
          "a name STARTING with the text wins over one merely containing it");

    /* filtering mode narrows the listing instead of jumping */
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_set_filtering(v, 1);
    tiku_trk_poseview_type_ahead(v, 'm');
    check(tiku_trk_poseview_count(v) == 3, "PVK-f",
          "filtering narrows the list to matches (gamma, omega, sigma)");
    tiku_trk_poseview_type_clear(v);
    check(tiku_trk_poseview_count(v) == 6, "PVK-f",
          "clearing the filter restores every entry");
    tiku_trk_poseview_set_filtering(v, 0);

    /* --- sorting -------------------------------------------------------- */
    {
        const char *first_asc, *first_desc;

        first_asc = tiku_trk_poseview_at(v, 0)->model.name;
        check(strncmp(first_asc, "alpha", 5) == 0, "TS",
              "a fresh view is sorted by name, alpha first");
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_NAME);
        first_desc = tiku_trk_poseview_at(v, 0)->model.name;
        check(strncmp(first_desc, "sigma", 5) == 0, "TS",
              "clicking the same column again reverses it");
        check(tiku_trk_poseview_sort_reverse(v), "TS",
              "and the reverse flag is reported for the sort marker");
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_KIND);
        check(!tiku_trk_poseview_sort_reverse(v), "TS",
              "a different column sorts ascending again");
    }

    /* --- icon mode, grid and spatial (PVL-012..021) --------------------- */
    printf("icon mode\n");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    check(tiku_trk_poseview_mode(v) == TIKU_TRK_MODE_ICON, "PVL-001",
          "the view switches to icon mode");
    {
        int gx, gy, ox, oy, x, y, placed, i;

        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(gx == 32 + 28 && gy == gx, "PVL-013",
              "icon grid is iconSize + ceil(iconSize*0.875) on both axes");
        check(ox == 20 && oy == 20, "PVL-013",
              "and the origin offset is ceil(iconSize*0.625)");

        x = ox + gx + 3;  y = oy + gy - 4;
        tiku_trk_poseview_pin_to_grid(v, &x, &y);
        check(x == ox + gx && y == oy + gy, "PVL-014",
              "pinning rounds to the NEAREST cell in both directions");

        placed = tiku_trk_poseview_place_unplaced(v, 400);
        check(placed == 6, "PVL-015", "every unplaced pose gets a slot");
        {   /* no two icons may share a slot */
            int clash = 0, j;
            for (i = 0; i < 6; i++) {
                for (j = i + 1; j < 6; j++) {
                    int ax, ay, bx, by;
                    tiku_trk_poseview_pose_location(v,
                        tiku_trk_poseview_at(v, i), &ax, &ay);
                    tiku_trk_poseview_pose_location(v,
                        tiku_trk_poseview_at(v, j), &bx, &by);
                    if (ax == bx && ay == by) { clash++; }
                }
            }
            check(clash == 0, "PVL-015", "and no two land on the same slot");
        }
        {   /* placement wraps at the right edge rather than running off */
            int maxx = 0;
            for (i = 0; i < 6; i++) {
                int px, py;
                tiku_trk_poseview_pose_location(v,
                    tiku_trk_poseview_at(v, i), &px, &py);
                if (px > maxx) { maxx = px; }
            }
            check(maxx < 400, "PVL-015", "placement wraps at the view edge");
        }
    }
    {   /* an arrangement made at 32 px reappears in proportion at 64 px */
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);
        int x32, y32, x64, y64;

        tiku_trk_poseview_move_pose(v, p0, 120, 80, 0);
        tiku_trk_poseview_pose_location(v, p0, &x32, &y32);
        tiku_trk_poseview_set_icon_size(v, 64);
        tiku_trk_poseview_pose_location(v, p0, &x64, &y64);
        check(x64 == x32 * 2 && y64 == y32 * 2, "PVL-012",
              "locations are stored unscaled, so a layout scales with the "
              "icon size");
        tiku_trk_poseview_set_icon_size(v, 32);
    }
    {   /* rubber band selects what it touches */
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);
        int hits;

        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_move_pose(v, p0, 100, 100, 0);
        hits = tiku_trk_poseview_select_rect(v, 90, 90, 180, 180, 0);
        check(hits >= 1 && p0->selected, "PVS-band",
              "a band selects the poses it intersects");
        hits = tiku_trk_poseview_select_rect(v, 900, 900, 950, 950, 0);
        check(hits == 0 && !p0->selected, "PVS-band",
              "and an empty band clears the selection");
    }
    {   /* hit-testing finds the icon under a point */
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);
        int ix, iy, iw, ih;

        tiku_trk_poseview_pose_parts(v, p0, &ix, &iy, &iw, &ih,
                                     NULL, NULL, NULL, NULL);
        check(tiku_trk_poseview_pose_at(v, ix + 4, iy + 4) == p0, "PVL-hit",
              "a point on an icon finds that icon");
        check(tiku_trk_poseview_pose_at(v, ix - 300, iy - 300) == NULL,
              "PVL-hit", "and empty space finds nothing");
    }
    {   /* spatial memory survives a reload */
        int px, py, qx, qy;
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);

        tiku_trk_poseview_move_pose(v, p0, 200, 160, 1);
        tiku_trk_poseview_pose_location(v, p0, &px, &py);
        check(tiku_trk_poseview_save_state(v) >= 1, "PVL-021",
              "user-placed positions are written to the store");
        (void)tiku_trk_poseview_reload(v);
        check(tiku_trk_poseview_restore_state(v) >= 1, "PVL-021",
              "and read back on reload");
        {
            int i, n = tiku_trk_poseview_count(v), found = 0;
            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *q = tiku_trk_poseview_at(v, i);
                if (strcmp(q->model.name, p0->model.name) != 0) { continue; }
                tiku_trk_poseview_pose_location(v, q, &qx, &qy);
                found = (qx == px && qy == py);
            }
            check(found, "PVL-021",
                  "the icon comes back exactly where it was left");
        }
    }

    printf("\ndragging\n");
    {
        tiku_trk_pose_t *p;
        int gx, gy, ox, oy, x0, y0, x1, y1, step;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        (void)tiku_trk_poseview_place_unplaced(v, 400);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        p = tiku_trk_poseview_at(v, 0);
        p->selected = 1;
        tiku_trk_poseview_pose_location(v, p, &x0, &y0);
        p->drag_x0 = x0;
        p->drag_y0 = y0;

        /* One cell to the right, delivered as the many small steps a mouse
         * actually sends.  Each step is far smaller than the grid, so a drag
         * measured from the pose's LIVE position would round straight back
         * into the cell it is already in and the icon would never move. */
        for (step = 1; step <= gx; step++) {
            tiku_trk_poseview_move_pose(v, p, p->drag_x0 + step, p->drag_y0,
                                        1);
        }
        tiku_trk_poseview_pose_location(v, p, &x1, &y1);
        check(x1 != x0, "PVL-026", "a snapped drag actually moves the icon");
        check(x1 == x0 + gx, "PVL-026", "and lands exactly one cell over");
        check(y1 == y0, "PVS-046", "without drifting on the other axis");
    }

    /* Mini-icon mode is its own size, not the icon-mode size in a short
     * cell -- otherwise it is not a third mode at all. */
    {
        int icon_art, mini_art;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(v, 64);
        icon_art = tiku_trk_poseview_art_size(v);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
        mini_art = tiku_trk_poseview_art_size(v);
        check(icon_art == 64, "PVL-001", "icon mode draws at its chosen size");
        check(mini_art == TIKU_TRK_MINI_ICON, "PVL-001",
              "mini-icon mode draws mini icons whatever that size says");
    }

    printf("\nfiltering survives the listing changing under it\n");
    {
        char sub[300];
        int i, shown, ok = 1;

        /* A second folder with FEWER entries, kept OUTSIDE the fixture: a
         * subdirectory would add a seventh entry to the parent and quietly
         * break every earlier count in this file.  The bug only shows when
         * the list the filter indexes into is replaced by a shorter one,
         * which is what switching folder does. */
        snprintf(sub, sizeof sub, "%s_inner", dir);
        (void)mkdir(sub, 0755);
        {
            char f[400];
            FILE *fp;

            snprintf(f, sizeof f, "%s/zeta.txt", sub);
            fp = fopen(f, "w");
            if (fp != NULL) { (void)fclose(fp); }
        }

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
        tiku_trk_poseview_set_filtering(v, 1);
        tiku_trk_poseview_type_ahead(v, 'a');
        check(tiku_trk_poseview_count(v) > 1, "PVL-083",
              "the filter matches several of the six names");

        (void)tiku_trk_poseview_switch_dir(v, sub);
        shown = tiku_trk_poseview_count(v);
        for (i = 0; i < shown; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

            /* Every row must belong to the folder now being shown. */
            if (p == NULL || strstr(p->model.path, "_inner/") == NULL) {
                ok = 0;
            }
        }
        check(ok, "PVL-083",
              "after switching folder no row is left from the previous one");

        check(shown == 1, "PVL-083",
              "and the count is the new folder's, not the old one's");
        tiku_trk_poseview_type_clear(v);
        tiku_trk_poseview_set_filtering(v, 0);
        (void)tiku_trk_poseview_switch_dir(v, dir);
        rm_tree(sub);
    }

    printf("\ncleaning up, and what survives a session\n");
    {
        int x0, y0, x1, y1;
        tiku_trk_pose_t *p0;

        /* An arrangement made in icon mode must not be destroyed by a
         * clean-up invoked while the view is showing a list. */
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        (void)tiku_trk_poseview_place_unplaced(v, 400);
        p0 = tiku_trk_poseview_at(v, 0);
        tiku_trk_poseview_move_pose(v, p0, 300, 200, 0);
        tiku_trk_poseview_pose_location(v, p0, &x0, &y0);

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
        tiku_trk_poseview_cleanup(v, 400);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        p0 = tiku_trk_poseview_at(v, 0);
        tiku_trk_poseview_pose_location(v, p0, &x1, &y1);
        check(x1 == x0 && y1 == y0, "PVL-023",
              "cleaning up in list mode leaves the icon arrangement alone");

        /* And in icon mode it really does re-lay out. */
        tiku_trk_poseview_cleanup(v, 400);
        p0 = tiku_trk_poseview_at(v, 0);
        tiku_trk_poseview_pose_location(v, p0, &x1, &y1);
        check(x1 != x0 || y1 != y0, "PVL-023",
              "but in icon mode it does what it says");
    }


    /*---------------------------------------------------------------------*/
    /* Cluster 2: filtering, selection commands, sort keys, rename arming   */
    /*---------------------------------------------------------------------*/

    /* Start from a clean, unfiltered, list-mode view. */
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_select_none(v);

    /* --- multi-term filtering (PVK-031) ---------------------------------- */
    tiku_trk_poseview_set_filtering(v, 1);
    tiku_trk_poseview_type_ahead(v, 'a');
    check(tiku_trk_poseview_count(v) == 6, "PVK-031",
          "one term filters on any column's text");
    check(tiku_trk_poseview_filter_term(v) == 1 &&
          tiku_trk_poseview_filter_terms(v) == 2, "PVK-031",
          "Shift+Space starts a second term");
    check(!tiku_trk_poseview_filter_term(v), "PVK-031",
          "and is refused while that term is empty");
    tiku_trk_poseview_type_ahead(v, 'm');
    check(tiku_trk_poseview_count(v) == 3, "PVK-031",
          "a pose must match EVERY term (a and m: gamma, omega, sigma)");
    check(strcmp(tiku_trk_poseview_type_string(v), "a|m") == 0, "PVK-031",
          "and the terms are shown separated by '|'");
    tiku_trk_poseview_type_ahead(v, 'z');
    check(tiku_trk_poseview_count(v) == 0, "PVK-031",
          "a term nothing matches empties the listing");
    tiku_trk_poseview_type_backspace(v);
    check(tiku_trk_poseview_count(v) == 3, "PVK-031",
          "and backspacing it brings the rows back");

    /* PVL-092: a row that leaves the filter leaves the selection with it --
     * a command must not act on something the user can no longer see. */
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_select_all(v);
    tiku_trk_poseview_type_ahead(v, 'z');
    check(tiku_trk_poseview_selection_count(v) == 0, "PVL-092",
          "rows the filter drops are dropped from the selection too");
    tiku_trk_poseview_type_clear(v);

    /* A term matched against a column that is not the name (FilterPose). */
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_type_ahead(v, 'f');
    tiku_trk_poseview_type_ahead(v, 'i');
    tiku_trk_poseview_type_ahead(v, 'l');
    tiku_trk_poseview_type_ahead(v, 'e');
    check(tiku_trk_poseview_count(v) == 6, "PVK-031",
          "\"file\" matches every row through the Kind column");

    /* --- PVL-081: a column going away re-evaluates the filter ----------- */
    check(tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_KIND) == 1 &&
          tiku_trk_poseview_count(v) == 0, "PVL-081",
          "removing the column the filter matched on hides those rows");
    check(tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_KIND) == 1 &&
          tiku_trk_poseview_count(v) == 6, "PVL-081",
          "and adding it back brings them straight back");

    /* --- PVK-015/016: Return while filtering ---------------------------- */
    tiku_trk_poseview_select_none(v);
    check(tiku_trk_poseview_return_pressed(v, 0) == 0 &&
          tiku_trk_poseview_selection_count(v) == 1 &&
          sel_at(v, 0), "PVK-015",
          "Return with nothing selected takes the top filtered row");
    check(tiku_trk_poseview_return_pressed(v, 1) == 1, "PVK-016",
          "and Shift says to drop the filter afterwards");
    tiku_trk_poseview_set_filtering(v, 0);
    check(tiku_trk_poseview_return_pressed(v, 1) == 0, "PVK-016",
          "which it does not say when nothing is filtered");
    tiku_trk_poseview_type_clear(v);

    /* --- the batched load (PVL-088, PVN-047, PVL-089, PVL-090, TS-038) -- */
    {
        int64_t t0 = 1000000;
        int staged, more, first_count;

        staged = tiku_trk_poseview_load_begin(v, t0);
        check(staged == 6 && tiku_trk_poseview_loading(v), "PVL-088",
              "the folder is staged and the load is in flight");
        check(tiku_trk_poseview_count(v) == 0, "PVN-047",
              "nothing is published before the first batch");
        check(!tiku_trk_poseview_pole(v, t0), "TS-038",
              "and no pole yet -- it is armed, not shown");
        check(!tiku_trk_poseview_pole(v, t0 + TIKU_TRK_POLE_AFTER_US - 1),
              "TS-038",
              "a load that finishes inside half a second never shows one");
        check(tiku_trk_poseview_pole(v, t0 + TIKU_TRK_POLE_AFTER_US),
              "TS-038", "past half a second it appears");

        more = tiku_trk_poseview_load_step(v, t0);
        first_count = tiku_trk_poseview_count(v);
        check(more && first_count == 1, "PVN-047",
              "the first row is published on its own, so the window shows "
              "something at once");
        while (tiku_trk_poseview_load_step(v, t0)) {
        }
        check(tiku_trk_poseview_count(v) == 6 &&
              !tiku_trk_poseview_loading(v), "PVL-088",
              "the rest arrive in batches and the load completes");
        check(!tiku_trk_poseview_pole(v, t0 + 10 * TIKU_TRK_POLE_AFTER_US),
              "TS-038", "and the pole goes with it");
    }

    /* --- PVL-089: a load belongs to the folder that started it ---------- */
    {
        int gen_before = tiku_trk_poseview_generation(v);

        (void)tiku_trk_poseview_load_begin(v, 0);
        check(tiku_trk_poseview_generation(v) == gen_before + 1, "PVL-089",
              "starting a load moves the generation on");
        (void)tiku_trk_poseview_switch_dir(v, "/tmp");
        check(tiku_trk_poseview_generation(v) != gen_before + 1, "PVL-089",
              "and switching folders moves it again, so a batch still in "
              "flight for the old one can be told apart");
        (void)tiku_trk_poseview_switch_dir(v, dir);
    }

    /* --- PVL-090: the loader and a notice are two mouths, one listing --- */
    {
        tiku_trk_event_t e;
        tiku_trk_model_t late;

        /* Take a real entry, then start a load and let a CREATION notice
         * for that same entry arrive while it is still STAGED -- the race
         * the guard exists for. */
        (void)tiku_trk_poseview_reload(v);
        late = tiku_trk_poseview_at(v, 5)->model;
        (void)tiku_trk_poseview_load_begin(v, 0);
        memset(&e, 0, sizeof e);
        e.op = TIKU_TRK_EV_CREATED;
        e.model = late;
        snprintf(e.path, sizeof e.path, "%s", late.path);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_count(v) == 1, "PVL-090",
              "the notice brings the row in ahead of the loader");
        while (tiku_trk_poseview_load_step(v, 0)) {
        }
        check(tiku_trk_poseview_count(v) == 6, "PVL-090",
              "and the loader reaching the same node adds nothing -- one "
              "node, one row, whichever mouth reported it first");

        /* The harder case, and the one identity exists for: the notice
         * brings the node in under a NEW name while the loader still has
         * it staged under the old one, so the paths do not match and only
         * the node id can tell they are the same entry. */
        (void)tiku_trk_poseview_load_begin(v, 0);
        memset(&e, 0, sizeof e);
        e.op = TIKU_TRK_EV_CREATED;
        e.model = late;
        snprintf(e.model.name, sizeof e.model.name, "renamed.txt");
        snprintf(e.model.path, sizeof e.model.path, "%s/renamed.txt", dir);
        snprintf(e.path, sizeof e.path, "%s", e.model.path);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        while (tiku_trk_poseview_load_step(v, 0)) {
        }
        check(tiku_trk_poseview_count(v) == 6, "PVL-090",
              "a node renamed under the loader appears once, under its new "
              "name -- the paths differ, so only its identity says so");
    }

    /* --- PVN-047: batching is by TIME as well as by count --------------- */
    {
        int64_t t = 5000000;
        int rows_first, rows_second;

        (void)tiku_trk_poseview_load_begin(v, t);
        (void)tiku_trk_poseview_load_step(v, t);       /* row one alone    */
        rows_first = tiku_trk_poseview_count(v);
        /* Each later call arrives well past the batch deadline, so each
         * publishes one batch and yields rather than draining the folder. */
        t += 10 * TIKU_TRK_LOAD_CHUNK_US;
        (void)tiku_trk_poseview_load_step(v, t);
        rows_second = tiku_trk_poseview_count(v);
        check(rows_first == 1 && rows_second == 2, "PVN-047",
              "a step that is already past its deadline publishes one batch "
              "and yields, instead of draining the folder in one go");
        while (tiku_trk_poseview_load_step(v, t)) {
            t += 10 * TIKU_TRK_LOAD_CHUNK_US;
        }
        check(tiku_trk_poseview_count(v) == 6, "PVN-047",
              "and the folder still finishes complete");
    }
    (void)tiku_trk_poseview_reload(v);
    (void)tiku_trk_poseview_restore_state(v);

    /* --- PVK-005: shift+arrow shrinks REPEATEDLY, not once -------------- */
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 0, 0);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, TIKU_TRK_MOD_SHIFT);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, TIKU_TRK_MOD_SHIFT);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_selection_count(v) == 4, "PVK-005",
          "three shift+downs grow the selection to four");
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_UP, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_selection_count(v) == 3, "PVK-005",
          "shift+up walks one back off the end");
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_UP, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_selection_count(v) == 2, "PVK-005",
          "and again -- the walk-back moves its mark, so the second step "
          "drops a different pose instead of the one already dropped");

    /* --- PVK-045: select-all re-anchors -------------------------------- */
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 4, 0);
    tiku_trk_poseview_select_all(v);
    tiku_trk_poseview_click(v, 2, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_selection_count(v) == 3 && sel_at(v, 0) &&
          sel_at(v, 2) && !sel_at(v, 3), "PVK-045",
          "after Select All a shift-click extends from the FIRST row, not "
          "from wherever the last click left an anchor");

    /* --- PVK-010 / PVL-055: Home/End/Page in an icon mode --------------- */
    tiku_trk_poseview_set_visible_rows(v, 3);
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 0, 0);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_PGDN, 0);
    check(active_row_of(v) == 2, "PVL-055",
          "a page is what the window shows -- three rows visible steps two, "
          "not a hardcoded twenty");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 1, 0);
    tiku_trk_poseview_scroll_to(v, 0);
    tiku_trk_poseview_set_viewport(v, 200, 100);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_END, 0);
    {
        int ox = 0, oy = 0;

        tiku_trk_poseview_origin(v, &ox, &oy);
        check(sel_at(v, 1) && tiku_trk_poseview_selection_count(v) == 1 &&
              oy > 0, "PVK-010",
              "in icon mode End moves the view's own pixel ORIGIN and "
              "leaves the selection alone -- there is no last ROW to "
              "select in a two-dimensional arrangement, and the row index "
              "it used to write was read by nothing that draws icons");
    }
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_scroll_to(v, 0);

    /* --- MA-055: a re-placed row must not leave the filter pointing at
     *     whatever slid into its slot ------------------------------------ */
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_set_filtering(v, 1);
    tiku_trk_poseview_type_ahead(v, 'a');
    {
        int shown = tiku_trk_poseview_count(v), i2, ok = 1;
        tiku_trk_pose_t *q = tiku_trk_poseview_at(v, 0);
        int moved_at = (q != NULL) ? q->index : -1;

        /* Rename a filtered pose so it sorts elsewhere AND stops matching,
         * then re-place it.  The filtered list holds indices INTO the pose
         * array, so the move renumbers exactly what it indexes. */
        if (q != NULL) {
            snprintf(q->model.name, sizeof q->model.name, "zzz.txt");
            (void)tiku_trk_poseview_recheck_order(v, moved_at);
        }
        for (i2 = 0; i2 < tiku_trk_poseview_count(v); i2++) {
            tiku_trk_pose_t *r2 = tiku_trk_poseview_at(v, i2);

            if (r2 == NULL || strcasestr(r2->model.name, "a") == NULL) {
                ok = 0;
            }
        }
        check(moved_at >= 0 && ok &&
              tiku_trk_poseview_count(v) == shown - 1, "MA-055",
              "every row the filter still shows really matches it -- a "
              "re-placed pose renumbers the array the filter indexes");
    }
    tiku_trk_poseview_set_filtering(v, 0);
    tiku_trk_poseview_type_clear(v);
    (void)tiku_trk_poseview_reload(v);
    (void)tiku_trk_poseview_restore_state(v);

    /* --- CW-013 / PVL-061: never scrolled past the content -------------- */
    tiku_trk_poseview_set_visible_rows(v, 2);
    tiku_trk_poseview_scroll_to(v, 4);
    check(tiku_trk_poseview_scroll(v) == 4, "CW-013",
          "scrolled to the last two of six rows");
    tiku_trk_poseview_set_visible_rows(v, 6);
    check(tiku_trk_poseview_scroll(v) == 0, "CW-013",
          "a window that grows to fit everything scrolls back to the top "
          "instead of showing blank space under the content");
    tiku_trk_poseview_set_visible_rows(v, 2);
    tiku_trk_poseview_scroll_to(v, 4);
    tiku_trk_poseview_set_filtering(v, 1);
    tiku_trk_poseview_type_ahead(v, 'a');
    tiku_trk_poseview_type_ahead(v, 'l');
    check(tiku_trk_poseview_count(v) == 1 &&
          tiku_trk_poseview_scroll(v) == 0, "PVL-061",
          "and a filter that shortens the listing does the same");
    tiku_trk_poseview_set_filtering(v, 0);
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_scroll_to(v, 0);

    /* --- PVK-046: invert ------------------------------------------------ */
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 1, 0);
    tiku_trk_poseview_invert_selection(v);
    check(tiku_trk_poseview_selection_count(v) == 5 && !sel_at(v, 1) &&
          sel_at(v, 0) && sel_at(v, 5), "PVK-046",
          "inverting selects everything else and deselects what was selected");
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 0, 0);
    tiku_trk_poseview_invert_selection(v);
    tiku_trk_poseview_click(v, 3, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_selection_count(v) == 1 && sel_at(v, 3),
          "PVK-046",
          "the anchors are dropped, so a later shift-click takes just that "
          "row instead of extending from a row nobody clicked");

    /* --- PVK-047: select by expression ---------------------------------- */
    {
        char err[80];
        int n;

        /* gamma, omega and sigma carry an 'm'; alpha, beta and delta
         * do not, so the two answers must add up to six. */
        n = tiku_trk_poseview_select_matching(v, "*m*",
                                              TIKU_TRK_MATCH_GLOB, 1, 0,
                                              err, sizeof err);
        check(n == 3 && tiku_trk_poseview_selection_count(v) == 3,
              "PVK-047", "a wildcard selects every matching pose");
        n = tiku_trk_poseview_select_matching(v, "*m*",
                                              TIKU_TRK_MATCH_GLOB, 1, 1,
                                              err, sizeof err);
        check(n == 3 && tiku_trk_poseview_selection_count(v) == 3 &&
              !sel_at(v, 3), "PVK-047",
              "and inverted selects exactly the rest");
        n = tiku_trk_poseview_select_matching(v, "^alpha",
                                              TIKU_TRK_MATCH_REGEX, 1, 0,
                                              err, sizeof err);
        check(n == 1, "PVK-047", "a regular expression selects by pattern");
        n = tiku_trk_poseview_select_matching(v, "alp(ha",
                                              TIKU_TRK_MATCH_REGEX, 1, 0,
                                              err, sizeof err);
        check(n == -1 && err[0] != '\0' &&
              tiku_trk_poseview_selection_count(v) == 1,
              "PVK-047",
              "a bad expression is reported and leaves the selection alone");
    }

    /* --- PVS-018: the context menu normalizes the selection -------------- */
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 2, 0);
    check(tiku_trk_poseview_context_click(v, 4) == 1 && sel_at(v, 4) &&
          !sel_at(v, 2), "PVS-018",
          "right-clicking an unselected pose selects it alone");
    tiku_trk_poseview_click(v, 1, 0);
    tiku_trk_poseview_click(v, 3, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_context_click(v, 2) == 0 &&
          tiku_trk_poseview_selection_count(v) == 3, "PVS-018",
          "but right-clicking INSIDE a group keeps the group");
    check(tiku_trk_poseview_context_click(v, -1) == 1 &&
          tiku_trk_poseview_selection_count(v) == 0, "PVS-018",
          "and on empty space it clears the selection");

    /* --- PVS-023: deactivation hides the paint, not the selection -------- */
    tiku_trk_poseview_click(v, 2, 0);
    tiku_trk_poseview_click(v, 4, TIKU_TRK_MOD_SHIFT);
    tiku_trk_poseview_window_activated(v, 0);
    check(tiku_trk_poseview_selection_count(v) == 3, "PVS-023",
          "an inactive window still OWNS its selection");
    check(!tiku_trk_poseview_draws_selected(v, tiku_trk_poseview_at(v, 3)),
          "PVS-023", "but stops drawing it as selected");
    tiku_trk_poseview_window_activated(v, 1);
    check(tiku_trk_poseview_draws_selected(v, tiku_trk_poseview_at(v, 3)),
          "PVS-023", "and paints it again on the way back");
    tiku_trk_poseview_window_activated(v, 0);
    tiku_trk_poseview_window_activated(v, 1);
    tiku_trk_poseview_click(v, 0, TIKU_TRK_MOD_SHIFT);
    check(tiku_trk_poseview_selection_count(v) == 3, "PVS-023",
          "the pivot survived the round trip, so shift still extends "
          "from where the user anchored");
    tiku_trk_poseview_show_selection_when_inactive(v, 1);
    tiku_trk_poseview_window_activated(v, 0);
    check(tiku_trk_poseview_draws_selected(v, tiku_trk_poseview_at(v, 0)),
          "PVS-023",
          "with show-when-inactive the paint stays through deactivation");
    tiku_trk_poseview_show_selection_when_inactive(v, 0);
    tiku_trk_poseview_window_activated(v, 1);

    /* --- TS-003/004: primary and secondary sort -------------------------- */
    /* From a known key: the view restores the one it was left with now
     * (MA-061), and these rules are about the CLICK, not about what was
     * remembered. */
    tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_NAME);
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_KIND, 0);
    check(tiku_trk_poseview_sort_field(v) == TIKU_TRK_COL_KIND &&
          !tiku_trk_poseview_sort_reverse(v), "TS-003",
          "a click on another column sorts ascending on it");
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_KIND, 0);
    check(tiku_trk_poseview_sort_reverse(v), "TS-003",
          "the same column again reverses");
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_KIND, 1);
    check(!tiku_trk_poseview_sort_reverse(v), "TS-003",
          "Shift on the key with no tie-break falls through to reversing");
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_NAME, 0);
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_KIND, 1);
    check(tiku_trk_poseview_sort_secondary(v) == TIKU_TRK_COL_KIND &&
          tiku_trk_poseview_sort_field(v) == TIKU_TRK_COL_NAME, "TS-004",
          "Shift elsewhere sets the tie-break and leaves the key alone");
    check(!tiku_trk_poseview_sort_reverse(v), "TS-004",
          "and does not also reverse the key");
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_KIND, 1);
    check(tiku_trk_poseview_sort_secondary(v) == -1, "TS-004",
          "Shift on the tie-break column clears it");
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_KIND, 1);
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_NAME, 1);
    check(tiku_trk_poseview_sort_field(v) == TIKU_TRK_COL_KIND &&
          tiku_trk_poseview_sort_secondary(v) == TIKU_TRK_COL_NAME,
          "TS-003", "Shift on the key WITH a tie-break swaps the two");
    check(tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_NAME) == 0,
          "TS-004", "the name column cannot be removed");
    tiku_trk_poseview_sort_click(v, TIKU_TRK_COL_NAME, 0);

    /* --- PVS-014 / MA-043: rename armed on the slow second click --------- */
    {
        int64_t t0 = tiku_trk_now();

        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, 2, 0);
        check(!tiku_trk_poseview_name_clicked(v, 2, t0), "PVS-014",
              "the click that SELECTED a pose does not arm a rename");
        check(!tiku_trk_poseview_name_clicked(v, 3, t0), "PVS-014",
              "nor does a click on a pose that is not selected");

        t0 += TIKU_TRK_CLICK_SPEED_US + 1000;
        check(tiku_trk_poseview_name_clicked(v, 2, t0) &&
              tiku_trk_poseview_edit_armed(v), "PVS-014",
              "a later click on the selected name arms it");
        check(tiku_trk_poseview_edit_due(v, t0 + 1000) == -1, "MA-043",
              "and it does NOT start inside the double-click window");
        check(tiku_trk_poseview_edit_due(v, t0 + TIKU_TRK_CLICK_SPEED_US +
                                            1000) == 2, "MA-043",
              "it starts once that window has passed with no further click");
        check(!tiku_trk_poseview_edit_armed(v), "MA-043",
              "and disarms itself, so one expiry cannot open two editors");

        /* Cancelled by a click elsewhere (PVS-015). */
        t0 += 4 * TIKU_TRK_CLICK_SPEED_US;
        (void)tiku_trk_poseview_name_clicked(v, 2, t0);
        tiku_trk_poseview_click(v, 4, 0);
        check(!tiku_trk_poseview_edit_armed(v), "PVS-015",
              "clicking a different pose cancels a pending rename");
        t0 += 4 * TIKU_TRK_CLICK_SPEED_US;
        tiku_trk_poseview_click(v, 2, 0);
        t0 += 4 * TIKU_TRK_CLICK_SPEED_US;
        (void)tiku_trk_poseview_name_clicked(v, 2, t0);
        tiku_trk_poseview_click(v, -1, 0);
        check(!tiku_trk_poseview_edit_armed(v), "PVS-015",
              "and so does a click on empty space");

        /* Refused outright where editing is disabled. */
        tiku_trk_poseview_click(v, 2, 0);
        t0 += 4 * TIKU_TRK_CLICK_SPEED_US;
        tiku_trk_poseview_allow_editing(v, 0);
        check(!tiku_trk_poseview_name_clicked(v, 2, t0), "PVS-015",
              "a view with pose editing off never arms one at all");
        tiku_trk_poseview_allow_editing(v, 1);
    }

    /* --- PVL-003: the icon size ladder ---------------------------------- */
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(v, 32);
    check(tiku_trk_poseview_step_icon_size(v, 1) &&
          tiku_trk_poseview_icon_size(v) == 40, "PVL-003",
          "32 steps up to 40");
    tiku_trk_poseview_set_icon_size(v, 128);
    check(!tiku_trk_poseview_step_icon_size(v, 1) &&
          tiku_trk_poseview_icon_size(v) == 128, "PVL-003",
          "128 is the top: stepping up is a no-op, not a wrap");
    tiku_trk_poseview_set_icon_size(v, 32);
    check(!tiku_trk_poseview_step_icon_size(v, 0), "PVL-003",
          "and 32 is the bottom");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    check(!tiku_trk_poseview_step_icon_size(v, 1), "PVL-003",
          "list mode has no icon size to step");

    /* --- PVL-011: positions carry across a mode change ------------------ */
    {
        int ax, ay, bx, by, gx, gy, ox, oy;
        tiku_trk_pose_t *p0, *p1;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(v, 32);
        p0 = tiku_trk_poseview_at(v, 0);
        p1 = tiku_trk_poseview_at(v, 1);
        /* Two icons in the same column, one cell apart. */
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        tiku_trk_poseview_move_pose(v, p0, ox, oy, 1);
        tiku_trk_poseview_move_pose(v, p1, ox, oy + gy, 1);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        tiku_trk_poseview_pose_location(v, tiku_trk_poseview_at(v, 0),
                                        &ax, &ay);
        tiku_trk_poseview_pose_location(v, tiku_trk_poseview_at(v, 1),
                                        &bx, &by);
        check(ax == bx && ax == ox && ay == oy, "PVL-011",
              "the arrangement lands on the NEW grid, at its own origin");
        check(by - ay == gy, "PVL-011",
              "and one cell apart is still one cell apart, measured in the "
              "new cell rather than carried over as raw pixels");
    }
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);

    /* --- PVL-065: a long name widens the pose's rect -------------------- */
    {
        int x0, y0, w0, h0, w1;
        tiku_trk_pose_t *p0;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        p0 = tiku_trk_poseview_at(v, 0);
        tiku_trk_poseview_set_measure(v, NULL, NULL);
        tiku_trk_poseview_pose_rect(v, p0, &x0, &y0, &w0, &h0);
        tiku_trk_poseview_set_measure(v, wide_measure, NULL);
        tiku_trk_poseview_pose_rect(v, p0, &x0, &y0, &w1, &h0);
        check(w1 > w0, "PVL-065",
              "a label wider than the icon widens the clickable rect");
        tiku_trk_poseview_set_measure(v, NULL, NULL);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    }

    /* --- PVS-027: a command-band toggles against what it started with --- */
    {
        int gx, gy, ox, oy;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        tiku_trk_poseview_cleanup(v, 4 * gx);
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, 0, 0);
        /* The command-drag keeps the selection, so the band starts from it. */
        tiku_trk_poseview_set_band(v, 1);
        (void)tiku_trk_poseview_select_rect(v, 0, 0, 4 * gx, gy / 2, 0u);
        check(!sel_at(v, 0), "PVS-027",
              "a swept pose that was already selected is toggled OFF");
        check(sel_at(v, 1), "PVS-027",
              "and one that was not is toggled on");
        (void)tiku_trk_poseview_select_rect(v, 0, 0, 1, 1, 0u);
        check(sel_at(v, 0) && !sel_at(v, 1), "PVS-027",
              "sweeping back off them restores what the band started with");
        tiku_trk_poseview_set_band(v, 0);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    }

    /* --- what is selected after a delete (FS-050) ----------------------- */
    {
        int n = tiku_trk_poseview_count(v);
        int got;

        got = tiku_trk_poseview_select_after_removal(v, 1);
        check(got == 1 && sel_at(v, 1) &&
              tiku_trk_poseview_selection_count(v) == 1,
              "FS-050", "after a delete the NEXT item down is selected, on "
              "its own");
        got = tiku_trk_poseview_select_after_removal(v, n);
        check(got == n - 1 && sel_at(v, n - 1),
              "FS-050", "and deleting the last row leaves the new last row "
              "selected rather than nothing");
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        check(tiku_trk_poseview_select_after_removal(v, 1) == -1,
              "FS-050", "icon mode has no next-one-down and selects nothing");
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    }

    printf("\nsweeping a volume that left (PVN-043)\n");
    {
        int before = tiku_trk_poseview_count(v);
        uint64_t dev = 0;
        int i;

        for (i = 0; i < before; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

            if (p != NULL) {
                p->model.facts.dev = (i == 0) ? 4242u : 7u;
                if (i == 0) { dev = 4242u; }
            }
        }
        check(tiku_trk_poseview_on_device(v, dev), "PVN-043",
              "a view can say whether it is showing anything on a volume, "
              "by IDENTITY -- a window on a mount point stays stat-able "
              "after the volume under it goes, so the deletion test that "
              "closes a vanished folder cannot see an unmount at all");
        check(!tiku_trk_poseview_on_device(v, 99999u), "PVN-043",
              "and it says no about a volume it has nothing from");
        check(tiku_trk_poseview_device_gone(v, dev) == 1 &&
              tiku_trk_poseview_count(v) == before - 1, "PVN-043",
              "the rows that lived on it go, and only those: an unmount "
              "takes entries out of other folders too, and leaving them "
              "offers the user things that are not there");
    }

    printf("\nwhat the Size column says, and what sorts first "
           "(MA-024, MA-039)\n");
    {
        tiku_trk_poseview_t *v2;
        char sub2[256];
        int i, folder_at = -1, file_at = -1;

        /* A folder whose NAME sorts last, so folders-first is the only
         * thing that could put it in front. */
        snprintf(sub2, sizeof sub2, "%s/zzz", dir);
        (void)mkdir(sub2, 0755);
        v2 = tiku_trk_poseview_new(b, dir);
        if (v2 != NULL) {
            (void)tiku_trk_poseview_reload(v2);
            for (i = 0; i < tiku_trk_poseview_count(v2); i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v2, i);

                if (p == NULL) { continue; }
                if (tiku_trk_model_is_container(&p->model) &&
                    folder_at < 0) { folder_at = i; }
                if (!tiku_trk_model_is_container(&p->model) &&
                    file_at < 0) { file_at = i; }
            }
            check(folder_at >= 0 && file_at >= 0 && folder_at < file_at,
                  "MA-039",
                  "folders sort ahead of files, which is the setting's "
                  "default and what makes a listing readable");
            tiku_trk_poseview_set_folders_first(v2, 0);
            folder_at = file_at = -1;
            for (i = 0; i < tiku_trk_poseview_count(v2); i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v2, i);

                if (p == NULL) { continue; }
                if (tiku_trk_model_is_container(&p->model) &&
                    folder_at < 0) { folder_at = i; }
                if (!tiku_trk_model_is_container(&p->model) &&
                    file_at < 0) { file_at = i; }
            }
            check(file_at >= 0 && folder_at >= 0 && file_at < folder_at,
                  "MA-039",
                  "turned off, the listing is in column order alone -- a "
                  "name that sorts first is first whatever it is");
            {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v2, 0);
                char buf[64];
                tiku_trk_model_t vol;

                /* A volume reports its capacity where a file reports its
                 * size; a plain folder reports neither. */
                if (p != NULL) {
                    vol = p->model;
                    p->model.kind = TIKU_TRK_KIND_VOLUME;
                    p->model.facts.total = 4096u * 1024u;
                    check(strcmp(tiku_trk_poseview_cell_text(v2, p,
                              TIKU_TRK_COL_SIZE, buf, sizeof buf), "-") != 0,
                          "MA-024",
                          "a volume's Size is its CAPACITY, not a dash -- "
                          "the fact is carried and nothing was reading it");
                    p->model = vol;
                    p->model.kind = TIKU_TRK_KIND_SYMLINK;
                    p->model.facts.size = 12;
                    check(strcmp(tiku_trk_poseview_cell_text(v2, p,
                              TIKU_TRK_COL_SIZE, buf, sizeof buf), "-") == 0,
                          "MA-024",
                          "and a link shows none: its own bytes are not "
                          "what anyone reading the column is asking about");
                    p->model = vol;
                }
            }
            tiku_trk_poseview_free(v2);
        }
        (void)rmdir(sub2);
    }

    printf("\nthe columns a listing can show (CW-025, TS-023)\n");
    {
        char buf[128];
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);

        check(tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_PATH) == 1 &&
              tiku_trk_poseview_has_column(v, TIKU_TRK_COL_PATH),
              "CW-025",
              "Location is a column the listing can be given: where an "
              "entry IS was a fact every model carried and no column could "
              "show");
        check(p0 != NULL &&
              strcmp(tiku_trk_poseview_cell_text(v, p0, TIKU_TRK_COL_PATH,
                                                 buf, sizeof buf),
                     dir) == 0, "CW-025",
              "and it shows the folder the entry is IN, not its own path: "
              "the row already says its name");
        check(tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_PERM) == 1 &&
              strcmp(tiku_trk_poseview_cell_text(v, p0, TIKU_TRK_COL_PERM,
                                                 buf, sizeof buf),
                     "rw-") == 0, "CW-025",
              "Permissions is another, and it answers what THIS user may "
              "do rather than printing the raw mode bits");
        check(tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_PATH) == 1 &&
              !tiku_trk_poseview_has_column(v, TIKU_TRK_COL_PATH),
              "CW-025", "and the same item takes it away again");
        check(tiku_trk_poseview_last_column(v, TIKU_TRK_COL_NAME) &&
              tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_NAME) == 0,
              "TS-023",
              "the name column cannot be removed, and the view can SAY so "
              "rather than refusing silently -- a listing with no columns "
              "is a window showing nothing");
        (void)tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_PERM);
    }

    printf("\ncolumns that show an ATTRIBUTE (CW-026)\n");
    {
        tiku_trk_store_t *st = (b->ops->state_store != NULL)
                                   ? b->ops->state_store(b) : NULL;
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);
        char buf[128];
        int f;

        f = tiku_trk_poseview_attr_column(v, "user.trk.author", "Author",
                                          120);
        check(f >= TIKU_TRK_COL_ATTR_BASE, "CW-026",
              "an attribute can be given a column id, which is what lets "
              "the rest of the column machinery carry it");
        check(tiku_trk_poseview_attr_column(v, "user.trk.author", "Author",
                                            120) == f, "CW-026",
              "and asking again gives the same one rather than a second "
              "column for one attribute");
        check(tiku_trk_poseview_toggle_column(v, f) == 1 &&
              tiku_trk_poseview_has_column(v, f), "CW-026",
              "it is added and removed by the same toggle as every other "
              "column");
        if (st != NULL && p0 != NULL) {
            (void)tiku_trk_state_write(st, p0->model.path,
                                       "user.trk.author", "Ada", 3);
            check(strcmp(tiku_trk_poseview_cell_text(v, p0, f, buf,
                                                     sizeof buf),
                         "Ada") == 0, "CW-026",
                  "and the cell shows what the entry's attribute says");
            check(strcmp(tiku_trk_poseview_cell_text(v,
                             tiku_trk_poseview_at(v, 1), f, buf,
                             sizeof buf), "-") == 0, "CW-026",
                  "while an entry with none shows a dash: the column is "
                  "the listing's, and a blank would read as not measured "
                  "yet rather than as none");
        }
        {
            /* The layout survives by NAME: a slot number means nothing to
             * the next session. */
            tiku_trk_poseview_t *again = tiku_trk_poseview_new(b, dir);

            (void)tiku_trk_poseview_save_columns(v);
            if (again != NULL) {
                (void)tiku_trk_poseview_reload(again);
                (void)tiku_trk_poseview_attr_column(again, "user.trk.other",
                                                    "Other", 80);
                (void)tiku_trk_poseview_restore_columns(again);
                {
                    const char *got = NULL;
                    const tiku_trk_column_t *cs = NULL;
                    int i, n = tiku_trk_poseview_columns(again, &cs);

                    for (i = 0; i < n && cs != NULL; i++) {
                        if (cs[i].field >= TIKU_TRK_COL_ATTR_BASE) {
                            got = tiku_trk_poseview_attr_of(again,
                                                            cs[i].field);
                        }
                    }
                    check(got != NULL &&
                          strcmp(got, "user.trk.author") == 0, "CW-026",
                          "a saved layout comes back showing the same "
                          "ATTRIBUTE even when this session numbered its "
                          "columns differently");
                }
                tiku_trk_poseview_free(again);
            }
        }
        (void)tiku_trk_poseview_toggle_column(v, f);
    }

    printf("\nwhat a selection the user cannot see is (PVL-054)\n");
    {
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
        tiku_trk_poseview_set_visible_rows(v, 2);
        tiku_trk_poseview_scroll_to(v, 0);
        tiku_trk_poseview_type_clear(v);
        /* "sigma" is the last of the six names, two rows past the window. */
        tiku_trk_poseview_type_ahead(v, 's');
        tiku_trk_poseview_type_ahead(v, 'i');
        check(tiku_trk_poseview_scroll(v) > 0, "PVL-054",
              "typing a name that is off screen scrolls to it: a selection "
              "the user cannot see is one they cannot tell happened");
        tiku_trk_poseview_type_clear(v);
        tiku_trk_poseview_scroll_to(v, 0);
        {
            char err[64];

            (void)tiku_trk_poseview_select_matching(v, "sigma*",
                      TIKU_TRK_MATCH_GLOB, 1, 0, err, sizeof err);
            check(tiku_trk_poseview_scroll(v) > 0, "PVL-054",
                  "and so does selecting by pattern -- a window that "
                  "selected a row off screen looks like one that did "
                  "nothing");
        }
        tiku_trk_poseview_scroll_to(v, 0);
    }

    printf("\nquery windows stay lists (Q-041)\n");
    {
        tiku_trk_model_t result[2];
        const tiku_trk_column_t *cols = NULL;
        int nc;

        result[0] = tiku_trk_poseview_at(v, 0)->model;
        result[1] = tiku_trk_poseview_at(v, 1)->model;
        (void)tiku_trk_poseview_set_results(v, result, 2);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        check(tiku_trk_poseview_mode(v) == TIKU_TRK_MODE_LIST, "Q-041",
              "a query result view refuses icon mode");
        check(tiku_trk_poseview_is_query(v), "Q-041",
              "and remains identified as results rather than a folder");
        nc = tiku_trk_poseview_columns(v, &cols);
        check(nc == 4 && cols[0].field == TIKU_TRK_COL_NAME &&
              cols[1].field == TIKU_TRK_COL_PATH &&
              cols[2].field == TIKU_TRK_COL_SIZE &&
              cols[3].field == TIKU_TRK_COL_DATE, "Q-041",
              "with the stable Name, Location, Size and Modified columns");
    }

    tiku_trk_poseview_free(v);
    tiku_trk_backend_close(b);
    rm_tree(dir);

    printf("\n%s\n", fails ? "FAILURES" : "all poseview rules match the source");
    return fails ? 1 : 0;
}
