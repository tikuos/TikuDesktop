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
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_trk_attr.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
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
    int                  press_down;
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
    }
    return c;
}

/** @brief Draw a folder window's contents: list rows, then the count. */
static void
folder_draw(tiku_trk_win_t *w, tiku_desk_surface_t *s,
            tiku_desk_rect_t content, void *ctx)
{
    folder_t *fl = ctx;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    desktop_t *app = fl->app;
    tiku_desk_rect_t list = content;
    int row_h = app->row_h;
    int rows, scroll, total, i;

    (void)w;
    list.h -= COUNT_H;
    tiku_desk_ui_sunken(s, list, TIKU_DESK_C_DOC);

    rows = (list.h - 4) / row_h;
    /* The view cannot see its own window; the rules stated in terms of what
     * is VISIBLE need this. */
    tiku_trk_poseview_set_visible_rows(fl->view, rows);
    scroll = tiku_trk_poseview_scroll(fl->view);
    total = tiku_trk_poseview_count(fl->view);

    tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
    for (i = 0; i < rows && scroll + i < total; i++) {
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
        tiku_desk_fill(s, row, drawn_sel ? TIKU_DESK_C_SELECT
                                         : TIKU_DESK_C_DOC);
        {   /* A mini icon at the head of the name, as list mode has. */
            int mini = row_h - 2;
            int tx = row.x + 4;

            if (mini > MINI_ICON) {
                mini = MINI_ICON;
            }
            /* A link with nowhere to go is drawn faded: the art is the same
             * as a live link's, so the state has to show some other way. */
            if (((p->model.facts.link_broken || cut)
                     ? tiku_trk_icon_paint_dim(s,
                           tiku_trk_model_icon_name(&p->model), row.x + 2,
                           row.y + (row_h - mini) / 2, mini, 0.55f,
                           TIKU_DESK_C_DOC)
                     : drawn_sel
                     /* PVL-082: selection reads on the icon as well as the
                      * label, so a row whose label is scrolled away still
                      * shows it. */
                     ? tiku_trk_icon_paint_selected(s,
                           tiku_trk_model_icon_name(&p->model), row.x + 2,
                           row.y + (row_h - mini) / 2, mini)
                     : tiku_trk_icon_paint(s,
                           tiku_trk_model_icon_name(&p->model), row.x + 2,
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
    if (app->renaming && app->ren_fl == fl) {
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
    snprintf(title, sizeof title, "find #%d", ++app->find_serial);
    {
        folder_t *fl = open_results(app, title, run);

        if (fl != NULL) {
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
        tiku_trk_poseview_set_icon_size(fl->view, 32);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_48:
        tiku_trk_poseview_set_icon_size(fl->view, 48);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_40:
        tiku_trk_poseview_set_icon_size(fl->view, 40);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_64:
        tiku_trk_poseview_set_icon_size(fl->view, 64);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_96:
        tiku_trk_poseview_set_icon_size(fl->view, 96);
        break;
    case TIKU_TRK_CMD_ICON_SIZE_128:
        tiku_trk_poseview_set_icon_size(fl->view, 128);
        break;
    case TIKU_TRK_CMD_SORT_BY_NAME:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_NAME);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_win_content(fl->win).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_KIND:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_KIND);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_win_content(fl->win).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_SIZE:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_SIZE);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_win_content(fl->win).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_VALUE:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_VALUE);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_win_content(fl->win).w);
        break;
    case TIKU_TRK_CMD_SORT_BY_CAP:
        tiku_trk_poseview_sort_by(fl->view, TIKU_TRK_COL_CAP);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_win_content(fl->win).w);
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
                                  tiku_trk_win_content(fl->win).w);
        break;
    case TIKU_TRK_CMD_CLEANUP:
    case TIKU_TRK_CMD_CLEANUP_ALL:
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_win_content(fl->win).w);
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
                            snprintf(app->status, sizeof app->status,
                                     "'%.60s' is a system folder -- hold "
                                     "Shift to confirm",
                                     sel[j]);
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
    fl->app = app;
    fl->view = tiku_trk_poseview_new(app->backend, path);
    if (fl->view == NULL) {
        return NULL;
    }
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
    tiku_desk_rect_t c = tiku_trk_win_content(fl->win);
    int rel;

    if (x < c.x || x > c.x + c.w || y < c.y + 2 ||
        y > c.y + c.h - COUNT_H) {
        return -1;
    }
    rel = (y - c.y - 2) / app->row_h;
    rel += tiku_trk_poseview_scroll(fl->view);
    return (rel < tiku_trk_poseview_count(fl->view)) ? rel : -1;
}

static void
blit(desktop_t *app)
{
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

static void
draw_all(desktop_t *app)
{
    publish_windows(app);
    tiku_trk_desk_draw(app->desk);
    tiku_trk_deskbar_draw(app->deskbar, app->surf);
    /* A menu is drawn last because it is above everything, including the
     * Deskbar strip. */
    if (app->popup != NULL) {
        tiku_trk_menu_draw(app->popup, app->surf, app->popup_frame);
        if (app->popup_sub != NULL) {
            tiku_trk_menu_draw(app->popup_sub, app->surf,
                               app->popup_sub_frame);
        }
    }
    app->dirty = 0;
}

int
main(int argc, char **argv)
{
    desktop_t app;
    const char *port = NULL, *shot = NULL;
    int baud = 115200, use_device = 0, i;
    int64_t shot_at = 0, deadline = 0, last_pump = 0, last_requery = 0;
    int shot_menu = 0;
    const char *shot_edit = NULL, *shot_find = NULL;
    int shot_panel = 0, shot_live = 0, shot_shift = 0, shot_zoom = 0;
    int shot_cascade = 0, shot_notify = 0, shot_select = 0;
    int64_t last_click = 0;
    int last_row = -1;
    const char *openme[TIKU_TRK_WIN_MAX];
    int nopen = 0;

    memset(&app, 0, sizeof app);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0)                      { use_device = 1; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { port = argv[++i]; use_device = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { shot = argv[++i]; }
        else if (strcmp(argv[i], "-shotmenu") == 0) { shot_menu = 1; }
        else if (strcmp(argv[i], "-shotedit") == 0 && i + 1 < argc) {
            shot_edit = argv[++i];
        }
        else if (strcmp(argv[i], "-shotfind") == 0 && i + 1 < argc) {
            shot_find = argv[++i];
        }
        else if (strcmp(argv[i], "-shotpanel") == 0) { shot_panel = 1; }
        else if (strcmp(argv[i], "-shotlive") == 0) { shot_live = 1; }
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
    tiku_trk_cwmenu_init(&app.menus);
    tiku_trk_find_init(&app.panel);
    /* The Select window's defaults are wildcard and ignore-case, and a
     * zeroed struct is neither -- it is "starts with", case-sensitive. */
    tiku_trk_select_init(&app.sel);
    tiku_trk_clip_init(&app.clip);
    app.store = tiku_trk_store_memory_open();

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
        const char *home = getenv("HOME");
        (void)open_folder(&app, home ? home : "/");
    }

    app.dpy = XOpenDisplay(NULL);
    if (app.dpy == NULL) {
        fprintf(stderr, "tracker: no display\n");
        return 1;
    }
    {
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
        app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app.popup_frame = tiku_trk_menu_place(app.popup, 250, 160,
                              (tiku_desk_rect_t){ 0, 0, DESK_W, DESK_H });
    }
    app.running = 1;
    app.dirty = 1;

    while (app.running) {
        while (XPending(app.dpy) > 0) {
            XEvent ev;
            XNextEvent(app.dpy, &ev);
            switch (ev.type) {
            case Expose:
                app.dirty = 1;
                break;
            case ButtonPress: {
                tiku_trk_win_t *w = NULL;
                tiku_trk_hit_t hit;
                tiku_trk_db_pick_t pick;

                /* A menu that is showing eats the next click wherever it
                 * lands: choosing an item, or dismissing. */
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
                hit = tiku_trk_desk_hit(app.desk, ev.xbutton.x, ev.xbutton.y,
                                        &w);
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
                } else if (hit == TIKU_TRK_HIT_CONTENT &&
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
                } else if (hit == TIKU_TRK_HIT_CONTENT &&
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
                    /* The panel grows and shrinks with its rows. */
                    w->frame.h = tiku_trk_find_height(&app.panel) +
                                 TIKU_TRK_WIN_TAB_H + 2 * TIKU_TRK_WIN_BORDER;
                    app.dirty = 1;
                } else if (hit == TIKU_TRK_HIT_CONTENT) {
                    folder_t *fl = folder_of(&app, w);
                    if (fl != NULL) {
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
                            app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (row >= 0 && p != NULL)
                                                ? &p->model : NULL, &c);
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
                if (tiku_trk_desk_tracking(app.desk)) {
                    if (tiku_trk_desk_track(app.desk, ev.xmotion.x,
                                            ev.xmotion.y)) {
                        app.dirty = 1;
                    }
                }
                break;
            case ButtonRelease:
                /* A long press already put the menu up, so its release must
                 * not put a second one up (PVS-017). */
                app.press_down = 0;
                app.press_menu = 0;
                tiku_trk_desk_end_track(app.desk);
                break;
            case KeyRelease:
            case KeyPress: {
                /* A menu that is open rewrites itself as modifiers move:
                 * nine of its items change label AND command together while
                 * Shift is held.  The table already knows how; this is the
                 * edge that tells it to.  Handled on BOTH key edges, since
                 * releasing Shift has to put the labels back. */
                if (app.popup != NULL) {
                    KeySym mk = XLookupKeysym(&ev.xkey, 0);

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
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                tiku_trk_win_t *w = tiku_trk_desk_focused(app.desk);
                folder_t *fl = folder_of(&app, w);
                unsigned mods = 0;

                if (ev.xkey.state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
                if (ev.xkey.state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }
                /* While editing, the keyboard belongs to the field: the
                 * shortcuts underneath must not fire, or typing "w" in a
                 * value would close the window. */
                if (app.panel_win != NULL && app.panel_win->open &&
                    tiku_trk_desk_focused(app.desk) == app.panel_win) {
                    if (ks == XK_Return) {
                        panel_search(&app);
                    } else if (ks == XK_Escape) {
                        close_window(&app, app.panel_win);
                    } else if (ks == XK_BackSpace) {
                        tiku_trk_find_backspace(&app.panel);
                    } else {
                        char ch[8];
                        int n = XLookupString(&ev.xkey, ch, sizeof ch, NULL,
                                              NULL);
                        if (n == 1) {
                            tiku_trk_find_key(&app.panel, ch[0]);
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
                        char ch[8];
                        int n = XLookupString(&ev.xkey, ch, sizeof ch, NULL,
                                              NULL);
                        if (n == 1 && ch[0] >= 0x20 && ch[0] < 0x7F &&
                            app.find_len < (int)sizeof app.find_buf - 1) {
                            app.find_buf[app.find_len++] = ch[0];
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
                    char ch[8];
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
                        if (XLookupString(&ev.xkey, ch, sizeof ch, NULL,
                                          NULL) != 1) {
                            goto sel_done;
                        }
                        c = ch[0];
                    }
                    (void)tiku_trk_edit_key(&app.sel.expr, k, shift, c);
                    app.dirty = 1;
sel_done:
                    break;
                }
                if (app.renaming) {
                    tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
                    char ch[8];
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
                        if (XLookupString(&ev.xkey, ch, sizeof ch, NULL,
                                          NULL) != 1) {
                            break;
                        }
                        c = ch[0];
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
                    char ch[8];
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
                        if (XLookupString(&ev.xkey, ch, sizeof ch, NULL,
                                          NULL) != 1) {
                            break;
                        }
                        c = ch[0];
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

                    if (!XQueryPointer(app.dpy, app.win, &r1, &c1, &rx, &ry,
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
                                              tiku_trk_win_content(fl->win).w);
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
                    int nav = (ks == XK_Up)    ? TIKU_TRK_KEY_UP
                            : (ks == XK_Down)  ? TIKU_TRK_KEY_DOWN
                            : (ks == XK_Home)  ? TIKU_TRK_KEY_HOME
                            : (ks == XK_End)   ? TIKU_TRK_KEY_END : 0;
                    if (nav != 0) {
                        (void)tiku_trk_poseview_key_nav(fl->view, nav, mods);
                    } else if (ks == XK_Return) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_active(fl->view);
                        if (p != NULL &&
                            tiku_trk_model_is_container(&p->model)) {
                            (void)open_folder(&app, p->model.path);
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
        if (tiku_trk_now() - last_pump > 400000) {
            int changed = 0;
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
        if (deadline > 0 && tiku_trk_now() > deadline) {
            app.running = 0;
        }
        usleep(10000);
    }

    for (i = 0; i < app.folder_count; i++) {
        /* A folder that was deleted under us keeps no arrangement: writing
         * one at exit would resurrect a position for something that is not
         * there, and undo the deliberate skip taken when it went. */
        if (!app.folders[i].gone) {
            (void)tiku_trk_poseview_save_state(app.folders[i].view);
        }
        tiku_trk_poseview_free(app.folders[i].view);
    }
    tiku_trk_deskbar_free(app.deskbar);
    tiku_trk_desk_free(app.desk);
    tiku_trk_fs_free(app.fs);
    tiku_trk_backend_close(app.backend);
    XDestroyImage(app.img);
    XCloseDisplay(app.dpy);
    tiku_desk_surface_free(app.surf);
    return 0;
}
