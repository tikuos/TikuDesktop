/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_open_with_window.h - the Open With window (AW-045, AW-047).
 *
 * Single-selection, list only, no menus, Escape cancels: a window that
 * exists to answer ONE question -- which application -- and get out of the
 * way.  Every registered application is listed with its relation to the
 * selection said in words (MA-081); the buttons live-update from the
 * selection, and an application that does not claim the type asks first
 * (AW-046).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_OPEN_WITH_WINDOW_H_
#define TIKU_TRK_OPEN_WITH_WINDOW_H_

#include "tiku_desk_window.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_model.h"

#define TIKU_TRK_OW_PATHS 16

typedef void (*tiku_trk_ow_open_fn)(void *context,
    const tiku_trk_app_t *app, const char *const *paths, int count);

typedef struct {
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t    *window;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    tiku_trk_ow_open_fn    open;
    void                  *open_context;
    tiku_trk_alert_t      *alert;       /* the shell's, for the confirm  */
    struct tiku_trk_store *store;       /* where preferred lives         */
    /* The question being asked. */
    tiku_trk_cand_t        cand[TIKU_TRK_APP_MAX];
    int                    count;
    char                   paths[TIKU_TRK_OW_PATHS][TIKU_TRK_PATH_MAX];
    const char            *path_ptr[TIKU_TRK_OW_PATHS];
    int                    path_count;
    char                   types[TIKU_TRK_APP_TYPES_MAX][TIKU_TRK_TYPE_MAX];
    int                    type_count;
    int                    generic_only; /* every document is untyped     */
    int                    sel;          /* single selection, or -1       */
    int                    scroll;
} tiku_trk_open_with_window_t;

void tiku_trk_open_with_window_init(tiku_trk_open_with_window_t *controller,
    tiku_desk_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_ow_open_fn open, void *open_context,
    tiku_trk_alert_t *alert, struct tiku_trk_store *store);

/** @brief Ask which application, for @p paths of @p types. */
void tiku_trk_open_with_window_open(tiku_trk_open_with_window_t *controller,
    const tiku_trk_apps_t *apps, const char *const *types, int ntypes,
    const char *const *paths, int npaths);

tiku_desk_window_t *tiku_trk_open_with_window_handle(
    tiku_trk_open_with_window_t *controller);

/** @return Nonzero when the focused window consumed the key. */
int tiku_trk_open_with_window_key(tiku_trk_open_with_window_t *controller,
                                  int key, unsigned modifiers);

/** @brief The confirm alert said Open anyway (AW-046). */
void tiku_trk_open_with_window_confirmed(
    tiku_trk_open_with_window_t *controller);

#endif /* TIKU_TRK_OPEN_WITH_WINDOW_H_ */
