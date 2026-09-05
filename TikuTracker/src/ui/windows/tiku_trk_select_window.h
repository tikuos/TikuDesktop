/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_select_window.h - Select by pattern window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SELECT_WINDOW_H_
#define TIKU_TRK_SELECT_WINDOW_H_

#include "tiku_window.h"
#include "tiku_trk_select.h"

typedef tiku_window_t *(*tiku_trk_select_apply_fn)(
    void *context, void *target, const tiku_trk_select_t *selection);

typedef struct {
    tiku_trk_select_t       model;
    tiku_workspace_t  *workspace;
    tiku_window_t     *window;
    void                   *target;
    int                     field;
    int                    *dirty;
    int                     screen_w;
    int                     screen_h;
    tiku_trk_select_apply_fn apply;
    void                   *apply_context;
} tiku_trk_select_window_t;

/**
 * @brief Bind the controller to its workspace and the shell's apply
 *        callback.  Opens no window.
 */
void tiku_trk_select_window_init(tiku_trk_select_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_select_apply_fn apply, void *apply_context);
/** @brief The form behind the window. */
tiku_trk_select_t *tiku_trk_select_window_model(
    tiku_trk_select_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_select_window_handle(
    tiku_trk_select_window_t *controller);
/**
 * @brief Open the window at @p x, @p y for @p target, the folder the
 *        selection is made in.
 */
void tiku_trk_select_window_open(tiku_trk_select_window_t *controller,
                                 void *target, int x, int y);

/** @return Nonzero when the focused Select window consumed the key. */
int tiku_trk_select_window_key(tiku_trk_select_window_t *controller,
    int key, unsigned modifiers, char character);

#endif /* TIKU_TRK_SELECT_WINDOW_H_ */
