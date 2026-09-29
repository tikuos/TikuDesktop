/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_window_grab.c - what a screenshot needs of the workspace: a window
 * drawn alone shows all of itself though another covers it, and nothing
 * outside its frame; a window marked hidden leaves the desk behind it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_ui.h"
#include "tiku_window.h"

#define RED  0xffd02020u
#define BLUE 0xff2040c0u
#define INK  0xff000000u

static int fails;

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) fails++;
}

/** @brief A window's content, one colour: its context. */
static void fill(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                 void *ctx)
{
    (void)w;
    tiku_fill(s, r, *(const tiku_rgb_t *)ctx);
}

/** @brief The pixel at @p x, @p y of @p s, its colour alone. */
static uint32_t at(const tiku_surface_t *s, int x, int y)
{
    return s->px[(size_t)y * (size_t)s->w + (size_t)x] & 0xffffffu;
}

/** @brief Two windows, the second over the first; drawn together, the
 *         first alone, and without the first. */
int main(void)
{
    static const tiku_rgb_t red = RED, blue = BLUE;
    tiku_surface_t *s = tiku_surface_new(400, 300, TIKU_C_BACKDROP);
    tiku_surface_t *out = tiku_surface_new(400, 300, INK);
    tiku_workspace_t *ws = tiku_workspace_new(s);
    tiku_window_t *a = tiku_workspace_open(ws, "Under",
        (tiku_rect_t){20, 20, 200, 150}, fill, (void *)&red, NULL);
    tiku_window_t *b = tiku_workspace_open(ws, "Over",
        (tiku_rect_t){120, 90, 200, 150}, fill, (void *)&blue, NULL);
    tiku_rect_t ca = tiku_window_content(a);

    printf("Window grab: one window alone, one left out\n");
    tiku_workspace_draw(ws);
    check(at(s, 150, 140) == (BLUE & 0xffffffu), "WG-001",
          "on the desk, the upper window covers the lower");
    tiku_workspace_draw_window(ws, a, out);
    check(at(out, 150, 140) == (RED & 0xffffffu) &&
          at(out, ca.x + 5, ca.y + 5) == (RED & 0xffffffu), "WG-002",
          "drawn alone, the lower window is all itself, covered or not");
    check(at(out, 300, 250) == (INK & 0xffffffu) &&
          at(out, 10, 10) == (INK & 0xffffffu), "WG-003",
          "and nothing is drawn outside its frame, not even a shadow");
    a->hidden = 1;
    tiku_workspace_draw(ws);
    check(at(s, ca.x + 5, ca.y + 5) == (TIKU_C_BACKDROP & 0xffffffu) &&
          at(s, 150, 140) == (BLUE & 0xffffffu), "WG-004",
          "hidden, it leaves the desk behind it; the other stays");
    a->hidden = 0;
    tiku_workspace_draw(ws);
    check(at(s, ca.x + 5, ca.y + 5) == (RED & 0xffffffu), "WG-005",
          "and shown again, it is back");
    (void)b;
    tiku_workspace_free(ws);
    tiku_surface_free(out);
    tiku_surface_free(s);
    printf("%s\n", fails ? "FAIL" : "PASS: a window alone, a window left out");
    return fails ? 1 : 0;
}
