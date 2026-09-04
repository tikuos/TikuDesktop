/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_private.h - what the tiku_trk_app files share: the includes,
 * the types, the small accessors that read them, and the functions one file
 * defines for another.  Nothing outside this directory includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_APP_PRIVATE_H_
#define TIKU_TRK_APP_PRIVATE_H_

/* split: tag browser */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_strings.h"
#include "tiku_host.h"
#include "tiku_trk_application.h"
#include "tiku_attr.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_panel.h"
#include "tiku_field.h"
#include "tiku_model.h"
#include "tiku_fs.h"
#include "tiku_iconpaint.h"
#include "tiku_trk_clip.h"
#include "tiku_dl.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_trk_navigator.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_prefs.h"
#include "tiku_scroll.h"
#include "tiku_trk_titleview.h"
#include "tiku_volume.h"
#include "tiku_trk_script.h"
#include "tiku_trk_task.h"

#define WIN_W 760
#define WIN_H 520
#define TAB_H 21
#define BORDER 5
#define TITLE_H 18
#define NAV_H 26
#define COUNT_H 16
#define SCROLL_W 15

/* List mode uses Tracker's mini icon size; a taller row does not grow it. */
#define MINI_ICON_MAX 16

/* Tracker's own list row: ceilf(plain font size * 1.65) = 20 at 12 px. */

typedef struct {
    tiku_host_t    *host;
    tiku_surface_t *surf;
    tiku_trk_poseview_t *view;
    tiku_backend_t  *backend;
    char                 title[160];
    int                  row_h;
    int                  running;
    int                  dirty;
    /* rubber band and icon drag */
    int                  band_active;
    int                  band_x0, band_y0, band_x1, band_y1;
    int                  drag_active;
    int                  drag_x, drag_y;
    /* file operations */
    tiku_fs_t       *fs;
    char                 status[220];
    /* inline rename: the editor is a mode, as Tracker's TextWidget is */
    int                  editing;
    tiku_field_t      ed;
    char                 edit_path[TIKU_PATH_MAX];
    int                  edit_row;
    /* The window's own activation state, which decides whether a selection
     * is PAINTED; and where the last press landed, for the long press. */
    int                  active;
    int64_t              press_at;
    int                  press_x, press_y;
    int                  press_row;
    int                  press_down;
    int                  read_only;  /* the store refuses writes           */
    tiku_trk_titleview_t tv;         /* the column strip's tracking state  */
    int                  cursor_ew;  /* which cursor the window is showing */
    /* The list row to select once a delete has finished; -1 when the last
     * operation was not one that removes rows (FS-050). */
    int                  select_next;
    /* The scrollbar as an object: a range that can be held steady while
     * the user drags it, and detached while a bulk re-layout runs
     * (PVL-051, PVL-052, PVL-060). */
    tiku_scroll_t    sb;
    tiku_scroll_t    hsb;       /* the horizontal bar (TS-044)        */
    tiku_rect_t     hsb_rect;
    tiku_rect_t     sb_rect;
    /* The same container-window menus the desktop shell presents. */
    tiku_trk_cwmenu_t    menus;
    tiku_menu_t     *popup;
    tiku_menu_t     *popup_sub;
    tiku_rect_t     popup_frame;
    tiku_rect_t     popup_sub_frame;
    tiku_rect_t     viewbtn_rect;  /* the gutter's view popup    */
    int                  theme_mode;    /* -1 until first applied     */
    int                  bar_open;
    /* Browser chrome owns its history and location editor per window. */
    tiku_trk_navigator_t nav;
    tiku_field_t      nav_edit;
    int                  nav_editing;
    int                  show_nav;
    char                 prefs_path[TIKU_PATH_MAX];
    int                  prefs_present;
    /* TS-052: the preference that swaps type-to-select for type-to-filter,
     * applied live when the settings file changes and re-applied when the
     * view returns to list mode. */
    int                  filter_pref;
    time_t               prefs_mtime;
    off_t                prefs_size;
    int64_t              prefs_checked;
    /* The same command layer the multi-window shell uses.  The folder is a
     * wrapper over this window's own view, so one implementation of what
     * Delete or Paste means serves both shells (FEATURES rule 6). */
    tiku_trk_folder_window_t         folder;
    tiku_trk_clip_t                  clip;
    tiku_trk_folder_file_actions_t   file_actions;
    /* Deferred and periodic work, pulsed from the loop's own tick: the
     * cooperative protothread the TIKU note asks for (AW-083). */
    tiku_trk_loop_t                 *loop;
    int                  ptr_x, ptr_y;   /* the pointer, as last seen    */
    /* The modal question, hosted the way the desktop shell hosts it: the
     * same overlay, over this window's one surface, so the delete question
     * and a collision are the same question in both shells (FS-046). */
    tiku_trk_status_overlay_t        status_overlay;
} app_t;




/** @brief The strip the barber pole occupies at the right of the count. */
#define POLE_W 10

/* What the command line asked for. */
typedef struct {
    const char *port;
    const char *start;              /* the folder to open, else the home */
    int         baud;
    int         use_device;
    int64_t     deadline_us;        /* -seconds: stop at this time, 0 never */
    const char *shot;               /* write a frame here, then stop */
    const char *script_path;
} browser_options_t;

/* What the shell's loop keeps between one event and the next. */
typedef struct {
    tiku_trk_click_t    click;      /* the click before this one */
    tiku_trk_script_t  *sc;         /* the script driving this run, or NULL */
    int64_t             shot_at_us; /* when the -shot frame is due, 0 never */
} browser_loop_t;

/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_app.c */
int browser_paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                            int size, int dimmed);

/* tiku_trk_app_commands.c */
int browser_menubar_press(app_t *a, int x, int y, unsigned state);
int browser_navigator_press(app_t *a, int x, int y);
int browser_popup_press(app_t *a, int x, int y);
void browser_menu_invoke(app_t *a, int command);
void browser_open_context_menu(app_t *a, int row, int x, int y, unsigned state);

/* tiku_trk_app_context.c */
int browser_col_x(const app_t *a, const tiku_trk_column_t *c);
int browser_first_selected_index(app_t *a);
int browser_index_of_path(app_t *a, const char *path);
int browser_measure_prefix_px(const char *text, int n, void *ctx);
int browser_measure_text(const char *s, void *ctx);
int browser_navigator_height(const app_t *a);
int browser_next_edit_row(app_t *a, int row, int backwards);
int browser_pose_index(app_t *a, const tiku_trk_pose_t *wanted);
int browser_visible_rows(const app_t *a);
tiku_rect_t browser_list_rect(const app_t *a);
tiku_rect_t browser_menubar_rect(const app_t *a);
tiku_rect_t browser_navigator_rect(const app_t *a);
tiku_trk_sc_ctx_t browser_menu_ctx(app_t *a, unsigned mods);
void browser_app_alert_answer(void *context, const tiku_alert_t *alert);
void browser_app_alert_stop(void *context);
void browser_draw_folder_icon(app_t *a, tiku_rect_t bar);
void browser_draw_title(app_t *a, tiku_rect_t strip, const tiku_trk_column_t *c,
                        int pressed, int drag_dx);
void browser_navigator_layout(const app_t *a, tiku_rect_t *back,
                              tiku_rect_t *forward, tiku_rect_t *up,
                              tiku_rect_t *field);

/* tiku_trk_app_draw.c */
void browser_blit(app_t *a);
void browser_count_label(app_t *a, int width, char *label, size_t max);
void browser_draw(app_t *a);

/* tiku_trk_app_events.c */
void browser_pointer_down(app_t *app, const tiku_event_t *ev,
                          browser_loop_t *lp);
void browser_pointer_move(app_t *app, const tiku_event_t *ev,
                          browser_loop_t *lp);
void browser_pointer_up(app_t *app, const tiku_event_t *ev, browser_loop_t *lp);
void browser_turn_end(app_t *app, const browser_options_t *o,
                      browser_loop_t *lp);

/* tiku_trk_app_fileops.c */
void browser_after_op(app_t *a);
void browser_drain_fs(tiku_fs_t *fs);

/* tiku_trk_app_keys.c */
int browser_title_hit(app_t *a, int px, int py, int *cx, int *cy);
void browser_handle_key(app_t *a, const tiku_event_t *event);
void browser_menu_key(app_t *a, unsigned key, unsigned modifiers);

/* tiku_trk_app_layout.c */
int browser_selection_paths(app_t *a, char paths[][TIKU_PATH_MAX],
                            const char *out[], int max);
void browser_retitle(app_t *a);
void browser_settle_layout(app_t *a);
void browser_settle_writability(app_t *a);
void browser_sync_navigator_preference(app_t *a, int force);

/* tiku_trk_app_lifecycle.c */
int browser_boot(app_t *app, browser_options_t *o, browser_loop_t *lp);
int browser_parse_args(int argc, char **argv, browser_options_t *o);
int browser_teardown(app_t *app, browser_loop_t *lp);

/* tiku_trk_app_navigate.c */
int browser_navigate_to(app_t *a, const char *path, int record);
void browser_begin_location(app_t *a);
void browser_end_location(app_t *a, int apply);
void browser_navigate_back(app_t *a);
void browser_navigate_forward(app_t *a);
void browser_navigate_up(app_t *a);
void browser_open_animation(app_t *a, tiku_rect_t from);
void browser_open_selection(app_t *a);

/* tiku_trk_app_rename.c */
void browser_begin_rename(app_t *a);
void browser_begin_rename_row(app_t *a, int row);
void browser_end_rename(app_t *a, int commit);

/* tiku_trk_app_script.c */
char browser_key_char(const tiku_event_t *event);
int browser_count_at(app_t *a, int px, int py);
int browser_row_at(app_t *a, int px, int py);
int browser_script_pose_at(void *ctx, const char *name, int *x, int *y);
int browser_script_pose_screen(void *ctx, const char *name, int *x, int *y);
int browser_script_row_point(void *ctx, int row, int *x, int *y);
int browser_script_say(void *ctx, const char *noun, char *out, size_t max);
size_t browser_script_narrate(void *ctx, char *out, size_t max);
unsigned browser_script_pixel_at(void *ctx, int x, int y);

/* End of the shared declarations. */

#endif /* TIKU_TRK_APP_PRIVATE_H_ */
