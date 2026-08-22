/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag_target.h - drag hover, scrolling, and spring-open state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DRAG_TARGET_H_
#define TIKU_TRK_DRAG_TARGET_H_

#include <stdint.h>

#include "tiku_trk_deskbar.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_springmenu.h"

typedef void (*tiku_trk_drag_open_fn)(void *context, const char *path);

/** @brief Dependencies and state for resolving the target under a drag. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_trk_deskbar_t *deskbar;
    tiku_trk_desktop_view_t *desktop_view;
    tiku_trk_drag_t *drag;
    tiku_trk_folder_window_t *folders;
    int *folder_count;
    int row_h;
    int *dirty;
    tiku_trk_drag_open_fn open;
    void *open_context;
    tiku_trk_folder_window_t *folder;
    int row;
    int64_t hover_at;
    int64_t scroll_at;
    int sprung;
    /* Non-zero when a script drives the shell: the spring hold is counted
     * in pulses, not wall time, for the same reason the band's is. */
    int spring_script_pulses;
    int spring_held;
    tiku_trk_springmenu_t spring;
} tiku_trk_drag_target_t;

/** @brief Count the spring hold in pulses rather than wall time. */
void tiku_trk_drag_target_set_script_delay(
    tiku_trk_drag_target_t *controller, int pulses);

void tiku_trk_drag_target_init(tiku_trk_drag_target_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_trk_desktop_view_t *desktop_view, tiku_trk_drag_t *drag,
    tiku_trk_folder_window_t *folders, int *folder_count, int row_h,
    int *dirty, tiku_trk_drag_open_fn open, void *open_context);
int tiku_trk_drag_target_motion(tiku_trk_drag_target_t *controller,
                                int x, int y, int64_t now);
int tiku_trk_drag_target_pulse(tiku_trk_drag_target_t *controller,
                               int64_t now);
void tiku_trk_drag_target_reset(tiku_trk_drag_target_t *controller);

#endif /* TIKU_TRK_DRAG_TARGET_H_ */
