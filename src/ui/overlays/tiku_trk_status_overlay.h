/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_status_overlay.h - modal alert and job-status presentation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_STATUS_OVERLAY_H_
#define TIKU_TRK_STATUS_OVERLAY_H_

#include <stdint.h>

#include "tiku_gfx.h"
#include "tiku_event.h"
#include "tiku_trk_alert.h"

typedef void (*tiku_trk_status_alert_fn)(void *context,
                                         const tiku_trk_alert_t *alert);
typedef void (*tiku_trk_status_stop_fn)(void *context);

typedef struct {
    tiku_trk_alert_t          alert;
    tiku_trk_progress_win_t   progress;
    tiku_surface_t      *surface;
    int                       screen_w;
    int                       screen_h;
    int                      *dirty;
    int64_t                   progress_opened;
    tiku_trk_status_alert_fn  answer;
    tiku_trk_status_stop_fn   stop;
    void                     *callback_context;
} tiku_trk_status_overlay_t;

void tiku_trk_status_overlay_init(tiku_trk_status_overlay_t *controller,
    tiku_surface_t *surface, int screen_w, int screen_h, int *dirty,
    tiku_trk_status_alert_fn answer, tiku_trk_status_stop_fn stop,
    void *callback_context);

tiku_trk_alert_t *tiku_trk_status_overlay_alert(
    tiku_trk_status_overlay_t *controller);
tiku_trk_progress_win_t *tiku_trk_status_overlay_progress(
    tiku_trk_status_overlay_t *controller);

void tiku_trk_status_overlay_progress_start(
    tiku_trk_status_overlay_t *controller, int64_t now);
void tiku_trk_status_overlay_progress_finish(
    tiku_trk_status_overlay_t *controller);
int64_t tiku_trk_status_overlay_progress_opened(
    const tiku_trk_status_overlay_t *controller);

void tiku_trk_status_overlay_draw(tiku_trk_status_overlay_t *controller);

/** @return Nonzero when a modal alert or progress control consumed it. */
int tiku_trk_status_overlay_pointer(tiku_trk_status_overlay_t *controller,
                                    int x, int y);

/** @return Nonzero while a modal alert owns the keyboard. */
int tiku_trk_status_overlay_key(tiku_trk_status_overlay_t *controller,
                                unsigned key);

#endif /* TIKU_TRK_STATUS_OVERLAY_H_ */
