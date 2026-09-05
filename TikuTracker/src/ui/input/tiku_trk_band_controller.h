/*
 * TikuTracker -- the file manager for TikuOS.
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
    /* Non-zero when a SCRIPT drives the shell: the hold delay is counted
     * in loop pulses instead of wall time, because a scripted wait is a
     * number of turns and the wall clock under load crosses a 600 ms line
     * whenever it pleases.  A real user's shell leaves this 0. */
    int script_delay_pulses;
    int held_pulses;
    int row_h;
    int *dirty;
} tiku_trk_band_controller_t;

/** @brief Count the hold delay in pulses rather than wall time. */
void tiku_trk_band_controller_set_script_delay(
    tiku_trk_band_controller_t *controller, int pulses);

/** @brief A controller with the row height and the dirty flag it raises. */
void tiku_trk_band_controller_init(tiku_trk_band_controller_t *controller,
                                   int row_h, int *dirty);
/** @brief A press at @p x, @p y that may become a band in @p folder. */
void tiku_trk_band_controller_arm(tiku_trk_band_controller_t *controller,
    tiku_trk_folder_window_t *folder, int x, int y, unsigned modifiers);
/**
 * @brief The pointer moved: past the slop the press becomes a band, and a
 *        band selects what it covers.  @return 1 while a band is drawn.
 */
int tiku_trk_band_controller_motion(tiku_trk_band_controller_t *controller,
                                    int x, int y, unsigned modifiers);
/** @brief The band ends.  @return 1 when there was one. */
int tiku_trk_band_controller_finish(tiku_trk_band_controller_t *controller);
/**
 * @brief A tick while the band is held at an edge: the view scrolls and
 *        the band follows.  @return 1 when something moved.
 */
int tiku_trk_band_controller_pulse(tiku_trk_band_controller_t *controller,
                                   int64_t now);

#endif /* TIKU_TRK_BAND_CONTROLLER_H_ */
