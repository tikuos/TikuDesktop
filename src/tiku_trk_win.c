/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_win.c - the window system.
 *
 * Windows live in one array ordered back to front; drawing walks it forward
 * and hit-testing walks it backward, which is the whole of z-order.  Nothing
 * is cached: a window redraws its content when asked, so there is no backing
 * store to keep coherent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_win.h"
#include "tiku_desk_ui.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TAB_H    TIKU_TRK_WIN_TAB_H
#define BORDER   TIKU_TRK_WIN_BORDER
#define BUTTON   12          /* close / zoom square on the tab */
#define MIN_W   180
#define MIN_H    90

struct tiku_trk_desk {
    tiku_desk_surface_t *surf;
    tiku_trk_win_t       wins[TIKU_TRK_WIN_MAX];
    int                  order[TIKU_TRK_WIN_MAX];   /* back to front */
    int                  count;
    int                  focused;                   /* index, or -1  */
    tiku_desk_rect_t     reserved;
    /* tracking */
    tiku_trk_hit_t       track_what;
    int                  track_win;
    int                  track_x, track_y;
    void               (*backdrop)(tiku_desk_surface_t *s, void *ctx);
    void                *backdrop_ctx;
};

tiku_trk_desk_t *
tiku_trk_desk_new(tiku_desk_surface_t *surface)
{
    tiku_trk_desk_t *d = calloc(1, sizeof *d);

    if (d != NULL) {
        d->surf = surface;
        d->focused = -1;
        d->track_win = -1;
    }
    return d;
}

void
tiku_trk_desk_free(tiku_trk_desk_t *d)
{
    while (d != NULL && d->count > 0) {
        tiku_trk_desk_close(d, &d->wins[d->order[d->count - 1]]);
    }
    free(d);
}

void
tiku_trk_desk_set_reserved(tiku_trk_desk_t *d, tiku_desk_rect_t r)
{
    if (d != NULL) {
        d->reserved = r;
    }
}

int
tiku_trk_desk_count(const tiku_trk_desk_t *d)
{
    return (d != NULL) ? d->count : 0;
}

tiku_trk_win_t *
tiku_trk_desk_at(tiku_trk_desk_t *d, int i)
{
    if (d == NULL || i < 0 || i >= d->count) {
        return NULL;
    }
    return &d->wins[d->order[i]];
}

tiku_trk_win_t *
tiku_trk_desk_focused(tiku_trk_desk_t *d)
{
    if (d == NULL || d->focused < 0) {
        return NULL;
    }
    return &d->wins[d->focused];
}

/** @brief Index of a window in the z-order array, or -1. */
static int
order_index(tiku_trk_desk_t *d, int slot)
{
    int i;

    for (i = 0; i < d->count; i++) {
        if (d->order[i] == slot) {
            return i;
        }
    }
    return -1;
}

void
tiku_trk_desk_activate(tiku_trk_desk_t *d, tiku_trk_win_t *w)
{
    int slot, at, i;

    if (d == NULL || w == NULL) {
        return;
    }
    slot = (int)(w - d->wins);
    at = order_index(d, slot);
    if (at < 0) {
        return;
    }
    /* Lift to the front by sliding everything above it down one. */
    for (i = at; i < d->count - 1; i++) {
        d->order[i] = d->order[i + 1];
    }
    d->order[d->count - 1] = slot;
    d->focused = slot;
}

tiku_trk_win_t *
tiku_trk_desk_find(tiku_trk_desk_t *d, void *tag)
{
    int i;

    for (i = 0; d != NULL && i < TIKU_TRK_WIN_MAX; i++) {
        if (d->wins[i].open && d->wins[i].tag == tag) {
            return &d->wins[i];
        }
    }
    return NULL;
}

tiku_trk_win_t *
tiku_trk_desk_open(tiku_trk_desk_t *d, const char *title,
                   tiku_desk_rect_t frame, tiku_trk_win_draw_fn draw,
                   void *ctx, void *tag)
{
    int slot;
    tiku_trk_win_t *w;

    if (d == NULL) {
        return NULL;
    }
    /* Opening a folder that is already open brings its window forward -- the
     * spatial promise: one window per place, where you left it. */
    w = (tag != NULL) ? tiku_trk_desk_find(d, tag) : NULL;
    if (w != NULL) {
        tiku_trk_desk_activate(d, w);
        return w;
    }
    for (slot = 0; slot < TIKU_TRK_WIN_MAX; slot++) {
        if (!d->wins[slot].open) {
            break;
        }
    }
    if (slot == TIKU_TRK_WIN_MAX) {
        return NULL;
    }
    w = &d->wins[slot];
    memset(w, 0, sizeof *w);
    snprintf(w->title, sizeof w->title, "%s", title ? title : "");
    w->frame = frame;
    w->draw = draw;
    w->ctx = ctx;
    w->tag = tag;
    w->open = 1;
    w->resizable = 1;
    w->zoomable = 1;
    d->order[d->count++] = slot;
    d->focused = slot;
    return w;
}

void
tiku_trk_desk_close(tiku_trk_desk_t *d, tiku_trk_win_t *w)
{
    int slot, at, i;
    tiku_trk_win_destroy_fn destroy;
    void *ctx;

    if (d == NULL || w == NULL || !w->open) {
        return;
    }
    slot = (int)(w - d->wins);
    at = order_index(d, slot);
    w->open = 0;
    if (at >= 0) {
        for (i = at; i < d->count - 1; i++) {
            d->order[i] = d->order[i + 1];
        }
        d->count--;
    }
    /* Focus falls to whatever is now in front, as it does on a real desk. */
    d->focused = (d->count > 0) ? d->order[d->count - 1] : -1;
    /* Clear before calling out so a destroy callback that indirectly closes
     * the window cannot run twice. */
    destroy = w->destroy;
    ctx = w->ctx;
    w->destroy = NULL;
    if (destroy != NULL) {
        destroy(w, ctx);
    }
}

void
tiku_trk_win_set_handlers(tiku_trk_win_t *w, tiku_trk_win_event_fn event,
                          tiku_trk_win_close_fn close_requested,
                          tiku_trk_win_destroy_fn destroy)
{
    if (w != NULL) {
        w->event = event;
        w->close_requested = close_requested;
        w->destroy = destroy;
    }
}

int
tiku_trk_win_send(tiku_trk_win_t *w, const tiku_desk_event_t *event)
{
    if (w == NULL || !w->open || event == NULL || w->event == NULL) {
        return 0;
    }
    return w->event(w, event, w->ctx);
}

int
tiku_trk_desk_request_close(tiku_trk_desk_t *d, tiku_trk_win_t *w)
{
    if (d == NULL || w == NULL || !w->open) {
        return 0;
    }
    if (w->close_requested != NULL && !w->close_requested(w, w->ctx)) {
        return 0;
    }
    tiku_trk_desk_close(d, w);
    return 1;
}

/*---------------------------------------------------------------------------*/
/* geometry                                                                  */
/*---------------------------------------------------------------------------*/

tiku_desk_rect_t
tiku_trk_win_content(const tiku_trk_win_t *w)
{
    tiku_desk_rect_t c;

    c.x = w->frame.x + BORDER;
    c.y = w->frame.y + TAB_H + BORDER;
    c.w = w->frame.w - 2 * BORDER;
    c.h = w->frame.h - TAB_H - 2 * BORDER;
    if (c.w < 0) { c.w = 0; }
    if (c.h < 0) { c.h = 0; }
    return c;
}

void
tiku_trk_win_set_size_controls(tiku_trk_win_t *w, int resizable,
                               int zoomable)
{
    if (w != NULL) {
        w->resizable = resizable != 0;
        w->zoomable = zoomable != 0;
    }
}

/** @brief The tab rectangle: shorter than the window, at its left edge. */
static tiku_desk_rect_t
tab_rect(const tiku_trk_win_t *w)
{
    tiku_desk_rect_t t;
    int tw = 24 + (int)strlen(w->title) * 7 + 2 * BUTTON;

    if (tw > w->frame.w - 8) { tw = w->frame.w - 8; }
    if (tw < 60) { tw = 60; }
    t.x = w->frame.x + 4;
    t.y = w->frame.y;
    t.w = tw;
    t.h = TAB_H;
    return t;
}

static int
in_rect(tiku_desk_rect_t r, int x, int y)
{
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

tiku_trk_hit_t
tiku_trk_desk_hit(tiku_trk_desk_t *d, int x, int y, tiku_trk_win_t **out)
{
    int i;

    if (out != NULL) {
        *out = NULL;
    }
    if (d == NULL) {
        return TIKU_TRK_HIT_NONE;
    }
    /* Front to back: the topmost window under the point wins. */
    for (i = d->count - 1; i >= 0; i--) {
        tiku_trk_win_t *w = &d->wins[d->order[i]];
        tiku_desk_rect_t tab, content;

        if (!w->open || w->minimized) {
            continue;
        }
        tab = tab_rect(w);
        if (in_rect(tab, x, y)) {
            if (out != NULL) { *out = w; }
            if (x < tab.x + BUTTON + 6) {
                return TIKU_TRK_HIT_CLOSE;
            }
            if (w->zoomable && x > tab.x + tab.w - BUTTON - 6) {
                return TIKU_TRK_HIT_ZOOM;
            }
            return TIKU_TRK_HIT_TAB;
        }
        if (!in_rect(w->frame, x, y)) {
            continue;
        }
        if (out != NULL) { *out = w; }
        /* The bottom-right corner resizes; it is the only grip R5 has. */
        if (w->resizable && x > w->frame.x + w->frame.w - 16 &&
            y > w->frame.y + w->frame.h - 16) {
            return TIKU_TRK_HIT_RESIZE;
        }
        content = tiku_trk_win_content(w);
        return in_rect(content, x, y) ? TIKU_TRK_HIT_CONTENT
                                      : TIKU_TRK_HIT_BORDER;
    }
    return TIKU_TRK_HIT_NONE;
}

/*---------------------------------------------------------------------------*/
/* dragging and resizing                                                     */
/*---------------------------------------------------------------------------*/

void
tiku_trk_desk_begin_track(tiku_trk_desk_t *d, tiku_trk_win_t *w,
                          tiku_trk_hit_t what, int x, int y)
{
    if (d == NULL || w == NULL) {
        return;
    }
    d->track_what = what;
    d->track_win = (int)(w - d->wins);
    d->track_x = x;
    d->track_y = y;
}

int
tiku_trk_desk_tracking(const tiku_trk_desk_t *d)
{
    return (d != NULL && d->track_win >= 0);
}

int
tiku_trk_desk_track(tiku_trk_desk_t *d, int x, int y)
{
    tiku_trk_win_t *w;
    int dx, dy;

    if (d == NULL || d->track_win < 0) {
        return 0;
    }
    w = &d->wins[d->track_win];
    dx = x - d->track_x;
    dy = y - d->track_y;
    if (dx == 0 && dy == 0) {
        return 0;
    }
    d->track_x = x;
    d->track_y = y;

    if (d->track_what == TIKU_TRK_HIT_TAB) {
        w->frame.x += dx;
        w->frame.y += dy;
        /* Keep the tab reachable: a window dragged off the top edge could
         * never be dragged back. */
        if (w->frame.y < 0) { w->frame.y = 0; }
        if (w->frame.x + 40 > d->surf->w) { w->frame.x = d->surf->w - 40; }
        if (w->frame.x + w->frame.w < 40) { w->frame.x = 40 - w->frame.w; }
        if (w->frame.y + TAB_H > d->surf->h) {
            w->frame.y = d->surf->h - TAB_H;
        }
    } else if (d->track_what == TIKU_TRK_HIT_RESIZE) {
        w->frame.w += dx;
        w->frame.h += dy;
        if (w->frame.w < MIN_W) { w->frame.w = MIN_W; }
        if (w->frame.h < MIN_H) { w->frame.h = MIN_H; }
    } else {
        return 0;
    }
    return 1;
}

void
tiku_trk_desk_end_track(tiku_trk_desk_t *d)
{
    if (d != NULL) {
        d->track_win = -1;
        d->track_what = TIKU_TRK_HIT_NONE;
    }
}

/*---------------------------------------------------------------------------*/
/* drawing                                                                   */
/*---------------------------------------------------------------------------*/

int
tiku_trk_desk_zoom(tiku_trk_desk_t *d, tiku_trk_win_t *w, int fit_w,
                   int fit_h)
{
    tiku_desk_rect_t was, want;
    int max_w, max_h;

    if (d == NULL || w == NULL) {
        return 0;
    }
    was = w->frame;
    want = w->frame;
    {
        /* Measure the chrome from the window itself rather than rederiving
         * it: a frame built from a second copy of the border arithmetic is
         * one edit away from disagreeing with the one that draws it, and the
         * symptom is a last row cut off by exactly the difference. */
        tiku_desk_rect_t content = tiku_trk_win_content(w);

        want.w = fit_w + (was.w - content.w);
        want.h = fit_h + (was.h - content.h);
    }

    /* Clamped to the screen inset by five, with the tab's height taken off
     * the top: a window zoomed to fit must still be entirely reachable. */
    max_w = d->surf->w - 10 - want.x;
    max_h = d->surf->h - 10 - want.y - TIKU_TRK_WIN_TAB_H;
    if (d->reserved.w > 0 && want.x < d->reserved.x) {
        int room = d->reserved.x - want.x - 5;

        if (room < max_w) {
            max_w = room;
        }
    }
    if (want.w > max_w) { want.w = max_w; }
    if (want.h > max_h) { want.h = max_h; }
    if (want.w < 120)   { want.w = 120; }
    if (want.h < 80)    { want.h = 80; }

    if (want.w == was.w && want.h == was.h) {
        /* Already the right size: this is the second click, so go back --
         * but only if there is something valid to go back to. */
        if (!w->zoom_valid) {
            return 0;
        }
        w->frame = w->saved_zoom;
        w->zoom_valid = 0;
        return 1;
    }
    w->saved_zoom = was;
    w->zoom_valid = 1;
    w->frame = want;
    return 1;
}

/** @brief A tab button: the close square, or the zoom square. */
static void
draw_button(tiku_desk_surface_t *s, int x, int y, int zoom, int active)
{
    tiku_desk_rect_t b = { x, y, BUTTON, BUTTON };
    tiku_desk_rgb_t face = active ? TIKU_DESK_C_TAB : TIKU_DESK_C_TAB_IDLE;

    /* Drawn by the shared painter: a diagonal gradient inside one dark
     * outline, and for zoom, two overlapping squares.  Bevelling them
     * instead is what made a window tab read as a row of checkboxes. */
    tiku_desk_ui_tab_widget(s, b, zoom ? TIKU_DESK_TAB_ZOOM
                                       : TIKU_DESK_TAB_CLOSE, face, 0);
}

/** @brief One window: chrome, then the owner's content, clipped to it. */
static void
draw_window(tiku_trk_desk_t *d, tiku_trk_win_t *w, int active)
{
    tiku_desk_surface_t *s = d->surf;
    const tiku_desk_font_t *f = tiku_desk_font_bold();
    tiku_desk_rect_t tab = tab_rect(w);
    tiku_desk_rect_t body = { w->frame.x, w->frame.y + TAB_H - 1,
                              w->frame.w, w->frame.h - TAB_H + 1 };
    tiku_desk_rect_t content = tiku_trk_win_content(w);
    tiku_desk_rgb_t tabc = active ? TIKU_DESK_C_TAB : TIKU_DESK_C_TAB_IDLE;
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);

    tiku_desk_fill(s, body, TIKU_DESK_C_PANEL);
    tiku_desk_frame(s, body, dark);
    tiku_desk_bevel(s, tiku_desk_inset(body, 1),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_MAX),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));

    tiku_desk_fill(s, tab, tabc);
    tiku_desk_frame(s, tab, dark);
    tiku_desk_bevel(s, tiku_desk_inset(tab, 1),
                    tiku_desk_tint(tabc, TIKU_DESK_LIGHTEN_1),
                    tiku_desk_tint(tabc, TIKU_DESK_DARKEN_2));
    /* The tab's foot IS the body's head; erase the line between them. */
    tiku_desk_hline(s, tab.x + 1, tab.y + tab.h - 1, tab.w - 2, tabc);

    draw_button(s, tab.x + 4, tab.y + (TAB_H - BUTTON) / 2, 0, active);
    if (w->zoomable) {
        draw_button(s, tab.x + tab.w - BUTTON - 4,
                    tab.y + (TAB_H - BUTTON) / 2, 1, active);
    }
    {
        tiku_desk_rect_t label = { tab.x + BUTTON + 8, tab.y,
                                   tab.w - BUTTON - 16 -
                                       (w->zoomable ? BUTTON + 8 : 0),
                                   TAB_H };
        tiku_desk_clip_set(s, label);
        (void)tiku_desk_text_centered(s, f, label, w->title,
                                      TIKU_DESK_C_TEXT);
        tiku_desk_clip_reset(s);
    }

    if (w->draw != NULL && content.w > 0 && content.h > 0) {
        tiku_desk_clip_set(s, content);
        w->draw(w, s, content, w->ctx);
        tiku_desk_clip_reset(s);
    }
    /* The resize grip, bottom-right, drawn last so it sits over the frame. */
    if (w->resizable) {
        int gx = w->frame.x + w->frame.w - 14;
        int gy = w->frame.y + w->frame.h - 14;
        int i;
        for (i = 0; i < 3; i++) {
            tiku_desk_hline(s, gx + i * 3, gy + 10, 10 - i * 3,
                            tiku_desk_tint(TIKU_DESK_C_PANEL,
                                           TIKU_DESK_DARKEN_2));
            tiku_desk_hline(s, gx + i * 3, gy + 11, 10 - i * 3,
                            tiku_desk_tint(TIKU_DESK_C_PANEL,
                                           TIKU_DESK_LIGHTEN_MAX));
        }
    }
}

void
tiku_trk_desk_set_backdrop(tiku_trk_desk_t *d,
                           void (*fn)(tiku_desk_surface_t *s, void *ctx),
                           void *ctx)
{
    if (d != NULL) {
        d->backdrop = fn;
        d->backdrop_ctx = ctx;
    }
}

void
tiku_trk_desk_draw(tiku_trk_desk_t *d)
{
    tiku_desk_rect_t all;
    int i;

    if (d == NULL) {
        return;
    }
    all = (tiku_desk_rect_t){ 0, 0, d->surf->w, d->surf->h };
    tiku_desk_fill(d->surf, all, TIKU_DESK_C_BACKDROP);
    if (d->backdrop != NULL) {
        d->backdrop(d->surf, d->backdrop_ctx);
    }
    /* Back to front, so a window in front covers the one behind it. */
    for (i = 0; i < d->count; i++) {
        tiku_trk_win_t *w = &d->wins[d->order[i]];
        if (w->open && !w->minimized) {
            draw_window(d, w, d->order[i] == d->focused);
        }
    }
}
