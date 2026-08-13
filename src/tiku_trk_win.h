/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_win.h - the window system.
 *
 * One host window is the desktop; everything inside it -- folder windows, the
 * Deskbar, menus -- is ours, composited back to front with the R5 tab chrome.
 * The model is R5's own: no per-window backing store, windows are asked to
 * redraw what is exposed, which is why this fits a small machine as readily
 * as a large one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_WIN_H_
#define TIKU_TRK_WIN_H_

#include "tiku_desk_event.h"
#include "tiku_trk_gfx_dep.h"

#define TIKU_TRK_WIN_MAX     16
#define TIKU_TRK_WIN_TAB_H   21
#define TIKU_TRK_WIN_BORDER   5
#define TIKU_TRK_WIN_TITLE_MAX 160

/** @brief What a point on a window is over. */
typedef enum {
    TIKU_TRK_HIT_NONE = 0,
    TIKU_TRK_HIT_CONTENT,
    TIKU_TRK_HIT_TAB,
    TIKU_TRK_HIT_CLOSE,
    TIKU_TRK_HIT_ZOOM,
    TIKU_TRK_HIT_RESIZE,
    TIKU_TRK_HIT_BORDER,
} tiku_trk_hit_t;

typedef struct tiku_trk_win tiku_trk_win_t;
typedef struct tiku_trk_desk tiku_trk_desk_t;

/** @brief Draw a window's contents into @p content.  Supplied by the owner. */
typedef void (*tiku_trk_win_draw_fn)(tiku_trk_win_t *w,
                                     tiku_desk_surface_t *s,
                                     tiku_desk_rect_t content, void *ctx);

/** Application-owned window behavior.  These callbacks are the migration
 * seam from the central Tracker event switch to self-contained windows. */
typedef int (*tiku_trk_win_event_fn)(tiku_trk_win_t *w,
                                     const tiku_desk_event_t *event,
                                     void *ctx);
typedef int (*tiku_trk_win_close_fn)(tiku_trk_win_t *w, void *ctx);
typedef void (*tiku_trk_win_destroy_fn)(tiku_trk_win_t *w, void *ctx);

/** @brief A window: a rectangle, a tab, and someone else's content. */
struct tiku_trk_win {
    char                 title[TIKU_TRK_WIN_TITLE_MAX];
    tiku_desk_rect_t     frame;        /* including tab and border         */
    int                  z;            /* higher is nearer the front       */
    int                  open;
    int                  minimized;
    int                  resizable;
    int                  zoomable;
    tiku_trk_win_draw_fn draw;
    tiku_trk_win_event_fn event;
    tiku_trk_win_close_fn close_requested;
    tiku_trk_win_destroy_fn destroy;
    void                *ctx;          /* the owner's data (a pose view)   */
    void                *tag;          /* owner's identity, for lookups    */
    /* Zoom is a TOGGLE, so the frame it left has to be remembered. */
    tiku_desk_rect_t     saved_zoom;
    int                  zoom_valid;
};

/** @brief Create the desktop over a surface the caller owns. */
tiku_trk_desk_t *tiku_trk_desk_new(tiku_desk_surface_t *surface);

void tiku_trk_desk_free(tiku_trk_desk_t *d);

/**
 * @brief Open a window.
 *
 * @param tag  Owner identity; opening the same tag twice brings the existing
 *             window forward instead of making a second one, which is what
 *             makes a spatial file manager feel spatial.
 * @return The window, or NULL when the desktop is full.
 */
tiku_trk_win_t *tiku_trk_desk_open(tiku_trk_desk_t *d, const char *title,
                                   tiku_desk_rect_t frame,
                                   tiku_trk_win_draw_fn draw, void *ctx,
                                   void *tag);

/** @brief The window owning @p tag, or NULL. */
tiku_trk_win_t *tiku_trk_desk_find(tiku_trk_desk_t *d, void *tag);

void tiku_trk_desk_close(tiku_trk_desk_t *d, tiku_trk_win_t *w);

/** Attach the application behavior owned by this window. */
void tiku_trk_win_set_handlers(tiku_trk_win_t *w,
                               tiku_trk_win_event_fn event,
                               tiku_trk_win_close_fn close_requested,
                               tiku_trk_win_destroy_fn destroy);

/** Deliver one normalized event directly to a window. */
int tiku_trk_win_send(tiku_trk_win_t *w, const tiku_desk_event_t *event);

/** Ask permission, then close and destroy the window if allowed. */
int tiku_trk_desk_request_close(tiku_trk_desk_t *d, tiku_trk_win_t *w);

/** @brief Bring @p w to the front and give it focus. */
void tiku_trk_desk_activate(tiku_trk_desk_t *d, tiku_trk_win_t *w);

tiku_trk_win_t *tiku_trk_desk_focused(tiku_trk_desk_t *d);

int tiku_trk_desk_count(const tiku_trk_desk_t *d);

/** @brief Window at index @p i in z-order, back to front. */
tiku_trk_win_t *tiku_trk_desk_at(tiku_trk_desk_t *d, int i);

/**
 * @brief Which window and part is under a point.
 *
 * @param out_win  Receives the window, or NULL for the backdrop.
 * @return What the point is over.
 */
tiku_trk_hit_t tiku_trk_desk_hit(tiku_trk_desk_t *d, int x, int y,
                                 tiku_trk_win_t **out_win);

/** @brief The content rectangle inside a window's chrome. */
tiku_desk_rect_t tiku_trk_win_content(const tiku_trk_win_t *w);

/** @brief Set whether the window exposes its resize grip and zoom button. */
void tiku_trk_win_set_size_controls(tiku_trk_win_t *w, int resizable,
                                    int zoomable);

/** @brief Begin dragging or resizing at a point. */
void tiku_trk_desk_begin_track(tiku_trk_desk_t *d, tiku_trk_win_t *w,
                               tiku_trk_hit_t what, int x, int y);

/** @brief Continue a drag or resize.  @return 1 when something moved. */
int tiku_trk_desk_track(tiku_trk_desk_t *d, int x, int y);

void tiku_trk_desk_end_track(tiku_trk_desk_t *d);

int tiku_trk_desk_tracking(const tiku_trk_desk_t *d);

/** @brief Paint the backdrop and every window, back to front. */
void tiku_trk_desk_draw(tiku_trk_desk_t *d);

/**
 * @brief What to paint on the backdrop, between the fill and the windows.
 *
 * The backdrop is a pose area, not a colour: the Desktop's own icons are
 * BEHIND every window and in front of the fill, and that order is decided
 * here because this is what owns the z-order (AW-018).
 */
void tiku_trk_desk_set_backdrop(tiku_trk_desk_t *d,
                                void (*fn)(tiku_desk_surface_t *s, void *ctx),
                                void *ctx);

/**
 * @brief Zoom: fit the window to its contents, or restore what it left.
 *
 * The first click saves the current frame and resizes to fit; a second click
 * puts it back -- but only when the resize actually changed the frame.  A
 * window already the right size for its contents does not silently swallow
 * the click and then restore something stale on the next one.
 *
 * @param fit_w Content width the owner wants, excluding chrome.
 * @param fit_h Content height the owner wants, excluding chrome.
 * @return 1 when the frame moved.
 */
int tiku_trk_desk_zoom(tiku_trk_desk_t *d, tiku_trk_win_t *w, int fit_w,
                       int fit_h);

/** @brief Reserve an edge strip the Deskbar owns; windows avoid it. */
void tiku_trk_desk_set_reserved(tiku_trk_desk_t *d, tiku_desk_rect_t r);

#endif /* TIKU_TRK_WIN_H_ */
