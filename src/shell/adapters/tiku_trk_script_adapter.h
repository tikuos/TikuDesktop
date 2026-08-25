/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_adapter.h - script observations of desktop presentation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SCRIPT_ADAPTER_H_
#define TIKU_TRK_SCRIPT_ADAPTER_H_

#include "tiku_window.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_find_window.h"
#include "tiku_trk_folder_edit.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_panel_window.h"
#include "tiku_menu.h"
#include "tiku_trk_script.h"

/** @brief Read-only presentation dependencies exposed to script callbacks. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_surface_t *surface;
    tiku_trk_folder_lifecycle_t *folders;
    tiku_trk_desktop_view_t *desktop_view;
    tiku_trk_deskbar_t *deskbar;
    tiku_alert_t *alert;
    tiku_trk_find_window_t *find_window;
    tiku_trk_panel_window_t *panel_window;
    tiku_trk_folder_edit_t *folder_edit;
    tiku_menu_t **popup;
    tiku_menu_t **popup_sub;
    tiku_rect_t *popup_frame;
    tiku_rect_t *popup_sub_frame;
    char *status;
    char *count_text;
    char menu_labels[512];
    char shot_path[TIKU_PATH_MAX];
    int row_h;
    /* The shell's own full redraw, for the check that what the frames
     * painted is what a full redraw would have drawn. */
    long               (*redraw_diff)(void *ctx);
    void                *redraw_ctx;
    /* The shell's named-fact answerer -- the SAME function its conductor
     * channel answers QUERY with, so both surfaces stay one surface. */
    int                (*say)(void *ctx, const char *noun, char *out,
                              size_t max);
    void                *say_ctx;
} tiku_trk_script_adapter_t;

void tiku_trk_script_adapter_init(tiku_trk_script_adapter_t *adapter,
    tiku_workspace_t *workspace, tiku_surface_t *surface,
    tiku_trk_folder_lifecycle_t *folders,
    tiku_trk_desktop_view_t *desktop_view, tiku_trk_deskbar_t *deskbar,
    tiku_alert_t *alert, tiku_trk_find_window_t *find_window,
    tiku_trk_panel_window_t *panel_window,
    tiku_trk_folder_edit_t *folder_edit, tiku_menu_t **popup,
    tiku_menu_t **popup_sub, tiku_rect_t *popup_frame,
    tiku_rect_t *popup_sub_frame, char *status, char *count_text,
    int row_h);
int tiku_trk_script_adapter_tab_point(void *context, const char *title,
                                      int *x, int *y);

int tiku_trk_script_adapter_row_point(void *context, int row,
                                      int *x, int *y);
unsigned tiku_trk_script_adapter_pixel_at(void *context, int x, int y);
int tiku_trk_script_adapter_pose_screen(void *context, const char *name,
                                        int *x, int *y);
int tiku_trk_script_adapter_pose_at(void *context, const char *name,
                                    int *x, int *y);

typedef enum {
    TIKU_TRK_SCRIPT_ADAPTER_STOP = -1,
    TIKU_TRK_SCRIPT_ADAPTER_IDLE = 0,
    TIKU_TRK_SCRIPT_ADAPTER_EVENT = 1,
    TIKU_TRK_SCRIPT_ADAPTER_SHOT = 2
} tiku_trk_script_adapter_result_t;

tiku_trk_script_adapter_result_t tiku_trk_script_adapter_next(
    tiku_trk_script_adapter_t *adapter, tiku_trk_script_t *script,
    tiku_event_t *event);

#endif /* TIKU_TRK_SCRIPT_ADAPTER_H_ */
