/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * draw_demo.c - a window of the board's own: a line of text, a button
 * that counts its presses, and a gauge that follows a slider.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw.h"

#include <stdio.h>

TIKU_PROCESS(draw_demo_process, "draw-demo");

static tiku_draw_window_t win;
static int presses, level = 40;

/** @brief The whole window, from its state. */
static void
paint(void)
{
    char line[40];

    tiku_draw_begin(&win);
    tiku_draw_panel(&win, 0, 0, win.w, win.h);
    tiku_draw_text(&win, TIKU_DRAW_BOLD, 12, 22, "Drawn by the board",
                   TIKU_DRAW_TEXT);
    snprintf(line, sizeof line, "Pressed %d time%s", presses,
             presses == 1 ? "" : "s");
    tiku_draw_text(&win, TIKU_DRAW_PLAIN, 12, 46, line, TIKU_DRAW_TEXT);
    /* The button as wide as its word wants, measured as the desktop
     * will draw it. */
    tiku_draw_button(&win, 12, 60, tiku_draw_text_width(TIKU_DRAW_PLAIN,
                                                       "Count") + 40, 24,
                     "Count", 0u);
    tiku_draw_slider(&win, 12, 96, win.w - 24, 24, 0, 100, level);
    tiku_draw_gauge(&win, 12, 128, win.w - 24, 20, level * 10);
    (void)tiku_draw_present(&win);
}

TIKU_PROCESS_THREAD(draw_demo_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    (void)tiku_process_register("draw-demo", &draw_demo_process);
    (void)tiku_draw_open(&win, "Board window", 240, 160,
                         &draw_demo_process);
    paint();

    while (1) {
        tiku_draw_window_t *w;

        TIKU_PROCESS_WAIT_EVENT();
        /* Every event that queued while this thread was elsewhere. */
        while ((w = tiku_draw_event(ev, data)) != NULL) {
        if (w->event.kind == TIKU_DRAW_POINTER_DOWN) {
            if (w->event.y >= 60 && w->event.y < 84 && w->event.x >= 12 &&
                w->event.x < 132) {
                presses++;
            } else if (w->event.y >= 96 && w->event.y < 120) {
                level = (w->event.x - 12) * 100 / (w->w - 24);
                level = level < 0 ? 0 : level > 100 ? 100 : level;
            }
            paint();
        } else if (w->event.kind == TIKU_DRAW_SESSION) {
            /* A desktop arrived: the count starts over for it. */
            presses = 0;
            paint();
        } else if (w->event.kind == TIKU_DRAW_CLOSED) {
            /* Opened again on the next boot, not now: the person closed it. */
        }
        }
    }

    TIKU_PROCESS_END();
}

