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
    /* A pick on a Deskbar row is a press and a release, as a click is:
     * the release is owed at the row's point on the next turn, since a
     * program's row starts its program only when the button comes up. */
    int release_owed;
    int release_x, release_y;
    int row_h;
    /* The shell's own full redraw, for the check that what the frames
     * painted is what a full redraw would have drawn. */
    long               (*redraw_diff)(void *ctx);
    void                *redraw_ctx;
    /* The shell's screen resize, for a script that wants a picture larger
     * than the window it began in. */
    int                (*resize)(void *ctx, int width, int height);
    void                *resize_ctx;
    /* The shell's named-fact answerer -- the SAME function its conductor
     * channel answers QUERY with, so both surfaces stay one surface. */
    int                (*say)(void *ctx, const char *noun, char *out,
                              size_t max);
    void                *say_ctx;
} tiku_trk_script_adapter_t;

/**
 * @brief Bind the adapter to the shell's pieces: the workspace, the
 *        surface, the windows and menus a script may ask about.
 *
 * Nothing is owned; every pointer must outlive the adapter.
 */
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
/**
 * @brief A point inside the tab of the open window whose title contains
 *        @p title (or its path's leaf), where that tab is really in front.
 *
 * @return 1 with @p x and @p y set, 0 when no such window is open.
 */
int tiku_trk_script_adapter_tab_point(void *context, const char *title,
                                      int *x, int *y);

/**
 * @brief A point on row @p row of the focused list-mode folder window.
 *
 * @return 1 with @p x and @p y set, 0 when the focused window is not a
 *         folder in list mode or the row is not on screen.
 */
int tiku_trk_script_adapter_row_point(void *context, int row,
                                      int *x, int *y);
/** @brief The RGB of the surface pixel at (@p x, @p y); 0 off the surface. */
unsigned tiku_trk_script_adapter_pixel_at(void *context, int x, int y);
/**
 * @brief Where the pose named @p name is drawn, in screen pixels: the
 *        focused folder first, then the Desktop.
 *
 * @return 1 with @p x and @p y set, 0 when no pose has that name.
 */
int tiku_trk_script_adapter_pose_screen(void *context, const char *name,
                                        int *x, int *y);
/**
 * @brief Where the pose named @p name sits in its view: the focused
 *        folder first, then the Desktop.
 *
 * @return 1 with @p x and @p y set, 0 when no pose has that name.
 */
int tiku_trk_script_adapter_pose_at(void *context, const char *name,
                                    int *x, int *y);

typedef enum {
    TIKU_TRK_SCRIPT_ADAPTER_STOP = -1,
    TIKU_TRK_SCRIPT_ADAPTER_IDLE = 0,
    TIKU_TRK_SCRIPT_ADAPTER_EVENT = 1,
    TIKU_TRK_SCRIPT_ADAPTER_SHOT = 2
} tiku_trk_script_adapter_result_t;

/**
 * @brief Run the script's next step against the shell's current state.
 *
 * A key, a move, a press or a release comes back as @p event; a shot
 * leaves its path in the adapter; quit and a finished script stop.
 *
 * @return EVENT when @p event is to be delivered, SHOT when a frame is
 *         due, IDLE when the step needs no event, STOP when it is over.
 */
tiku_trk_script_adapter_result_t tiku_trk_script_adapter_next(
    tiku_trk_script_adapter_t *adapter, tiku_trk_script_t *script,
    tiku_event_t *event);

#endif /* TIKU_TRK_SCRIPT_ADAPTER_H_ */
