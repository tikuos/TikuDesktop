/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find_window.h - Find window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FIND_WINDOW_H_
#define TIKU_TRK_FIND_WINDOW_H_

#include <stddef.h>

#include "tiku_desk_window.h"
#include "tiku_trk_find.h"
#include "tiku_trk_state.h"

/** @brief Execute the controller's query without exposing shell state. */
typedef int (*tiku_trk_find_window_search_fn)(void *context,
                                               tiku_trk_find_t *model);

typedef struct {
    tiku_trk_find_t       model;
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t    *window;
    tiku_trk_store_t      *store;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    char                  *status;
    size_t                 status_size;
    tiku_trk_find_window_search_fn search;
    /* Raised when Save has nowhere attached to save INTO: the shell
     * answers with the save panel (AW-052). */
    void                 (*save_as)(void *context);
    void                  *save_as_context;
    /* 'Select folders...' chose the panel; the shell raises it (Q-019). */
    void                 (*select_folder)(void *context);
    void                  *search_context;
} tiku_trk_find_window_t;

void tiku_trk_find_window_init(tiku_trk_find_window_t *controller,
    tiku_desk_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, char *status, size_t status_size,
    tiku_trk_find_window_search_fn search, void *search_context);
void tiku_trk_find_window_set_store(tiku_trk_find_window_t *controller,
                                    tiku_trk_store_t *store);
void tiku_trk_find_window_set_types(tiku_trk_find_window_t *controller,
                                    const tiku_trk_types_t *types);

tiku_trk_find_t *tiku_trk_find_window_model(
    tiku_trk_find_window_t *controller);
tiku_desk_window_t *tiku_trk_find_window_handle(
    tiku_trk_find_window_t *controller);

void tiku_trk_find_window_open(tiku_trk_find_window_t *controller);
void tiku_trk_find_window_close(tiku_trk_find_window_t *controller);

/** @return Nonzero when the focused Find window consumed the key. */
int tiku_trk_find_window_key(tiku_trk_find_window_t *controller,
    unsigned key, char character);

/** @return Nonzero while a popup extends outside the Find window. */
/** @brief Whether (x, y) is on the draggable query icon (Q-034). */
int tiku_trk_find_window_icon_at(tiku_trk_find_window_t *controller,
                                 int x, int y);

int tiku_trk_find_window_popup_open(
    const tiku_trk_find_window_t *controller);

#endif /* TIKU_TRK_FIND_WINDOW_H_ */
