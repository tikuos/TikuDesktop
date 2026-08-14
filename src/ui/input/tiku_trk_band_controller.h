/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_band_controller.h - rubber-band selection interaction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_BAND_CONTROLLER_H_
#define TIKU_TRK_BAND_CONTROLLER_H_

#include <stdint.h>

#include "tiku_trk_folder_window.h"

/** @brief Retained state for an armed or active selection sweep. */
typedef struct {
    tiku_trk_folder_window_t *folder;
    tiku_trk_folder_window_t *armed_folder;
    int x0;
    int y0;
    int x1;
    int y1;
    unsigned modifiers;
    int64_t scroll_at;
    int row_h;
    int *dirty;
} tiku_trk_band_controller_t;

void tiku_trk_band_controller_init(tiku_trk_band_controller_t *controller,
                                   int row_h, int *dirty);
void tiku_trk_band_controller_arm(tiku_trk_band_controller_t *controller,
    tiku_trk_folder_window_t *folder, int x, int y, unsigned modifiers);
int tiku_trk_band_controller_motion(tiku_trk_band_controller_t *controller,
                                    int x, int y, unsigned modifiers);
int tiku_trk_band_controller_finish(tiku_trk_band_controller_t *controller);
int tiku_trk_band_controller_pulse(tiku_trk_band_controller_t *controller,
                                   int64_t now);

#endif /* TIKU_TRK_BAND_CONTROLLER_H_ */
