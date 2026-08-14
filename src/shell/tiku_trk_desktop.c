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

#include <stdio.h>
#include <stdlib.h>
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
#include "tiku_trk_panel.h"
#include "tiku_trk_panel_window.h"
#include "tiku_trk_thumb.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_prefs_window.h"
#include "tiku_trk_volume.h"
#include "tiku_trk_switcher.h"
#include "tiku_trk_switcher_window.h"
#include "tiku_trk_navmenu.h"
#include "tiku_trk_script.h"
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
#include "tiku_trk_folder_actions.h"
#include "tiku_trk_folder_menu.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_launch_controller.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_trk_state.h"
#include "tiku_trk_query.h"
#include "tiku_trk_task.h"
#include "tiku_desk_window.h"

#define DESK_W 1000
#define DESK_H  680
#define COUNT_H  16

/* How many entries per turn are asked whether they carry a picture of their
 * own.  A bound rather than the whole listing: the ask opens the file. */
#define HINTS_PER_TURN 24

/* Where the cascade starts again once it has run off the screen, and how far
 * each window steps from the last. */
#define CASCADE_HOME_X 45
#define CASCADE_HOME_Y 40
#define CASCADE_STEP   22

/* Tracker's own list row: ceilf(plain font size * 1.65), which at the 12 px
 * face is 20 -- tall enough that a 16 px mini icon sits in it with room, and
 * the reason rows do not tighten onto the text. */
#define LIST_ROW_H 20

/** @brief How much of the count view's text a script can read. */
#define TIKU_TRK_COUNT_TEXT_MAX 64

/**
 * @brief How long a drag must rest on a folder before it springs open.
 *
 * Long enough that crossing a folder on the way somewhere else does not
 * open it, short enough to feel like an answer rather than a delay.
 */
#define TIKU_TRK_SPRING_US 700000

/** @brief The band, outside the list, in which a drag scrolls it. */
#define AUTOSCROLL_BAND 6

/** @brief Past this much overshoot the scroll speeds up (kSlowScrollBucket). */
#define AUTOSCROLL_FAST 30

/** @brief How long the pointer must hold in the band before it starts. */
#define AUTOSCROLL_DELAY_US 600000

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
    int                  folder_count;
    int                  row_h;
    int                  team;
    int                  running;
    int                  dirty;
    /* Menus.  One set serves every folder window, rebuilt from whichever is
     * focused each time a menu opens -- the original rebuilds on open too. */
    tiku_trk_cwmenu_t    menus;
    tiku_trk_launch_controller_t launcher;
    tiku_trk_folder_menu_t folder_menu;
    tiku_trk_menu_t     *popup;          /* the menu showing, or NULL       */
    tiku_desk_rect_t     popup_frame;
    tiku_trk_menu_t     *popup_sub;      /* its open submenu, or NULL       */
    tiku_desk_rect_t     popup_sub_frame;
    folder_t            *bar_fl;         /* window whose bar opened popup   */
    int                  bar_open;       /* title in that window, or -1     */
    /* Editing a node's VALUE in place.  A namespace browser that can only
     * read is half a browser: this is where a write happens. */
    int                  editing;
    tiku_trk_edit_t      ed;
    char                 edit_path[TIKU_TRK_PATH_MAX];
    tiku_trk_desc_t      edit_desc;
    char                 status[200];
    /* Find is a MODE, as the shell's own watch is: there is no second window
     * to put a panel in, and a namespace search wants the same one-line
     * entry the rest of the system uses. */
    int                  finding;
    char                 find_buf[64];
    int                  find_len;
    int                  find_serial;
    tiku_trk_find_window_t find_window;
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
    /* Where the last press landed, for the long press and for telling a
     * slow second click from a drag. */
    int64_t              press_at;
    int                  press_x, press_y;
    int                  press_row;
    tiku_trk_drag_t      drag;      /* the gesture between press and drop */
    char                 drop_dst[TIKU_TRK_PATH_MAX];
    tiku_trk_drag_t      pending;   /* payload a drop menu is deciding */
    tiku_trk_thumb_q_t   thumbs;    /* image previews, made off-tick  */
    /* ONE node-icon cache for the desktop: the same file shown in two
     * windows is one entry, and an icon edited on disk is dropped once
     * rather than per window (IV-038). */
    tiku_trk_nodeicon_t  nicons;
    /* Which applications there are and what each reads.  One registry for
     * the desktop: the Open with menu, a double click and a drop on an
     * application all ask the same question (AW-040). */
    tiku_trk_apps_t      apps;
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
    /* Which broken link a picker was opened to re-point, or -1. */
    int                  info_relink;
    tiku_trk_status_overlay_t status_overlay;
    tiku_trk_prefs_window_t prefs_window;
    tiku_trk_switcher_window_t switcher_window;
    tiku_trk_nav_t       nav;      /* the open navigation walk   */
    int                  nav_on;
    tiku_desk_window_t      *prog_was_active;  /* focus to give back */
    int64_t              hover_at;  /* when the drag settled on a target  */
    int64_t              scroll_at; /* when it entered the scroll band    */
    int                  sprung;    /* that target has already opened     */
    folder_t            *drop_fl;   /* window the pointer is over         */
    int                  drop_row;  /* pose highlighted as target, or -1  */
    int                  press_down;
    int                  dock_drag;     /* Ctrl-dragging the Deskbar     */
    /* The rubber band.  In WINDOW coordinates, because that is where the
     * press arrives and where it is drawn; the view is handed view-space
     * coordinates when it is asked to sweep. */
    folder_t            *band_fl;
    folder_t            *band_arm;      /* pressed, not yet swept        */
    int                  band_x0, band_y0, band_x1, band_y1;
    unsigned             band_mods;     /* held while idle scrolling     */
    int64_t              band_scroll_at;
    folder_t            *press_fl;
    int                  press_menu;    /* the long press already fired    */
    /* Renaming an ENTRY, which is a different thing from editing a node's
     * value: this one changes what the entry is called. */
    int                  renaming;
    tiku_trk_edit_t      ren;
    char                 ren_path[TIKU_TRK_PATH_MAX];
    folder_t            *ren_fl;
    int                  ren_row;
} desktop_t;

static folder_t *open_folder(desktop_t *app, const char *path);

/** @brief Let the launch controller hand Tracker-owned paths to the shell. */
static void
launch_open_path(void *context, const char *path)
{
    (void)open_folder(context, path);
}

/** @brief Preferences remain shared data; their window owns the lifetime. */
static tiku_trk_prefs_t *
prefs_model(desktop_t *app)
{
    return tiku_trk_prefs_window_model(&app->prefs_window);
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
    }
}

/** @brief Stop asks the operation engine to cancel at its safe boundary. */
static void
status_stop(void *context)
{
    desktop_t *app = context;

    tiku_trk_fs_cancel(app->fs);
}

/** @brief What the action table needs to know about the focused window. */
static tiku_trk_sc_ctx_t
menu_ctx(desktop_t *app, folder_t *fl, unsigned mods)
{
    return tiku_trk_folder_menu_context(&app->folder_menu, fl, mods);
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

static folder_t *folder_of(desktop_t *app, tiku_desk_window_t *w);

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
    char err[128], canon[64];

    if (!app->editing) {
        return;
    }
    app->editing = 0;
    tiku_trk_edit_stop(&app->ed);
    if (!tiku_trk_attr_validate(&app->edit_desc, app->ed.text, err,
                                sizeof err)) {
        snprintf(app->status, sizeof app->status, "%s", err);
        app->dirty = 1;
        return;
    }
    (void)tiku_trk_attr_canonical(&app->edit_desc, app->ed.text, canon,
                                  sizeof canon);
    if (app->backend->ops->write(app->backend, app->edit_path, canon,
                                 strlen(canon), err, sizeof err) != 0) {
        /* The device refused it after all; show its words, not ours. */
        snprintf(app->status, sizeof app->status, "%s", err);
    } else {
        {
            const char *slash = strrchr(app->edit_path, '/');

            snprintf(app->status, sizeof app->status, "%.100s = %.64s",
                     (slash != NULL) ? slash + 1 : app->edit_path, canon);
        }
        if (fl != NULL) {
            (void)tiku_trk_poseview_refresh_values(fl->view, 0, 64);
        }
    }
    app->dirty = 1;
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
save_frame_of(desktop_t *app, folder_t *fl)
{
    tiku_trk_frame_t now;

    if (fl == NULL || fl->win == NULL || fl->gone) {
        return;
    }
    now.x = (int32_t)fl->win->frame.x;
    now.y = (int32_t)fl->win->frame.y;
    now.w = (int32_t)fl->win->frame.w;
    now.h = (int32_t)fl->win->frame.h;
    (void)tiku_trk_frame_save(app->store, fl->state_path, &now,
                              fl->had_frame ? &fl->opened_with : NULL);
}

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
    info_slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (info_slot >= 0) {
        tiku_trk_info_windows_close(&app->info_windows, info_slot);
        return;
    }
    fl = folder_of(app, w);
    if (fl != NULL) {
        (void)tiku_trk_poseview_save_state(fl->view);
        (void)tiku_trk_poseview_save_columns(fl->view);
        save_frame_of(app, fl);
        if (app->bar_fl == fl) {
            app->bar_fl = NULL;
            app->bar_open = -1;
            app->menus.bar.open = -1;
            app->popup = NULL;
            app->popup_sub = NULL;
        }
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
 * @brief Resolve and perform the drop the pointer is over.
 *
 * The destination is the highlighted pose when there is one and the hovered
 * window's own folder otherwise -- which is also what a drop onto one of the
 * dragged poses means, since such a pose never highlights (PVS-042).
 */
static void
do_drop(desktop_t *app, int x, int y, unsigned state, unsigned button)
{
    const char *sel[TIKU_TRK_DRAG_MAX];
    char dst[TIKU_TRK_PATH_MAX];
    unsigned mods = 0;
    tiku_trk_drop_t what;
    int i, n = 0, same;

    (void)x;
    (void)y;
    {   /* The Deskbar first: it overlays the desktop, so a drop there is
         * a drop on IT rather than on whatever window is behind. */
        int team = -1;
        tiku_trk_db_drop_t d = tiku_trk_deskbar_drop_at(app->deskbar, x, y,
                                                        &team);

        if (d == TIKU_TRK_DB_DROP_NONE) {
            /* The open menu sits beside the bar, not on it, so it is
             * asked separately (DB-009). */
            char into[TIKU_TRK_PATH_MAX];

            if (tiku_trk_deskbar_menu_drop_at(app->deskbar, x, y, into,
                                              sizeof into) ==
                    TIKU_TRK_DB_DROP_MENU_DIR) {
                const char *sel3[TIKU_TRK_DRAG_MAX];
                int k, n3 = 0;

                for (k = 0; k < app->drag.count; k++) {
                    sel3[n3++] = app->drag.path[k];
                }
                if (n3 > 0) {
                    (void)tiku_trk_fs_link(app->fs, sel3, n3, into, 0);
                    snprintf(app->status, sizeof app->status,
                             "linking %d item%s into %.40s", n3,
                             (n3 == 1) ? "" : "s", into);
                }
                return;
            }
        }
        if (d != TIKU_TRK_DB_DROP_NONE) {
            const char *sel2[TIKU_TRK_DRAG_MAX];
            int k, n2 = 0;

            for (k = 0; k < app->drag.count; k++) {
                sel2[n2++] = app->drag.path[k];
            }
            if (n2 == 0) {
                return;
            }
            if (d == TIKU_TRK_DB_DROP_LEAF) {
                /* Links, not copies: the Deskbar menu is a set of
                 * pointers at things, and copying into it would make a
                 * second one of everything (DB-009). */
                const char *home = getenv("HOME");
                char into[TIKU_TRK_PATH_MAX];

                if (home != NULL &&
                    snprintf(into, sizeof into, "%s/.config/deskbar",
                             home) > 0) {
                    (void)mkdir(into, 0755);
                    (void)tiku_trk_fs_link(app->fs, sel2, n2, into, 0);
                    snprintf(app->status, sizeof app->status,
                             "linking %d item%s into the Deskbar", n2,
                             (n2 == 1) ? "" : "s");
                }
                return;
            }
            if (d == TIKU_TRK_DB_DROP_TEAM && team >= 0) {
                const char *types[TIKU_TRK_DRAG_TYPES];
                int nt = tiku_trk_drag_types(&app->drag), ti;

                for (ti = 0; ti < nt; ti++) {
                    types[ti] = app->drag.type[ti];
                }
                if (!tiku_trk_deskbar_team_accepts(app->deskbar, team,
                        types, nt, (state & TIKU_DESK_MOD_CMD) != 0u)) {
                    snprintf(app->status, sizeof app->status,
                             "%.40s cannot open %s",
                             tiku_trk_deskbar_team_name(app->deskbar, team),
                             (n2 == 1) ? "that" : "those");
                    return;
                }
                snprintf(app->status, sizeof app->status,
                         "handing %d item%s to %.40s", n2,
                         (n2 == 1) ? "" : "s",
                         tiku_trk_deskbar_team_name(app->deskbar, team));
                return;
            }
            {   /* The bar itself launches, and folders are silently
                 * passed over (DB-066, DB-075). */
                int launched = 0;

                for (k = 0; k < n2; k++) {
                    tiku_trk_model_t lm;

                    if (app->backend->ops->stat(app->backend, sel2[k],
                                                &lm) == 0 &&
                        tiku_trk_deskbar_launchable(&lm)) {
                        launched++;
                    }
                }
                snprintf(app->status, sizeof app->status,
                         "launching %d item%s", launched,
                         (launched == 1) ? "" : "s");
            }
            return;
        }
    }
    if (app->drop_fl == NULL) {
        return;                     /* dropped on nothing */
    }
    if (app->drop_row < 0 && app->drop_fl->view != NULL &&
        tiku_trk_poseview_mode(app->drop_fl->view) != TIKU_TRK_MODE_LIST &&
        strcmp(app->drag.src_dir, app->drop_fl->path) == 0) {
        /* Dropped in the view it came from: an ARRANGEMENT, not a file
         * operation.  The whole selection moves by the drag's delta, so
         * what was picked up is put down in the same shape, and holding
         * Command snaps the landing points to the grid (PVL-026,
         * PVS-046). */
        int dx = x - app->drag.x0, dy = y - app->drag.y0;
        int k, kn = tiku_trk_poseview_count(app->drop_fl->view);
        int snap = (state & TIKU_DESK_MOD_CMD) != 0u;

        for (k = 0; k < kn; k++) {
            tiku_trk_pose_t *q = tiku_trk_poseview_at(app->drop_fl->view, k);
            int px, py;

            if (q == NULL || !q->selected) {
                continue;
            }
            /* Its LOCATION, not the rectangle it draws in: the rect is
             * the union of the art and a label that may be wider, so
             * moving to a rect corner would shift the icon sideways by
             * however long its name is. */
            tiku_trk_poseview_pose_location(app->drop_fl->view, q, &px, &py);
            tiku_trk_poseview_move_pose(app->drop_fl->view, q, px + dx,
                                        py + dy, snap);
        }
        if (app->drop_fl == desktop_folder(app)) {
            /* The Desktop cannot scroll, so an icon dropped off the edge
             * is one that is simply gone (PVS-058). */
            (void)tiku_trk_desktop_view_ensure_visible(&app->desktop_view);
        }
        (void)tiku_trk_poseview_save_state(app->drop_fl->view);
        app->dirty = 1;
        return;
    }
    if (app->drop_row >= 0) {
        tiku_trk_pose_t *q = tiku_trk_poseview_at(app->drop_fl->view,
                                                  app->drop_row);

        if (q == NULL) {
            return;
        }
        snprintf(dst, sizeof dst, "%s", q->model.path);
    } else {
        snprintf(dst, sizeof dst, "%s", app->drop_fl->path);
    }
    if ((state & TIKU_DESK_MOD_OPTION) != 0u)    { mods |= TIKU_TRK_DRAG_MOD_COPY; }
    if ((state & TIKU_DESK_MOD_CMD) != 0u) { mods |= TIKU_TRK_DRAG_MOD_LINK; }

    {   /* A refusal is SAID, not swallowed.  A drop that quietly does
         * nothing looks like a drop that missed, and the user tries again
         * harder (PVS-043). */
        tiku_trk_model_t dm;
        const char *why = NULL;

        if (app->backend->ops->stat(app->backend, dst, &dm) != 0 ||
            !tiku_trk_model_is_mutable_container(&dm)) {
            why = "You can't move or copy items to read-only volumes.";
        } else if (tiku_trk_fs_in_trash(app->fs, dst)) {
            if ((mods & TIKU_TRK_DRAG_MOD_LINK) != 0u) {
                why = "Sorry, you can't create links in the Trash.";
            } else if ((mods & TIKU_TRK_DRAG_MOD_COPY) != 0u) {
                why = "Sorry, you can't copy items to the Trash.";
            }
        }
        if (why == NULL) {
            char refused[200];

            /* The engine's own refusals -- a folder into itself, the Trash
             * itself, a file over a folder -- said in its words. */
            if (tiku_trk_fs_check_move(app->fs, app->drag.path[0], dst,
                                       refused, sizeof refused) !=
                    TIKU_TRK_REFUSE_NONE) {
                tiku_trk_alert_open(alert_model(app), TIKU_TRK_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_REFUSAL, refused,
                                    "Cancel");
                return;
            }
        } else {
            tiku_trk_alert_open(alert_model(app), TIKU_TRK_ALERT_WARN,
                                TIKU_TRK_ALERT_TAG_REFUSAL, why, "Cancel");
            return;
        }
    }

    /* Same store or not is the engine's question, not a path comparison:
     * it already knows how to tell, and it is what decides move or copy. */
    same = (tiku_trk_fs_drop_action(app->fs, app->drag.path[0], dst, 0) ==
            TIKU_TRK_OP_MOVE);
    what = tiku_trk_drag_action(&app->drag, dst, mods, same);
    if (what == TIKU_TRK_DROP_NOTHING) {
        return;
    }
    for (i = 0; i < app->drag.count; i++) {
        sel[n++] = app->drag.path[i];
    }
    if (n == 0) {
        return;
    }
    /* The secondary button asks rather than assumes: the menu already
     * exists and had no caller (PVS-039, FS-003). */
    if (button == 3u) {
        tiku_trk_model_t dm;
        int dst_ro = 0, src_ro = 0;

        if (app->backend->ops->stat(app->backend, dst, &dm) == 0) {
            dst_ro = !tiku_trk_model_is_mutable_container(&dm);
        }
        if (app->backend->ops->stat(app->backend, app->drag.src_dir,
                                    &dm) == 0) {
            src_ro = !tiku_trk_model_is_mutable_container(&dm);
        }
        snprintf(app->drop_dst, sizeof app->drop_dst, "%s", dst);
        app->pending = app->drag;   /* the release is about to clear it */
        app->popup = tiku_trk_cwmenu_drop(&app->menus, dst_ro, src_ro,
                                          (state & TIKU_DESK_MOD_SHIFT) ?
                                              TIKU_TRK_MOD_SHIFT : 0u);
        app->popup_sub = NULL;
        app->popup_frame = tiku_trk_menu_place(app->popup, x, y,
                               (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
        return;
    }
    if (app->drop_row < 0 &&
        tiku_trk_poseview_mode(app->drop_fl->view) != TIKU_TRK_MODE_LIST) {
        /* Reserved BEFORE the operation runs: the entries appear from the
         * folder watch, and a view told nothing about them auto-places
         * them at the top-left -- so the arrangement has to be waiting
         * (PVS-045, PVL-027). */
        tiku_desk_rect_t c = tiku_trk_folder_window_content(app->drop_fl);
        int ox = 0, oy = 0;

        tiku_trk_poseview_origin(app->drop_fl->view, &ox, &oy);
        for (i = 0; i < n; i++) {
            const char *name = strrchr(sel[i], '/');
            int lx = 0, ly = 0;

            tiku_trk_drag_landing(&app->drag, i, x - (c.x + 2) + ox,
                                  y - (c.y + 2) + oy, &lx, &ly);
            tiku_trk_poseview_drop_land(app->drop_fl->view,
                                        (name != NULL) ? name + 1 : sel[i],
                                        lx, ly);
        }
    }
    snprintf(app->prog_dest, sizeof app->prog_dest, "%s", dst);
    switch (what) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_trk_fs_copy(app->fs, sel, n, dst, TIKU_TRK_CONFLICT_ASK);
        snprintf(app->status, sizeof app->status, "copying %d item%s", n,
                 (n == 1) ? "" : "s");
        break;
    case TIKU_TRK_DROP_LINK:
        {
            /* Shift with the link modifier asks for a RELATIVE link: the
             * two are one gesture with a variant, not two gestures, and
             * the engine already knows how to make either (FS-002). */
            int rel = (state & TIKU_DESK_MOD_SHIFT) != 0u;

            (void)tiku_trk_fs_link(app->fs, sel, n, dst, rel);
            snprintf(app->status, sizeof app->status, "%s %d item%s",
                     rel ? "linking (relative)" : "linking", n,
                     (n == 1) ? "" : "s");
        }
        break;
    default:
        (void)tiku_trk_fs_move(app->fs, sel, n, dst, TIKU_TRK_CONFLICT_ASK);
        snprintf(app->status, sizeof app->status, "moving %d item%s", n,
                 (n == 1) ? "" : "s");
        break;
    }
}

static void
drain_fs(tiku_trk_fs_t *fs)
{
    int guard = 0;

    if (fs == NULL) {
        return;
    }
    /* 50 turns of 100 ms, the original's own budget. */
    while (tiku_trk_fs_busy(fs) && guard++ < 50) {
        if (tiku_trk_fs_step(fs, 16) == 0) {
            const tiku_trk_progress_t *pr = tiku_trk_fs_progress(fs);

            /* A question stops the queue and there is nobody left to
             * answer it, so answer it the way the running shell would and
             * keep going rather than spinning out the whole budget. */
            if (pr->conflict) {
                tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_RENAME);
            } else if (pr->error_ask) {
                tiku_trk_fs_resolve_error(fs, 1);
            } else {
                break;
            }
        }
        usleep(100000);
    }
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
    folder_t *fl = open_folder(app, title);
    tiku_trk_frame_t fr;
    int i, n;
    tiku_trk_model_t *set;

    if (fl == NULL) {
        tiku_trk_query_stop(run);
        return NULL;
    }
    tiku_trk_poseview_set_layout_store(fl->view, app->store);
    tiku_trk_poseview_set_query_type(fl->view,
                                    tiku_trk_query_search_type(run));
    snprintf(fl->state_path, sizeof fl->state_path, "%s",
             tiku_trk_poseview_layout_path(fl->view));
    if (tiku_trk_frame_load(app->store, fl->state_path, &fr)) {
        tiku_trk_frame_clamp(&fr, DESK_W, DESK_H);
        fl->win->frame = (tiku_desk_rect_t){ fr.x, fr.y, fr.w, fr.h };
        fl->opened_with = fr;
        fl->had_frame = 1;
    } else {
        fl->had_frame = 0;
    }
    n = tiku_trk_query_count(run);
    set = calloc((size_t)(n > 0 ? n : 1), sizeof *set);
    if (set != NULL) {
        for (i = 0; i < n; i++) {
            set[i] = *tiku_trk_query_at(run, i);
        }
        (void)tiku_trk_poseview_set_results(fl->view, set, n);
        free(set);
    }
    if (fl->run != NULL) {
        tiku_trk_query_stop(fl->run);
    }
    fl->run = run;
    fl->live = 1;
    return fl;
}

/** @brief Re-run a live window's query and apply what changed. */
static int
refresh_results(desktop_t *app, folder_t *fl)
{
    tiku_trk_qdiff_t d;
    tiku_trk_model_t *set;
    int i, n;

    if (fl == NULL || fl->run == NULL || !fl->live) {
        return 0;
    }
    if (tiku_trk_query_refresh(fl->run, &d) != 0) {
        return 0;
    }
    /* Membership is not the only thing that moves: a result whose size or
     * kind changed with the set unchanged still has to be re-applied, or a
     * query window updates on strictly less than a folder window does. */
    n = tiku_trk_query_count(fl->run);
    set = calloc((size_t)(n > 0 ? n : 1), sizeof *set);
    if (set != NULL) {
        for (i = 0; i < n; i++) {
            set[i] = *tiku_trk_query_at(fl->run, i);
        }
        /* Through the handler, so a result still present keeps its row and
         * its selection instead of the window being rebuilt under the user. */
        (void)tiku_trk_poseview_apply_results(fl->view, set, n,
                                              tiku_trk_now());
        free(set);
    }
    /* A row that left because its node is GONE is a different event from one
     * that merely stopped matching, and only the first is worth saying. */
    if (d.vanished > 0) {
        snprintf(app->status, sizeof app->status,
                 "%d gone, %d no longer match, %d new", d.vanished,
                 d.removed - d.vanished, d.added);
    } else {
        snprintf(app->status, sizeof app->status, "%d in, %d out", d.added,
                 d.removed);
    }
    app->dirty = 1;
    return 1;
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
    tiku_trk_pose_t *p;

    if (fl == NULL || app->fs == NULL) {
        return;
    }
    p = tiku_trk_poseview_at(fl->view, row);
    if (p == NULL) {
        return;
    }
    if (p->model.kind == TIKU_TRK_KIND_DEVICE_NODE ||
        p->model.kind == TIKU_TRK_KIND_VIRTUAL_DIR) {
        snprintf(app->status, sizeof app->status,
                 "'%.40s' is part of the device namespace and cannot be "
                 "renamed", p->model.name);
        app->dirty = 1;
        return;
    }
    app->renaming = 1;
    app->ren_fl = fl;
    app->ren_row = row;
    tiku_trk_edit_start(&app->ren, p->model.name);
    tiku_trk_edit_deny(&app->ren, tiku_trk_fs_name_deny());
    snprintf(app->ren_path, sizeof app->ren_path, "%s", p->model.path);
    app->status[0] = '\0';
    app->dirty = 1;
}

/** @brief Finish the rename, keeping or discarding what was typed. */
static void
commit_rename(desktop_t *app, int commit)
{
    char err[160];

    if (!app->renaming) {
        return;
    }
    app->renaming = 0;
    tiku_trk_edit_stop(&app->ren);
    {   /* MA-050: an unchanged name is not a rename. */
        const char *slash = strrchr(app->ren_path, '/');
        const char *was = (slash != NULL) ? slash + 1 : app->ren_path;

        if (commit && strcmp(was, app->ren.text) == 0) {
            commit = 0;
        }
    }
    if (commit && app->ren.len > 0) {
        if (tiku_trk_fs_rename(app->fs, app->ren_path, app->ren.text, err,
                               sizeof err) != 0) {
            snprintf(app->status, sizeof app->status, "%s", err);
        } else {
            app->status[0] = '\0';
        }
    }
    app->dirty = 1;
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
        snprintf(app->status, sizeof app->status, "%s", error);
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
    tiku_trk_pose_t *p;

    if (fl == NULL) {
        return;
    }
    p = tiku_trk_poseview_active(fl->view);
    if (p == NULL) {
        return;
    }
    if (!tiku_trk_attr_editable(&p->model, &p->desc)) {
        snprintf(app->status, sizeof app->status, "%.40s cannot be written",
                 p->model.name);
        app->dirty = 1;
        return;
    }
    app->editing = 1;
    app->edit_desc = p->desc;
    snprintf(app->edit_path, sizeof app->edit_path, "%s", p->model.path);
    /* MA-045: opened with the whole value selected, exactly as the rename
     * editor is, so the first keystroke replaces the reading rather than
     * appending to it -- a value is REPLACED far more often than extended. */
    tiku_trk_edit_start(&app->ed, p->value_valid ? p->value : "");
    app->status[0] = '\0';
    app->dirty = 1;
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
    int i, n = tiku_trk_poseview_count(fl->view), k = 0;

    (void)app;
    for (i = 0; i < n && k < max; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);

        if (p == NULL || !p->selected) {
            continue;
        }
        snprintf(paths[k], TIKU_TRK_PATH_MAX, "%s", p->model.path);
        out[k] = paths[k];
        k++;
    }
    return k;
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
static void
select_named(desktop_t *app, folder_t *fl, const char *name)
{
    int i, n;

    if (fl == NULL || name == NULL || name[0] == '\0') {
        return;
    }
    n = tiku_trk_poseview_count(fl->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);

        if (p != NULL && strcmp(p->model.name, name) == 0) {
            tiku_trk_poseview_click(fl->view, i, 0u);
            tiku_trk_poseview_scroll_to(fl->view, i);
            app->dirty = 1;
            return;
        }
    }
}

/** @brief Carry out a menu choice on the focused folder. */
static void
menu_invoke(desktop_t *app, folder_t *fl, int command)
{
    if (fl == NULL) {
        return;
    }
    if (tiku_trk_folder_actions_invoke(app->desk, fl, command)) {
        app->dirty = 1;
        return;
    }
    switch (command) {
    case TIKU_TRK_CMD_OPEN: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        if (p != NULL && tiku_trk_model_is_container(&p->model)) {
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
        (void)tiku_trk_info_windows_open(&app->info_windows,
            (p != NULL && p->selected) ? p->model.path : fl->path);
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
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
    case TIKU_TRK_CMD_DELETE: {
        char paths[64][TIKU_TRK_PATH_MAX];
        const char *sel[64];
        int n = folder_selection(app, fl, paths, sel, 64);

        if (n > 0) {
            /* The menu item and the key agree because both ask the same
             * question of the same items (FS-046, FS-047). */
            tiku_trk_delete_q_t q = tiku_trk_fs_delete_question(app->fs,
                sel[0], command == TIKU_TRK_CMD_DELETE);

            if (q == TIKU_TRK_DELETE_STRAIGHT) {
                snprintf(app->status, sizeof app->status,
                         "deleting %d item%s -- this cannot be undone", n,
                         (n == 1) ? "" : "s");
                (void)tiku_trk_fs_delete(app->fs, sel, n);
                {
                    int j;

                    for (j = 0; j < n; j++) {
                        close_windows_under(app, sel[j]);
                    }
                }
            } else {
                snprintf(app->status, sizeof app->status,
                         "moving %d item%s to the Trash", n,
                         (n == 1) ? "" : "s");
                {   /* Some folders are not the user's to move without
                     * being asked twice; Shift is the second answer, and
                     * the check runs before a byte moves (FS-027). */
                    int j, stop = 0;

                    for (j = 0; j < n && !stop; j++) {
                        tiku_trk_confirm_t c =
                            tiku_trk_fs_confirm_move(sel[j]);

                        if (c == TIKU_TRK_CONFIRM_OVERRIDE &&
                            !(command == TIKU_TRK_CMD_DELETE)) {
                            char msg[300];

                            /* Asked properly now the layer exists: the
                             * confirmation needs Shift, so the alert says
                             * which key rather than refusing outright
                             * (FS-027). */
                            snprintf(msg, sizeof msg,
                                     "\"%.60s\" is a system folder.  Hold "
                                     "Shift and choose Move to confirm.",
                                     sel[j]);
                            tiku_trk_alert_open(alert_model(app),
                                TIKU_TRK_ALERT_STOP,
                                TIKU_TRK_ALERT_TAG_REFUSAL, msg, "Cancel");
                            stop = 1;
                        } else if (c == TIKU_TRK_CONFIRM_ASK) {
                            snprintf(app->status, sizeof app->status,
                                     "'%.60s' holds settings -- moving it "
                                     "anyway", sel[j]);
                        }
                    }
                    if (stop) {
                        break;
                    }
                }
                (void)tiku_trk_fs_trash(app->fs, sel, n);
                {   /* A window onto a folder that has just gone is a
                     * window onto nothing, and so is a window onto
                     * anything inside it (FS-042). */
                    int j;

                    for (j = 0; j < n; j++) {
                        close_windows_under(app, sel[j]);
                    }
                }
            }
        }
        break;
    }
    case TIKU_TRK_CMD_RESTORE: {
        char paths[64][TIKU_TRK_PATH_MAX];
        const char *sel[64];
        char err[200];
        int n = folder_selection(app, fl, paths, sel, 64);
        int back = tiku_trk_fs_restore(app->fs, sel, n, err, sizeof err);

        if (back > 0) {
            snprintf(app->status, sizeof app->status,
                     "put %d item%s back", back, (back == 1) ? "" : "s");
        } else if (err[0] != '\0') {
            snprintf(app->status, sizeof app->status, "%s", err);
        }
        break;
    }
    case TIKU_TRK_CMD_EMPTY_TRASH: {
        int n = tiku_trk_fs_empty_trash(app->fs);

        snprintf(app->status, sizeof app->status,
                 (n > 0) ? "emptying the Trash: %d item%s"
                         : "the Trash is empty", n, (n == 1) ? "" : "s");
        break;
    }
    case TIKU_TRK_CMD_CUT:
    case TIKU_TRK_CMD_CUT_MORE:
    case TIKU_TRK_CMD_COPY:
    case TIKU_TRK_CMD_COPY_MORE: {
        tiku_trk_model_t sel[64];
        int i2, k = 0, n2 = tiku_trk_poseview_count(fl->view);
        int cut = (command == TIKU_TRK_CMD_CUT ||
                   command == TIKU_TRK_CMD_CUT_MORE);
        int more = (command == TIKU_TRK_CMD_CUT_MORE ||
                    command == TIKU_TRK_CMD_COPY_MORE);

        for (i2 = 0; i2 < n2 && k < 64; i2++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i2);

            if (p != NULL && p->selected) {
                sel[k++] = p->model;
            }
        }
        /* Nothing is copied here: the clipboard holds references and a
         * mode, and the work happens at the Paste. */
        (void)tiku_trk_clip_add(&app->clip, sel, k,
                                cut ? TIKU_TRK_CLIP_MOVE
                                    : TIKU_TRK_CLIP_COPY, !more);
        snprintf(app->status, sizeof app->status, "%d item%s on the clipboard",
                 tiku_trk_clip_count(&app->clip),
                 (tiku_trk_clip_count(&app->clip) == 1) ? "" : "s");
        break;
    }
    case TIKU_TRK_CMD_MOVE_HERE:
    case TIKU_TRK_CMD_COPY_HERE:
    case TIKU_TRK_CMD_LINK_HERE:
    case TIKU_TRK_CMD_REL_LINK_HERE: {
        /* The answer to the secondary-button drop.  The payload is still
         * held because the release deliberately did not clear it when the
         * menu went up (FS-003). */
        const char *sel[TIKU_TRK_DRAG_MAX];
        int i, n = 0;

        for (i = 0; i < app->pending.count; i++) {
            sel[n++] = app->pending.path[i];
        }
        if (n == 0 || app->drop_dst[0] == '\0') {
            break;
        }
        if (command == TIKU_TRK_CMD_COPY_HERE) {
            (void)tiku_trk_fs_copy(app->fs, sel, n, app->drop_dst,
                                   TIKU_TRK_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status, "copying %d item%s",
                     n, (n == 1) ? "" : "s");
        } else if (command == TIKU_TRK_CMD_MOVE_HERE) {
            (void)tiku_trk_fs_move(app->fs, sel, n, app->drop_dst,
                                   TIKU_TRK_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status, "moving %d item%s",
                     n, (n == 1) ? "" : "s");
        } else {
            (void)tiku_trk_fs_link(app->fs, sel, n, app->drop_dst,
                       command == TIKU_TRK_CMD_REL_LINK_HERE);
            snprintf(app->status, sizeof app->status, "linking %d item%s",
                     n, (n == 1) ? "" : "s");
        }
        tiku_trk_drag_reset(&app->pending);
        app->drop_dst[0] = '\0';
        break;
    }
    case TIKU_TRK_CMD_PREFERENCES:
        tiku_trk_prefs_window_open(&app->prefs_window);
        break;
    case TIKU_TRK_CMD_NEW_FOLDER: {
        char made[TIKU_TRK_PATH_MAX], err[200];

        if (app->fs != NULL &&
            tiku_trk_fs_new_folder(app->fs, fl->path, made, sizeof made,
                                   err, sizeof err) == 0) {
            const char *slash = strrchr(made, '/');

            snprintf(app->status, sizeof app->status, "created '%.60s'",
                     (slash != NULL) ? slash + 1 : made);
        } else {
            snprintf(app->status, sizeof app->status, "%.150s",
                     err[0] ? err : "could not create a new folder");
        }
        break;
    }
    case TIKU_TRK_CMD_PASTE:
    case TIKU_TRK_CMD_PASTE_LINKS: {
        char paths[64][TIKU_TRK_PATH_MAX];
        char here[64][TIKU_TRK_PATH_MAX];
        const char *sel[64], *same[64];
        int i2, k = 0, nsame = 0, moving = 0;
        int links = (command == TIKU_TRK_CMD_PASTE_LINKS);

        /* Entries whose files have gone are dropped rather than pasted:
         * a reference to nothing is not something to hand to a copy. */
        (void)tiku_trk_clip_prune(&app->clip);
        for (i2 = 0; i2 < tiku_trk_clip_count(&app->clip) && k < 64; i2++) {
            const tiku_trk_clip_entry_t *e = tiku_trk_clip_at(&app->clip, i2);

            {   /* An item pasted into the folder it already lives in is
                 * a DUPLICATE when it was copied, and nothing at all when
                 * it was cut: moving something to where it is has no
                 * meaning (FS-056). */
                const char *slash = strrchr(e->path, '/');
                size_t dl = (slash != NULL) ? (size_t)(slash - e->path) : 0u;

                if (!links && slash != NULL && strlen(fl->path) == dl &&
                    strncmp(fl->path, e->path, dl) == 0) {
                    if (e->mode != TIKU_TRK_CLIP_MOVE && nsame < 64) {
                        snprintf(here[nsame], sizeof here[nsame], "%s",
                                 e->path);
                        same[nsame] = here[nsame];
                        nsame++;
                    }
                    continue;
                }
            }
            snprintf(paths[k], sizeof paths[k], "%s", e->path);
            sel[k] = paths[k];
            if (e->mode == TIKU_TRK_CLIP_MOVE) {
                moving = 1;
            }
            k++;
        }
        if (nsame > 0) {
            (void)tiku_trk_fs_duplicate(app->fs, same, nsame);
            snprintf(app->status, sizeof app->status,
                     "duplicating %d item%s already here", nsame,
                     (nsame == 1) ? "" : "s");
            if (k == 0) {
                break;
            }
        }
        if (k == 0) {
            snprintf(app->status, sizeof app->status,
                     "the clipboard is empty");
            break;
        }
        /* Neither a copy nor a link belongs in the Trash: one would hide
         * a second copy where nothing looks, the other would leave a link
         * behind when the Trash is emptied (FS-014). */
        if (tiku_trk_fs_in_trash(app->fs, fl->path) && (links || !moving)) {
            snprintf(app->status, sizeof app->status, "%s",
                     links ? "Sorry, you can't create links in the Trash."
                           : "Sorry, you can't copy items to the Trash.");
            break;
        }
        /* What a Paste DOES is the mode the clipboard carries: cut items
         * move, copied items copy -- unless Shift asked for links, which
         * overrides both (FS-057). */
        if (links) {
            (void)tiku_trk_fs_link(app->fs, sel, k, fl->path, 0);
            snprintf(app->status, sizeof app->status,
                     "linking %d item%s here", k, (k == 1) ? "" : "s");
            break;
        }
        if (moving) {
            (void)tiku_trk_fs_move(app->fs, sel, k, fl->path,
                                   TIKU_TRK_CONFLICT_ASK);
            /* A cut is spent by its paste; a copy stays for the next one. */
            tiku_trk_clip_clear(&app->clip);
        } else {
            (void)tiku_trk_fs_copy(app->fs, sel, k, fl->path,
                                   TIKU_TRK_CONFLICT_ASK);
        }
        snprintf(app->status, sizeof app->status, "%s %d item%s here",
                 moving ? "moving" : "copying", k, (k == 1) ? "" : "s");
        break;
    }
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
    folder_t *fl;
    tiku_desk_rect_t frame;
    int i;

    /* Already open?  Raise it: one window per place. */
    for (i = 0; i < app->folder_count; i++) {
        if (strcmp(app->folders[i].path, path) == 0 &&
            app->folders[i].win != NULL && app->folders[i].win->open) {
            tiku_desk_workspace_activate(app->desk, app->folders[i].win);
            app->dirty = 1;
            return &app->folders[i];
        }
    }
    if (app->folder_count >= TIKU_DESK_WINDOW_MAX) {
        return NULL;
    }
    fl = &app->folders[app->folder_count];
    memset(fl, 0, sizeof *fl);
    snprintf(fl->path, sizeof fl->path, "%s", path);
    snprintf(fl->state_path, sizeof fl->state_path, "%s", path);
    /* Opening a folder is what puts it in the recent list the Deskbar
     * menu offers -- the list is a record of where the user has been, and
     * this is the one place that knows (DB-002, AW-056). */
    tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS, path);
    fl->view = tiku_trk_poseview_new(app->backend, path);
    if (fl->view == NULL) {
        return NULL;
    }
    tiku_trk_folder_window_configure(fl, &app->folder_render);
    /* The columns this folder was left with.  The pair has existed since
     * the single-window shell was built and this shell called neither, so
     * every column a user added, moved or resized here was discarded when
     * the window closed (CW-005, MA-070). */
    tiku_trk_poseview_set_folders_first(fl->view,
        tiku_trk_prefs_get(prefs_model(app), "SortFolderNamesFirst"));
    (void)tiku_trk_poseview_restore_columns(fl->view);
    (void)tiku_trk_poseview_restore_state(fl->view);

    /* Cascade, so a new window never lands exactly on the last one. */
    if (app->cascade_x == 0 && app->cascade_y == 0) {
        app->cascade_x = CASCADE_HOME_X;
        app->cascade_y = CASCADE_HOME_Y;
    }
    frame.x = app->cascade_x;
    frame.y = app->cascade_y;
    frame.w = 460;
    frame.h = 300;
    app->cascade_x += CASCADE_STEP;
    app->cascade_y += CASCADE_STEP;
    {
        /* Where this folder's window WAS, if it has ever been open; failing
         * that what its parent's window is, offset; failing that the
         * cascade above.  This is the spatial promise: a folder opens where
         * you left it (CW-001, CW-005). */
        tiku_trk_frame_t fr;

        fl->had_frame = tiku_trk_frame_load(app->store, path, &fr);
        if (!fl->had_frame) {
            /* Inherited frames are not the folder's own, so they are not
             * remembered as one: only what the user does to the window
             * counts as its place. */
            if (tiku_trk_frame_inherit(app->store, path, &fr)) {
                tiku_trk_frame_clamp(&fr, DESK_W, DESK_H);
                frame.x = fr.x;
                frame.y = fr.y;
                frame.w = fr.w;
                frame.h = fr.h;
            }
        } else {
            tiku_trk_frame_clamp(&fr, DESK_W, DESK_H);
            frame.x = fr.x;
            frame.y = fr.y;
            frame.w = fr.w;
            frame.h = fr.h;
            fl->opened_with = fr;
        }
    }
    if (app->cascade_x + frame.w > DESK_W ||
        app->cascade_y + frame.h > DESK_H) {
        app->cascade_x = CASCADE_HOME_X;
        app->cascade_y = CASCADE_HOME_Y;
    }
    /* A frame that would land entirely off the screen is pulled back on:
     * a window nobody can reach is the same as no window. */
    if (frame.x >= DESK_W || frame.y >= DESK_H || frame.x + frame.w <= 0 ||
        frame.y + frame.h <= 0) {
        frame.x = CASCADE_HOME_X;
        frame.y = CASCADE_HOME_Y;
    }
    fl->win = tiku_desk_workspace_open(app->desk, path, frame,
        tiku_trk_folder_window_draw, fl, fl->path);
    if (fl->win == NULL) {
        tiku_trk_poseview_free(fl->view);
        return NULL;
    }
    app->folder_count++;
    app->dirty = 1;
    return fl;
}

/** @brief The folder behind a window, or NULL. */
static folder_t *
folder_of(desktop_t *app, tiku_desk_window_t *w)
{
    int i;

    for (i = 0; w != NULL && i < app->folder_count; i++) {
        if (app->folders[i].win == w) {
            return &app->folders[i];
        }
    }
    return NULL;
}

/** @brief Re-evaluate a band in view coordinates, including any new scroll. */
static void
folder_band_select(desktop_t *app)
{
    tiku_desk_rect_t c;
    int ox = 0, oy = 0;

    if (app == NULL || app->band_fl == NULL) {
        return;
    }
    c = tiku_trk_folder_window_content(app->band_fl);
    tiku_trk_poseview_origin(app->band_fl->view, &ox, &oy);
    (void)tiku_trk_poseview_select_rect(app->band_fl->view,
        app->band_x0 - (c.x + 2) + ox,
        app->band_y0 - (c.y + 2) + oy,
        app->band_x1 - (c.x + 2) + ox,
        app->band_y1 - (c.y + 2) + oy, app->band_mods);
}

/** @brief Resolve a visible focused-window row for a scripted click. */
static int
script_row_point(void *ctx, int row, int *x, int *y)
{
    desktop_t *app = (desktop_t *)ctx;
    tiku_desk_window_t *fw;
    folder_t *fl;
    tiku_desk_rect_t c;
    int rel, visible;

    if (app == NULL || x == NULL || y == NULL || row < 0) {
        return 0;
    }
    fw = tiku_desk_workspace_focused(app->desk);
    fl = (fw != NULL) ? folder_of(app, fw) : NULL;
    if (fl == NULL || fl->view == NULL ||
        tiku_trk_poseview_mode(fl->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    c = tiku_trk_folder_window_content(fl);
    rel = row - tiku_trk_poseview_scroll(fl->view);
    visible = (c.h - COUNT_H - 4) / app->row_h;
    if (rel < 0 || rel >= visible ||
        row >= tiku_trk_poseview_count(fl->view)) {
        return 0;
    }
    *x = c.x + 20;
    *y = c.y + 2 + rel * app->row_h + app->row_h / 2;
    return 1;
}


/** @brief What is drawn at @p x, @p y right now, 0xRRGGBB (script). */
static unsigned
script_pixel_at(void *ctx, int x, int y)
{
    desktop_t *app = (desktop_t *)ctx;

    if (app == NULL || app->surf == NULL || app->surf->px == NULL ||
        x < 0 || y < 0 || x >= app->surf->w || y >= app->surf->h) {
        return 0u;
    }
    return (unsigned)app->surf->px[(size_t)y * (size_t)app->surf->w +
                                   (size_t)x] & 0xffffffu;
}

/**
 * @brief Where the pose named @p name sits in the focused view (script).
 *
 * In view pixels, which is the space a drop is expressed in -- the check
 * is about the ARRANGEMENT a drop left behind, not about the window.
 */
static int
script_pose_at(void *ctx, const char *name, int *x, int *y)
{
    desktop_t *app = (desktop_t *)ctx;
    tiku_desk_window_t *fw;
    folder_t *fl;
    int i, n;

    if (app == NULL || name == NULL) {
        return 0;
    }
    fw = tiku_desk_workspace_focused(app->desk);
    fl = (fw != NULL) ? folder_of(app, fw) : NULL;
    for (;;) {
        if (fl != NULL && fl->view != NULL) {
            n = tiku_trk_poseview_count(fl->view);
            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);
                int pw, ph;

                if (p == NULL || strcmp(p->model.name, name) != 0) {
                    continue;
                }
                tiku_trk_poseview_pose_rect(fl->view, p, x, y, &pw, &ph);
                return 1;
            }
        }
        if (fl == desktop_folder(app)) {
            return 0;
        }
        /* The other place a pose can be.  The Desktop has no window, so it
         * is never the focused one and would otherwise be unaskable. */
        fl = desktop_folder(app);
    }
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

static void
draw_all(desktop_t *app)
{
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
            tiku_trk_paint_tint(app->surf, box, TIKU_DESK_C_BACKDROP,
                                255 - alpha);
        }
    }
    /* Drawing another folder rebuilt the one shared menu model for that
     * folder.  Put it back on the bar that actually owns the popup before
     * painting or hit-testing it. */
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
        tiku_trk_menu_draw(app->popup, app->surf, app->popup_frame);
        if (app->popup_sub != NULL) {
            tiku_trk_menu_draw(app->popup_sub, app->surf,
                               app->popup_sub_frame);
        }
    }
    if (app->nav_on && app->popup != NULL) {
        /* Every open level, outermost first, each hanging off the row that
         * opened it -- so the trail back out is on screen (CW-045). */
        const tiku_desk_font_t *nf = tiku_desk_font_plain();
        int px = app->popup_frame.x + app->popup_frame.w - 4;
        int py = app->popup_frame.y + 4;
        int k;

        for (k = 0; k < app->nav.depth; k++) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];
            tiku_desk_rect_t r;
            int i;

            tiku_trk_nav_place(&app->nav, k, px, py, DESK_W, DESK_H,
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
    tiku_trk_status_overlay_draw(&app->status_overlay);
    app->dirty = 0;
}

static int
tracker_run(int argc, char **argv)
{
    desktop_t app;
    const char *port = NULL, *shot = NULL, *script_path = NULL;
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
    app.surf = tiku_desk_surface_new(DESK_W, DESK_H, TIKU_DESK_C_BACKDROP);
    app.row_h = LIST_ROW_H;
    app.desk = tiku_desk_workspace_new(app.surf);
    app.deskbar = tiku_trk_deskbar_new(app.backend);
    app.team = tiku_trk_deskbar_add_team(app.deskbar, "Tracker");
    tiku_trk_deskbar_scan_processes(app.deskbar);
    tiku_trk_cwmenu_init(&app.menus);
    tiku_trk_status_overlay_init(&app.status_overlay, app.surf,
        DESK_W, DESK_H, &app.dirty, status_answer, status_stop, &app);
    tiku_trk_find_window_init(&app.find_window, app.desk, DESK_W, DESK_H,
        &app.dirty, app.status, sizeof app.status, panel_search, &app);
    tiku_trk_select_window_init(&app.select_window, app.desk,
        DESK_W, DESK_H, &app.dirty, apply_select_model, &app);
    tiku_trk_clip_init(&app.clip);
    /*
     * A SIDECAR, not a volatile store.  Everything kept here -- saved
     * queries, the panel's frame, the recent lists, favourites, the
     * thumbnails' stamps -- was written to a store documented as being for
     * "tests, and nodes whose state must not persist", so every one of them
     * was forgotten at exit while appearing to be saved.  It cannot be the
     * backend's own store either: several of these live under reserved KEYS
     * rather than under paths, and the file-keyed store can only write
     * where there is a file.
     */
    app.store = tiku_trk_store_sidecar_open(
                    (app.backend->devid[0] != '\0') ? app.backend->devid
                                                    : "local");
    if (app.store == NULL) {
        app.store = tiku_trk_store_memory_open();
    }
    tiku_trk_find_window_set_store(&app.find_window, app.store);
    (void)tiku_trk_poseview_install_query_defaults(app.backend, app.store);
    {
        /* Where the bar was docked and how wide it was left.  Kept with
         * the session rather than with any folder: the bar belongs to the
         * desktop (DB-048, DB-050). */
        int32_t rec[2];

        if (tiku_trk_state_read(app.store, TIKU_TRK_KEY_SESSION,
                                TIKU_TRK_ATTR_DOCK, rec, sizeof rec) ==
                (int)sizeof rec) {
            tiku_trk_deskbar_set_dock(app.deskbar,
                                      (tiku_trk_db_dock_t)rec[0]);
            (void)tiku_trk_deskbar_set_width(app.deskbar, (int)rec[1]);
        }
    }
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
    tiku_trk_thumb_init(&app.thumbs);
    tiku_trk_nodeicon_init(&app.nicons);
    tiku_trk_volwatch_init(&app.volwatch);
    app.info_relink = -1;
    tiku_trk_info_windows_init(&app.info_windows, app.desk, app.backend,
        app.store, app.fs, alert_model(&app), &app.dirty);
    tiku_trk_panel_window_init(&app.file_panel, app.desk, app.backend,
        app.store, &app.nicons, app.row_h, DESK_W, DESK_H, &app.dirty,
        fpanel_commit, &app);
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
    tiku_trk_switcher_window_init(&app.switcher_window, app.desk,
        app.deskbar, DESK_W, DESK_H, &app.dirty, app.status,
        sizeof app.status);
    app.folder_render = (tiku_trk_folder_render_t){
        .workspace = app.desk,
        .menus = &app.menus,
        .prefs = prefs_model(&app),
        .clip = &app.clip,
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
        .editing = &app.editing,
        .edit_path = app.edit_path,
        .value_edit = &app.ed,
        .renaming = &app.renaming,
        .rename_folder = &app.ren_fl,
        .rename_row = &app.ren_row,
        .rename_edit = &app.ren,
        .band_folder = &app.band_fl,
        .band_x0 = &app.band_x0,
        .band_y0 = &app.band_y0,
        .band_x1 = &app.band_x1,
        .band_y1 = &app.band_y1,
        .drop_folder = &app.drop_fl,
        .drop_row = &app.drop_row,
        .prepare_menu = tiku_trk_folder_menu_prepare,
        .prepare_context = &app.folder_menu
    };

    app.desk_row = -1;
    (void)tiku_trk_desktop_view_init(&app.desktop_view, app.backend, app.fs,
        prefs_model(&app), app.deskbar, &app.folder_render, DESK_W, DESK_H);

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
    if (nopen > 0) {
        for (i = 0; i < nopen; i++) {
            (void)open_folder(&app, openme[i]);
        }
    } else {
        /* What was open last time, if anything -- the windows come back
         * where they were, which is the whole of the spatial promise
         * (AW-004).  A safe start asks for nothing and gets home. */
        tiku_trk_session_entry_t sess[TIKU_TRK_SESSION_MAX];
        int nsess = safe_mode ? 0
                              : tiku_trk_session_load_entries(app.store, sess,
                                                              TIKU_TRK_SESSION_MAX);
        int opened = 0;

        for (i = 0; i < nsess; i++) {
            folder_t *restored;

            /* Root is the Disks window in this port.  Keeping a stale root
             * record must not override the user's current icon setting. */
            if (strcmp(sess[i].path, "/") == 0 &&
                !tiku_trk_desktop_view_shows_disks(&app.desktop_view)) {
                continue;
            }
            restored = open_folder(&app, sess[i].path);
            if (restored != NULL) {
                restored->win->minimized = sess[i].minimized;
                opened++;
            }
        }
        if (opened == 0) {
            if (use_device) {
                (void)open_folder(&app, "/sys/mem");
                (void)open_folder(&app, "/dev");
            } else {
                const char *home = getenv("HOME");

                (void)open_folder(&app, home ? home : "/");
            }
        }
    }

    (void)tiku_trk_find_open_default(find_model(&app), (int64_t)time(NULL));

    if (script_path != NULL) {
        /* Displayless.  The shell draws into the same surface either way,
         * so a scripted run exercises the real draw path -- it just has
         * nobody to show it to (see tiku_trk_script.h). */
        sc = tiku_trk_script_open(script_path);
        if (sc == NULL) {
            return 2;
        }
    } else {
        app.host = tiku_desk_host_open(DESK_W, DESK_H, "TikuOS Desktop");
        if (app.host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
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
                if (eq != NULL && app.editing) {
                    tiku_trk_edit_start(&app.ed, eq + 1);
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
                              (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
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
                              (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
    }
    app.running = 1;
    app.dirty = 1;

    while (app.running) {
        for (;;) {
            tiku_desk_event_t ev;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (sc != NULL) {
                /* One instruction per turn, so the deferred work between
                 * turns -- a stepped copy, the armed rename, the notice
                 * poll -- happens where it happens on a real clock. */
                tiku_trk_script_step_t sp;
                tiku_trk_script_state_t sst;
                tiku_desk_window_t *fw = tiku_desk_workspace_focused(app.desk);

                memset(&sst, 0, sizeof sst);
                sst.status = app.status;
                sst.title = (fw != NULL) ? fw->title : NULL;
                sst.scroll = 0;
                sst.rows = 0;
                sst.alert = alert_model(&app)->open ? alert_model(&app)->text : NULL;
                {
                    int wi, wn = 0;

                    for (wi = 0; wi < tiku_desk_workspace_count(app.desk); wi++) {
                        tiku_desk_window_t *ww = tiku_desk_workspace_at(app.desk, wi);

                        if (ww != NULL && ww->open) {
                            wn++;
                        }
                    }
                    sst.windows = wn;
                }
                sst.selected = 0;
                sst.sel_name = NULL;
                sst.renaming = app.renaming;
                sst.find_text = find_model(&app)->name_text;
                sst.find_type = tiku_trk_find_type_label(find_model(&app));
                sst.columns = 0;
                {
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    sst.bar_x = bf.x;
                    sst.bar_y = bf.y;
                }
                sst.win_x = (fw != NULL) ? fw->frame.x : -1;
                sst.win_y = (fw != NULL) ? fw->frame.y : -1;
                sst.kinds = 0;
                {
                    /* What the open menu says, so a script can check a menu
                     * whose items depend on the selection. */
                    static char labels[512];
                    int mi;

                    labels[0] = '\0';
                    if (app.popup != NULL) {
                        for (mi = 0; mi < app.popup->count; mi++) {
                            size_t at = strlen(labels);

                            snprintf(labels + at, sizeof labels - at, "%s|",
                                     app.popup->item[mi].label);
                        }
                    }
                    if (app.popup_sub != NULL) {
                        for (mi = 0; mi < app.popup_sub->count; mi++) {
                            size_t at = strlen(labels);

                            snprintf(labels + at, sizeof labels - at, "%s|",
                                     app.popup_sub->item[mi].label);
                        }
                    }
                    sst.menu = labels;
                }
                sst.count = app.count_text;
                sst.desk_selected = 0;
                sst.desk_sel_name = NULL;
                if (desktop_folder(&app)->view != NULL) {
                    int di, dn = tiku_trk_poseview_count(
                        desktop_folder(&app)->view);

                    for (di = 0; di < dn; di++) {
                        tiku_trk_pose_t *dp =
                            tiku_trk_poseview_at(desktop_folder(&app)->view,
                                                 di);

                        if (dp != NULL && dp->selected) {
                            if (sst.desk_sel_name == NULL) {
                                sst.desk_sel_name = dp->model.name;
                            }
                            sst.desk_selected++;
                        }
                    }
                }
                sst.pose_at = script_pose_at;
                sst.row_point = script_row_point;
                sst.pixel_at = script_pixel_at;
                sst.pose_ctx = &app;
                {
                    folder_t *sfl = (fw != NULL) ? folder_of(&app, fw) : NULL;

                    if (sfl != NULL) {
                        int si, sn = tiku_trk_poseview_count(sfl->view);

                        for (si = 0; si < sn; si++) {
                            tiku_trk_pose_t *sp =
                                tiku_trk_poseview_at(sfl->view, si);

                            if (sp != NULL) {
                                sst.kinds |= 2;
                                if (!tiku_trk_model_type_is(sp->model.type,
                                        find_model(&app)->type)) {
                                    sst.kinds |= 1;
                                }
                            }
                            if (sp != NULL && sp->selected) {
                                if (sst.sel_name == NULL) {
                                    sst.sel_name = sp->model.name;
                                }
                                sst.selected++;
                            }
                        }
                    }
                    if (sfl != NULL) {
                        {
                            const tiku_trk_column_t *cols = NULL;

                            sst.columns =
                                tiku_trk_poseview_columns(sfl->view, &cols);
                        }
                        sst.scroll = tiku_trk_poseview_scroll(sfl->view);
                        sst.rows = tiku_trk_poseview_count(sfl->view);
                    } else if (fw != NULL && fw ==
                               tiku_trk_panel_window_handle(
                                   &app.file_panel)) {
                        /* The panel has a view too, and it is the one the
                         * script is looking at while the panel is up. */
                        sst.scroll = tiku_trk_panel_window_scroll(
                            &app.file_panel);
                        sst.rows = tiku_trk_panel_window_rows(
                            &app.file_panel);
                    }
                }
                if (!tiku_trk_script_pump(sc, &sst, &sp)) {
                    app.running = 0;
                    break;
                }
                switch (sp.what) {
                case TIKU_TRK_SC_KEY:
                    ev.type = TIKU_DESK_EVENT_KEY_DOWN;
                    ev.key = sp.key;
                    ev.modifiers = sp.modifiers;
                    if (sp.key >= 0x20u && sp.key < 0x7fu) {
                        ev.text[0] = (char)sp.key;
                        if ((sp.modifiers & TIKU_DESK_MOD_SHIFT) != 0u &&
                            ev.text[0] >= 'a' && ev.text[0] <= 'z') {
                            ev.text[0] = (char)(ev.text[0] - 'a' + 'A');
                        }
                    }
                    have = 1;
                    break;
                case TIKU_TRK_SC_MOVE:
                    ev.type = TIKU_DESK_EVENT_POINTER_MOVE;
                    ev.x = sp.x;
                    ev.y = sp.y;
                    ev.modifiers = sp.modifiers;
                    have = 1;
                    break;
                case TIKU_TRK_SC_PRESS:
                case TIKU_TRK_SC_RELEASE:
                    ev.type = (sp.what == TIKU_TRK_SC_PRESS)
                                  ? TIKU_DESK_EVENT_POINTER_DOWN
                                  : TIKU_DESK_EVENT_POINTER_UP;
                    ev.x = sp.x;
                    ev.y = sp.y;
                    ev.modifiers = sp.modifiers;
                    ev.button = sp.button;
                    ev.time_us = tiku_trk_now();
                    have = 1;
                    break;
                case TIKU_TRK_SC_MENU_PICK: {
                    /* Name the item rather than guess its pixels: a menu's
                     * layout is the menu's business, and a script that
                     * hardcoded coordinates would break every time an item
                     * was added above the one it meant. */
                    tiku_trk_menu_t *m = (app.popup_sub != NULL)
                                             ? app.popup_sub : app.popup;
                    tiku_desk_rect_t fr = (app.popup_sub != NULL)
                                              ? app.popup_sub_frame
                                              : app.popup_frame;
                    int k;

                    if (m == NULL &&
                        tiku_trk_find_popup_open(find_model(&app))) {
                        m = (find_model(&app)->popup_sub != NULL)
                                ? find_model(&app)->popup_sub
                                : &find_model(&app)->popup;
                        fr = (find_model(&app)->popup_sub != NULL)
                                ? find_model(&app)->popup_sub_frame
                                : find_model(&app)->popup_frame;
                    }

                    if (m == NULL) {
                        fprintf(stderr, "script: no menu is open for "
                                        "'%s'\n", sp.path);
                        break;
                    }
                    for (k = 0; k < m->count; k++) {
                        if (!m->item[k].separator &&
                            strstr(m->item[k].label, sp.path) != NULL) {
                            ev.type = TIKU_DESK_EVENT_POINTER_DOWN;
                            ev.x = fr.x + 8;
                            ev.y = fr.y +
                                tiku_trk_menu_item_top(m, k) +
                                tiku_trk_menu_item_height(m, k) / 2;
                            ev.button = 1u;
                            ev.time_us = tiku_trk_now();
                            have = 1;
                            break;
                        }
                    }
                    if (!have) {
                        fprintf(stderr, "script: no menu item matching "
                                        "'%s'\n", sp.path);
                    }
                    break;
                }
                case TIKU_TRK_SC_SHOT:
                    draw_all(&app);
                    (void)tiku_desk_surface_png(app.surf, sp.path);
                    break;
                case TIKU_TRK_SC_QUIT:
                    app.running = 0;
                    break;
                default:
                    break;
                }
            } else if (tiku_desk_host_poll(app.host, &ev)) {
                have = 1;
            }
            if (!have) {
                break;
            }
            switch (ev.type) {
            case TIKU_DESK_EVENT_EXPOSE:
                app.dirty = 1;
                break;
            case TIKU_DESK_EVENT_POINTER_DOWN: {
                if (ev.button == 1u &&
                    (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    /* Before anything else, because a Ctrl+click on the
                     * bar means only one thing and every handler below
                     * would read it as something else (DB-049). */
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.x >= bf.x &&
                        ev.x < bf.x + bf.w &&
                        ev.y >= bf.y &&
                        ev.y < bf.y + bf.h) {
                        app.dock_drag = 1;
                        break;
                    }
                }
                {   /* An info window takes its own clicks first. */
                    tiku_desk_window_t *iw = tiku_desk_workspace_focused(app.desk);
                    int islot = tiku_trk_info_windows_slot(&app.info_windows,
                                                           iw);
                    if (islot >= 0 && !alert_model(&app)->open) {
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
                            /* Double-clicking the icon opens the item, as
                             * double-clicking its row would (AW-075). */
                            if (tiku_trk_now() - app.press_at < 500000) {
                                (void)open_folder(&app, info->path);
                            }
                            app.press_at = tiku_trk_now();
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

                        tiku_trk_nav_place(&app.nav, k, px2, py2, DESK_W,
                                           DESK_H, &r.x, &r.y, &r.w, &r.h);
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
                                                         : folder_of(&app, fw);
                    int pick = -1;

                    if (app.popup_sub != NULL) {
                        pick = tiku_trk_menu_item_at(app.popup_sub,
                                                     app.popup_sub_frame,
                                                     ev.x,
                                                     ev.y);
                        if (pick >= 0) {
                            menu_invoke(&app, ffl,
                                        app.popup_sub->item[pick].command);
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
                                        (tiku_desk_rect_t){ 0, 0, DESK_W,
                                                            DESK_H });
                                app.dirty = 1;
                                break;
                            }
                            menu_invoke(&app, ffl,
                                        app.popup->item[pick].command);
                        }
                    }
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
                    unsigned dmods = 0;
                    int secondary = (ev.button == 3);

                    if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                        dmods |= TIKU_TRK_MOD_CMD;
                    }
                    if (tiku_trk_deskbar_click(app.deskbar, ev.x,
                                               ev.y, dmods,
                                               secondary, &pick)) {
                        tiku_desk_window_t *t;

                        switch (pick.what) {
                        case TIKU_TRK_DB_FIND:
                            /* The leaf's Find item is the panel: the same
                             * entry point as Ctrl+F, reachable by mouse. */
                            open_panel(&app);
                            break;
                        case TIKU_TRK_DB_QUIT:
                            app.running = 0;
                            break;
                        case TIKU_TRK_DB_OPEN_PATH:
                            (void)open_folder(&app, pick.path);
                            break;
                        case TIKU_TRK_DB_ACTIVATE_WINDOW:
                            t = tiku_desk_workspace_find(app.desk, pick.tag);
                            if (t != NULL) {
                                t->minimized = 0;
                                tiku_desk_workspace_activate(app.desk, t);
                            }
                            break;
                        case TIKU_TRK_DB_MINIMIZE_WINDOW:
                            t = tiku_desk_workspace_find(app.desk, pick.tag);
                            if (t != NULL) { t->minimized = 1; }
                            break;
                        case TIKU_TRK_DB_CLOSE_WINDOW:
                            t = tiku_desk_workspace_find(app.desk, pick.tag);
                            close_window(&app, t);
                            break;
                        case TIKU_TRK_DB_HIDE_ALL:
                        case TIKU_TRK_DB_SHOW_ALL: {
                            int k, m = tiku_desk_workspace_count(app.desk);
                            for (k = 0; k < m; k++) {
                                tiku_desk_window_t *ww =
                                    tiku_desk_workspace_at(app.desk, k);
                                if (ww != NULL) {
                                    ww->minimized =
                                        (pick.what == TIKU_TRK_DB_HIDE_ALL);
                                }
                            }
                            break;
                        }
                        case TIKU_TRK_DB_CLOSE_ALL: {
                            /* Closing every window of the file manager is a
                             * request to close its WINDOWS, never to quit
                             * it -- the original makes the same
                             * distinction. */
                            while (tiku_desk_workspace_count(app.desk) > 0) {
                                tiku_desk_workspace_close(app.desk,
                                    tiku_desk_workspace_at(app.desk, 0));
                            }
                            break;
                        }
                        default:
                            break;
                        }
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
                        if (tiku_trk_deskbar_scroll(app.deskbar,
                                (ev.button == 4u) ? -1 : 1)) {
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
                        int bi;

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
                                (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
                            app.nav_on = 0;
                            app.dirty = 1;
                            break;
                        }
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
                                    &app.find_window)) {
                    (void)tiku_desk_window_send(w, &ev);
                } else if (hit == TIKU_DESK_HIT_CONTENT) {
                    folder_t *fl = (w != NULL) ? folder_of(&app, w)
                                               : desktop_folder(&app);
                    if (fl != NULL && fl->view != NULL) {
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
                            app.press_down = 1;
                            app.press_menu = 0;
                            tiku_trk_drag_arm(&app.drag, ev.x,
                                              ev.y, 3u, mods, 1,
                                              w, fl->path);
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
                            app.drop_fl = NULL;
                            app.drop_row = -1;
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
                                app.band_arm = fl;
                                app.band_fl = NULL;
                                app.band_x0 = app.band_x1 = ev.x;
                                app.band_y0 = app.band_y1 = ev.y;
                                app.band_mods = bm;
                                app.band_scroll_at = 0;
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
                            app.drop_fl = NULL;
                            app.drop_row = -1;
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
                if (tiku_trk_drag_moved(&app.drag, ev.x,
                                        ev.y) ||
                    tiku_trk_drag_active(&app.drag)) {
                    /* Where the pointer is now decides the target, and it
                     * is resolved across WINDOWS: the whole point of a
                     * spatial desktop is dragging between them (FS-001). */
                    tiku_desk_window_t *ow = NULL;
                    folder_t *ofl = NULL;
                    int orow = -1;

                    (void)tiku_desk_workspace_hit(app.desk, ev.x,
                                            ev.y, &ow);
                    ofl = (ow != NULL) ? folder_of(&app, ow)
                                       : desktop_folder(&app);
                    if (ofl != NULL && ofl->view == NULL) {
                        ofl = NULL;
                    }
                    if (ofl != NULL) {
                        int r = tiku_trk_folder_window_row_at(ofl, ev.x,
                                                              ev.y);
                        if (r >= 0) {
                            tiku_trk_pose_t *q =
                                tiku_trk_poseview_at(ofl->view, r);

                            /* Only a pose that will actually take the drop
                             * lights up; one being dragged never does
                             * (MA-016, PVS-042). */
                            if (q == NULL ||
                                !tiku_trk_drag_can_drop(&app.drag,
                                                        &q->model)) {
                                r = -1;
                            }
                        }
                        orow = r;
                    }
                    tiku_trk_deskbar_set_dragging(app.deskbar, 1);
                    {   /* What the bar would take, worked out during the
                         * hover rather than at the drop, so the highlight
                         * and the drop cannot disagree. */
                        int hteam = -1;

                        (void)tiku_trk_deskbar_drop_at(app.deskbar,
                                  ev.x, ev.y, &hteam);
                        if (hteam != tiku_trk_deskbar_drop_team(app.deskbar)) {
                            tiku_trk_deskbar_set_drop_team(app.deskbar,
                                                           hteam);
                            app.dirty = 1;
                        }
                    }
                    if (ofl != app.drop_fl || orow != app.drop_row) {
                        app.drop_fl = ofl;
                        app.drop_row = orow;
                        /* Leaving takes the highlight with it: a target
                         * still lit while the pointer is elsewhere is a
                         * promise the release will not keep (PVS-056). */
                        app.hover_at = tiku_trk_now();
                        app.sprung = 0;
                        app.dirty = 1;
                    }
                    if (ofl != NULL && orow < 0 &&
                        ow != tiku_desk_workspace_focused(app.desk)) {
                        /* Hovering the empty part of a window behind
                         * brings it forward, so a drag can reach what it
                         * covers without being dropped first (PVS-055). */
                        tiku_desk_workspace_activate(app.desk, ow);
                        app.dirty = 1;
                    }
                }
                if (app.band_arm != NULL &&
                    (ev.x - app.band_x0 > TIKU_TRK_DRAG_SLOP ||
                     app.band_x0 - ev.x > TIKU_TRK_DRAG_SLOP ||
                     ev.y - app.band_y0 > TIKU_TRK_DRAG_SLOP ||
                     app.band_y0 - ev.y > TIKU_TRK_DRAG_SLOP)) {
                    /* Past the slop: now it is a sweep. */
                    tiku_trk_poseview_set_band(app.band_arm->view, 1);
                    app.band_fl = app.band_arm;
                    app.band_arm = NULL;
                }
                if (app.band_fl != NULL) {
                    unsigned bm = 0u;

                    if (ev.modifiers & TIKU_DESK_MOD_SHIFT) {
                        bm |= TIKU_TRK_MOD_SHIFT;
                    }
                    if (ev.modifiers & TIKU_DESK_MOD_CMD) {
                        bm |= TIKU_TRK_MOD_CMD;
                    }
                    app.band_x1 = ev.x;
                    app.band_y1 = ev.y;
                    app.band_mods = bm;
                    /* Handed VIEW coordinates: the band is drawn where the
                     * pointer is, but what it sweeps is where the icons are,
                     * and those two differ by the scroll origin. */
                    folder_band_select(&app);
                    app.dirty = 1;
                }
                if (app.dock_drag) {
                    /* Ctrl held on the bar: the pointer IS the picker.
                     * Where it lets go decides the dock, and a release in
                     * the middle keeps the one it started from (DB-049). */
                    tiku_trk_db_dock_t want =
                        tiku_trk_deskbar_dock_at(ev.x, ev.y,
                            DESK_W, DESK_H,
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

                app.dock_drag = 0;
                app.band_arm = NULL;
                app.band_scroll_at = 0;
                app.scroll_at = 0;
                if (app.band_fl != NULL) {
                    /* The sweep's own selection is already what the view
                     * holds -- membership is a per-pose flag, so there is
                     * no list to rebuild on release (PVS-029) -- and the
                     * arrangement is saved because a sweep can move the
                     * origin under auto-scroll. */
                    tiku_trk_poseview_set_band(app.band_fl->view, 0);
                    (void)tiku_trk_poseview_save_state(app.band_fl->view);
                    app.band_fl = NULL;
                    app.dirty = 1;
                }
                if (dropped) {
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
                        (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
                    app.dirty = 1;
                }
                tiku_trk_drag_reset(&app.drag);
                tiku_trk_deskbar_set_dragging(app.deskbar, 0);
                app.drop_fl = NULL;
                app.drop_row = -1;
                /* A long press already put the menu up, so its release must
                 * not put a second one up (PVS-017). */
                app.press_down = 0;
                app.press_menu = 0;
                tiku_desk_workspace_end_track(app.desk);
                break;
            }
            case TIKU_DESK_EVENT_KEY_UP:
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
                if (app.renaming) {
                    tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
                    int shift = (ev.modifiers & TIKU_DESK_MOD_SHIFT) ? 1 : 0;
                    char c = '\0';

                    switch (key) {
                    case TIKU_DESK_KEY_RETURN:  k = TIKU_TRK_EDIT_COMMIT;    break;
                    case TIKU_DESK_KEY_ESCAPE:    k = TIKU_TRK_EDIT_CANCEL;    break;
                    case TIKU_DESK_KEY_BACKSPACE: k = TIKU_TRK_EDIT_BACKSPACE; break;
                    case TIKU_DESK_KEY_DELETE:    k = TIKU_TRK_EDIT_DELETE;    break;
                    case TIKU_DESK_KEY_LEFT:      k = TIKU_TRK_EDIT_LEFT;      break;
                    case TIKU_DESK_KEY_RIGHT:     k = TIKU_TRK_EDIT_RIGHT;     break;
                    case TIKU_DESK_KEY_HOME:      k = TIKU_TRK_EDIT_HOME;      break;
                    case TIKU_DESK_KEY_END:       k = TIKU_TRK_EDIT_END;       break;
                    default: break;
                    }
                    if (k == TIKU_TRK_EDIT_CHAR) {
                        c = key_char(&ev);
                        if (c == '\0') {
                            break;
                        }
                    }
                    switch (tiku_trk_edit_key(&app.ren, k, shift, c)) {
                    case TIKU_TRK_EDIT_COMMITTED: commit_rename(&app, 1); break;
                    case TIKU_TRK_EDIT_CANCELLED: commit_rename(&app, 0); break;
                    default: break;
                    }
                    app.dirty = 1;
                    break;
                }
                if (app.editing) {
                    tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
                    int shift = (ev.modifiers & TIKU_DESK_MOD_SHIFT) ? 1 : 0;
                    char c = '\0';

                    switch (key) {
                    case TIKU_DESK_KEY_RETURN:  k = TIKU_TRK_EDIT_COMMIT;    break;
                    case TIKU_DESK_KEY_ESCAPE:    k = TIKU_TRK_EDIT_CANCEL;    break;
                    case TIKU_DESK_KEY_BACKSPACE: k = TIKU_TRK_EDIT_BACKSPACE; break;
                    case TIKU_DESK_KEY_DELETE:    k = TIKU_TRK_EDIT_DELETE;    break;
                    case TIKU_DESK_KEY_LEFT:      k = TIKU_TRK_EDIT_LEFT;      break;
                    case TIKU_DESK_KEY_RIGHT:     k = TIKU_TRK_EDIT_RIGHT;     break;
                    case TIKU_DESK_KEY_HOME:      k = TIKU_TRK_EDIT_HOME;      break;
                    case TIKU_DESK_KEY_END:       k = TIKU_TRK_EDIT_END;       break;
                    default: break;
                    }
                    if (k == TIKU_TRK_EDIT_CHAR) {
                        c = key_char(&ev);
                        if (c == '\0') {
                            break;
                        }
                    }
                    switch (tiku_trk_edit_key(&app.ed, k, shift, c)) {
                    case TIKU_TRK_EDIT_COMMITTED:
                        commit_edit(&app, fl);
                        break;
                    case TIKU_TRK_EDIT_CANCELLED:
                        app.editing = 0;
                        app.status[0] = '\0';
                        break;
                    default:
                        break;
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_DESK_MOD_CMD)) {
                    app.running = 0;
                    break;
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
                        wx = DESK_W / 2;
                        wy = DESK_H / 2;
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
                if (key == TIKU_DESK_KEY_DELETE && fl != NULL) {
                    /* What the key MEANS depends on where the items are: a
                     * move that can be undone where there is a Trash to
                     * move them to, and a deletion inside it or when Shift
                     * says the user has already decided (PVK-038).  This
                     * shell had no Delete key at all. */
                    char paths[64][TIKU_TRK_PATH_MAX];
                    const char *sel[64];
                    int n = folder_selection(&app, fl, paths, sel, 64);

                    if (n > 0) {
                        int shift = (ev.modifiers & TIKU_DESK_MOD_SHIFT) ? 1 : 0;

                        if (tiku_trk_fs_delete_question(app.fs, sel[0],
                                shift) == TIKU_TRK_DELETE_STRAIGHT) {
                            (void)tiku_trk_fs_delete(app.fs, sel, n);
                            snprintf(app.status, sizeof app.status,
                                     "deleting %d item%s", n,
                                     (n == 1) ? "" : "s");
                        } else {
                            (void)tiku_trk_fs_trash(app.fs, sel, n);
                            snprintf(app.status, sizeof app.status,
                                     "moving %d item%s to the Trash", n,
                                     (n == 1) ? "" : "s");
                        }
                    }
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

        if (app.nav_on && app.popup != NULL) {
            /* A chunk per turn: the menu answers the mouse while a large
             * folder is still arriving (CW-055). */
            if (tiku_trk_nav_fill(&app.nav, app.nav.depth - 1)) {
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
        if (app.band_fl != NULL) {
            /* A sweep scrolls at the edges too: a band that could only
             * reach what was already on screen would be a band that stops
             * at the window's edge (PVS-050). */
            tiku_desk_rect_t c = tiku_trk_folder_window_content(app.band_fl);
            int top = c.y + 2, bot = c.y + c.h - COUNT_H, over = 0;

            if (app.band_y1 < top + AUTOSCROLL_BAND) {
                over = -((top + AUTOSCROLL_BAND) - app.band_y1);
            } else if (app.band_y1 > bot - AUTOSCROLL_BAND) {
                over = app.band_y1 - (bot - AUTOSCROLL_BAND);
            }
            if (over == 0) {
                app.band_scroll_at = 0;
            } else if (app.band_scroll_at == 0) {
                app.band_scroll_at = tiku_trk_now();
            } else if (tiku_trk_now() - app.band_scroll_at >
                           AUTOSCROLL_DELAY_US) {
                int far = (over < 0) ? -over : over;
                int step = (far > AUTOSCROLL_FAST) ? 3 : 1;

                if (tiku_trk_poseview_mode(app.band_fl->view) ==
                        TIKU_TRK_MODE_LIST) {
                    tiku_trk_poseview_scroll_to(app.band_fl->view,
                        tiku_trk_poseview_scroll(app.band_fl->view) +
                            ((over < 0) ? -step : step));
                } else {
                    int ox = 0, oy = 0;

                    tiku_trk_poseview_origin(app.band_fl->view, &ox, &oy);
                    tiku_trk_poseview_set_origin(app.band_fl->view, ox,
                        oy + ((over < 0) ? -step : step) * app.row_h);
                }
                /* There may be no further motion event while the pointer is
                 * held still.  Growing the view under the stationary band
                 * must therefore update its membership here (PVS-050). */
                folder_band_select(&app);
                app.dirty = 1;
            }
        }
        if (tiku_trk_drag_active(&app.drag) && app.drop_fl != NULL) {
            /* Auto-scroll.  The band is OUTSIDE the list, which is what
             * makes it reachable: a drag that has to leave the view to
             * scroll it can still see where it is going.  Held still for
             * the delay first, so crossing an edge on the way past does
             * not start it (PVS-050). */
            tiku_desk_rect_t c = tiku_trk_folder_window_content(app.drop_fl);
            int over = 0;

            int top = c.y + 2;
            int bot = c.y + c.h - COUNT_H;


            /* The band sits just INSIDE the list, where Tracker's is just
             * outside it.  Tracker's lives in the scroll-bar gutter; these
             * windows have none, and a pointer past the list edge is over
             * no window at all, so a band out there could never be
             * reached. */
            if (app.drag.y < top + AUTOSCROLL_BAND) {
                over = -((top + AUTOSCROLL_BAND) - app.drag.y);
            } else if (app.drag.y > bot - AUTOSCROLL_BAND) {
                over = app.drag.y - (bot - AUTOSCROLL_BAND);
            }
            if (over == 0) {
                app.scroll_at = 0;  /* out of the zone: re-arm */
            } else if (app.scroll_at == 0) {
                app.scroll_at = tiku_trk_now();
            } else if (tiku_trk_now() - app.scroll_at >
                           AUTOSCROLL_DELAY_US) {
                /* Faster the further past the edge, in the original's two
                 * buckets rather than continuously. */
                int far = (over < 0) ? -over : over;
                int step = (far > AUTOSCROLL_FAST) ? 3 : 1;


                if (tiku_trk_poseview_mode(app.drop_fl->view) !=
                        TIKU_TRK_MODE_LIST) {
                    /* An icon view scrolls in PIXELS, so the same two
                     * buckets move a row's worth rather than a row
                     * (PVL-056). */
                    int ox = 0, oy = 0;

                    tiku_trk_poseview_origin(app.drop_fl->view, &ox, &oy);
                    tiku_trk_poseview_set_origin(app.drop_fl->view, ox,
                        oy + ((over < 0) ? -step : step) * app.row_h);
                } else {
                    tiku_trk_poseview_scroll_to(app.drop_fl->view,
                        tiku_trk_poseview_scroll(app.drop_fl->view) +
                            ((over < 0) ? -step : step));
                }
                app.dirty = 1;
            }
        }
        if (tiku_trk_drag_active(&app.drag) && app.drop_fl != NULL &&
            app.drop_row >= 0 && !app.sprung &&
            tiku_trk_now() - app.hover_at > TIKU_TRK_SPRING_US) {
            /* Held still over a folder: open it, so a drag can be delivered
             * somewhere the window does not show without abandoning the
             * gesture (PVS-051).  Checked on the IDLE tick and not on
             * motion, because a drag resting perfectly still is exactly the
             * case and it produces no motion at all.  The original pops a
             * navigable menu; a window is what this shell has, and it lands
             * the drag in the same place. */
            tiku_trk_pose_t *q = tiku_trk_poseview_at(app.drop_fl->view,
                                                      app.drop_row);

            app.sprung = 1;
            if (q != NULL) {
                (void)open_folder(&app, q->model.path);
            }
            app.dirty = 1;
        }

        /* PVS-017: a button held still for the click interval opens the
         * context menu without waiting for the release. */
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
                app.press_y, (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
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
            if (due >= 0 && !app.editing) {
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
            (void)tiku_trk_query_sweep(app.store, (int64_t)time(NULL),
                                       (tiku_trk_now() - started_at) /
                                           1000000, open, nopen);
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
                        snprintf(fl->win->title, sizeof fl->win->title, "%s",
                                 tiku_trk_poseview_path(fl->view));
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
                    if (vc[k].mounted) {
                        /* A folder window showing the parent finds it by
                         * re-listing, which the poll above already did.
                         * Nothing else to do until there is a Desktop to
                         * put an icon on (PVN-040). */
                        changed++;
                        continue;
                    }
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
                    }
                }
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
                                         app.prog_dest[0] != '\0'
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
        usleep((sc != NULL) ? 20000 : 10000);
    }

    {
        /* The set of windows that were open, in the order they were opened,
         * which is the order they will come back in and therefore which
         * ends up in front (AW-004). */
        tiku_trk_session_entry_t open_now[TIKU_TRK_SESSION_MAX];
        int nsess = 0;

        for (i = 0; i < app.folder_count && nsess < TIKU_TRK_SESSION_MAX;
             i++) {
            folder_t *fl = &app.folders[i];

            if (fl->win == NULL || !fl->win->open || fl->gone ||
                tiku_trk_poseview_is_query(fl->view)) {
                /* A results window is not a place: reopening it would
                 * re-run a search the user has not asked for again. */
                continue;
            }
            snprintf(open_now[nsess].path, TIKU_TRK_PATH_MAX, "%s", fl->path);
            open_now[nsess].minimized = fl->win->minimized;
            nsess++;
        }
        (void)tiku_trk_session_save_entries(app.store, open_now, nsess);
    }
    for (i = 0; i < app.folder_count; i++) {
        /* A folder that was deleted under us keeps no arrangement: writing
         * one at exit would resurrect a position for something that is not
         * there, and undo the deliberate skip taken when it went. */
        if (!app.folders[i].gone) {
            (void)tiku_trk_poseview_save_state(app.folders[i].view);
            (void)tiku_trk_poseview_save_columns(app.folders[i].view);
            save_frame_of(&app, &app.folders[i]);
        }
        tiku_trk_poseview_free(app.folders[i].view);
    }
    tiku_trk_desktop_view_destroy(&app.desktop_view);
    /* The sidecar batches; without this the session's queries, frames and
     * recent lists are held in memory and dropped on the way out -- saved
     * in every sense except the one that matters. */
    {
        int32_t rec[2];

        rec[0] = (int32_t)tiku_trk_deskbar_dock(app.deskbar);
        rec[1] = (int32_t)tiku_trk_deskbar_width(app.deskbar);
        (void)tiku_trk_state_write(app.store, TIKU_TRK_KEY_SESSION,
                                   TIKU_TRK_ATTR_DOCK, rec, sizeof rec);
    }
    (void)tiku_trk_store_flush(app.store);
    if (tiku_trk_switcher_window_shown(&app.switcher_window)) {
        tiku_trk_switcher_window_close(&app.switcher_window, 0);
    }
    tiku_trk_panel_window_destroy(&app.file_panel);
    tiku_trk_prefs_window_destroy(&app.prefs_window);
    tiku_trk_deskbar_free(app.deskbar);
    tiku_desk_workspace_free(app.desk);
    /* Quitting waits for work already under way, up to five seconds
     * (AW-009): the alternative is a copy abandoned mid-file with
     * nothing to say so. */
    drain_fs(app.fs);
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
