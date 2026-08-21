/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fonts_window.h - Fonts preferences controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FONTS_WINDOW_H_
#define TIKU_TRK_FONTS_WINDOW_H_

#include "tiku_desk_window.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_fontprefs.h"

#define TIKU_TRK_FONTS_W 372

typedef struct {
    tiku_trk_fontprefs_t       model;
    tiku_trk_fontprefs_offer_t offer;
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t    *window;
    tiku_trk_deskbar_t    *deskbar;
    tiku_trk_store_t      *store;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
} tiku_trk_fonts_window_t;

void tiku_trk_fonts_window_init(tiku_trk_fonts_window_t *controller,
                                tiku_desk_workspace_t *workspace,
                                tiku_trk_deskbar_t *deskbar,
                                tiku_trk_store_t *store, int screen_w,
                                int screen_h, int *dirty);
void tiku_trk_fonts_window_destroy(tiku_trk_fonts_window_t *controller);
void tiku_trk_fonts_window_open(tiku_trk_fonts_window_t *controller);
void tiku_trk_fonts_window_close(tiku_trk_fonts_window_t *controller);
tiku_desk_window_t *tiku_trk_fonts_window_handle(
    tiku_trk_fonts_window_t *controller);

#endif /* TIKU_TRK_FONTS_WINDOW_H_ */
