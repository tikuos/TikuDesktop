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
static int events;
static int destroys;
static int allow_close;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

static int
on_event(tiku_trk_win_t *win, const tiku_desk_event_t *event, void *ctx)
{
    (void)win;
    (void)ctx;
    events++;
    return event->type == TIKU_DESK_EVENT_KEY_DOWN;
}

static int
on_close(tiku_trk_win_t *win, void *ctx)
{
    (void)win;
    (void)ctx;
    return allow_close;
}

static void
on_destroy(tiku_trk_win_t *win, void *ctx)
{
    (void)win;
    (void)ctx;
    destroys++;
}

int
main(void)
{
    tiku_trk_desk_t *desk = tiku_trk_desk_new(NULL);
    tiku_desk_rect_t frame = { 220, 244, 560, 191 };
    tiku_trk_win_t *win;
    tiku_trk_win_t *hit_win = NULL;
    tiku_desk_event_t event = { 0 };

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

    printf("\napplication-owned window lifecycle\n");
    tiku_trk_win_set_handlers(win, on_event, on_close, on_destroy);
    event.type = TIKU_DESK_EVENT_KEY_DOWN;
    check(tiku_trk_win_send(win, &event) == 1 && events == 1,
          "a normalized event reaches the window that owns its controller");
    allow_close = 0;
    check(!tiku_trk_desk_request_close(desk, win) && win->open &&
          destroys == 0,
          "a window may refuse a close request without being destroyed");
    allow_close = 1;
    check(tiku_trk_desk_request_close(desk, win) && !win->open &&
          destroys == 1,
          "an accepted close destroys application state exactly once");

    tiku_trk_desk_free(desk);
    printf("\n%s\n", fails ? "window rule FAILED" :
                              "every window rule holds");
    return fails ? 1 : 0;
}
