/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_open_with_window.h - the Open With window (AW-045, AW-047).
 *
 * Single-selection, list only, no menus, Escape cancels: a window that
 * exists to answer ONE question -- which application -- and get out of the
 * way. Every document-capable application is listed with its relation to the
 * selection said in words (MA-081); the buttons live-update from the
 * selection, and an application that does not claim the type asks first
 * (AW-046).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_OPEN_WITH_WINDOW_H_
#define TIKU_TRK_OPEN_WITH_WINDOW_H_

#include "tiku_window.h"
#include "tiku_list.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_launch.h"
#include "tiku_model.h"

#define TIKU_TRK_OW_PATHS 16

typedef void (*tiku_trk_ow_open_fn)(void *context,
    const tiku_trk_app_t *app, const char *const *paths, int count);

typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    tiku_trk_ow_open_fn    open;
    void                  *open_context;
    tiku_alert_t      *alert;       /* the shell's, for the confirm  */
    struct tiku_store *store;       /* where preferred lives         */
    /* The question being asked. */
    tiku_trk_cand_t        cand[TIKU_TRK_APP_MAX];
    int                    count;
    char                   paths[TIKU_TRK_OW_PATHS][TIKU_PATH_MAX];
    const char            *path_ptr[TIKU_TRK_OW_PATHS];
    int                    path_count;
    char                   types[TIKU_TRK_APP_TYPES_MAX][TIKU_TYPE_MAX];
    int                    type_count;
    int                    generic_only; /* every document is untyped     */
    /* The rows, the one that is picked and the one shown first: the
     * kit's list rather than an index and a scroll of its own, so the
     * anchor rules and the walk are the same ones every other list on
     * this desktop keeps. */
    tiku_list_t       list;
} tiku_trk_open_with_window_t;

/**
 * @brief Bind the controller to its workspace, the launcher it opens
 *        through, the alert and the store.  Opens no window.
 */
void tiku_trk_open_with_window_init(tiku_trk_open_with_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_ow_open_fn open, void *open_context,
    tiku_alert_t *alert, struct tiku_store *store);

/** @brief Ask which application, for @p paths of @p types. */
void tiku_trk_open_with_window_open(tiku_trk_open_with_window_t *controller,
    const tiku_trk_apps_t *apps, const char *const *types, int ntypes,
    const char *const *paths, int npaths);

/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_open_with_window_handle(
    tiku_trk_open_with_window_t *controller);

/** @return Nonzero when the focused window consumed the key. */
int tiku_trk_open_with_window_key(tiku_trk_open_with_window_t *controller,
                                  int key, unsigned modifiers);

/** @brief The confirm alert said Open anyway (AW-046). */
void tiku_trk_open_with_window_confirmed(
    tiku_trk_open_with_window_t *controller);

#endif /* TIKU_TRK_OPEN_WITH_WINDOW_H_ */
