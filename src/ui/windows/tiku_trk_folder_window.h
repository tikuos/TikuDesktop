/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_window.h - folder-window state and rendering.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_WINDOW_H_
#define TIKU_TRK_FOLDER_WINDOW_H_

#include <stddef.h>

#include "tiku_desk_window.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_edit.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_background.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_query.h"
#include "tiku_trk_thumb.h"

typedef struct tiku_trk_folder_window tiku_trk_folder_window_t;

typedef void (*tiku_trk_folder_prepare_menu_fn)(void *context,
                                                 tiku_trk_folder_window_t *);

/** @brief Shared drawing services and live shell presentation state. */
typedef struct {
    tiku_desk_workspace_t *workspace;
    tiku_trk_cwmenu_t     *menus;
    tiku_trk_prefs_t      *prefs;
    tiku_trk_clip_t       *clip;
    tiku_trk_backend_t    *backend;
    tiku_trk_nodeicon_t   *icons;
    tiku_trk_store_t      *store;
    tiku_trk_thumb_q_t    *thumbs;
    int                    row_h;
    /* How many list rows the last frame actually painted.  A repaint that
     * touches one row and a repaint that touches every row produce the
     * same picture, so this is the only way to tell them apart (PVL-045,
     * PVL-048, PVL-075). */
    int                    rows_drawn;
    int                    screen_w;
    int                    screen_h;
    char                  *status;
    char                  *count_text;
    size_t                 count_text_size;
    int                   *finding;
    char                  *find_text;
    int                   *editing;
    char                  *edit_path;
    tiku_trk_edit_t       *value_edit;
    int                   *renaming;
    tiku_trk_folder_window_t **rename_folder;
    int                   *rename_row;
    tiku_trk_edit_t       *rename_edit;
    tiku_trk_folder_window_t **band_folder;
    int                   *band_x0;
    int                   *band_y0;
    int                   *band_x1;
    int                   *band_y1;
    tiku_trk_folder_window_t **drop_folder;
    int                   *drop_row;
    tiku_trk_folder_prepare_menu_fn prepare_menu;
    void                  *prepare_context;
} tiku_trk_folder_render_t;

/** @brief One folder window and the query or frame state it retains. */
struct tiku_trk_folder_window {
    tiku_trk_poseview_t       *view;
    char                       path[TIKU_TRK_PATH_MAX];
    char                       state_path[TIKU_TRK_PATH_MAX];
    tiku_desk_window_t        *win;
    tiku_trk_qrun_t           *run;
    int                        live;
    int                        gone;
    tiku_trk_frame_t           opened_with;
    int                        had_frame;
    int                        count_last_total;
    int                        count_last_selected;
    char                       qname[TIKU_TRK_NAME_MAX];
    /* The picture behind it, decoded once and kept with the window that
     * shows it: re-reading the file every frame is a decode per frame. */
    tiku_trk_image_t           bg_image;
    char                       bg_shown[TIKU_TRK_PATH_MAX];
    int                        bg_drawn;   /* labels need a halo over it */
    tiku_trk_folder_render_t  *render;
};

void tiku_trk_folder_window_configure(tiku_trk_folder_window_t *folder,
                                      tiku_trk_folder_render_t *render);

void tiku_trk_folder_window_draw(tiku_desk_window_t *window,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content, void *context);
void tiku_trk_folder_window_draw_icons(tiku_trk_folder_window_t *folder,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content);

/**
 * @brief Draw the picture behind @p folder, if it carries one (CW-020).
 *
 * Icon modes only, which is the rule itself: a picture behind a table of
 * text is unreadable.  The desktop draws its own the same way.
 *
 * @return 1 when a picture was drawn, which is what tells the labels to
 *         carry a halo.
 */
int tiku_trk_folder_window_draw_background(tiku_trk_folder_window_t *folder,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content);

tiku_desk_rect_t tiku_trk_folder_window_content(
    const tiku_trk_folder_window_t *folder);
tiku_desk_rect_t tiku_trk_folder_window_bar(
    const tiku_trk_folder_window_t *folder);
tiku_desk_rect_t tiku_trk_folder_window_title(
    const tiku_trk_folder_window_t *folder);
tiku_desk_rect_t tiku_trk_folder_window_icon(
    const tiku_trk_folder_window_t *folder);
int tiku_trk_folder_window_fit_width(
    const tiku_trk_folder_window_t *folder);
int tiku_trk_folder_window_fit_height(
    const tiku_trk_folder_window_t *folder);
int tiku_trk_folder_window_row_at(const tiku_trk_folder_window_t *folder,
                                  int x, int y);
/** @brief Whether a point is in the bottom count/navigation strip (TS-043). */
int tiku_trk_folder_window_count_at(
    const tiku_trk_folder_window_t *folder, int x, int y);
int tiku_trk_folder_window_name_at(const tiku_trk_folder_window_t *folder,
                                   int row, int x, int y);

#endif /* TIKU_TRK_FOLDER_WINDOW_H_ */
