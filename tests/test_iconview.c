/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_iconview.c - the icon viewport: cells, the extent, origins, what a
 * click lands on, and where an arrow goes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_poseview.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static char work[256];

static void
sh(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (system(cmd) != 0) { /* the caller checks the effect */ }
}

/** @brief A label measure: eight pixels a character, no font needed. */
static int
measure(const char *text, void *ctx)
{
    (void)ctx;
    return (text != NULL) ? (int)strlen(text) * 8 : 0;
}

/** @brief An icon mask with a transparent left half, so sides differ. */
static int
half_mask(const tiku_trk_model_t *m, int size, int px, int py, void *ctx)
{
    (void)m;
    (void)py;
    (void)ctx;
    return (px >= size / 2);
}

int
main(void)
{
    tiku_trk_backend_t *b = tiku_trk_backend_posix_open();
    tiku_trk_poseview_t *v;
    int gx, gy, ox, oy, x, y, w, h;

    snprintf(work, sizeof work, "/tmp/trk-iconview-%d", (int)getpid());
    sh("rm -rf %s && mkdir -p %s", work, work);
    sh("cd %s && touch alpha beta gamma delta epsilon", work);

    v = tiku_trk_poseview_new(b, work);
    tiku_trk_poseview_set_measure(v, measure, NULL);
    (void)tiku_trk_poseview_reload(v);
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(v, 32);

    printf("the cell is as tall as what goes in it (PVL-007)\n");
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    {
        int without = gy;

        tiku_trk_poseview_set_font_metrics(v, 12, 9);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(gy == 32 + 12 + 1, "PVL-007",
              "icon mode: the art plus the label's height, since the name "
              "is drawn UNDER the icon");
        check(gy != without, "PVL-007",
              "which is not what a cell derived from the art alone gives");
        tiku_trk_poseview_set_font_metrics(v, 24, 18);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(gy == 32 + 24 + 1, "PVL-007",
              "a bigger face makes a taller cell -- a height fixed to the "
              "icon clips the name at one font and floats it at another");
        check(tiku_trk_poseview_row_height(v) == (int)(24 * 1.65f + 0.999f),
              "PVL-007", "and a list row is the font's own height, scaled");
        tiku_trk_poseview_set_font_metrics(v, 12, 9);
    }
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    check(gy == 16, "PVL-007",
          "mini mode: the art's height while the art is the taller, since "
          "there the two sit side by side rather than stacked");
    tiku_trk_poseview_set_font_metrics(v, 24, 18);
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    check(gy == 25, "PVL-007",
          "and the LABEL's when the label is -- a row shorter than the "
          "text in it clips every name");
    tiku_trk_poseview_set_font_metrics(v, 12, 9);
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);

    printf("\nwhat a click lands on (PVS-019, IV-026)\n");
    (void)tiku_trk_poseview_place_unplaced(v, 400);
    {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, 0);
        int ix, iy, iw, ih, lx, ly, lw, lh, px, py, pw, ph, cx, cy;

        tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih,
                                     &lx, &ly, &lw, &lh);
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        check(ly >= iy + ih, "PVS-019",
              "the label sits below the icon, not over it");
        check(px <= ix && px + pw >= ix + iw, "PVS-019",
              "and the rect a band sweeps is their union");
        check(tiku_trk_poseview_pose_at(v, ix + iw / 2, iy + 2) == p,
              "PVS-019", "a point on the icon hits the pose");
        check(tiku_trk_poseview_pose_at(v, lx + 2, ly + 2) == p, "PVS-019",
              "so does a point on the name, where the name is DRAWN");
        check(lw < iw * 3, "PVS-019",
              "the fixture's names are shorter than their cells, which is "
              "what the next check needs");
        tiku_trk_poseview_pose_location(v, p, &cx, &cy);
        check(tiku_trk_poseview_pose_at(v, cx + 1, ly + 2) != p,
              "PVS-019",
              "but the space BESIDE the name hits nothing, and neither "
              "does the empty bottom of the cell: in an icon mode a cell "
              "is mostly empty, and treating the cell as the pose is what "
              "makes the gap between two icons undraggable");
        check(tiku_trk_poseview_pose_at(v, ix + iw / 2,
                                        ly + lh + 2) != p, "PVS-019",
              "nor does the space under it");
        tiku_trk_poseview_set_icon_hit(v, half_mask, NULL);
        check(tiku_trk_poseview_pose_at(v, ix + iw - 2, iy + 2) == p,
              "IV-026", "with a mask, an opaque pixel still hits");
        check(tiku_trk_poseview_pose_at(v, ix + 1, iy + 2) != p, "IV-026",
              "and a TRANSPARENT one does not -- an icon is not a "
              "rectangle, and arming from its clear corner is what makes "
              "the gap between two icons impossible to sweep through");
        tiku_trk_poseview_set_icon_hit(v, NULL, NULL);
        check(tiku_trk_poseview_pose_at(v, ix + 1, iy + 2) == p, "IV-026",
              "without one the square answers, as it did before");
    }

    printf("\nthe scrollable area (PVL-049)\n");
    tiku_trk_poseview_extent(v, &x, &y, &w, &h);
    {
        int i, n = tiku_trk_poseview_count(v), lox = 0, hix = 0, hiy = 0;

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
            int px, py, pw, ph;

            tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
            if (i == 0 || px < lox)         { lox = px; }
            if (i == 0 || px + pw > hix)    { hix = px + pw; }
            if (i == 0 || py + ph > hiy)    { hiy = py + ph; }
        }
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(w == (hix - lox) + 2 * ox, "PVL-049",
              "the bounding box of every icon, outset by the layout's own "
              "margin so an icon at the origin is not flush to the edge");
        check(h >= hiy - y, "PVL-049", "in both directions");
    }
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_extent(v, &x, &y, &w, &h);
    check(h == tiku_trk_poseview_count(v) * tiku_trk_poseview_row_height(v),
          "PVL-049",
          "in list mode it is the rows, which is a different question with "
          "the same name");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);

    printf("\nthe box is grown, not walked (PVL-050)\n");
    {
        int w0, h0, w1, h1;

        tiku_trk_poseview_extent(v, &x, &y, &w0, &h0);
        tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 0), 900, 900,
                                    0);
        tiku_trk_poseview_extent(v, &x, &y, &w1, &h1);
        check(w1 > w0 && h1 > h0, "PVL-050",
              "moving an icon out grows the box");
        tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 0), 10, 10,
                                    0);
        tiku_trk_poseview_extent(v, &x, &y, &w1, &h1);
        check(w1 > w0 || h1 > h0, "PVL-050",
              "moving it back does NOT shrink it: a running box cannot "
              "shrink, and pretending otherwise costs a walk per move");
        {   /* A removal is the one thing that pays for the walk. */
            sh("rm -f %s/alpha", work);
            (void)tiku_trk_poseview_poll(v, 0, NULL, -1, 1);
            tiku_trk_poseview_extent(v, &x, &y, &w1, &h1);
            check(w1 <= w0 + 1 && h1 <= h0 + 1, "PVL-050",
                  "and a removal recomputes it, which is what keeps a "
                  "deleted icon from leaving empty space to scroll into");
        }
    }

    printf("\none origin per mode (PVL-009)\n");
    /* A window smaller than the content, or there is nothing to scroll and
     * every origin clamps to the same place. */
    tiku_trk_poseview_set_viewport(v, 60, 40);
    tiku_trk_poseview_set_origin(v, 40, 30);
    tiku_trk_poseview_origin(v, &x, &y);
    check(x == 40 && y == 30, "PVL-009", "an icon view scrolls in pixels");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_origin(v, &x, &y);
    check(x == 0 && y == 0, "PVL-009",
          "a list has no pixel origin of its own -- it counts rows");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_origin(v, &x, &y);
    check(x == 40 && y == 30, "PVL-009",
          "and coming back returns to where the icons were, which one "
          "shared origin could not do");
    {   /* And the two ICON modes do not share one either. */
        int ix0, iy0, mx, my, ix1, iy1;

        tiku_trk_poseview_origin(v, &ix0, &iy0);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
        tiku_trk_poseview_set_viewport(v, 60, 40);
        tiku_trk_poseview_set_origin(v, ix0 + 37, iy0 + 21);
        tiku_trk_poseview_origin(v, &mx, &my);
        check(mx != ix0 || my != iy0, "PVL-009",
              "the mini view is put somewhere else");
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_viewport(v, 60, 40);
        tiku_trk_poseview_origin(v, &ix1, &iy1);
        check(ix1 == ix0 && iy1 == iy0, "PVL-009",
              "and the icon view is where it was left, not where the mini "
              "view was put: their cells are different shapes, so the same "
              "pixel is a different place");
    }
    tiku_trk_poseview_set_origin(v, -500, -500);
    tiku_trk_poseview_origin(v, &x, &y);
    {
        int ex, ey, ew, eh;

        tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
        check(x >= ex && y >= ey, "PVL-009",
              "an origin before the content is clamped to it");
        tiku_trk_poseview_set_origin(v, 100000, 100000);
        tiku_trk_poseview_origin(v, &x, &y);
        check(x <= ex + ew && y <= ey + eh, "PVL-009",
              "and one past the end is too: rows of blank space with "
              "nothing to scroll back to is how a folder opens looking "
              "empty");
    }

    printf("\nHome, End and the pages (PVK-010, PVK-011)\n");
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 0, 0u);
    tiku_trk_poseview_set_origin(v, 0, 0);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_END, 0u);
    tiku_trk_poseview_origin(v, &x, &y);
    check(y > 0 && tiku_trk_poseview_selection_count(v) == 1, "PVK-010",
          "End moves the view and leaves the selection alone: there is no "
          "last ROW to select in a two-dimensional arrangement");
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_HOME, 0u);
    tiku_trk_poseview_origin(v, &x, &y);
    {
        int ex, ey, ew, eh;

        tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
        check(y == ey, "PVK-010", "and Home goes back to the top");
    }
    {
        int before, after;

        tiku_trk_poseview_origin(v, &x, &before);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_PGDN, 0u);
        tiku_trk_poseview_origin(v, &x, &after);
        check(after - before == 40, "PVK-011",
              "a page is exactly what the WINDOW shows -- forty pixels of "
              "a forty-pixel window -- not a count of rows a layout "
              "without rows does not have");
    }

    printf("\nwhat is on screen (PVK-011, PVL-062)\n");
    {
        int first = -1, last = -1, px, py, pw, ph, n;

        /* Parked on one pose's own corner, because the corner of the
         * BOUNDING BOX need not contain any pose at all: the extreme x can
         * come from one icon and the extreme y from another. */
        tiku_trk_poseview_pose_rect(v, tiku_trk_poseview_at(v, 0), &px, &py,
                                    &pw, &ph);
        tiku_trk_poseview_set_origin(v, px, py);
        check(tiku_trk_poseview_visible_range(v, &first, &last) &&
              first == 0, "PVL-062",
              "the view can say which poses are in the window");
        n = tiku_trk_poseview_count(v);
        check(last < n - 1, "PVL-062",
              "and that the ones past the bottom are NOT -- which is the "
              "whole point: a renderer that cannot ask walks every pose in "
              "the folder on every frame");
        tiku_trk_poseview_set_origin(v, px, py + 100000);
        {
            int f2 = -1, l2 = -1;

            (void)tiku_trk_poseview_visible_range(v, &f2, &l2);
            check(f2 != first, "PVL-062",
                  "the answer follows the origin");
        }
        tiku_trk_poseview_set_origin(v, px, py);
    }

    printf("\nkeeping the middle while the size changes (PVL-010)\n");
    {
        int y0, y1;

        tiku_trk_poseview_set_icon_size(v, 32);
        tiku_trk_poseview_set_origin(v, 0, 200);
        tiku_trk_poseview_origin(v, &x, &y0);
        (void)tiku_trk_poseview_step_icon_size(v, 1);
        tiku_trk_poseview_origin(v, &x, &y1);
        check(y1 > y0, "PVL-010",
              "growing the icons moves the origin down with the content: "
              "the pixels all moved, and an origin that did not would jump "
              "the view to the top-left");
    }

    printf("\nwhere an arrow goes (PVK-007, PVK-008)\n");
    {
        tiku_trk_pose_t *a, *bb, *cc;

        /* A deliberate arrangement: b to the right of a, c below a. */
        a = tiku_trk_poseview_at(v, 0);
        bb = tiku_trk_poseview_at(v, 1);
        cc = tiku_trk_poseview_at(v, 2);
        tiku_trk_poseview_move_pose(v, a, 100, 100, 0);
        tiku_trk_poseview_move_pose(v, bb, 200, 100, 0);
        tiku_trk_poseview_move_pose(v, cc, 100, 200, 0);
        tiku_trk_poseview_select_none(v);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0u);
        {
            int i, n = tiku_trk_poseview_count(v), sel = -1, topmost = -1;
            int besty = 0, bestx = 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
                int px, py, pw, ph;

                if (p->selected) { sel = i; }
                tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
                if (topmost < 0 || py < besty ||
                    (py == besty && px < bestx)) {
                    topmost = i;
                    besty = py;
                    bestx = px;
                }
            }
            check(sel == topmost, "PVK-008",
                  "the first arrow with nothing selected picks the "
                  "upper-left-most pose, whichever key it was");
        }
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, a->index, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_RIGHT, 0u);
        check(tiku_trk_poseview_at(v, 1)->selected, "PVK-007",
              "Right from an icon picks the one to its RIGHT, not the next "
              "in sort order -- a walk by index jumps across the window");
        /* One NEARER than the target, but the wrong way. */
        tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 3), 100, 70,
                                    0);
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, a->index, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0u);
        check(tiku_trk_poseview_at(v, 2)->selected, "PVK-007",
              "and Down picks the one below it, not the one ABOVE it that "
              "happens to be nearer -- a direction key is a half-plane, "
              "not a distance");
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, bb->index, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_RIGHT, 0u);
        check(tiku_trk_poseview_at(v, 1)->selected, "PVK-007",
              "with nothing that way the selection stays where it is "
              "rather than wrapping to the other side of the window");
    }

    tiku_trk_poseview_free(v);
    tiku_trk_backend_close(b);
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "the icon viewport holds");
    return fails ? 1 : 0;
}
