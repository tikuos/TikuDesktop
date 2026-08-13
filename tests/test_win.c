/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_win.c - window chrome capabilities.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "tiku_trk_win.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_trk_desk_t *desk = tiku_trk_desk_new(NULL);
    tiku_desk_rect_t frame = { 220, 244, 560, 191 };
    tiku_trk_win_t *win;
    tiku_trk_win_t *hit_win = NULL;

    printf("window size controls (Q-001)\n");
    win = tiku_trk_desk_open(desk, "Find", frame, NULL, NULL, NULL);
    check(win != NULL, "a window opens");
    check(tiku_trk_desk_hit(desk, 294, 250, &hit_win) ==
          TIKU_TRK_HIT_ZOOM && hit_win == win,
          "ordinary windows expose their zoom button");
    check(tiku_trk_desk_hit(desk, 770, 425, &hit_win) ==
          TIKU_TRK_HIT_RESIZE,
          "ordinary windows expose their resize corner");

    tiku_trk_win_set_size_controls(win, 0, 0);
    check(tiku_trk_desk_hit(desk, 294, 250, &hit_win) ==
          TIKU_TRK_HIT_TAB,
          "Q-001: a non-zoomable window has no active zoom button");
    check(tiku_trk_desk_hit(desk, 770, 425, &hit_win) !=
          TIKU_TRK_HIT_RESIZE,
          "Q-001: a non-resizable window has no active resize corner");

    tiku_trk_desk_free(desk);
    printf("\n%s\n", fails ? "window rule FAILED" :
                              "every window rule holds");
    return fails ? 1 : 0;
}
