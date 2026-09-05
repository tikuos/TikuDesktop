/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fonts_window.h - Fonts preferences controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FONTS_WINDOW_H_
#define TIKU_TRK_FONTS_WINDOW_H_

#include "tiku_window.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_fontprefs.h"

#define TIKU_TRK_FONTS_W 372

typedef struct {
    tiku_trk_fontprefs_t       model;
    tiku_trk_fontprefs_offer_t offer;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    tiku_trk_deskbar_t    *deskbar;
    tiku_store_t      *store;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
} tiku_trk_fonts_window_t;

/**
 * @brief Bind the controller to its workspace, the bar the face is applied
 *        through, and the store.  Opens no window.
 */
void tiku_trk_fonts_window_init(tiku_trk_fonts_window_t *controller,
                                tiku_workspace_t *workspace,
                                tiku_trk_deskbar_t *deskbar,
                                tiku_store_t *store, int screen_w,
                                int screen_h, int *dirty);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_fonts_window_destroy(tiku_trk_fonts_window_t *controller);
/**
 * @brief Open the window, reloading the settings and re-reading the faces
 *        on offer; an open window is activated instead.
 */
void tiku_trk_fonts_window_open(tiku_trk_fonts_window_t *controller);
/** @brief Close the window and ask for a redraw. */
void tiku_trk_fonts_window_close(tiku_trk_fonts_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_fonts_window_handle(
    tiku_trk_fonts_window_t *controller);

#endif /* TIKU_TRK_FONTS_WINDOW_H_ */
