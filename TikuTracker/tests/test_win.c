/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_win.c - window chrome capabilities.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_ui.h"
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

/** @brief A window of two controls and a line of prose, for narration. */
static void
narrated_draw(tiku_window_t *win, tiku_surface_t *s,
              tiku_rect_t content, void *ctx)
{
    tiku_rect_t b = { content.x + 8, content.y + 8, 80, 24 };
    tiku_rect_t c = { content.x + 8, content.y + 40, 120, 16 };

    (void)win;
    (void)ctx;
    tiku_ui_button(s, b, "Apply", TIKU_S_DEFAULT);
    tiku_ui_checkbox(s, c, "Loud", TIKU_S_ON);
    tiku_text(s, tiku_font_plain(), content.x + 8, content.y + 70,
                   "three files", TIKU_C_TEXT);
}

/** @brief The same window, plus one bare pixel the wire cannot carry. */
static void
stroking_draw(tiku_window_t *win, tiku_surface_t *s,
              tiku_rect_t content, void *ctx)
{
    narrated_draw(win, s, content, ctx);
    tiku_pixel(s, content.x + 2, content.y + 2, TIKU_C_TEXT);
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

    printf("\na window can say what it shows, from its own display list\n");
    {
        tiku_surface_t *surface = tiku_surface_new(800, 600, TIKU_C_PANEL);
        tiku_workspace_t *ws = tiku_workspace_new(surface);
        tiku_rect_t at = { 100, 100, 300, 200 };
        tiku_window_t *w =
            tiku_workspace_open(ws, "Sound", at, narrated_draw, NULL, NULL);
        char lines[2048];
        size_t used;

        /* A sentinel where the window sits: narration that painted the
         * shown surface to answer would erase it. */
        surface->px[(size_t)150 * 800u + 150u] = 0x123456u;
        used = tiku_workspace_narrate(ws, w, lines, sizeof lines);

        check(used > 0u, "narration comes back non-empty");
        check(strstr(lines, "button \"Apply\", the default") != NULL,
              "and says the button by name, with its ring");
        check(strstr(lines, "checkbox \"Loud\", on") != NULL,
              "and the checkbox with its state");
        check(strstr(lines, "text \"three files\"") != NULL,
              "and the prose the window shows");
        check(strstr(lines, "could not carry") == NULL,
              "a window of controls and text narrates whole");
        check(surface->px[(size_t)150 * 800u + 150u] == 0x123456u,
              "and the shown surface was not painted on to answer");

        w = tiku_workspace_open(ws, "Marks", at, stroking_draw, NULL, NULL);
        used = tiku_workspace_narrate(ws, w, lines, sizeof lines);
        check(used > 0u &&
              strstr(lines, "could not carry") != NULL,
              "a window that strokes bare pixels confesses, not omits");

        check(tiku_workspace_narrate(ws, NULL, lines, sizeof lines) == 0u &&
              lines[0] == '\0',
              "no window narrates as silence, not as a fault");

        /* The walkable form: the same window as machine lines a driver
         * can ACT on.  The button's line is pinned whole -- kind, label,
         * the rect it was drawn at, and its state -- because a rect a
         * reader cannot trust is a rect nobody can click. */
        w = tiku_workspace_open(ws, "Sound2", at, narrated_draw, NULL,
                                     NULL);
        used = tiku_workspace_controls(ws, w, lines, sizeof lines);
        {
            char want[128];
            tiku_rect_t c2 = tiku_window_content(w);

            snprintf(want, sizeof want,
                     "button\t\"Apply\"\t%d %d 80 24\t%u\n",
                     c2.x + 8, c2.y + 8, (unsigned)TIKU_S_DEFAULT);
            check(used > 0u && strstr(lines, want) != NULL,
                  "a control's fact line carries its kind, label, rect "
                  "and state");
            check(strstr(lines, "checkbox\t\"Loud\"") != NULL,
                  "and every control is a line of its own");
        }
        tiku_workspace_free(ws);
        tiku_surface_free(surface);
    }

    /* Ghosts: with a clock, a closed window leaves its picture behind
     * for a moment and the workspace says it is moving; without one,
     * nothing lingers; a minimized window flies, a restored one rises. */
    {
        tiku_surface_t *gs = tiku_surface_new(300, 200, TIKU_RGB(1, 2, 3));
        tiku_workspace_t *gw = tiku_workspace_new(gs);
        tiku_window_t *win;
        int64_t now = 1000000;

        check(gw != NULL, "a workspace for ghosts");
        tiku_workspace_set_now(gw, now);
        win = tiku_workspace_open(gw, "Ghost", (tiku_rect_t){ 60, 40, 120, 90 },
                                  NULL, NULL, NULL);
        check(win != NULL, "a window to lose");
        tiku_workspace_set_now(gw, now + 1000000);   /* long open: still */
        tiku_workspace_draw(gw);
        check(!tiku_workspace_animating(gw), "nothing moves");
        tiku_workspace_close(gw, win);
        tiku_workspace_set_now(gw, now + 1010000);
        tiku_workspace_draw(gw);
        check(tiku_workspace_animating(gw), "something is moving");       /* the ghost is going */
        tiku_workspace_set_now(gw, now + 2000000);
        tiku_workspace_draw(gw);
        check(!tiku_workspace_animating(gw), "nothing moves");      /* and gone */
        win = tiku_workspace_open(gw, "Away", (tiku_rect_t){ 60, 40, 120, 90 },
                                  NULL, NULL, NULL);
        tiku_workspace_set_now(gw, now + 4000000);
        tiku_workspace_draw(gw);
        tiku_workspace_minimize(gw, win, (tiku_rect_t){ 280, 180, 16, 16 });
        check(win->minimized, "the window is minimized");
        tiku_workspace_set_now(gw, now + 4050000);
        tiku_workspace_draw(gw);
        check(tiku_workspace_animating(gw), "something is moving");
        tiku_workspace_set_now(gw, now + 5000000);
        tiku_workspace_draw(gw);
        check(!tiku_workspace_animating(gw), "nothing moves");
        tiku_workspace_restore(gw, win);
        check(!win->minimized && win->opened_at == now + 5000000, "restored, and set to rise");
        tiku_workspace_set_now(gw, now + 5010000);
        tiku_workspace_draw(gw);
        check(tiku_workspace_animating(gw), "something is moving");       /* it rises */
        /* No clock: a close leaves nothing. */
        tiku_workspace_set_now(gw, 0);
        tiku_workspace_close(gw, win);
        tiku_workspace_draw(gw);
        check(!tiku_workspace_animating(gw), "nothing moves");
        tiku_workspace_free(gw);
        tiku_surface_free(gs);
    }
    printf("\n%s\n", fails ? "window rule FAILED" :
                              "every window rule holds");
    return fails ? 1 : 0;
}
