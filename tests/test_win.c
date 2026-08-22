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

#include "tiku_window.h"

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
on_event(tiku_window_t *win, const tiku_event_t *event, void *ctx)
{
    (void)win;
    (void)ctx;
    events++;
    return event->type == TIKU_EVENT_KEY_DOWN;
}

static int
on_close(tiku_window_t *win, void *ctx)
{
    (void)win;
    (void)ctx;
    return allow_close;
}

static void
on_destroy(tiku_window_t *win, void *ctx)
{
    (void)win;
    (void)ctx;
    destroys++;
}

int
main(void)
{
    tiku_workspace_t *desk = tiku_workspace_new(NULL);
    tiku_rect_t frame = { 220, 244, 560, 191 };
    tiku_window_t *win;
    tiku_window_t *hit_win = NULL;
    tiku_event_t event = { 0 };

    printf("window size controls (Q-001)\n");
    win = tiku_workspace_open(desk, "Find", frame, NULL, NULL, NULL);
    check(win != NULL, "a window opens");
    {
        /* Found by asking rather than by arithmetic: the tab is as wide
         * as its title, which is the tab's business. */
        int x, zoom_x = -1;

        for (x = 224; x < 500 && zoom_x < 0; x++) {
            if (tiku_workspace_hit(desk, x, 250, &hit_win) ==
                TIKU_HIT_ZOOM) {
                zoom_x = x;
            }
        }
        check(zoom_x > 0 && hit_win == win,
              "ordinary windows expose their zoom button");
        check(tiku_workspace_hit(desk, 770, 425, &hit_win) ==
              TIKU_HIT_RESIZE,
              "ordinary windows expose their resize corner");

        tiku_window_set_size_controls(win, 0, 0);
        for (x = 224; x < 500; x++) {
            if (tiku_workspace_hit(desk, x, 250, &hit_win) ==
                TIKU_HIT_ZOOM) {
                break;
            }
        }
        check(x == 500,
              "Q-001: a non-zoomable window has no active zoom button");
    }
    check(tiku_workspace_hit(desk, 770, 425, &hit_win) !=
          TIKU_HIT_RESIZE,
          "Q-001: a non-resizable window has no active resize corner");

    printf("\napplication-owned window lifecycle\n");
    tiku_window_set_handlers(win, on_event, on_close, on_destroy);
    event.type = TIKU_EVENT_KEY_DOWN;
    check(tiku_window_send(win, &event) == 1 && events == 1,
          "a normalized event reaches the window that owns its controller");
    allow_close = 0;
    check(!tiku_workspace_request_close(desk, win) && win->open &&
          destroys == 0,
          "a window may refuse a close request without being destroyed");
    allow_close = 1;
    check(tiku_workspace_request_close(desk, win) && !win->open &&
          destroys == 1,
          "an accepted close destroys application state exactly once");

    tiku_workspace_free(desk);
    printf("\n%s\n", fails ? "window rule FAILED" :
                              "every window rule holds");
    return fails ? 1 : 0;
}
