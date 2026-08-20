/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop.c - the desktop shell.
 *
 * One host window holds the whole thing: a backdrop, folder windows with the
 * R5 tab chrome, and the Deskbar in the corner with a live tray.  Opening a
 * folder opens ITS window, and opening it again brings that window forward
 * rather than making a second one -- the spatial promise.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tiku_trk_attr.h"
#include "tiku_desk_host.h"
#include "tiku_trk_application.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_info.h"
#include "tiku_trk_info_window.h"
#include "tiku_trk_open_with_window.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_panel_window.h"
#include "tiku_trk_thumb.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_prefs_window.h"
#include "tiku_trk_deskbar_prefs_window.h"
#include "tiku_trk_volume.h"
#include "tiku_trk_switcher.h"
#include "tiku_trk_switcher_window.h"
#include "tiku_trk_navmenu.h"
#include "tiku_trk_titleview.h"
#include "tiku_trk_script.h"
#include "tiku_trk_script_api.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_roster.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_types.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_model.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_edit.h"
#include "tiku_trk_select.h"
#include "tiku_trk_select_window.h"
#include "tiku_trk_find.h"
#include "tiku_trk_find_window.h"
#include "tiku_trk_drop_controller.h"
#include "tiku_trk_folder_actions.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_folder_edit.h"
#include "tiku_trk_folder_menu.h"
#include "tiku_trk_addon.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_band_controller.h"
#include "tiku_trk_drag_target.h"
#include "tiku_trk_launch_controller.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_trk_state.h"
#include "tiku_trk_query.h"
#include "tiku_trk_task.h"
#include "tiku_trk_session_controller.h"
#include "tiku_trk_script_adapter.h"
#include "tiku_desk_window.h"

#define DESK_W 1000
#define DESK_H  680
#define COUNT_H  16

/* How many entries per turn are asked whether they carry a picture of their
 * own.  A bound rather than the whole listing: the ask opens the file. */
#define HINTS_PER_TURN 24

/* Tracker's own list row: ceilf(plain font size * 1.65), which at the 12 px
 * face is 20 -- tall enough that a 16 px mini icon sits in it with room, and
 * the reason rows do not tighten onto the text. */
#define LIST_ROW_H 20

/** @brief How much of the count view's text a script can read. */
#define TIKU_TRK_COUNT_TEXT_MAX 64

typedef tiku_trk_folder_window_t folder_t;

typedef struct desktop {
    tiku_desk_host_t    *host;
    tiku_desk_surface_t *surf;
    tiku_desk_workspace_t     *desk;
    tiku_trk_deskbar_t  *deskbar;
    tiku_trk_backend_t  *backend;
    tiku_trk_fs_t       *fs;
    folder_t             folders[TIKU_DESK_WINDOW_MAX];
    tiku_trk_folder_render_t folder_render;
    tiku_trk_folder_lifecycle_t folder_lifecycle;
    int                  folder_count;
    int                  row_h;
    int                  team;
    int                  running;
    int                  dirty;
    /* Menus.  One set serves every folder window, rebuilt from whichever is
     * focused each time a menu opens -- the original rebuilds on open too. */
    tiku_trk_cwmenu_t    menus;
    tiku_trk_launch_controller_t launcher;
    tiku_trk_drop_controller_t drop_controller;
    tiku_trk_folder_file_actions_t file_actions;
    tiku_trk_folder_edit_t folder_edit;
    tiku_trk_band_controller_t band_controller;
    tiku_trk_drag_target_t drag_target;
    tiku_trk_session_controller_t session;
    tiku_trk_script_adapter_t script_adapter;
    tiku_trk_folder_menu_t folder_menu;
    tiku_trk_menu_t     *popup;          /* the menu showing, or NULL       */
    tiku_desk_rect_t     popup_frame;
    tiku_trk_menu_t     *popup_sub;      /* its open submenu, or NULL       */
    tiku_desk_rect_t     popup_sub_frame;
    folder_t            *bar_fl;         /* window whose bar opened popup   */
    int                  bar_open;       /* title in that window, or -1     */
    char                 status[200];
    uint32_t             backdrop_color;
    /* Find is a MODE, as the shell's own watch is: there is no second window
     * to put a panel in, and a namespace search wants the same one-line
     * entry the rest of the system uses. */
    int                  finding;
    char                 find_buf[64];
    int                  find_len;
    int                  find_serial;
    tiku_trk_find_window_t find_window;
    /* The query the save panel is asking a destination for. */
    tiku_trk_query_t     pending_query;
    /* Which theme the desktop is wearing; -1 until first applied. */
    int                  theme_dark;
    /* The Find icon in flight (Q-034): dragged, and where it landed. */
    int                  qdrag_on;
    char                 qdrop_dest[TIKU_TRK_PATH_MAX];
    /* The Deskbar's own double-click memory (DB-019). */
    int64_t              db_click_at;
    int                  db_click_x, db_click_y;
    /* The zoom: a frame sliding between the menu item and the window it
     * names, alive for a few frames after a Deskbar window action
     * (DB-030). */
    int64_t              zoom_t0;
    tiku_desk_rect_t     zoom_from, zoom_to;
    /* A CMD press on the bar, still ambiguous: moved it re-docks, held
     * still it is a chord the rows answer (DB-049 vs DB-021). */
    int                  dock_armed;
    int                  dock_x, dock_y;
    unsigned             dock_mods;
    unsigned             dock_button;
    /* The resize strip along the bar's inner edge (DB-050). */
    int                  bar_resize;
    int                  bar_resize_x;
    int                  bar_resize_w;
    /* One cascade origin shared by every window that opens without a
     * remembered position, advanced per window and reset when it runs off
     * the screen -- not a modulo, which would silently stack the sixth
     * window on the first. */
    int                  cascade_x, cascade_y;
    struct tiku_trk_store *store;
    /* The backdrop pose view owns its union source and watcher. */
    tiku_trk_desktop_view_t desktop_view;
    int                  desk_row;      /* pose under the pointer, or -1 */
    /* ONE clipboard for the desktop, as the system's is one: cutting in a
     * window and pasting in another is the whole point of it. */
    tiku_trk_clip_t      clip;
    /* Select by pattern.  ONE of these serves the whole desktop and is
     * hidden rather than destroyed, so its expression outlives the window
     * it was last used on (CW-063, AW-105). */
    tiku_trk_select_window_t select_window;
    tiku_trk_open_with_window_t open_with_window;
    /* Deferred and periodic work, pulsed from this loop's own tick: the
     * cooperative protothread the TIKU note asks for in place of the
     * source's worker thread (AW-083). */
    tiku_trk_loop_t     *loop;
    char                 clock_stamp[64]; /* what the clock last showed  */
    /* Where the last press landed, for the long press and for telling a
     * slow second click from a drag. */
    int64_t              press_at;
    int                  press_x, press_y;
    int                  press_row;
    tiku_trk_drag_t      drag;      /* the gesture between press and drop */
    unsigned             drag_live_mods; /* modifiers as of the last move  */
    /* The column strip's tracker (TS-063): one gesture at a time, owned
     * by whichever folder window the press landed in. */
    tiku_trk_titleview_t tstrip;
    folder_t            *tstrip_fl;
    folder_t            *sbar_fl;   /* whose bar holds the pointer      */
    int                  strip_ptr_x;
    int                  strip_ptr_y;
    tiku_trk_thumb_q_t   thumbs;    /* image previews, made off-tick  */
    /* ONE node-icon cache for the desktop: the same file shown in two
     * windows is one entry, and an icon edited on disk is dropped once
     * rather than per window (IV-038). */
    tiku_trk_nodeicon_t  nicons;
    /* Which applications there are and what each reads.  One registry for
     * the desktop: the Open with menu, a double click and a drop on an
     * application all ask the same question (AW-040). */
    tiku_trk_apps_t      apps;
    tiku_trk_addons_t    addons;
    /* The add-ons directory and the user shortcut file, both watched by
     * modification time so an install or a rebind lands without a restart
     * (AW-022, AW-023). */
    char                 addons_dir[TIKU_TRK_PATH_MAX];
    char                 addons_keys[TIKU_TRK_PATH_MAX];
    time_t               addons_mtime;
    time_t               keys_mtime;
    int64_t              addons_checked;
    /* What the system knows about a type: its description, its icon, and
     * the columns it is worth showing (AW-013, CW-026). */
    tiku_trk_types_t     types;
    /* What the count view last drew, so a script can read the one line
     * that reports the selection rather than the folder. */
    char                 count_text[TIKU_TRK_COUNT_TEXT_MAX];
    /* The folder the running job is writing into, for the progress
     * window's "To:" line (FS-069). */
    char                 prog_dest[TIKU_TRK_PATH_MAX];
    /* The Open/Save panel.  ONE, because it is asking one question at a
     * time and a second would be a second answer to the same ask. */
    /* What is mounted, and what it was last time we looked: the port's
     * only source of mount and unmount notices (PVN-008). */
    tiku_trk_volwatch_t  volwatch;
    /* The record the last search wrote, so Edit query has something to
     * reopen when the window it came from is the one asking. */
    char                 last_query[TIKU_TRK_NAME_MAX];
    tiku_trk_panel_window_t file_panel;
    tiku_trk_info_windows_t info_windows;
    /* The broken link an open offered Get Info for (AW-031). */
    char                 broken_open_path[TIKU_TRK_PATH_MAX];
    /* Which broken link a picker was opened to re-point, or -1. */
    int                  info_relink;
    tiku_trk_status_overlay_t status_overlay;
    tiku_trk_prefs_window_t prefs_window;
    tiku_trk_deskbar_prefs_window_t deskbar_prefs_window;
    int                  applied_hide_dotfiles;
    int                  applied_full_path;
    int                  applied_folders_first;
    tiku_trk_switcher_window_t switcher_window;
    tiku_trk_nav_t       nav;      /* the open navigation walk   */
    int                  nav_on;
    /* The SPRING navigation: the same walk, anchored to a sprung pose
     * instead of a context-menu row (PVS-051). */
    int                  spring_nav_on;
    int                  spring_nav_x;
    int                  spring_nav_y;
    tiku_desk_window_t      *prog_was_active;  /* focus to give back */
    int                  press_down;
    int                  ptr_x, ptr_y;  /* where the pointer last was      */
    /* A context menu raised on one named thing -- the info window's icon --
     * rather than on a window's selection (AW-076). */
    char                 popup_path[TIKU_TRK_PATH_MAX];
    /* And which FOLDER: the menu model is one shared thing that every
     * drawing folder rebuilds for itself, so an open popup has to be put
     * back for the folder it was raised on -- and the choice carried out
     * there, not on whatever window has focus. */
    folder_t            *popup_fl;
    tiku_desk_window_t  *press_window; /* deferred content raise (PVS-022) */
    tiku_desk_window_t  *press_focus;  /* who was in front when it began   */
    int                  dock_drag;     /* Ctrl-dragging the Deskbar     */
    folder_t            *press_fl;
    int                  press_menu;    /* the long press already fired    */
    int                  deskbar_press; /* primary hold over the clock     */
} desktop_t;

static folder_t *open_folder(desktop_t *app, const char *path);
static void close_windows_under(desktop_t *app, const char *path);
static int list_children(tiku_trk_backend_t *b, const char *path,
                         tiku_trk_model_t *out, int max);
static void info_opens_with_fill(desktop_t *app, tiku_trk_info_t *info);
static void info_opens_with_next(desktop_t *app, tiku_trk_info_t *info);
static folder_t *folder_of(desktop_t *app, tiku_desk_window_t *w);
static folder_t *desktop_folder(desktop_t *app);
static void close_window(desktop_t *app, tiku_desk_window_t *w);
static int select_named(desktop_t *app, folder_t *fl, const char *name);

static void
set_backdrop_color(void *context, const tiku_trk_color_t *color)
{
    desktop_t *app = context;

    if (app != NULL && color != NULL) {
        app->backdrop_color = TIKU_DESK_RGB(color->r, color->g, color->b);
    }
}

static void
parent_path(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, size, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= size) {
        n = size - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

/** @brief Let the launch controller hand Tracker-owned paths to the shell. */
static void
launch_open_path(void *context, const char *path)
{
    (void)open_folder(context, path);
}

/** @brief Ensure the editable local menu exists before showing it. */
static void
deskbar_edit_path(void *context, const char *path)
{
    desktop_t *app = context;

    if (app->backend != NULL && app->backend->ops != NULL &&
        strcmp(app->backend->ops->name, "posix") == 0) {
        char parent[TIKU_TRK_PATH_MAX];
        char *slash;

        snprintf(parent, sizeof parent, "%s", path);
        slash = strrchr(parent, '/');
        if (slash != NULL && slash != parent) {
            *slash = '\0';
            (void)mkdir(parent, 0700);
        }
        (void)mkdir(path, 0700);
    }
    (void)open_folder(app, path);
}

/** @brief Close invalidated spatial windows on behalf of file actions. */
static void
file_actions_close_under(void *context, const char *path)
{
    close_windows_under(context, path);
}

/** @brief Preferences remain shared data; their window owns the lifetime. */
static tiku_trk_prefs_t *
prefs_model(desktop_t *app)
{
    return tiku_trk_prefs_window_model(&app->prefs_window);
}

/** @brief Apply folder-list settings to every already-open spatial view. */
static void
sync_folder_preferences(desktop_t *app)
{
    tiku_trk_prefs_t *prefs = prefs_model(app);
    int hide = tiku_trk_prefs_get(prefs, "HideDotFiles") != 0;
    int folders_first = tiku_trk_prefs_get(prefs, "SortFolderNamesFirst") != 0;
    int filter = tiku_trk_prefs_get(prefs, "TypeAheadFiltering") != 0;
    int full_path = tiku_trk_prefs_get(prefs, "ShowFullPathInTitleBar") != 0;
    int i;

    /* The thumbnail queue's own switch IS the preference (TS-053). */
    app->thumbs.enabled = tiku_trk_prefs_get(prefs,
                              "GenerateImageThumbnails") != 0;
    {
        /* Dark mode is a THEME SWAP, applied the moment the setting says
         * so (settings semantics: immediate, no OK button).  A flip
         * forgets every window's pixels -- they were painted in the other
         * palette -- and repaints the world once. */
        int dark = tiku_trk_prefs_get(prefs, "DarkMode") != 0;

        if (dark != app->theme_dark) {
            tiku_trk_folder_window_t *desk =
                tiku_trk_desktop_view_folder(&app->desktop_view);

            app->theme_dark = dark;
            tiku_desk_theme_set(dark ? tiku_desk_theme_dark() : NULL);
            for (i = 0; i < app->folder_count; i++) {
                tiku_trk_folder_window_forget(&app->folders[i]);
            }
            if (desk != NULL) {
                tiku_trk_folder_window_forget(desk);
            }
            app->dirty = 1;
        }
    }
    /* The file panel's top: with the setting on, Up from the root lands
     * on the Desktop and the Desktop is the top (AW-061).  Read live, so
     * a change re-answers the very next Up. */
    app->file_panel.model.desktop_is_root =
        tiku_trk_prefs_get(prefs, "DesktopFilePanelRoot") != 0;

    {   /* With BOTH storage settings off there is no doorway to the root
         * left on the Desktop, so a Disks window still open is a view of
         * something the user just asked not to see: it closes with the
         * icon (PVN-058). */
        int disks = tiku_trk_prefs_get(prefs, "ShowDisksIcon") != 0 ||
                    tiku_trk_prefs_get(prefs,
                                       "MountVolumesOntoDesktop") != 0;

        if (!disks) {
            for (i = app->folder_count - 1; i >= 0; i--) {
                folder_t *folder = &app->folders[i];

                if (folder->win != NULL && folder->win->open &&
                    strcmp(folder->path, "/") == 0) {
                    close_window(app, folder->win);
                    app->dirty = 1;
                }
            }
        }
    }

    if (full_path != app->applied_full_path) {
        /* kWindowsShowFullPathChanged: every OPEN window re-titles the
         * moment the preference moves (TS-047). */
        app->applied_full_path = full_path;
        for (i = 0; i < app->folder_count; i++) {
            folder_t *folder = &app->folders[i];

            if (folder->view != NULL && folder->win != NULL) {
                tiku_trk_folder_title_for(prefs, folder->path,
                                          folder->win->title,
                                          sizeof folder->win->title);
            }
        }
        app->dirty = 1;
    }

    /* Every turn, not only on change: a window opened or switched to list
     * mode since the last pass must pick the preference up too, and the
     * per-view guard keeps the steady state free (TS-052). */
    for (i = 0; i < app->folder_count; i++) {
        folder_t *folder = &app->folders[i];

        if (folder->view == NULL ||
            tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
            continue;
        }
        if (filter != tiku_trk_poseview_is_filtering(folder->view)) {
            tiku_trk_poseview_set_filtering(folder->view, filter);
            app->dirty = 1;
        }
    }
    if (hide == app->applied_hide_dotfiles &&
        folders_first == app->applied_folders_first) {
        return;
    }
    app->applied_hide_dotfiles = hide;
    app->applied_folders_first = folders_first;
    for (i = 0; i < app->folder_count; i++) {
        folder_t *folder = &app->folders[i];

        if (folder->view == NULL) {
            continue;
        }
        tiku_trk_poseview_set_hide_dotfiles(folder->view, hide);
        tiku_trk_poseview_set_folders_first(folder->view, folders_first);
    }
    app->dirty = 1;
}

/** @brief Select state is retained by its reusable window controller. */
static tiku_trk_select_t *
select_model(desktop_t *app)
{
    return tiku_trk_select_window_model(&app->select_window);
}

/** @brief Find state is retained by its reusable window controller. */
static tiku_trk_find_t *
find_model(desktop_t *app)
{
    return tiku_trk_find_window_model(&app->find_window);
}

/** @brief Modal question state is presented by the shared overlay. */
static tiku_trk_alert_t *
alert_model(desktop_t *app)
{
    return tiku_trk_status_overlay_alert(&app->status_overlay);
}

/** @brief File-job state is presented by the shared overlay. */
static tiku_trk_progress_win_t *
progress_model(desktop_t *app)
{
    return tiku_trk_status_overlay_progress(&app->status_overlay);
}

/** @brief Preferences, as this shell shows them: a window of its own. */
static void
open_preferences(void *context)
{
    desktop_t *app = context;

    tiku_trk_prefs_window_open(&app->prefs_window);
}

/**
 * @brief Offer Get Info for a link that leads nowhere (AW-031).
 *
 * Instead of a bare error: the window that can SAY what is wrong -- and
 * re-point it -- is one button away.
 */
static void
offer_broken_link(desktop_t *app, const char *path)
{
    snprintf(app->broken_open_path, sizeof app->broken_open_path, "%s",
             path);
    tiku_trk_alert_open(alert_model(app), TIKU_TRK_ALERT_WARN,
                        TIKU_TRK_ALERT_TAG_BROKEN_OPEN,
                        "There is no file to open: the link's target "
                        "cannot be found.", "Cancel|Get info");
}


/** @brief Real art for the Deskbar's own menu rows (DB-002). */
static int
deskbar_menu_icon(void *context, tiku_desk_surface_t *s,
                  const tiku_trk_db_item_t *item, int x, int y, int size)
{
    const char *art = NULL;
    struct stat st;

    (void)context;
    switch (item->kind) {
    case TIKU_TRK_DB_ITEM_DIR:
        art = "folder";
        break;
    case TIKU_TRK_DB_ITEM_FIND:
        art = "query";
        break;
    case TIKU_TRK_DB_ITEM_RECENT:
        art = (item->which == 1) ? "folder"
            : (item->which == 2) ? "application" : "file";
        break;
    case TIKU_TRK_DB_ITEM_PATH:
        art = (stat(item->path, &st) == 0 && S_ISDIR(st.st_mode))
                  ? "folder" : "file";
        break;
    default:
        return 0;               /* a command row carries no art */
    }
    return tiku_trk_icon_paint(s, art, x, y, size);
}

/** @brief Real art for menu items, dimmed for a disabled one (IV-054). */
static int
paint_menu_icon(tiku_desk_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    return dimmed
        ? tiku_trk_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_DESK_C_PANEL)
        : tiku_trk_icon_paint(s, icon, x, y, size);
}

/** @brief Hand a modal filesystem answer back to the operation engine. */
static void
status_answer(void *context, const tiku_trk_alert_t *alert)
{
    desktop_t *app = context;

    if (alert->tag == TIKU_TRK_ALERT_TAG_CONFLICT) {
        if (alert->chosen == alert->cancel) {
            tiku_trk_fs_cancel(app->fs);
        } else {
            tiku_trk_fs_resolve(app->fs,
                                tiku_trk_alert_conflict_answer(alert));
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_ERROR) {
        tiku_trk_fs_resolve_error(app->fs,
                                  alert->chosen != alert->cancel);
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SHUTDOWN) {
        if (alert->chosen != alert->cancel) {
            app->running = 0;
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SAVE_REPLACE) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_panel_window_confirmed(&app->file_panel);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_OPEN_ANYWAY) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_open_with_window_confirmed(&app->open_with_window);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_BROKEN_OPEN) {
        if (alert->chosen != alert->cancel &&
            app->broken_open_path[0] != '\0') {
            {
                int islot = tiku_trk_info_windows_open(&app->info_windows,
                                                app->broken_open_path);

                if (islot >= 0) {
                    info_opens_with_fill(app,
                        &app->info_windows.model[islot]);
                }
            }
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_EMPTY_TRASH) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_folder_file_actions_answer_empty(&app->file_actions);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_DELETE) {
        /* Cancel, Move to Trash, Delete, in the order the buttons are
         * offered (FS-046). */
        folder_t *fl = folder_of(app, tiku_desk_workspace_focused(app->desk));

        if (fl == NULL) {
            fl = desktop_folder(app);
        }
        {
            /* Cancel, Move to Trash, Delete, in the order the buttons
             * are offered (FS-046); the alert's own default already
             * steps left of Delete, so Return lands on Move to Trash. */
            static const tiku_trk_delete_answer_t by_button[] = {
                TIKU_TRK_DELETE_ANSWER_CANCEL,
                TIKU_TRK_DELETE_ANSWER_TRASH,
                TIKU_TRK_DELETE_ANSWER_DELETE,
            };
            int chosen = (alert->chosen >= 0 && alert->chosen < 3)
                             ? alert->chosen : 0;

            tiku_trk_folder_file_actions_answer_delete(&app->file_actions,
                fl, by_button[chosen]);
        }
    }
}

/** @brief Stop asks the operation engine to cancel at its safe boundary. */
static void
status_stop(void *context)
{
    desktop_t *app = context;

    tiku_trk_fs_cancel(app->fs);
}

/** @brief The clock's own pulse: a minute must not go stale (DB-044). */
static int
tick_clock(void *ctx)
{
    desktop_t *app = ctx;
    char stamp[64];

    if (tiku_trk_deskbar_clock_stamp(app->deskbar, stamp,
                                     sizeof stamp) &&
        strcmp(stamp, app->clock_stamp) != 0) {
        /* Only when the displayed time turned over: a blind dirty every
         * second would repaint rows the economy checks rightly budget.
         * The FIRST observation is a baseline, not a change -- settings
         * and the fixture freeze both arrive after the loop starts. */
        if (app->clock_stamp[0] != '\0') {
            app->dirty = 1;
        }
        snprintf(app->clock_stamp, sizeof app->clock_stamp, "%s", stamp);
    }
    return 0;                       /* periodic: never done */
}

/**
 * @brief Re-resolve what a mount may have fixed, half a second on (PVN-042).
 *
 * Deliberately deferred so a link to the arriving volume resolves as a
 * volume rather than a plain directory.
 */
static int
refresh_after_mount(void *ctx)
{
    desktop_t *app = ctx;
    int i;

    for (i = 0; i < app->folder_count; i++) {
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            app->folders[i].view != NULL) {
            (void)tiku_trk_poseview_reload(app->folders[i].view);
        }
    }
    tiku_trk_desktop_view_rebuild(&app->desktop_view);
    app->dirty = 1;
    return 1;
}

/** @brief One created entry waiting to be selected where it lives. */
typedef struct {
    desktop_t *app;
    char       path[TIKU_TRK_PATH_MAX];
} select_created_t;

/**
 * @brief Select what an operation made, once it shows up (AW-033).
 *
 * Retried on its period until the pose exists or the loop's timeout gives
 * up: the entry may still be arriving through the listing.
 */
static int
select_created(void *ctx)
{
    select_created_t *sc = ctx;
    char parent[TIKU_TRK_PATH_MAX];
    char *slash;
    int i;

    snprintf(parent, sizeof parent, "%s", sc->path);
    slash = strrchr(parent, '/');
    if (slash == NULL) {
        return 1;
    }
    if (slash == parent) { parent[1] = '\0'; } else { *slash = '\0'; }
    for (i = 0; i < sc->app->folder_count; i++) {
        folder_t *fl = &sc->app->folders[i];

        if (fl->win == NULL || !fl->win->open || fl->view == NULL ||
            strcmp(fl->path, parent) != 0) {
            continue;
        }
        slash = strrchr(sc->path, '/');
        if (select_named(sc->app, fl,
                         slash != NULL ? slash + 1 : sc->path) >= 0) {
            sc->app->dirty = 1;
            return 1;           /* found and selected: done */
        }
        return 0;               /* the window is there; the entry not yet */
    }
    return 1;                   /* no window shows the parent: nothing owed */
}

/**
 * @brief Save what is open on the leaving volume, first (AW-096).
 *
 * While the volume is still there to save to: positions flushed for every
 * window on it, and an active query holding the device open is closed so
 * it cannot block the unmount.
 */
static void
save_before_unmount(dev_t dev, void *context)
{
    desktop_t *app = context;
    int i;

    for (i = 0; i < app->folder_count; i++) {
        folder_t *fl = &app->folders[i];

        if (fl->win == NULL || !fl->win->open || fl->view == NULL) {
            continue;
        }
        if (tiku_trk_poseview_is_query(fl->view) &&
            tiku_trk_query_targets_device(fl->run, (uint64_t)dev)) {
            close_window(app, fl->win);
            continue;
        }
        if (tiku_trk_poseview_on_device(fl->view, (uint64_t)dev)) {
            (void)tiku_trk_poseview_save_state(fl->view);
        }
    }
    {
        folder_t *desk = desktop_folder(app);

        if (desk != NULL && desk->view != NULL &&
            tiku_trk_poseview_on_device(desk->view, (uint64_t)dev)) {
            (void)tiku_trk_poseview_save_state(desk->view);
        }
    }
}

/**
 * @brief Keep the add-on inventory true to the directory and the user's
 *        shortcut file (AW-022, AW-023, CW-038).
 */
static void
sync_addons(desktop_t *app, int force)
{
    struct stat st;
    int64_t now = tiku_trk_now();

    if (!force && now - app->addons_checked < 500000) {
        return;
    }
    app->addons_checked = now;
    if (app->addons_dir[0] != '\0') {
        time_t mtime = (stat(app->addons_dir, &st) == 0) ? st.st_mtime : 0;

        if (force || mtime != app->addons_mtime) {
            app->addons_mtime = mtime;
            (void)tiku_trk_addons_discover(&app->addons, app->addons_dir);
            app->dirty = 1;
        }
    }
    if (app->addons_keys[0] != '\0') {
        time_t mtime = (stat(app->addons_keys, &st) == 0) ? st.st_mtime : 0;

        if ((force || mtime != app->keys_mtime) && mtime != 0) {
            FILE *f = fopen(app->addons_keys, "r");

            app->keys_mtime = mtime;
            if (f != NULL) {
                char buf[2048];
                size_t got = fread(buf, 1u, sizeof buf - 1u, f);

                buf[got] = '\0';
                (void)fclose(f);
                (void)tiku_trk_addons_rebind(&app->addons, buf);
                app->dirty = 1;
            }
        }
    }
}

/** @brief What the action table needs to know about the focused window. */
static tiku_trk_sc_ctx_t
menu_ctx(desktop_t *app, folder_t *fl, unsigned mods)
{
    return tiku_trk_folder_menu_context(&app->folder_menu, fl, mods);
}

/** @brief One destination row of a picker (CW-035). */
static void
picker_add(desktop_t *app, int k, const char *label, const char *path,
           int move_dev_ok)
{
    tiku_trk_cwmenu_t *cw = &app->menus;
    int n = cw->picker_paths[k];
    int i;

    if (path == NULL || path[0] == '\0' || n >= TIKU_TRK_CMD_DEST_MAX ||
        (k == 0 && !move_dev_ok)) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(cw->picker_path[k][i], path) == 0) {
            return;             /* one row per destination */
        }
    }
    if (tiku_trk_menu_add(&cw->picker[k], label,
            TIKU_TRK_CMD_DEST_BASE + k * TIKU_TRK_CMD_DEST_MAX + n,
            0, 0u, 1) >= 0) {
        snprintf(cw->picker_path[k][n], TIKU_TRK_PATH_MAX, "%s", path);
        cw->picker_paths[k] = n + 1;
    }
}

/**
 * @brief The Move to / Copy to / Create link pickers (CW-035, CW-058).
 *
 * Current folder, Recent folders, Desktop, Home, then volumes -- and Move
 * offers only destinations on the item's own device, the addLocalOnly
 * rule: a cross-device move is a copy wearing the wrong name.
 */
static void
build_pickers(desktop_t *app, folder_t *fl)
{
    static const char *title[3] = { "Move to", "Copy to", "Create link" };
    const char *home = getenv("HOME");
    char recent[8][TIKU_TRK_PATH_MAX];
    char desk[TIKU_TRK_PATH_MAX];
    struct stat here;
    tiku_trk_volumes_t vs;
    int have_here;
    int nrecent;
    int nvol;
    int k, i;

    if (fl == NULL) {
        return;
    }
    have_here = stat(fl->path, &here) == 0;
    nrecent = tiku_trk_recent_list(app->store, TIKU_TRK_RECENT_FOLDERS, 6,
                                   recent, 8);
    nvol = (tiku_trk_volumes_scan(&vs) > 0) ? vs.n : 0;
    snprintf(desk, sizeof desk, "%s/Desktop",
             (home != NULL && home[0] != '\0') ? home : "");
    for (k = 0; k < 3; k++) {
        tiku_trk_menu_clear(&app->menus.picker[k]);
        snprintf(app->menus.picker[k].title,
                 sizeof app->menus.picker[k].title, "%s", title[k]);
        app->menus.picker_paths[k] = 0;
        picker_add(app, k, "Current folder", fl->path, 1);
        for (i = 0; i < nrecent; i++) {
            struct stat rst;
            const char *leaf = strrchr(recent[i], '/');
            const char *rows[8];
            char label[TIKU_TRK_PATH_MAX];
            int r;

            if (stat(recent[i], &rst) != 0) {
                continue;       /* a destination that went away */
            }
            /* Two recents can share a leaf name; the one that does gains
             * the minimum distinguishing path (NeXT UIG ch8). */
            for (r = 0; r < nrecent; r++) {
                rows[r] = recent[r];
            }
            (void)tiku_trk_attr_disambiguate(recent[i], rows, nrecent,
                                             label, sizeof label);
            if (strcmp(recent[i], desk) == 0 ||
                (home != NULL && strcmp(recent[i], home) == 0)) {
                /* Desktop and Home keep their own NAMED rows below; a
                 * recent that is one of them would shadow the name. */
                continue;
            }
            (void)leaf;
            picker_add(app, k, label, recent[i],
                       have_here && rst.st_dev == here.st_dev);
        }
        {
            struct stat dst;

            if (stat(desk, &dst) == 0) {
                picker_add(app, k, "Desktop", desk,
                           have_here && dst.st_dev == here.st_dev);
            }
            if (home != NULL && stat(home, &dst) == 0) {
                picker_add(app, k, "Home", home,
                           have_here && dst.st_dev == here.st_dev);
            }
        }
        for (i = 0; i < nvol; i++) {
            struct stat vst;

            if (!tiku_trk_volume_shows_icon(&vs, &vs.v[i]) ||
                stat(vs.v[i].mount, &vst) != 0) {
                continue;
            }
            picker_add(app, k, vs.v[i].name, vs.v[i].mount,
                       have_here && vst.st_dev == here.st_dev);
        }
        if (k < 2 && app->menus.picker[k].count > 0) {
            /* Somewhere deeper than a row can name: the panel is the
             * navigator (AW-050), reached from inside the picker. */
            (void)tiku_trk_menu_add_separator(&app->menus.picker[k]);
            (void)tiku_trk_menu_add(&app->menus.picker[k],
                "Choose folder\xe2\x80\xa6",
                (k == 0) ? TIKU_TRK_CMD_MOVE_TO : TIKU_TRK_CMD_COPY_TO,
                0, 0u, 1);
        }
    }
}

/**
 * @brief Fill the Open-with submenu for what @p fl has selected (AW-040).
 *
 * Rebuilt per menu open rather than cached: the answer depends on the
 * selection, and a list that outlived the selection would offer an
 * application for something else (PVL-095, PVN-035).
 */
static void
build_open_with(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_menu_build_open_with(&app->folder_menu, fl);
    build_pickers(app, fl);
    if (fl != NULL && fl->view != NULL) {
        const char *types[TIKU_TRK_APP_TYPES_MAX];
        int n = tiku_trk_poseview_types_present(fl->view, types,
                                                TIKU_TRK_APP_TYPES_MAX);
        (void)tiku_trk_addons_build_menu(&app->addons, types, n,
                                         &app->menus.addons,
                                         TIKU_TRK_ADDON_COMMAND_BASE);
    } else {
        (void)tiku_trk_addons_build_menu(&app->addons, NULL, 0,
                                         &app->menus.addons,
                                         TIKU_TRK_ADDON_COMMAND_BASE);
    }
}

/**
 * @brief Hand @p paths to an application, and say what happened (AW-039).
 *
 * One place, because the Open-with menu, a double click on a document and
 * a drop on an application are the same act with three ways in -- and a
 * launch that reported differently depending on how it was asked for would
 * be three behaviours wearing one name.
 */
static int
launch_paths(desktop_t *app, const tiku_trk_app_t *application,
             const char *const *paths, int count)
{
    return tiku_trk_launch_controller_run(&app->launcher, application, paths,
                                          count);
}

/** @brief The application a document should open in, or NULL (AW-030). */
static const tiku_trk_app_t *
handler_for(desktop_t *app, const tiku_trk_model_t *model)
{
    return tiku_trk_launch_controller_handler(&app->launcher, model);
}

/**
 * @brief Fill an Info window's "Opens with" row from the registry (AW-071).
 *
 * The user's preferred handler when one is set, the best candidate when
 * not, and "Default application" when the choice is the ranking's own.
 */
static void
info_opens_with_fill(desktop_t *app, tiku_trk_info_t *info)
{
    const char *pref;

    info->opens_with[0] = '\0';
    if (info == NULL || tiku_trk_model_is_container(&info->model) ||
        (info->model.facts.perm & TIKU_TRK_P_EXEC) != 0u) {
        return;
    }
    char prefbuf[48];

    pref = tiku_trk_launch_preferred(app->store, info->model.type, prefbuf,
                                     sizeof prefbuf);
    if (pref != NULL && pref[0] != '\0') {
        snprintf(info->opens_with, sizeof info->opens_with, "%s", pref);
    } else {
        snprintf(info->opens_with, sizeof info->opens_with,
                 "Default application");
    }
}

/** @brief Walk "Opens with" to the next candidate and keep it (AW-071). */
static void
info_opens_with_next(desktop_t *app, tiku_trk_info_t *info)
{
    const char *types[1];
    tiku_trk_cand_t cand[16];
    const char *current;
    int count, i, at = -1;

    types[0] = info->model.type;
    count = tiku_trk_launch_candidates(&app->apps, types, 1, app->store,
                                       cand, 16);
    if (count == 0) {
        snprintf(app->status, sizeof app->status,
                 "no application claims %s", info->model.type);
        return;
    }
    char curbuf[48];

    current = tiku_trk_launch_preferred(app->store, info->model.type,
                                        curbuf, sizeof curbuf);
    for (i = 0; i < count; i++) {
        if (current != NULL && strcmp(cand[i].app->name, current) == 0) {
            at = i;
        }
    }
    if (at + 1 < count) {
        (void)tiku_trk_launch_set_preferred(app->store, info->model.type,
                                            cand[at + 1].app->name);
        snprintf(app->status, sizeof app->status, "%s opens with %s",
                 info->model.name, cand[at + 1].app->name);
    } else {
        /* Past the last candidate the choice returns to the ranking's
         * own: "Default application". */
        (void)tiku_trk_launch_set_preferred(app->store, info->model.type,
                                            "");
        snprintf(app->status, sizeof app->status,
                 "%s opens with the default application",
                 info->model.name);
    }
    (void)tiku_trk_store_flush(app->store);
    info_opens_with_fill(app, info);
}

/** @brief The Open With window's launch: the same one act (AW-039). */
static void
open_with_window_launch(void *context, const tiku_trk_app_t *application,
                        const char *const *paths, int count)
{
    desktop_t *app = context;

    (void)launch_paths(app, application, paths, count);
}


/**
 * @brief Run one scripting line against the shell (PVK-052).
 *
 * The suite resolves what a verb acts on and NAMES it; the shell is the
 * caller that acts -- execute opens exactly as a double-click would, and
 * delete moves to the Trash.  Replies go beside the command file, in-band
 * (PVK-054's rule: reported, never thrown).
 */
static void
script_command_line(desktop_t *app, const char *line, FILE *reply)
{
    char store[8][TIKU_TRK_PATH_MAX];
    tiku_trk_req_t req;
    tiku_trk_rep_t rep;
    folder_t *fl = NULL;
    int i;

    {
        tiku_desk_window_t *fw = tiku_desk_workspace_focused(app->desk);

        fl = folder_of(app, fw);
        if (fl == NULL && app->folder_count > 0) {
            fl = &app->folders[0];
        }
    }
    if (fl == NULL || fl->view == NULL ||
        tiku_trk_script_parse(line, &req, store) != 0) {
        if (reply != NULL) {
            fprintf(reply, "error unhandled\n");
        }
        return;
    }
    memset(&rep, 0, sizeof rep);
    tiku_trk_script_run(fl->view, &req, &rep);
    if (rep.error != TIKU_TRK_ERR_OK) {
        if (reply != NULL) {
            fprintf(reply, "error %d\n", rep.error);
        }
        return;
    }
    if (req.verb == TIKU_TRK_VERB_EXECUTE) {
        for (i = 0; i < rep.nresult; i++) {
            tiku_trk_model_t m;

            if (app->backend->ops->stat(app->backend, rep.result[i],
                                        &m) != 0) {
                continue;
            }
            if (tiku_trk_model_is_container(&m)) {
                (void)open_folder(app, rep.result[i]);
            } else {
                const char *one[1];

                one[0] = rep.result[i];
                (void)launch_paths(app, handler_for(app, &m), one, 1);
            }
        }
    } else if (req.verb == TIKU_TRK_VERB_DELETE) {
        const char *sel[64];
        int n = 0;

        for (i = 0; i < rep.nresult && n < 64; i++) {
            sel[n++] = rep.result[i];
        }
        if (n > 0) {
            (void)tiku_trk_fs_trash(app->fs, sel, n);
        }
    }
    if (reply != NULL) {
        fprintf(reply, "ok %d\n", rep.nresult);
        for (i = 0; i < rep.nresult; i++) {
            fprintf(reply, "%s\n", rep.result[i]);
        }
    }
    app->dirty = 1;
}

/** @brief The command FILE is the transport (PVK-052): read, act, reply. */
static void
script_command_file(desktop_t *app, const char *path)
{
    FILE *f = fopen(path, "r");
    FILE *reply = NULL;
    char rpath[TIKU_TRK_PATH_MAX];
    char line[512];

    if (f == NULL) {
        return;
    }
    snprintf(rpath, sizeof rpath, "%s.reply", path);
    reply = fopen(rpath, "w");
    while (fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');

        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] != '\0' && line[0] != '#') {
            script_command_line(app, line, reply);
        }
    }
    (void)fclose(f);
    if (reply != NULL) {
        (void)fclose(reply);
    }
}

/**
 * @brief The union every volume's Trash makes (PVN-027).
 *
 * Each Trash on the machine contributes its entries; the models keep
 * their REAL paths, so restore and delete act on the file wherever its
 * volume keeps it.
 */
static int
trash_union_models(desktop_t *app, tiku_trk_model_t *out, int max)
{
    char dirs[8][TIKU_TRK_PATH_MAX];
    int ndirs = tiku_trk_fs_trash_dirs(app->fs, dirs, 8);
    int i, n = 0;

    for (i = 0; i < ndirs; i++) {
        n += list_children(app->backend, dirs[i], out + n, max - n);
    }
    return n;
}

/** @brief Re-derive a union Trash window from its contributors. */
static int
trash_union_refresh(desktop_t *app, folder_t *fl)
{
    tiku_trk_model_t *models;
    unsigned nf;
    int n;

    models = calloc(256, sizeof *models);
    if (models == NULL) {
        return 0;
    }
    n = trash_union_models(app, models, 256);
    nf = tiku_trk_poseview_apply_results(fl->view, models, n,
                                         tiku_trk_now());
    free(models);
    return nf != TIKU_TRK_NOTIFY_NOTHING;
}

/** @brief Re-apply the spring menu's enabled states from the live drag. */
static void
spring_nav_states(desktop_t *app)
{
    const char *types[TIKU_TRK_DRAG_TYPES];
    int i;

    for (i = 0; i < app->drag.ntype; i++) {
        types[i] = app->drag.type[i];
    }
    tiku_trk_nav_spring_states(&app->nav,
        (const struct tiku_trk_apps *)&app->apps, types, app->drag.ntype);
}

/**
 * @brief The spring: a drag resting on a folder opens a NAVIGABLE menu of
 *        its contents rather than a window (PVS-051).
 */
static void
spring_open_nav(void *context, const char *path)
{
    desktop_t *app = context;

    tiku_trk_nav_open(&app->nav, app->backend, path);
    app->nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(app),
                                                "HideDotFiles");
    app->nav.folders_first = tiku_trk_prefs_get(prefs_model(app),
                                                "SortFolderNamesFirst");
    app->spring_nav_on = 1;
    app->spring_nav_x = app->drag_target.spring.x +
                        app->drag_target.spring.w;
    app->spring_nav_y = app->drag_target.spring.y;
    app->dirty = 1;
}

/** @brief Close every spring level and put the walk away. */
static void
spring_nav_close(desktop_t *app)
{
    while (tiku_trk_nav_leave(&app->nav)) {
    }
    app->nav.depth = 0;
    app->spring_nav_on = 0;
    app->dirty = 1;
}

/**
 * @brief The frame of spring level @p k, chained the way the draw chains.
 */
static tiku_desk_rect_t
spring_nav_frame(desktop_t *app, int k)
{
    int px = app->spring_nav_x;
    int py = app->spring_nav_y;
    tiku_desk_rect_t r = { 0, 0, 0, 0 };
    int j;

    for (j = 0; j <= k && j < app->nav.depth; j++) {
        tiku_trk_nav_level_t *lv = &app->nav.level[j];

        tiku_trk_nav_place(&app->nav, j, px, py, app->surf->w,
                           app->surf->h, &r.x, &r.y, &r.w, &r.h);
        px = r.x + r.w - 4;
        py = r.y + 2 + ((lv->sel >= 0) ? lv->sel : 0) * TIKU_TRK_NAV_ROW;
    }
    return r;
}

/**
 * @brief Follow the pointer over the spring levels (PVS-051, PVS-054).
 *
 * Hovering a row selects it; a container row opens the next level in
 * place; leaving every level (and the sprung pose) by more than the
 * margin closes the whole thing.
 */
static void
spring_nav_track(desktop_t *app, int x, int y)
{
    int hit_level = -1;
    int hit_item = -1;
    int inside_margin = 0;
    int k;

    for (k = 0; k < app->nav.depth; k++) {
        tiku_desk_rect_t r = spring_nav_frame(app, k);
        int at;

        if (x >= r.x - TIKU_TRK_SPRING_MARGIN &&
            x < r.x + r.w + TIKU_TRK_SPRING_MARGIN &&
            y >= r.y - TIKU_TRK_SPRING_MARGIN &&
            y < r.y + r.h + TIKU_TRK_SPRING_MARGIN) {
            inside_margin = 1;
        }
        at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, x, y);
        if (at >= 0) {
            hit_level = k;
            hit_item = at;
        }
    }
    if (hit_level >= 0) {
        tiku_trk_nav_level_t *lv = &app->nav.level[hit_level];

        if (lv->sel != hit_item) {
            /* A new row: anything deeper hung off the OLD row. */
            while (app->nav.depth > hit_level + 1) {
                (void)tiku_trk_nav_leave(&app->nav);
            }
            lv->sel = hit_item;
            if (lv->item[hit_item].is_container &&
                lv->item[hit_item].enabled) {
                (void)tiku_trk_nav_enter(&app->nav, hit_item);
            }
            app->dirty = 1;
        }
        return;
    }
    /* Off every level: the sprung pose itself keeps it alive too, so the
     * pointer can travel from the pose into the menu (PVS-054). */
    if (!inside_margin &&
        !tiku_trk_springmenu_inside(&app->drag_target.spring, x, y)) {
        spring_nav_close(app);
    }
}

/**
 * @brief What releasing the drag at @p x,@p y over the spring means.
 *
 * @return 1 with @p dest set for an enabled row, 2 for a disabled row
 *         (the release is swallowed), 0 when the menu was not hit.
 */
static int
spring_nav_hit(desktop_t *app, int x, int y, char *dest, size_t max)
{
    int k;

    for (k = app->nav.depth - 1; k >= 0; k--) {
        tiku_desk_rect_t r = spring_nav_frame(app, k);
        int at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, x, y);

        if (at >= 0) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];

            if (!lv->item[at].enabled) {
                return 2;
            }
            snprintf(dest, max, "%s", lv->item[at].path);
            return 1;
        }
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return 2;               /* in the menu, between rows */
        }
    }
    return 0;
}

/** @brief A drop on an application pose is the same one act (PVS-044). */
static int
drop_launch(void *context, const struct tiku_trk_app *application,
            const char *const *paths, int count)
{
    desktop_t *app = context;

    return launch_paths(app, (const tiku_trk_app_t *)application, paths,
                        count);
}

/** @brief Open the selection with the candidate at @p at (AW-040). */
static void
open_with_pick(desktop_t *app, folder_t *fl, int at)
{
    tiku_trk_launch_controller_pick(&app->launcher, fl, at);
}

/** @brief Fill the Attributes menu's per-type submenus. */
static void
build_per_type(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_menu_build_per_type(&app->folder_menu, fl);
}

/** @brief The folder interface shared by Desktop and folder interactions. */
static folder_t *
desktop_folder(desktop_t *app)
{
    return tiku_trk_desktop_view_folder(&app->desktop_view);
}

/**
 * @brief Finish an edit: check the value, then write it.
 *
 * The descriptor is consulted BEFORE the device is: a refusal that names the
 * range or the expected spelling is worth more than the write error the
 * device would return, and it costs the device nothing.
 */
static void
commit_edit(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_edit_commit_value(&app->folder_edit, fl);
}

/**
 * @brief Close a window, persisting what it was showing first.
 *
 * State is written HERE rather than only at exit, so a session that is killed
 * does not lose every arrangement made since it started.
 *
 * Tracker quits when its last window closes, and that rule does NOT carry
 * over: there, the Desktop is a window of its own that outlives the folders.
 * Here the backdrop and the Deskbar ARE the application, so closing the last
 * folder must leave them standing.
 */
/**
 * @brief Write a folder window's frame back, if it moved (CW-001, CW-002).
 *
 * A folder that has GONE keeps no frame, for the same reason it keeps no
 * arrangement: there is nothing left for the position to be the position of.
 */
static void
close_window(desktop_t *app, tiku_desk_window_t *w)
{
    folder_t *fl;
    int info_slot;

    if (w == NULL) {
        return;
    }
    if (w == tiku_trk_panel_window_handle(&app->file_panel)) {
        tiku_trk_panel_window_close(&app->file_panel);
        return;
    }
    if (w == tiku_trk_find_window_handle(&app->find_window)) {
        tiku_trk_find_window_close(&app->find_window);
        return;
    }
    if (w == tiku_trk_deskbar_prefs_window_handle(
                 &app->deskbar_prefs_window)) {
        tiku_trk_deskbar_prefs_window_close(&app->deskbar_prefs_window);
        return;
    }
    info_slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (info_slot >= 0) {
        tiku_trk_info_windows_close(&app->info_windows, info_slot);
        return;
    }
    fl = folder_of(app, w);
    if (fl != NULL) {
        if (app->bar_fl == fl) {
            app->bar_fl = NULL;
            app->bar_open = -1;
            app->menus.bar.open = -1;
            app->popup = NULL;
            app->popup_sub = NULL;
        }
        tiku_trk_folder_lifecycle_close(&app->folder_lifecycle, fl);
        return;
    }
    tiku_desk_workspace_close(app->desk, w);
    app->dirty = 1;
}

/**
 * @brief Close every window showing @p path or anything under it (FS-042).
 *
 * A window onto a folder that has just gone to the Trash is a window onto
 * nothing; leaving it up would let the user keep working in a folder whose
 * every path has changed underneath them.
 */
/**
 * @brief Let queued file work finish before the process goes.
 *
 * Bounded, because a job that cannot finish must not hold the application
 * open forever: the original gives up after five seconds and so does this.
 * What is already done stays done either way -- the engine has no
 * transaction above one item.
 */
/**
 * @brief The character a key event types, or 0.
 *
 * With no display there is no keyboard map to consult, so a Latin-1 keysym
 * stands for itself -- which is all a script needs in order to type.
 */
static char
key_char(const tiku_desk_event_t *event)
{
    return (event != NULL) ? event->text[0] : (char)0;
}

/** @brief Name a portable key for controllers whose API uses key labels. */
static const char *
key_name(const tiku_desk_event_t *event, char printable[2])
{
    if (event == NULL) { return ""; }
    switch (event->key) {
    case TIKU_DESK_KEY_UP:     return "Up";
    case TIKU_DESK_KEY_DOWN:   return "Down";
    case TIKU_DESK_KEY_LEFT:   return "Left";
    case TIKU_DESK_KEY_RIGHT:  return "Right";
    case TIKU_DESK_KEY_ESCAPE: return "Escape";
    case TIKU_DESK_KEY_RETURN: return "Return";
    case TIKU_DESK_KEY_TAB:    return "Tab";
    default:
        if (event->key == ' ') { return "space"; }
        if (event->key >= 0x20u && event->key < 0x7fu) {
            printable[0] = (char)event->key;
            printable[1] = '\0';
            return printable;
        }
        return "";
    }
}

/**
 * @brief A drop landing anywhere on the file panel (AW-059).
 *
 * Exactly one carried item navigates the panel; any other count is
 * swallowed, exactly as the original's MessageDropFilter swallows it.
 * @return nonzero when the drop was the panel's, handled or swallowed.
 */
static int
drop_on_panel(desktop_t *app, int x, int y)
{
    tiku_desk_window_t *w = NULL;

    (void)tiku_desk_workspace_hit(app->desk, x, y, &w);
    if (w == NULL || w != tiku_trk_panel_window_handle(&app->file_panel)) {
        return 0;
    }
    if (app->drag.count == 1) {
        tiku_trk_panel_window_drop(&app->file_panel, app->drag.path[0]);
    }
    app->dirty = 1;
    return 1;
}

/**
 * @brief The Info window icon under the pointer that would take a drop.
 *
 * The icon stands for the window's own item, so it is a drop target
 * exactly when that item is a container the store can write (AW-075).
 * @return the slot, or -1.
 */
static int
info_icon_drop_slot(desktop_t *app, int x, int y)
{
    tiku_desk_window_t *w = NULL;
    tiku_trk_info_t *info;
    tiku_trk_model_t m;
    int slot;

    (void)tiku_desk_workspace_hit(app->desk, x, y, &w);
    slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (slot < 0 ||
        tiku_trk_info_windows_hit(&app->info_windows, slot, x, y, NULL) !=
            TIKU_TRK_INFO_HIT_ICON) {
        return -1;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, slot);
    if (app->backend->ops->stat(app->backend, info->path, &m) != 0 ||
        !tiku_trk_model_is_mutable_container(&m)) {
        return -1;
    }
    return slot;
}

/** @brief Drop the drag on an Info window's icon.  @return handled. */
static int
drop_on_info_icon(desktop_t *app, int x, int y, unsigned modifiers)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    tiku_trk_info_t *info;
    unsigned drag_modifiers = 0u;
    tiku_trk_drop_t action;
    int slot = info_icon_drop_slot(app, x, y);
    int same_store, i, count = 0;

    if (slot < 0 || app->drag.count == 0) {
        return 0;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, slot);
    if ((modifiers & TIKU_DESK_MOD_OPTION) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_COPY;
    }
    if ((modifiers & TIKU_DESK_MOD_CMD) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_LINK;
    }
    same_store = tiku_trk_fs_drop_action(app->fs, app->drag.path[0],
                                         info->path, 0) == TIKU_TRK_OP_MOVE;
    action = tiku_trk_drag_action(&app->drag, info->path, drag_modifiers,
                                  same_store);
    if (action == TIKU_TRK_DROP_NOTHING) {
        return 1;               /* over the icon, but nothing to do */
    }
    for (i = 0; i < app->drag.count; i++) {
        paths[count++] = app->drag.path[i];
    }
    snprintf(app->prog_dest, sizeof app->prog_dest, "%s", info->path);
    switch (action) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_trk_fs_copy(app->fs, paths, count, info->path,
                               TIKU_TRK_CONFLICT_ASK);
        break;
    case TIKU_TRK_DROP_LINK:
        (void)tiku_trk_fs_link(app->fs, paths, count, info->path,
                               (modifiers & TIKU_DESK_MOD_SHIFT) != 0u);
        break;
    default:
        (void)tiku_trk_fs_move(app->fs, paths, count, info->path,
                               TIKU_TRK_CONFLICT_ASK);
        break;
    }
    snprintf(app->status, sizeof app->status,
             "dropped %d item%s on %s", count, count == 1 ? "" : "s",
             info->model.name);
    return 1;
}

/**
 * @brief Resolve and perform the drop the pointer is over.
 *
 * The destination is the highlighted pose when there is one and the hovered
 * window's own folder otherwise -- which is also what a drop onto one of the
 * dragged poses means, since such a pose never highlights (PVS-042).
 */
static void
do_drop(desktop_t *app, int x, int y, unsigned state, unsigned button)
{
    tiku_trk_drop_controller_execute(&app->drop_controller, x, y, state,
                                     button);
}

static void
close_windows_under(desktop_t *app, const char *path)
{
    size_t n;
    int i;

    if (app == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    n = strlen(path);
    /* Backwards, because closing compacts the array. */
    for (i = app->folder_count - 1; i >= 0; i--) {
        const char *p = app->folders[i].path;

        /* The folder itself, or a folder INSIDE it -- matched on a whole
         * component so a sibling sharing the same prefix is left alone. */
        if (strncmp(p, path, n) == 0 && (p[n] == '\0' || p[n] == '/')) {
            close_window(app, app->folders[i].win);
        }
    }
}

/** @brief Collect a directory's entries into @p out.  @return how many. */
struct collect_ctx { tiku_trk_model_t *out; int n, max; };

static int
collect_one(const tiku_trk_model_t *m, void *ctx)
{
    struct collect_ctx *c = ctx;

    if (c->n >= c->max) {
        return 1;
    }
    c->out[c->n++] = *m;
    return 0;
}

static int
list_children(tiku_trk_backend_t *b, const char *path, tiku_trk_model_t *out,
              int max)
{
    struct collect_ctx c = { out, 0, max };

    if (b == NULL || b->ops->list == NULL) {
        return 0;
    }
    (void)b->ops->list(b, path, collect_one, &c);
    return c.n;
}

/**
 * @brief Adopt a result set into a window, live.
 *
 * The run is handed over rather than copied from and dropped: its lifetime is
 * the subscription's lifetime.
 */
static folder_t *
open_results(desktop_t *app, const char *title, tiku_trk_qrun_t *run)
{
    return tiku_trk_folder_lifecycle_open_results(&app->folder_lifecycle,
                                                   title, run);
}

/** @brief Re-run a live window's query and apply what changed. */
static int
refresh_results(desktop_t *app, folder_t *fl)
{
    return tiku_trk_folder_lifecycle_refresh_results(
        &app->folder_lifecycle, fl);
}



/**
 * @brief Start the inline rename the slow-second-click timer armed.
 *
 * A namespace node has no name of its own to change; refused here rather
 * than letting an editor open over something the device will not rename.
 */
static void
begin_rename(desktop_t *app, folder_t *fl, int row)
{
    tiku_trk_folder_edit_begin_rename(&app->folder_edit, fl, row);
}

/** @brief Finish the rename, keeping or discarding what was typed. */
static void
commit_rename(desktop_t *app, int commit)
{
    tiku_trk_folder_edit_finish_rename(&app->folder_edit, commit);
}

/** @brief Apply a Select controller expression to its originating folder. */
static tiku_desk_window_t *
apply_select_model(void *context, void *target,
                   const tiku_trk_select_t *selection)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char error[96];
    int count;

    if (folder == NULL || folder->win == NULL || !folder->win->open) {
        snprintf(app->status, sizeof app->status,
                 "the window this was opened from has gone");
        return NULL;
    }
    count = tiku_trk_poseview_select_matching(folder->view,
        selection->expr.text, tiku_trk_select_rule(selection->rule),
        selection->ignore_case, selection->invert, error, sizeof error);
    if (count < 0) {
        /* A bad expression is a stop alert, closeable with Escape, and it
         * selects nothing (PVK-047). */
        tiku_trk_alert_open(alert_model(app), TIKU_TRK_ALERT_STOP,
                            TIKU_TRK_ALERT_TAG_REFUSAL, error, "Cancel");
        return NULL;
    }
    snprintf(app->status, sizeof app->status, "selected %d item%s", count,
             count == 1 ? "" : "s");
    return folder->win;
}

/** @brief Open the Find panel, or bring it forward. */
static void
open_panel(desktop_t *app)
{
    tiku_trk_find_window_open(&app->find_window);
}

/** @brief Run what the panel is asking and show the results. */
static int
panel_search(void *context, tiku_trk_find_t *panel)
{
    desktop_t *app = context;
    tiku_trk_query_t q;
    tiku_trk_qrun_t *run;
    char title[TIKU_TRK_PATH_MAX];
    char scope[64];
    char record_name[TIKU_TRK_NAME_MAX];
    int replacing;

    tiku_trk_find_build(panel, &q);
    if (q.invalid) {
        snprintf(app->status, sizeof app->status,
                 "invalid query formula");
        app->dirty = 1;
        return 0;
    }
    /* The checkbox stores the choice; the actual Trash path is resolved at
     * run time because it belongs to this machine and this volume, not to a
     * portable saved-query record (Q-036). */
    (void)tiku_trk_fs_trash_dir(app->fs, desktop_folder(app)->path,
                                q.trash_path,
                                sizeof q.trash_path);
    tiku_trk_query_bind_scopes(&q, app->backend);
    run = tiku_trk_query_start(app->backend, &q);
    if (run == NULL) {
        return 0;
    }
    while (!tiku_trk_query_done(run)) {
        if (tiku_trk_query_step(run, 16) < 0) {
            break;
        }
    }
    (void)tiku_trk_find_scope_label(panel, scope, sizeof scope);
    replacing = panel->loaded_kind == TIKU_TRK_QREC_QUERY &&
                panel->loaded_name[0] != '\0';
    record_name[0] = '\0';
    {
        /* Every run leaves a record, marked TEMPORARY: a search is a thing
         * that can be re-opened and edited, not only a window that appears
         * -- and one the user never named is swept up later rather than
         * accumulating for ever (Q-023, MA-053). */
        tiku_trk_query_t previous;
        int i;

        q.temporary = 1;
        if (replacing) {
            snprintf(record_name, sizeof record_name, "%s",
                     panel->loaded_name);
            if (tiku_trk_query_load(&previous, record_name, app->store) ==
                    0) {
                q.temporary = previous.temporary;
            }
            /* The old window owns the layout being inherited.  Close it
             * first so its frame and columns reach the store before the
             * replacement reads them (Q-025). */
            for (i = 0; i < app->folder_count; i++) {
                if (app->folders[i].win != NULL &&
                    app->folders[i].win->open &&
                    strcmp(app->folders[i].qname, record_name) == 0) {
                    close_window(app, app->folders[i].win);
                }
            }
        }
        /* The WALL clock, not the running one: how old a record is has to
         * mean the same thing after a restart, and it is also the date its
         * name carries. */
        q.saved_at = (int64_t)time(NULL);
        if (record_name[0] != '\0' ||
            tiku_trk_find_default_name(panel, (long)q.saved_at,
                                       record_name,
                                       sizeof record_name) > 0) {
            (void)tiku_trk_query_save(&q, record_name, app->store);
            snprintf(app->last_query, sizeof app->last_query, "%s",
                     record_name);
        }
    }
    snprintf(title, sizeof title, "find #%d", ++app->find_serial);
    {
        folder_t *fl = open_results(app, title, run);

        if (fl != NULL) {
            snprintf(fl->qname, sizeof fl->qname, "%s", app->last_query);
            snprintf(app->status, sizeof app->status,
                     "%d result%s in %.30s -- live",
                     tiku_trk_query_count(fl->run),
                     (tiku_trk_query_count(fl->run) == 1) ? "" : "s", scope);
        } else {
            return 0;
        }
    }
    app->dirty = 1;
    return 1;
}

/**
 * @brief Run the typed search and open its results in their own window.
 *
 * The scope is the folder the search started from, which is what makes a
 * search from /dev mean "in /dev" without a target menu to set it in.
 */
static void
run_find(desktop_t *app, folder_t *from)
{
    tiku_trk_query_t q;
    tiku_trk_qrun_t *run;
    char title[TIKU_TRK_PATH_MAX];
    folder_t *fl;

    tiku_trk_query_init(&q);
    tiku_trk_query_set_name(&q, app->find_buf);
    if (from != NULL) {
        (void)tiku_trk_query_add_scope(&q, from->path);
    }
    (void)tiku_trk_fs_trash_dir(app->fs,
                                (from != NULL) ? from->path
                                               : desktop_folder(app)->path,
                                q.trash_path, sizeof q.trash_path);
    run = tiku_trk_query_start(app->backend, &q);
    if (run == NULL) {
        return;
    }
    while (!tiku_trk_query_done(run)) {
        if (tiku_trk_query_step(run, 8) < 0) {
            break;
        }
    }
    /* The window is named for the search, not for a place: two searches for
     * different things must not collapse onto one window. */
    snprintf(title, sizeof title, "find:%.40s#%d", app->find_buf,
             ++app->find_serial);
    fl = open_folder(app, title);
    if (fl != NULL) {
        int i, n = tiku_trk_query_count(run);
        tiku_trk_model_t *set = calloc((size_t)(n > 0 ? n : 1), sizeof *set);

        if (set != NULL) {
            for (i = 0; i < n; i++) {
                set[i] = *tiku_trk_query_at(run, i);
            }
            (void)tiku_trk_poseview_set_results(fl->view, set, n);
            free(set);
        }
        snprintf(app->status, sizeof app->status,
                 "%d result%s for \"%.30s\" in %.40s", n, (n == 1) ? "" : "s",
                 app->find_buf, (from != NULL) ? from->path : "/");
    }
    tiku_trk_query_stop(run);
    app->dirty = 1;
}

/** @brief Begin editing the active pose's value, if it has one to edit. */
static void
begin_edit(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_edit_begin_value(&app->folder_edit, fl);
}

/**
 * @brief The selected rows' paths, for an operation that acts on them.
 *
 * @return How many were collected.
 */
static int
folder_selection(desktop_t *app, folder_t *fl,
                 char paths[][TIKU_TRK_PATH_MAX], const char **out, int max)
{
    (void)app;
    return tiku_trk_folder_file_actions_selection(fl, paths, out, max);
}

/*---------------------------------------------------------------------------*/
/* The Open/Save panel (AW-050..AW-064)                                       */
/*---------------------------------------------------------------------------*/

/** @brief Apply the destination panel answer to its opaque folder target. */
static void
fpanel_commit(void *context, void *target, int command,
              const char *destination)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char paths[16][TIKU_TRK_PATH_MAX];
    const char *selection[16];
    int count;

    if (command == TIKU_TRK_CMD_ADD_SCOPE && destination != NULL) {
        /* The chosen folder becomes a place and the search is scoped to
         * it at once (Q-019). */
        if (tiku_trk_find_scope_to_folder(&app->find_window.model,
                                          destination)) {
            snprintf(app->status, sizeof app->status,
                     "scoped to %.100s", destination);
        }
        return;
    }
    if (command == TIKU_TRK_CMD_SAVE_QUERY && destination != NULL) {
        /* The panel already checked the destination (AW-052); what is
         * left is the writing: the query's saved text as a real file,
         * and the same record under the file's name so the Queries menu
         * knows it too. */
        char text[TIKU_TRK_QUERY_PRED_MAX * 2];
        char err[160];
        const char *base = strrchr(destination, '/');
        int n = tiku_trk_query_serialise(&app->pending_query, text,
                                         sizeof text);

        if (n < 0 || app->backend->ops->write == NULL ||
            app->backend->ops->write(app->backend, destination, text,
                                     (size_t)n, err, sizeof err) != 0) {
            snprintf(app->status, sizeof app->status,
                     "could not save the query");
            return;
        }
        (void)tiku_trk_query_save(&app->pending_query,
                                  base != NULL ? base + 1 : destination,
                                  app->store);
        (void)tiku_trk_store_flush(app->store);
        snprintf(app->status, sizeof app->status, "query saved");
        return;
    }
    if (folder == NULL || destination == NULL) {
        return;
    }
    count = folder_selection(app, folder, paths, selection, 16);
    if (count > 0) {
        if (command == TIKU_TRK_CMD_COPY_TO) {
            (void)tiku_trk_fs_copy(app->fs, selection, count, destination,
                                   TIKU_TRK_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status,
                     "copying %d item%s", count, count == 1 ? "" : "s");
        } else {
            (void)tiku_trk_fs_move(app->fs, selection, count, destination,
                                   TIKU_TRK_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status,
                     "moving %d item%s", count, count == 1 ? "" : "s");
        }
        tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS,
                            destination);
    }
}

/** @brief Find's 'Select folders...': the panel chooses a scope (Q-019). */
static void
find_select_folder(void *context)
{
    desktop_t *app = context;

    tiku_trk_panel_window_open(&app->file_panel, app,
                               TIKU_TRK_CMD_ADD_SCOPE, "/");
}

/** @brief Find's Save with nowhere attached: ask WHERE with the panel. */
static void
find_save_as(void *context)
{
    desktop_t *app = context;
    const char *home = getenv("HOME");

    tiku_trk_find_build(&app->find_window.model, &app->pending_query);
    tiku_trk_panel_window_open_save(&app->file_panel, app,
                                    TIKU_TRK_CMD_SAVE_QUERY,
                                    (home != NULL && home[0] != '\0')
                                        ? home : "/",
                                    "query");
}

/* The argv the desktop was started with, for Restart system (DB-006). */
static char **restart_argv;

/**
 * @brief Carry out what a Deskbar click asked for.
 *
 * Shared by the press path and the armed-dock release path: a CMD press
 * on the bar might be the start of a re-dock drag, so its CLICK meaning
 * is delivered only if the press never moved (DB-049 vs DB-019/021).
 */
static void
deskbar_pick_dispatch(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y)
{
    tiku_desk_window_t *t;

    switch (pick->what) {
    case TIKU_TRK_DB_FIND:
        /* The leaf's Find item is the panel: the same
         * entry point as Ctrl+F, reachable by mouse. */
        open_panel(app);
        break;
    case TIKU_TRK_DB_PREFERENCES:
        tiku_trk_deskbar_prefs_window_open(
            &app->deskbar_prefs_window);
        break;
    case TIKU_TRK_DB_TRACKER_PREFERENCES:
        tiku_trk_prefs_window_open(&app->prefs_window);
        break;
    case TIKU_TRK_DB_QUIT:
        /* The ellipsis is a promise: Shut down… asks
         * before anything is taken down (DB-006). */
        tiku_trk_alert_open(alert_model(app),
            TIKU_TRK_ALERT_WARN,
            TIKU_TRK_ALERT_TAG_SHUTDOWN,
            "Shut down the desktop?",
            "Cancel|Shut down");
        break;
    case TIKU_TRK_DB_OPEN_PATH:
        (void)open_folder(app, pick->path);
        break;
    case TIKU_TRK_DB_ACTIVATE_WINDOW:
        /* Invoke is a no-op while dragging: the drop
         * owns the release, and a window that raised
         * itself under it would steal the target
         * (DB-030). */
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_desk_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            /* The zoom animates only when the state
             * really changes (DB-030). */
            if (t->minimized) {
                app->zoom_t0 = tiku_trk_now();
                app->zoom_from = (tiku_desk_rect_t){
                    x - 8, y - 8, 16, 16 };
                app->zoom_to = t->frame;
            }
            t->minimized = 0;
            tiku_desk_workspace_activate(app->desk, t);
        }
        break;
    case TIKU_TRK_DB_MINIMIZE_WINDOW:
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_desk_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            if (!t->minimized) {
                app->zoom_t0 = tiku_trk_now();
                app->zoom_from = t->frame;
                app->zoom_to = (tiku_desk_rect_t){
                    x - 8, y - 8, 16, 16 };
            }
            t->minimized = 1;
        }
        break;
    case TIKU_TRK_DB_CLOSE_WINDOW:
        t = tiku_desk_workspace_find(app->desk, pick->tag);
        close_window(app, t);
        break;
    case TIKU_TRK_DB_ACTIVATE_TEAM: {
        /* Activating the application: its windows to
         * the front, the top one focused (DB-019). */
        int k, m = tiku_desk_workspace_count(app->desk);

        for (k = 0; k < m; k++) {
            tiku_desk_window_t *ww =
                tiku_desk_workspace_at(app->desk, k);
            if (ww != NULL) {
                ww->minimized = 0;
            }
        }
        if (m > 0) {
            tiku_desk_workspace_activate(app->desk,
                tiku_desk_workspace_at(app->desk,
                                       m - 1));
        }
        break;
    }
    case TIKU_TRK_DB_LAUNCH_TEAM: {
        /* Middle click: another instance of the same
         * image (DB-021). */
        const char *cmd =
            tiku_trk_deskbar_team_command(app->deskbar,
                                          pick->team);

        if (cmd[0] != '\0') {
            pid_t child = fork();

            if (child == 0) {
                execlp(cmd, cmd, (char *)NULL);
                _exit(127);
            }
            if (child > 0) {
                (void)waitpid(child, NULL, WNOHANG);
            }
        }
        break;
    }
    case TIKU_TRK_DB_POWER_OFF:
        /* Chosen inside the submenu: immediate, no question -- the
         * parent's ellipsis is where the asking lives (DB-006). */
        app->running = 0;
        break;
    case TIKU_TRK_DB_RESTART:
        /* A real restart of the desktop: the same image, started
         * over. */
        execv("/proc/self/exe", restart_argv);
        app->running = 0;       /* exec refused: end instead */
        break;
    case TIKU_TRK_DB_QUIT_TEAM: {
        /* Politely: SIGTERM, the process's chance to
         * put its affairs in order (DB-028). */
        long tp[16];
        int k, m = tiku_trk_deskbar_team_pids(
            app->deskbar, pick->team, tp, 16);

        for (k = 0; k < m; k++) {
            (void)kill((pid_t)tp[k], SIGTERM);
        }
        break;
    }
    case TIKU_TRK_DB_KILL_TEAM: {
        /* The Vulcan Death Grip: no quit request,
         * and the row goes without waiting for the
         * roster (DB-020). */
        long tp[16];
        int k, m = tiku_trk_deskbar_team_pids(
            app->deskbar, pick->team, tp, 16);

        for (k = 0; k < m; k++) {
            (void)kill((pid_t)tp[k], SIGKILL);
        }
        tiku_trk_deskbar_remove_team(app->deskbar,
                                     pick->team);
        break;
    }
    case TIKU_TRK_DB_HIDE_ALL:
    case TIKU_TRK_DB_SHOW_ALL: {
        int k, m = tiku_desk_workspace_count(app->desk);
        for (k = 0; k < m; k++) {
            tiku_desk_window_t *ww =
                tiku_desk_workspace_at(app->desk, k);
            if (ww != NULL) {
                ww->minimized =
                    (pick->what == TIKU_TRK_DB_HIDE_ALL);
            }
        }
        break;
    }
    case TIKU_TRK_DB_CLOSE_ALL: {
        /* Closing every window of the file manager is a
         * request to close its WINDOWS, never to quit
         * it -- the original makes the same
         * distinction. */
        while (tiku_desk_workspace_count(app->desk) > 0) {
            tiku_desk_workspace_close(app->desk,
                tiku_desk_workspace_at(app->desk, 0));
        }
        break;
    }
    case TIKU_TRK_DB_CLOCK_HIDE:
    case TIKU_TRK_DB_CLOCK_SHOW:
        app->deskbar_prefs_window.model.value.show_clock =
            (pick->what == TIKU_TRK_DB_CLOCK_SHOW);
        tiku_trk_deskbar_settings_normalize(
            &app->deskbar_prefs_window.model.value);
        tiku_trk_deskbar_apply_settings(app->deskbar,
            &app->deskbar_prefs_window.model.value);
        (void)tiku_trk_deskbar_prefs_save(
            &app->deskbar_prefs_window.model, app->store);
        break;
    case TIKU_TRK_DB_CLOCK_SECONDS:
    case TIKU_TRK_DB_CLOCK_DAY:
    case TIKU_TRK_DB_CLOCK_ZONE:
        if (pick->what == TIKU_TRK_DB_CLOCK_SECONDS) {
            app->deskbar_prefs_window.model.value.show_seconds =
                !app->deskbar_prefs_window.model.value.show_seconds;
        } else if (pick->what == TIKU_TRK_DB_CLOCK_DAY) {
            app->deskbar_prefs_window.model.value.show_day_of_week =
                !app->deskbar_prefs_window.model.value.show_day_of_week;
        } else {
            app->deskbar_prefs_window.model.value.show_timezone =
                !app->deskbar_prefs_window.model.value.show_timezone;
        }
        tiku_trk_deskbar_settings_normalize(
            &app->deskbar_prefs_window.model.value);
        tiku_trk_deskbar_apply_settings(app->deskbar,
            &app->deskbar_prefs_window.model.value);
        (void)tiku_trk_deskbar_prefs_save(
            &app->deskbar_prefs_window.model, app->store);
        break;
    default:
        break;
    }
}

/** @brief The roster's heartbeat: sync teams with the live processes. */
static int
rescan_teams(void *context)
{
    desktop_t *app = context;

    if (tiku_trk_deskbar_scan_processes(app->deskbar)) {
        app->dirty = 1;
    }
    /* The tray rides the same beat: a subscription whose file moved is
     * re-pointed, one whose file went is removed (DB-040). */
    if (tiku_trk_deskbar_sync_replicants(app->deskbar)) {
        app->dirty = 1;
    }
    return 0;                   /* keep the beat */
}

/** @brief Ask the reusable panel where an operation should put its items. */
static void
fpanel_open(desktop_t *app, folder_t *folder, int command)
{
    if (folder != NULL) {
        tiku_trk_panel_window_open(&app->file_panel, folder, command,
                                   folder->path);
    }
}

/**
 * @brief Select the row called @p name in @p fl, and scroll it into view.
 *
 * What "open the parent WITH the item selected" means: landing on the
 * folder with nothing picked would leave the user to find it again.
 */
static int
select_named(desktop_t *app, folder_t *fl, const char *name)
{
    int i, n;

    if (fl == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    n = tiku_trk_poseview_count(fl->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);

        if (p != NULL && strcmp(p->model.name, name) == 0) {
            tiku_trk_poseview_click(fl->view, i, 0u);
            /* Into view, minimally: the application changed the selection,
             * so it keeps it visible (NeXT UIG ch3) -- but a row already
             * on screen must not jump to the top. */
            tiku_trk_poseview_scroll_into_view(fl->view, i,
                tiku_trk_poseview_visible_rows(fl->view));
            app->dirty = 1;
            return i;
        }
    }
    return -1;
}

static int
invoke_addon(const tiku_trk_addon_t *addon, void *context)
{
    desktop_t *app = context;

    if (addon == NULL || app == NULL) {
        return 0;
    }
    if (addon->external) {
        /* Run against the selection, in a process of its own -- the
         * port's separate thread (CW-038). */
        char paths[64][TIKU_TRK_PATH_MAX];
        const char *sel[64];
        folder_t *fl = folder_of(app,
                                 tiku_desk_workspace_focused(app->desk));
        int n;

        if (fl == NULL) {
            fl = desktop_folder(app);
        }
        n = tiku_trk_folder_file_actions_selection(fl, paths, sel, 64);
        if (tiku_trk_addons_run_external(addon, sel, n) == 0) {
            snprintf(app->status, sizeof app->status,
                     "running %.48s on %d item%s", addon->name, n,
                     (n == 1) ? "" : "s");
        } else {
            snprintf(app->status, sizeof app->status,
                     "could not run %.60s", addon->name);
        }
        return 1;
    }
    snprintf(app->status, sizeof app->status, "add-on requested: %.80s",
             addon->name);
    return 1;
}

/**
 * @brief Point a new entry at where it was asked for (PVL-025, PVL-098).
 *
 * The menu's own corner is where it was opened, which is the click that
 * asked for the folder; a keyboard shortcut has no menu, so the pointer
 * answers instead, and only while it is over the window.
 */
static int
seed_new_entry(desktop_t *app, folder_t *fl, int *vx, int *vy)
{
    tiku_desk_rect_t c;
    int ox = 0, oy = 0, x, y;

    if (fl == NULL || fl->view == NULL ||
        tiku_trk_poseview_mode(fl->view) == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (app->popup != NULL) {
        x = app->popup_frame.x;
        y = app->popup_frame.y;
    } else {
        x = app->ptr_x;
        y = app->ptr_y;
    }
    c = tiku_trk_folder_window_content(fl);
    if (x < c.x || x >= c.x + c.w || y < c.y || y >= c.y + c.h) {
        return 0;
    }
    tiku_trk_poseview_origin(fl->view, &ox, &oy);
    *vx = x - (c.x + 2) + ox;
    *vy = y - (c.y + 2) + oy;
    return 1;
}

/** @brief Give the entry just made the place it was asked for. */
static void
land_new_entry(desktop_t *app, folder_t *fl, const char *made, int seeded,
               int vx, int vy)
{
    const char *slash = (made != NULL) ? strrchr(made, '/') : NULL;

    if (!seeded || fl == NULL || fl->view == NULL) {
        return;
    }
    (void)app;
    tiku_trk_poseview_drop_land(fl->view,
                                (slash != NULL) ? slash + 1 : made, vx, vy);
}

/** @brief Carry out a menu choice on the focused folder. */
static void
menu_invoke(desktop_t *app, folder_t *fl, int command)
{
    const char *template_path;
    const char *template_parent;
    int seeded = 0, seed_x = 0, seed_y = 0;

    if (fl == NULL) {
        return;
    }
    {
        /* The one enablement the menus use gates EVERY route here: a
         * chord for a command the menu would show grey answers out loud
         * instead of being silently swallowed (NeXT UIG ch6). */
        tiku_trk_sc_ctx_t gc = menu_ctx(app, fl, 0u);

        /* Three commands are exempt because their handlers carry WIDER
         * semantics than the menu rule and say their own refusals:
         * Unmount acts on the volume being SHOWN when nothing is
         * selected (AW-096), and the trash/delete pair routes a selected
         * VOLUME to an unmount with its own boot-volume refusal
         * (FS-041). */
        if (command != TIKU_TRK_CMD_UNMOUNT &&
            command != TIKU_TRK_CMD_MOVE_TO_TRASH &&
            command != TIKU_TRK_CMD_DELETE &&
            !tiku_trk_sc_enabled(command, &gc)) {
            snprintf(app->status, sizeof app->status,
                     "%s is not available here",
                     tiku_trk_sc_label(command, 0u));
            *&app->dirty = 1;
            return;
        }
    }
    if (command == TIKU_TRK_CMD_CREATE_QUERY ||
        command == TIKU_TRK_CMD_CREATE_QUERY_TPL) {
        /* The Find icon's drop chose (Q-034): the current search becomes
         * a record in the folder it was dropped on, under the first free
         * "New Query" name, as a query or as a template. */
        char dest[TIKU_TRK_PATH_MAX];
        tiku_trk_model_t probe;
        int k;

        if (app->qdrop_dest[0] == '\0') {
            return;
        }
        tiku_trk_find_build(&app->find_window.model, &app->pending_query);
        app->pending_query.kind =
            (command == TIKU_TRK_CMD_CREATE_QUERY_TPL)
                ? TIKU_TRK_QREC_TEMPLATE : TIKU_TRK_QREC_QUERY;
        for (k = 0; k < 100; k++) {
            if (k == 0) {
                snprintf(dest, sizeof dest, "%s/New Query",
                         app->qdrop_dest);
            } else {
                snprintf(dest, sizeof dest, "%s/New Query %d",
                         app->qdrop_dest, k + 1);
            }
            if (app->backend->ops->stat(app->backend, dest, &probe) != 0) {
                break;
            }
        }
        fpanel_commit(app, fl, TIKU_TRK_CMD_SAVE_QUERY, dest);
        app->qdrop_dest[0] = '\0';
        return;
    }
    if (command >= TIKU_TRK_CMD_DEST_BASE &&
        command < TIKU_TRK_CMD_DEST_BASE + 3 * TIKU_TRK_CMD_DEST_MAX) {
        /* A picker row IS the destination (CW-035): the same act the
         * panel commits, aimed where the row points. */
        int k = (command - TIKU_TRK_CMD_DEST_BASE) / TIKU_TRK_CMD_DEST_MAX;
        int at = (command - TIKU_TRK_CMD_DEST_BASE) % TIKU_TRK_CMD_DEST_MAX;
        const char *dest = app->menus.picker_path[k][at];

        if (at >= app->menus.picker_paths[k]) {
            return;
        }
        if (k == 2) {
            char paths[16][TIKU_TRK_PATH_MAX];
            const char *selection[16];
            int count = folder_selection(app, fl, paths, selection, 16);

            if (count > 0) {
                (void)tiku_trk_fs_link(app->fs, selection, count, dest, 0);
                snprintf(app->status, sizeof app->status,
                         "linking %d item%s", count, count == 1 ? "" : "s");
                tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS,
                                    dest);
            }
        } else {
            fpanel_commit(app, fl, (k == 0) ? TIKU_TRK_CMD_MOVE_TO
                                            : TIKU_TRK_CMD_COPY_TO, dest);
        }
        return;
    }
    template_path = tiku_trk_cwmenu_template_path(&app->menus, command);
    /* Where the user asked for it, worked out BEFORE the entry exists: the
     * menu that makes it is still up, and its corner is the click that
     * opened it (PVL-025, PVL-098). */
    if (template_path != NULL || command == TIKU_TRK_CMD_NEW_FOLDER) {
        seeded = seed_new_entry(app, fl, &seed_x, &seed_y);
    }
    if (template_path != NULL) {
        char made[TIKU_TRK_PATH_MAX], error[200];
        const char *slash;
        int row;

        if (tiku_trk_fs_new_from_template(app->fs, template_path, fl->path,
                                          made, sizeof made, error,
                                          sizeof error) == 0) {
            slash = strrchr(made, '/');
            land_new_entry(app, fl, made, seeded, seed_x, seed_y);
            (void)tiku_trk_poseview_reload(fl->view);
            row = select_named(app, fl,
                               slash != NULL ? slash + 1 : made);
            if (row >= 0) {
                begin_rename(app, fl, row);
            }
            snprintf(app->status, sizeof app->status, "created '%.150s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error : "could not create from template");
        }
        app->dirty = 1;
        return;
    }
    template_parent = tiku_trk_cwmenu_template_add_path(&app->menus, command);
    if (template_parent != NULL) {
        char made[TIKU_TRK_PATH_MAX], error[200];

        if (tiku_trk_fs_new_folder(app->fs, template_parent, made,
                                   sizeof made, error, sizeof error) == 0) {
            const char *root = tiku_trk_cwmenu_templates_root(&app->menus);

            snprintf(app->status, sizeof app->status,
                     "created template submenu '%.130s'", strrchr(made, '/')
                     != NULL ? strrchr(made, '/') + 1 : made);
            if (root != NULL) {
                folder_t *templates = open_folder(app, template_parent);
                const char *slash = strrchr(made, '/');

                if (templates != NULL && slash != NULL) {
                    (void)select_named(app, templates, slash + 1);
                }
            }
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error :
                     "could not create template submenu");
        }
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_EDIT_TEMPLATES) {
        const char *root = tiku_trk_cwmenu_templates_root(&app->menus);

        if (root != NULL) {
            (void)open_folder(app, root);
        }
        app->dirty = 1;
        return;
    }
    if (tiku_trk_addons_invoke(&app->addons, command,
                               TIKU_TRK_ADDON_COMMAND_BASE,
                               invoke_addon, app)) {
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_COPY_LAYOUT) {
        (void)tiku_trk_poseview_copy_layout(fl->view);
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_PASTE_LAYOUT) {
        if (tiku_trk_poseview_paste_layout(fl->view) > 0) {
            (void)tiku_trk_poseview_save_columns(fl->view);
            snprintf(app->status, sizeof app->status, "pasted layout");
        }
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_MOUNT_SETTINGS) {
        /* Straight to the Disk mount page: the one caller the jump-to-page
         * request has (AW-101, CW-034). */
        app->prefs_window.page = TIKU_TRK_PAGE_MOUNT;
        tiku_trk_prefs_window_open(&app->prefs_window);
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_MOUNT_ALL ||
        (command >= TIKU_TRK_CMD_MOUNT_PART_BASE &&
         command < TIKU_TRK_CMD_MOUNT_PART_BASE +
                       TIKU_TRK_MOUNT_PART_MAX)) {
        /* Asked of the system, and the answer -- on this host, that
         * mounting needs its leave -- is said out loud (CW-034). */
        char why[200];
        int failed = 0, tried = 0;

        why[0] = '\0';
        if (command == TIKU_TRK_CMD_MOUNT_ALL) {
            tiku_trk_partition_t part[TIKU_TRK_MOUNT_PART_MAX];
            int pn = tiku_trk_partitions_scan(part,
                                              TIKU_TRK_MOUNT_PART_MAX);
            int k;

            for (k = 0; k < pn; k++) {
                if (part[k].mounted) {
                    continue;
                }
                tried++;
                if (tiku_trk_volume_mount(part[k].name, why,
                                          sizeof why) != 0) {
                    failed++;
                    break;
                }
            }
        } else {
            int idx = command - TIKU_TRK_CMD_MOUNT_PART_BASE;

            if (idx < app->menus.mount_parts) {
                tried++;
                failed = tiku_trk_volume_mount(app->menus.mount_part[idx],
                                               why, sizeof why) != 0;
            }
        }
        if (failed && why[0] != '\0') {
            tiku_trk_alert_open(alert_model(app), TIKU_TRK_ALERT_STOP,
                                TIKU_TRK_ALERT_TAG_REFUSAL, why, "Cancel");
        } else if (tried == 0) {
            snprintf(app->status, sizeof app->status,
                     "every partition is already mounted");
        }
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_OPEN_WITH_WINDOW) {
        char wpaths[TIKU_TRK_OW_PATHS][TIKU_TRK_PATH_MAX];
        const char *wsel[TIKU_TRK_OW_PATHS];
        const char *wtypes[TIKU_TRK_APP_TYPES_MAX];
        int wn, wt = 0, wi, wk, wcount;

        wn = tiku_trk_folder_file_actions_selection(fl, wpaths, wsel,
                                                    TIKU_TRK_OW_PATHS);
        if (wn > 0) {
            wcount = tiku_trk_poseview_count(fl->view);
            for (wi = 0; wi < wcount && wt < TIKU_TRK_APP_TYPES_MAX; wi++) {
                tiku_trk_pose_t *wp = tiku_trk_poseview_at(fl->view, wi);

                if (wp == NULL || !wp->selected) {
                    continue;
                }
                for (wk = 0; wk < wt; wk++) {
                    if (strcmp(wtypes[wk], wp->model.type) == 0) {
                        break;
                    }
                }
                if (wk == wt) {
                    wtypes[wt++] = wp->model.type;
                }
            }
            tiku_trk_open_with_window_open(&app->open_with_window,
                &app->apps, wtypes, wt, wsel, wn);
        }
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_UNMOUNT) {
        /* The selection names the volume; with none, the volume ROOT the
         * window is showing is the one the gesture means (CW-034).
         * Everything open on it saves itself first (AW-096), and a
         * refusal or failure is said out loud. */
        char upaths[1][TIKU_TRK_PATH_MAX];
        const char *usel[1];
        const char *upath =
            (tiku_trk_folder_file_actions_selection(fl, upaths, usel, 1) > 0)
                ? usel[0] : fl->path;
        tiku_trk_volumes_t uvols;
        char uwhy[200];

        uwhy[0] = '\0';
        if (tiku_trk_volumes_scan(&uvols) > 0 &&
            tiku_trk_volume_is_root(&uvols, upath)) {
            const tiku_trk_volume_t *uvol =
                tiku_trk_volume_of(&uvols, upath);

            if (tiku_trk_volume_unmount(&uvols, upath, save_before_unmount,
                                        app, uwhy, sizeof uwhy) ==
                    TIKU_TRK_UNMOUNT_OK) {
                snprintf(app->status, sizeof app->status,
                         "unmounted \"%.60s\"",
                         (uvol != NULL) ? uvol->name : upath);
            } else if (uwhy[0] != '\0') {
                tiku_trk_alert_open(alert_model(app), TIKU_TRK_ALERT_STOP,
                                    TIKU_TRK_ALERT_TAG_REFUSAL, uwhy,
                                    "Cancel");
            }
        } else {
            /* Nothing here IS a volume: said, not swallowed (NeXT UIG
             * ch6). */
            snprintf(app->status, sizeof app->status,
                     "Unmount is not available here");
        }
        app->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_NEW_FOLDER) {
        char made[TIKU_TRK_PATH_MAX], error[200];
        const char *slash;
        int row;

        error[0] = '\0';
        if (app->fs != NULL && tiku_trk_fs_new_folder(
                app->fs, fl->path, made, sizeof made, error,
                sizeof error) == 0) {
            slash = strrchr(made, '/');
            snprintf(app->status, sizeof app->status, "created '%.150s'",
                     slash != NULL ? slash + 1 : made);
            land_new_entry(app, fl, made, seeded, seed_x, seed_y);
            (void)tiku_trk_poseview_reload(fl->view);
            row = select_named(app, fl,
                               slash != NULL ? slash + 1 : made);
            if (row >= 0) {
                begin_rename(app, fl, row);
                /* Opening the editor must not hide the operation result;
                 * scripts and users should still see what was made. */
                snprintf(app->status, sizeof app->status,
                         "created '%.150s'",
                         slash != NULL ? slash + 1 : made);
            }
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error : "could not create a new folder");
        }
        app->dirty = 1;
        return;
    }
    if (tiku_trk_folder_actions_invoke(app->desk, fl, command)) {
        app->dirty = 1;
        return;
    }
    if (tiku_trk_folder_file_actions_invoke(&app->file_actions, fl,
                                             command)) {
        app->dirty = 1;
        return;
    }
    switch (command) {
    case TIKU_TRK_CMD_OPEN: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        if (p != NULL && p->model.facts.link_broken) {
            offer_broken_link(app, p->model.path);
        } else if (p != NULL && tiku_trk_model_is_container(&p->model)) {
            (void)open_folder(app, p->model.path);
        }
        break;
    }
    case TIKU_TRK_CMD_OPEN_PARENT: {
        char parent[TIKU_TRK_PATH_MAX];
        char *slash;

        snprintf(parent, sizeof parent, "%s", fl->path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent) { parent[1] = '\0'; } else { *slash = '\0'; }
            (void)open_folder(app, parent);
        }
        break;
    }
    case TIKU_TRK_CMD_GET_INFO: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        /* The selection if there is one, else the folder itself: asking
         * about "this window" with nothing picked is asking about the
         * folder it is showing. */
        {
            int islot = tiku_trk_info_windows_open(&app->info_windows,
                (p != NULL && p->selected) ? p->model.path : fl->path);

            if (islot >= 0) {
                info_opens_with_fill(app, &app->info_windows.model[islot]);
            }
        }
        break;
    }
    case TIKU_TRK_CMD_EDIT_QUERY: {
        /* Re-open the panel ON the query rather than running it again:
         * what the user wants from a result set is usually one more
         * condition, not the same search a second time (Q-045). */
        int whole = (fl->qname[0] != '\0')
                        ? tiku_trk_find_load_named(find_model(app), fl->qname)
                        : -1;

        if (whole < 0) {
            snprintf(app->status, sizeof app->status,
                     "that query cannot be opened");
            break;
        }
        if (!whole) {
            /* Said rather than swallowed: a scope this panel cannot offer
             * means the restored search is not the saved one. */
            snprintf(app->status, sizeof app->status,
                     "one of its places is no longer here");
        }
        open_panel(app);
        break;
    }
    case TIKU_TRK_CMD_MOVE_TO:
    case TIKU_TRK_CMD_COPY_TO:
        /* Both ask the same question -- WHERE -- so both open the same
         * picker and it is the command that decides what happens with the
         * answer. */
        fpanel_open(app, fl, command);
        break;
    case TIKU_TRK_CMD_SELECT_MATCHING:
        /* The same window the keyboard opens: an item that named it and
         * did nothing was the menu disagreeing with the shortcut. */
        tiku_trk_select_window_open(&app->select_window, fl, 520, 300);
        break;
    case TIKU_TRK_CMD_CLOSE:
        close_window(app, fl->win);
        break;
    case TIKU_TRK_CMD_CLOSE_ALL:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE: {
        int i;

        for (i = 0; i < app->folder_count; i++) {
            if (app->folders[i].win != NULL) {
                close_window(app, app->folders[i].win);
            }
        }
        break;
    }
    default:
        if (command >= TIKU_TRK_CMD_COL_ATTR_BASE &&
            command < TIKU_TRK_CMD_OPEN_WITH_BASE) {
            (void)tiku_trk_poseview_toggle_column(fl->view,
                      command - TIKU_TRK_CMD_COL_ATTR_BASE);
            break;
        }
        if (command >= TIKU_TRK_CMD_OPEN_WITH_BASE) {
            open_with_pick(app, fl, command - TIKU_TRK_CMD_OPEN_WITH_BASE);
            break;
        }
        /* Everything else is built and enabled correctly but has no
         * implementation behind it yet; it must do nothing rather than
         * something approximate. */
        break;
    }
    app->dirty = 1;
}

/** @brief Open (or raise) the window for @p path. */
static folder_t *
open_folder(desktop_t *app, const char *path)
{
    folder_t *fl = tiku_trk_folder_lifecycle_open(&app->folder_lifecycle,
                                                  path);

    if (fl != NULL && fl->view != NULL && !fl->trash_union &&
        path != NULL && tiku_trk_fs_in_trash(app->fs, path)) {
        /* A Trash window shows the UNION of every volume's trash
         * (PVN-027): the view becomes a result set over the
         * contributors, refreshed on the query beat, each entry keeping
         * its real path so restore and delete land on the right
         * volume. */
        tiku_trk_model_t m;

        if (app->backend->ops->stat(app->backend, path, &m) == 0 &&
            m.kind == TIKU_TRK_KIND_TRASH) {
            tiku_trk_model_t *models = calloc(256, sizeof *models);

            if (models != NULL) {
                int n = trash_union_models(app, models, 256);
                (void)tiku_trk_poseview_set_results(fl->view, models, n);
                free(models);
                fl->trash_union = 1;
            }
        }
    }
    return fl;
}

/** @brief The folder behind a window, or NULL. */
static folder_t *
folder_of(desktop_t *app, tiku_desk_window_t *w)
{
    return tiku_trk_folder_lifecycle_find(&app->folder_lifecycle, w);
}


static void
blit(desktop_t *app)
{
    if (app->host == NULL) {
        return;                     /* displayless: the surface IS the
                                     * output, and -shot writes it */
    }
    (void)tiku_desk_host_present(app->host, app->surf);
}

/** @brief Adopt a native host frame throughout the desktop controllers. */
static int
resize_desktop(desktop_t *app, int width, int height)
{
    if (app == NULL || width <= 0 || height <= 0 ||
        (width == app->surf->w && height == app->surf->h)) {
        return 0;
    }
    if (tiku_desk_surface_resize(app->surf, width, height,
                                 app->backdrop_color) != 0) {
        return -1;
    }
    app->status_overlay.screen_w = width;
    app->status_overlay.screen_h = height;
    app->find_window.screen_w = width;
    app->find_window.screen_h = height;
    app->select_window.screen_w = width;
    app->select_window.screen_h = height;
    app->prefs_window.screen_w = width;
    app->prefs_window.screen_h = height;
    app->deskbar_prefs_window.screen_w = width;
    app->deskbar_prefs_window.screen_h = height;
    app->switcher_window.screen_w = width;
    app->switcher_window.screen_h = height;
    app->file_panel.screen_w = width;
    app->file_panel.screen_h = height;
    app->folder_render.screen_w = width;
    app->folder_render.screen_h = height;
    app->folder_lifecycle.screen_w = width;
    app->folder_lifecycle.screen_h = height;
    (void)tiku_trk_desktop_view_resize(&app->desktop_view, width, height);
    app->drop_controller.screen_w = width;
    app->drop_controller.screen_h = height;
    return 1;
}

static void draw_nav_levels(desktop_t *app, int px, int py);

static void
draw_all(desktop_t *app)
{
    /* The strip tracker's owner, refreshed per frame so only the window a
     * gesture is running in draws the slide and the guide (TS-063). */
    app->folder_render.strip_owner =
        (tiku_trk_titleview_tracking(&app->tstrip) != TIKU_TRK_TRACK_NONE)
            ? app->tstrip_fl : NULL;

    tiku_desk_fill(app->surf,
                   (tiku_desk_rect_t){ 0, 0, app->surf->w, app->surf->h },
                   app->backdrop_color);
    tiku_trk_deskbar_publish_workspace(app->deskbar, app->team, app->desk);
    if (!tiku_trk_deskbar_raised(app->deskbar)) {
        /* Under the windows: with auto-raise on, a window may cover the
         * bar until the user reaches for it.  Draw order IS the z-order
         * here -- there is no window server to ask (DB-052). */
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
        tiku_desk_workspace_draw(app->desk);
    } else {
        tiku_desk_workspace_draw(app->desk);
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
    }
    if (app->zoom_t0 != 0) {
        /* The window frame sliding out of or into the menu item's
         * rectangle: four steps of an inverted outline, done in an eye
         * blink (DB-030). */
        int64_t age = tiku_trk_now() - app->zoom_t0;

        if (age > 120000) {
            app->zoom_t0 = 0;
        } else {
            int num = (int)(age / 30000) + 1;   /* 1..4 */
            tiku_desk_rect_t z;

            z.x = app->zoom_from.x +
                  (app->zoom_to.x - app->zoom_from.x) * num / 4;
            z.y = app->zoom_from.y +
                  (app->zoom_to.y - app->zoom_from.y) * num / 4;
            z.w = app->zoom_from.w +
                  (app->zoom_to.w - app->zoom_from.w) * num / 4;
            z.h = app->zoom_from.h +
                  (app->zoom_to.h - app->zoom_from.h) * num / 4;
            tiku_desk_invert_frame(app->surf, z);
            app->dirty = 1;     /* the next frame moves it again */
        }
    }
    if (app->info_windows.hover_slot >= 0 &&
        app->info_windows.hover_field >= 0) {
        /* The truncated field's full text, floated over everything and
         * clamped to the screen (AW-073).  Only when the value really is
         * wider than its row -- a pop-up repeating what is already
         * readable would be noise. */
        int slot = app->info_windows.hover_slot;
        tiku_trk_info_t *info = &app->info_windows.model[slot];
        char value[TIKU_TRK_INFO_VAL_MAX];
        const char *label = "";
        int fx, fy, fw, fh;

        if (tiku_trk_info_field(info, app->store,
                app->info_windows.hover_field, &label, value,
                sizeof value) != TIKU_TRK_INFO_F_NONE &&
            tiku_trk_info_windows_field_rect(&app->info_windows, slot,
                app->info_windows.hover_field, &fx, &fy, &fw, &fh)) {
            const tiku_desk_font_t *font = tiku_desk_font_plain();
            int tw = tiku_desk_text_width(font, value);

            if (tw > fw) {
                tiku_desk_rect_t box = { fx - 4, fy - 2, tw + 12,
                                         font->height + 6 };

                if (box.x + box.w > app->surf->w) {
                    box.x = app->surf->w - box.w;
                }
                if (box.x < 0) {
                    box.x = 0;
                }
                tiku_desk_fill(app->surf, box, TIKU_DESK_C_NOTE);
                tiku_desk_frame(app->surf, box, TIKU_DESK_C_TEXT);
                tiku_desk_text(app->surf, font, box.x + 5,
                               box.y + 2 + font->ascent, value,
                               TIKU_DESK_C_TEXT);
            }
        }
    }
    if (tiku_trk_titleview_tracking(&app->tstrip) == TIKU_TRK_TRACK_DRAG &&
        app->tstrip.removed) {
        /* The torn-out column follows the pointer as a dotted outline
         * (TS-008): dashes, drawn over everything, the column's own width
         * and the strip's height. */
        int wds = app->tstrip.removed_width;
        int x0 = app->strip_ptr_x - wds / 2;
        int y0 = app->strip_ptr_y - 20 /* TITLE_HEIGHT */ / 2;
        int d;

        for (d = 0; d < wds; d += 4) {
            tiku_desk_rect_t dash = { x0 + d, y0, 2, 1 };

            tiku_desk_fill(app->surf, dash, TIKU_DESK_RGB(0, 0, 0));
            dash.y = y0 + 20 /* TITLE_HEIGHT */ - 1;
            tiku_desk_fill(app->surf, dash, TIKU_DESK_RGB(0, 0, 0));
        }
        for (d = 0; d < 20 /* TITLE_HEIGHT */; d += 4) {
            tiku_desk_rect_t dash = { x0, y0 + d, 1, 2 };

            tiku_desk_fill(app->surf, dash, TIKU_DESK_RGB(0, 0, 0));
            dash.x = x0 + wds - 1;
            tiku_desk_fill(app->surf, dash, TIKU_DESK_RGB(0, 0, 0));
        }
    }
    if (tiku_trk_drag_active(&app->drag)) {
        /* What is being carried, drawn at the pointer: a drag with no
         * picture is a drag the user has to remember the contents of.
         * Translucent, and faded further the more it holds, so what it is
         * being dropped ON stays readable (IV-053). */
        int i, shown = (app->drag.count < 4) ? app->drag.count : 4;
        int alpha = (app->drag.count > 3) ? 120 : 168;

        for (i = 0; i < shown; i++) {
            tiku_desk_rect_t box;
            char art[TIKU_TRK_NAME_MAX];
            tiku_trk_model_t dm;

            if (app->backend->ops->stat(app->backend, app->drag.path[i],
                                        &dm) != 0) {
                continue;
            }
            (void)tiku_trk_nodeicon_resolve(&app->nicons, app->store, &dm,
                                            art, sizeof art);
            box.x = app->drag.x - 16 + i * 6;
            box.y = app->drag.y - 16 + i * 6;
            box.w = 32;
            box.h = 32;
            (void)tiku_trk_icon_paint(app->surf, art, box.x, box.y, 32);
            /* Tinted over its own pixels rather than composited in an
             * offscreen: the picture is what the pointer carries, and it
             * has to sit over whatever is under it. */
            tiku_trk_paint_tint(app->surf, box, app->backdrop_color,
                                255 - alpha);
            if (app->drag.count > 3) {
                /* Large selections get a short fade at the edge of the
                 * pointer picture.  The surface is opaque, so the fade is
                 * expressed as progressively stronger backdrop tinting over
                 * the four edge bands rather than an off-screen alpha
                 * bitmap (PVS-034). */
                tiku_desk_rect_t edge = box;
                edge.w = 4;
                tiku_trk_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge.x = box.x + box.w - 4;
                tiku_trk_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge = box;
                edge.h = 4;
                tiku_trk_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge.y = box.y + box.h - 4;
                tiku_trk_paint_tint(app->surf, edge, app->backdrop_color, 96);
            }
        }
        /* The copy cursor's spelling here: when the drop in front of the
         * pointer would COPY -- a directory on another store, or Option
         * forcing it -- the ghost wears a small plus badge, so the user is
         * told before the release which of the two acts this is
         * (PVS-036). */
        if (app->drag_target.folder != NULL) {
            char dest[TIKU_TRK_PATH_MAX];
            unsigned dmods = 0u;
            int same;

            if (app->drag_target.row >= 0) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(
                    app->drag_target.folder->view, app->drag_target.row);

                snprintf(dest, sizeof dest, "%s",
                         (p != NULL) ? p->model.path : "");
            } else {
                snprintf(dest, sizeof dest, "%s",
                         app->drag_target.folder->path);
            }
            if ((app->drag_live_mods & TIKU_DESK_MOD_OPTION) != 0u) {
                dmods |= TIKU_TRK_DRAG_MOD_COPY;
            }
            if ((app->drag_live_mods & TIKU_DESK_MOD_CMD) != 0u) {
                dmods |= TIKU_TRK_DRAG_MOD_LINK;
            }
            same = dest[0] != '\0' &&
                   tiku_trk_fs_drop_action(app->fs, app->drag.path[0],
                                           dest, 0) == TIKU_TRK_OP_MOVE;
            if (dest[0] != '\0' &&
                tiku_trk_drag_action(&app->drag, dest, dmods, same) ==
                    TIKU_TRK_DROP_COPY) {
                tiku_desk_rect_t badge = { app->drag.x + 10,
                                           app->drag.y + 10, 13, 13 };
                tiku_desk_rect_t stroke;

                tiku_desk_fill(app->surf, badge,
                               TIKU_DESK_RGB(255, 255, 190));
                tiku_desk_frame(app->surf, badge, TIKU_DESK_C_TEXT);
                stroke = (tiku_desk_rect_t){ badge.x + 3, badge.y + 6,
                                             7, 1 };
                tiku_desk_fill(app->surf, stroke, TIKU_DESK_C_TEXT);
                stroke = (tiku_desk_rect_t){ badge.x + 6, badge.y + 3,
                                             1, 7 };
                tiku_desk_fill(app->surf, stroke, TIKU_DESK_C_TEXT);
            }
        }
    }
    /* Drawing another folder rebuilt the one shared menu model for that
     * folder.  Put it back on the bar that actually owns the popup before
     * painting or hit-testing it. */
    if (app->bar_fl == NULL && app->bar_open < 0 && app->popup != NULL &&
        app->popup_fl != NULL) {
        tiku_trk_sc_ctx_t c;

        build_open_with(app, app->popup_fl);
        build_per_type(app, app->popup_fl);
        c = menu_ctx(app, app->popup_fl, 0u);
        tiku_trk_cwmenu_rebuild(&app->menus, &c);
    }
    if (app->bar_fl != NULL && app->bar_open >= 0) {
        tiku_trk_sc_ctx_t c;

        build_open_with(app, app->bar_fl);
        build_per_type(app, app->bar_fl);
        c = menu_ctx(app, app->bar_fl, 0u);
        tiku_trk_cwmenu_rebuild(&app->menus, &c);
        app->menus.bar.open = app->bar_open;
        if (app->bar_open < app->menus.bar.count) {
            app->popup = app->menus.bar.menu[app->bar_open];
        }
    }
    /* A menu is drawn last because it is above everything, including the
     * Deskbar strip. */
    if (app->popup != NULL) {
        /* Re-aimed here because the menus are rebuilt per draw, which
         * clears the hot row a moment after the motion set it. */
        (void)tiku_trk_menu_track(app->popup, app->popup_frame,
                                  app->ptr_x, app->ptr_y);
        tiku_trk_menu_draw(app->popup, app->surf, app->popup_frame);
        if (app->popup_sub != NULL) {
            (void)tiku_trk_menu_track(app->popup_sub,
                                      app->popup_sub_frame, app->ptr_x,
                                      app->ptr_y);
            tiku_trk_menu_draw(app->popup_sub, app->surf,
                               app->popup_sub_frame);
        }
    }
    if (app->qdrag_on) {
        /* The query icon in flight (Q-034), riding under the pointer. */
        (void)tiku_trk_icon_paint(app->surf, "query", app->ptr_x - 16,
                                  app->ptr_y - 16, 32);
    }
    if (app->nav_on && app->popup != NULL) {
        draw_nav_levels(app, app->popup_frame.x + app->popup_frame.w - 4,
                        app->popup_frame.y + 4);
    } else if (app->spring_nav_on) {
        /* The spring's walk hangs off the sprung pose (PVS-051). */
        draw_nav_levels(app, app->spring_nav_x, app->spring_nav_y);
    }
    tiku_trk_status_overlay_draw(&app->status_overlay);
    app->dirty = 0;
}

/**
 * @brief Every open nav level, outermost first, each hanging off the row
 *        that opened it -- so the trail back out is on screen (CW-045).
 */
static void
draw_nav_levels(desktop_t *app, int px, int py)
{
    {
        const tiku_desk_font_t *nf = tiku_desk_font_plain();
        int k;

        for (k = 0; k < app->nav.depth; k++) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];
            tiku_desk_rect_t r;
            int i;

            tiku_trk_nav_place(&app->nav, k, px, py, app->surf->w,
                               app->surf->h,
                               &r.x, &r.y, &r.w, &r.h);
            tiku_desk_fill(app->surf, r, TIKU_DESK_C_PANEL);
            tiku_desk_bevel(app->surf, r,
                tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
                tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
            if (lv->n == 0) {
                tiku_desk_text(app->surf, nf, r.x + 8,
                               r.y + 2 + nf->ascent, TIKU_TRK_NAV_EMPTY,
                               tiku_desk_tint(TIKU_DESK_C_TEXT,
                                              TIKU_DESK_LIGHTEN_2));
            }
            for (i = 0; i < lv->n; i++) {
                int ty = r.y + 2 + i * TIKU_TRK_NAV_ROW;
                tiku_desk_rect_t row2;

                row2.x = r.x + 1; row2.y = ty; row2.w = r.w - 2;
                row2.h = TIKU_TRK_NAV_ROW;
                if (i == lv->sel) {
                    tiku_desk_fill(app->surf, row2, TIKU_DESK_C_SELECT);
                }
                tiku_desk_text(app->surf, nf, r.x + 8, ty + nf->ascent + 2,
                               lv->item[i].name,
                               (i == lv->sel) ? TIKU_DESK_C_SELTEXT
                               : (lv->item[i].enabled ? TIKU_DESK_C_TEXT
                                  : tiku_desk_tint(TIKU_DESK_C_TEXT,
                                                   TIKU_DESK_LIGHTEN_2)));
                if (lv->item[i].is_container) {
                    /* The arrow says a row goes further, which is the one
                     * thing a nested menu must announce. */
                    tiku_desk_text(app->surf, nf, r.x + r.w - 12,
                                   ty + nf->ascent + 2, ">",
                                   (i == lv->sel) ? TIKU_DESK_C_SELTEXT
                                                  : TIKU_DESK_C_TEXT);
                }
            }
            /* The next level hangs off this level's chosen row. */
            px = r.x + r.w - 4;
            py = r.y + 2 + ((lv->sel >= 0) ? lv->sel : 0) *
                 TIKU_TRK_NAV_ROW;
        }
    }
}

/**
 * @brief Pixels a window built up frame by frame has wrong (PVL-045).
 *
 * A window that paints what changed can only be held against the picture
 * it is meant to be equal to: keep what is on the screen, throw the
 * windows' kept pixels away so the next draw builds them from nothing, and
 * count where the two disagree.  Nothing is drawn before the copy is
 * taken -- a frame drawn to settle the window would be the very repaint
 * the check exists to do without.  The from-scratch draw stands: it is the
 * picture the shell should be showing anyway.
 */
static long
script_redraw_diff(void *ctx)
{
    desktop_t *app = ctx;
    tiku_desk_rgb_t *was;
    size_t n, i;
    long differ = 0;

    if (app == NULL || app->surf == NULL || app->surf->px == NULL) {
        return -1;
    }
    n = (size_t)app->surf->w * (size_t)app->surf->h;
    if (app->surf->scale > 1) {
        n *= (size_t)app->surf->scale * (size_t)app->surf->scale;
    }
    was = malloc(n * sizeof *was);
    if (was == NULL) {
        return -1;
    }
    memcpy(was, app->surf->px, n * sizeof *was);
    for (i = 0u; i < (size_t)app->folder_count; i++) {
        tiku_trk_folder_window_forget(&app->folders[i]);
    }
    draw_all(app);
    for (i = 0u; i < n; i++) {
        if (was[i] != app->surf->px[i]) {
            differ++;
        }
    }
    free(was);
    return differ;
}

static int
tracker_run(int argc, char **argv)
{
    desktop_t app;
    const char *port = NULL, *shot = NULL, *script_path = NULL;

    restart_argv = argv;
    tiku_trk_script_t *sc = NULL;
    int rc = 0;
    int baud = 115200, use_device = 0, i;
    int64_t shot_at = 0, deadline = 0, last_pump = 0, last_requery = 0;
    int64_t last_sweep = 0, started_at = tiku_trk_now();
    int shot_menu = 0;
    const char *shot_edit = NULL, *shot_find = NULL;
    int shot_panel = 0, shot_live = 0, shot_shift = 0, shot_zoom = 0;
    int shot_cascade = 0, shot_notify = 0, shot_select = 0;
    tiku_trk_click_t click = { 0 };
    const char *openme[TIKU_DESK_WINDOW_MAX];
    int nopen = 0;
    /* Safe mode starts deliberately plain: it restores no session.  The
     * row's other clauses -- replicants and add-ons -- have nothing here to
     * suppress (AW-007). */
    int safe_mode = 0;

    memset(&app, 0, sizeof app);
    app.theme_dark = -1;      /* unapplied: the first sync decides */
    (void)setlocale(LC_ALL, "");
    app.bar_open = -1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0)                      { use_device = 1; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { port = argv[++i]; use_device = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { shot = argv[++i]; }
        else if (strcmp(argv[i], "-shotmenu") == 0) { shot_menu = 1; }
        else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc) { script_path = argv[++i]; }
        else if (strcmp(argv[i], "-shotedit") == 0 && i + 1 < argc) {
            shot_edit = argv[++i];
        }
        else if (strcmp(argv[i], "-shotfind") == 0 && i + 1 < argc) {
            shot_find = argv[++i];
        }
        else if (strcmp(argv[i], "-shotpanel") == 0) { shot_panel = 1; }
        else if (strcmp(argv[i], "-shotlive") == 0) { shot_live = 1; }
        else if (strcmp(argv[i], "-safe") == 0) { safe_mode = 1; }
        else if (strcmp(argv[i], "-shotshift") == 0) { shot_shift = 1; }
        else if (strcmp(argv[i], "-shotzoom") == 0) { shot_zoom = 1; }
        else if (strcmp(argv[i], "-shotcascade") == 0) { shot_cascade = 1; }
        else if (strcmp(argv[i], "-shotnotify") == 0) { shot_notify = 1; }
        else if (strcmp(argv[i], "-shotselect") == 0) { shot_select = 1; }
        else if (argv[i][0] != '-' && nopen < (int)(sizeof openme /
                                                    sizeof openme[0])) {
            /* AW-100: EVERY path argument opens its own window.  Keeping
             * only the last is how a shell glob silently opens one folder
             * out of the several the user asked for. */
            openme[nopen++] = argv[i];
        }
        else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) {
            deadline = tiku_trk_now() + (int64_t)atoi(argv[++i]) * 1000000;
        }
    }
    app.backend = use_device ? tiku_trk_backend_tiku_open(port, baud)
                             : tiku_trk_backend_posix_open();
    if (app.backend == NULL) {
        fprintf(stderr, "tracker: no backend\n");
        return 1;
    }
    app.fs = tiku_trk_fs_new(app.backend);
    (void)tiku_trk_fs_prepare_trash(app.fs);
    app.backdrop_color = TIKU_DESK_C_BACKDROP;
    app.surf = tiku_desk_surface_new(DESK_W, DESK_H, app.backdrop_color);
    app.row_h = tiku_desk_font_plain()->height + 5;
    app.desk = tiku_desk_workspace_new(app.surf);
    app.deskbar = tiku_trk_deskbar_new(app.backend);
    app.team = tiku_trk_deskbar_add_team(app.deskbar, "Tracker");
    (void)tiku_trk_deskbar_scan_processes(app.deskbar);
    tiku_trk_cwmenu_init(&app.menus);
    tiku_trk_status_overlay_init(&app.status_overlay, app.surf,
        DESK_W, DESK_H, &app.dirty, status_answer, status_stop, &app);
    tiku_trk_find_window_init(&app.find_window, app.desk, DESK_W, DESK_H,
        &app.dirty, app.status, sizeof app.status, panel_search, &app);
    app.loop = tiku_trk_loop_new();
    /* Once a second, so the displayed minute can never go stale while
     * nothing else dirties the surface (DB-044). */
    (void)tiku_trk_run_periodic(app.loop, tick_clock, &app, NULL, 1000000,
                                0);
    if (script_path == NULL) {
        /* The living half of the roster: applications that quit leave
         * the list, new ones join it (DB-014/DB-015).  Never in a
         * scripted run, whose fixture must not chase the host's
         * processes. */
        (void)tiku_trk_run_periodic(app.loop, rescan_teams, &app, NULL,
                                    2000000, 0);
    }
    tiku_trk_open_with_window_init(&app.open_with_window, app.desk,
        DESK_W, DESK_H, &app.dirty, open_with_window_launch, &app,
        alert_model(&app), app.store);
    tiku_trk_select_window_init(&app.select_window, app.desk,
        DESK_W, DESK_H, &app.dirty, apply_select_model, &app);
    tiku_trk_clip_init(&app.clip);
    app.store = tiku_trk_session_open_store(app.backend);
    tiku_trk_deskbar_set_store(app.deskbar, app.store);
    (void)tiku_trk_deskbar_load_replicants(app.deskbar);
    tiku_trk_session_controller_init(&app.session, app.store, app.deskbar,
        &app.desktop_view, &app.folder_lifecycle);
    tiku_trk_find_window_set_store(&app.find_window, app.store);
    (void)tiku_trk_poseview_install_query_defaults(app.backend, app.store);
    tiku_trk_session_restore_deskbar(&app.session);
    {
        /* Where the menu's own folder is, and the lists it shows above it.
         * Fed rather than read by the bar: the recent lists are the same
         * ones the Open panel keeps, and a second copy would disagree
         * (DB-002, DB-003). */
        const char *home = getenv("HOME");
        char user[TIKU_TRK_PATH_MAX];

        snprintf(user, sizeof user, "%s/.config/deskbar",
                 (home != NULL && home[0] != '\0') ? home : "");
        tiku_trk_deskbar_set_menu_dir(app.deskbar, user,
                                      "/usr/share/tiku-tracker/deskbar");
    }
    tiku_trk_deskbar_prefs_window_init(&app.deskbar_prefs_window, app.desk,
        app.deskbar, app.store, DESK_W, DESK_H,
        tiku_trk_deskbar_user_menu_dir(app.deskbar), &app.dirty,
        deskbar_edit_path, &app);
    {
        /* Which applications there are.  A file rather than a database:
         * what the rows are about is the ANSWERS -- which application
         * opens what, and how well -- not where the list is kept. */
        const char *home = getenv("HOME");
        char reg[TIKU_TRK_PATH_MAX];

        {
            char tdb[TIKU_TRK_PATH_MAX];

            snprintf(tdb, sizeof tdb, "%s/.config/tracker/types",
                     (home != NULL && home[0] != '\0') ? home : "");
            (void)tiku_trk_types_install(&app.types, tdb);
            tiku_trk_find_window_set_types(&app.find_window, &app.types);
            /* The art a type wears is the database's to declare, and the
             * icon cache is what asks (IV-011). */
            tiku_trk_nodeicon_set_types(&app.nicons, &app.types);
        }
        tiku_trk_launch_clear(&app.apps);
        {
            /* This Tracker is in the list, so choosing it is answerable:
             * it opens folders here rather than starting a second desktop
             * (AW-035). */
            int self = tiku_trk_launch_add(&app.apps, "Tracker", "",
                                           "inode/directory");
            if (self >= 0) {
                app.apps.app[self].is_tracker = 1;
            }
        }
        snprintf(reg, sizeof reg, "%s/.config/tracker/apps",
                 (home != NULL && home[0] != '\0') ? home : "");
        if (tiku_trk_launch_load(&app.apps, reg) < 0) {
            (void)tiku_trk_launch_load(&app.apps,
                                       "/usr/share/tiku-tracker/apps");
        }
    }
    tiku_trk_launch_controller_init(&app.launcher, &app.apps, app.store,
        app.status, sizeof app.status, launch_open_path, &app);
    tiku_trk_folder_menu_init(&app.folder_menu, &app.menus, &app.clip,
        app.backend, app.fs, &app.types, &app.launcher, &app.bar_fl,
        &app.bar_open);
    tiku_trk_menu_set_icon_painter(paint_menu_icon);
    tiku_trk_deskbar_set_menu_icon_painter(app.deskbar, deskbar_menu_icon,
                                           &app);
    tiku_trk_addons_init(&app.addons);
    (void)tiku_trk_addons_register(&app.addons, "Open in Tracker", "",
                                   "inode/directory", 'o');
    (void)tiku_trk_addons_register(&app.addons, "Open with Editor", "",
                                   "text/plain", 'e');
    (void)tiku_trk_addons_register(&app.addons, "Inspect", "", "*", 'i');
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(app.addons_dir, sizeof app.addons_dir,
                     "%s/.config/tracker/add-ons", home);
            snprintf(app.addons_keys, sizeof app.addons_keys,
                     "%s/.config/tracker/addon-shortcuts", home);
        }
        sync_addons(&app, 1);
    }
    tiku_trk_thumb_init(&app.thumbs);
    tiku_trk_nodeicon_init(&app.nicons);
    tiku_trk_volwatch_init(&app.volwatch);
    app.info_relink = -1;
    tiku_trk_info_windows_init(&app.info_windows, app.desk, app.backend,
        app.store, app.fs, alert_model(&app), &app.dirty);
    tiku_trk_panel_window_init(&app.file_panel, app.desk, app.backend,
        app.store, &app.nicons, app.row_h, DESK_W, DESK_H, &app.dirty,
        fpanel_commit, &app);
    tiku_trk_panel_window_set_alert(&app.file_panel, alert_model(&app));
    app.find_window.save_as = find_save_as;
    app.find_window.save_as_context = &app;
    app.find_window.select_folder = find_select_folder;
    {
        const char *home = getenv("HOME");
        char path[TIKU_TRK_PATH_MAX] = "";

        if (home != NULL && home[0] != '\0') {
            snprintf(path, sizeof path, "%s/.config/TrackerSettings", home);
        }
        tiku_trk_prefs_window_init(&app.prefs_window, app.desk,
            DESK_W, DESK_H, path, &app.dirty,
            &alert_model(&app)->open);
    }
    tiku_trk_folder_file_actions_init(&app.file_actions, app.fs,
        app.backend, &app.clip,
        &app.drop_controller.pending, app.drop_controller.destination,
        alert_model(&app), open_preferences, &app, app.status,
        sizeof app.status, file_actions_close_under, &app);
    tiku_trk_folder_edit_init(&app.folder_edit, app.backend, app.fs,
        app.status, sizeof app.status, &app.dirty);
    tiku_trk_folder_menu_set_editor(&app.folder_menu, &app.folder_edit);
    tiku_trk_band_controller_init(&app.band_controller, app.row_h,
                                  &app.dirty);
    tiku_trk_switcher_window_init(&app.switcher_window, app.desk,
        app.deskbar, DESK_W, DESK_H, &app.dirty, app.status,
        sizeof app.status);
    app.folder_render = (tiku_trk_folder_render_t){
        .workspace = app.desk,
        .menus = &app.menus,
        .prefs = prefs_model(&app),
        .clip = &app.clip,
        .backend = app.backend,
        .icons = &app.nicons,
        .store = app.store,
        .thumbs = &app.thumbs,
        .row_h = app.row_h,
        .screen_w = DESK_W,
        .screen_h = DESK_H,
        .status = app.status,
        .count_text = app.count_text,
        .count_text_size = sizeof app.count_text,
        .finding = &app.finding,
        .find_text = app.find_buf,
        .editing = &app.folder_edit.editing,
        .edit_path = app.folder_edit.edit_path,
        .value_edit = &app.folder_edit.value_edit,
        .renaming = &app.folder_edit.renaming,
        .rename_folder = &app.folder_edit.rename_folder,
        .rename_row = &app.folder_edit.rename_row,
        .rename_edit = &app.folder_edit.rename_edit,
        .band_folder = &app.band_controller.folder,
        .band_x0 = &app.band_controller.x0,
        .band_y0 = &app.band_controller.y0,
        .band_x1 = &app.band_controller.x1,
        .band_y1 = &app.band_controller.y1,
        .drop_folder = &app.drag_target.folder,
        .drop_row = &app.drag_target.row,
        .prepare_menu = tiku_trk_folder_menu_prepare,
        .strip = &app.tstrip,
        .strip_owner = NULL,
        .prepare_context = &app.folder_menu
    };
    tiku_trk_folder_lifecycle_init(&app.folder_lifecycle, app.folders,
        TIKU_DESK_WINDOW_MAX, &app.folder_count, &app.cascade_x,
        &app.cascade_y, app.desk, app.backend, app.store,
        prefs_model(&app), &app.folder_render, app.status,
        sizeof app.status, &app.dirty, DESK_W, DESK_H);

    app.desk_row = -1;
    (void)tiku_trk_desktop_view_init(&app.desktop_view, app.backend, app.fs,
        prefs_model(&app), app.deskbar, &app.folder_render, DESK_W, DESK_H);
    tiku_trk_drag_target_init(&app.drag_target, app.desk, app.deskbar,
        &app.desktop_view, &app.drag, app.folders, &app.folder_count,
        app.row_h, &app.dirty, spring_open_nav, &app);
    tiku_trk_script_adapter_init(&app.script_adapter, app.desk, app.surf,
        &app.folder_lifecycle, &app.desktop_view, app.deskbar,
        alert_model(&app), &app.find_window, &app.file_panel,
        &app.folder_edit, &app.popup, &app.popup_sub, &app.popup_frame,
        &app.popup_sub_frame, app.status, app.count_text, app.row_h);
    /* After the init, which writes the whole adapter. */
    app.script_adapter.redraw_diff = script_redraw_diff;
    app.script_adapter.redraw_ctx = &app;
    tiku_trk_drop_controller_init(&app.drop_controller, app.deskbar,
        app.backend, app.fs, &app.desktop_view, &app.menus,
        alert_model(&app), &app.drag, &app.drag_target.folder,
        &app.drag_target.row,
        &app.popup, &app.popup_sub, &app.popup_frame, app.prog_dest,
        sizeof app.prog_dest, app.status, sizeof app.status, &app.dirty,
        DESK_W, DESK_H, set_backdrop_color, &app);
    tiku_trk_drop_controller_set_launcher(&app.drop_controller,
        (const struct tiku_trk_apps *)&app.apps, drop_launch, &app);

    if (use_device) {
        /* The tray is the device: three values that say at a glance whether
         * the board is alive and what it is doing. */
        (void)tiku_trk_deskbar_add_replicant(app.deskbar, "uptime",
                                             "/sys/uptime");
        (void)tiku_trk_deskbar_add_replicant(app.deskbar, "boots",
                                             "/sys/boot_count");
        (void)tiku_trk_deskbar_add_replicant(app.deskbar, "free",
                                             "/sys/mem/nvmfree");
        /* The panel offers the namespace's own top level as its scopes,
         * read from the device rather than assumed. */
        {
            tiku_trk_model_t roots[TIKU_TRK_FIND_PLACES_MAX];
            int nr = 0, k;

            nr = list_children(app.backend, "/", roots,
                               TIKU_TRK_FIND_PLACES_MAX);
            for (k = 0; k < nr; k++) {
                if (tiku_trk_model_is_container(&roots[k])) {
                    (void)tiku_trk_find_add_place_identity(
                        find_model(&app), roots[k].name, roots[k].facts.dev,
                        app.backend->devid[0] != '\0' ? app.backend->devid
                                                     : roots[k].name);
                }
            }
        }
    }
    tiku_trk_session_restore_folders(&app.session, openme, nopen,
                                     safe_mode, use_device);

    (void)tiku_trk_find_open_default(find_model(&app), (int64_t)time(NULL));

    if (script_path != NULL) {
        /* Displayless.  The shell draws into the same surface either way,
         * so a scripted run exercises the real draw path -- it just has
         * nobody to show it to (see tiku_trk_script.h). */
        sc = tiku_trk_script_open(script_path);
        /* A scripted wait is a number of turns: count the autoscroll hold
         * in pulses so the same script passes under any load (PVS-050). */
        tiku_trk_band_controller_set_script_delay(&app.band_controller, 30);
        tiku_trk_drag_target_set_script_delay(&app.drag_target, 30);
        {
            /* The fixture's clock: 7 Aug 16:26, the sample the clock row
             * itself quotes.  Live, a minute rolling over mid-comparison
             * cost the redraw checks a fistful of clock pixels. */
            struct tm fixed;

            memset(&fixed, 0, sizeof fixed);
            fixed.tm_year = 2026 - 1900;
            fixed.tm_mon = 7;
            fixed.tm_mday = 7;
            fixed.tm_hour = 16;
            fixed.tm_min = 26;
            fixed.tm_isdst = -1;
            tiku_trk_deskbar_set_clock(app.deskbar, mktime(&fixed));
        }
        if (sc == NULL) {
            return 2;
        }
    } else {
        app.host = tiku_desk_host_open(DESK_W, DESK_H, "TikuOS Desktop");
        if (app.host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
        if (tiku_desk_host_scale(app.host) > 1) {
            (void)tiku_desk_surface_rescale(app.surf,
                tiku_desk_host_scale(app.host), app.backdrop_color);
        }
    }
    if (shot != NULL) {
        shot_at = tiku_trk_now() + 2000000;
    }
    if (shot_select) {
        /* Scripted: the Select window over the front folder, with an
         * expression already in it, so a capture shows the whole control
         * set the way it comes up. */
        tiku_desk_window_t *fw = tiku_desk_workspace_focused(app.desk);
        folder_t *ffl = folder_of(&app, fw);

        tiku_trk_select_window_open(&app.select_window, ffl, 520, 300);
        {
            const char *e = "gpio*";

            while (*e != '\0') {
                (void)tiku_trk_edit_key(&select_model(&app)->expr,
                                        TIKU_TRK_EDIT_CHAR, 0, *e++);
            }
        }
    }
    if (shot_live) {
        /* Search for what is currently on under /dev, change one of them,
         * and let the window's OWN refresh path apply the difference. */
        char err[128];

        open_panel(&app);
        tiku_trk_find_set_mode(find_model(&app), TIKU_TRK_QMODE_ATTRIBUTE);
        find_model(&app)->row[0].field = TIKU_TRK_QF_VALUE;
        find_model(&app)->row[0].op = TIKU_TRK_OP_IS;
        snprintf(find_model(&app)->row[0].value, sizeof find_model(&app)->row[0].value, "1");
        find_model(&app)->everywhere = 0;
        {
            int k;

            for (k = 0; k < find_model(&app)->place_count; k++) {
                if (strcmp(find_model(&app)->place[k], "dev") == 0) {
                    find_model(&app)->place_mask = 1u << k;
                }
            }
        }
        (void)panel_search(&app, find_model(&app));
        (void)app.backend->ops->write(app.backend, "/dev/led2", "1", 1, err,
                                      sizeof err);
        {
            int k;

            for (k = 0; k < app.folder_count; k++) {
                (void)refresh_results(&app, &app.folders[k]);
            }
        }
        tiku_trk_find_window_close(&app.find_window);
    }
    if (shot_panel) {
        /* Drive the panel the way a user would: open it, choose attribute
         * mode, fill two rows joined by or, scope it, and search. */
        open_panel(&app);
        tiku_trk_find_set_mode(find_model(&app), TIKU_TRK_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(find_model(&app));
        find_model(&app)->row[0].field = TIKU_TRK_QF_UNIT;
        find_model(&app)->row[0].op = TIKU_TRK_OP_IS;
        snprintf(find_model(&app)->row[0].value, sizeof find_model(&app)->row[0].value,
                 "bytes");
        find_model(&app)->row[0].join = TIKU_TRK_JOIN_OR;
        find_model(&app)->row[1].field = TIKU_TRK_QF_UNIT;
        find_model(&app)->row[1].op = TIKU_TRK_OP_IS;
        snprintf(find_model(&app)->row[1].value, sizeof find_model(&app)->row[1].value, "s");
        find_model(&app)->everywhere = 0;
        find_model(&app)->place_mask = 1u;
        (void)panel_search(&app, find_model(&app));
        open_panel(&app);
    }
    if (shot_find != NULL) {
        snprintf(app.find_buf, sizeof app.find_buf, "%.60s", shot_find);
        app.find_len = (int)strlen(app.find_buf);
        run_find(&app, &app.folders[app.folder_count - 1]);
    }
    if (shot_edit != NULL) {
        /* Select the node, start an edit, type the value and commit -- the
         * same path a user drives, so a screenshot is evidence. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        int i2, n2 = tiku_trk_poseview_count(fl->view);
        const char *eq = strchr(shot_edit, '=');
        char want[32];

        snprintf(want, sizeof want, "%.*s",
                 (int)(eq ? (size_t)(eq - shot_edit) : strlen(shot_edit)),
                 shot_edit);
        for (i2 = 0; i2 < n2; i2++) {
            tiku_trk_pose_t *p2 = tiku_trk_poseview_at(fl->view, i2);

            if (p2 != NULL && strcmp(p2->model.name, want) == 0) {
                tiku_trk_poseview_click(fl->view, i2, 0u);
                begin_edit(&app, fl);
                if (eq != NULL && app.folder_edit.editing) {
                    tiku_trk_edit_start(&app.folder_edit.value_edit, eq + 1);
                    commit_edit(&app, fl);
                }
                break;
            }
        }
    }
    if (shot_notify) {
        /* Change a value under the view and let the POLL derive the notice,
         * so what the screen shows came through the handler. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        char err[128];
        unsigned nf;

        (void)app.backend->ops->write(app.backend, "/dev/led0", "1", 1, err,
                                      sizeof err);
        {
            tiku_trk_change_t ch[32];
            int nch = -1, complete = 1;

            if (app.backend->ops->changes != NULL) {
                nch = app.backend->ops->changes(app.backend, ch, 32,
                                                &complete);
            }
            nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(), ch, nch,
                                        complete);
        }
        (void)tiku_trk_poseview_refresh_values(fl->view, 0, 64);
        snprintf(app.status, sizeof app.status,
                 "poll: flags=%u, %d parked, %d rows", nf,
                 tiku_trk_poseview_parked(fl->view),
                 tiku_trk_poseview_count(fl->view));
    }
    if (shot_cascade) {
        static const char *dirs[] = { "/sys", "/sys/mem", "/sys/cpu",
                                      "/sys/timer", "/sys/clock",
                                      "/dev/gpio", "/dev/uart", "/dev/i2c",
                                      "/sys/watchdog", "/sys/power" };
        size_t k;

        for (k = 0; k < sizeof dirs / sizeof *dirs; k++) {
            (void)open_folder(&app, dirs[k]);
        }
    }
    if (shot_zoom) {
        folder_t *fl = &app.folders[app.folder_count - 1];
        const tiku_trk_column_t *cols;
        int nc = tiku_trk_poseview_columns(fl->view, &cols);
        int wide = 0, k;

        for (k = 0; k < nc; k++) { wide += cols[k].width; }
        (void)tiku_desk_workspace_zoom(app.desk, fl->win, wide + 8,
            tiku_trk_poseview_count(fl->view) * app.row_h + COUNT_H + 6);
    }
    if (shot_shift) {
        /* Open a pose context menu, then apply the modifier the way the key
         * handler does, so the shot shows the shifted faces. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 7, 0u);
        p = tiku_trk_poseview_at(fl->view, 7);
        c = menu_ctx(&app, fl, 0u);
        build_open_with(&app, fl);
        build_per_type(&app, fl);
        app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app.popup_frame = tiku_trk_menu_place(app.popup, 250, 150,
                              (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                  app.surf->h });
        c = menu_ctx(&app, fl, TIKU_TRK_MOD_SHIFT);
        tiku_trk_sc_update(app.popup, &c);
    }
    if (shot_menu) {
        /* Drive the real path -- select a pose, ask which context menu
         * belongs to it, place it -- so a screenshot shows what a click
         * would, not a mock-up. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 1, 0u);
        p = tiku_trk_poseview_at(fl->view, 1);
        c = menu_ctx(&app, fl, 0u);
        build_open_with(&app, fl);
        build_per_type(&app, fl);
        app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app.popup_frame = tiku_trk_menu_place(app.popup, 250, 160,
                              (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                  app.surf->h });
    }
    app.running = 1;
    app.applied_hide_dotfiles = -1;
    app.applied_full_path = -1;
    app.applied_folders_first = -1;
    app.dirty = 1;

    while (app.running) {
        {   /* The settings FILE is a doorway too: a change written by
             * anything -- another shell, a script, an editor -- lands in
             * the live model, the reload the browser shell already does
             * (TS-052, TS-047). */
            static time_t settings_mtime;
            static off_t settings_size;
            struct stat sst;

            if (app.prefs_window.path[0] != '\0' &&
                stat(app.prefs_window.path, &sst) == 0 &&
                (sst.st_mtime != settings_mtime ||
                 sst.st_size != settings_size)) {
                settings_mtime = sst.st_mtime;
                settings_size = sst.st_size;
                (void)tiku_trk_prefs_load(prefs_model(&app),
                                          app.prefs_window.path);
                app.dirty = 1;
            }
        }
        {   /* The scripting suite's transport (PVK-052): a command file
             * whose change delivers verb/property/specifier lines into
             * this running shell. */
            static int64_t command_stamp;
            struct stat cst;
            char cpath[TIKU_TRK_PATH_MAX];
            const char *chome = getenv("HOME");

            snprintf(cpath, sizeof cpath, "%s/.config/tracker/command",
                     (chome != NULL && chome[0] != '\0') ? chome : "");
            if (stat(cpath, &cst) == 0) {
                /* Nanoseconds, not seconds: two commands written within
                 * one second are two commands. */
                int64_t stamp = (int64_t)cst.st_mtim.tv_sec * 1000000000 +
                                cst.st_mtim.tv_nsec;

                if (stamp != command_stamp) {
                    command_stamp = stamp;
                    script_command_file(&app, cpath);
                }
            }
        }
        sync_folder_preferences(&app);
        sync_addons(&app, 0);
        for (;;) {
            tiku_desk_event_t ev;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (sc != NULL) {
                tiku_trk_script_adapter_result_t script_result =
                    tiku_trk_script_adapter_next(&app.script_adapter, sc,
                                                 &ev);

                if (script_result == TIKU_TRK_SCRIPT_ADAPTER_STOP) {
                    app.running = 0;
                    break;
                }
                if (script_result == TIKU_TRK_SCRIPT_ADAPTER_SHOT) {
                    draw_all(&app);
                    (void)tiku_desk_surface_png(
                        app.surf, app.script_adapter.shot_path);
                }
                have = script_result == TIKU_TRK_SCRIPT_ADAPTER_EVENT;
           } else if (tiku_desk_host_poll(app.host, &ev)) {
                have = 1;
            }
            if (!have) {
                break;
            }
            switch (ev.type) {
            case TIKU_DESK_EVENT_RESIZE:
                (void)resize_desktop(&app, ev.width, ev.height);
                app.dirty = 1;
                break;
            case TIKU_DESK_EVENT_EXPOSE:
                app.dirty = 1;
                break;
            case TIKU_DESK_EVENT_POINTER_DOWN: {
                if (ev.button == 1u && ev.modifiers == 0u) {
                    /* The resize strip: four pixels along the bar's
                     * inner edge, and the drag sets the short axis by
                     * hand (DB-050). */
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);
                    tiku_trk_db_dock_t dk =
                        tiku_trk_deskbar_dock(app.deskbar);
                    int inner_ok = 0;

                    if (dk == TIKU_TRK_DB_RIGHT_EXPANDED &&
                        ev.x >= bf.x && ev.x < bf.x + 4) {
                        inner_ok = 1;
                    }
                    if (dk == TIKU_TRK_DB_LEFT_EXPANDED &&
                        ev.x >= bf.x + bf.w - 4 && ev.x < bf.x + bf.w) {
                        inner_ok = 1;
                    }
                    if (inner_ok && ev.y >= bf.y && ev.y < bf.y + bf.h) {
                        app.bar_resize = 1;
                        app.bar_resize_x = ev.x;
                        app.bar_resize_w = tiku_trk_deskbar_width(
                            app.deskbar);
                        break;
                    }
                }
                if (ev.button == 1u &&
                    (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    /* Before anything else -- but ARMED, not decided: a
                     * CMD press that then moves re-docks the bar
                     * (DB-049); one that lets go where it landed is a
                     * chord the rows answer (DB-019/021). */
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.x >= bf.x &&
                        ev.x < bf.x + bf.w &&
                        ev.y >= bf.y &&
                        ev.y < bf.y + bf.h) {
                        app.dock_armed = 1;
                        app.dock_x = ev.x;
                        app.dock_y = ev.y;
                        app.dock_mods = ev.modifiers;
                        app.dock_button = ev.button;
                        break;
                    }
                }
                {   /* An info window takes its own clicks first. */
                    tiku_desk_window_t *iw = tiku_desk_workspace_focused(app.desk);
                    int islot = tiku_trk_info_windows_slot(&app.info_windows,
                                                           iw);
                    /* An open menu owns the next click wherever it lands,
                     * including over this window: choosing from the menu it
                     * just raised is not another click on the icon. */
                    if (islot >= 0 && !alert_model(&app)->open &&
                        app.popup == NULL) {
                        int which = -1;
                        tiku_trk_info_t *info = tiku_trk_info_windows_model(
                            &app.info_windows, islot);
                        tiku_trk_info_hit_t ih = tiku_trk_info_windows_hit(
                            &app.info_windows, islot, ev.x, ev.y, &which);

                        if (ih == TIKU_TRK_INFO_HIT_TAB && which >= 0) {
                            info->tab = (tiku_trk_info_tab_t)which;
                            if (info->tab ==
                                    TIKU_TRK_INFO_TAB_ATTRIBUTES) {
                                /* Re-read on the way in: an attribute
                                 * written since the window opened would
                                 * otherwise never appear. */
                                (void)tiku_trk_info_attrs(info, app.store);
                            }
                            app.dirty = 1;
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_NAME) {
                            tiku_trk_info_windows_begin_rename(
                                &app.info_windows, islot);
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_PERMISSION && which >= 0) {
                            char perr[160];

                            if (tiku_trk_info_perm_toggle(info,
                                    app.backend, which, perr,
                                    sizeof perr) != 0) {
                                tiku_trk_alert_open(alert_model(&app),
                                    TIKU_TRK_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_ERROR, perr,
                                    "Cancel");
                            }
                            app.dirty = 1;
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_ICON) {
                            if (ev.button == 3u) {
                                /* The icon stands for the thing the window
                                 * is about, so it answers a secondary click
                                 * with that thing's own menu -- built from
                                 * the MODEL, and carried out against its
                                 * path because there is no selection here
                                 * to read (AW-076). */
                                tiku_trk_model_t m;
                                tiku_trk_sc_ctx_t c =
                                    menu_ctx(&app, desktop_folder(&app), 0u);

                                if (app.backend->ops->stat(app.backend,
                                        info->path, &m) != 0) {
                                    break;
                                }
                                /* The menu acts on ONE NAMED THING: the
                                 * enable rules must see it as the
                                 * selection, whatever the desktop's
                                 * selection happens to be (AW-076). */
                                c.selection = 1;
                                c.selection_is_volume =
                                    m.kind == TIKU_TRK_KIND_VOLUME;
                                c.selection_ro =
                                    !tiku_trk_model_is_writable(&m);
                                c.can_trash_or_dup = !c.selection_ro;
                                c.can_edit_name =
                                    m.kind != TIKU_TRK_KIND_TRASH &&
                                    m.kind != TIKU_TRK_KIND_DESKTOP &&
                                    !c.selection_ro;
                                snprintf(app.popup_path,
                                         sizeof app.popup_path, "%s",
                                         info->path);
                                app.popup = tiku_trk_cwmenu_context(
                                    &app.menus, &m, &c);
                                /* A COPY: every folder draw rebuilds the
                                 * shared context menu, and the icon's
                                 * extra rows must survive the frame. */
                                app.menus.info_ctx = *app.popup;
                                app.popup = &app.menus.info_ctx;
                                /* The items the source adds for THIS menu
                                 * alone (AW-076): a folder recounts, a
                                 * link re-points. */
                                if (tiku_trk_model_is_container(&m) &&
                                    m.kind != TIKU_TRK_KIND_VOLUME &&
                                    m.kind != TIKU_TRK_KIND_ROOT) {
                                    (void)tiku_trk_menu_add(app.popup,
                                        "Recalculate folder size",
                                        TIKU_TRK_CMD_RECALC_SIZE, 0, 0u,
                                        1);
                                }
                                if (m.kind == TIKU_TRK_KIND_SYMLINK) {
                                    (void)tiku_trk_menu_add(app.popup,
                                        "Set new link target",
                                        TIKU_TRK_CMD_SET_LINK_TARGET, 0,
                                        0u, 1);
                                }
                                app.popup_sub = NULL;
                                app.popup_frame = tiku_trk_menu_place(
                                    app.popup, ev.x, ev.y,
                                    (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                        app.surf->h });
                                app.dirty = 1;
                                break;
                            }
                            /* Double-clicking the icon opens the item, as
                             * double-clicking its row would (AW-075). */
                            if (tiku_trk_now() - app.press_at < 500000) {
                                (void)open_folder(&app, info->path);
                                app.press_at = tiku_trk_now();
                                break;
                            }
                            app.press_at = tiku_trk_now();
                            /* And a press that moves carries the item
                             * itself -- except the Trash, which can only
                             * ever be opened (AW-075). */
                            if (ev.button == 1u &&
                                info->model.kind != TIKU_TRK_KIND_TRASH) {
                                char iparent[TIKU_TRK_PATH_MAX];
                                tiku_trk_model_t im;

                                if (app.backend->ops->stat(app.backend,
                                        info->path, &im) == 0) {
                                    parent_path(info->path, iparent,
                                                sizeof iparent);
                                    tiku_trk_drag_arm(&app.drag, ev.x, ev.y,
                                                      ev.button, 0u, 1, iw,
                                                      iparent);
                                    (void)tiku_trk_drag_add_at(&app.drag,
                                        info->path, im.type, ev.x, ev.y, 1);
                                    tiku_trk_drag_target_reset(
                                        &app.drag_target);
                                }
                            }
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_FIELD && which >= 0) {
                            char value[TIKU_TRK_INFO_VAL_MAX];
                            const char *label = "";
                            tiku_trk_info_field_t fk =
                                tiku_trk_info_field(info,
                                    app.store, which, &label, value,
                                    sizeof value);
                            char dir[TIKU_TRK_PATH_MAX];
                            char sel[TIKU_TRK_NAME_MAX];

                            if (fk == TIKU_TRK_INFO_F_OPENS_WITH) {
                                /* The pop-up as a walk: each click moves
                                 * to the next supporting application and
                                 * writes the preference at once
                                 * (AW-071). */
                                info_opens_with_next(&app, info);
                                app.dirty = 1;
                                break;
                            }
                            if (fk == TIKU_TRK_INFO_F_SIZE &&
                                tiku_trk_model_is_container(
                                    &info->model)) {
                                /* Clicking the size asks again: a folder
                                 * counted an hour ago is a guess (AW-068). */
                                tiku_trk_info_calc_start(info);
                            } else if (fk == TIKU_TRK_INFO_F_LOCATION &&
                                tiku_trk_info_location(info,
                                    dir, sizeof dir, sel, sizeof sel)) {
                                folder_t *nf = open_folder(&app, dir);

                                if (nf != NULL) {
                                    select_named(&app, nf, sel);
                                }
                            } else if (fk == TIKU_TRK_INFO_F_LINK) {
                                if (tiku_trk_info_link_target(
                                        info, app.backend, dir,
                                        sizeof dir, sel, sizeof sel)) {
                                    folder_t *nf = open_folder(&app, dir);

                                    if (nf != NULL) {
                                        select_named(&app, nf, sel);
                                    }
                                } else {
                                    /* Nowhere to follow: the window offers
                                     * a picker rather than an error, which
                                     * is the whole of AW-074's flow. */
                                    folder_t *ffl = folder_of(&app,
                                        tiku_desk_workspace_focused(app.desk));

                                    app.info_relink = islot;
                                    fpanel_open(&app, (ffl != NULL) ? ffl
                                                    : &app.folders[0],
                                                TIKU_TRK_CMD_GET_INFO);
                                }
                            }
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                {
                    tiku_desk_window_t *dbprefs =
                        tiku_trk_deskbar_prefs_window_handle(
                            &app.deskbar_prefs_window);

                    if (dbprefs != NULL && dbprefs->open &&
                        !alert_model(&app)->open) {
                        tiku_desk_event_t window_event = { 0 };

                        window_event.type = TIKU_DESK_EVENT_POINTER_DOWN;
                        window_event.time_us = tiku_trk_now();
                        window_event.x = ev.x;
                        window_event.y = ev.y;
                        window_event.button = ev.button;
                        if (tiku_desk_window_send(dbprefs, &window_event)) {
                            break;
                        }
                    }
                }
                {
                    tiku_desk_window_t *prefs_window =
                        tiku_trk_prefs_window_handle(&app.prefs_window);

                    if (prefs_window != NULL && prefs_window->open &&
                        !alert_model(&app)->open) {
                        tiku_desk_event_t window_event = { 0 };

                        window_event.type = TIKU_DESK_EVENT_POINTER_DOWN;
                        window_event.time_us = tiku_trk_now();
                        window_event.x = ev.x;
                        window_event.y = ev.y;
                        window_event.button = ev.button;
                        if (tiku_desk_window_send(prefs_window,
                                                  &window_event)) {
                            break;
                        }
                    }
                }
                if (tiku_trk_status_overlay_pointer(&app.status_overlay,
                                                    ev.x, ev.y)) {
                    break;
                }
                tiku_desk_window_t *w = NULL;
                tiku_desk_hit_t hit;
                tiku_trk_db_pick_t pick;

                /* A menu that is showing eats the next click wherever it
                 * lands: choosing an item, or dismissing. */
                if (app.nav_on && app.popup != NULL) {
                    /* A click inside a level chooses: a folder walks in,
                     * and anything else is not choosable, so the click is
                     * simply consumed (CW-056). */
                    const tiku_desk_font_t *nf = tiku_desk_font_plain();
                    int px2 = app.popup_frame.x + app.popup_frame.w - 4;
                    int py2 = app.popup_frame.y + 4;
                    int k, took = 0;

                    (void)nf;
                    for (k = 0; k < app.nav.depth && !took; k++) {
                        tiku_desk_rect_t r;
                        int at;

                        tiku_trk_nav_place(&app.nav, k, px2, py2,
                                           app.surf->w, app.surf->h,
                                           &r.x, &r.y, &r.w, &r.h);
                        at = tiku_trk_nav_item_at(&app.nav, k, r.x, r.y,
                                                  ev.x,
                                                  ev.y);
                        if (at >= 0) {
                            /* Levels deeper than the one clicked go: the
                             * walk follows the pointer rather than
                             * accumulating every branch visited. */
                            while (app.nav.depth > k + 1) {
                                (void)tiku_trk_nav_leave(&app.nav);
                            }
                            app.nav.level[k].sel = at;
                            if (app.nav.level[k].item[at].is_container) {
                                (void)tiku_trk_nav_enter(&app.nav, at);
                            }
                            took = 1;
                        }
                        px2 = r.x + r.w - 4;
                        py2 = r.y + 2 +
                              ((app.nav.level[k].sel >= 0)
                                   ? app.nav.level[k].sel : 0) *
                              TIKU_TRK_NAV_ROW;
                    }
                    if (took) {
                        app.dirty = 1;
                        break;
                    }
                }
                if (app.popup != NULL) {
                    tiku_desk_window_t *fw = tiku_desk_workspace_focused(app.desk);
                    folder_t *ffl = (app.bar_fl != NULL) ? app.bar_fl
                                  : (app.popup_fl != NULL) ? app.popup_fl
                                                           : folder_of(&app, fw);
                    int pick = -1;

                    if (app.popup_sub != NULL) {
                        pick = tiku_trk_menu_item_at(app.popup_sub,
                                                     app.popup_sub_frame,
                                                     ev.x,
                                                     ev.y);
                        if (pick >= 0) {
                            if (app.popup_path[0] != '\0') {
                                (void)tiku_trk_folder_file_actions_invoke_path(
                                    &app.file_actions, app.popup_path,
                                    app.popup_sub->item[pick].command);
                                app.dirty = 1;
                            } else {
                                menu_invoke(&app, ffl,
                                    app.popup_sub->item[pick].command);
                            }
                        }
                    }
                    if (pick < 0) {
                        pick = tiku_trk_menu_item_at(app.popup,
                                                     app.popup_frame,
                                                     ev.x,
                                                     ev.y);
                        if (pick >= 0 && app.popup->item[pick].enabled) {
                            if (app.popup->item[pick].submenu != NULL) {
                                /* Opening a submenu is not a choice; the
                                 * menu stays up. */
                                app.popup_sub = app.popup->item[pick].submenu;
                                app.popup_sub_frame =
                                    tiku_trk_menu_place_sub(app.popup, pick,
                                        app.popup_frame,
                                        (tiku_desk_rect_t){ 0, 0,
                                            app.surf->w, app.surf->h });
                                app.dirty = 1;
                                break;
                            }
                            if (app.popup_path[0] != '\0') {
                                int pc = app.popup->item[pick].command;
                                int islot = -1;
                                {
                                    int fi;

                                    for (fi = 0;
                                         fi < TIKU_TRK_INFO_WINDOW_MAX;
                                         fi++) {
                                        tiku_trk_info_t *im =
                                            &app.info_windows.model[fi];

                                        if (im->used &&
                                            strcmp(im->path,
                                                   app.popup_path) == 0) {
                                            islot = fi;
                                            break;
                                        }
                                    }
                                }

                                if (pc == TIKU_TRK_CMD_RECALC_SIZE &&
                                    islot >= 0) {
                                    tiku_trk_info_calc_start(
                                        &app.info_windows.model[islot]);
                                } else if (pc ==
                                               TIKU_TRK_CMD_SET_LINK_TARGET
                                           && islot >= 0) {
                                    /* The same picker AW-074's broken-
                                     * link flow raises. */
                                    folder_t *ffl2 = folder_of(&app,
                                        tiku_desk_workspace_focused(
                                            app.desk));

                                    app.info_relink = islot;
                                    fpanel_open(&app,
                                        (ffl2 != NULL) ? ffl2
                                                       : &app.folders[0],
                                        TIKU_TRK_CMD_GET_INFO);
                                } else {
                                    (void)tiku_trk_folder_file_actions_invoke_path(
                                        &app.file_actions, app.popup_path,
                                        pc);
                                }
                                app.dirty = 1;
                            } else {
                                menu_invoke(&app, ffl,
                                            app.popup->item[pick].command);
                            }
                        }
                    }
                    app.popup_path[0] = '\0';
                    app.popup_fl = NULL;
                    app.popup = NULL;
                    app.popup_sub = NULL;
                    app.bar_fl = NULL;
                    app.bar_open = -1;
                    app.menus.bar.open = -1;
                    app.dirty = 1;
                    break;
                }

                /* The Deskbar and its menu take precedence: it sits above
                 * every window, as a corner strip must. */
                if (tiku_trk_deskbar_menu_open(app.deskbar) ||
                    tiku_trk_deskbar_hit(app.deskbar, ev.x,
                                         ev.y)) {
                    int secondary = (ev.button == 3);
                    int dclicks = 1;

                    /* The Deskbar's own double-click: same spot, inside
                     * the interval -- and NOT refreshed when it fires,
                     * so a triple click cannot re-trigger (DB-019). */
                    if (ev.button == 1u &&
                        tiku_trk_now() - app.db_click_at < 500000 &&
                        ev.x - app.db_click_x <= 4 &&
                        app.db_click_x - ev.x <= 4 &&
                        ev.y - app.db_click_y <= 4 &&
                        app.db_click_y - ev.y <= 4) {
                        dclicks = 2;
                        app.db_click_at = 0;
                    } else if (ev.button == 1u) {
                        app.db_click_at = tiku_trk_now();
                        app.db_click_x = ev.x;
                        app.db_click_y = ev.y;
                    }
                    if (tiku_trk_deskbar_click(app.deskbar, ev.x,
                                               ev.y, ev.modifiers,
                                               (int)ev.button, dclicks,
                                               &pick)) {
                        if (ev.button == 1u && !secondary) {
                            app.deskbar_press = 1;
                            app.press_at = tiku_trk_now();
                            app.press_x = ev.x;
                            app.press_y = ev.y;
                        }
                        deskbar_pick_dispatch(&app, &pick,
                                              ev.x, ev.y);
                        app.dirty = 1;
                        break;
                    }
                }
                if (ev.button == 4u || ev.button == 5u) {
                    /* The wheel.  In an icon mode it moves the pixel
                     * origin; in a list it steps rows, which is what it
                     * has always done. */
                    tiku_desk_window_t *ww = NULL;
                    folder_t *wfl;
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.x >= bf.x &&
                        ev.x < bf.x + bf.w &&
                        ev.y >= bf.y &&
                        ev.y < bf.y + bf.h) {
                        /* Over the bar: the list scrolls rather than
                         * whatever window happens to be under it
                         * (DB-034). */
                        int wstep = ((ev.modifiers &
                                      (TIKU_DESK_MOD_SHIFT |
                                       TIKU_DESK_MOD_CMD |
                                       TIKU_DESK_MOD_OPTION)) != 0u)
                                        ? 3 : 1;

                        /* A modifier scrolls three times as fast, the
                         * wheel and the arrows alike (DB-034). */
                        if (tiku_trk_deskbar_scroll(app.deskbar,
                                (ev.button == 4u) ? -wstep : wstep)) {
                            app.dirty = 1;
                        }
                        break;
                    }
                    (void)tiku_desk_workspace_hit(app.desk, ev.x,
                                            ev.y, &ww);
                    wfl = folder_of(&app, ww);
                    if (wfl != NULL) {
                        int step = (ev.button == 4u) ? -1 : 1;

                        if (tiku_trk_poseview_mode(wfl->view) !=
                                TIKU_TRK_MODE_LIST) {
                            int ox = 0, oy = 0;

                            tiku_trk_poseview_origin(wfl->view, &ox, &oy);
                            tiku_trk_poseview_set_origin(wfl->view, ox,
                                oy + step * 3 * app.row_h);
                        } else {
                            tiku_trk_poseview_scroll_to(wfl->view,
                                tiku_trk_poseview_scroll(wfl->view) +
                                    step * 3);
                        }
                        app.dirty = 1;
                    }
                    break;
                }
                hit = tiku_desk_workspace_hit(app.desk, ev.x, ev.y,
                                        &w);
                if (tiku_trk_find_window_popup_open(&app.find_window)) {
                    /* Its menus hang below the panel's window and over
                     * whatever is behind it, so the click goes to the
                     * panel wherever it lands -- the panel eats it either
                     * way, which is what closes a menu clicked off. */
                    w = tiku_trk_find_window_handle(&app.find_window);
                    hit = TIKU_DESK_HIT_CONTENT;
                }
                if (w == NULL && desktop_folder(&app)->view != NULL) {
                    /* The backdrop is a pose area, so a click on it is a
                     * click in a view -- the same branch every window's
                     * content goes through, which is what keeps the
                     * Desktop's icons behaving like a folder's (AW-018). */
                    hit = TIKU_DESK_HIT_CONTENT;
                }
                if (w != NULL && ev.button == 1u) {
                    folder_t *bfl = folder_of(&app, w);

                    if (bfl != NULL) {
                        tiku_desk_rect_t bf = tiku_trk_folder_window_bar(bfl);
                        tiku_desk_rect_t fi =
                            tiku_trk_folder_window_icon(bfl);
                        int bi;

                        if (ev.x >= fi.x && ev.x < fi.x + fi.w &&
                            ev.y >= fi.y && ev.y < fi.y + fi.h) {
                            tiku_trk_model_t model;
                            char parent[TIKU_TRK_PATH_MAX];
                            char art[TIKU_TRK_NAME_MAX];

                            art[0] = '\0';

                            /* IV-057: the drag arms only from a pixel
                             * that is part of the PICTURE -- an icon is
                             * not a rectangle, and its transparent corner
                             * is not a handle. */
                            if (app.backend->ops->stat(app.backend, bfl->path,
                                                       &model) == 0) {
                                (void)tiku_trk_nodeicon_resolve(&app.nicons,
                                    app.store, &model, art, sizeof art);
                            }
                            if (art[0] != '\0' &&
                                tiku_trk_icon_hit(art, fi.w, ev.x - fi.x,
                                                  ev.y - fi.y)) {
                                parent_path(bfl->path, parent, sizeof parent);
                                tiku_desk_workspace_activate(app.desk, w);
                                app.press_at = tiku_trk_now();
                                app.press_x = ev.x;
                                app.press_y = ev.y;
                                app.press_row = -1;
                                app.press_fl = bfl;
                                app.press_down = 1;
                                app.press_menu = 0;
                                tiku_trk_drag_arm(&app.drag, ev.x, ev.y,
                                                  ev.button, 0u, 1, w,
                                                  parent);
                                (void)tiku_trk_drag_add_at(&app.drag,
                                    bfl->path, model.type, fi.x, fi.y, 1);
                                tiku_trk_drag_target_reset(&app.drag_target);
                                app.dirty = 1;
                                break;
                            }
                        }

                        build_open_with(&app, bfl);
                        build_per_type(&app, bfl);
                        {
                            tiku_trk_sc_ctx_t bc =
                                menu_ctx(&app, bfl, ev.modifiers);

                            tiku_trk_cwmenu_rebuild(&app.menus, &bc);
                        }
                        bi = tiku_trk_menubar_title_at(&app.menus.bar, bf,
                                ev.x, ev.y);
                        if (bi >= 0) {
                            commit_rename(&app, 1);
                            tiku_desk_workspace_activate(app.desk, w);
                            app.bar_fl = bfl;
                            app.bar_open = bi;
                            app.menus.bar.open = bi;
                            app.popup = app.menus.bar.menu[bi];
                            app.popup_sub = NULL;
                            app.popup_frame = tiku_trk_menubar_panel(
                                &app.menus.bar, bf,
                                (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                    app.surf->h });
                            app.nav_on = 0;
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (w != NULL && ev.button == 1u) {
                    /* The window's own scroll bar (PVL-051): arrows step,
                     * the track pages, the thumb is held. */
                    folder_t *sfl = folder_of(&app, w);
                    tiku_desk_rect_t sb = (sfl != NULL)
                        ? tiku_trk_folder_window_scrollbar(sfl)
                        : (tiku_desk_rect_t){ 0, 0, 0, 0 };

                    if (sfl != NULL && sb.w > 0 && ev.x >= sb.x &&
                        ev.x < sb.x + sb.w && ev.y >= sb.y &&
                        ev.y < sb.y + sb.h) {
                        int at = ev.y - sb.y;
                        int btn = sb.w;

                        if (at < btn) {
                            tiku_trk_scroll_arrow_press(&sfl->sbar, -1);
                            tiku_trk_poseview_scroll_to(sfl->view,
                                tiku_trk_poseview_scroll(sfl->view) -
                                    tiku_trk_scroll_line(&sfl->sbar));
                        } else if (at >= sb.h - btn) {
                            tiku_trk_scroll_arrow_press(&sfl->sbar, 1);
                            tiku_trk_poseview_scroll_to(sfl->view,
                                tiku_trk_poseview_scroll(sfl->view) +
                                    tiku_trk_scroll_line(&sfl->sbar));
                        } else {
                            tiku_trk_poseview_scroll_to(sfl->view,
                                tiku_trk_scroll_press(&sfl->sbar,
                                    sb.h - 2 * btn, at - btn));
                        }
                        app.sbar_fl = sfl;
                        app.dirty = 1;
                        break;
                    }
                }
                if (w != NULL && ev.button == 1u) {
                    /* The column strip: a press starts the one tracker
                     * every heading gesture lives behind -- click sorts,
                     * Shift-click seconds, an edge drags a new width, a
                     * title slides to reorder, and out of the strip tears
                     * the column off (TS-063). */
                    folder_t *tfl = folder_of(&app, w);
                    tiku_desk_rect_t title = (tfl != NULL)
                        ? tiku_trk_folder_window_title(tfl)
                        : (tiku_desk_rect_t){ 0, 0, 0, 0 };

                    if (tfl != NULL && title.w > 0 && ev.x >= title.x &&
                        ev.x < title.x + title.w && ev.y >= title.y &&
                        ev.y < title.y + title.h) {
                        unsigned tf;

                        commit_rename(&app, 1);
                        tiku_trk_poseview_cancel_edit_wait(tfl->view);
                        if (app.tstrip_fl != tfl) {
                            tiku_trk_titleview_init(&app.tstrip, tfl->view,
                                                    title.h);
                            app.tstrip_fl = tfl;
                        }
                        tf = tiku_trk_titleview_mouse_down(&app.tstrip,
                            ev.x - title.x, ev.y - title.y, 1,
                            w == tiku_desk_workspace_focused(app.desk),
                            tiku_trk_now());
                        if ((tf & TIKU_TRK_TT_ACTIVATE) != 0u) {
                            tiku_desk_workspace_activate(app.desk, w);
                        }
                        if (tiku_trk_titleview_tracking(&app.tstrip) !=
                                TIKU_TRK_TRACK_NONE) {
                            /* The press engaged a title or an edge.  A
                             * press in the strip's empty gutter is NOT the
                             * strip's: it falls through to the view, where
                             * it starts a band as it always has. */
                            app.strip_ptr_x = ev.x;
                            app.strip_ptr_y = ev.y;
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (w != NULL && ev.button == 3u) {
                    folder_t *tfl = folder_of(&app, w);
                    tiku_desk_rect_t title = (tfl != NULL)
                        ? tiku_trk_folder_window_title(tfl)
                        : (tiku_desk_rect_t){ 0, 0, 0, 0 };

                    if (tfl != NULL && ev.x >= title.x &&
                        ev.x < title.x + title.w && ev.y >= title.y &&
                        ev.y < title.y + title.h) {
                        tiku_trk_sc_ctx_t tc = menu_ctx(&app, tfl,
                                                         ev.modifiers);

                        build_open_with(&app, tfl);
                        build_per_type(&app, tfl);
                        tiku_trk_cwmenu_rebuild(&app.menus, &tc);
                        tiku_desk_workspace_activate(app.desk, w);
                        app.bar_fl = NULL;
                        app.bar_open = -1;
                        app.menus.bar.open = -1;
                        app.popup = &app.menus.attributes;
                        app.popup_sub = NULL;
                        app.popup_frame = tiku_trk_menu_place(app.popup,
                            ev.x, ev.y,
                            (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                app.surf->h });
                        app.press_down = 0;
                        app.dirty = 1;
                        break;
                    }
                }
                if (w != NULL && hit != TIKU_DESK_HIT_CONTENT) {
                    /* The CHROME activates a window; the content does not.
                     * That is what lets a click reach a list in a window
                     * behind another without the raise eating it -- the
                     * click-through a spatial desktop depends on. */
                    tiku_desk_workspace_activate(app.desk, w);
                }
                if (hit == TIKU_DESK_HIT_ZOOM) {
                    folder_t *fl = folder_of(&app, w);

                    if (fl != NULL) {
                        (void)tiku_desk_workspace_zoom(app.desk, w,
                                                 tiku_trk_folder_window_fit_width(fl),
                                                 tiku_trk_folder_window_fit_height(fl));
                    }
                    app.dirty = 1;
                    break;
                }
                if (hit == TIKU_DESK_HIT_CLOSE) {
                    /* Control+close closes every window rather than this
                     * one, which is the only way to clear a spatial desktop
                     * without visiting each window. */
                    if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                        int k;

                        for (k = 0; k < app.folder_count; k++) {
                            if (app.folders[k].win != NULL) {
                                tiku_desk_workspace_close(app.desk,
                                                    app.folders[k].win);
                            }
                        }
                        if (tiku_trk_find_window_handle(&app.find_window) !=
                                NULL) {
                            tiku_trk_find_window_close(&app.find_window);
                        }
                        app.dirty = 1;
                        break;
                    }
                    /* Option+close takes the folder's whole subtree with
                     * it: the windows a descent through a deep folder left
                     * behind are exactly the ones nobody wants to close one
                     * at a time (AW-012). */
                    if (ev.modifiers & TIKU_DESK_MOD_OPTION) {
                        folder_t *cf = folder_of(&app, w);

                        if (cf != NULL) {
                            close_windows_under(&app, cf->path);
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (hit == TIKU_DESK_HIT_CLOSE) {
                    close_window(&app, w);
                } else if (hit == TIKU_DESK_HIT_TAB ||
                           hit == TIKU_DESK_HIT_RESIZE) {
                    tiku_desk_workspace_begin_track(app.desk, w, hit, ev.x,
                                              ev.y);
                } else if (hit == TIKU_DESK_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_select_window_handle(
                                    &app.select_window)) {
                    (void)tiku_desk_window_send(w, &ev);
                } else if (hit == TIKU_DESK_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_panel_window_handle(
                                    &app.file_panel)) {
                    (void)tiku_desk_window_send(w, &ev);
                } else if (hit == TIKU_DESK_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_find_window_handle(
                                    &app.find_window) &&
                           ev.button == 1u &&
                           tiku_trk_find_window_icon_at(&app.find_window,
                                                        ev.x, ev.y)) {
                    /* Picking up the query icon (Q-034): the search
                     * travels with the pointer until it is dropped. */
                    app.qdrag_on = 1;
                    app.dirty = 1;
                } else if (hit == TIKU_DESK_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_find_window_handle(
                                    &app.find_window)) {
                    (void)tiku_desk_window_send(w, &ev);
                } else if (hit == TIKU_DESK_HIT_CONTENT) {
                    folder_t *fl = (w != NULL) ? folder_of(&app, w)
                                               : desktop_folder(&app);
                    if (fl != NULL && fl->view != NULL) {
                        if (ev.button == 1u &&
                            tiku_trk_folder_window_count_at(fl, ev.x, ev.y)) {
                            /* The desktop shell exposes the same parent
                             * hierarchy through the existing Open Parent
                             * action; a count-strip click starts at its
                             * nearest ancestor. */
                            menu_invoke(&app, fl, TIKU_TRK_CMD_OPEN_PARENT);
                            app.dirty = 1;
                            break;
                        }
                        int row = tiku_trk_folder_window_row_at(fl, ev.x,
                                                               ev.y);
                        unsigned mods = 0;
                        unsigned click_flags = 0;
                        int64_t now = tiku_trk_now();

                        if (ev.modifiers & TIKU_DESK_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                        if (ev.modifiers & TIKU_DESK_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                        if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                            click_flags |= TIKU_TRK_CLICK_EXTEND;
                        }
                        if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                            click_flags |= TIKU_TRK_CLICK_EXTEND |
                                           TIKU_TRK_CLICK_CONTROL;
                        }
                        if (ev.button == 3) {
                            int k, kn;
                            int lst = (tiku_trk_poseview_mode(fl->view) ==
                                       TIKU_TRK_MODE_LIST);

                            /* Normalize on DOWN, but do not build the menu
                             * until UP: a secondary press is still allowed
                             * to turn into a drag (PVS-016). */
                            tiku_trk_poseview_cancel_edit_wait(fl->view);
                            commit_rename(&app, 1);
                            (void)tiku_trk_poseview_context_click(fl->view,
                                                                  row);
                            tiku_desk_workspace_activate(app.desk, w);
                            app.press_at = now;
                            app.press_x = ev.x;
                            app.press_y = ev.y;
                            app.press_row = row;
                            app.press_fl = fl;
                            app.press_window = w;
                            app.press_focus =
                                tiku_desk_workspace_focused(app.desk);
                            app.press_down = 1;
                            app.press_menu = 0;
                            tiku_trk_drag_arm(&app.drag, ev.x,
                                              ev.y, 3u, mods, 1,
                                              w, fl->path);
                            {
                                tiku_trk_model_t source;
                                tiku_trk_drag_set_source_virtual(&app.drag,
                                    app.backend->ops->stat(app.backend,
                                        fl->path, &source) == 0 &&
                                    source.kind == TIKU_TRK_KIND_VIRTUAL_DIR);
                            }
                            kn = tiku_trk_poseview_count(fl->view);
                            for (k = 0; k < kn; k++) {
                                tiku_trk_pose_t *q =
                                    tiku_trk_poseview_at(fl->view, k);
                                int qx = 0, qy = 0, qw, qh;

                                if (q != NULL && q->selected) {
                                    if (!lst) {
                                        tiku_trk_poseview_pose_rect(fl->view,
                                            q, &qx, &qy, &qw, &qh);
                                    }
                                    (void)tiku_trk_drag_add_at(&app.drag,
                                        q->model.path, q->model.type, qx, qy,
                                        lst);
                                }
                            }
                            tiku_trk_drag_target_reset(&app.drag_target);
                            (void)tiku_trk_click_press(&click, NULL, 0, 0,
                                                       3u, 0u, now);
                        } else if (ev.button == 4 ||
                                   ev.button == 5) {
                            tiku_trk_poseview_scroll_to(fl->view,
                                tiku_trk_poseview_scroll(fl->view) +
                                ((ev.button == 4) ? -3 : 3));
                        } else {
                            tiku_trk_pose_t *clicked = (row >= 0)
                                ? tiku_trk_poseview_at(fl->view, row) : NULL;
                            int twice = tiku_trk_click_press(&click, clicked,
                                ev.x, ev.y,
                                ev.button, click_flags, now);

                            {
                                tiku_desk_rect_t c = tiku_trk_folder_window_content(fl);
                                int ox = 0, oy = 0;

                                tiku_trk_poseview_origin(fl->view, &ox, &oy);
                                tiku_trk_poseview_click_at(fl->view, row,
                                    ev.x - (c.x + 2) + ox,
                                    ev.y - (c.y + 2) + oy, mods);
                            }
                            app.press_at = now;
                            app.press_x = ev.x;
                            app.press_y = ev.y;
                            app.press_row = row;
                            app.press_fl = fl;
                            app.press_window = w;
                            app.press_focus =
                                tiku_desk_workspace_focused(app.desk);
                            app.press_down = 1;
                            app.press_menu = 0;
                            /* Armed on a pose, started only if the pointer
                             * then moves far enough (PVS-032).  The whole
                             * selection travels, not the pose under the
                             * pointer (PVS-033). */
                            tiku_trk_drag_arm(&app.drag, ev.x,
                                              ev.y,
                                              ev.button, mods,
                                              twice ? 2 : 1,
                                              w, fl->path);
                            {
                                tiku_trk_model_t source;
                                tiku_trk_drag_set_source_virtual(&app.drag,
                                    app.backend->ops->stat(app.backend,
                                        fl->path, &source) == 0 &&
                                    source.kind == TIKU_TRK_KIND_VIRTUAL_DIR);
                            }
                            if (row < 0 && ev.button == 1u) {
                                /* Empty space: a sweep, not a click.  In
                                 * EVERY mode -- a list's rows are swept by
                                 * the same rectangle, and this shell had
                                 * no band at all, so the only way to
                                 * gather several was one modifier click at
                                 * a time (PVS-024). */
                                unsigned bm = 0u;

                                if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                                    bm |= TIKU_TRK_MOD_SHIFT;
                                }
                                if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                                    bm |= TIKU_TRK_MOD_CMD;
                                }
                                if (bm == 0u) {
                                    /* Without a modifier the band starts
                                     * from nothing; with one it toggles on
                                     * top of what is there (PVS-027). */
                                    tiku_trk_poseview_select_none(fl->view);
                                }
                                /* ARMED, not started: a band begins only
                                 * once the pointer has moved past the
                                 * slop, so a click on empty space that
                                 * clears the selection does not also flash
                                 * a rectangle (PVS-024). */
                                tiku_trk_band_controller_arm(
                                    &app.band_controller, fl, ev.x, ev.y,
                                    bm);
                            /* A sweep between two poses also ENDS any
                                 * half-finished double click: without this
                                 * the click that starts the band is
                                 * invisible to the counter, and the next
                                 * click on the icon just left completes a
                                 * double click the user did not make. */
                                (void)tiku_trk_click_press(&click, NULL, 0,
                                    0, ev.button, 0u, now);
                                app.dirty = 1;
                                break;
                            }
                            if (row >= 0) {
                                int k, kn = tiku_trk_poseview_count(fl->view);
                                int lst = (tiku_trk_poseview_mode(fl->view) ==
                                           TIKU_TRK_MODE_LIST);

                                for (k = 0; k < kn; k++) {
                                    tiku_trk_pose_t *q =
                                        tiku_trk_poseview_at(fl->view, k);
                                    int qx = 0, qy = 0, qw, qh;

                                    if (q == NULL || !q->selected) {
                                        continue;
                                    }
                                    if (!lst) {
                                        /* Where it sat, so a drop can put
                                         * the selection down in the shape
                                         * it was picked up in (PVS-045). */
                                        tiku_trk_poseview_pose_rect(fl->view,
                                                                    q, &qx,
                                                                    &qy, &qw,
                                                                    &qh);
                                    }
                                    (void)tiku_trk_drag_add_at(
                                        &app.drag, q->model.path,
                                        q->model.type, qx, qy, lst);
                                }
                            }
                            tiku_trk_drag_target_reset(&app.drag_target);
                            if (row >= 0 && tiku_trk_folder_window_name_at(
                                    fl, row, ev.x, ev.y)) {
                                /* Armed, not started: the rename begins only
                                 * once the double-click window passes with
                                 * no further click (PVS-014, MA-043). */
                                (void)tiku_trk_poseview_name_clicked(fl->view,
                                                                     row, now);
                            }
                            if (twice) {
                                tiku_trk_pose_t *p =
                                    tiku_trk_poseview_at(fl->view, row);
                                if (p != NULL &&
                                    tiku_trk_folder_window_column_at(fl,
                                        ev.x) == TIKU_TRK_COL_PATH) {
                                    /* PVS-011: a double click INSIDE the
                                     * Location column opens the folder the
                                     * path names -- the column's answer,
                                     * not the row's. */
                                    char parent[TIKU_TRK_PATH_MAX];
                                    char *slash;

                                    snprintf(parent, sizeof parent, "%s",
                                             p->model.path);
                                    slash = strrchr(parent, '/');
                                    if (slash != NULL) {
                                        if (slash == parent) {
                                            parent[1] = '\0';
                                        } else {
                                            *slash = '\0';
                                        }
                                        (void)open_folder(&app, parent);
                                    }
                                } else if (p != NULL &&
                                    tiku_trk_model_is_container(&p->model)) {
                                    /* A folder opens its OWN window, which
                                     * is what makes this spatial rather
                                     * than a single browser. */
                                    (void)open_folder(&app, p->model.path);
                                    if (ev.modifiers & TIKU_DESK_MOD_OPTION) {
                                        /* PVK-018: Option-open closes the
                                         * window it came from, which is how
                                         * a spatial desktop is walked
                                         * without leaving a trail. */
                                        close_window(&app, w);
                                    }
                                } else if (p != NULL &&
                                           tiku_trk_poseview_is_query(
                                               fl->view)) {
                                    /* PVS-011: a result is identified by its
                                     * PATH here, and the useful thing to do
                                     * with a path is open what holds it --
                                     * the result itself is a value, not a
                                     * place. */
                                    char parent[TIKU_TRK_PATH_MAX];
                                    char *slash;

                                    snprintf(parent, sizeof parent, "%s",
                                             p->model.path);
                                    slash = strrchr(parent, '/');
                                    if (slash != NULL) {
                                        if (slash == parent) {
                                            parent[1] = '\0';
                                        } else {
                                            *slash = '\0';
                                        }
                                        (void)open_folder(&app, parent);
                                    }
                                } else if (p != NULL &&
                                           p->model.facts.link_broken) {
                                    offer_broken_link(&app, p->model.path);
                                } else if (p != NULL) {
                                    /* Not a container: it is LAUNCHED, in
                                     * whatever opens its type -- which is
                                     * what a double click has always meant
                                     * and this shell could not do (AW-030,
                                     * PVS-010). */
                                    const char *one[1];

                                    int went;

                                    one[0] = p->model.path;
                                    went = launch_paths(&app,
                                              handler_for(&app, &p->model),
                                              one, 1);
                                    if ((ev.modifiers & TIKU_DESK_MOD_OPTION) &&
                                        went) {
                                        /* Option-open closes the window it
                                         * came from, once the launch has
                                         * actually happened (AW-032). */
                                        close_window(&app, w);
                                    }
                                }
                            }
                        }
                    }
                }
                app.dirty = 1;
                break;
            }
            case TIKU_DESK_EVENT_POINTER_MOVE:
                app.ptr_x = ev.x;
                app.ptr_y = ev.y;
                if (app.qdrag_on) {
                    app.dirty = 1;
                }
                if (tiku_trk_deskbar_motion(app.deskbar, ev.x, ev.y)) {
                    app.dirty = 1;
                }
                if (app.popup != NULL) {
                    /* The hot row follows the pointer, in the level it is
                     * over: without this a menu gives no sign of which
                     * item a pick would take. */
                    int menu_moved = 0;

                    menu_moved |= tiku_trk_menu_track(app.popup,
                                                      app.popup_frame,
                                                      ev.x, ev.y);
                    if (app.popup_sub != NULL) {
                        menu_moved |= tiku_trk_menu_track(app.popup_sub,
                                                          app.popup_sub_frame,
                                                          ev.x, ev.y);
                    }
                    if (menu_moved) {
                        app.dirty = 1;
                    }
                }
                if (app.deskbar_press &&
                    (ev.x - app.press_x > 4 || app.press_x - ev.x > 4 ||
                     ev.y - app.press_y > 4 || app.press_y - ev.y > 4)) {
                    app.deskbar_press = 0;
                }
                if (app.press_down &&
                    (ev.x - app.press_x > 4 ||
                     app.press_x - ev.x > 4 ||
                     ev.y - app.press_y > 4 ||
                     app.press_y - ev.y > 4)) {
                    /* Moved out of the long-press radius: this is a drag,
                     * not a held click, so neither the menu nor the rename
                     * is owed (PVS-015, PVS-017). */
                    if (app.press_fl != NULL) {
                        tiku_trk_poseview_cancel_edit_wait(app.press_fl->view);
                    }
                    app.press_down = 0;
                }
                if (app.sbar_fl != NULL && app.sbar_fl->sbar.tracking) {
                    tiku_desk_rect_t sb =
                        tiku_trk_folder_window_scrollbar(app.sbar_fl);

                    tiku_trk_poseview_scroll_to(app.sbar_fl->view,
                        tiku_trk_scroll_drag(&app.sbar_fl->sbar,
                            sb.h - 2 * sb.w, ev.y - sb.y - sb.w));
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_titleview_tracking(&app.tstrip) !=
                        TIKU_TRK_TRACK_NONE && app.tstrip_fl != NULL) {
                    tiku_desk_rect_t st =
                        tiku_trk_folder_window_title(app.tstrip_fl);
                    unsigned tf = tiku_trk_titleview_mouse_moved(
                        &app.tstrip, ev.x - st.x, ev.y - st.y,
                        tiku_trk_now());

                    app.strip_ptr_x = ev.x;
                    app.strip_ptr_y = ev.y;
                    if ((tf & (TIKU_TRK_TT_REDRAW | TIKU_TRK_TT_LINE |
                               TIKU_TRK_TT_NO_LINE)) != 0u) {
                        app.dirty = 1;
                    }
                    break;
                }
                if (tiku_trk_drag_moved(&app.drag, ev.x, ev.y) ||
                    tiku_trk_drag_active(&app.drag)) {
                    if (app.spring_nav_on) {
                        spring_nav_track(&app, ev.x, ev.y);
                    }
                    (void)tiku_trk_drag_target_motion(&app.drag_target,
                                                      ev.x, ev.y,
                                                      tiku_trk_now());
                    /* Read at the badge, so the copy indication follows
                     * the modifier live, as the cursor did (PVS-036). */
                    if (app.drag_live_mods != ev.modifiers) {
                        app.drag_live_mods = ev.modifiers;
                        app.dirty = 1;
                    }
                }
                {
                    /* An Info window's icon answers a hovering drag with
                     * its selected variant (IV-058). */
                    int hot = tiku_trk_drag_active(&app.drag)
                                  ? info_icon_drop_slot(&app, ev.x, ev.y)
                                  : -1;

                    if (hot != app.info_windows.drop_hot) {
                        app.info_windows.drop_hot = hot;
                        app.dirty = 1;
                    }
                }
                {
                    /* The pointer resting on a truncated Info field opens
                     * the expanding pop-up; leaving takes it away
                     * (AW-073). */
                    tiku_desk_window_t *hw = NULL;
                    int hslot = -1, hfield = -1, hwhich = -1;

                    if (!tiku_trk_drag_active(&app.drag)) {
                        (void)tiku_desk_workspace_hit(app.desk, ev.x, ev.y,
                                                      &hw);
                        hslot = tiku_trk_info_windows_slot(
                            &app.info_windows, hw);
                        if (hslot >= 0 &&
                            hw == tiku_desk_workspace_focused(app.desk) &&
                            tiku_trk_info_windows_hit(&app.info_windows,
                                hslot, ev.x, ev.y, &hwhich) ==
                                TIKU_TRK_INFO_HIT_FIELD) {
                            hfield = hwhich;
                        } else {
                            hslot = -1;
                        }
                    }
                    if (hslot != app.info_windows.hover_slot ||
                        hfield != app.info_windows.hover_field) {
                        app.info_windows.hover_slot = hslot;
                        app.info_windows.hover_field = hfield;
                        app.dirty = 1;
                    }
                }
                {
                    unsigned bm = 0u;

                    if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                        bm |= TIKU_TRK_MOD_SHIFT;
                    }
                    if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                        bm |= TIKU_TRK_MOD_CMD;
                    }
                    (void)tiku_trk_band_controller_motion(
                        &app.band_controller, ev.x, ev.y, bm);
                }
                if (app.bar_resize) {
                    int dw = (tiku_trk_deskbar_dock(app.deskbar) ==
                              TIKU_TRK_DB_RIGHT_EXPANDED)
                                 ? app.bar_resize_x - ev.x
                                 : ev.x - app.bar_resize_x;

                    if (tiku_trk_deskbar_set_width(app.deskbar,
                            app.bar_resize_w + dw) > 0) {
                        app.dirty = 1;
                    }
                }
                if (app.dock_armed &&
                    ((ev.x - app.dock_x) * (ev.x - app.dock_x) +
                     (ev.y - app.dock_y) * (ev.y - app.dock_y) >
                     TIKU_TRK_DRAG_SLOP * TIKU_TRK_DRAG_SLOP)) {
                    app.dock_armed = 0;
                    app.dock_drag = 1;
                }
                if (app.dock_drag) {
                    /* Ctrl held on the bar: the pointer IS the picker.
                     * Where it lets go decides the dock, and a release in
                     * the middle keeps the one it started from (DB-049). */
                        tiku_trk_db_dock_t want =
                        tiku_trk_deskbar_dock_at(ev.x, ev.y,
                            app.surf->w, app.surf->h,
                            tiku_trk_deskbar_dock(app.deskbar));

                    if (want != tiku_trk_deskbar_dock(app.deskbar)) {
                        tiku_trk_deskbar_set_dock(app.deskbar, want);
                        app.dirty = 1;
                    }
                }
                if (tiku_trk_deskbar_pointer(app.deskbar, ev.x,
                                             ev.y)) {
                    /* The one input the bar never had.  Auto-hide and
                     * auto-raise are both answers to "where is the
                     * pointer", and neither could be asked (DB-051,
                     * DB-052). */
                    app.dirty = 1;
                }
                if (tiku_desk_workspace_tracking(app.desk)) {
                    if (tiku_desk_workspace_track(app.desk, ev.x,
                                            ev.y)) {
                        app.dirty = 1;
                    }
                }
                break;
            case TIKU_DESK_EVENT_POINTER_UP: {
                int dropped = tiku_trk_drag_active(&app.drag);

                if (app.qdrag_on) {
                    /* The query icon lands (Q-034): over a folder window
                     * or the desktop, the drop menu asks what to make of
                     * it; back on the Find window it is just put down. */
                    tiku_desk_window_t *tw = NULL;
                    folder_t *tfl;

                    (void)tiku_desk_workspace_hit(app.desk, ev.x, ev.y,
                                                  &tw);
                    tfl = (tw != NULL) ? folder_of(&app, tw)
                                       : desktop_folder(&app);
                    app.qdrag_on = 0;
                    if (tfl != NULL && tfl->view != NULL &&
                        (tw == NULL ||
                         tw != tiku_trk_find_window_handle(
                                   &app.find_window))) {
                        tiku_trk_model_t dm;
                        int ro = 1;

                        if (app.backend->ops->stat(app.backend, tfl->path,
                                                   &dm) == 0) {
                            ro = !tiku_trk_model_is_mutable_container(&dm);
                        }
                        snprintf(app.qdrop_dest, sizeof app.qdrop_dest,
                                 "%s", tfl->path);
                        app.popup = tiku_trk_cwmenu_query_drop(&app.menus,
                                                               ro);
                        app.popup_fl = tfl;
                        app.popup_sub = NULL;
                        app.popup_frame = tiku_trk_menu_place(app.popup,
                            ev.x, ev.y,
                            (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                app.surf->h });
                    }
                    app.dirty = 1;
                    break;
                }

                if (tiku_trk_titleview_tracking(&app.tstrip) !=
                        TIKU_TRK_TRACK_NONE && app.tstrip_fl != NULL) {
                    tiku_desk_rect_t st =
                        tiku_trk_folder_window_title(app.tstrip_fl);
                    unsigned tf = tiku_trk_titleview_mouse_up(&app.tstrip,
                        ev.x - st.x, ev.y - st.y,
                        (ev.modifiers & TIKU_DESK_MOD_SHIFT) != 0,
                        tiku_trk_now());

                    (void)tf;
                    app.dirty = 1;
                    break;
                }

                app.dock_drag = 0;
                app.bar_resize = 0;
                if (app.dock_armed) {
                    /* The press never moved: it was the chord after all
                     * (DB-019/020/021). */
                    tiku_trk_db_pick_t hpick;

                    app.dock_armed = 0;
                    if (tiku_trk_deskbar_click(app.deskbar, app.dock_x,
                                               app.dock_y, app.dock_mods,
                                               (int)app.dock_button, 1,
                                               &hpick)) {
                        deskbar_pick_dispatch(&app, &hpick, app.dock_x,
                                              app.dock_y);
                        app.dirty = 1;
                    }
                }
                app.deskbar_press = 0;
                if (tiku_trk_deskbar_release(app.deskbar, ev.x, ev.y)) {
                    app.dirty = 1;
                }
                (void)tiku_trk_band_controller_finish(&app.band_controller);
                if (dropped && app.spring_nav_on) {
                    /* Choosing a spring entry IS the drop (PVS-051): the
                     * chosen row is where the items land, a disabled row
                     * swallows the release, and a release outside the
                     * menu falls back to the drop under the pointer. */
                    char sdest[TIKU_TRK_PATH_MAX];
                    int got = spring_nav_hit(&app, ev.x, ev.y, sdest,
                                             sizeof sdest);

                    spring_nav_close(&app);
                    if (got == 1) {
                        tiku_trk_drop_controller_execute_into(
                            &app.drop_controller, sdest, ev.x, ev.y,
                            ev.modifiers, ev.button, 0);
                    } else if (got == 0) {
                        do_drop(&app, ev.x, ev.y, ev.modifiers, ev.button);
                    }
                } else if (dropped && drop_on_panel(&app, ev.x, ev.y)) {
                    /* Consumed by the panel, action or not (AW-059). */
                } else if (dropped && drop_on_info_icon(&app, ev.x, ev.y,
                                                 ev.modifiers)) {
                    /* Taken by an Info window's icon: the folder drop
                     * machinery must not run a second time. */
                } else if (dropped) {
                    do_drop(&app, ev.x, ev.y,
                            ev.modifiers, ev.button);
                } else if (ev.button == 3u && app.press_down &&
                           !app.press_menu && app.press_fl != NULL) {
                    folder_t *pfl = app.press_fl;
                    tiku_trk_pose_t *p = (app.press_row >= 0)
                        ? tiku_trk_poseview_at(pfl->view, app.press_row)
                        : NULL;
                    unsigned cmods = 0u;
                    tiku_trk_sc_ctx_t c;

                    if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                        cmods |= TIKU_TRK_MENU_MOD_SHIFT;
                    }
                    if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                        cmods |= TIKU_TRK_MENU_MOD_CMD;
                    }
                    c = menu_ctx(&app, pfl, cmods);
                    app.popup_fl = pfl;

                    build_open_with(&app, pfl);
                    build_per_type(&app, pfl);
                    app.popup = tiku_trk_cwmenu_context(&app.menus,
                                        (p != NULL) ? &p->model : NULL, &c);
                    app.bar_fl = NULL;
                    app.bar_open = -1;
                    app.menus.bar.open = -1;
                    app.nav_on = 0;
                    if (p != NULL && tiku_trk_nav_wanted(&p->model)) {
                        tiku_trk_nav_open(&app.nav, app.backend,
                                          p->model.path);
                        app.nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(&app),
                                                       "HideDotFiles");
                        app.nav.folders_first = tiku_trk_prefs_get(prefs_model(&app),
                                                   "SortFolderNamesFirst");
                        app.nav_on = 1;
                    }
                    app.popup_sub = NULL;
                    app.popup_frame = tiku_trk_menu_place(app.popup,
                        ev.x, ev.y,
                        (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                            app.surf->h });
                    app.dirty = 1;
                }
                if (!dropped && app.press_down && !app.press_menu &&
                    app.press_window != NULL &&
                    tiku_desk_workspace_focused(app.desk) ==
                        app.press_focus) {
                    /* Content clicks raise on release, while movement has
                     * already cleared press_down, leaving drag-out from a
                     * background window click-through.  A click that opened
                     * something is exempt: the raise would put the window
                     * clicked in back over the window it just opened. */
                    tiku_desk_workspace_activate(app.desk,
                                                  app.press_window);
                }
                if (app.sbar_fl != NULL) {
                    tiku_trk_scroll_release(&app.sbar_fl->sbar);
                    app.sbar_fl = NULL;
                }
                tiku_trk_drag_reset(&app.drag);
                tiku_trk_drag_target_reset(&app.drag_target);
                app.info_windows.drop_hot = -1;
                app.info_windows.hover_slot = -1;
                app.info_windows.hover_field = -1;
                /* A release ends whatever the press began -- a drag with a
                 * picture under the pointer, a band, a drop that raised an
                 * alert.  All of it is on the screen until a frame says
                 * otherwise, and a shell with no pointer moving over it
                 * gets no other reason to draw one. */
                app.dirty = 1;
                /* A long press already put the menu up, so its release must
                 * not put a second one up (PVS-017). */
                app.press_down = 0;
                app.press_menu = 0;
                app.press_window = NULL;
                app.press_focus = NULL;
                tiku_desk_workspace_end_track(app.desk);
                break;
            }
            case TIKU_DESK_EVENT_KEY_UP:
                if (ev.type == TIKU_DESK_EVENT_KEY_DOWN ||
                    ev.type == TIKU_DESK_EVENT_POINTER_DOWN ||
                    ev.type == TIKU_DESK_EVENT_POINTER_MOVE) {
                    /* The user did something: idle-only work starts its
                     * wait again (AW-085). */
                    tiku_trk_loop_note_activity(app.loop);
                }
                if (tiku_trk_switcher_window_shown(&app.switcher_window) &&
                    ev.key == TIKU_DESK_KEY_CMD) {
                    /* Released inside the hair trigger the user meant the
                     * previous application; held longer they meant the
                     * list, and letting go now is the choice (DB-054). */
                    (void)tiku_trk_switcher_window_release(
                        &app.switcher_window, tiku_trk_now());
                    tiku_trk_switcher_window_close(&app.switcher_window, 1);
                    break;
                }
                /* fall through */
            case TIKU_DESK_EVENT_KEY_DOWN: {
                /* A menu that is open rewrites itself as modifiers move:
                 * nine of its items change label AND command together while
                 * Shift is held.  The table already knows how; this is the
                 * edge that tells it to.  Handled on BOTH key edges, since
                 * releasing Shift has to put the labels back. */
                if (app.popup != NULL) {
                    unsigned mk = ev.key;

                    if (mk == TIKU_DESK_KEY_SHIFT ||
                        mk == TIKU_DESK_KEY_CMD) {
                        tiku_desk_window_t *fw = tiku_desk_workspace_focused(app.desk);
                        folder_t *ffl = folder_of(&app, fw);
                        unsigned m2 = 0;
                        tiku_trk_sc_ctx_t c2;

                        /* TIKU_DESK_EVENT_KEY_UP reports the state BEFORE the release,
                         * so the bit has to be taken out by hand or the menu
                         * stays in its shifted face. */
                        if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                            m2 |= TIKU_TRK_MOD_SHIFT;
                        }
                        if (ev.type == TIKU_DESK_EVENT_KEY_UP &&
                            mk == TIKU_DESK_KEY_SHIFT) {
                            m2 &= ~(unsigned)TIKU_TRK_MOD_SHIFT;
                        }
                        if (ev.type == TIKU_DESK_EVENT_KEY_DOWN &&
                            mk == TIKU_DESK_KEY_SHIFT) {
                            m2 |= TIKU_TRK_MOD_SHIFT;
                        }
                        c2 = menu_ctx(&app, ffl, m2);
                        tiku_trk_sc_update(app.popup, &c2);
                        if (app.popup_sub != NULL) {
                            tiku_trk_sc_update(app.popup_sub, &c2);
                        }
                        app.dirty = 1;
                    }
                    break;
                }
                if (ev.type == TIKU_DESK_EVENT_KEY_UP) {
                    break;
                }
                {
                unsigned key = ev.key;
                tiku_desk_window_t *w = tiku_desk_workspace_focused(app.desk);
                folder_t *fl = folder_of(&app, w);
                unsigned mods = 0;

                if (ev.modifiers & TIKU_DESK_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                if (ev.modifiers & TIKU_DESK_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                if (key == TIKU_DESK_KEY_ESCAPE &&
                    tiku_trk_deskbar_calendar_open(app.deskbar)) {
                    /* Escape is a focus-loss equivalent for the small clock
                     * popups; it must not close a folder or edit behind it. */
                    tiku_trk_deskbar_close_calendar(app.deskbar);
                    app.dirty = 1;
                    break;
                }
                /* Get Info owns its rename editor and window shortcuts. */
                if (tiku_trk_info_windows_key(&app.info_windows, key,
                                              ev.modifiers,
                                              key_char(&ev))) {
                    break;
                }
                if (tiku_trk_panel_window_key(&app.file_panel, key,
                                              ev.modifiers,
                                              key_char(&ev))) {
                    break;
                }
                if (tiku_desk_workspace_focused(app.desk) ==
                        tiku_trk_find_window_handle(&app.find_window) &&
                    app.find_window.model.focus ==
                        TIKU_TRK_FIND_FOCUS_NAME &&
                    key == ' ' && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    /* Cmd-Space completes the name against the first
                     * scoped place, or home when the search is everywhere
                     * (NeXT UIG ch3). */
                    tiku_trk_find_t *fm = &app.find_window.model;
                    char dir[TIKU_TRK_PATH_MAX];
                    char full[TIKU_TRK_NAME_MAX];
                    const char *home = getenv("HOME");
                    int pi, chosen = -1;

                    for (pi = 0; pi < fm->place_count; pi++) {
                        if (fm->place_mask & (1u << pi)) {
                            chosen = pi;
                            break;
                        }
                    }
                    if (chosen >= 0) {
                        snprintf(dir, sizeof dir, "/%s", fm->place[chosen]);
                    } else {
                        snprintf(dir, sizeof dir, "%s",
                                 (home != NULL && home[0] != '\0') ? home
                                                                    : "/");
                    }
                    if (tiku_trk_panel_complete(app.backend, dir,
                            fm->name_text, full, sizeof full) > 0) {
                        snprintf(fm->name_text, sizeof fm->name_text,
                                 "%s", full);
                    }
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_find_window_key(&app.find_window, key,
                                             key_char(&ev))) {
                    break;
                }
                if (app.finding) {
                    if (key == TIKU_DESK_KEY_RETURN) {
                        app.finding = 0;
                        run_find(&app, fl);
                    } else if (key == TIKU_DESK_KEY_ESCAPE) {
                        app.finding = 0;
                        app.status[0] = '\0';
                        app.dirty = 1;
                    } else if (key == TIKU_DESK_KEY_BACKSPACE) {
                        if (app.find_len > 0) {
                            app.find_buf[--app.find_len] = '\0';
                        }
                        app.dirty = 1;
                    } else {
                        char typed = key_char(&ev);

                        if (typed >= 0x20 && typed < 0x7F &&
                            app.find_len < (int)sizeof app.find_buf - 1) {
                            app.find_buf[app.find_len++] = typed;
                            app.find_buf[app.find_len] = '\0';
                            app.dirty = 1;
                        }
                    }
                    break;
                }
                if (tiku_trk_select_window_key(&app.select_window, key,
                                               ev.modifiers,
                                               key_char(&ev))) {
                    break;
                }
                if (tiku_trk_open_with_window_key(&app.open_with_window,
                                                  key, ev.modifiers)) {
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_folder_edit_key(&app.folder_edit, fl, key,
                        ev.modifiers, key_char(&ev))) {
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    tiku_desk_workspace_focused(app.desk) ==
                        tiku_trk_prefs_window_handle(&app.prefs_window)) {
                    tiku_trk_prefs_window_close(&app.prefs_window);
                    app.dirty = 1;
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    tiku_desk_workspace_focused(app.desk) ==
                        tiku_trk_deskbar_prefs_window_handle(
                            &app.deskbar_prefs_window)) {
                    /* Deskbar preferences close; the desktop stays: quit
                     * belongs to the desktop, not to a settings window
                     * (DB-053). */
                    tiku_trk_deskbar_prefs_window_close(
                        &app.deskbar_prefs_window);
                    app.dirty = 1;
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    app.running = 0;
                    break;
                }
                {
                    unsigned addon_mods = 0u;
                    int addon_command;

                    if (ev.modifiers & TIKU_DESK_MOD_OPTION) addon_mods |= 0x1u;
                    if (ev.modifiers & TIKU_DESK_MOD_CMD) addon_mods |= 0x2u;
                    addon_command = tiku_trk_addons_shortcut(
                        &app.addons, key_char(&ev), addon_mods,
                        TIKU_TRK_ADDON_COMMAND_BASE);
                    if (addon_command >= 0) {
                        menu_invoke(&app, fl, addon_command);
                        app.dirty = 1;
                        break;
                    }
                }
                if (key == 'f' && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    open_panel(&app);
                    break;
                }
                if (key == 'a' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    (ev.modifiers & TIKU_DESK_MOD_SHIFT)) {
                    /* PVK-047: Select by pattern, on the window it was
                     * invoked from, opened near the pointer. */
                    int wx, wy;

                    if (!tiku_desk_host_pointer(app.host, &wx, &wy, NULL)) {
                        wx = app.surf->w / 2;
                        wy = app.surf->h / 2;
                    }
                    tiku_trk_select_window_open(&app.select_window, fl,
                                                wx, wy);
                    break;
                }
                if (key == 'a' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    tiku_trk_poseview_select_all(fl->view);
                    app.dirty = 1;
                    break;
                }
                if (key == 'u' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    /* Cmd+U, which the volume menus advertise (CW-034). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_UNMOUNT);
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_DESK_KEY_ESCAPE && fl != NULL &&
                    !alert_model(&app)->open) {
                    /* The alert's own Escape lives in the overlay, later
                     * in this chain; this one is the clipboard's
                     * (PVK-034). */
                    if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                        /* Shift+Escape removes the selection from the
                         * clipboard, or every pose in the folder when
                         * nothing is selected. */
                        int ci, cn = tiku_trk_poseview_count(fl->view);
                        int any =
                            tiku_trk_poseview_selection_count(fl->view) > 0;

                        for (ci = 0; ci < cn; ci++) {
                            tiku_trk_pose_t *p =
                                tiku_trk_poseview_at(fl->view, ci);

                            if (p != NULL && (!any || p->selected)) {
                                (void)tiku_trk_clip_remove(&app.clip,
                                                           &p->model, 1);
                            }
                        }
                        app.dirty = 1;
                        break;
                    }
                    if (tiku_trk_clip_count(&app.clip) > 0) {
                        /* Escape aborts a pending cut before anything
                         * else it means. */
                        tiku_trk_clip_clear(&app.clip);
                        app.dirty = 1;
                        break;
                    }
                    /* Nothing pending: fall through for whoever else
                     * answers to Escape. */
                }
                if (key == TIKU_DESK_KEY_DELETE && fl == NULL) {
                    /* No window focused: the key speaks to the Desktop's
                     * own selection, as it does in Tracker. */
                    fl = desktop_folder(&app);
                }
                if (key == TIKU_DESK_KEY_DELETE && fl != NULL) {
                    /* What the key MEANS depends on where the items are: a
                     * move that can be undone where there is a Trash to
                     * move them to, and a deletion inside it or when Shift
                     * says the user has already decided (PVK-038).  The
                     * key asks the same controller the menu item does, so
                     * the question a delete puts up is one rule and not
                     * one per way of reaching it (FS-046). */
                    menu_invoke(&app, fl,
                        (ev.modifiers & TIKU_DESK_MOD_SHIFT)
                            ? TIKU_TRK_CMD_DELETE
                            : TIKU_TRK_CMD_MOVE_TO_TRASH);
                    app.dirty = 1;
                    break;
                }
                if (key == 'g' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    /* Cmd+G, which the menus now advertise (CW-033). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_EDIT_QUERY);
                    app.dirty = 1;
                    break;
                }
                if (key == 'i' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    /* The chord the menus have always advertised; until
                     * now nothing was listening for it (AW-065). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_GET_INFO);
                    app.dirty = 1;
                    break;
                }
                if (key == 'o' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

                    if (p != NULL &&
                        tiku_trk_model_is_container(&p->model)) {
                        (void)open_folder(&app, p->model.path);
                    } else if (p != NULL) {
                        begin_edit(&app, fl);
                    } else {
                        /* Grey in the menu; said out loud for the chord
                         * (NeXT UIG ch6). */
                        snprintf(app.status, sizeof app.status,
                                 "Open is not available here");
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 'e' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    begin_rename(&app, fl,
                        tiku_trk_poseview_active(fl->view) != NULL
                            ? tiku_trk_poseview_active(fl->view)->index : -1);
                    app.dirty = 1;
                    break;
                }
                if (key == 'd' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    menu_invoke(&app, fl, TIKU_TRK_CMD_DUPLICATE);
                    app.dirty = 1;
                    break;
                }
                if (key == 'k' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    menu_invoke(&app, fl,
                        (ev.modifiers & TIKU_DESK_MOD_SHIFT)
                            ? TIKU_TRK_CMD_CLEANUP_ALL
                            : TIKU_TRK_CMD_CLEANUP);
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_DESK_MOD_CMD) && fl != NULL &&
                    (key == 'x' || key == 'c' || key == 'v')) {
                    /* The clipboard chords, which until now only the menu
                     * could reach here; Shift turns each into its "more"
                     * or "links" variant, as the menu labels do. */
                    unsigned m = (ev.modifiers & TIKU_DESK_MOD_SHIFT)
                                     ? TIKU_TRK_MOD_SHIFT : 0u;
                    int base = (key == 'x') ? TIKU_TRK_CMD_CUT
                             : (key == 'c') ? TIKU_TRK_CMD_COPY
                                            : TIKU_TRK_CMD_PASTE;

                    menu_invoke(&app, fl, tiku_trk_sc_command(base, m));
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_DESK_MOD_CMD) && key == 'z') {
                    /* Cmd+Z / Shift+Cmd+Z: the file operations' undo and
                     * redo, in this shell too (CW-061).  The inline editor
                     * kept the key above, so a focused text field never
                     * loses it. */
                    char what[64], err[200];

                    if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                        if (tiku_trk_fs_can_redo(app.fs, what,
                                                 sizeof what)) {
                            int n = tiku_trk_fs_redo(app.fs, err,
                                                     sizeof err);

                            snprintf(app.status, sizeof app.status,
                                     "redid %s (%d item%s)", what, n,
                                     (n == 1) ? "" : "s");
                        } else {
                            snprintf(app.status, sizeof app.status,
                                     "nothing to redo");
                        }
                    } else if (tiku_trk_fs_can_undo(app.fs, what,
                                                    sizeof what)) {
                        int n = tiku_trk_fs_undo(app.fs, err, sizeof err);

                        snprintf(app.status, sizeof app.status,
                                 "undid %s (%d item%s)", what, n,
                                 (n == 1) ? "" : "s");
                    } else {
                        snprintf(app.status, sizeof app.status,
                                 "nothing to undo");
                    }
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_DESK_MOD_CMD) && fl != NULL &&
                    tiku_trk_sc_command_for_key(key, ev.modifiers) != 0) {
                    /* The rest of the standard set, resolved from the one
                     * table the menus draw their shortcuts from -- a
                     * chord the menu advertises must answer (PVK-042). */
                    menu_invoke(&app, fl,
                        tiku_trk_sc_command_for_key(key, ev.modifiers));
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_DESK_MOD_CMD) && fl != NULL &&
                    (key == '1' || key == '2' || key == '3')) {
                    /* AW-025/CW-065: the same chords the single-window app
                     * binds.  The menu offers these modes; a shortcut that
                     * works in one window and not the other is worse than
                     * no shortcut at all. */
                    menu_invoke(&app, fl,
                        (key == '1') ? TIKU_TRK_CMD_ICON_VIEW :
                        (key == '2') ? TIKU_TRK_CMD_MINI_ICON_VIEW
                                     : TIKU_TRK_CMD_LIST_VIEW);
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_DESK_MOD_CMD) && fl != NULL &&
                    (key == '+' || key == '=' || key == '-')) {
                    (void)tiku_trk_poseview_step_icon_size(fl->view,
                        (key == '-') ? 0 : 1);
                    tiku_trk_poseview_cleanup(fl->view,
                                              tiku_trk_folder_window_content(fl).w);
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_status_overlay_key(&app.status_overlay, key)) {
                    break;
                }
                {
                    char printable[2];

                    if (tiku_desk_workspace_focused(app.desk) ==
                            tiku_trk_deskbar_prefs_window_handle(
                                &app.deskbar_prefs_window) &&
                        tiku_trk_deskbar_prefs_window_key(
                            &app.deskbar_prefs_window,
                            key_name(&ev, printable))) {
                        app.dirty = 1;
                        break;
                    }
                }
                if (tiku_trk_switcher_window_shown(&app.switcher_window)) {
                    /* Modal while it is up, and every key goes to it: the
                     * switcher IS the focus for as long as it shows. */
                    char printable[2];
                    const char *nm = key_name(&ev, printable);
                    tiku_trk_sw_act_t act = tiku_trk_switcher_window_key(
                        &app.switcher_window, nm,
                        (ev.modifiers & TIKU_DESK_MOD_SHIFT) ? 1 : 0);

                    if (act == TIKU_TRK_SW_COMMIT) {
                        tiku_trk_switcher_window_close(&app.switcher_window,
                                                       1);
                    } else if (act == TIKU_TRK_SW_CANCEL) {
                        tiku_trk_switcher_window_close(&app.switcher_window,
                                                       0);
                    } else if (act == TIKU_TRK_SW_QUIT_APP ||
                               act == TIKU_TRK_SW_HIDE_APP) {
                        const char *selected = tiku_trk_switcher_window_selected(
                            &app.switcher_window);

                        snprintf(app.status, sizeof app.status,
                                 "%s %.50s",
                                 (act == TIKU_TRK_SW_QUIT_APP) ? "quitting"
                                                               : "hiding",
                                 (selected != NULL) ? selected : "");
                    }
                    if (key == TIKU_DESK_KEY_TAB) {
                        /* Tab while it is up moves on, which is the same
                         * gesture that opened it. */
                        tiku_trk_switcher_window_open(&app.switcher_window,
                            (ev.modifiers & TIKU_DESK_MOD_SHIFT) ? 1 : 0);
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_DESK_KEY_TAB && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    tiku_trk_switcher_window_open(&app.switcher_window,
                        (ev.modifiers & TIKU_DESK_MOD_SHIFT) ? 1 : 0);
                    app.dirty = 1;
                    break;
                }
                if (key == ',' && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    /* The settings are the application's, not a window's,
                     * so this works with no folder focused. */
                    tiku_trk_prefs_window_open(&app.prefs_window);
                    app.dirty = 1;
                    break;
                }
                if (key == 'n' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    menu_invoke(&app, fl, TIKU_TRK_CMD_NEW_FOLDER);
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_DESK_KEY_MENU && (ev.modifiers &
                        (TIKU_DESK_MOD_CMD | TIKU_DESK_MOD_SHIFT | TIKU_DESK_MOD_OPTION)) == 0) {
                    /* The Menu key alone opens the leaf menu -- the one
                     * gesture that reaches the Deskbar without the
                     * pointer (DB-011). */
                    if (tiku_trk_deskbar_menu_open(app.deskbar)) {
                        tiku_trk_deskbar_close_menu(app.deskbar);
                    } else {
                        tiku_trk_deskbar_open_menu(app.deskbar);
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 's' && (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    fl != NULL) {
                    /* PVK-046: invert -- the only way to say "everything
                     * except these" without clicking the rest. */
                    tiku_trk_poseview_invert_selection(fl->view);
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_DESK_KEY_TAB && fl != NULL) {
                    /* Tab is a name-order walk over poses.  While filtering
                     * is active it is deliberately swallowed. */
                    if (!tiku_trk_poseview_is_filtering(fl->view)) {
                        (void)tiku_trk_poseview_tab_next(fl->view,
                            (ev.modifiers & TIKU_DESK_MOD_SHIFT) != 0u);
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 'w' && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    close_window(&app, w);
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && key == TIKU_DESK_KEY_DOWN &&
                    (ev.modifiers & TIKU_DESK_MOD_CMD) &&
                    (ev.modifiers & TIKU_DESK_MOD_OPTION)) {
                    tiku_trk_pose_t *p =
                        tiku_trk_poseview_active(fl->view);

                    if (p != NULL &&
                        tiku_trk_model_is_container(&p->model)) {
                        tiku_desk_window_t *from = fl->win;

                        if (open_folder(&app, p->model.path) != NULL) {
                            close_window(&app, from);
                        }
                    }
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && key == TIKU_DESK_KEY_DOWN &&
                    (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    tiku_trk_pose_t *p =
                        tiku_trk_poseview_active(fl->view);

                    if (p != NULL &&
                        tiku_trk_model_is_container(&p->model)) {
                        (void)open_folder(&app, p->model.path);
                    } else if (p != NULL) {
                        begin_edit(&app, fl);
                    }
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && key == TIKU_DESK_KEY_UP &&
                    (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    /* PVK-040/CW-051: Cmd+Up opens the parent; Option
                     * additionally takes the spatial window we left. */
                    if (strcmp(fl->path, "/") != 0) {
                        char parent[TIKU_TRK_PATH_MAX];
                        char *slash;
                        tiku_desk_window_t *from = fl->win;

                        snprintf(parent, sizeof parent, "%s", fl->path);
                        slash = strrchr(parent, '/');
                        if (slash == parent) {
                            parent[1] = '\0';
                        } else if (slash != NULL) {
                            *slash = '\0';
                        }
                        if (slash != NULL &&
                            open_folder(&app, parent) != NULL &&
                            (ev.modifiers & TIKU_DESK_MOD_OPTION)) {
                            close_window(&app, from);
                        }
                    }
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL) {
                    /* Left and Right matter in an icon mode, where the
                     * arrangement is two-dimensional; the pages matter in
                     * both and were bound in neither shell's multi-window
                     * half (PVK-007, PVK-011). */
                    int nav = (key == TIKU_DESK_KEY_UP)    ? TIKU_TRK_KEY_UP
                            : (key == TIKU_DESK_KEY_DOWN)  ? TIKU_TRK_KEY_DOWN
                            : (key == TIKU_DESK_KEY_LEFT)  ? TIKU_TRK_KEY_LEFT
                            : (key == TIKU_DESK_KEY_RIGHT) ? TIKU_TRK_KEY_RIGHT
                            : (key == TIKU_DESK_KEY_PAGE_UP) ? TIKU_TRK_KEY_PGUP
                            : (key == TIKU_DESK_KEY_PAGE_DOWN)  ? TIKU_TRK_KEY_PGDN
                            : (key == TIKU_DESK_KEY_HOME)  ? TIKU_TRK_KEY_HOME
                            : (key == TIKU_DESK_KEY_END)   ? TIKU_TRK_KEY_END : 0;
                    if (nav != 0) {
                        (void)tiku_trk_poseview_key_nav(fl->view, nav, mods);
                    } else if (key == TIKU_DESK_KEY_RETURN) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_active(fl->view);
                        if (p != NULL &&
                            tiku_trk_model_is_container(&p->model)) {
                            /* Option+Return opens and takes the window it
                             * came from with it, the keyboard's version of
                             * an Option double-click (PVK-017). */
                            int chase = (ev.modifiers & TIKU_DESK_MOD_OPTION) != 0;
                            tiku_desk_window_t *from = fl->win;

                            if (open_folder(&app, p->model.path) != NULL &&
                                chase) {
                                close_window(&app, from);
                            }
                        } else if (p != NULL) {
                            /* Opening a leaf means editing its value: there
                             * is no other window for it to open. */
                            begin_edit(&app, fl);
                        }
                    } else if (key == TIKU_DESK_KEY_BACKSPACE) {
                        char parent[TIKU_TRK_PATH_MAX];
                        char *slash;
                        snprintf(parent, sizeof parent, "%s", fl->path);
                        slash = strrchr(parent, '/');
                        if (slash != NULL) {
                            if (slash == parent) { parent[1] = '\0'; }
                            else { *slash = '\0'; }
                            (void)open_folder(&app, parent);
                        }
                    }
                    app.dirty = 1;
                }
                break;
            }
            }
            case TIKU_DESK_EVENT_CLOSE:
                app.running = 0;
                break;
            default:
                break;
            }
            if (sc != NULL) {
                break;              /* one instruction per turn */
            }
        }

        if ((app.nav_on && app.popup != NULL) || app.spring_nav_on) {
            /* A chunk per turn: the menu answers the mouse while a large
             * folder is still arriving (CW-055). */
            if (tiku_trk_nav_fill(&app.nav, app.nav.depth - 1)) {
                if (app.spring_nav_on) {
                    /* What the drag can land on is per entry, so fresh
                     * entries get their states as they arrive (PVS-052). */
                    spring_nav_states(&app);
                }
                app.dirty = 1;
            }
        } else if (app.nav_on && app.popup == NULL) {
            app.nav_on = 0;         /* the menu went; so does the walk */
        }
        {   /* Ask the entries that have not been asked whether they have
             * a picture of their own -- a bounded batch per turn, for the
             * same reason the thumbnails are made in one: a folder of a
             * thousand images must not open a thousand files inside one
             * tick (IV-005, IV-037). */
            int budget = HINTS_PER_TURN;

            for (i = 0; i < app.folder_count && budget > 0; i++) {
                folder_t *fl = &app.folders[i];
                int k, n;

                if (fl->win == NULL || !fl->win->open) {
                    continue;
                }
                n = tiku_trk_poseview_count(fl->view);
                for (k = 0; k < n && budget > 0; k++) {
                    tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, k);

                    if (p == NULL || p->model.facts.icon_hint !=
                                         TIKU_TRK_ICONHINT_UNASKED) {
                        continue;
                    }
                    (void)tiku_trk_nodeicon_hint(app.store, &app.thumbs,
                                                 &p->model);
                    budget--;
                    /* One pose learned what picture it has, so one row is
                     * what needs painting: a bare dirty here repainted the
                     * whole listing and undid the damage the notice had
                     * just recorded (PVL-075). */
                    tiku_trk_poseview_damage_row(fl->view, k);
                    app.dirty = 1;
                }
            }
        }
        /* One directory of one folder-size walk per turn, so a Get Info
         * window on a deep tree fills in while the desktop keeps answering. */
        (void)tiku_trk_info_windows_calc_step(&app.info_windows);
        if (tiku_trk_thumb_step(&app.thumbs, app.store)) {
            /* One picture per turn, on the idle tick: the window keeps
             * answering while a folder of images fills in (IV-031). */
            app.dirty = 1;
        }
        (void)tiku_trk_band_controller_pulse(&app.band_controller,
                                             tiku_trk_now());
        (void)tiku_trk_drag_target_pulse(&app.drag_target,
                                         tiku_trk_now());

        /* PVS-017: a button held still for the click interval opens the
         * context menu without waiting for the release. */
        if (app.deskbar_press &&
            tiku_trk_now() - app.press_at > 2 * TIKU_TRK_CLICK_SPEED_US) {
            tiku_trk_db_pick_t dbpick;

            /* A still primary press is the Deskbar's long-press equivalent
             * of a secondary click.  It replaces the calendar with clock
             * options without waiting for release. */
            tiku_trk_deskbar_close_calendar(app.deskbar);
            (void)tiku_trk_deskbar_click(app.deskbar, app.press_x,
                                         app.press_y, 0u, 3, 1, &dbpick);
            app.deskbar_press = 0;
            app.dirty = 1;
        }
        if (app.press_down && !app.press_menu && app.popup == NULL &&
            app.press_fl != NULL &&
            tiku_trk_now() - app.press_at > TIKU_TRK_CLICK_SPEED_US) {
            folder_t *pfl = app.press_fl;
            tiku_trk_pose_t *p = (app.press_row >= 0)
                ? tiku_trk_poseview_at(pfl->view, app.press_row) : NULL;
            tiku_trk_sc_ctx_t c;

            tiku_trk_poseview_cancel_edit_wait(pfl->view);
            commit_rename(&app, 1);            /* CW-029: mouse-opened */
            (void)tiku_trk_poseview_context_click(pfl->view, app.press_row);
            c = menu_ctx(&app, pfl, 0u);
            build_open_with(&app, pfl);
            build_per_type(&app, pfl);
            app.popup = tiku_trk_cwmenu_context(&app.menus,
                            (p != NULL) ? &p->model : NULL, &c);
            app.popup_sub = NULL;
            app.popup_frame = tiku_trk_menu_place(app.popup, app.press_x,
                app.press_y, (tiku_desk_rect_t){ 0, 0, app.surf->w,
                                                 app.surf->h });
            app.press_menu = 1;
            app.press_down = 0;
            app.dirty = 1;
        }
        /* Tracker's Pulse: a rename armed by a slow second click starts
         * here rather than on the click itself (MA-043). */
        for (i = 0; i < app.folder_count; i++) {
            folder_t *fl = &app.folders[i];
            int due;

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            due = tiku_trk_poseview_edit_due(fl->view, tiku_trk_now());
            if (due >= 0 && !app.folder_edit.editing) {
                begin_rename(&app, fl, due);
                app.dirty = 1;
            }
        }
        /* PVS-023: whichever window has focus paints its selection; the
         * rest own theirs without showing it.  Done once per cycle so every
         * path that changes focus is covered rather than each one
         * remembering to. */
        {
            tiku_desk_window_t *fw = tiku_desk_workspace_focused(app.desk);

            for (i = 0; i < app.folder_count; i++) {
                folder_t *fl = &app.folders[i];

                if (fl->win == NULL || !fl->win->open) {
                    continue;
                }
                tiku_trk_poseview_window_activated(fl->view,
                                                   fl->win == fw);
            }
        }

        /* One pump serves every window and the tray, so a device is read
         * once per cycle however many windows are showing it. */
        for (i = 0; i < app.folder_count; i++) {
            /* One batch per window per turn: a folder still arriving keeps
             * the desktop drawing rather than holding the loop. */
            if (app.folders[i].win != NULL && app.folders[i].win->open &&
                tiku_trk_poseview_loading(app.folders[i].view)) {
                (void)tiku_trk_poseview_load_step(app.folders[i].view,
                                                  tiku_trk_now());
                app.dirty = 1;
            }
        }
        if (tiku_trk_now() - last_sweep > 60000000) {
            /* The reaping of searches nobody named.  On a slow tick of its
             * own and skipped for the first half hour, because it is the
             * least urgent work there is (Q-037, AW-090). */
            const char *open[TIKU_DESK_WINDOW_MAX];
            int nopen = 0;

            for (i = 0; i < app.folder_count; i++) {
                if (app.folders[i].qname[0] != '\0' &&
                    app.folders[i].win != NULL && app.folders[i].win->open) {
                    open[nopen++] = app.folders[i].qname;
                }
            }
            if (!tiku_trk_task_system_busy()) {
                /* Deleting user data is idle-only work twice over: the
                 * half-hour hold-off AND the machine's own load gate
                 * (AW-090). */
                (void)tiku_trk_query_sweep(app.store, (int64_t)time(NULL),
                                           (tiku_trk_now() - started_at) /
                                               1000000, open, nopen);
            }
            last_sweep = tiku_trk_now();
        }
        if (tiku_trk_now() - last_pump > 400000) {
            int changed = 0;
            {
                /* What the leaf menu offers above the Deskbar folder.
                 * Refreshed on the tick rather than read by the bar: they
                 * are the same lists the Open panel keeps (DB-002). */
                static const tiku_trk_recent_t kWhich[] = {
                    TIKU_TRK_RECENT_DOCS, TIKU_TRK_RECENT_FOLDERS,
                    TIKU_TRK_RECENT_APPS
                };
                char paths[TIKU_TRK_DB_RECENT_MAX][TIKU_TRK_PATH_MAX];
                int k;

                for (k = 0; k < 3; k++) {
                    int nr = tiku_trk_recent_list(app.store, kWhich[k],
                                 TIKU_TRK_DB_RECENT_MAX, paths,
                                 TIKU_TRK_DB_RECENT_MAX);

                    tiku_trk_deskbar_set_recent(app.deskbar,
                        (tiku_trk_db_recent_t)k, paths, nr);
                }
            }
            /*
             * Drained ONCE for the whole desktop: reading the device's ring
             * takes the records out of it, so a per-window drain would let
             * the first window consume everyone else's and leave them told
             * that nothing happened.
             */
            tiku_trk_change_t ch[32];
            int nch = -1, complete = 1;

            if (app.backend->ops->changes != NULL) {
                nch = app.backend->ops->changes(app.backend, ch, 32,
                                                &complete);
            }
            for (i = 0; i < app.folder_count; i++) {
                folder_t *fl = &app.folders[i];
                unsigned nf;

                if (fl->win == NULL || !fl->win->open) {
                    continue;
                }
                changed += tiku_trk_poseview_pump(fl->view, 5);
                /* Membership changes go through the notice handler rather
                 * than a reload: a re-read would rebuild every row and take
                 * the selection, the scroll position and the arrangement
                 * with it. */
                nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(),
                                            ch, nch, complete);
                if ((nf & TIKU_TRK_NOTIFY_GONE) != 0u) {
                    /* The folder is gone.  Its window goes with it, and its
                     * position is deliberately NOT saved -- there is nothing
                     * left for it to be the position of, and the teardown
                     * sweep must not write one either. */
                    fl->gone = 1;
                    tiku_desk_workspace_close(app.desk, fl->win);
                    changed++;
                } else if (nf != TIKU_TRK_NOTIFY_NOTHING) {
                    if ((nf & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
                        tiku_trk_folder_title_for(prefs_model(&app),
                            tiku_trk_poseview_path(fl->view),
                            fl->win->title, sizeof fl->win->title);
                        snprintf(fl->path, sizeof fl->path, "%s",
                                 tiku_trk_poseview_path(fl->view));
                    }
                    changed++;
                }
            }
            changed += tiku_trk_desktop_view_poll(&app.desktop_view,
                tiku_trk_now(), ch, nch, complete);
            changed += tiku_trk_panel_window_poll(&app.file_panel,
                tiku_trk_now(), ch, nch, complete);
            {   /* What has been mounted or unmounted since the last look.
                 * Polled with everything else rather than on a timer of its
                 * own: a volume arriving is a change like any other. */
                tiku_trk_volchange_t vc[TIKU_TRK_VOLWATCH_MAX];
                int nv = tiku_trk_volwatch_poll(&app.volwatch, vc,
                                                TIKU_TRK_VOLWATCH_MAX);
                int k;

                for (k = 0; k < nv; k++) {
                    if (vc[k].renamed) {
                        /* Only the label moved: the union rebuild re-reads
                         * every volume's name, and the relist refreshes
                         * open listings (MA-021). */
                        tiku_trk_desktop_view_rebuild(&app.desktop_view);
                        changed++;
                        continue;
                    }
                    if (vc[k].mounted) {
                        /* A folder window showing the parent finds it by
                         * re-listing, which the poll above already did.
                         * The Desktop union gains the volume's Desktop
                         * folder as a source, live (PVN-040, PVN-051). */
                        tiku_trk_desktop_view_rebuild(&app.desktop_view);
                        /* And half a second on, broken links get another
                         * look: by then a link to the new volume resolves
                         * as a volume (PVN-042). */
                        (void)tiku_trk_run_later(app.loop,
                            refresh_after_mount, &app, NULL, 500000);
                        changed++;
                        continue;
                    }
                    /* And loses it: a source that has gone must not be
                     * polled for. */
                    tiku_trk_desktop_view_rebuild(&app.desktop_view);
                    /* Unmounted.  A window ON the volume closes -- and its
                     * arrangement is deliberately NOT saved, because the
                     * disk it belongs to has gone; a window merely SHOWING
                     * rows from it drops those rows (PVN-043). */
                    for (i = 0; i < app.folder_count; i++) {
                        folder_t *fl = &app.folders[i];

                        if (fl->win == NULL || !fl->win->open) {
                            continue;
                        }
                        if (tiku_trk_poseview_is_query(fl->view) &&
                            tiku_trk_query_targets_device(
                                fl->run, (uint64_t)vc[k].v.dev)) {
                            /* The run is the query's subscription.  Stop it
                             * with the window so no watch survives a target
                             * volume disappearing (Q-046). */
                            tiku_trk_query_stop(fl->run);
                            fl->run = NULL;
                            fl->live = 0;
                            fl->gone = 1;
                            tiku_desk_workspace_close(app.desk, fl->win);
                            changed++;
                        } else if (strncmp(fl->path, vc[k].v.mount,
                                    strlen(vc[k].v.mount)) == 0) {
                            fl->gone = 1;
                            tiku_desk_workspace_close(app.desk, fl->win);
                            changed++;
                        } else if (tiku_trk_poseview_device_gone(fl->view,
                                       (uint64_t)vc[k].v.dev) > 0) {
                            changed++;
                        }
                    }
                    changed += tiku_trk_panel_window_device_gone(
                        &app.file_panel, vc[k].v.mount);
                    changed += tiku_trk_info_windows_device_gone(
                        &app.info_windows, (uint64_t)vc[k].v.dev);
                }
            }
            changed += tiku_trk_info_windows_refresh(&app.info_windows,
                                                      tiku_trk_now());
            changed += tiku_trk_deskbar_refresh(app.deskbar);
            /* Live queries re-evaluate on a slower beat than values are
             * read: a re-walk costs a directory listing per subtree, and a
             * result set that moves every 400 ms is unreadable anyway. */
            if (tiku_trk_now() - last_requery > 3000000) {
                for (i = 0; i < app.folder_count; i++) {
                    if (app.folders[i].win != NULL &&
                        app.folders[i].win->open) {
                        changed += refresh_results(&app, &app.folders[i]);
                        if (app.folders[i].trash_union) {
                            changed += trash_union_refresh(&app,
                                                           &app.folders[i]);
                        }
                    }
                }
                changed += tiku_trk_desktop_view_trash_check(
                    &app.desktop_view);
                last_requery = tiku_trk_now();
            }
            last_pump = tiku_trk_now();
            if (changed > 0) {
                app.dirty = 1;
            }
        }
        if (shot != NULL && shot_at > 0 && tiku_trk_now() >= shot_at) {
            app.dirty = 1;
        }
        if (app.dirty) {
            draw_all(&app);
            blit(&app);
            if (shot != NULL && shot_at > 0 && tiku_trk_now() >= shot_at) {
                (void)tiku_desk_surface_png(app.surf, shot);
                printf("wrote %s\n", shot);
                fflush(stdout);
                shot_at = 0;
            }
        }
        if (app.fs != NULL && tiku_trk_fs_busy(app.fs) &&
            !progress_model(&app)->row[0].paused) {
            const tiku_trk_progress_t *pr;

            /* Stepped here, not inside the command: start() only builds the
             * queue, so a shell that never steps reports work it has not
             * done.  Membership changes reach the windows through the
             * notice poll above, which is why nothing is reloaded here. */
            (void)tiku_trk_fs_step(app.fs, 4);
            if (!tiku_trk_fs_busy(app.fs)) {
                /* The operation just finished: what it MADE gets selected
                 * where it lives, once it shows up (AW-033). */
                char made[8][TIKU_TRK_PATH_MAX];
                int nmade = tiku_trk_fs_last_created(app.fs, made, 8);
                int mi;

                for (mi = 0; mi < nmade; mi++) {
                    select_created_t *scx = malloc(sizeof *scx);

                    if (scx == NULL) {
                        break;
                    }
                    scx->app = &app;
                    snprintf(scx->path, sizeof scx->path, "%s", made[mi]);
                    (void)tiku_trk_run_periodic(app.loop, select_created,
                                                scx, free, 200000,
                                                5000000);
                }
            }
            /* A mixed paste left its second half waiting for the engine to
             * go idle; this is where it gets to start (FS-055). */
            (void)tiku_trk_folder_file_actions_pump(&app.file_actions);
            pr = tiku_trk_fs_progress(app.fs);
            if (pr->conflict) {
                /* Asked, not assumed.  The engine worked out which form
                 * the question takes; this puts it up and waits. */
                if (!alert_model(&app)->open) {
                    tiku_trk_alert_for_conflict(alert_model(&app), pr);
                }
            } else if (pr->error_ask) {
                if (!alert_model(&app)->open) {
                    tiku_trk_alert_for_error(alert_model(&app), pr);
                }
            } else {
                snprintf(app.status, sizeof app.status, "%d of %d%s%s",
                         pr->done + pr->skipped, pr->total,
                         pr->skipped ? " (some skipped)" : "",
                         pr->failed ? " -- some failed" : "");
            }
            if (tiku_trk_progress_wanted(pr->op, pr->total)) {
                /* Where it is going, which is the other half of "what is
                 * happening": a file name alone does not say whether this
                 * is the copy the user meant (FS-069). */
                tiku_trk_progress_update(progress_model(&app), 0, pr, 1,
                                         pr->to[0] != '\0' ? pr->to
                                         : app.prog_dest[0] != '\0'
                                             ? app.prog_dest : NULL);
                tiku_trk_progress_sample(progress_model(&app), 0, pr->bytes,
                                         tiku_trk_now());
            }
            app.dirty = 1;
            if (tiku_trk_status_overlay_progress_opened(
                    &app.status_overlay) == 0) {
                tiku_trk_status_overlay_progress_start(&app.status_overlay,
                                                       tiku_trk_now());
                /* Whoever was in front when the job began gets the focus
                 * back when it ends: an operation is not a reason to be
                 * left looking at something else (FS-073). */
                app.prog_was_active = tiku_desk_workspace_focused(app.desk);
            }
        } else if (progress_model(&app)->open) {
            tiku_trk_progress_update(progress_model(&app), 0, NULL, 0, NULL);
            tiku_trk_status_overlay_progress_finish(&app.status_overlay);
            if (app.prog_was_active != NULL &&
                app.prog_was_active->open) {
                tiku_desk_workspace_activate(app.desk, app.prog_was_active);
            }
            app.prog_was_active = NULL;
            app.dirty = 1;
        }
        if (deadline > 0 && tiku_trk_now() > deadline) {
            app.running = 0;
        }
        /* A script still lets the clock run, just faster: the notice
         * poll, the armed rename and the barber pole are all timed, and a
         * loop that spun without time passing would test none of them. */
        if (app.loop != NULL) {
            /* A task that changed something on screen dirties the surface
             * itself; running is not, by itself, a repaint. */
            (void)tiku_trk_loop_pulse(app.loop);
            if (app.sbar_fl != NULL && app.sbar_fl->view != NULL) {
                /* A held arrow keeps stepping (NeXT UIG ch3). */
                int rep = tiku_trk_scroll_arrow_repeat(&app.sbar_fl->sbar);

                if (rep != 0) {
                    tiku_trk_poseview_scroll_to(app.sbar_fl->view,
                        tiku_trk_poseview_scroll(app.sbar_fl->view) +
                            rep * tiku_trk_scroll_line(&app.sbar_fl->sbar));
                    app.dirty = 1;
                }
            }
        }
        {
            /* Sleep until the next task is due, or one heartbeat,
             * whichever is sooner (AW-086). */
            int64_t nap = (sc != NULL) ? 20000 : 10000;
            int64_t due = (app.loop != NULL)
                              ? tiku_trk_loop_next_due(app.loop) : -1;

            if (due >= 0 && due < nap) {
                nap = (due < 1000) ? 1000 : due;
            }
            usleep((useconds_t)nap);
        }
    }

    tiku_trk_session_controller_save(&app.session);
    for (i = 0; i < app.folder_count; i++) {
        tiku_trk_poseview_free(app.folders[i].view);
    }
    tiku_trk_desktop_view_destroy(&app.desktop_view);
    if (tiku_trk_switcher_window_shown(&app.switcher_window)) {
        tiku_trk_switcher_window_close(&app.switcher_window, 0);
    }
    tiku_trk_panel_window_destroy(&app.file_panel);
    tiku_trk_deskbar_prefs_window_destroy(&app.deskbar_prefs_window);
    tiku_trk_prefs_window_destroy(&app.prefs_window);
    tiku_trk_deskbar_free(app.deskbar);
    tiku_desk_workspace_free(app.desk);
    /* Quitting waits for work already under way, up to five seconds
     * (AW-009): the alternative is a copy abandoned mid-file with
     * nothing to say so. */
    tiku_trk_session_drain_filesystem(app.fs);
    if (sc != NULL) {
        rc = tiku_trk_script_report(sc, "desktop");
        tiku_trk_script_free(sc);
    }
    tiku_trk_fs_free(app.fs);
    tiku_trk_backend_close(app.backend);
    tiku_desk_host_close(app.host);
    tiku_desk_surface_free(app.surf);
    return rc;
}

const tiku_desk_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker",
    .name = "Tracker",
    .run = tracker_run
};
