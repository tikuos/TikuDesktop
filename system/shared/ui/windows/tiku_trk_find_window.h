/*
 * TikuTracker -- the file manager for TikuOS.
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

#include "tiku_window.h"
#include "tiku_trk_find.h"
#include "tiku_state.h"

/** @brief Execute the controller's query without exposing shell state. */
typedef int (*tiku_trk_find_window_search_fn)(void *context,
                                               tiku_trk_find_t *model);

typedef struct {
    tiku_trk_find_t       model;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    tiku_store_t      *store;
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

/**
 * @brief Bind the controller to its workspace, the shell's dirty flag and
 *        status line, and the search it runs.  Opens no window.
 */
void tiku_trk_find_window_init(tiku_trk_find_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, char *status, size_t status_size,
    tiku_trk_find_window_search_fn search, void *search_context);
/** @brief The store saved queries go to, for the form and the window. */
void tiku_trk_find_window_set_store(tiku_trk_find_window_t *controller,
                                    tiku_store_t *store);
/** @brief The type registry the form offers its type menu from. */
void tiku_trk_find_window_set_types(tiku_trk_find_window_t *controller,
                                    const tiku_trk_types_t *types);

/** @brief The form behind the window. */
tiku_trk_find_t *tiku_trk_find_window_model(
    tiku_trk_find_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_find_window_handle(
    tiku_trk_find_window_t *controller);

/**
 * @brief Open the window centred on the screen, sized to the form; an open
 *        window is brought up to date and activated instead.
 */
void tiku_trk_find_window_open(tiku_trk_find_window_t *controller);
/** @brief Close the window, if open, and ask for a redraw. */
void tiku_trk_find_window_close(tiku_trk_find_window_t *controller);

/** @return Nonzero when the focused Find window consumed the key. */
int tiku_trk_find_window_key(tiku_trk_find_window_t *controller,
    unsigned key, char character);

/** @brief Whether (x, y) is on the draggable query icon (Q-034). */
int tiku_trk_find_window_icon_at(tiku_trk_find_window_t *controller,
                                 int x, int y);

/** @return Nonzero while the open window has a popup up. */
int tiku_trk_find_window_popup_open(
    const tiku_trk_find_window_t *controller);

/** @brief Insert committed text only; 0 not focused, 1 handled, -1 rejected. */
int tiku_trk_find_window_text(tiku_trk_find_window_t *controller,
    const char *text);

#endif /* TIKU_TRK_FIND_WINDOW_H_ */
