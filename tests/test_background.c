/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_background.c - the picture behind a folder: record, and placement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_trk_background.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_trk_bg_t bg;
    char rec[600];
    int x, y, w, h;

    printf("the record a node carries (CW-020)\n");
    memset(&bg, 0, sizeof bg);
    bg.mode = TIKU_TRK_BG_AT_OFFSET;
    bg.x = 120;
    bg.y = 340;
    snprintf(bg.path, sizeof bg.path, "%s", "/tmp/pic tures/wall.png");
    check(tiku_trk_bg_format(&bg, rec, sizeof rec) > 0, "CW-020",
          "a background writes down as one line");
    memset(&bg, 0, sizeof bg);
    check(tiku_trk_bg_parse(rec, &bg) == 1 &&
          bg.mode == TIKU_TRK_BG_AT_OFFSET && bg.x == 120 && bg.y == 340 &&
          strcmp(bg.path, "/tmp/pic tures/wall.png") == 0, "CW-020",
          "and reads back whole -- including a path with a space in it, "
          "which is why the path is last and takes the rest of the line");
    check(tiku_trk_bg_parse("nonsense", &bg) == 0, "CW-020",
          "a record that is not one of ours is refused rather than read as "
          "a picture at 0,0");
    check(tiku_trk_bg_parse("", &bg) == 0, "CW-020",
          "and so is an empty one");
    check(tiku_trk_bg_parse("9 0 0 /tmp/wall.png", &bg) == 0, "CW-020",
          "a mode number nothing lays out is refused too, rather than "
          "falling through to a picture at the corner");

    printf("\nwhere the picture goes (AW-016)\n");
    memset(&bg, 0, sizeof bg);
    bg.mode = TIKU_TRK_BG_SCALED;
    check(tiku_trk_bg_place(&bg, 400, 300, 64, 64, 0, &x, &y, &w, &h) &&
          x == 0 && y == 0 && w == 400 && h == 300, "AW-016",
          "scaled covers the view whatever the picture's own size");
    check(!tiku_trk_bg_place(&bg, 400, 300, 64, 64, 1, &x, &y, &w, &h),
          "AW-016", "and is drawn once");

    bg.mode = TIKU_TRK_BG_CENTERED;
    check(tiku_trk_bg_place(&bg, 400, 300, 100, 50, 0, &x, &y, &w, &h) &&
          x == 150 && y == 125 && w == 100 && h == 50, "AW-016",
          "centred keeps its own size and sits in the middle");
    check(tiku_trk_bg_place(&bg, 100, 100, 300, 300, 0, &x, &y, &w, &h) &&
          x == -100 && y == -100, "AW-016",
          "a picture bigger than the view is centred over it rather than "
          "shrunk: the caller clips, so the middle of it is what shows");

    bg.mode = TIKU_TRK_BG_AT_OFFSET;
    bg.x = 30;
    bg.y = 40;
    check(tiku_trk_bg_place(&bg, 400, 300, 100, 50, 0, &x, &y, &w, &h) &&
          x == 30 && y == 40 && w == 100 && h == 50, "AW-016",
          "at-offset puts its corner where it was told");

    bg.mode = TIKU_TRK_BG_TILED;
    {
        int i = 0, seen = 0, covered_x = 0, covered_y = 0;

        while (tiku_trk_bg_place(&bg, 400, 300, 150, 100, i, &x, &y, &w, &h)) {
            if (x + w > covered_x) { covered_x = x + w; }
            if (y + h > covered_y) { covered_y = y + h; }
            seen++;
            i++;
            if (i > 100) { break; }
        }
        check(seen == 9, "AW-016",
              "tiled walks a grid that covers the view: three across and "
              "three down for a 150x100 picture in 400x300");
        check(covered_x >= 400 && covered_y >= 300, "AW-016",
              "and the last row and column run past the edge rather than "
              "stopping short of it, which would leave a bare strip");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the background holds");
    return fails ? 1 : 0;
}
