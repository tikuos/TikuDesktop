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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_attr.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_info.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_thumb.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_volume.h"
#include "tiku_trk_switcher.h"
#include "tiku_trk_navmenu.h"
#include "tiku_trk_script.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskicons.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_model.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_edit.h"
#include "tiku_trk_select.h"
#include "tiku_trk_find.h"
#include "tiku_trk_state.h"
#include "tiku_trk_query.h"
#include "tiku_trk_task.h"
#include "tiku_trk_win.h"

#define DESK_W 1000
#define DESK_H  680
#define COUNT_H  16
#define MINI_ICON 16

/* How many entries per turn are asked whether they carry a picture of their
 * own.  A bound rather than the whole listing: the ask opens the file. */
#define HINTS_PER_TURN 24

/* How many Get Info windows may be open at once. */
#define INFO_MAX 8

/* The clear space a row keeps between a name and a right-aligned value; a
 * value that would cross it is dropped rather than overlapping the name. */
#define VALUE_GAP 120

/* Where the cascade starts again once it has run off the screen, and how far
 * each window steps from the last. */
#define CASCADE_HOME_X 45
#define CASCADE_HOME_Y 40
#define CASCADE_STEP   22

/* Tracker's own list row: ceilf(plain font size * 1.65), which at the 12 px
 * face is 20 -- tall enough that a 16 px mini icon sits in it with room, and
 * the reason rows do not tighten onto the text. */
#define LIST_ROW_H 20

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

/** @brief One folder window: a pose view plus the path that identifies it. */
typedef struct folder {
    tiku_trk_poseview_t *view;
    char                 path[TIKU_TRK_PATH_MAX];
    tiku_trk_win_t      *win;
    struct desktop      *app;
    /* A results window keeps its run alive: the run IS the subscription, so
     * stopping it when the first fill finishes would quietly make the window
     * a snapshot instead of a live query. */
    tiku_trk_qrun_t     *run;
    int                  live;
    int                  gone;      /* the folder itself was removed      */
    /* What the window opened with, so a close can tell whether it MOVED.
     * Without it every window is written back on every close (CW-002). */
    tiku_trk_frame_t     opened_with;
    int                  had_frame;
    /* For a results window, the saved record it is showing.  The sweep
     * needs it: a by-product with a window open on it is one the user is
     * still looking at (Q-037). */
    char                 qname[TIKU_TRK_NAME_MAX];
} folder_t;

typedef struct desktop {
    Display             *dpy;
    Window               win;
    GC                   gc;
    XImage              *img;
    tiku_desk_surface_t *surf;
    Atom                 del;
    tiku_trk_desk_t     *desk;
    tiku_trk_deskbar_t  *deskbar;
    tiku_trk_backend_t  *backend;
    tiku_trk_fs_t       *fs;
    folder_t             folders[TIKU_TRK_WIN_MAX];
    int                  folder_count;
    int                  row_h;
    int                  team;
    int                  running;
    int                  dirty;
    /* Menus.  One set serves every folder window, rebuilt from whichever is
     * focused each time a menu opens -- the original rebuilds on open too. */
    tiku_trk_cwmenu_t    menus;
    tiku_trk_menu_t     *popup;          /* the menu showing, or NULL       */
    tiku_desk_rect_t     popup_frame;
    tiku_trk_menu_t     *popup_sub;      /* its open submenu, or NULL       */
    tiku_desk_rect_t     popup_sub_frame;
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
    /* The panel, and the window it lives in. */
    tiku_trk_find_t      panel;
    tiku_trk_win_t      *panel_win;
    /* One cascade origin shared by every window that opens without a
     * remembered position, advanced per window and reset when it runs off
     * the screen -- not a modulo, which would silently stack the sixth
     * window on the first. */
    int                  cascade_x, cascade_y;
    struct tiku_trk_store *store;
    /* The Desktop itself: a pose view with no window, over the backdrop.
     * Its source is the union of every volume's Desktop folder, so a
     * second disk's items appear beside the boot volume's (PVN-051). */
    folder_t             dfl;
    tiku_trk_vdir_t      dvdir;
    tiku_trk_vdir_watch_t dwatch;
    int                  desk_row;      /* pose under the pointer, or -1 */
    /* ONE clipboard for the desktop, as the system's is one: cutting in a
     * window and pasting in another is the whole point of it. */
    tiku_trk_clip_t      clip;
    /* Select by pattern.  ONE of these serves the whole desktop and is
     * hidden rather than destroyed, so its expression outlives the window
     * it was last used on (CW-063, AW-105). */
    tiku_trk_select_t    sel;
    tiku_trk_win_t      *sel_win;
    folder_t            *sel_for;
    int                  sel_field;     /* which control has the keyboard */
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
    /* The Open/Save panel.  ONE, because it is asking one question at a
     * time and a second would be a second answer to the same ask. */
    /* What is mounted, and what it was last time we looked: the port's
     * only source of mount and unmount notices (PVN-008). */
    tiku_trk_volwatch_t  volwatch;
    /* The record the last search wrote, so Edit query has something to
     * reopen when the window it came from is the one asking. */
    char                 last_query[TIKU_TRK_NAME_MAX];
    tiku_trk_panel_t     fpanel;
    tiku_trk_win_t      *fpanel_win;
    tiku_trk_poseview_t *fpanel_view;
    folder_t            *fpanel_for;   /* whose selection it is moving   */
    int                  fpanel_cmd;   /* the menu command that opened it */
    tiku_trk_edit_t      fpanel_name;  /* the save field                 */
    /* Get Info: one window per NODE, so a second ask for the same entry
     * brings the first forward rather than opening a rival (AW-065). */
    tiku_trk_info_t      info[INFO_MAX];
    tiku_trk_win_t      *info_win[INFO_MAX];
    struct info_ctx     *info_ctx;      /* one per slot; see info_open   */
    int                  info_renaming; /* which slot's name is open     */
    tiku_trk_edit_t      info_name;
    /* Which broken link a picker was opened to re-point, or -1. */
    int                  info_relink;
    tiku_trk_alert_t     alert;     /* the question, when there is one */
    tiku_trk_progress_win_t prog;   /* the shared status window        */
    tiku_trk_prefs_t     prefs;
    tiku_trk_win_t      *prefs_win;
    tiku_trk_page_t      prefs_page;
    char                 prefs_path[TIKU_TRK_PATH_MAX];
    tiku_trk_switcher_t  sw;
    tiku_trk_win_t      *sw_win;
    tiku_trk_nav_t       nav;      /* the open navigation walk   */
    int                  nav_on;
    int64_t              prog_opened;
    tiku_trk_win_t      *prog_was_active;  /* focus to give back */
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
    int                  band_x0, band_y0, band_x1, band_y1;
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

/** @brief What the action table needs to know about the focused window. */
static tiku_trk_sc_ctx_t
menu_ctx(desktop_t *app, folder_t *fl, unsigned mods)
{
    tiku_trk_sc_ctx_t c;

    memset(&c, 0, sizeof c);
    c.mods = mods;
    c.view_mode = TIKU_TRK_MODE_LIST;
    if (fl == NULL) {
        return c;
    }
    (void)app;
    c.selection = tiku_trk_poseview_selection_count(fl->view);
    c.view_mode = tiku_trk_poseview_mode(fl->view);
    c.icon_size = tiku_trk_poseview_icon_size(fl->view);
    c.sort_field = tiku_trk_poseview_sort_field(fl->view);
    c.reverse_sort = tiku_trk_poseview_sort_reverse(fl->view);
    {
        static const int fields[] = { TIKU_TRK_COL_NAME, TIKU_TRK_COL_KIND,
                                      TIKU_TRK_COL_VALUE, TIKU_TRK_COL_SIZE,
                                      TIKU_TRK_COL_CAP };
        size_t k;

        for (k = 0; k < sizeof fields / sizeof *fields; k++) {
            if (tiku_trk_poseview_has_column(fl->view, fields[k])) {
                c.columns |= 1u << fields[k];
            }
        }
    }
    c.can_edit_name = (c.selection == 1);
    /* FS-051: the clipboard really has something in it, so the item can
     * finally be enabled -- it has been permanently grey until now. */
    c.clipboard_has_refs = (tiku_trk_clip_count(&app->clip) > 0);
    c.can_open_parent = (strcmp(fl->path, "/") != 0);
    {   /* Empty Trash is offered only when there is something to empty, and
         * the answer comes from the store rather than from a guess: without
         * it the item is disabled forever (AW-029). */
        char trash[TIKU_TRK_PATH_MAX];

        if (app->fs != NULL &&
            tiku_trk_fs_trash_dir(app->fs, fl->path, trash,
                                  sizeof trash) == 0) {
            tiku_trk_model_t tm;

            if (app->backend->ops->stat(app->backend, trash, &tm) == 0) {
                c.trash_full = tm.facts.trash_full;
            }
        }
    }
    {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        /* A namespace directory has a fixed shape, and that one answer is
         * what disables New folder, Paste, Cut and Duplicate together
         * rather than each deciding for itself. */
        if (p != NULL) {
            c.is_virtual = (p->model.kind == TIKU_TRK_KIND_VIRTUAL_DIR);
            c.selection_ro = !tiku_trk_model_is_writable(&p->model);
            c.can_trash_or_dup = c.selection > 0 && !c.selection_ro;
        }
        c.target_ro = (strncmp(fl->path, "/sys", 4) == 0 ||
                       strncmp(fl->path, "/dev", 4) == 0 ||
                       strncmp(fl->path, "/proc", 5) == 0);
        c.is_root = (strcmp(fl->path, "/") == 0);
        /* A results window IS a query, which is what makes Edit query
         * reachable from the window it produced (CW-033). */
        c.is_query = tiku_trk_poseview_is_query(fl->view);
    }
    return c;
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
    tiku_trk_menu_t *m = &app->menus.open_with;
    const char *types[TIKU_TRK_APP_TYPES_MAX];
    tiku_trk_cand_t cand[TIKU_TRK_APP_MAX];
    int i, n = 0, ntypes = 0, count;

    tiku_trk_menu_clear(m);
    if (fl == NULL || fl->view == NULL) {
        return;
    }
    count = tiku_trk_poseview_count(fl->view);
    for (i = 0; i < count && ntypes < TIKU_TRK_APP_TYPES_MAX; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);
        int k, seen = 0;

        if (p == NULL || !p->selected || p->model.type[0] == '\0') {
            continue;
        }
        if (tiku_trk_model_is_container(&p->model)) {
            continue;           /* a folder is opened, never opened WITH */
        }
        for (k = 0; k < ntypes; k++) {
            if (strcmp(types[k], p->model.type) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            types[ntypes++] = p->model.type;
        }
    }
    if (ntypes == 0) {
        return;
    }
    n = tiku_trk_launch_candidates(&app->apps, types, ntypes, app->store,
                                   cand, TIKU_TRK_APP_MAX);
    for (i = 0; i < n; i++) {
        char label[TIKU_TRK_APP_NAME_MAX + 40];

        /* Each row says why it is there, which is the sentence the
         * original's Relation column carries (AW-042, MA-081). */
        snprintf(label, sizeof label, "%s (%s)", cand[i].app->name,
                 tiku_trk_launch_relation_label(cand[i].rel));
        (void)tiku_trk_menu_add(m, label,
                                TIKU_TRK_CMD_OPEN_WITH_BASE + i, 0, 0u, 1);
        if (cand[i].preferred) {
            m->item[m->count - 1].marked = 1;
        }
    }
}

static folder_t *open_folder(desktop_t *app, const char *path);

/**
 * @brief Hand @p paths to an application, and say what happened (AW-039).
 *
 * One place, because the Open-with menu, a double click on a document and
 * a drop on an application are the same act with three ways in -- and a
 * launch that reported differently depending on how it was asked for would
 * be three behaviours wearing one name.
 */
static int
launch_paths(desktop_t *app, const tiku_trk_app_t *a,
             const char *const *paths, int n)
{
    tiku_trk_launch_err_t e;

    if (a == NULL) {
        snprintf(app->status, sizeof app->status, "%s",
                 tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NO_APP));
        return 0;
    }
    e = tiku_trk_launch_run(a, paths, n);
    if (e == TIKU_TRK_LAUNCH_OK) {
        snprintf(app->status, sizeof app->status, "opening %d item%s with "
                 "%.40s", n, (n == 1) ? "" : "s", a->name);
    } else if (e == TIKU_TRK_LAUNCH_IS_TRACKER) {
        /* The running Tracker takes it: opening a folder is what this
         * application does with one (AW-035). */
        int i;

        for (i = 0; i < n; i++) {
            (void)open_folder(app, paths[i]);
        }
    } else {
        snprintf(app->status, sizeof app->status, "%s",
                 tiku_trk_launch_reason(e));
    }
    return (e == TIKU_TRK_LAUNCH_OK);
}

/** @brief The application a document should open in, or NULL (AW-030). */
static const tiku_trk_app_t *
handler_for(desktop_t *app, const tiku_trk_model_t *m)
{
    tiku_trk_cand_t cand[TIKU_TRK_APP_MAX];
    const char *types[1];
    int n;

    if (m == NULL || m->type[0] == '\0') {
        return NULL;
    }
    types[0] = m->type;
    n = tiku_trk_launch_candidates(&app->apps, types, 1, app->store, cand,
                                   TIKU_TRK_APP_MAX);
    /* The best match, which is the user's choice when they made one: the
     * list is already in that order. */
    return (n > 0) ? cand[0].app : NULL;
}

/** @brief Open the selection with the candidate at @p at (AW-040). */
static void
open_with_pick(desktop_t *app, folder_t *fl, int at)
{
    const char *types[TIKU_TRK_APP_TYPES_MAX];
    const char *paths[TIKU_TRK_DRAG_MAX];
    tiku_trk_cand_t cand[TIKU_TRK_APP_MAX];
    int i, ntypes = 0, npath = 0, n, count;

    if (fl == NULL || fl->view == NULL) {
        return;
    }
    count = tiku_trk_poseview_count(fl->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);
        int k, seen = 0;

        if (p == NULL || !p->selected ||
            tiku_trk_model_is_container(&p->model)) {
            continue;
        }
        if (npath < TIKU_TRK_DRAG_MAX) {
            paths[npath++] = p->model.path;
        }
        for (k = 0; k < ntypes; k++) {
            if (strcmp(types[k], p->model.type) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen && p->model.type[0] != '\0' &&
            ntypes < TIKU_TRK_APP_TYPES_MAX) {
            types[ntypes++] = p->model.type;
        }
    }
    if (npath == 0) {
        snprintf(app->status, sizeof app->status, "%s",
                 tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NOTHING));
        return;
    }
    n = tiku_trk_launch_candidates(&app->apps, types, ntypes, app->store,
                                   cand, TIKU_TRK_APP_MAX);
    if (at < 0 || at >= n) {
        return;                 /* the list moved under the menu */
    }
    (void)launch_paths(app, cand[at].app, paths, npath);
}

/** @brief Draw a folder window's contents: list rows, then the count. */
/** @brief How wide a label is, for the view's own geometry. */
static int
measure_label(const char *text, void *ctx)
{
    (void)ctx;
    return tiku_desk_text_width(tiku_desk_font_plain(), text);
}

/**
 * @brief Whether a point lands on a pose's icon PIXELS (IV-026).
 *
 * The art is resolved the way the drawing resolves it -- through the
 * node-icon tier -- so what is hit is what is painted.
 */
static int
pose_icon_hit(const tiku_trk_model_t *m, int size, int px, int py, void *ctx)
{
    desktop_t *app = ctx;
    char art[TIKU_TRK_NAME_MAX];

    (void)tiku_trk_nodeicon_resolve(&app->nicons, app->store, m, art,
                                    sizeof art);
    return tiku_trk_icon_hit(art, size, px, py);
}

/**
 * @brief The capacity bar of a volume pose, inside its icon rect.
 *
 * Down the RIGHT edge of the icon in an icon mode and along the bottom in a
 * list row, which is where each has room: a horizontal bar under a 32 px
 * icon would sit on the name.
 */
static void
draw_space_bar(desktop_t *app, tiku_desk_surface_t *s,
               const tiku_trk_model_t *m, int ix, int iy, int size,
               int vertical)
{
    tiku_desk_rect_t bar;
    int used = 0;
    tiku_trk_space_t which = tiku_trk_volume_space_facts(m->facts.total,
                                                        m->facts.avail,
                                                        &used);
    const char *key = (which == TIKU_TRK_SPACE_WARNING)
                          ? "WarningSpaceColor" : "UsedSpaceColor";
    int32_t c = tiku_trk_prefs_get(&app->prefs, key);
    uint32_t fc = (uint32_t)tiku_trk_prefs_get(&app->prefs,
                                               "FreeSpaceColor");
    int thick = (size >= 32) ? (7 * (size + 1)) / 32 : 3;

    if (thick < 2) {
        thick = 2;
    }
    if (vertical) {
        bar.x = ix + size - thick;
        bar.y = iy;
        bar.w = thick;
        bar.h = size;
    } else {
        bar.x = ix;
        bar.y = iy + size - 3;
        bar.w = size;
        bar.h = 3;
    }
    tiku_desk_fill(s, bar, TIKU_DESK_RGB((fc >> 16) & 0xffu,
                                        (fc >> 8) & 0xffu, fc & 0xffu));
    /* The USED part grows from the same edge the bar is anchored to, so a
     * nearly-full disk reads as nearly full at a glance. */
    if (vertical) {
        bar.h = size * used / 100;
    } else {
        bar.w = size * used / 100;
    }
    if (bar.w > 0 && bar.h > 0) {
        tiku_desk_fill(s, bar, TIKU_DESK_RGB(((uint32_t)c >> 16) & 0xffu,
                                             ((uint32_t)c >> 8) & 0xffu,
                                             (uint32_t)c & 0xffu));
    }
}

/**
 * @brief Draw one pose as an icon with its name under it (CW-065).
 *
 * The coordinates are the VIEW's, so the caller subtracts the origin: a
 * renderer that scrolled by moving the poses would move the things the hit
 * test asks about too.
 */
static void
draw_pose_icon(folder_t *fl, tiku_desk_surface_t *s, tiku_trk_pose_t *p,
               int ox, int oy)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    desktop_t *app = fl->app;
    int ix, iy, iw, ih, lx, ly, lw, lh;
    int drawn_sel = tiku_trk_poseview_draws_selected(fl->view, p);
    char art[TIKU_TRK_NAME_MAX];
    tiku_desk_rect_t label;

    tiku_trk_poseview_pose_parts(fl->view, p, &ix, &iy, &iw, &ih,
                                 &lx, &ly, &lw, &lh);
    ix -= ox; iy -= oy; lx -= ox; ly -= oy;
    (void)tiku_trk_nodeicon_resolve(&app->nicons, app->store, &p->model,
                                    art, sizeof art);
    if (drawn_sel) {
        (void)tiku_trk_icon_paint_selected(s, art, ix, iy, iw);
    } else if (p->model.facts.link_broken) {
        (void)tiku_trk_icon_paint_dim(s, art, ix, iy, iw, 0.55f,
                                      TIKU_DESK_C_DOC);
    } else {
        (void)tiku_trk_icon_paint(s, art, ix, iy, iw);
    }
    if (p->model.kind == TIKU_TRK_KIND_VOLUME &&
        tiku_trk_prefs_get(&app->prefs, "ShowVolumeSpaceBar")) {
        /* An icon-mode ornament in the original, and it was drawn only in
         * the list rows here -- so the mode the bar was designed for was
         * the one mode that had none (IV-045). */
        draw_space_bar(app, s, &p->model, ix, iy, iw, 1);
    }
    /* The label is filled behind when selected, not the whole cell: in an
     * icon mode the cell is mostly empty, and a filled cell would read as a
     * block rather than as a name that is chosen (PVL-082). */
    label.x = lx - 2;
    label.y = ly;
    label.w = lw + 4;
    label.h = (lh > 0) ? lh : f->height;
    if (drawn_sel) {
        tiku_desk_fill(s, label, TIKU_DESK_C_SELECT);
    }
    {
        /* On the Desktop the ink follows the BACKDROP's brightness rather
         * than the window colour: a dark backdrop takes white labels, and
         * the folder windows are unaffected either way (AW-028). */
        tiku_desk_rgb_t ink = TIKU_DESK_C_TEXT;

        if (fl->win == NULL) {
            tiku_desk_rgb_t bg = TIKU_DESK_C_BACKDROP;
            int lum = ((int)((bg >> 16) & 0xffu) * 299 +
                       (int)((bg >> 8) & 0xffu) * 587 +
                       (int)(bg & 0xffu) * 114) / 1000;

            ink = (lum <= 127) ? TIKU_DESK_RGB(255, 255, 255)
                               : TIKU_DESK_RGB(0, 0, 0);
        }
        tiku_desk_text(s, f, lx, ly + f->ascent, p->model.name,
                       drawn_sel ? TIKU_DESK_C_SELTEXT : ink);
    }
}

/*---------------------------------------------------------------------------*/
/* The Desktop: a pose view with no window, over the backdrop                */
/*---------------------------------------------------------------------------*/

/** @brief The pose area, which for the Desktop is the whole screen. */
static tiku_desk_rect_t
folder_content(desktop_t *app, folder_t *fl)
{
    (void)app;
    if (fl != NULL && fl->win != NULL) {
        return tiku_trk_win_content(fl->win);
    }
    /* No chrome to inset: the Desktop has no menu bar, no scroll bars and
     * no border view, so its pose area IS the screen (AW-018). */
    return (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H };
}

/**
 * @brief Where a Desktop icon may sit: the screen, less the Deskbar strip.
 *
 * The Desktop cannot scroll, so the area an icon is placed in has to be the
 * area it can be reached in -- an icon under the bar is an icon behind
 * something that is always on top (PVL-018).
 */
static tiku_trk_rect_t
desk_area(desktop_t *app)
{
    tiku_desk_rect_t bf = tiku_trk_deskbar_frame(app->deskbar);
    tiku_trk_rect_t a = { 0, 0, DESK_W, DESK_H };

    if (bf.w <= 0 || bf.h <= 0) {
        return a;
    }
    if (bf.w < bf.h) {
        if (bf.x > DESK_W / 2) {
            a.w = bf.x - TIKU_TRK_DESK_BAR_MARGIN;
        } else {
            a.x = bf.x + bf.w + TIKU_TRK_DESK_BAR_MARGIN;
            a.w = DESK_W - a.x;
        }
    } else {
        if (bf.y > DESK_H / 2) {
            a.h = bf.y - TIKU_TRK_DESK_BAR_MARGIN;
        } else {
            a.y = bf.y + bf.h + TIKU_TRK_DESK_BAR_MARGIN;
            a.h = DESK_H - a.y;
        }
    }
    return a;
}

/** @brief Put a node on the Desktop that does not live in a Desktop folder. */
static void
desk_add_pose(desktop_t *app, const char *path)
{
    tiku_trk_model_t m;

    if (path == NULL || path[0] == '\0' ||
        app->backend->ops->stat(app->backend, path, &m) != 0) {
        return;
    }
    /* Through the view's own insert rather than a code path of its own:
     * the arrival machinery already knows how to place a pose in the next
     * free slot, and a second way in would place it by different rules
     * (AW-026, AW-027). */
    (void)tiku_trk_poseview_adopt(app->dfl.view, &m);
}

/**
 * @brief Pull every Desktop icon back somewhere it can be reached (PVL-018).
 *
 * Never a grid remap and never a scroll: the Desktop cannot scroll, so an
 * icon off the screen or under the Deskbar is an icon that is simply gone
 * (AW-019).
 */
static int
desk_ensure_visible(desktop_t *app)
{
    tiku_trk_rect_t screen = { 0, 0, DESK_W, DESK_H };
    tiku_trk_rect_t area = desk_area(app);
    tiku_desk_rect_t bf = tiku_trk_deskbar_frame(app->deskbar);
    tiku_trk_rect_t bar = { bf.x, bf.y, bf.w, bf.h };
    int i, n = tiku_trk_poseview_count(app->dfl.view), moved = 0;

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(app->dfl.view, i);
        tiku_trk_rect_t r;
        int px, py, pw, ph;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(app->dfl.view, p, &px, &py, &pw, &ph);
        r = (tiku_trk_rect_t){ px, py, pw, ph };
        if (tiku_trk_deskicons_valid(r, screen, bar, NULL, 0)) {
            continue;
        }
        if (tiku_trk_deskicons_pull_back(&r, area)) {
            int lx, ly;

            /* Moved by the same DELTA the rectangle needed: the pose's
             * location is not its rectangle's corner -- the label may hang
             * wider than the art -- so setting the location to the
             * rectangle would leave it short by the overhang. */
            tiku_trk_poseview_pose_location(app->dfl.view, p, &lx, &ly);
            tiku_trk_poseview_move_pose(app->dfl.view, p, lx + (r.x - px),
                                        ly + (r.y - py), 0);
            moved++;
        }
    }
    return moved;
}

/** @brief Build the Desktop's contents: the union, then Disks, then Trash. */
static void
desk_icons_rebuild(desktop_t *app)
{
    tiku_trk_volumes_t vs;
    tiku_trk_model_t set[64];
    char trash[TIKU_TRK_PATH_MAX];
    int n = 0;

    if (app->dfl.view == NULL) {
        return;
    }
    if (tiku_trk_volumes_scan(&vs) >= 0) {
        const char *home = getenv("HOME");
        char hd[TIKU_TRK_PATH_MAX];

        snprintf(hd, sizeof hd, "%s/Desktop",
                 (home != NULL && home[0] != '\0') ? home : "");
        tiku_trk_deskicons_sources(&vs, app->backend, hd, &app->dvdir);
    }
    n = tiku_trk_vdir_list(&app->dvdir, app->backend, set,
                           (int)(sizeof set / sizeof set[0]));
    if (n < 0) {
        n = 0;
    }
    (void)tiku_trk_poseview_set_models(app->dfl.view, set, n);
    (void)tiku_trk_poseview_restore_state(app->dfl.view);
    /* Never a list, whatever a saved state says: a Desktop in list mode is
     * one the user cannot put an icon on (MA-069, PVL-008). */
    tiku_trk_poseview_set_mode(app->dfl.view,
        tiku_trk_deskicons_mode(tiku_trk_poseview_mode(app->dfl.view)));
    (void)tiku_trk_poseview_place_unplaced(app->dfl.view,
                                           desk_area(app).w);
    /* Added AFTER the folder's own items are placed, so each lands in the
     * next free slot rather than in the middle of them. */
    if (tiku_trk_deskicons_show_disks(
            tiku_trk_prefs_get(&app->prefs, "ShowDisksIcon"),
            tiku_trk_prefs_get(&app->prefs, "MountVolumesOntoDesktop"))) {
        desk_add_pose(app, "/");
    }
    if (tiku_trk_fs_trash_dir(app->fs, app->dfl.path, trash,
                              sizeof trash) == 0) {
        desk_add_pose(app, trash);
    }
    (void)tiku_trk_poseview_place_unplaced(app->dfl.view,
                                           desk_area(app).w);
    (void)desk_ensure_visible(app);
}

/** @brief Draw the poses of an icon mode, and only the ones on screen. */
static void
folder_draw_icons(folder_t *fl, tiku_desk_surface_t *s,
                  tiku_desk_rect_t list)
{
    int first = -1, last = -1, i, ox = 0, oy = 0;

    tiku_trk_poseview_origin(fl->view, &ox, &oy);
    /* Shifted so the view's own coordinates land in the window. */
    ox -= list.x + 2;
    oy -= list.y + 2;
    if (!tiku_trk_poseview_visible_range(fl->view, &first, &last)) {
        return;
    }
    /* Only what can be seen.  A walk of every pose is a walk of the whole
     * folder on every frame, which is what a folder of ten thousand makes
     * unaffordable (PVL-062). */
    for (i = first; i <= last; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);

        if (p != NULL && p->placed) {
            draw_pose_icon(fl, s, p, ox, oy);
        }
    }
}

/** @brief Paint the Desktop's icons on the backdrop, under every window. */
static void
desk_icons_draw(tiku_desk_surface_t *s, void *ctx)
{
    desktop_t *app = (desktop_t *)ctx;

    if (app == NULL || app->dfl.view == NULL) {
        return;
    }
    folder_draw_icons(&app->dfl, s, folder_content(app, &app->dfl));
}


static void
folder_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s,
            tiku_desk_rect_t content, void *ctx)
{
    folder_t *fl = ctx;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    desktop_t *app = fl->app;
    tiku_desk_rect_t list = content;
    int row_h = app->row_h;
    int rows, scroll, total, i, icons;

    (void)w;
    list.h -= COUNT_H;
    tiku_desk_ui_sunken(s, list, TIKU_DESK_C_DOC);
    /* The view is told how big its window is, in pixels: the row count it
     * used to be given cannot answer where an icon layout is scrolled to
     * or what is on screen (PVL-049). */
    tiku_trk_poseview_set_viewport(fl->view, list.w - 4, list.h - 4);
    tiku_trk_poseview_set_row_height(fl->view, row_h);
    icons = (tiku_trk_poseview_mode(fl->view) != TIKU_TRK_MODE_LIST);
    if (icons) {
        /* Anything with no position of its own gets one, wrapping at the
         * window's width.  Done here because this is where the width is
         * known, and a pose with no position is not drawn at all. */
        (void)tiku_trk_poseview_place_unplaced(fl->view, list.w - 4);
    }

    rows = (list.h - 4) / row_h;
    /* The view cannot see its own window; the rules stated in terms of what
     * is VISIBLE need this. */
    tiku_trk_poseview_set_visible_rows(fl->view, rows);
    scroll = tiku_trk_poseview_scroll(fl->view);
    total = tiku_trk_poseview_count(fl->view);

    tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
    if (icons) {
        folder_draw_icons(fl, s, list);
    }
    for (i = 0; !icons && i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, scroll + i);
        tiku_desk_rect_t row = { list.x + 2, list.y + 2 + i * row_h,
                                 list.w - 4, row_h };
        int ty = row.y + (row_h - f->height) / 2 + f->ascent;
        char val[64];

        int drawn_sel, cut;

        if (p == NULL) {
            break;
        }
        /* Whether a selection is PAINTED is the window's business; whether a
         * pose is IN it is the pose's.  An inactive window still owns its
         * selection, it just stops showing it (PVS-023). */
        drawn_sel = tiku_trk_poseview_draws_selected(fl->view, p);
        /* A CUT item is drawn faded wherever it shows, in this window and
         * every other: it is still where it was, and the fade is the only
         * thing saying it is on its way out (FS-054).  A copied item is
         * not faded -- nothing is going to happen to it. */
        cut = (tiku_trk_clip_mode_of(&fl->app->clip, p->model.node_id)
                   == TIKU_TRK_CLIP_MOVE);
        /* The pose a hovering drag would land in is drawn selected, which
         * is how the original highlights a drop target -- it transiently
         * selects it rather than inventing a third state (PVS-035). */
        if (fl->app->drop_fl == fl && fl->app->drop_row == i) {
            drawn_sel = 1;
        }
        tiku_desk_fill(s, row, drawn_sel ? TIKU_DESK_C_SELECT
                                         : TIKU_DESK_C_DOC);
        {   /* A mini icon at the head of the name, as list mode has. */
            int mini = row_h - 2;
            int tx = row.x + 4;
            int thumbed = 0;
            /* Where the row's picture comes from, and its name.  Resolved
             * ONCE per row rather than per paint call: the fallback walk
             * is the same walk whether the row is selected, cut or plain
             * (IV-001). */
            char art[TIKU_TRK_NAME_MAX];
            tiku_trk_icon_from_t src =
                tiku_trk_nodeicon_resolve(&fl->app->nicons, fl->app->store,
                                          &p->model, art, sizeof art);

            if (mini > MINI_ICON) {
                mini = MINI_ICON;
            }
            if (p->model.kind == TIKU_TRK_KIND_VOLUME &&
                tiku_trk_prefs_get(&fl->app->prefs, "ShowVolumeSpaceBar")) {
                /* A capacity bar along the bottom of the icon.  Drawn from
                 * the FACTS the store already read, so a listing of a
                 * dozen volumes is a dozen rows and not a dozen
                 * statvfs calls (PVL-072, TS-054). */
                draw_space_bar(app, s, &p->model, row.x + 2,
                               row.y + (row_h - mini) / 2 + 2, mini, 0);
            }
            {   /* An image shows its own picture.  A HIT draws it; a
                 * miss QUEUES the work and falls through to a type icon,
                 * so the row is never waiting on a decode (IV-028,
                 * IV-031).  Which rows those are is the HINT, taken once
                 * when the entry arrived -- asking here would sniff every
                 * file's first bytes once per frame (IV-005). */
                tiku_trk_image_t th;

                if (src == TIKU_TRK_ICON_FROM_THUMB) {
                    if (tiku_trk_thumb_fresh(fl->app->store, p->model.path,
                                             p->model.facts.mtime) &&
                        tiku_trk_thumb_load(fl->app->store, p->model.path,
                                            &th) == 0) {
                        tiku_trk_thumb_paint(s, &th, row.x + 2,
                                             row.y + (row_h - mini) / 2,
                                             mini);
                        tiku_trk_image_free(&th);
                        thumbed = 1;
                    } else {
                        (void)tiku_trk_thumb_queue(&fl->app->thumbs,
                                                   p->model.path,
                                                   p->model.facts.size,
                                                   mini);
                    }
                }
            }
            /* A link with nowhere to go is drawn faded: the art is the same
             * as a live link's, so the state has to show some other way. */
            if (thumbed) {
                tx = row.x + 2 + mini + 4;
            } else if (((p->model.facts.link_broken || cut)
                     ? tiku_trk_icon_paint_dim(s, art, row.x + 2,
                           row.y + (row_h - mini) / 2, mini, 0.55f,
                           TIKU_DESK_C_DOC)
                     : drawn_sel
                     /* PVL-082: selection reads on the icon as well as the
                      * label, so a row whose label is scrolled away still
                      * shows it. */
                     ? tiku_trk_icon_paint_selected(s, art, row.x + 2,
                           row.y + (row_h - mini) / 2, mini)
                     : tiku_trk_icon_paint(s, art, row.x + 2,
                           row.y + (row_h - mini) / 2, mini))) {
                tx = row.x + 2 + mini + 4;
            }
            tiku_desk_text(s, f, tx, ty,
                           tiku_trk_poseview_is_query(fl->view)
                               ? p->model.path : p->model.name,
                           drawn_sel ? TIKU_DESK_C_SELTEXT
                                     : (cut ? tiku_desk_tint(
                                                  TIKU_DESK_C_TEXT,
                                                  TIKU_DESK_LIGHTEN_2)
                                            : TIKU_DESK_C_TEXT));
        }
        /* The value column earns its place on a device: it is the reason a
         * namespace browser is not just a file list. */
        if (!tiku_trk_model_is_container(&p->model)) {
            {   /* The node's own descriptor decides how its reading reads:
                 * 32768 is "32.0 KiB" on a bytes node and "32.768 kHz" on a
                 * clock, and the raw integer on neither. */
                (void)tiku_trk_attr_format(&p->desc,
                                           p->value_valid ? p->value : "",
                                           row.w / 3, val, sizeof val);
            }
            {
                int vw = tiku_desk_text_width(f, val);
                if (vw < row.w - VALUE_GAP) {
                    tiku_desk_text(s, f, row.x + row.w - vw - 6, ty, val,
                                   drawn_sel ? TIKU_DESK_C_SELTEXT
                                             : TIKU_DESK_C_TEXT);
                }
            }
        }
    }
    if (app->band_fl == fl) {
        /* Two styles, chosen by the setting on every frame: tinted (the
         * default) draws the band over the icons and lets them show
         * through; the other inverts its outline, which is visible over
         * whatever it lands on and needs nothing saved underneath
         * (PVL-069, PVS-030, TS-049). */
        tiku_desk_rect_t b;
        int lox = (app->band_x0 < app->band_x1) ? app->band_x0
                                                : app->band_x1;
        int loy = (app->band_y0 < app->band_y1) ? app->band_y0
                                                : app->band_y1;

        b.x = lox;
        b.y = loy;
        b.w = (app->band_x0 > app->band_x1) ? app->band_x0 - app->band_x1
                                            : app->band_x1 - app->band_x0;
        b.h = (app->band_y0 > app->band_y1) ? app->band_y0 - app->band_y1
                                            : app->band_y1 - app->band_y0;
        if (b.w > 0 && b.h > 0) {
            tiku_trk_paint_band(s, b,
                tiku_trk_prefs_get(&app->prefs, "TransparentSelection"));
        }
    }
    if (app->renaming && app->ren_fl == fl && !icons) {
        /* The editor sits ON the row, not in the count view: an in-place
         * rename has to show the name being changed where the name is. */
        int at = app->ren_row - scroll;

        if (at >= 0 && at < rows) {
            tiku_desk_rect_t box;

            box.x = list.x + 2;
            box.y = list.y + 2 + at * row_h;
            box.w = (list.w - 4 > 260) ? 260 : list.w - 4;
            box.h = row_h;
            tiku_desk_ui_textfield_sel(s, box, app->ren.text, app->ren.caret,
                                       app->ren.sel_a, app->ren.sel_b,
                                       TIKU_DESK_S_FOCUS);
        }
    }
    tiku_desk_clip_reset(s);
    {
        tiku_desk_rect_t count = { content.x, content.y + content.h - COUNT_H,
                                   content.w, COUNT_H };
        const char *typed = tiku_trk_poseview_type_string(fl->view);
        char label[256];

        if (app->finding && fl->win == tiku_trk_desk_focused(app->desk)) {
            tiku_desk_ui_sunken(s, count, TIKU_DESK_C_DOC);
            snprintf(label, sizeof label, "find: %.60s_", app->find_buf);
        } else if (app->editing &&
                   fl->win == tiku_trk_desk_focused(app->desk)) {
            /* The editor takes the count view on a document ground, the way
             * type-ahead does: it is a field now, not a readout. */
            tiku_desk_ui_sunken(s, count, TIKU_DESK_C_DOC);
            {   /* The leaf is what the user is looking at; the full path
                 * would not fit and would not add anything. */
                const char *slash = strrchr(app->edit_path, '/');

                snprintf(label, sizeof label, "%.100s = %.64s_",
                         (slash != NULL) ? slash + 1 : app->edit_path,
                         app->ed.text);
            }
        } else if (app->status[0] != '\0') {
            /* A refusal or a confirmation displaces the count until the next
             * thing happens; it is what the user needs to read. */
            tiku_desk_fill(s, count, TIKU_DESK_C_PANEL);
            snprintf(label, sizeof label, "%s", app->status);
        } else if (typed[0] != '\0') {
            tiku_desk_fill(s, count, TIKU_DESK_C_PANEL);
            snprintf(label, sizeof label, "%s", typed);
        } else {
            tiku_desk_fill(s, count, TIKU_DESK_C_PANEL);
            /* TS-037: an empty folder says so rather than counting to
             * zero, which reads like a count that failed. */
            if (total == 0) {
                snprintf(label, sizeof label, "empty folder");
            } else {
                snprintf(label, sizeof label, "%d item%s", total,
                         (total == 1) ? "" : "s");
            }
        }
        tiku_desk_text(s, f, count.x + 4,
                       count.y + (COUNT_H - f->height) / 2 + f->ascent,
                       label, TIKU_DESK_C_TEXT);
    }
}

static folder_t *open_folder(desktop_t *app, const char *path);
static void prefs_open(desktop_t *app);
static folder_t *folder_of(desktop_t *app, tiku_trk_win_t *w);

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
    (void)tiku_trk_frame_save(app->store, fl->path, &now,
                              fl->had_frame ? &fl->opened_with : NULL);
}

static void
close_window(desktop_t *app, tiku_trk_win_t *w)
{
    folder_t *fl;

    if (w == NULL) {
        return;
    }
    fl = folder_of(app, w);
    if (fl != NULL) {
        (void)tiku_trk_poseview_save_state(fl->view);
        (void)tiku_trk_poseview_save_columns(fl->view);
        save_frame_of(app, fl);
    }
    tiku_trk_desk_close(app->desk, w);
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
 * @brief Seed and refresh the Deskbar's team list from the live process set.
 *
 * On this host the process registry IS /proc; on TikuOS it is the kernel's
 * own, which is why the reader is small and local rather than a subsystem.
 * A background process has no window, and the Deskbar never lists itself.
 */
static void
scan_teams(desktop_t *app)
{
    DIR *d = opendir("/proc");
    struct dirent *e;

    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char comm[TIKU_TRK_PATH_MAX], name[64];
        FILE *f;
        size_t n;
        struct stat ps;

        if (e->d_name[0] < '1' || e->d_name[0] > '9') {
            continue;               /* not a pid */
        }
        /* Somebody else's processes are not in this user's Deskbar. */
        if (snprintf(comm, sizeof comm, "/proc/%s", e->d_name) < 0 ||
            stat(comm, &ps) != 0 || ps.st_uid != getuid()) {
            continue;
        }
        /* An empty cmdline is a KERNEL thread: it has no image, no window
         * and nothing to raise.  This is the closest thing this host has
         * to the background-app flag the rule is written against. */
        if (snprintf(comm, sizeof comm, "/proc/%s/cmdline", e->d_name) < 0) {
            continue;
        }
        f = fopen(comm, "r");
        if (f == NULL) {
            continue;
        }
        n = fread(name, 1, sizeof name, f);
        (void)fclose(f);
        if (n == 0u) {
            continue;
        }
        if (snprintf(comm, sizeof comm, "/proc/%s/comm", e->d_name) < 0) {
            continue;
        }
        f = fopen(comm, "r");
        if (f == NULL) {
            continue;               /* gone between readdir and open */
        }
        if (fgets(name, sizeof name, f) == NULL) {
            (void)fclose(f);
            continue;
        }
        (void)fclose(f);
        n = strlen(name);
        while (n > 0u && (name[n - 1u] == '\n' || name[n - 1u] == ' ')) {
            name[--n] = '\0';
        }
        /* Kernel threads are bracketed and own nothing a user can raise;
         * and the Deskbar does not list the application it is part of
         * twice, which the seed already added (DB-014). */
        if (name[0] == '\0' || strcmp(name, "desktop") == 0) {
            continue;
        }
        (void)tiku_trk_deskbar_add_team(app->deskbar, name);
    }
    (void)closedir(d);
}

/**
 * @brief The character a key event types, or 0.
 *
 * With no display there is no keyboard map to consult, so a Latin-1 keysym
 * stands for itself -- which is all a script needs in order to type.
 */
static char
key_char(desktop_t *app, XKeyEvent *ev, KeySym ks)
{
    char buf[8];

    if (app->dpy != NULL) {
        return (XLookupString(ev, buf, sizeof buf, NULL, NULL) == 1)
                   ? buf[0] : (char)0;
    }
    if (ks >= 0x20 && ks < 0x7f) {
        char c = (char)ks;

        if ((ev->state & ShiftMask) != 0u && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        return c;
    }
    return (char)0;
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
                        types, nt, (state & ControlMask) != 0u)) {
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
        int snap = (state & ControlMask) != 0u;

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
        if (app->drop_fl == &app->dfl) {
            /* The Desktop cannot scroll, so an icon dropped off the edge
             * is one that is simply gone (PVS-058). */
            (void)desk_ensure_visible(app);
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
    if ((state & Mod1Mask) != 0u)    { mods |= TIKU_TRK_DRAG_MOD_COPY; }
    if ((state & ControlMask) != 0u) { mods |= TIKU_TRK_DRAG_MOD_LINK; }

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
                tiku_trk_alert_open(&app->alert, TIKU_TRK_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_REFUSAL, refused,
                                    "Cancel");
                return;
            }
        } else {
            tiku_trk_alert_open(&app->alert, TIKU_TRK_ALERT_WARN,
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
                                          (state & ShiftMask) ?
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
        tiku_desk_rect_t c = folder_content(app, app->drop_fl);
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
    switch (what) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_trk_fs_copy(app->fs, sel, n, dst, TIKU_TRK_CONFLICT_ASK);
        snprintf(app->status, sizeof app->status, "copying %d item%s", n,
                 (n == 1) ? "" : "s");
        break;
    case TIKU_TRK_DROP_LINK:
        (void)tiku_trk_fs_link(app->fs, sel, n, dst, 0);
        snprintf(app->status, sizeof app->status, "linking %d item%s", n,
                 (n == 1) ? "" : "s");
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
 * @brief Content width the widest row in @p fl wants.
 *
 * Measured from the text actually drawn, not from the column table: this
 * window paints a name and a value, and a width taken from columns it does
 * not use would fit something that is not on screen.
 */
static int
folder_fit_w(desktop_t *app, folder_t *fl)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int i, n = tiku_trk_poseview_count(fl->view), widest = 0;
    int query = tiku_trk_poseview_is_query(fl->view);

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);
        char val[64];
        int w;

        if (p == NULL) {
            continue;
        }
        w = MINI_ICON + 6 + tiku_desk_text_width(f, query ? p->model.path
                                                          : p->model.name);
        if (!tiku_trk_model_is_container(&p->model)) {
            int need;

            (void)tiku_trk_attr_format(&p->desc,
                                       p->value_valid ? p->value : "", 190,
                                       val, sizeof val);
            /* The row only PAINTS a value when it clears the gap the drawing
             * keeps between the two columns, so a fit that merely made room
             * for the text would produce a window whose values are still
             * hidden.  The width has to satisfy the rule that decides, not a
             * second guess at it -- VALUE_GAP is that rule's constant. */
            need = tiku_desk_text_width(f, val) + VALUE_GAP + 8;
            if (need > w) {
                w = need;
            }
        }
        if (w > widest) {
            widest = w;
        }
    }
    (void)app;
    return (widest < 200) ? 200 : widest + 12;
}

/** @brief Content height every row plus the count view wants. */
static int
folder_fit_h(desktop_t *app, folder_t *fl)
{
    /* The same arithmetic folder_draw uses to decide how many rows fit,
     * solved the other way round. */
    return tiku_trk_poseview_count(fl->view) * app->row_h + 4 + COUNT_H;
}

/** @brief Draw the Find panel into its window. */
static void
panel_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s,
           tiku_desk_rect_t content, void *ctx)
{
    (void)w;
    tiku_trk_find_draw(ctx, s, content);
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
    int i, n;
    tiku_trk_model_t *set;

    if (fl == NULL) {
        tiku_trk_query_stop(run);
        return NULL;
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

/*---------------------------------------------------------------------------*/
/* Select by pattern (PVK-047, AW-080, AW-081, AW-105, CW-063)               */
/*---------------------------------------------------------------------------*/

#define SEL_W 360
#define SEL_H 132

/** @brief Which control a point in the Select window is over. */
typedef enum {
    SEL_HIT_NONE = 0,
    SEL_HIT_EXPR,
    SEL_HIT_RULE,
    SEL_HIT_IGNORE,
    SEL_HIT_INVERT,
    SEL_HIT_SELECT
} sel_hit_t;

/** @brief The control rectangles, derived once so drawing and hits agree. */
static void
sel_layout(tiku_desk_rect_t c, tiku_desk_rect_t *expr, tiku_desk_rect_t *rule,
           tiku_desk_rect_t *ign, tiku_desk_rect_t *inv,
           tiku_desk_rect_t *go)
{
    expr->x = c.x + 10;
    expr->y = c.y + 28;
    expr->w = c.w - 20;
    expr->h = 20;
    rule->x = c.x + 10;
    rule->y = c.y + 56;
    rule->w = c.w - 20;
    rule->h = 20;
    ign->x = c.x + 10;
    ign->y = c.y + 84;
    ign->w = 110;
    ign->h = 16;
    inv->x = c.x + 130;
    inv->y = c.y + 84;
    inv->w = 120;
    inv->h = 16;
    go->x = c.x + c.w - 80;
    go->y = c.y + c.h - 28;
    go->w = 70;
    go->h = 22;
}

static void
select_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s, tiku_desk_rect_t c,
            void *ctx)
{
    desktop_t *app = ctx;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t expr, rule, ign, inv, go;
    char label[96];

    (void)w;
    tiku_desk_fill(s, c, TIKU_DESK_C_PANEL);
    sel_layout(c, &expr, &rule, &ign, &inv, &go);
    tiku_desk_text(s, f, c.x + 10, c.y + 18, "Select items whose name",
                   TIKU_DESK_C_TEXT);
    tiku_desk_ui_textfield_sel(s, expr, app->sel.expr.text,
                               (app->sel_field == SEL_HIT_EXPR)
                                   ? app->sel.expr.caret : -1,
                               app->sel.expr.sel_a, app->sel.expr.sel_b,
                               (app->sel_field == SEL_HIT_EXPR)
                                   ? TIKU_DESK_S_FOCUS : 0u);
    snprintf(label, sizeof label, "%s",
             tiku_trk_select_rule_label(app->sel.rule));
    tiku_desk_ui_menufield(s, rule, label, 0u);
    tiku_desk_ui_checkbox(s, ign, "Ignore case",
                          app->sel.ignore_case ? TIKU_DESK_S_ON : 0u);
    tiku_desk_ui_checkbox(s, inv, "Invert selection",
                          app->sel.invert ? TIKU_DESK_S_ON : 0u);
    tiku_desk_ui_button(s, go, "Select", 0u);
}

static sel_hit_t
select_hit(desktop_t *app, int x, int y)
{
    tiku_desk_rect_t c, expr, rule, ign, inv, go;

    if (app->sel_win == NULL) {
        return SEL_HIT_NONE;
    }
    c = tiku_trk_win_content(app->sel_win);
    sel_layout(c, &expr, &rule, &ign, &inv, &go);
#define IN(r) (x >= (r).x && x < (r).x + (r).w && \
               y >= (r).y && y < (r).y + (r).h)
    if (IN(expr)) { return SEL_HIT_EXPR; }
    if (IN(rule)) { return SEL_HIT_RULE; }
    if (IN(ign))  { return SEL_HIT_IGNORE; }
    if (IN(inv))  { return SEL_HIT_INVERT; }
    if (IN(go))   { return SEL_HIT_SELECT; }
#undef IN
    return SEL_HIT_NONE;
}

/** @brief Open it near the mouse, keeping whatever it held (CW-063). */
static void
open_select(desktop_t *app, folder_t *fl, int mx, int my)
{
    tiku_desk_rect_t frame;

    if (fl == NULL) {
        return;
    }
    app->sel_for = fl;
    tiku_trk_select_show(&app->sel, mx, my, SEL_W, SEL_H, DESK_W, DESK_H);
    frame.x = app->sel.x;
    frame.y = app->sel.y;
    frame.w = SEL_W;
    frame.h = SEL_H;
    if (app->sel_win != NULL && app->sel_win->open) {
        /* Reused, not remade: a second window would come up empty and the
         * expression the user is iterating on would be in the other one. */
        app->sel_win->frame = frame;
        tiku_trk_desk_activate(app->desk, app->sel_win);
    } else {
        app->sel_win = tiku_trk_desk_open(app->desk, "Select", frame,
                                          select_draw, app, &app->sel);
    }
    app->sel_field = SEL_HIT_EXPR;
    app->sel.expr.active = 1;
    app->dirty = 1;
}

/** @brief Apply the expression to the window it was opened from. */
static void
apply_select(desktop_t *app)
{
    char err[96];
    int n;

    if (app->sel_for == NULL || app->sel_for->win == NULL ||
        !app->sel_for->win->open) {
        snprintf(app->status, sizeof app->status,
                 "the window this was opened from has gone");
        return;
    }
    n = tiku_trk_poseview_select_matching(app->sel_for->view,
                                          app->sel.expr.text,
                                          tiku_trk_select_rule(
                                              app->sel.rule),
                                          app->sel.ignore_case,
                                          app->sel.invert, err, sizeof err);
    if (n < 0) {
        snprintf(app->status, sizeof app->status, "%s", err);
        return;
    }
    snprintf(app->status, sizeof app->status, "selected %d item%s", n,
             (n == 1) ? "" : "s");
    /* Hidden BEFORE the window it acted on is raised, so the selection
     * lands in an active window rather than behind this one. */
    tiku_trk_select_hide(&app->sel);
    if (app->sel_win != NULL) {
        tiku_trk_desk_close(app->desk, app->sel_win);
        app->sel_win = NULL;
    }
    tiku_trk_desk_activate(app->desk, app->sel_for->win);
}

/** @brief Open the Find panel, or bring it forward. */
static void
open_panel(desktop_t *app)
{
    tiku_desk_rect_t frame;

    if (app->panel_win != NULL && app->panel_win->open) {
        tiku_trk_desk_activate(app->desk, app->panel_win);
        app->dirty = 1;
        return;
    }
    frame.w = 560;
    frame.h = tiku_trk_find_height(&app->panel) + TIKU_TRK_WIN_TAB_H +
              2 * TIKU_TRK_WIN_BORDER;
    frame.x = 90;
    frame.y = 300;
    app->panel_win = tiku_trk_desk_open(app->desk, "Find", frame, panel_draw,
                                        &app->panel, &app->panel);
    app->dirty = 1;
}

/** @brief Run what the panel is asking and show the results. */
static void
panel_search(desktop_t *app)
{
    tiku_trk_query_t q;
    tiku_trk_qrun_t *run;
    char title[TIKU_TRK_PATH_MAX];
    char scope[64];

    tiku_trk_find_build(&app->panel, &q);
    run = tiku_trk_query_start(app->backend, &q);
    if (run == NULL) {
        return;
    }
    while (!tiku_trk_query_done(run)) {
        if (tiku_trk_query_step(run, 16) < 0) {
            break;
        }
    }
    (void)tiku_trk_find_scope_label(&app->panel, scope, sizeof scope);
    {
        /* Every run leaves a record, marked TEMPORARY: a search is a thing
         * that can be re-opened and edited, not only a window that appears
         * -- and one the user never named is swept up later rather than
         * accumulating for ever (Q-023, MA-053). */
        char name[TIKU_TRK_NAME_MAX];

        q.temporary = 1;
        /* The WALL clock, not the running one: how old a record is has to
         * mean the same thing after a restart, and it is also the date its
         * name carries. */
        q.saved_at = (int64_t)time(NULL);
        if (tiku_trk_find_default_name(&app->panel, (long)q.saved_at, name,
                                       sizeof name) > 0) {
            (void)tiku_trk_query_save(&q, name, app->store);
            snprintf(app->last_query, sizeof app->last_query, "%s", name);
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
        }
    }
    app->dirty = 1;
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

#define FPANEL_BTN_W   78
#define FPANEL_BTN_H   22
#define FPANEL_PAD     10
#define FPANEL_FIELD 22

/** @brief What a click inside a panel landed on. */
typedef enum {
    FPANEL_HIT_NONE = 0,
    FPANEL_HIT_LIST,
    FPANEL_HIT_NAME,
    FPANEL_HIT_DEFAULT,
    FPANEL_HIT_CANCEL
} fpanel_hit_t;

/** @brief The panel's parts inside @p c. */
static void
fpanel_layout(const desktop_t *app, tiku_desk_rect_t c, tiku_desk_rect_t *list,
             tiku_desk_rect_t *name, tiku_desk_rect_t *def,
             tiku_desk_rect_t *cancel)
{
    int bottom = c.y + c.h - FPANEL_PAD - FPANEL_BTN_H;
    int field = (app->fpanel.mode == TIKU_TRK_PANEL_SAVE)
                    ? FPANEL_FIELD + FPANEL_PAD : 0;

    list->x = c.x + FPANEL_PAD;
    list->y = c.y + FPANEL_PAD;
    list->w = c.w - 2 * FPANEL_PAD;
    list->h = bottom - list->y - FPANEL_PAD - field;
    if (list->h < 40) {
        list->h = 40;
    }
    name->x = list->x;
    name->y = list->y + list->h + FPANEL_PAD;
    name->w = list->w;
    name->h = FPANEL_FIELD;
    def->w = FPANEL_BTN_W;
    def->h = FPANEL_BTN_H;
    def->x = c.x + c.w - FPANEL_PAD - FPANEL_BTN_W;
    def->y = bottom;
    *cancel = *def;
    cancel->x = def->x - FPANEL_PAD - FPANEL_BTN_W;
}

/** @brief The models the panel's own view has selected. */
static int
fpanel_selection(const desktop_t *app, tiku_trk_model_t *out, int max)
{
    int i, n, k = 0;

    if (app->fpanel_view == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(app->fpanel_view);
    for (i = 0; i < n && k < max; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(app->fpanel_view, i);

        if (p != NULL && p->selected) {
            out[k++] = p->model;
        }
    }
    return k;
}

static void
fpanel_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s, tiku_desk_rect_t c,
           void *ctx)
{
    desktop_t *app = ctx;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t list, name, def, cancel;
    tiku_trk_model_t sel[8];
    int nsel = fpanel_selection(app, sel, 8);
    int rows, scroll, total, i;

    (void)w;
    tiku_desk_ui_panel(s, c);
    fpanel_layout(app, c, &list, &name, &def, &cancel);
    tiku_desk_ui_sunken(s, list, TIKU_DESK_C_DOC);
    rows = (list.h - 4) / app->row_h;
    tiku_trk_poseview_set_visible_rows(app->fpanel_view, rows);
    scroll = tiku_trk_poseview_scroll(app->fpanel_view);
    total = tiku_trk_poseview_count(app->fpanel_view);
    tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(app->fpanel_view,
                                                  scroll + i);
        tiku_desk_rect_t row;
        int mini = app->row_h - 4;

        if (p == NULL) {
            continue;
        }
        row.x = list.x + 2;
        row.y = list.y + 2 + i * app->row_h;
        row.w = list.w - 4;
        row.h = app->row_h;
        if (mini > MINI_ICON) {
            mini = MINI_ICON;
        }
        if (p->selected) {
            tiku_desk_fill(s, row, TIKU_DESK_C_SELECT);
        }
        (void)tiku_trk_icon_paint(s, tiku_trk_model_icon_name(&p->model),
                                  row.x + 2, row.y + (app->row_h - mini) / 2,
                                  mini);
        tiku_desk_text(s, f, row.x + 2 + mini + 4,
                       row.y + (app->row_h + f->height) / 2 - 2,
                       p->model.name,
                       p->selected ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
    }
    tiku_desk_clip_reset(s);
    if (app->fpanel.mode == TIKU_TRK_PANEL_SAVE) {
        tiku_desk_ui_textfield_sel(s, name, app->fpanel_name.text,
            (app->fpanel.focus == TIKU_TRK_PANEL_FOCUS_NAME)
                ? app->fpanel_name.caret : -1,
            app->fpanel_name.sel_a, app->fpanel_name.sel_b,
            (app->fpanel.focus == TIKU_TRK_PANEL_FOCUS_NAME)
                ? TIKU_DESK_S_FOCUS : 0u);
    }
    tiku_desk_ui_button(s, cancel, "Cancel", 0u);
    /* The label and the enabling both come from the panel, so what the
     * button SAYS and what it will do cannot disagree (AW-053). */
    tiku_desk_ui_button(s, def,
                        tiku_trk_panel_button_label(&app->fpanel, sel, nsel),
                        tiku_trk_panel_button_enabled(&app->fpanel, sel,
                                                      nsel)
                            ? TIKU_DESK_S_DEFAULT : TIKU_DESK_S_DISABLED);
}

/** @brief Point the panel's view at whatever folder the panel now shows. */
static void
fpanel_reload(desktop_t *app, const char *select)
{
    if (app->fpanel_view != NULL) {
        tiku_trk_poseview_free(app->fpanel_view);
    }
    app->fpanel_view = tiku_trk_poseview_new(app->backend, app->fpanel.dir);
    if (app->fpanel_view == NULL) {
        return;
    }
    tiku_trk_poseview_set_icons(app->fpanel_view, &app->nicons);
    /* List mode, and positions are never saved: a panel is a way through a
     * tree, not a place the user arranges (AW-064). */
    tiku_trk_poseview_set_mode(app->fpanel_view, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_set_multiple(app->fpanel_view, app->fpanel.multiple);
    (void)tiku_trk_poseview_reload(app->fpanel_view);
    if (select != NULL && select[0] != '\0') {
        int i, n = tiku_trk_poseview_count(app->fpanel_view);

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(app->fpanel_view, i);

            if (p != NULL && strcmp(p->model.name, select) == 0) {
                tiku_trk_poseview_click(app->fpanel_view, i, 0u);
                break;
            }
        }
    }
    if (app->fpanel_win != NULL) {
        tiku_trk_panel_title(&app->fpanel, app->fpanel_win->title,
                             sizeof app->fpanel_win->title);
    }
    app->dirty = 1;
}

/** @brief Open a panel for @p command over @p fl's selection. */
static void
fpanel_open(desktop_t *app, folder_t *fl, int command)
{
    tiku_desk_rect_t fr;
    int x = 0, y = 0, w = 460, h = 320;

    if (fl == NULL) {
        return;
    }
    app->fpanel_for = fl;
    app->fpanel_cmd = command;
    /* Asking WHERE, so folders are the answer and only one of them; the
     * files being moved are already chosen in the window behind it. */
    tiku_trk_panel_open(&app->fpanel, TIKU_TRK_PANEL_OPEN,
                        TIKU_TRK_PANEL_DIR, fl->path, app->backend,
                        "Tracker");
    app->fpanel.multiple = 0;
    if (!tiku_trk_panel_restore_frame(app->store, &x, &y, &w, &h)) {
        /* Nothing restored: centred, which is where a question belongs. */
        x = (DESK_W - w) / 2;
        y = (DESK_H - h) / 3;
    }
    fr.x = x;
    fr.y = y;
    fr.w = w;
    fr.h = h;
    tiku_trk_edit_start(&app->fpanel_name, "");
    if (app->fpanel_win != NULL && app->fpanel_win->open) {
        app->fpanel_win->frame = fr;
        tiku_trk_desk_activate(app->desk, app->fpanel_win);
    } else {
        char title[TIKU_TRK_WIN_TITLE_MAX];

        tiku_trk_panel_title(&app->fpanel, title, sizeof title);
        app->fpanel_win = tiku_trk_desk_open(app->desk, title, fr, fpanel_draw,
                                            app, &app->panel);
    }
    fpanel_reload(app, NULL);
}

/** @brief Close the panel, remembering the frame every panel will use. */
static void
fpanel_close(desktop_t *app)
{
    if (app->fpanel_win != NULL) {
        tiku_trk_panel_save_frame(app->store, app->fpanel_win->frame.x,
                                  app->fpanel_win->frame.y,
                                  app->fpanel_win->frame.w,
                                  app->fpanel_win->frame.h);
        tiku_trk_desk_close(app->desk, app->fpanel_win);
        app->fpanel_win = NULL;
    }
    if (app->fpanel_view != NULL) {
        tiku_trk_poseview_free(app->fpanel_view);
        app->fpanel_view = NULL;
    }
    app->fpanel_for = NULL;
    app->dirty = 1;
}

/** @brief Carry out what the panel was opened to ask. */
static void
fpanel_commit(desktop_t *app)
{
    char paths[16][TIKU_TRK_PATH_MAX];
    const char *sel[16];
    tiku_trk_model_t chosen[8];
    char dst[TIKU_TRK_PATH_MAX];
    int n, nsel;

    if (app->fpanel_for == NULL) {
        return;
    }
    /* A picked folder is the destination; nothing picked means the folder
     * being SHOWN, which is how "put it here" works without a row for it. */
    nsel = fpanel_selection(app, chosen, 8);
    if (nsel == 1 && tiku_trk_model_is_container(&chosen[0])) {
        snprintf(dst, sizeof dst, "%s", chosen[0].path);
    } else {
        snprintf(dst, sizeof dst, "%s", app->fpanel.dir);
    }
    n = folder_selection(app, app->fpanel_for, paths, sel, 16);
    if (n > 0) {
        if (app->fpanel_cmd == TIKU_TRK_CMD_COPY_TO) {
            (void)tiku_trk_fs_copy(app->fs, sel, n, dst,
                                   TIKU_TRK_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status, "copying %d item%s",
                     n, (n == 1) ? "" : "s");
        } else {
            (void)tiku_trk_fs_move(app->fs, sel, n, dst,
                                   TIKU_TRK_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status, "moving %d item%s",
                     n, (n == 1) ? "" : "s");
        }
        /* Chosen THROUGH a panel, so it joins the recent folders -- which
         * is what makes the list a record of where the user works rather
         * than of where they browsed (AW-056). */
        tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS, dst);
    }
    fpanel_close(app);
}

/** @brief Go somewhere else, keeping the panel open. */
static void
fpanel_go(desktop_t *app, const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    snprintf(app->fpanel.dir, sizeof app->fpanel.dir, "%s", dir);
    fpanel_reload(app, NULL);
}

/** @brief What a point inside the panel is over. */
static fpanel_hit_t
fpanel_hit(desktop_t *app, int x, int y, int *row)
{
    tiku_desk_rect_t c, list, name, def, cancel;

    if (row != NULL) {
        *row = -1;
    }
    if (app->fpanel_win == NULL || !app->fpanel_win->open) {
        return FPANEL_HIT_NONE;
    }
    c = tiku_trk_win_content(app->fpanel_win);
    fpanel_layout(app, c, &list, &name, &def, &cancel);
#define IN(r) (x >= (r).x && x < (r).x + (r).w && \
               y >= (r).y && y < (r).y + (r).h)
    if (IN(def))    { return FPANEL_HIT_DEFAULT; }
    if (IN(cancel)) { return FPANEL_HIT_CANCEL; }
    if (app->fpanel.mode == TIKU_TRK_PANEL_SAVE && IN(name)) {
        return FPANEL_HIT_NAME;
    }
    if (IN(list)) {
        int i = (y - list.y - 2) / app->row_h;

        if (row != NULL) {
            i += tiku_trk_poseview_scroll(app->fpanel_view);
            *row = (i >= 0 && i < tiku_trk_poseview_count(app->fpanel_view))
                       ? i : -1;
        }
        return FPANEL_HIT_LIST;
    }
#undef IN
    return FPANEL_HIT_NONE;
}

/** @brief The default button, however it was pressed. */
static void
fpanel_default(desktop_t *app)
{
    tiku_trk_model_t sel[8];
    int n = fpanel_selection(app, sel, 8);

    if (!tiku_trk_panel_button_enabled(&app->fpanel, sel, n)) {
        return;
    }
    /* A lone folder is ENTERED rather than chosen, which is what the
     * button relabelled itself to say (AW-053, AW-055). */
    if (n == 1 && tiku_trk_model_is_container(&sel[0])) {
        fpanel_go(app, sel[0].path);
        return;
    }
    fpanel_commit(app);
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

/*---------------------------------------------------------------------------*/
/* The Get Info window (AW-065..AW-079)                                       */
/*---------------------------------------------------------------------------*/

#define INFO_W       360
#define INFO_H       300
#define INFO_PAD      10
#define INFO_ICON     32
#define INFO_TAB_H    20
#define INFO_ROW_H    18

/** @brief What a click inside an info window landed on. */
typedef enum {
    IHIT_NONE = 0,
    IHIT_TAB,
    IHIT_NAME,
    IHIT_ICON,
    IHIT_FIELD,
    IHIT_PERM
} ihit_t;

/** @brief Which desktop a draw callback belongs to, and which slot. */
struct info_ctx {
    desktop_t *app;
    int        ix;
};

static struct info_ctx info_ctxs[INFO_MAX];

/** @brief The parts of an info window inside @p c. */
static void
info_layout(tiku_desk_rect_t c, tiku_desk_rect_t *icon,
            tiku_desk_rect_t *name, tiku_desk_rect_t *tabs,
            tiku_desk_rect_t *body)
{
    icon->x = c.x + INFO_PAD;
    icon->y = c.y + INFO_PAD;
    icon->w = INFO_ICON;
    icon->h = INFO_ICON;
    name->x = icon->x + INFO_ICON + INFO_PAD;
    name->y = icon->y + 6;
    name->w = c.w - (name->x - c.x) - INFO_PAD;
    name->h = 20;
    tabs->x = c.x + INFO_PAD;
    tabs->y = icon->y + INFO_ICON + INFO_PAD;
    tabs->w = c.w - 2 * INFO_PAD;
    tabs->h = INFO_TAB_H;
    body->x = tabs->x;
    body->y = tabs->y + INFO_TAB_H;
    body->w = tabs->w;
    body->h = c.y + c.h - INFO_PAD - body->y;
    if (body->h < 20) {
        body->h = 20;
    }
}

/** @brief The rectangle of tab @p i. */
static tiku_desk_rect_t
info_tab_rect(tiku_desk_rect_t tabs, int i)
{
    tiku_desk_rect_t r = tabs;

    r.w = tabs.w / TIKU_TRK_INFO_TABS;
    r.x = tabs.x + i * r.w;
    return r;
}

/** @brief The checkbox for permission @p i inside @p body. */
static tiku_desk_rect_t
info_perm_rect(tiku_desk_rect_t body, int i)
{
    tiku_desk_rect_t r;

    r.x = body.x + 8;
    r.y = body.y + 10 + i * (INFO_ROW_H + 4);
    r.w = 120;
    r.h = INFO_ROW_H;
    return r;
}

static void
info_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s, tiku_desk_rect_t c,
          void *ctx)
{
    struct info_ctx *ic = ctx;
    desktop_t *app = ic->app;
    tiku_trk_info_t *in = &app->info[ic->ix];
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t icon, name, tabs, body;
    int i;

    (void)w;
    tiku_desk_ui_panel(s, c);
    info_layout(c, &icon, &name, &tabs, &body);
    (void)tiku_trk_icon_paint(s, tiku_trk_model_icon_name(&in->model),
                              icon.x, icon.y, INFO_ICON);
    if (app->info_renaming == ic->ix) {
        tiku_desk_ui_textfield_sel(s, name, app->info_name.text,
                                   app->info_name.caret,
                                   app->info_name.sel_a,
                                   app->info_name.sel_b,
                                   TIKU_DESK_S_FOCUS);
    } else {
        tiku_desk_text(s, f, name.x, name.y + f->height, in->model.name,
                       TIKU_DESK_C_TEXT);
    }
    for (i = 0; i < TIKU_TRK_INFO_TABS; i++) {
        tiku_desk_rect_t r = info_tab_rect(tabs, i);

        tiku_desk_ui_button(s, r,
                            tiku_trk_info_tab_name((tiku_trk_info_tab_t)i),
                            (in->tab == (tiku_trk_info_tab_t)i)
                                ? TIKU_DESK_S_DEFAULT : 0u);
    }
    tiku_desk_ui_sunken(s, body, TIKU_DESK_C_PANEL);
    if (in->tab == TIKU_TRK_INFO_TAB_INFO) {
        for (i = 0; i < TIKU_TRK_INFO_FIELDS; i++) {
            char value[TIKU_TRK_INFO_VAL_MAX];
            const char *label = "";
            int y = body.y + 6 + i * INFO_ROW_H + f->height;

            if (tiku_trk_info_field(in, app->store, i, &label, value,
                                    sizeof value) == TIKU_TRK_INFO_F_NONE) {
                break;
            }
            tiku_desk_text(s, f, body.x + 8, y, label, TIKU_DESK_C_TEXT);
            tiku_desk_text(s, f, body.x + 96, y, value, TIKU_DESK_C_TEXT);
        }
    } else if (in->tab == TIKU_TRK_INFO_TAB_PERMISSIONS) {
        static const char *const WHO[] = { "Read", "Write", "Execute" };

        for (i = 0; i < 3; i++) {
            tiku_desk_ui_checkbox(s, info_perm_rect(body, i), WHO[i],
                (unsigned)(tiku_trk_info_perm(in, i) ? TIKU_DESK_S_ON : 0u) |
                (unsigned)(in->perm_editable ? 0u : TIKU_DESK_S_DISABLED));
        }
        if (!in->perm_editable) {
            tiku_desk_text(s, f, body.x + 8,
                           body.y + 14 + 3 * (INFO_ROW_H + 4) + f->height,
                           "This item's permissions are not yours to set.",
                           TIKU_DESK_C_TEXT);
        }
    } else {
        for (i = 0; i < in->nattr; i++) {
            int y = body.y + 6 + i * INFO_ROW_H + f->height;

            if (y > body.y + body.h) {
                break;
            }
            tiku_desk_text(s, f, body.x + 8, y, in->attr[i].name,
                           TIKU_DESK_C_TEXT);
            tiku_desk_text(s, f, body.x + 150, y, in->attr[i].type,
                           TIKU_DESK_C_TEXT);
            tiku_desk_text(s, f, body.x + 210, y, in->attr[i].value,
                           TIKU_DESK_C_TEXT);
        }
        if (in->nattr == 0) {
            tiku_desk_text(s, f, body.x + 8, body.y + 6 + f->height,
                           "No attributes.", TIKU_DESK_C_TEXT);
        }
    }
}

/** @brief The slot an info window for @p path is in, or -1. */
static int
info_slot_of(const desktop_t *app, const char *path)
{
    int i;

    for (i = 0; i < INFO_MAX; i++) {
        if (app->info[i].used && strcmp(app->info[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief Close the info window in slot @p i. */
static void
info_close(desktop_t *app, int i)
{
    if (i < 0 || i >= INFO_MAX) {
        return;
    }
    if (app->info_win[i] != NULL) {
        tiku_trk_desk_close(app->desk, app->info_win[i]);
        app->info_win[i] = NULL;
    }
    if (app->info_renaming == i) {
        app->info_renaming = -1;
    }
    memset(&app->info[i], 0, sizeof app->info[i]);
    app->dirty = 1;
}

/** @brief Open Get Info on @p path, or bring its window forward (AW-065). */
static void
info_open(desktop_t *app, const char *path)
{
    tiku_desk_rect_t fr;
    int i = info_slot_of(app, path);

    if (i >= 0) {
        /* One window per node: asking again is asking for the one that is
         * already showing the answer. */
        if (app->info_win[i] != NULL) {
            tiku_trk_desk_activate(app->desk, app->info_win[i]);
        }
        return;
    }
    for (i = 0; i < INFO_MAX; i++) {
        if (!app->info[i].used) {
            break;
        }
    }
    if (i >= INFO_MAX || tiku_trk_info_open(&app->info[i], app->backend,
                                            path) != 0) {
        return;
    }
    (void)tiku_trk_info_attrs(&app->info[i], app->store);
    /* Cascaded, so several info windows are all readable rather than one
     * hiding the rest (AW-065). */
    fr.x = 120 + i * CASCADE_STEP;
    fr.y = 80 + i * CASCADE_STEP;
    fr.w = INFO_W;
    fr.h = INFO_H;
    info_ctxs[i].app = app;
    info_ctxs[i].ix = i;
    {
        char title[TIKU_TRK_WIN_TITLE_MAX];

        tiku_trk_info_title(&app->info[i], title, sizeof title);
        app->info_win[i] = tiku_trk_desk_open(app->desk, title, fr,
                                              info_draw, &info_ctxs[i],
                                              &app->info[i]);
    }
    app->dirty = 1;
}

/** @brief What a point inside info window @p i is over. */
static ihit_t
info_hit(desktop_t *app, int i, int x, int y, int *which)
{
    tiku_desk_rect_t c, icon, name, tabs, body;
    int k;

    if (which != NULL) {
        *which = -1;
    }
    if (i < 0 || i >= INFO_MAX || app->info_win[i] == NULL) {
        return IHIT_NONE;
    }
    c = tiku_trk_win_content(app->info_win[i]);
    info_layout(c, &icon, &name, &tabs, &body);
#define IN(r) (x >= (r).x && x < (r).x + (r).w && \
               y >= (r).y && y < (r).y + (r).h)
    if (IN(icon)) { return IHIT_ICON; }
    if (IN(name)) { return IHIT_NAME; }
    for (k = 0; k < TIKU_TRK_INFO_TABS; k++) {
        tiku_desk_rect_t r = info_tab_rect(tabs, k);

        if (IN(r)) {
            if (which != NULL) { *which = k; }
            return IHIT_TAB;
        }
    }
    if (app->info[i].tab == TIKU_TRK_INFO_TAB_PERMISSIONS) {
        for (k = 0; k < 3; k++) {
            tiku_desk_rect_t r = info_perm_rect(body, k);

            if (IN(r)) {
                if (which != NULL) { *which = k; }
                return IHIT_PERM;
            }
        }
    }
    if (app->info[i].tab == TIKU_TRK_INFO_TAB_INFO && IN(body)) {
        k = (y - body.y - 6) / INFO_ROW_H;
        if (k >= 0 && k < TIKU_TRK_INFO_FIELDS) {
            if (which != NULL) { *which = k; }
            return IHIT_FIELD;
        }
    }
#undef IN
    return IHIT_NONE;
}

/** @brief Start renaming from the header (AW-070). */
static void
info_rename_begin(desktop_t *app, int i)
{
    if (i < 0 || i >= INFO_MAX || !app->info[i].used) {
        return;
    }
    app->info_renaming = i;
    tiku_trk_edit_start(&app->info_name, app->info[i].model.name);
    tiku_trk_edit_deny(&app->info_name, tiku_trk_fs_name_deny());
    app->dirty = 1;
}

/** @brief Finish renaming: @p keep says whether to apply it (AW-070). */
static void
info_rename_end(desktop_t *app, int keep)
{
    int i = app->info_renaming;
    char err[160];

    if (i < 0 || i >= INFO_MAX) {
        return;
    }
    app->info_renaming = -1;
    if (!keep || app->info_name.len == 0 ||
        strcmp(app->info_name.text, app->info[i].model.name) == 0) {
        app->dirty = 1;
        return;
    }
    if (tiku_trk_fs_rename(app->fs, app->info[i].path, app->info_name.text,
                           err, sizeof err) != 0) {
        /* Said out loud and the old name kept: a rename that failed
         * silently would leave the window showing a name the entry does
         * not have (AW-070). */
        tiku_trk_alert_open(&app->alert, TIKU_TRK_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_ERROR, err, "Cancel");
    } else {
        char parent[TIKU_TRK_PATH_MAX], to[TIKU_TRK_PATH_MAX];

        if (tiku_trk_info_location(&app->info[i], parent, sizeof parent,
                                   NULL, 0)) {
            snprintf(to, sizeof to, "%.*s/%.*s",
                     (int)(sizeof to - sizeof app->info_name.text - 2u),
                     (strcmp(parent, "/") == 0) ? "" : parent,
                     (int)(sizeof app->info_name.text - 1u),
                     app->info_name.text);
            (void)tiku_trk_info_moved(&app->info[i], app->backend,
                                      app->info[i].path, to);
            if (app->info_win[i] != NULL) {
                tiku_trk_info_title(&app->info[i], app->info_win[i]->title,
                                    sizeof app->info_win[i]->title);
            }
        }
    }
    app->dirty = 1;
}

/** @brief Carry out a menu choice on the focused folder. */
static void
menu_invoke(desktop_t *app, folder_t *fl, int command)
{
    if (fl == NULL) {
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
        info_open(app, (p != NULL && p->selected) ? p->model.path
                                                 : fl->path);
        break;
    }
    case TIKU_TRK_CMD_EDIT_QUERY: {
        /* Re-open the panel ON the query rather than running it again:
         * what the user wants from a result set is usually one more
         * condition, not the same search a second time (Q-045). */
        tiku_trk_query_t q;

        if (app->last_query[0] == '\0' ||
            tiku_trk_query_load(&q, app->last_query, app->store) != 0) {
            snprintf(app->status, sizeof app->status,
                     "that query cannot be opened");
            break;
        }
        if (!tiku_trk_find_from_query(&app->panel, &q)) {
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
    case TIKU_TRK_CMD_SELECT_ALL:
        tiku_trk_poseview_select_all(fl->view);
        break;
    case TIKU_TRK_CMD_LIST_VIEW:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_LIST);
        break;
    case TIKU_TRK_CMD_ICON_VIEW:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        break;
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_MINI);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_32:
        /* Choosing a size also switches INTO icon mode: the sizes are the
         * icon view's own submenu, and picking one in list mode otherwise
         * changes a number nothing on screen reads (CW-024). */
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(fl->view, 32);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_48:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(fl->view, 48);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_40:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(fl->view, 40);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_64:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(fl->view, 64);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_96:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(fl->view, 96);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_128:
        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(fl->view, 128);
        break;
    case TIKU_TRK_CMD_SORT_BY_NAME:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_NAME);
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_KIND:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_KIND);
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_SIZE:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_SIZE);
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_VALUE:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_VALUE);
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_CAP:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_CAP);
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_COL_KIND:
        (void)tiku_trk_poseview_toggle_column(fl->view, TIKU_TRK_COL_KIND);
        break;
    case TIKU_TRK_CMD_COL_VALUE:
        (void)tiku_trk_poseview_toggle_column(fl->view, TIKU_TRK_COL_VALUE);
        break;
    case TIKU_TRK_CMD_COL_SIZE:
        (void)tiku_trk_poseview_toggle_column(fl->view, TIKU_TRK_COL_SIZE);
        break;
    case TIKU_TRK_CMD_COL_CAP:
        (void)tiku_trk_poseview_toggle_column(fl->view, TIKU_TRK_COL_CAP);
        break;
    case TIKU_TRK_CMD_REVERSE_ORDER:
        /* Sorting by the field already in force is the reverse toggle, and
         * the icons are re-laid out after it -- the same Cleanup the
         * Arrange-by path runs, so both routes end in one arrangement. */
        tiku_trk_poseview_sort_by(fl->view,
                                  tiku_trk_poseview_sort_field(fl->view));
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_CLEANUP:
        /* SNAP, not re-lay-out: Clean up straightens the arrangement the
         * user made, and replacing it with sort order would throw away
         * exactly what they were tidying (PVL-021). */
        (void)tiku_trk_poseview_snap_to_grid(fl->view);
        break;
    case TIKU_TRK_CMD_CLEANUP_ALL:
        /* Shift is the stronger one, and it IS the re-lay-out. */
        tiku_trk_poseview_cleanup(fl->view,
                                  folder_content(app, fl).w);
        break;
    case TIKU_TRK_CMD_RESIZE_TO_FIT:
        (void)tiku_trk_desk_zoom(app->desk, fl->win, folder_fit_w(app, fl),
                                 folder_fit_h(app, fl));
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
                            tiku_trk_alert_open(&app->alert,
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
        prefs_open(app);
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
            tiku_trk_desk_activate(app->desk, app->folders[i].win);
            app->dirty = 1;
            return &app->folders[i];
        }
    }
    if (app->folder_count >= TIKU_TRK_WIN_MAX) {
        return NULL;
    }
    fl = &app->folders[app->folder_count];
    memset(fl, 0, sizeof *fl);
    snprintf(fl->path, sizeof fl->path, "%s", path);
    /* Opening a folder is what puts it in the recent list the Deskbar
     * menu offers -- the list is a record of where the user has been, and
     * this is the one place that knows (DB-002, AW-056). */
    tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS, path);
    fl->app = app;
    fl->view = tiku_trk_poseview_new(app->backend, path);
    if (fl->view == NULL) {
        return NULL;
    }
    /* Every window shares the one cache, so a file open in two of them has
     * one icon and an edit to it is dropped once. */
    tiku_trk_poseview_set_icons(fl->view, &app->nicons);
    tiku_trk_poseview_set_measure(fl->view, measure_label, app);
    {
        const tiku_desk_font_t *f = tiku_desk_font_plain();

        /* Every icon-mode height is a function of the label's, so the view
         * has to be told what it is being drawn in (PVL-007). */
        tiku_trk_poseview_set_font_metrics(fl->view, f->height, f->ascent);
    }
    tiku_trk_poseview_set_icon_hit(fl->view, pose_icon_hit, app);
    /* The columns this folder was left with.  The pair has existed since
     * the single-window shell was built and this shell called neither, so
     * every column a user added, moved or resized here was discarded when
     * the window closed (CW-005, MA-070). */
    tiku_trk_poseview_set_folders_first(fl->view,
        tiku_trk_prefs_get(&app->prefs, "SortFolderNamesFirst"));
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
    fl->win = tiku_trk_desk_open(app->desk, path, frame, folder_draw, fl,
                                 fl->path);
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
folder_of(desktop_t *app, tiku_trk_win_t *w)
{
    int i;

    for (i = 0; w != NULL && i < app->folder_count; i++) {
        if (app->folders[i].win == w) {
            return &app->folders[i];
        }
    }
    return NULL;
}

/** @brief Row under a point inside a folder window's content. */
static int
folder_row_at(desktop_t *app, folder_t *fl, int x, int y)
{
    tiku_desk_rect_t c = folder_content(app, fl);
    int rel;

    if (x < c.x || x > c.x + c.w || y < c.y + 2 ||
        y > c.y + c.h - COUNT_H) {
        return -1;
    }
    if (tiku_trk_poseview_mode(fl->view) != TIKU_TRK_MODE_LIST) {
        /* An icon layout has no rows to divide by: the point goes to the
         * view, which knows where every pose is and answers on its ART
         * rather than on its cell (PVS-019). */
        int ox = 0, oy = 0;
        tiku_trk_pose_t *p;

        tiku_trk_poseview_origin(fl->view, &ox, &oy);
        p = tiku_trk_poseview_pose_at(fl->view,
                                      x - (c.x + 2) + ox,
                                      y - (c.y + 2) + oy);
        return (p != NULL) ? p->index : -1;
    }
    rel = (y - c.y - 2) / app->row_h;
    rel += tiku_trk_poseview_scroll(fl->view);
    return (rel < tiku_trk_poseview_count(fl->view)) ? rel : -1;
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
    tiku_trk_win_t *fw;
    folder_t *fl;
    int i, n;

    if (app == NULL || name == NULL) {
        return 0;
    }
    fw = tiku_trk_desk_focused(app->desk);
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
        if (fl == &app->dfl) {
            return 0;
        }
        /* The other place a pose can be.  The Desktop has no window, so it
         * is never the focused one and would otherwise be unaskable. */
        fl = &app->dfl;
    }
}

static void
blit(desktop_t *app)
{
    if (app->dpy == NULL) {
        return;                     /* displayless: the surface IS the
                                     * output, and -shot writes it */
    }
    memcpy(app->img->data, app->surf->px,
           (size_t)app->surf->w * (size_t)app->surf->h * 4u);
    XPutImage(app->dpy, app->win, app->gc, app->img, 0, 0, 0, 0,
              (unsigned)app->surf->w, (unsigned)app->surf->h);
    XFlush(app->dpy);
}

/**
 * @brief Publish the open windows to the Deskbar's application entry.
 *
 * The bar shows windows nested under their application, so it needs the
 * list each time it draws; titles arrive with the application name stripped
 * because "Tracker: /sys" repeated down a column reads as noise.
 */
static void
publish_windows(desktop_t *app)
{
    tiku_trk_dbwin_t wins[TIKU_TRK_DB_WINS_MAX];
    tiku_trk_win_t *focused = tiku_trk_desk_focused(app->desk);
    int i, n = tiku_trk_desk_count(app->desk), k = 0;

    for (i = 0; i < n && k < TIKU_TRK_DB_WINS_MAX; i++) {
        tiku_trk_win_t *w = tiku_trk_desk_at(app->desk, i);
        if (w == NULL || !w->open) {
            continue;
        }
        memset(&wins[k], 0, sizeof wins[k]);
        tiku_trk_deskbar_strip_title("Tracker", w->title, wins[k].title,
                                     sizeof wins[k].title);
        wins[k].state = w->minimized ? TIKU_TRK_DBW_MINIMIZED
                                     : TIKU_TRK_DBW_SHOWN;
        wins[k].current = (w == focused);
        wins[k].tag = w->tag;
        k++;
    }
    tiku_trk_deskbar_set_windows(app->deskbar, app->team, wins, k,
                                 (focused != NULL));
}

/** @brief Where the alert sits: centred, and the same size every time. */
#define ALERT_W 420
#define ALERT_H 150

static tiku_desk_rect_t
alert_frame(void)
{
    tiku_desk_rect_t r;

    r.x = (DESK_W - ALERT_W) / 2;
    r.y = (DESK_H - ALERT_H) / 3;   /* a third down reads better than half */
    r.w = ALERT_W;
    r.h = ALERT_H;
    return r;
}

/**
 * @brief Draw the alert over everything.
 *
 * Above the menus as well as the windows: it is the one thing that must be
 * answered before anything else happens.
 */
static void
alert_draw(desktop_t *app)
{
    tiku_desk_rect_t f = alert_frame();
    const tiku_desk_font_t *fo = tiku_desk_font_plain();
    int i, ty;

    if (!app->alert.open) {
        return;
    }
    tiku_desk_fill(app->surf, f, TIKU_DESK_C_PANEL);
    tiku_desk_bevel(app->surf, f,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    ty = f.y + 24;
    {   /* Wrapped by hand: the message is a sentence about a file, and a
         * file name is exactly the thing that makes it too long. */
        const char *p = app->alert.text;
        char line[80];
        size_t n = 0;

        while (*p != '\0' && ty < f.y + f.h - 46) {
            size_t k = 0;
            const char *sp = NULL;

            while (p[k] != '\0' && k < sizeof line - 1u) {
                if (p[k] == ' ') { sp = p + k; }
                if (tiku_desk_text_width(fo, line) > f.w - 32) { break; }
                line[k] = p[k];
                k++;
                line[k] = '\0';
            }
            if (p[k] != '\0' && sp != NULL && sp > p) {
                k = (size_t)(sp - p);
                line[k] = '\0';
            }
            tiku_desk_text(app->surf, fo, f.x + 16, ty, line,
                           TIKU_DESK_C_TEXT);
            ty += fo->height + 4;
            p += k;
            while (*p == ' ') { p++; }
            n++;
            if (n > 6u) { break; }
        }
    }
    for (i = 0; i < app->alert.nbutton; i++) {
        tiku_desk_rect_t b;
        int bx, by, bw, bh;

        tiku_trk_alert_button_rect(&app->alert, i, f.w, f.h, &bx, &by, &bw,
                                   &bh);
        b.x = f.x + bx; b.y = f.y + by; b.w = bw; b.h = bh;
        tiku_desk_fill(app->surf, b, TIKU_DESK_C_PANEL);
        tiku_desk_bevel(app->surf, b,
                         tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
                         tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
        tiku_desk_text(app->surf, fo, b.x + 10, b.y + 16,
                       app->alert.button[i], TIKU_DESK_C_TEXT);
    }
}

/** @brief Draw the shared status window, if any job is running. */
static void
progress_draw(desktop_t *app)
{
    tiku_desk_rect_t f;
    const tiku_desk_font_t *fo = tiku_desk_font_plain();
    int i, row = 0;

    if (!app->prog.open) {
        return;
    }
    f.w = 380;
    f.h = 22 + TIKU_TRK_PROGRESS_ROWS * 34;
    f.x = DESK_W - f.w - 24;
    f.y = DESK_H - f.h - 24;
    tiku_desk_fill(app->surf, f, TIKU_DESK_C_PANEL);
    tiku_desk_bevel(app->surf, f,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    for (i = 0; i < TIKU_TRK_PROGRESS_ROWS; i++) {
        char line[160];
        const char *leaf;
        tiku_desk_rect_t bar;
        int y;

        if (!app->prog.row[i].used) {
            continue;
        }
        y = f.y + 14 + row * 34;
        {   /* A paused row says how to get going again; a frozen counter
             * with no explanation reads as a hang (FS-074). */
            const char *t = tiku_trk_progress_row_text(&app->prog, i);

            leaf = strrchr(t, '/');
            leaf = (leaf != NULL) ? leaf + 1 : t;
        }
        snprintf(line, sizeof line, "%s%.40s",
                 app->prog.row[i].paused ? "" : app->prog.row[i].caption,
                 leaf);
        tiku_desk_text(app->surf, fo, f.x + 10, y, line, TIKU_DESK_C_TEXT);
        {   /* Speed for eight seconds, then the estimate for four: the
             * cycle is shared by every row, since two alternating out of
             * step would be harder to read than one (FS-071). */
            int64_t open_for = tiku_trk_now() - app->prog_opened;

            if (tiku_trk_progress_show_speed(open_for)) {
                int64_t sp = tiku_trk_progress_speed(&app->prog, i);

                if (sp > 0) {
                    snprintf(line, sizeof line, "%lld KB/s",
                             (long long)(sp / 1024));
                } else {
                    snprintf(line, sizeof line, "%d of %d",
                             app->prog.row[i].done, app->prog.row[i].total);
                }
            } else {
                int64_t eta = tiku_trk_progress_eta(&app->prog, i);

                if (eta >= 0) {
                    snprintf(line, sizeof line, "%lld s left",
                             (long long)eta);
                } else {
                    snprintf(line, sizeof line, "%d of %d",
                             app->prog.row[i].done, app->prog.row[i].total);
                }
            }
        }
        tiku_desk_text(app->surf, fo, f.x + f.w - 70, y, line,
                       TIKU_DESK_C_TEXT);
        bar.x = f.x + 10;
        bar.y = y + 6;
        bar.w = f.w - 20;
        bar.h = 8;
        tiku_desk_fill(app->surf, bar, TIKU_DESK_C_DOC);
        if (app->prog.row[i].total > 0) {
            bar.w = (f.w - 20) * app->prog.row[i].done /
                    app->prog.row[i].total;
            if (bar.w > 0) {
                tiku_desk_fill(app->surf, bar, TIKU_DESK_C_SELECT);
            }
        }
        {   /* Pause and stop.  Stop is outermost: the destructive one is
             * furthest from the pause a user is aiming for. */
            int k;

            for (k = TIKU_TRK_PROGRESS_HIT_PAUSE;
                 k <= TIKU_TRK_PROGRESS_HIT_STOP; k++) {
                tiku_desk_rect_t b;
                int bx, by, bw, bh;

                tiku_trk_progress_button_rect(row, k, f.w, &bx, &by, &bw,
                                              &bh);
                b.x = f.x + bx; b.y = f.y + by; b.w = bw; b.h = bh;
                tiku_desk_fill(app->surf, b, TIKU_DESK_C_PANEL);
                tiku_desk_bevel(app->surf, b,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
                tiku_desk_text(app->surf, fo, b.x + 6, b.y + 13,
                    (k == TIKU_TRK_PROGRESS_HIT_STOP) ? "x"
                        : (app->prog.row[i].paused ? ">" : "|"),
                    TIKU_DESK_C_TEXT);
            }
        }
        row++;
    }
}

/**
 * @brief Hand the alert's answer back to whoever asked.
 *
 * The alert carries its own tag, so the shell does not have to remember
 * which question it put up -- which it could not do reliably once a job can
 * ask twice.
 */
static void
answer_alert(desktop_t *app)
{
    if (!app->alert.open || app->alert.chosen < 0) {
        return;
    }
    if (app->alert.tag == TIKU_TRK_ALERT_TAG_CONFLICT) {
        if (app->alert.chosen == app->alert.cancel) {
            /* Cancel abandons the whole job, not just this collision. */
            tiku_trk_fs_cancel(app->fs);
        } else {
            tiku_trk_fs_resolve(app->fs,
                                tiku_trk_alert_conflict_answer(&app->alert));
        }
    } else if (app->alert.tag == TIKU_TRK_ALERT_TAG_ERROR) {
        tiku_trk_fs_resolve_error(app->fs,
                                  app->alert.chosen != app->alert.cancel);
    }
    tiku_trk_alert_reset(&app->alert);
}

/**
 * @brief Draw the preferences window.
 *
 * Every control writes its setting as it is clicked and the file is saved
 * at once: there is no OK button, so there is no moment at which a change
 * is pending (TS-058).
 */
static void
prefs_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s, tiku_desk_rect_t c,
           void *ctx)
{
    desktop_t *app = ctx;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int idx[32], n, i, x, y, bw, bh;

    (void)w;
    tiku_desk_fill(s, c, TIKU_DESK_C_PANEL);
    for (i = 0; i < TIKU_TRK_PAGES; i++) {
        tiku_desk_rect_t r;
        int on = (i == (int)app->prefs_page);

        tiku_trk_prefs_page_rect(i, &x, &y, &bw, &bh);
        r.x = c.x + x; r.y = c.y + y; r.w = bw; r.h = bh;
        tiku_desk_fill(s, r, on ? TIKU_DESK_C_SELECT : TIKU_DESK_C_PANEL);
        /* A page with something changed is named in bold; with no bold
         * face to hand, it is marked instead -- the point is that the
         * user can see WHICH page they touched (TS-060). */
        tiku_desk_text(s, f, r.x + 6, r.y + 15,
                       tiku_trk_prefs_page_name((tiku_trk_page_t)i),
                       on ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
        if (tiku_trk_prefs_revertable(&app->prefs, (tiku_trk_page_t)i)) {
            tiku_desk_text(s, f, r.x + bw - 10, r.y + 15, "*",
                           on ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
        }
    }
    n = tiku_trk_prefs_page_controls(&app->prefs, app->prefs_page, idx, 32);
    for (i = 0; i < n; i++) {
        tiku_trk_pref_t *pr = &app->prefs.pref[idx[i]];
        tiku_desk_rect_t box;

        tiku_trk_prefs_control_rect(i, &x, &y, &bw, &bh);
        box.x = c.x + x; box.y = c.y + y + 4; box.w = 12; box.h = 12;
        if (pr->type == TIKU_TRK_PREF_BOOL) {
            tiku_desk_fill(s, box, TIKU_DESK_C_DOC);
            tiku_desk_frame(s, box, TIKU_DESK_C_TEXT);
            if (pr->value) {
                tiku_desk_text(s, f, box.x + 2, box.y + 11, "x",
                               TIKU_DESK_C_TEXT);
            }
        }
        tiku_desk_text(s, f, c.x + x + 18, c.y + y + 14, pr->name,
                       TIKU_DESK_C_TEXT);
    }
    for (i = 0; i < 2; i++) {
        tiku_desk_rect_t b;
        int live = i ? tiku_trk_prefs_any_revertable(&app->prefs)
                     : tiku_trk_prefs_any_defaultable(&app->prefs);

        tiku_trk_prefs_button_rect(i, &x, &y, &bw, &bh);
        b.x = c.x + x; b.y = c.y + y; b.w = bw; b.h = bh;
        tiku_desk_fill(s, b, TIKU_DESK_C_PANEL);
        tiku_desk_bevel(s, b,
            tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
            tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
        /* Disabled is drawn, not hidden: a button that vanishes when it
         * has nothing to do teaches the user it was never there. */
        tiku_desk_text(s, f, b.x + 12, b.y + 16, i ? "Revert" : "Defaults",
                       live ? TIKU_DESK_C_TEXT
                            : tiku_desk_tint(TIKU_DESK_C_TEXT,
                                             TIKU_DESK_LIGHTEN_2));
    }
}

/** @brief Open the preferences window, or bring it forward (AW-101). */
static void
prefs_open(desktop_t *app)
{
    tiku_desk_rect_t fr;

    if (app->prefs_win != NULL && app->prefs_win->open) {
        tiku_trk_desk_activate(app->desk, app->prefs_win);
        return;
    }
    fr.x = (DESK_W - TIKU_TRK_PREF_W) / 2;
    fr.y = (DESK_H - TIKU_TRK_PREF_H) / 3;
    fr.w = TIKU_TRK_PREF_W;
    /* The layout's height is the CONTENT's; the frame has to carry the
     * title bar on top of it or the buttons fall off the bottom. */
    fr.h = TIKU_TRK_PREF_H + 26;
    /* The baseline Revert measures against is taken when the window is
     * SHOWN, not when it was built: reopening it starts the comparison
     * again (TS-059). */
    tiku_trk_prefs_mark_opened(&app->prefs);
    app->prefs_win = tiku_trk_desk_open(app->desk, "Tracker preferences",
                                        fr, prefs_draw, app, NULL);
}

#define SW_SLOT   56
#define SW_PAD    12
#define SW_ARROW  14

/** @brief The panel's size, from the slots it must hold. */
static tiku_desk_rect_t
switcher_frame(desktop_t *app)
{
    tiku_desk_rect_t f;
    int slots = (app->sw.n < TIKU_TRK_SW_SLOTS) ? app->sw.n
                                                : TIKU_TRK_SW_SLOTS;
    int x = 0, y = 0;

    if (slots < 1) { slots = 1; }
    f.w = SW_PAD * 2 + slots * SW_SLOT + 2 * SW_ARROW;
    f.h = SW_PAD * 2 + TIKU_TRK_SW_LARGE + 22;
    tiku_trk_switcher_place(DESK_W, DESK_H, f.w, f.h, &x, &y);
    f.x = x;
    f.y = y;
    return f;
}

/**
 * @brief Draw the strip.
 *
 * Every icon's size comes from the switcher's own clock, so a redraw at any
 * moment shows the animation where it actually is rather than where a frame
 * counter thinks it is (DB-058).
 */
static void
switcher_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s, tiku_desk_rect_t c,
              void *ctx)
{
    desktop_t *app = ctx;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int64_t now = tiku_trk_now();
    char title[160];
    int i, slots;

    (void)w;
    tiku_desk_fill(s, c, TIKU_DESK_C_PANEL);
    slots = (app->sw.n < TIKU_TRK_SW_SLOTS) ? app->sw.n : TIKU_TRK_SW_SLOTS;
    for (i = 0; i < slots; i++) {
        int which = app->sw.scroll + i;
        int size, cx, cy;
        tiku_desk_rect_t box;

        if (which >= app->sw.n) {
            break;
        }
        size = tiku_trk_switcher_icon_size(&app->sw, which, now);
        /* Centred in its slot, so a growing icon expands about its middle
         * rather than crawling out of one corner. */
        cx = c.x + SW_PAD + SW_ARROW + i * SW_SLOT + SW_SLOT / 2;
        cy = c.y + SW_PAD + TIKU_TRK_SW_LARGE / 2;
        box.x = cx - size / 2;
        box.y = cy - size / 2;
        box.w = size;
        box.h = size;
        if (which == app->sw.sel) {
            tiku_desk_fill(s, box, TIKU_DESK_C_SELECT);
        }
        if (!tiku_trk_icon_paint(s, "app", box.x, box.y, size)) {
            tiku_desk_frame(s, box, TIKU_DESK_C_TEXT);
        }
    }
    /* Arrows only where there is more strip to reach. */
    if (tiku_trk_switcher_scroller(&app->sw, 0)) {
        tiku_desk_text(s, f, c.x + 4, c.y + SW_PAD + 28, "<",
                       TIKU_DESK_C_TEXT);
    }
    if (tiku_trk_switcher_scroller(&app->sw, 1)) {
        tiku_desk_text(s, f, c.x + c.w - 12, c.y + SW_PAD + 28, ">",
                       TIKU_DESK_C_TEXT);
    }
    tiku_trk_switcher_title(&app->sw, app->sw.sel, app->sw.selwin, title,
                            sizeof title);
    tiku_desk_text(s, f, c.x + SW_PAD, c.y + c.h - 10, title,
                   TIKU_DESK_C_TEXT);
}

/** @brief Show the switcher, seeding it from the Deskbar's team list. */
static void
switcher_open(desktop_t *app, int back)
{
    int i;

    if (!app->sw.shown) {
        /* Seeded from the teams the Deskbar already tracks: one list of
         * what is running, not two that can disagree. */
        tiku_trk_switcher_init(&app->sw);
        for (i = 0; i < tiku_trk_deskbar_team_count(app->deskbar); i++) {
            const char *nm = tiku_trk_deskbar_team_name(app->deskbar, i);

            if (nm != NULL) {
                (void)tiku_trk_switcher_app(&app->sw, nm,
                                            strcmp(nm, "Tracker") == 0);
            }
        }
    }
    tiku_trk_switcher_open(&app->sw, tiku_trk_now(), back);
    if (app->sw_win == NULL || !app->sw_win->open) {
        app->sw_win = tiku_trk_desk_open(app->desk, "Switcher",
                                         switcher_frame(app),
                                         switcher_draw, app, NULL);
    }
}

/** @brief Take the switcher down, committing or not. */
static void
switcher_close(desktop_t *app, int commit)
{
    if (app->sw_win != NULL) {
        tiku_trk_desk_close(app->desk, app->sw_win);
        app->sw_win = NULL;
    }
    if (commit && app->sw.sel >= 0 && app->sw.sel < app->sw.n) {
        snprintf(app->status, sizeof app->status, "switched to %.60s",
                 app->sw.app[app->sw.sel].name);
    }
    app->sw.shown = 0;
    app->dirty = 1;
}

static void
draw_all(desktop_t *app)
{
    publish_windows(app);
    if (!tiku_trk_deskbar_raised(app->deskbar)) {
        /* Under the windows: with auto-raise on, a window may cover the
         * bar until the user reaches for it.  Draw order IS the z-order
         * here -- there is no window server to ask (DB-052). */
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
        tiku_trk_desk_draw(app->desk);
    } else {
        tiku_trk_desk_draw(app->desk);
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
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
    progress_draw(app);
    alert_draw(app);
    app->dirty = 0;
}

int
main(int argc, char **argv)
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
    int64_t last_click = 0;
    int last_row = -1;
    const char *openme[TIKU_TRK_WIN_MAX];
    int nopen = 0;
    /* Started deliberately plain: no session is restored.  The row's other
     * clauses -- replicants, add-ons -- have nothing here to suppress
     * (AW-007). */
    int safe_mode = 0;

    memset(&app, 0, sizeof app);
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
    app.desk = tiku_trk_desk_new(app.surf);
    app.deskbar = tiku_trk_deskbar_new(app.backend);
    app.team = tiku_trk_deskbar_add_team(app.deskbar, "Tracker");
    scan_teams(&app);               /* everything else that is running */
    tiku_trk_cwmenu_init(&app.menus);
    tiku_trk_find_init(&app.panel);
    /* The Select window's defaults are wildcard and ignore-case, and a
     * zeroed struct is neither -- it is "starts with", case-sensitive. */
    tiku_trk_select_init(&app.sel);
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
    tiku_trk_find_set_store(&app.panel, app.store);
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
    tiku_trk_thumb_init(&app.thumbs);
    tiku_trk_nodeicon_init(&app.nicons);
    tiku_trk_volwatch_init(&app.volwatch);
    /* Not zero: slot 0 is a real window, and "0" would mean the first info
     * window opens with its name already being edited. */
    app.info_renaming = -1;
    app.info_relink = -1;
    tiku_trk_prefs_init(&app.prefs);
    app.prefs_page = TIKU_TRK_PAGE_DESKTOP;
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(app.prefs_path, sizeof app.prefs_path,
                     "%s/.config/TrackerSettings", home);
            (void)tiku_trk_prefs_load(&app.prefs, app.prefs_path);
        }
    }

    {
        /* The Desktop itself.  Its identity is the boot volume's Desktop
         * folder -- the state has to be filed under something, and the
         * union it shows is a rule about contents, not a place. */
        const char *home = getenv("HOME");

        snprintf(app.dfl.path, sizeof app.dfl.path, "%s/Desktop",
                 (home != NULL && home[0] != '\0') ? home : "");
        app.dfl.app = &app;
        app.dfl.win = NULL;
        app.dfl.view = tiku_trk_poseview_new(app.backend, app.dfl.path);
        app.desk_row = -1;
        if (app.dfl.view != NULL) {
            const tiku_desk_font_t *df = tiku_desk_font_plain();

            tiku_trk_poseview_set_icons(app.dfl.view, &app.nicons);
            tiku_trk_poseview_set_measure(app.dfl.view, measure_label, &app);
            tiku_trk_poseview_set_font_metrics(app.dfl.view, df->height,
                                               df->ascent);
            tiku_trk_poseview_set_icon_hit(app.dfl.view, pose_icon_hit,
                                           &app);
            tiku_trk_poseview_set_viewport(app.dfl.view, DESK_W, DESK_H);
            tiku_trk_poseview_set_mode(app.dfl.view, TIKU_TRK_MODE_ICON);
            desk_icons_rebuild(&app);
            tiku_trk_vdir_watch_init(&app.dwatch, &app.dvdir);
            tiku_trk_desk_set_backdrop(app.desk, desk_icons_draw, &app);
        }
    }

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
                    (void)tiku_trk_find_add_place(&app.panel,
                                                  roots[k].name);
                }
            }
        }
        (void)open_folder(&app, "/sys/mem");
        (void)open_folder(&app, "/dev");
    } else if (nopen > 0) {
        for (i = 0; i < nopen; i++) {
            (void)open_folder(&app, openme[i]);
        }
    } else {
        /* What was open last time, if anything -- the windows come back
         * where they were, which is the whole of the spatial promise
         * (AW-004).  A safe start asks for nothing and gets home. */
        char sess[TIKU_TRK_SESSION_MAX][TIKU_TRK_PATH_MAX];
        int nsess = safe_mode ? 0
                              : tiku_trk_session_load(app.store, sess,
                                                      TIKU_TRK_SESSION_MAX);
        int opened = 0;

        for (i = 0; i < nsess; i++) {
            if (open_folder(&app, sess[i]) != NULL) {
                opened++;
            }
        }
        if (opened == 0) {
            const char *home = getenv("HOME");

            (void)open_folder(&app, home ? home : "/");
        }
    }

    if (script_path != NULL) {
        /* Displayless.  The shell draws into the same surface either way,
         * so a scripted run exercises the real draw path -- it just has
         * nobody to show it to (see tiku_trk_script.h). */
        sc = tiku_trk_script_open(script_path);
        if (sc == NULL) {
            return 2;
        }
    } else {
        app.dpy = XOpenDisplay(NULL);
        if (app.dpy == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
    }
    if (app.dpy != NULL) {
        int scr = DefaultScreen(app.dpy);
        app.win = XCreateSimpleWindow(app.dpy, RootWindow(app.dpy, scr), 0, 0,
                                      DESK_W, DESK_H, 0,
                                      BlackPixel(app.dpy, scr),
                                      WhitePixel(app.dpy, scr));
        XStoreName(app.dpy, app.win, "TikuOS Desktop");
        XSelectInput(app.dpy, app.win, ExposureMask | KeyPressMask |
                     KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask);
        app.del = XInternAtom(app.dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(app.dpy, app.win, &app.del, 1);
        XMapWindow(app.dpy, app.win);
        app.gc = XCreateGC(app.dpy, app.win, 0, NULL);
        app.img = XCreateImage(app.dpy, DefaultVisual(app.dpy, scr),
                               (unsigned)DefaultDepth(app.dpy, scr), ZPixmap,
                               0, malloc((size_t)DESK_W * DESK_H * 4u),
                               DESK_W, DESK_H, 32, 0);
    }
    if (shot != NULL) {
        shot_at = tiku_trk_now() + 2000000;
    }
    if (shot_select) {
        /* Scripted: the Select window over the front folder, with an
         * expression already in it, so a capture shows the whole control
         * set the way it comes up. */
        tiku_trk_win_t *fw = tiku_trk_desk_focused(app.desk);
        folder_t *ffl = folder_of(&app, fw);

        open_select(&app, ffl, 520, 300);
        {
            const char *e = "gpio*";

            while (*e != '\0') {
                (void)tiku_trk_edit_key(&app.sel.expr, TIKU_TRK_EDIT_CHAR, 0,
                                        *e++);
            }
        }
    }
    if (shot_live) {
        /* Search for what is currently on under /dev, change one of them,
         * and let the window's OWN refresh path apply the difference. */
        char err[128];

        open_panel(&app);
        tiku_trk_find_set_mode(&app.panel, TIKU_TRK_QMODE_ATTRIBUTE);
        app.panel.row[0].field = TIKU_TRK_QF_VALUE;
        app.panel.row[0].op = TIKU_TRK_OP_IS;
        snprintf(app.panel.row[0].value, sizeof app.panel.row[0].value, "1");
        app.panel.everywhere = 0;
        {
            int k;

            for (k = 0; k < app.panel.place_count; k++) {
                if (strcmp(app.panel.place[k], "dev") == 0) {
                    app.panel.place_mask = 1u << k;
                }
            }
        }
        panel_search(&app);
        (void)app.backend->ops->write(app.backend, "/dev/led2", "1", 1, err,
                                      sizeof err);
        {
            int k;

            for (k = 0; k < app.folder_count; k++) {
                (void)refresh_results(&app, &app.folders[k]);
            }
        }
        close_window(&app, app.panel_win);
    }
    if (shot_panel) {
        /* Drive the panel the way a user would: open it, choose attribute
         * mode, fill two rows joined by or, scope it, and search. */
        open_panel(&app);
        tiku_trk_find_set_mode(&app.panel, TIKU_TRK_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(&app.panel);
        app.panel.row[0].field = TIKU_TRK_QF_UNIT;
        app.panel.row[0].op = TIKU_TRK_OP_IS;
        snprintf(app.panel.row[0].value, sizeof app.panel.row[0].value,
                 "bytes");
        app.panel.row[0].join = TIKU_TRK_JOIN_OR;
        app.panel.row[1].field = TIKU_TRK_QF_UNIT;
        app.panel.row[1].op = TIKU_TRK_OP_IS;
        snprintf(app.panel.row[1].value, sizeof app.panel.row[1].value, "s");
        app.panel.everywhere = 0;
        app.panel.place_mask = 1u;
        panel_search(&app);
        app.panel_win->frame.h = tiku_trk_find_height(&app.panel) +
                                 TIKU_TRK_WIN_TAB_H +
                                 2 * TIKU_TRK_WIN_BORDER;
        tiku_trk_desk_activate(app.desk, app.panel_win);
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
        (void)tiku_trk_desk_zoom(app.desk, fl->win, wide + 8,
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
            XEvent ev;
            KeySym ev_ks = NoSymbol;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (sc != NULL) {
                /* One instruction per turn, so the deferred work between
                 * turns -- a stepped copy, the armed rename, the notice
                 * poll -- happens where it happens on a real clock. */
                tiku_trk_script_step_t sp;
                tiku_trk_script_state_t sst;
                tiku_trk_win_t *fw = tiku_trk_desk_focused(app.desk);

                sst.status = app.status;
                sst.title = (fw != NULL) ? fw->title : NULL;
                sst.scroll = 0;
                sst.rows = 0;
                sst.alert = app.alert.open ? app.alert.text : NULL;
                {
                    int wi, wn = 0;

                    for (wi = 0; wi < tiku_trk_desk_count(app.desk); wi++) {
                        tiku_trk_win_t *ww = tiku_trk_desk_at(app.desk, wi);

                        if (ww != NULL && ww->open) {
                            wn++;
                        }
                    }
                    sst.windows = wn;
                }
                sst.selected = 0;
                sst.sel_name = NULL;
                sst.find_text = app.panel.name_text;
                sst.find_type = tiku_trk_find_type_label(&app.panel);
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
                sst.desk_selected = 0;
                sst.desk_sel_name = NULL;
                if (app.dfl.view != NULL) {
                    int di, dn = tiku_trk_poseview_count(app.dfl.view);

                    for (di = 0; di < dn; di++) {
                        tiku_trk_pose_t *dp =
                            tiku_trk_poseview_at(app.dfl.view, di);

                        if (dp != NULL && dp->selected) {
                            if (sst.desk_sel_name == NULL) {
                                sst.desk_sel_name = dp->model.name;
                            }
                            sst.desk_selected++;
                        }
                    }
                }
                sst.pose_at = script_pose_at;
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
                                        app.panel.type)) {
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
                    } else if (fw != NULL && fw == app.fpanel_win &&
                               app.fpanel_view != NULL) {
                        /* The panel has a view too, and it is the one the
                         * script is looking at while the panel is up. */
                        sst.scroll = tiku_trk_poseview_scroll(app.fpanel_view);
                        sst.rows = tiku_trk_poseview_count(app.fpanel_view);
                    }
                }
                if (!tiku_trk_script_pump(sc, &sst, &sp)) {
                    app.running = 0;
                    break;
                }
                switch (sp.what) {
                case TIKU_TRK_SC_KEY:
                    ev.type = KeyPress;
                    ev.xkey.state = sp.state;
                    ev_ks = sp.ks;
                    have = 1;
                    break;
                case TIKU_TRK_SC_MOVE:
                    ev.type = MotionNotify;
                    ev.xmotion.x = sp.x;
                    ev.xmotion.y = sp.y;
                    ev.xmotion.state = sp.state;
                    have = 1;
                    break;
                case TIKU_TRK_SC_PRESS:
                case TIKU_TRK_SC_RELEASE:
                    ev.type = (sp.what == TIKU_TRK_SC_PRESS) ? ButtonPress
                                                             : ButtonRelease;
                    ev.xbutton.x = sp.x;
                    ev.xbutton.y = sp.y;
                    ev.xbutton.state = sp.state;
                    ev.xbutton.button = sp.button;
                    ev.xbutton.time = (Time)(tiku_trk_now() / 1000);
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

                    if (m == NULL) {
                        fprintf(stderr, "script: no menu is open for "
                                        "'%s'\n", sp.path);
                        break;
                    }
                    for (k = 0; k < m->count; k++) {
                        if (!m->item[k].separator &&
                            strstr(m->item[k].label, sp.path) != NULL) {
                            ev.type = ButtonPress;
                            ev.xbutton.x = fr.x + 8;
                            ev.xbutton.y = fr.y +
                                tiku_trk_menu_item_top(m, k) +
                                tiku_trk_menu_item_height(m, k) / 2;
                            ev.xbutton.button = 1u;
                            ev.xbutton.time = (Time)(tiku_trk_now() / 1000);
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
            } else if (app.dpy != NULL && XPending(app.dpy) > 0) {
                XNextEvent(app.dpy, &ev);
                if (ev.type == KeyPress || ev.type == KeyRelease) {
                    /* The release matters now: letting go of Control is
                     * what commits the switcher. */
                    ev_ks = XLookupKeysym(&ev.xkey, 0);
                }
                have = 1;
            }
            if (!have) {
                break;
            }
            switch (ev.type) {
            case Expose:
                app.dirty = 1;
                break;
            case ButtonPress: {
                if (ev.xbutton.button == 1u &&
                    (ev.xbutton.state & ControlMask)) {
                    /* Before anything else, because a Ctrl+click on the
                     * bar means only one thing and every handler below
                     * would read it as something else (DB-049). */
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.xbutton.x >= bf.x &&
                        ev.xbutton.x < bf.x + bf.w &&
                        ev.xbutton.y >= bf.y &&
                        ev.xbutton.y < bf.y + bf.h) {
                        app.dock_drag = 1;
                        break;
                    }
                }
                {   /* An info window takes its own clicks first. */
                    tiku_trk_win_t *iw = tiku_trk_desk_focused(app.desk);
                    int islot = -1, k;

                    for (k = 0; k < INFO_MAX; k++) {
                        if (app.info_win[k] != NULL && app.info_win[k] == iw
                            && app.info_win[k]->open) {
                            islot = k;
                            break;
                        }
                    }
                    if (islot >= 0 && !app.alert.open) {
                        int which = -1;
                        ihit_t ih = info_hit(&app, islot, ev.xbutton.x,
                                             ev.xbutton.y, &which);

                        if (ih == IHIT_TAB && which >= 0) {
                            app.info[islot].tab =
                                (tiku_trk_info_tab_t)which;
                            if (app.info[islot].tab ==
                                    TIKU_TRK_INFO_TAB_ATTRIBUTES) {
                                /* Re-read on the way in: an attribute
                                 * written since the window opened would
                                 * otherwise never appear. */
                                (void)tiku_trk_info_attrs(&app.info[islot],
                                                          app.store);
                            }
                            app.dirty = 1;
                            break;
                        }
                        if (ih == IHIT_NAME) {
                            info_rename_begin(&app, islot);
                            break;
                        }
                        if (ih == IHIT_PERM && which >= 0) {
                            char perr[160];

                            if (tiku_trk_info_perm_toggle(&app.info[islot],
                                    app.backend, which, perr,
                                    sizeof perr) != 0) {
                                tiku_trk_alert_open(&app.alert,
                                    TIKU_TRK_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_ERROR, perr,
                                    "Cancel");
                            }
                            app.dirty = 1;
                            break;
                        }
                        if (ih == IHIT_ICON) {
                            /* Double-clicking the icon opens the item, as
                             * double-clicking its row would (AW-075). */
                            if (tiku_trk_now() - app.press_at < 500000) {
                                (void)open_folder(&app,
                                    tiku_trk_model_is_container(
                                        &app.info[islot].model)
                                        ? app.info[islot].path
                                        : app.info[islot].path);
                            }
                            app.press_at = tiku_trk_now();
                            break;
                        }
                        if (ih == IHIT_FIELD && which >= 0) {
                            char value[TIKU_TRK_INFO_VAL_MAX];
                            const char *label = "";
                            tiku_trk_info_field_t fk =
                                tiku_trk_info_field(&app.info[islot],
                                    app.store, which, &label, value,
                                    sizeof value);
                            char dir[TIKU_TRK_PATH_MAX];
                            char sel[TIKU_TRK_NAME_MAX];

                            if (fk == TIKU_TRK_INFO_F_SIZE &&
                                tiku_trk_model_is_container(
                                    &app.info[islot].model)) {
                                /* Clicking the size asks again: a folder
                                 * counted an hour ago is a guess (AW-068). */
                                tiku_trk_info_calc_start(&app.info[islot]);
                            } else if (fk == TIKU_TRK_INFO_F_LOCATION &&
                                tiku_trk_info_location(&app.info[islot],
                                    dir, sizeof dir, sel, sizeof sel)) {
                                folder_t *nf = open_folder(&app, dir);

                                if (nf != NULL) {
                                    select_named(&app, nf, sel);
                                }
                            } else if (fk == TIKU_TRK_INFO_F_LINK) {
                                if (tiku_trk_info_link_target(
                                        &app.info[islot], app.backend, dir,
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
                                        tiku_trk_desk_focused(app.desk));

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
                if (app.fpanel_win != NULL && app.fpanel_win->open &&
                    !app.alert.open &&
                    tiku_trk_desk_focused(app.desk) == app.fpanel_win) {
                    int prow = -1;
                    fpanel_hit_t fh = fpanel_hit(&app, ev.xbutton.x,
                                                 ev.xbutton.y, &prow);

                    if (fh == FPANEL_HIT_CANCEL) {
                        /* Cancelling answers the question too, and the
                         * frame is still remembered: the size the user
                         * chose is not undone by changing their mind. */
                        fpanel_close(&app);
                        break;
                    }
                    if (fh == FPANEL_HIT_DEFAULT) {
                        fpanel_default(&app);
                        break;
                    }
                    if (fh == FPANEL_HIT_NAME) {
                        app.fpanel.focus = TIKU_TRK_PANEL_FOCUS_NAME;
                        app.dirty = 1;
                        break;
                    }
                    if (fh == FPANEL_HIT_LIST) {
                        app.fpanel.focus = TIKU_TRK_PANEL_FOCUS_LIST;
                        tiku_trk_poseview_click(app.fpanel_view, prow,
                                                0u);
                        if (prow >= 0 &&
                            tiku_trk_now() - app.press_at < 500000 &&
                            app.press_row == prow) {
                            tiku_trk_pose_t *pp =
                                tiku_trk_poseview_at(app.fpanel_view, prow);

                            /* A double-click on a folder goes in; on
                             * anything else it is the answer (AW-055). */
                            if (pp != NULL &&
                                tiku_trk_panel_activate(&app.fpanel,
                                    &pp->model) ==
                                    TIKU_TRK_PANEL_ACT_ENTER) {
                                fpanel_go(&app, pp->model.path);
                            } else if (pp != NULL) {
                                fpanel_commit(&app);
                            }
                        }
                        app.press_at = tiku_trk_now();
                        app.press_row = prow;
                        app.dirty = 1;
                        break;
                    }
                }
                if (app.prefs_win != NULL && app.prefs_win->open &&
                    !app.alert.open) {
                    tiku_desk_rect_t pc =
                        tiku_trk_win_content(app.prefs_win);
                    int which = -1;
                    tiku_trk_pref_hit_t ph;

                    ph = tiku_trk_prefs_hit(&app.prefs, app.prefs_page,
                                            ev.xbutton.x - pc.x,
                                            ev.xbutton.y - pc.y, &which);
                    if (ph != TIKU_TRK_PREF_HIT_NONE) {
                        if (ph == TIKU_TRK_PREF_HIT_PAGE) {
                            app.prefs_page = (tiku_trk_page_t)which;
                        } else if (ph == TIKU_TRK_PREF_HIT_SETTING) {
                            tiku_trk_pref_t *pr = &app.prefs.pref[which];

                            if (pr->type == TIKU_TRK_PREF_BOOL) {
                                pr->value = !pr->value;
                            }
                        } else if (ph == TIKU_TRK_PREF_HIT_DEFAULTS) {
                            int k;

                            for (k = 0; k < TIKU_TRK_PAGES; k++) {
                                tiku_trk_prefs_defaults(&app.prefs,
                                    (tiku_trk_page_t)k);
                            }
                        } else {
                            int k;

                            for (k = 0; k < TIKU_TRK_PAGES; k++) {
                                tiku_trk_prefs_revert(&app.prefs,
                                    (tiku_trk_page_t)k);
                            }
                        }
                        /* Applied and saved at once: with no OK button
                         * there is no moment at which a change is
                         * pending (TS-058). */
                        if (app.prefs_path[0] != '\0') {
                            (void)tiku_trk_prefs_save(&app.prefs,
                                                      app.prefs_path);
                        }
                        app.dirty = 1;
                        break;
                    }
                }
                if (!app.alert.open && app.prog.open) {
                    tiku_desk_rect_t pf;
                    int prow = 0;
                    tiku_trk_progress_hit_t ph;

                    pf.w = 380;
                    pf.h = 22 + TIKU_TRK_PROGRESS_ROWS * 34;
                    pf.x = DESK_W - pf.w - 24;
                    pf.y = DESK_H - pf.h - 24;
                    ph = tiku_trk_progress_hit(&app.prog, pf.w,
                             ev.xbutton.x - pf.x, ev.xbutton.y - pf.y,
                             &prow);
                    if (ph == TIKU_TRK_PROGRESS_HIT_PAUSE) {
                        app.prog.row[prow].paused =
                            !app.prog.row[prow].paused;
                        app.dirty = 1;
                        break;
                    }
                    if (ph == TIKU_TRK_PROGRESS_HIT_STOP) {
                        /* Stop cancels rather than killing: what is
                         * already written stays, and the file in hand is
                         * cleaned up (FS-033). */
                        tiku_trk_fs_cancel(app.fs);
                        app.dirty = 1;
                        break;
                    }
                }
                if (app.alert.open) {
                    /* Modal: while a question is up nothing else takes a
                     * click, which is what makes it a question rather than
                     * a notice. */
                    tiku_desk_rect_t af = alert_frame();
                    int pick = tiku_trk_alert_button_at(&app.alert, af.w,
                                   af.h, ev.xbutton.x - af.x,
                                   ev.xbutton.y - af.y);

                    if (pick >= 0) {
                        app.alert.chosen = pick;
                        answer_alert(&app);
                    }
                    app.dirty = 1;
                    break;
                }
                tiku_trk_win_t *w = NULL;
                tiku_trk_hit_t hit;
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
                                                  ev.xbutton.x,
                                                  ev.xbutton.y);
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
                    tiku_trk_win_t *fw = tiku_trk_desk_focused(app.desk);
                    folder_t *ffl = folder_of(&app, fw);
                    int pick = -1;

                    if (app.popup_sub != NULL) {
                        pick = tiku_trk_menu_item_at(app.popup_sub,
                                                     app.popup_sub_frame,
                                                     ev.xbutton.x,
                                                     ev.xbutton.y);
                        if (pick >= 0) {
                            menu_invoke(&app, ffl,
                                        app.popup_sub->item[pick].command);
                        }
                    }
                    if (pick < 0) {
                        pick = tiku_trk_menu_item_at(app.popup,
                                                     app.popup_frame,
                                                     ev.xbutton.x,
                                                     ev.xbutton.y);
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
                    app.menus.bar.open = -1;
                    app.dirty = 1;
                    break;
                }

                /* The Deskbar and its menu take precedence: it sits above
                 * every window, as a corner strip must. */
                if (tiku_trk_deskbar_menu_open(app.deskbar) ||
                    tiku_trk_deskbar_hit(app.deskbar, ev.xbutton.x,
                                         ev.xbutton.y)) {
                    unsigned dmods = 0;
                    int secondary = (ev.xbutton.button == 3);

                    if (ev.xbutton.state & ControlMask) {
                        dmods |= TIKU_TRK_MOD_CMD;
                    }
                    if (tiku_trk_deskbar_click(app.deskbar, ev.xbutton.x,
                                               ev.xbutton.y, dmods,
                                               secondary, &pick)) {
                        tiku_trk_win_t *t;

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
                            t = tiku_trk_desk_find(app.desk, pick.tag);
                            if (t != NULL) {
                                t->minimized = 0;
                                tiku_trk_desk_activate(app.desk, t);
                            }
                            break;
                        case TIKU_TRK_DB_MINIMIZE_WINDOW:
                            t = tiku_trk_desk_find(app.desk, pick.tag);
                            if (t != NULL) { t->minimized = 1; }
                            break;
                        case TIKU_TRK_DB_CLOSE_WINDOW:
                            t = tiku_trk_desk_find(app.desk, pick.tag);
                            close_window(&app, t);
                            break;
                        case TIKU_TRK_DB_HIDE_ALL:
                        case TIKU_TRK_DB_SHOW_ALL: {
                            int k, m = tiku_trk_desk_count(app.desk);
                            for (k = 0; k < m; k++) {
                                tiku_trk_win_t *ww =
                                    tiku_trk_desk_at(app.desk, k);
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
                            while (tiku_trk_desk_count(app.desk) > 0) {
                                tiku_trk_desk_close(app.desk,
                                    tiku_trk_desk_at(app.desk, 0));
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
                if (ev.xbutton.button == 4u || ev.xbutton.button == 5u) {
                    /* The wheel.  In an icon mode it moves the pixel
                     * origin; in a list it steps rows, which is what it
                     * has always done. */
                    tiku_trk_win_t *ww = NULL;
                    folder_t *wfl;
                    tiku_desk_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.xbutton.x >= bf.x &&
                        ev.xbutton.x < bf.x + bf.w &&
                        ev.xbutton.y >= bf.y &&
                        ev.xbutton.y < bf.y + bf.h) {
                        /* Over the bar: the list scrolls rather than
                         * whatever window happens to be under it
                         * (DB-034). */
                        if (tiku_trk_deskbar_scroll(app.deskbar,
                                (ev.xbutton.button == 4u) ? -1 : 1)) {
                            app.dirty = 1;
                        }
                        break;
                    }
                    (void)tiku_trk_desk_hit(app.desk, ev.xbutton.x,
                                            ev.xbutton.y, &ww);
                    wfl = folder_of(&app, ww);
                    if (wfl != NULL) {
                        int step = (ev.xbutton.button == 4u) ? -1 : 1;

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
                hit = tiku_trk_desk_hit(app.desk, ev.xbutton.x, ev.xbutton.y,
                                        &w);
                if (app.panel_win != NULL && app.panel_win->open &&
                    tiku_trk_find_popup_open(&app.panel)) {
                    /* Its menus hang below the panel's window and over
                     * whatever is behind it, so the click goes to the
                     * panel wherever it lands -- the panel eats it either
                     * way, which is what closes a menu clicked off. */
                    w = app.panel_win;
                    hit = TIKU_TRK_HIT_CONTENT;
                }
                if (w == NULL && app.dfl.view != NULL) {
                    /* The backdrop is a pose area, so a click on it is a
                     * click in a view -- the same branch every window's
                     * content goes through, which is what keeps the
                     * Desktop's icons behaving like a folder's (AW-018). */
                    hit = TIKU_TRK_HIT_CONTENT;
                }
                if (w != NULL && hit != TIKU_TRK_HIT_CONTENT) {
                    /* The CHROME activates a window; the content does not.
                     * That is what lets a click reach a list in a window
                     * behind another without the raise eating it -- the
                     * click-through a spatial desktop depends on. */
                    tiku_trk_desk_activate(app.desk, w);
                }
                if (hit == TIKU_TRK_HIT_ZOOM) {
                    folder_t *fl = folder_of(&app, w);

                    if (fl != NULL) {
                        (void)tiku_trk_desk_zoom(app.desk, w,
                                                 folder_fit_w(&app, fl),
                                                 folder_fit_h(&app, fl));
                    } else if (w == app.panel_win) {
                        (void)tiku_trk_desk_zoom(app.desk, w, 544,
                                                 tiku_trk_find_height(
                                                     &app.panel));
                    }
                    app.dirty = 1;
                    break;
                }
                if (hit == TIKU_TRK_HIT_CLOSE) {
                    /* Control+close closes every window rather than this
                     * one, which is the only way to clear a spatial desktop
                     * without visiting each window. */
                    if (ev.xbutton.state & ControlMask) {
                        int k;

                        for (k = 0; k < app.folder_count; k++) {
                            if (app.folders[k].win != NULL) {
                                tiku_trk_desk_close(app.desk,
                                                    app.folders[k].win);
                            }
                        }
                        if (app.panel_win != NULL) {
                            close_window(&app, app.panel_win);
                        }
                        app.dirty = 1;
                        break;
                    }
                    /* Option+close takes the folder's whole subtree with
                     * it: the windows a descent through a deep folder left
                     * behind are exactly the ones nobody wants to close one
                     * at a time (AW-012). */
                    if (ev.xbutton.state & Mod1Mask) {
                        folder_t *cf = folder_of(&app, w);

                        if (cf != NULL) {
                            close_windows_under(&app, cf->path);
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (hit == TIKU_TRK_HIT_CLOSE) {
                    close_window(&app, w);
                } else if (hit == TIKU_TRK_HIT_TAB ||
                           hit == TIKU_TRK_HIT_RESIZE) {
                    /* The Find window is not resizable: its height is its
                     * contents, and dragging its corner would only tear the
                     * layout away from the controls (Q-001). */
                    if (hit == TIKU_TRK_HIT_RESIZE && w == app.panel_win) {
                        app.dirty = 1;
                        break;
                    }
                    tiku_trk_desk_begin_track(app.desk, w, hit, ev.xbutton.x,
                                              ev.xbutton.y);
                } else if (hit == TIKU_TRK_HIT_CONTENT && w != NULL &&
                           w == app.sel_win) {
                    switch (select_hit(&app, ev.xbutton.x, ev.xbutton.y)) {
                    case SEL_HIT_EXPR:
                        app.sel_field = SEL_HIT_EXPR;
                        break;
                    case SEL_HIT_RULE:
                        /* One control, five rules: clicking it steps to the
                         * next rather than opening a menu, which keeps the
                         * whole window keyboard- and mouse-reachable. */
                        tiku_trk_select_set_rule(&app.sel,
                            (app.sel.rule + 1) % TIKU_TRK_SELECT_RULES);
                        break;
                    case SEL_HIT_IGNORE:
                        app.sel.ignore_case = !app.sel.ignore_case;
                        break;
                    case SEL_HIT_INVERT:
                        app.sel.invert = !app.sel.invert;
                        break;
                    case SEL_HIT_SELECT:
                        apply_select(&app);
                        break;
                    default:
                        break;
                    }
                    app.dirty = 1;
                } else if (hit == TIKU_TRK_HIT_CONTENT && w != NULL &&
                           w == app.panel_win) {
                    tiku_trk_find_hit_t what =
                        tiku_trk_find_click(&app.panel,
                                            tiku_trk_win_content(w),
                                            ev.xbutton.x, ev.xbutton.y);

                    if (what == TIKU_TRK_FIND_HIT_SEARCH) {
                        panel_search(&app);
                    } else if (what == TIKU_TRK_FIND_HIT_SAVE) {
                        char nm[128];

                        tiku_trk_find_default_name(&app.panel,
                                                   (long)time(NULL), nm,
                                                   sizeof nm);
                        {
                            tiku_trk_query_t q;

                            tiku_trk_find_build(&app.panel, &q);
                            if (tiku_trk_query_save(&q, nm, app.store) == 0) {
                                snprintf(app.status, sizeof app.status,
                                         "saved as \"%.60s\"", nm);
                            } else {
                                snprintf(app.status, sizeof app.status,
                                         "could not save the query");
                            }
                        }
                    }
                    if (tiku_trk_find_note(&app.panel)[0] != '\0') {
                        /* A menu that loaded a saved search without saying
                         * which one leaves the user looking at a panel
                         * that changed by itself. */
                        snprintf(app.status, sizeof app.status, "%.90s",
                                 tiku_trk_find_note(&app.panel));
                        tiku_trk_find_clear_note(&app.panel);
                    }
                    /* The panel grows and shrinks with its rows. */
                    w->frame.h = tiku_trk_find_height(&app.panel) +
                                 TIKU_TRK_WIN_TAB_H + 2 * TIKU_TRK_WIN_BORDER;
                    app.dirty = 1;
                } else if (hit == TIKU_TRK_HIT_CONTENT) {
                    folder_t *fl = (w != NULL) ? folder_of(&app, w)
                                               : &app.dfl;
                    if (fl != NULL && fl->view != NULL) {
                        int row = folder_row_at(&app, fl, ev.xbutton.x,
                                                ev.xbutton.y);
                        unsigned mods = 0;
                        int64_t now = tiku_trk_now();

                        if (ev.xbutton.state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
                        if (ev.xbutton.state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }
                        if (ev.xbutton.button == 3) {
                            /* PVS-018: the menu is about what was pointed
                             * at, so the selection is normalized first, the
                             * window is activated and brought up to date,
                             * and any pending rename is dropped. */
                            tiku_trk_pose_t *p = (row >= 0)
                                ? tiku_trk_poseview_at(fl->view, row) : NULL;
                            tiku_trk_sc_ctx_t c;

                            tiku_trk_poseview_cancel_edit_wait(fl->view);
                            /* CW-029: a menu opened with the MOUSE commits
                             * an edit in progress; one opened from the
                             * keyboard leaves it alone, so a shortcut can
                             * still act on the text being typed. */
                            commit_rename(&app, 1);
                            (void)tiku_trk_poseview_context_click(fl->view,
                                                                  row);
                            tiku_trk_desk_activate(app.desk, w);
                            c = menu_ctx(&app, fl, mods);
                            build_open_with(&app, fl);
                            app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (row >= 0 && p != NULL)
                                                ? &p->model : NULL, &c);
                            /* A container's context menu carries a walk
                             * into it, so a descendant can be reached
                             * without leaving the window (CW-045). */
                            app.nav_on = 0;
                            if (row >= 0 && p != NULL &&
                                tiku_trk_nav_wanted(&p->model)) {
                                tiku_trk_nav_open(&app.nav, app.backend,
                                                  p->model.path);
                                app.nav.hide_dotfiles =
                                    tiku_trk_prefs_get(&app.prefs,
                                                       "HideDotFiles");
                                app.nav.folders_first =
                                    tiku_trk_prefs_get(&app.prefs,
                                                       "SortFolderNamesFirst");
                                app.nav_on = 1;
                            }
                            app.popup_sub = NULL;
                            app.popup_frame = tiku_trk_menu_place(app.popup,
                                ev.xbutton.x, ev.xbutton.y,
                                (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
                        } else if (ev.xbutton.button == 4 ||
                                   ev.xbutton.button == 5) {
                            tiku_trk_poseview_scroll_to(fl->view,
                                tiku_trk_poseview_scroll(fl->view) +
                                ((ev.xbutton.button == 4) ? -3 : 3));
                        } else {
                            tiku_trk_poseview_click(fl->view, row, mods);
                            app.press_at = now;
                            app.press_x = ev.xbutton.x;
                            app.press_y = ev.xbutton.y;
                            app.press_row = row;
                            app.press_fl = fl;
                            app.press_down = 1;
                            app.press_menu = 0;
                            /* Armed on a pose, started only if the pointer
                             * then moves far enough (PVS-032).  The whole
                             * selection travels, not the pose under the
                             * pointer (PVS-033). */
                            tiku_trk_drag_arm(&app.drag, ev.xbutton.x,
                                              ev.xbutton.y,
                                              ev.xbutton.button, mods,
                                              (row >= 0 && row == last_row &&
                                               now - last_click <
                                                   TIKU_TRK_CLICK_SPEED_US)
                                                  ? 2 : 1,
                                              w, fl->path);
                            if (row < 0 && ev.xbutton.button == 1u) {
                                /* Empty space: a sweep, not a click.  In
                                 * EVERY mode -- a list's rows are swept by
                                 * the same rectangle, and this shell had
                                 * no band at all, so the only way to
                                 * gather several was one modifier click at
                                 * a time (PVS-024). */
                                unsigned bm = 0u;

                                if (ev.xbutton.state & ShiftMask) {
                                    bm |= TIKU_TRK_MOD_SHIFT;
                                }
                                if (ev.xbutton.state & ControlMask) {
                                    bm |= TIKU_TRK_MOD_CMD;
                                }
                                if (bm == 0u) {
                                    /* Without a modifier the band starts
                                     * from nothing; with one it toggles on
                                     * top of what is there (PVS-027). */
                                    tiku_trk_poseview_select_none(fl->view);
                                }
                                tiku_trk_poseview_set_band(fl->view, 1);
                                app.band_fl = fl;
                                app.band_x0 = app.band_x1 = ev.xbutton.x;
                                app.band_y0 = app.band_y1 = ev.xbutton.y;
                                /* A sweep between two icons also ENDS any
                                 * half-finished double click: without this
                                 * the click that starts the band is
                                 * invisible to the counter, and the next
                                 * click on the icon just left completes a
                                 * double click the user did not make. */
                                last_row = -1;
                                last_click = 0;
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
                            if (row >= 0) {
                                /* Armed, not started: the rename begins only
                                 * once the double-click window passes with
                                 * no further click (PVS-014, MA-043). */
                                (void)tiku_trk_poseview_name_clicked(fl->view,
                                                                     row, now);
                            }
                            if (row >= 0 && row == last_row &&
                                now - last_click < 400000) {
                                tiku_trk_pose_t *p =
                                    tiku_trk_poseview_at(fl->view, row);
                                if (p != NULL &&
                                    tiku_trk_model_is_container(&p->model)) {
                                    /* A folder opens its OWN window, which
                                     * is what makes this spatial rather
                                     * than a single browser. */
                                    (void)open_folder(&app, p->model.path);
                                    if (ev.xbutton.state & Mod1Mask) {
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
                                    if ((ev.xbutton.state & Mod1Mask) &&
                                        went) {
                                        /* Option-open closes the window it
                                         * came from, once the launch has
                                         * actually happened (AW-032). */
                                        close_window(&app, w);
                                    }
                                }
                                last_row = -1;
                            } else {
                                last_row = row;
                                last_click = now;
                            }
                        }
                    }
                }
                app.dirty = 1;
                break;
            }
            case MotionNotify:
                if (app.press_down &&
                    (ev.xmotion.x - app.press_x > 4 ||
                     app.press_x - ev.xmotion.x > 4 ||
                     ev.xmotion.y - app.press_y > 4 ||
                     app.press_y - ev.xmotion.y > 4)) {
                    /* Moved out of the long-press radius: this is a drag,
                     * not a held click, so neither the menu nor the rename
                     * is owed (PVS-015, PVS-017). */
                    if (app.press_fl != NULL) {
                        tiku_trk_poseview_cancel_edit_wait(app.press_fl->view);
                    }
                    app.press_down = 0;
                }
                if (tiku_trk_drag_moved(&app.drag, ev.xmotion.x,
                                        ev.xmotion.y) ||
                    tiku_trk_drag_active(&app.drag)) {
                    /* Where the pointer is now decides the target, and it
                     * is resolved across WINDOWS: the whole point of a
                     * spatial desktop is dragging between them (FS-001). */
                    tiku_trk_win_t *ow = NULL;
                    folder_t *ofl = NULL;
                    int orow = -1;

                    (void)tiku_trk_desk_hit(app.desk, ev.xmotion.x,
                                            ev.xmotion.y, &ow);
                    ofl = (ow != NULL) ? folder_of(&app, ow) : &app.dfl;
                    if (ofl != NULL && ofl->view == NULL) {
                        ofl = NULL;
                    }
                    if (ofl != NULL) {
                        int r = folder_row_at(&app, ofl, ev.xmotion.x,
                                              ev.xmotion.y);
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
                                  ev.xmotion.x, ev.xmotion.y, &hteam);
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
                        ow != tiku_trk_desk_focused(app.desk)) {
                        /* Hovering the empty part of a window behind
                         * brings it forward, so a drag can reach what it
                         * covers without being dropped first (PVS-055). */
                        tiku_trk_desk_activate(app.desk, ow);
                        app.dirty = 1;
                    }
                }
                if (app.band_fl != NULL) {
                    tiku_desk_rect_t c = folder_content(&app, app.band_fl);
                    unsigned bm = 0u;
                    int ox = 0, oy = 0;

                    if (ev.xmotion.state & ShiftMask) {
                        bm |= TIKU_TRK_MOD_SHIFT;
                    }
                    if (ev.xmotion.state & ControlMask) {
                        bm |= TIKU_TRK_MOD_CMD;
                    }
                    app.band_x1 = ev.xmotion.x;
                    app.band_y1 = ev.xmotion.y;
                    /* Handed VIEW coordinates: the band is drawn where the
                     * pointer is, but what it sweeps is where the icons
                     * are, and those two differ by the scroll origin. */
                    tiku_trk_poseview_origin(app.band_fl->view, &ox, &oy);
                    (void)tiku_trk_poseview_select_rect(app.band_fl->view,
                        app.band_x0 - (c.x + 2) + ox,
                        app.band_y0 - (c.y + 2) + oy,
                        app.band_x1 - (c.x + 2) + ox,
                        app.band_y1 - (c.y + 2) + oy, bm);
                    app.dirty = 1;
                }
                if (app.dock_drag) {
                    /* Ctrl held on the bar: the pointer IS the picker.
                     * Where it lets go decides the dock, and a release in
                     * the middle keeps the one it started from (DB-049). */
                    tiku_trk_db_dock_t want =
                        tiku_trk_deskbar_dock_at(ev.xmotion.x, ev.xmotion.y,
                            DESK_W, DESK_H,
                            tiku_trk_deskbar_dock(app.deskbar));

                    if (want != tiku_trk_deskbar_dock(app.deskbar)) {
                        tiku_trk_deskbar_set_dock(app.deskbar, want);
                        app.dirty = 1;
                    }
                }
                if (tiku_trk_deskbar_pointer(app.deskbar, ev.xmotion.x,
                                             ev.xmotion.y)) {
                    /* The one input the bar never had.  Auto-hide and
                     * auto-raise are both answers to "where is the
                     * pointer", and neither could be asked (DB-051,
                     * DB-052). */
                    app.dirty = 1;
                }
                if (tiku_trk_desk_tracking(app.desk)) {
                    if (tiku_trk_desk_track(app.desk, ev.xmotion.x,
                                            ev.xmotion.y)) {
                        app.dirty = 1;
                    }
                }
                break;
            case ButtonRelease:
                app.dock_drag = 0;
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
                if (tiku_trk_drag_active(&app.drag)) {
                    do_drop(&app, ev.xbutton.x, ev.xbutton.y,
                            ev.xbutton.state, ev.xbutton.button);
                }
                tiku_trk_drag_reset(&app.drag);
                tiku_trk_deskbar_set_dragging(app.deskbar, 0);
                app.drop_fl = NULL;
                app.drop_row = -1;
                /* A long press already put the menu up, so its release must
                 * not put a second one up (PVS-017). */
                app.press_down = 0;
                app.press_menu = 0;
                tiku_trk_desk_end_track(app.desk);
                break;
            case KeyRelease:
                if (app.sw.shown && (ev_ks == XK_Control_L ||
                                     ev_ks == XK_Control_R)) {
                    /* Released inside the hair trigger the user meant the
                     * previous application; held longer they meant the
                     * list, and letting go now is the choice (DB-054). */
                    (void)tiku_trk_switcher_release(&app.sw,
                                                    tiku_trk_now());
                    switcher_close(&app, 1);
                    break;
                }
                /* fall through */
            case KeyPress: {
                /* A menu that is open rewrites itself as modifiers move:
                 * nine of its items change label AND command together while
                 * Shift is held.  The table already knows how; this is the
                 * edge that tells it to.  Handled on BOTH key edges, since
                 * releasing Shift has to put the labels back. */
                if (app.popup != NULL) {
                    KeySym mk = ev_ks;

                    if (mk == XK_Shift_L || mk == XK_Shift_R ||
                        mk == XK_Control_L || mk == XK_Control_R) {
                        tiku_trk_win_t *fw = tiku_trk_desk_focused(app.desk);
                        folder_t *ffl = folder_of(&app, fw);
                        unsigned m2 = 0;
                        tiku_trk_sc_ctx_t c2;

                        /* KeyRelease reports the state BEFORE the release,
                         * so the bit has to be taken out by hand or the menu
                         * stays in its shifted face. */
                        if (ev.xkey.state & ShiftMask) {
                            m2 |= TIKU_TRK_MOD_SHIFT;
                        }
                        if (ev.type == KeyRelease &&
                            (mk == XK_Shift_L || mk == XK_Shift_R)) {
                            m2 &= ~(unsigned)TIKU_TRK_MOD_SHIFT;
                        }
                        if (ev.type == KeyPress &&
                            (mk == XK_Shift_L || mk == XK_Shift_R)) {
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
                if (ev.type == KeyRelease) {
                    break;
                }
                {
                KeySym ks = ev_ks;
                tiku_trk_win_t *w = tiku_trk_desk_focused(app.desk);
                folder_t *fl = folder_of(&app, w);
                unsigned mods = 0;

                if (ev.xkey.state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
                if (ev.xkey.state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }
                /* While editing, the keyboard belongs to the field: the
                 * shortcuts underneath must not fire, or typing "w" in a
                 * value would close the window. */
                {   /* An info window's own keys, while it has focus. */
                    tiku_trk_win_t *iw = tiku_trk_desk_focused(app.desk);
                    int islot = -1, k;

                    for (k = 0; k < INFO_MAX; k++) {
                        if (app.info_win[k] != NULL &&
                            app.info_win[k] == iw && app.info_win[k]->open) {
                            islot = k;
                            break;
                        }
                    }
                    if (islot >= 0) {
                        if (app.info_renaming == islot) {
                            if (ks == XK_Return) {
                                info_rename_end(&app, 1);
                            } else if (ks == XK_Escape) {
                                info_rename_end(&app, 0);
                            } else if (ks == XK_BackSpace) {
                                (void)tiku_trk_edit_key(&app.info_name,
                                    TIKU_TRK_EDIT_BACKSPACE, 0, '\0');
                            } else {
                                char typed = key_char(&app, &ev.xkey, ks);

                                if (typed != '\0') {
                                    (void)tiku_trk_edit_key(&app.info_name,
                                        TIKU_TRK_EDIT_CHAR, 0, typed);
                                }
                            }
                            app.dirty = 1;
                            break;
                        }
                        if ((mods & TIKU_TRK_MOD_CMD) &&
                            (ks == 'e' || ks == 'E')) {
                            info_rename_begin(&app, islot);
                            break;
                        }
                        if ((mods & TIKU_TRK_MOD_CMD) &&
                            (ks == 'w' || ks == 'W')) {
                            info_close(&app, islot);
                            break;
                        }
                    }
                }
                if (app.fpanel_win != NULL && app.fpanel_win->open &&
                    tiku_trk_desk_focused(app.desk) == app.fpanel_win) {
                    tiku_trk_model_t psel[8];
                    int pn = fpanel_selection(&app, psel, 8);
                    int pkey = 0;
                    tiku_trk_panel_key_t what;

                    if (ks == XK_Escape)          { pkey = 27; }
                    else if (ks == XK_Return)     { pkey = '\n'; }
                    else if (ks == XK_Tab)        { pkey = '\t'; }
                    else if (ks == XK_Up)         { pkey = TIKU_TRK_KEY_UP; }
                    else if (ks == XK_Down)       { pkey = TIKU_TRK_KEY_DOWN; }
                    else if (ks >= 'a' && ks <= 'z') { pkey = (int)ks; }
                    what = tiku_trk_panel_key(&app.fpanel, pkey, mods,
                               app.fpanel.focus == TIKU_TRK_PANEL_FOCUS_LIST,
                               pn > 0);
                    switch (what) {
                    case TIKU_TRK_PANEL_KEY_CANCEL:
                        fpanel_close(&app);
                        break;
                    case TIKU_TRK_PANEL_KEY_DEFAULT:
                        fpanel_default(&app);
                        break;
                    case TIKU_TRK_PANEL_KEY_PARENT: {
                        char up[TIKU_TRK_PATH_MAX];

                        if (tiku_trk_panel_parent(&app.fpanel, up,
                                                  sizeof up)) {
                            fpanel_go(&app, up);
                        }
                        break;
                    }
                    case TIKU_TRK_PANEL_KEY_ENTER:
                        if (pn == 1 &&
                            tiku_trk_model_is_container(&psel[0])) {
                            fpanel_go(&app, psel[0].path);
                        }
                        break;
                    case TIKU_TRK_PANEL_KEY_HOME:
                        fpanel_go(&app, app.fpanel.home);
                        break;
                    case TIKU_TRK_PANEL_KEY_DESKTOP:
                        fpanel_go(&app, app.fpanel.desktop);
                        break;
                    case TIKU_TRK_PANEL_KEY_FOCUS:
                        /* Only where there is a second control to reach:
                         * an open panel has one list and nothing else, and
                         * a Tab that moved nowhere would look broken. */
                        if (app.fpanel.mode == TIKU_TRK_PANEL_SAVE) {
                            app.fpanel.focus =
                                (app.fpanel.focus ==
                                 TIKU_TRK_PANEL_FOCUS_LIST)
                                    ? TIKU_TRK_PANEL_FOCUS_NAME
                                    : TIKU_TRK_PANEL_FOCUS_LIST;
                        }
                        break;
                    default:
                        if (app.fpanel.focus == TIKU_TRK_PANEL_FOCUS_LIST &&
                            (ks == XK_Up || ks == XK_Down)) {
                            (void)tiku_trk_poseview_key_nav(app.fpanel_view,
                                (ks == XK_Up) ? TIKU_TRK_KEY_UP
                                              : TIKU_TRK_KEY_DOWN, mods);
                        }
                        break;
                    }
                    app.dirty = 1;
                    break;
                }
                if (app.panel_win != NULL && app.panel_win->open &&
                    tiku_trk_desk_focused(app.desk) == app.panel_win) {
                    if (ks == XK_Return) {
                        panel_search(&app);
                    } else if (ks == XK_Escape) {
                        close_window(&app, app.panel_win);
                    } else if (ks == XK_BackSpace) {
                        tiku_trk_find_backspace(&app.panel);
                    } else {
                        char typed = key_char(&app, &ev.xkey, ks);

                        if (typed != '\0') {
                            tiku_trk_find_key(&app.panel, typed);
                        }
                    }
                    app.dirty = 1;
                    break;
                }
                if (app.finding) {
                    if (ks == XK_Return) {
                        app.finding = 0;
                        run_find(&app, fl);
                    } else if (ks == XK_Escape) {
                        app.finding = 0;
                        app.status[0] = '\0';
                        app.dirty = 1;
                    } else if (ks == XK_BackSpace) {
                        if (app.find_len > 0) {
                            app.find_buf[--app.find_len] = '\0';
                        }
                        app.dirty = 1;
                    } else {
                        char typed = key_char(&app, &ev.xkey, ks);

                        if (typed >= 0x20 && typed < 0x7F &&
                            app.find_len < (int)sizeof app.find_buf - 1) {
                            app.find_buf[app.find_len++] = typed;
                            app.find_buf[app.find_len] = '\0';
                            app.dirty = 1;
                        }
                    }
                    break;
                }
                /* The Select window owns the keyboard while it has focus,
                 * and its expression field is an editor like any other. */
                if (app.sel_win != NULL && app.sel_win->open &&
                    tiku_trk_desk_focused(app.desk) == app.sel_win) {
                    tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
                    int shift = (ev.xkey.state & ShiftMask) ? 1 : 0;
                    char c = '\0';

                    switch (ks) {
                    case XK_Return:
                    case XK_KP_Enter:  apply_select(&app); app.dirty = 1;
                                       goto sel_done;
                    case XK_Escape:
                        /* Hidden, not destroyed: what is in it is what the
                         * user will want next time (AW-081). */
                        tiku_trk_select_hide(&app.sel);
                        close_window(&app, app.sel_win);
                        app.sel_win = NULL;
                        app.dirty = 1;
                        goto sel_done;
                    case XK_BackSpace: k = TIKU_TRK_EDIT_BACKSPACE; break;
                    case XK_Delete:    k = TIKU_TRK_EDIT_DELETE;    break;
                    case XK_Left:      k = TIKU_TRK_EDIT_LEFT;      break;
                    case XK_Right:     k = TIKU_TRK_EDIT_RIGHT;     break;
                    case XK_Home:      k = TIKU_TRK_EDIT_HOME;      break;
                    case XK_End:       k = TIKU_TRK_EDIT_END;       break;
                    case XK_Tab:
                        tiku_trk_select_set_rule(&app.sel,
                            (app.sel.rule + 1) % TIKU_TRK_SELECT_RULES);
                        app.dirty = 1;
                        goto sel_done;
                    default: break;
                    }
                    if (k == TIKU_TRK_EDIT_CHAR) {
                        c = key_char(&app, &ev.xkey, ks);
                        if (c == '\0') {
                            goto sel_done;
                        }
                    }
                    (void)tiku_trk_edit_key(&app.sel.expr, k, shift, c);
                    app.dirty = 1;
sel_done:
                    break;
                }
                if (app.renaming) {
                    tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
                    int shift = (ev.xkey.state & ShiftMask) ? 1 : 0;
                    char c = '\0';

                    switch (ks) {
                    case XK_Return:
                    case XK_KP_Enter:  k = TIKU_TRK_EDIT_COMMIT;    break;
                    case XK_Escape:    k = TIKU_TRK_EDIT_CANCEL;    break;
                    case XK_BackSpace: k = TIKU_TRK_EDIT_BACKSPACE; break;
                    case XK_Delete:    k = TIKU_TRK_EDIT_DELETE;    break;
                    case XK_Left:      k = TIKU_TRK_EDIT_LEFT;      break;
                    case XK_Right:     k = TIKU_TRK_EDIT_RIGHT;     break;
                    case XK_Home:      k = TIKU_TRK_EDIT_HOME;      break;
                    case XK_End:       k = TIKU_TRK_EDIT_END;       break;
                    default: break;
                    }
                    if (k == TIKU_TRK_EDIT_CHAR) {
                        c = key_char(&app, &ev.xkey, ks);
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
                    int shift = (ev.xkey.state & ShiftMask) ? 1 : 0;
                    char c = '\0';

                    switch (ks) {
                    case XK_Return:
                    case XK_KP_Enter:  k = TIKU_TRK_EDIT_COMMIT;    break;
                    case XK_Escape:    k = TIKU_TRK_EDIT_CANCEL;    break;
                    case XK_BackSpace: k = TIKU_TRK_EDIT_BACKSPACE; break;
                    case XK_Delete:    k = TIKU_TRK_EDIT_DELETE;    break;
                    case XK_Left:      k = TIKU_TRK_EDIT_LEFT;      break;
                    case XK_Right:     k = TIKU_TRK_EDIT_RIGHT;     break;
                    case XK_Home:      k = TIKU_TRK_EDIT_HOME;      break;
                    case XK_End:       k = TIKU_TRK_EDIT_END;       break;
                    default: break;
                    }
                    if (k == TIKU_TRK_EDIT_CHAR) {
                        c = key_char(&app, &ev.xkey, ks);
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
                if (ks == XK_q && (ev.xkey.state & ControlMask)) {
                    app.running = 0;
                    break;
                }
                if (ks == XK_f && (ev.xkey.state & ControlMask)) {
                    open_panel(&app);
                    break;
                }
                if (ks == XK_a && (ev.xkey.state & ControlMask) &&
                    (ev.xkey.state & ShiftMask)) {
                    /* PVK-047: Select by pattern, on the window it was
                     * invoked from, opened near the pointer. */
                    Window r1, c1;
                    int rx, ry, wx, wy;
                    unsigned msk;

                    if (app.dpy == NULL ||
                        !XQueryPointer(app.dpy, app.win, &r1, &c1, &rx, &ry,
                                       &wx, &wy, &msk)) {
                        wx = DESK_W / 2;
                        wy = DESK_H / 2;
                    }
                    open_select(&app, fl, wx, wy);
                    break;
                }
                if (ks == XK_a && (ev.xkey.state & ControlMask) &&
                    fl != NULL) {
                    tiku_trk_poseview_select_all(fl->view);
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_Delete && fl != NULL) {
                    /* What the key MEANS depends on where the items are: a
                     * move that can be undone where there is a Trash to
                     * move them to, and a deletion inside it or when Shift
                     * says the user has already decided (PVK-038).  This
                     * shell had no Delete key at all. */
                    char paths[64][TIKU_TRK_PATH_MAX];
                    const char *sel[64];
                    int n = folder_selection(&app, fl, paths, sel, 64);

                    if (n > 0) {
                        int shift = (ev.xkey.state & ShiftMask) ? 1 : 0;

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
                if (ks == XK_g && (ev.xkey.state & ControlMask) &&
                    fl != NULL) {
                    /* Cmd+G, which the menus now advertise (CW-033). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_EDIT_QUERY);
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_i && (ev.xkey.state & ControlMask) &&
                    fl != NULL) {
                    /* The chord the menus have always advertised; until
                     * now nothing was listening for it (AW-065). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_GET_INFO);
                    app.dirty = 1;
                    break;
                }
                if ((ev.xkey.state & ControlMask) && fl != NULL &&
                    (ks == XK_x || ks == XK_c || ks == XK_v)) {
                    /* The clipboard chords, which until now only the menu
                     * could reach here; Shift turns each into its "more"
                     * or "links" variant, as the menu labels do. */
                    unsigned m = (ev.xkey.state & ShiftMask)
                                     ? TIKU_TRK_MOD_SHIFT : 0u;
                    int base = (ks == XK_x) ? TIKU_TRK_CMD_CUT
                             : (ks == XK_c) ? TIKU_TRK_CMD_COPY
                                            : TIKU_TRK_CMD_PASTE;

                    menu_invoke(&app, fl, tiku_trk_sc_command(base, m));
                    app.dirty = 1;
                    break;
                }
                if ((ev.xkey.state & ControlMask) && fl != NULL &&
                    (ks == XK_1 || ks == XK_2 || ks == XK_3)) {
                    /* AW-025/CW-065: the same chords the single-window app
                     * binds.  The menu offers these modes; a shortcut that
                     * works in one window and not the other is worse than
                     * no shortcut at all. */
                    menu_invoke(&app, fl,
                        (ks == XK_1) ? TIKU_TRK_CMD_ICON_VIEW :
                        (ks == XK_2) ? TIKU_TRK_CMD_MINI_ICON_VIEW
                                     : TIKU_TRK_CMD_LIST_VIEW);
                    app.dirty = 1;
                    break;
                }
                if ((ev.xkey.state & ControlMask) && fl != NULL &&
                    (ks == XK_plus || ks == XK_equal || ks == XK_KP_Add ||
                     ks == XK_minus || ks == XK_KP_Subtract)) {
                    (void)tiku_trk_poseview_step_icon_size(fl->view,
                        (ks == XK_minus || ks == XK_KP_Subtract) ? 0 : 1);
                    tiku_trk_poseview_cleanup(fl->view,
                                              folder_content(&app, fl).w);
                    app.dirty = 1;
                    break;
                }
                if (app.alert.open) {
                    if (ks == XK_Escape && app.alert.cancel >= 0) {
                        app.alert.chosen = app.alert.cancel;
                        answer_alert(&app);
                    } else if (ks == XK_Return &&
                               app.alert.nbutton > 0) {
                        /* Return presses the default: the rightmost. */
                        app.alert.chosen = app.alert.nbutton - 1;
                        answer_alert(&app);
                    }
                    app.dirty = 1;
                    break;
                }
                if (app.sw.shown) {
                    /* Modal while it is up, and every key goes to it: the
                     * switcher IS the focus for as long as it shows. */
                    const char *nm = XKeysymToString(ks);
                    tiku_trk_sw_act_t act =
                        tiku_trk_switcher_key(&app.sw, (nm != NULL) ? nm : "",
                                              (ev.xkey.state & ShiftMask)
                                                  ? 1 : 0);

                    if (act == TIKU_TRK_SW_COMMIT) {
                        switcher_close(&app, 1);
                    } else if (act == TIKU_TRK_SW_CANCEL) {
                        switcher_close(&app, 0);
                    } else if (act == TIKU_TRK_SW_QUIT_APP ||
                               act == TIKU_TRK_SW_HIDE_APP) {
                        snprintf(app.status, sizeof app.status,
                                 "%s %.50s",
                                 (act == TIKU_TRK_SW_QUIT_APP) ? "quitting"
                                                               : "hiding",
                                 app.sw.app[app.sw.sel].name);
                    }
                    if (ks == XK_Tab) {
                        /* Tab while it is up moves on, which is the same
                         * gesture that opened it. */
                        switcher_open(&app,
                                      (ev.xkey.state & ShiftMask) ? 1 : 0);
                    }
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_Tab && (ev.xkey.state & ControlMask)) {
                    switcher_open(&app, (ev.xkey.state & ShiftMask) ? 1 : 0);
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_comma && (ev.xkey.state & ControlMask)) {
                    /* The settings are the application's, not a window's,
                     * so this works with no folder focused. */
                    prefs_open(&app);
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_n && (ev.xkey.state & ControlMask) &&
                    fl != NULL) {
                    menu_invoke(&app, fl, TIKU_TRK_CMD_NEW_FOLDER);
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_Menu && (ev.xkey.state &
                        (ControlMask | ShiftMask | Mod1Mask)) == 0) {
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
                if (ks == XK_s && (ev.xkey.state & ControlMask) &&
                    fl != NULL) {
                    /* PVK-046: invert -- the only way to say "everything
                     * except these" without clicking the rest. */
                    tiku_trk_poseview_invert_selection(fl->view);
                    app.dirty = 1;
                    break;
                }
                if (ks == XK_w && (ev.xkey.state & ControlMask)) {
                    close_window(&app, w);
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && ks == XK_Up &&
                    (ev.xkey.state & ControlMask)) {
                    /* PVK-040: Cmd+Up opens the parent. */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_OPEN_PARENT);
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL) {
                    /* Left and Right matter in an icon mode, where the
                     * arrangement is two-dimensional; the pages matter in
                     * both and were bound in neither shell's multi-window
                     * half (PVK-007, PVK-011). */
                    int nav = (ks == XK_Up)    ? TIKU_TRK_KEY_UP
                            : (ks == XK_Down)  ? TIKU_TRK_KEY_DOWN
                            : (ks == XK_Left)  ? TIKU_TRK_KEY_LEFT
                            : (ks == XK_Right) ? TIKU_TRK_KEY_RIGHT
                            : (ks == XK_Prior) ? TIKU_TRK_KEY_PGUP
                            : (ks == XK_Next)  ? TIKU_TRK_KEY_PGDN
                            : (ks == XK_Home)  ? TIKU_TRK_KEY_HOME
                            : (ks == XK_End)   ? TIKU_TRK_KEY_END : 0;
                    if (nav != 0) {
                        (void)tiku_trk_poseview_key_nav(fl->view, nav, mods);
                    } else if (ks == XK_Return) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_active(fl->view);
                        if (p != NULL &&
                            tiku_trk_model_is_container(&p->model)) {
                            /* Option+Return opens and takes the window it
                             * came from with it, the keyboard's version of
                             * an Option double-click (PVK-017). */
                            int chase = (ev.xkey.state & Mod1Mask) != 0;
                            tiku_trk_win_t *from = fl->win;

                            if (open_folder(&app, p->model.path) != NULL &&
                                chase) {
                                close_window(&app, from);
                            }
                        } else if (p != NULL) {
                            /* Opening a leaf means editing its value: there
                             * is no other window for it to open. */
                            begin_edit(&app, fl);
                        }
                    } else if (ks == XK_BackSpace) {
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
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == app.del) {
                    app.running = 0;
                }
            }
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
        {   /* One directory of one folder-size walk per turn, so a Get
             * Info window on a deep tree fills in while the desktop keeps
             * answering (AW-068). */
            for (i = 0; i < INFO_MAX; i++) {
                if (app.info[i].used &&
                    tiku_trk_info_calc_step(&app.info[i], app.backend)) {
                    app.dirty = 1;
                    break;
                }
            }
        }
        if (tiku_trk_thumb_step(&app.thumbs, app.store)) {
            /* One picture per turn, on the idle tick: the window keeps
             * answering while a folder of images fills in (IV-031). */
            app.dirty = 1;
        }
        if (app.band_fl != NULL) {
            /* A sweep scrolls at the edges too: a band that could only
             * reach what was already on screen would be a band that stops
             * at the window's edge (PVS-050). */
            tiku_desk_rect_t c = folder_content(&app, app.band_fl);
            int top = c.y + 2, bot = c.y + c.h - COUNT_H, over = 0;

            if (app.band_y1 < top + AUTOSCROLL_BAND) {
                over = -((top + AUTOSCROLL_BAND) - app.band_y1);
            } else if (app.band_y1 > bot - AUTOSCROLL_BAND) {
                over = app.band_y1 - (bot - AUTOSCROLL_BAND);
            }
            if (over != 0 &&
                tiku_trk_poseview_mode(app.band_fl->view) !=
                    TIKU_TRK_MODE_LIST) {
                int ox = 0, oy = 0;
                int step = (over > AUTOSCROLL_FAST) ? 3 : 1;

                tiku_trk_poseview_origin(app.band_fl->view, &ox, &oy);
                tiku_trk_poseview_set_origin(app.band_fl->view, ox,
                    oy + ((over < 0) ? -step : step) * app.row_h);
                app.dirty = 1;
            }
        }
        if (tiku_trk_drag_active(&app.drag) && app.drop_fl != NULL) {
            /* Auto-scroll.  The band is OUTSIDE the list, which is what
             * makes it reachable: a drag that has to leave the view to
             * scroll it can still see where it is going.  Held still for
             * the delay first, so crossing an edge on the way past does
             * not start it (PVS-050). */
            tiku_desk_rect_t c = folder_content(&app, app.drop_fl);
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
                int step = (over > AUTOSCROLL_FAST) ? 3 : 1;


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
            tiku_trk_win_t *fw = tiku_trk_desk_focused(app.desk);

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
            const char *open[TIKU_TRK_WIN_MAX];
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
                    tiku_trk_desk_close(app.desk, fl->win);
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
            if (app.dfl.view != NULL) {
                /* The Desktop is polled with the windows: a file dropped
                 * on it, or put in a Desktop folder by something else,
                 * appears without anyone reopening anything. */
                unsigned df = tiku_trk_poseview_poll(app.dfl.view,
                                  tiku_trk_now(), ch, nch, complete);
                int gone = -1;

                if (df != TIKU_TRK_NOTIFY_NOTHING) {
                    (void)tiku_trk_poseview_place_unplaced(app.dfl.view,
                                                       desk_area(&app).w);
                    /* Anything that arrived off the edge is pulled back:
                     * the Desktop cannot scroll to reach it (PVS-058). */
                    (void)desk_ensure_visible(&app);
                    changed++;
                }
                if (tiku_trk_vdir_poll(&app.dvdir, &app.dwatch, &gone) !=
                        TIKU_TRK_VDIR_CH_NONE) {
                    /* A contributing Desktop folder came, went or was
                     * edited: the union is a different set now (PVN-059). */
                    desk_icons_rebuild(&app);
                    changed++;
                }
            }
            if (app.fpanel_win != NULL && app.fpanel_win->open &&
                app.fpanel_view != NULL) {
                /* The panel's own view answers the two questions that
                 * matter to it, through the same handler a folder window
                 * uses: did the folder move, or did it go (AW-060)? */
                unsigned pf = tiku_trk_poseview_poll(app.fpanel_view,
                                  tiku_trk_now(), ch, nch, complete);

                if ((pf & TIKU_TRK_NOTIFY_GONE) != 0u) {
                    /* A folder WINDOW closes here; a panel falls back,
                     * because closing it would answer the question the
                     * user is in the middle of (PVN-029). */
                    tiku_trk_panel_dir_gone(&app.fpanel, app.backend);
                    fpanel_reload(&app, NULL);
                    changed++;
                } else if ((pf & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
                    (void)tiku_trk_panel_dir_moved(&app.fpanel,
                              app.fpanel.dir,
                              tiku_trk_poseview_path(app.fpanel_view));
                    changed++;
                } else if (pf != TIKU_TRK_NOTIFY_NOTHING) {
                    changed++;
                }
            }
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
                        if (strncmp(fl->path, vc[k].v.mount,
                                    strlen(vc[k].v.mount)) == 0) {
                            fl->gone = 1;
                            tiku_trk_desk_close(app.desk, fl->win);
                            changed++;
                        } else if (tiku_trk_poseview_device_gone(fl->view,
                                       (uint64_t)vc[k].v.dev) > 0) {
                            changed++;
                        }
                    }
                    if (app.fpanel_win != NULL && app.fpanel_win->open &&
                        strncmp(app.fpanel.dir, vc[k].v.mount,
                                strlen(vc[k].v.mount)) == 0) {
                        /* A picker jumps home rather than closing: it is
                         * in the middle of a question (PVN-043). */
                        tiku_trk_panel_dir_gone(&app.fpanel, app.backend);
                        fpanel_reload(&app, NULL);
                        changed++;
                    }
                    for (i = 0; i < INFO_MAX; i++) {
                        if (app.info[i].used &&
                            app.info[i].model.facts.dev ==
                                (uint64_t)vc[k].v.dev) {
                            info_close(&app, i);
                            changed++;
                        }
                    }
                }
            }
            for (i = 0; i < INFO_MAX; i++) {
                if (!app.info[i].used) {
                    continue;
                }
                if (tiku_trk_info_refresh(&app.info[i], app.backend,
                                          tiku_trk_now())) {
                    changed++;
                }
                {
                    tiku_trk_model_t im;

                    if (app.backend->ops->stat(app.backend,
                            app.info[i].path, &im) != 0) {
                        /* Gone from where it was.  A rename is the entry
                         * still being there under another name, which is
                         * asked by IDENTITY; anything else is a deletion
                         * and the window has nothing left to describe
                         * (AW-077). */
                        if (tiku_trk_info_refind(&app.info[i],
                                                 app.backend)) {
                            tiku_trk_info_title(&app.info[i],
                                app.info_win[i]->title,
                                sizeof app.info_win[i]->title);
                        } else {
                            info_close(&app, i);
                        }
                        changed++;
                    }
                }
            }
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
            !app.prog.row[0].paused) {
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
                if (!app.alert.open) {
                    tiku_trk_alert_for_conflict(&app.alert, pr);
                }
            } else if (pr->error_ask) {
                if (!app.alert.open) {
                    tiku_trk_alert_for_error(&app.alert, pr);
                }
            } else {
                snprintf(app.status, sizeof app.status, "%d of %d%s%s",
                         pr->done + pr->skipped, pr->total,
                         pr->skipped ? " (some skipped)" : "",
                         pr->failed ? " -- some failed" : "");
            }
            if (tiku_trk_progress_wanted(pr->op, pr->total)) {
                tiku_trk_progress_update(&app.prog, 0, pr, 1, NULL);
                tiku_trk_progress_sample(&app.prog, 0, pr->bytes,
                                         tiku_trk_now());
            }
            app.dirty = 1;
            if (app.prog_opened == 0) {
                app.prog_opened = tiku_trk_now();
                /* Whoever was in front when the job began gets the focus
                 * back when it ends: an operation is not a reason to be
                 * left looking at something else (FS-073). */
                app.prog_was_active = tiku_trk_desk_focused(app.desk);
            }
        } else if (app.prog.open) {
            tiku_trk_progress_update(&app.prog, 0, NULL, 0, NULL);
            app.prog_opened = 0;
            if (app.prog_was_active != NULL &&
                app.prog_was_active->open) {
                tiku_trk_desk_activate(app.desk, app.prog_was_active);
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
        char open_now[TIKU_TRK_SESSION_MAX][TIKU_TRK_PATH_MAX];
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
            snprintf(open_now[nsess++], TIKU_TRK_PATH_MAX, "%s", fl->path);
        }
        (void)tiku_trk_session_save(app.store, open_now, nsess);
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
    if (app.dfl.view != NULL) {
        /* The Desktop has no window to close, so nothing else would ever
         * write its arrangement back. */
        (void)tiku_trk_poseview_save_state(app.dfl.view);
        tiku_trk_poseview_free(app.dfl.view);
        app.dfl.view = NULL;
    }
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
    tiku_trk_deskbar_free(app.deskbar);
    tiku_trk_desk_free(app.desk);
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
    if (app.dpy != NULL) {
        XDestroyImage(app.img);
        XCloseDisplay(app.dpy);
    }
    tiku_desk_surface_free(app.surf);
    return rc;
}
