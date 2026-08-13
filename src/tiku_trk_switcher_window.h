/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_switcher_window.h - application switcher window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SWITCHER_WINDOW_H_
#define TIKU_TRK_SWITCHER_WINDOW_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_desk_window.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_switcher.h"

typedef struct {
    tiku_trk_switcher_t    model;
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t    *window;
    tiku_trk_deskbar_t    *deskbar;
    int                   *dirty;
    char                  *status;
    size_t                 status_size;
    int                    screen_w;
    int                    screen_h;
} tiku_trk_switcher_window_t;

void tiku_trk_switcher_window_init(tiku_trk_switcher_window_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    int screen_w, int screen_h, int *dirty, char *status,
    size_t status_size);

int tiku_trk_switcher_window_shown(
    const tiku_trk_switcher_window_t *controller);
void tiku_trk_switcher_window_open(tiku_trk_switcher_window_t *controller,
                                   int backwards);
tiku_trk_sw_act_t tiku_trk_switcher_window_key(
    tiku_trk_switcher_window_t *controller, const char *key, int shift);
tiku_trk_sw_act_t tiku_trk_switcher_window_release(
    tiku_trk_switcher_window_t *controller, int64_t now);
void tiku_trk_switcher_window_close(tiku_trk_switcher_window_t *controller,
                                    int commit);
const char *tiku_trk_switcher_window_selected(
    const tiku_trk_switcher_window_t *controller);

#endif /* TIKU_TRK_SWITCHER_WINDOW_H_ */
