/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app.c - the window: a real Tracker list view over a real backend.
 *
 * One X11 window drawn entirely by us, wired to the pose engine: the title bar
 * sorts, rows select with Tracker's own rules, the keyboard navigates, and a
 * device's values update live because the loop pumps the backend between
 * events instead of blocking on X.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
#include "tiku_trk_edit.h"
#include "tiku_model.h"
#include "tiku_fs.h"
#include "tiku_iconpaint.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_trk_navigator.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_scroll.h"
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
    tiku_trk_edit_t      ed;
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
    tiku_trk_scroll_t    sb;
    tiku_trk_scroll_t    hsb;       /* the horizontal bar (TS-044)        */
    tiku_rect_t     hsb_rect;
    tiku_rect_t     sb_rect;
    /* The same container-window menus the desktop shell presents. */
    tiku_trk_cwmenu_t    menus;
    tiku_trk_menu_t     *popup;
    tiku_trk_menu_t     *popup_sub;
    tiku_rect_t     popup_frame;
    tiku_rect_t     popup_sub_frame;
    tiku_rect_t     viewbtn_rect;  /* the gutter's view popup    */
    int                  theme_dark;    /* -1 until first applied     */
    int                  bar_open;
    /* Browser chrome owns its history and location editor per window. */
    tiku_trk_navigator_t nav;
    tiku_trk_edit_t      nav_edit;
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

static void menu_invoke(app_t *a, int command);


/** @brief Real art for menu items, dimmed for a disabled one (IV-054). */
static int
paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    return dimmed
        ? tiku_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_C_PANEL)
        : tiku_icon_paint(s, icon, x, y, size);
}

/** @brief Hand a modal filesystem answer back to the operation engine. */
static void
app_alert_answer(void *context, const tiku_trk_alert_t *alert)
{
    app_t *a = context;

    if (alert->tag == TIKU_TRK_ALERT_TAG_CONFLICT) {
        if (alert->chosen == alert->cancel) {
            tiku_fs_cancel(a->fs);
        } else {
            tiku_fs_resolve(a->fs,
                                tiku_trk_alert_conflict_answer(alert));
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_ERROR) {
        tiku_fs_resolve_error(a->fs, alert->chosen != alert->cancel);
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_EMPTY_TRASH) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_folder_file_actions_answer_empty(&a->file_actions);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_DELETE) {
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
        /* Cancel, Move to Trash, Delete, in the order the buttons are
         * offered (FS-046).  The folder wrapper is refreshed here: the
         * question was modal, but the answer must still act on the view
         * the window is showing NOW. */
        a->folder.view = a->view;
        snprintf(a->folder.path, sizeof a->folder.path, "%s",
                 tiku_trk_poseview_path(a->view));
        tiku_trk_folder_file_actions_answer_delete(&a->file_actions,
            &a->folder, by_button[chosen]);
        }
    }
    a->dirty = 1;
}

/** @brief Stop asks the operation engine to cancel at its safe boundary. */
static void
app_alert_stop(void *context)
{
    app_t *a = context;

    tiku_fs_cancel(a->fs);
}

/** @brief Index of the first selected row, or -1. */
static int
first_selected_index(app_t *a)
{
    int i, n = tiku_trk_poseview_count(a->view);

    for (i = 0; i < n; i++) {
        const tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);

        if (p != NULL && p->selected) {
            return i;
        }
    }
    return -1;
}

static int
pose_index(app_t *a, const tiku_trk_pose_t *wanted)
{
    int i, n = tiku_trk_poseview_count(a->view);

    for (i = 0; i < n; i++) {
        if (tiku_trk_poseview_at(a->view, i) == wanted) {
            return i;
        }
    }
    return -1;
}

static int
next_edit_row(app_t *a, int row, int backwards)
{
    int n = tiku_trk_poseview_count(a->view);
    int step = backwards ? -1 : 1;
    int i;

    for (i = 1; i <= n; i++) {
        int at = (row + step * i) % n;
        tiku_trk_pose_t *p;

        if (at < 0) { at += n; }
        p = tiku_trk_poseview_at(a->view, at);
        if (p != NULL && p->model.kind != TIKU_KIND_DEVICE_NODE &&
            p->model.kind != TIKU_KIND_VIRTUAL_DIR) {
            return at;
        }
    }
    return -1;
}

/** @brief Index of the row whose model sits at @p path, or -1. */
static int
index_of_path(app_t *a, const char *path)
{
    int i, n = tiku_trk_poseview_count(a->view);

    for (i = 0; i < n; i++) {
        const tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);

        if (p != NULL && strcmp(p->model.path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief The first @p n bytes' width in the UI face (MA-049). */
static int
measure_prefix_px(const char *text, int n, void *ctx)
{
    char tmp[TIKU_TRK_EDIT_MAX];

    (void)ctx;
    if (n < 0) { n = 0; }
    if (n >= (int)sizeof tmp) { n = (int)sizeof tmp - 1; }
    memcpy(tmp, text, (size_t)n);
    tmp[n] = '\0';
    return tiku_text_width(tiku_font_plain(), tmp);
}

static void blit(app_t *a);
static tiku_rect_t menubar_rect(const app_t *a);

/** @brief The navigator takes no layout space while globally hidden. */
static int
navigator_height(const app_t *a)
{
    return a->show_nav ? NAV_H : 0;
}

/** @brief Geometry of the row area, derived once per draw. */
static tiku_rect_t
list_rect(const app_t *a)
{
    tiku_rect_t r;

    int titled = (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST);

    r.x = BORDER;
    /* The column title bar belongs to list mode; icon mode gets the space
     * back, and hit-testing must use the same rectangle drawing does. */
    r.y = TAB_H + BORDER + a->menus.bar.height + navigator_height(a) +
          (titled ? TITLE_H : 0);
    r.w = a->surf->w - 2 * BORDER - (titled ? SCROLL_W : 0);
    r.h = a->surf->h - r.y - BORDER - COUNT_H;
    return r;
}

/** @brief Browser controls directly below the menu bar. */
static tiku_rect_t
navigator_rect(const app_t *a)
{
    tiku_rect_t bar = menubar_rect(a);
    tiku_rect_t r = { BORDER, bar.y + bar.h,
                           a->surf->w - 2 * BORDER,
                           navigator_height(a) };

    return r;
}

/** @brief Split the navigator into its three buttons and location field. */
static void
navigator_layout(const app_t *a, tiku_rect_t *back,
                 tiku_rect_t *forward, tiku_rect_t *up,
                 tiku_rect_t *field)
{
    tiku_rect_t r = navigator_rect(a);

    *back = (tiku_rect_t){ r.x + 2, r.y + 2, 30, r.h - 4 };
    *forward = (tiku_rect_t){ back->x + back->w + 2, r.y + 2,
                                   30, r.h - 4 };
    *up = (tiku_rect_t){ forward->x + forward->w + 2, r.y + 2,
                              34, r.h - 4 };
    *field = (tiku_rect_t){ up->x + up->w + 4, r.y + 2,
                                 r.x + r.w - (up->x + up->w + 6),
                                 r.h - 4 };
}

/** @brief The menu bar inside this window's content area. */
static tiku_rect_t
menubar_rect(const app_t *a)
{
    tiku_rect_t r = { BORDER, TAB_H + BORDER,
                           a->surf->w - 2 * BORDER,
                           a->menus.bar.height };

    return r;
}

static void
draw_folder_icon(app_t *a, tiku_rect_t bar)
{
    tiku_model_t model;
    tiku_rect_t icon;
    int size = bar.h - 4;

    if (a->backend == NULL || a->backend->ops == NULL ||
        a->backend->ops->stat(a->backend,
            tiku_trk_poseview_path(a->view), &model) != 0) {
        return;
    }
    if (size > 24) { size = 24; }
    if (size < 12) { size = 12; }
    icon = (tiku_rect_t){ bar.x + bar.w - size - 3, bar.y + 2,
                               size, size };
    (void)tiku_icon_paint(a->surf, tiku_model_icon_name(&model),
                              icon.x, icon.y, icon.w);
}

/** @brief Current facts used by every menu enablement rule. */
static tiku_trk_sc_ctx_t
menu_ctx(app_t *a, unsigned mods)
{
    static const int fields[] = { TIKU_TRK_COL_NAME, TIKU_TRK_COL_KIND,
                                  TIKU_TRK_COL_VALUE, TIKU_TRK_COL_SIZE,
                                  TIKU_TRK_COL_CAP, TIKU_TRK_COL_DATE,
                                  TIKU_TRK_COL_PATH, TIKU_TRK_COL_PERM,
                                  TIKU_TRK_COL_ORIGINAL };
    tiku_trk_sc_ctx_t c;
    tiku_trk_pose_t *p;
    size_t i;

    memset(&c, 0, sizeof c);
    c.mods = mods;
    c.selection = tiku_trk_poseview_selection_count(a->view);
    c.view_mode = tiku_trk_poseview_mode(a->view);
    c.icon_size = tiku_trk_poseview_icon_size(a->view);
    c.sort_field = tiku_trk_poseview_sort_field(a->view);
    c.reverse_sort = tiku_trk_poseview_sort_reverse(a->view);
    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        if (tiku_trk_poseview_has_column(a->view, fields[i])) {
            c.columns |= 1u << fields[i];
        }
    }
    p = tiku_trk_poseview_active(a->view);
    c.can_edit_name = (c.selection == 1);
    /* Without these every Paste stays grey: the enable rules can only see
     * what the context carries (CW-042). */
    c.clipboard_has_refs = tiku_trk_clip_count(&a->clip) > 0;
    c.clipboard_has_layout = tiku_trk_poseview_has_layout_clipboard();
    c.can_open_parent = strcmp(tiku_trk_poseview_path(a->view), "/") != 0;
    c.target_ro = a->read_only;
    c.is_root = !c.can_open_parent;
    c.is_query = tiku_trk_poseview_is_query(a->view);
    {
        char trash[TIKU_PATH_MAX];

        if (a->fs != NULL && tiku_fs_trash_dir(
                a->fs, tiku_trk_poseview_path(a->view), trash,
                sizeof trash) == 0) {
            tiku_model_t tm;

            c.is_trash = strcmp(tiku_trk_poseview_path(a->view), trash) == 0;
            c.in_trash = !c.is_trash && tiku_fs_in_trash(
                a->fs, tiku_trk_poseview_path(a->view));
            if (a->backend->ops->stat(a->backend, trash, &tm) == 0) {
                c.trash_full = tm.facts.trash_full;
            }
        }
    }
    if (p != NULL) {
        c.is_virtual = (p->model.kind == TIKU_KIND_VIRTUAL_DIR);
        c.selection_is_volume = p->model.kind == TIKU_KIND_VOLUME ||
                                p->model.kind == TIKU_KIND_ROOT;
        if (c.selection_is_volume) {
            /* Scanned only when a volume is the selection: menus are
             * rebuilt per draw, and the mount table is a file. */
            tiku_volumes_t vols;
            const tiku_volume_t *vol;

            if (tiku_volumes_scan(&vols) > 0 &&
                (vol = tiku_volume_of(&vols, p->model.path)) != NULL) {
                c.selection_is_boot = vol->is_boot;
            }
        }
        c.selection_ro = !tiku_model_is_writable(&p->model);
    }
    c.can_trash_or_dup = c.selection > 0 && !c.selection_ro;
    return c;
}

static int
visible_rows(const app_t *a)
{
    tiku_rect_t r = list_rect(a);

    return (a->row_h > 0) ? (r.h - 4) / a->row_h : 0;
}

/** @brief Width of @p s in the font @p ctx, for the text fitter. */
static int
measure_text(const char *s, void *ctx)
{
    return tiku_text_width(ctx, s);
}

/** @brief Where a column's data starts on screen, allowing for the scroll. */
static int
col_x(const app_t *a, const tiku_trk_column_t *c)
{
    return list_rect(a).x + c->offset - tiku_trk_poseview_hscroll(a->view);
}

/**
 * @brief One column title (TS-017, TS-018).
 *
 * The rectangle is the column's, widened by both margins: the left margin is
 * where the sort mark lives, which is why the text starts one pixel inside
 * the DATA rather than inside the rectangle.
 */
static void
draw_title(app_t *a, tiku_rect_t strip, const tiku_trk_column_t *c,
           int pressed, int drag_dx)
{
    tiku_surface_t *s = a->surf;
    /* Three-quarters of the body text, the source's own strip proportion
     * (TS-019): the titles are labels ABOUT the rows, not rows. */
    const tiku_font_t *f =
        tiku_font_at(tiku_font_size() * 3 / 4);
    tiku_rect_t r;
    char fit[64];
    int tw, tx, data_x = col_x(a, c) + drag_dx;

    r.x = data_x - TIKU_TRK_COL_LEFT_MARGIN;
    r.y = strip.y;
    r.w = c->width + TIKU_TRK_COL_MARGIN;
    r.h = strip.h;
    tiku_fill(s, r, pressed ? tiku_tint(TIKU_C_PANEL, 1.15f)
                                 : TIKU_C_PANEL);
    tiku_bevel(s, r, tiku_tint(TIKU_C_PANEL, 0.0f),
                    tiku_tint(TIKU_C_PANEL, 1.06f));
    /* Truncated against the column's own width -- the margins are not
     * text space -- and losing its END, because the head of a column name
     * is what identifies it. */
    (void)tiku_text_fit(c->title, c->width, TIKU_TRUNC_END,
                            measure_text, (void *)f, fit, sizeof fit);
    tw = tiku_text_width(f, fit);
    switch (c->alignment) {
    case TIKU_TRK_ALIGN_RIGHT:
        tx = data_x + c->width - tw;
        break;
    case TIKU_TRK_ALIGN_CENTER:
        /* Centred in the whole rectangle, both margins included, and on the
         * width AFTER truncation. */
        tx = r.x + (r.w - tw) / 2;
        break;
    default:
        tx = data_x + 1;
        break;
    }
    tiku_text(s, f, tx, r.y + (r.h - f->height) / 2 + f->ascent, fit,
                   pressed ? tiku_tint(TIKU_C_TEXT, 1.20f)
                           : TIKU_C_TEXT);
    if (pressed && drag_dx != 0) {
        /* A dragged title is outlined where it will LAND, which is the only
         * thing on screen that says the drag is going to move a column
         * rather than sort it. */
        tiku_frame(s, r, TIKU_C_FOCUS);
    }
    {
        /* The sort mark sits in the left margin, six pixels before the text
         * starts, so it follows the alignment.  The primary key is drawn
         * darker than the tie-break: two identical marks would say the
         * listing has two equal keys. */
        int primary = (c->field == tiku_trk_poseview_sort_field(a->view));
        int secondary = (c->field ==
                         tiku_trk_poseview_sort_secondary(a->view));
        int cx = tx - 6, cy = r.y + r.h / 2, k;
        int up = tiku_trk_poseview_sort_reverse(a->view);
        tiku_rgb_t ink =
            tiku_tint(TIKU_C_PANEL, primary ? 1.60f : 1.30f);

        if (primary || secondary) {
            /* Ascending points DOWN and reverse points UP, which is the way
             * round the original draws it -- the widest row is the base, so
             * the base has to be at the top for a downward apex. */
            for (k = 0; k < 3; k++) {
                int wgt = up ? k : 2 - k;

                tiku_hline(s, cx - wgt, cy - 1 + k, 2 * wgt + 1, ink);
            }
        }
    }
}

/** @brief The strip the barber pole occupies at the right of the count. */
#define POLE_W 10

/**
 * @brief The barber pole: a bevelled well with a moving stripe (TS-038).
 *
 * The phase comes from the clock rather than from a frame counter, so it
 * advances at the same rate whatever the draw rate happens to be.
 */
static void
draw_pole(app_t *a, tiku_rect_t count)
{
    tiku_surface_t *s = a->surf;
    tiku_rect_t r;
    int phase = (int)((tiku_trk_now() / 60000) % 6);
    int i;

    r.w = 7;
    r.h = 7;
    r.x = count.x + count.w - r.w - 3;
    r.y = count.y + (COUNT_H - r.h) / 2;
    tiku_fill(s, r, TIKU_C_DOC);
    tiku_frame(s, r, tiku_tint(TIKU_C_PANEL, 1.30f));
    for (i = -6; i < r.h + 6; i += 3) {
        int y = r.y + i + phase;
        int k;

        for (k = 0; k < 2; k++) {
            if (y + k >= r.y + 1 && y + k < r.y + r.h - 1) {
                tiku_hline(s, r.x + 1, y + k, r.w - 2,
                                tiku_tint(TIKU_C_PANEL, 1.45f));
            }
        }
    }
}

/** @brief The count view, shared by both modes. */
static void
count_label(app_t *a, int width, char *label, size_t max)
{
    const tiku_font_t *f = tiku_font_plain();
    const char *typed = tiku_trk_poseview_type_string(a->view);
    int total = tiku_trk_poseview_count(a->view);

    if (a->status[0] != '\0') {
        /* An operation's progress, a refusal, or a conflict question: the
         * count can wait, this is what the user needs to read. */
        snprintf(label, max, "%s", a->status);
    } else if (typed[0] != '\0' && tiku_trk_poseview_is_filtering(a->view)) {
        /* TS-042: the filter label carries its own count -- "N filter",
         * or "K/N filter" with a selection -- middle-truncated so both
         * ends survive a long expression. */
        char fit[200];
        int sel = tiku_trk_poseview_selection_count(a->view);

        if (tiku_text_fit(typed, width - 70, TIKU_TRUNC_MIDDLE,
                              measure_text, (void *)f, fit,
                              sizeof fit) < 0) {
            snprintf(fit, sizeof fit, "%s", typed);
        }
        if (sel > 0) {
            snprintf(label, max, "%d/%d %s", sel, total, fit);
        } else {
            snprintf(label, max, "%d %s", total, fit);
        }
    } else if (typed[0] != '\0') {
        /* TS-041: truncated from the BEGINNING, so the newest characters
         * -- the ones being typed -- stay visible. */
        if (tiku_text_fit(typed, width - 12, TIKU_TRUNC_BEGIN,
                              measure_text, (void *)f, label,
                              max > 240u ? 240u : max) < 0) {
            snprintf(label, max, "%s", typed);
        }
    } else if (tiku_trk_poseview_pole(a->view, tiku_trk_now())) {
        /* TS-038: a load long enough to notice says so.  The count is not
         * final yet, so it is not shown as though it were. */
        snprintf(label, max, tiku_str("status.items_so_far"), total,
                 (total == 1) ? "" : "s");
    } else if (total == 0) {
        /* TS-037: an empty folder says so in words.  "0 items" reads like
         * a count that failed rather than a folder with nothing in it. */
        snprintf(label, max, "%s",
                 tiku_trk_poseview_is_filtering(a->view)
                     ? tiku_str("status.nothing_matches")
                     : tiku_str("status.empty_folder"));
    } else if (tiku_trk_poseview_selection_count(a->view) > 0) {
        /* What is picked out is the thing being worked on, so the count
         * says how much of the folder that is (TS-037). */
        snprintf(label, max, tiku_str("status.selected"),
                 tiku_trk_poseview_selection_count(a->view), total);
    } else {
        snprintf(label, max, tiku_str("status.items_n"), total,
                 (total == 1) ? "" : "s",
                 tiku_trk_poseview_is_filtering(a->view)
                     ? tiku_str("status.filtered") : "");
    }
}

static void
draw_count(app_t *a, tiku_rect_t list)
{
    tiku_surface_t *s = a->surf;
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t count = { list.x, list.y + list.h, list.w, COUNT_H };
    const char *typed = tiku_trk_poseview_type_string(a->view);
    char label[240];

    /* The view-mode popup lives IN the scroller gutter, the one kind of
     * control that may (NeXT UIG ch7: only controls that decide how the
     * document is viewed belong beside the scrollers). */
    a->viewbtn_rect = (tiku_rect_t){ list.x + 110, count.y + 1, 18,
                                          COUNT_H - 2 };

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST &&
        list.w > 260) {
        /* The count keeps the bottom-LEFT and the horizontal scroll bar
         * shares the row at the same height (TS-044). */
        int content = tiku_trk_poseview_columns_width(a->view);
        float frac, pos;
        int tpos = 0, tlen = 0;

        count.w = 130;
        a->hsb_rect = (tiku_rect_t){ list.x + count.w,
                                          count.y, list.w - count.w,
                                          COUNT_H };
        (void)tiku_trk_scroll_set_range(&a->hsb, content, list.w);
        if (!a->hsb.tracking) {
            (void)tiku_trk_scroll_to(&a->hsb,
                                     tiku_trk_poseview_hscroll(a->view));
        }
        if (tiku_trk_scroll_thumb(&a->hsb, a->hsb_rect.w, &tpos, &tlen)) {
            frac = (float)tlen / (float)a->hsb_rect.w;
            pos = (a->hsb_rect.w > tlen)
                      ? (float)tpos / (float)(a->hsb_rect.w - tlen) : 0.0f;
        } else {
            frac = 1.0f;
            pos = 0.0f;
        }
        tiku_ui_scrollbar(s, a->hsb_rect, pos, frac, 1);
        if (a->active && !tiku_trk_poseview_is_query(a->view)) {
            tiku_frame(s, a->hsb_rect, TIKU_C_FOCUS);
        }
    } else {
        a->hsb_rect = (tiku_rect_t){ 0, 0, 0, 0 };
    }

    /* While typing, the string replaces the count on a document ground --
     * the user needs to see what they have typed, not how many items. */
    if (a->status[0] == '\0' && typed[0] != '\0') {
        tiku_ui_sunken(s, count, TIKU_C_DOC);
    } else {
        tiku_fill(s, count, TIKU_C_PANEL);
    }
    count_label(a, count.w, label, sizeof label);
    {   /* TS-039: while the pole is up the text gives up the strip it
         * occupies, so the two never overlap. */
        int pole = tiku_trk_poseview_pole(a->view, tiku_trk_now());
        tiku_rect_t clip = count;

        if (pole) {
            clip.w -= POLE_W;
        }
        tiku_clip_set(s, clip);
        tiku_text(s, f, count.x + 5,
                       count.y + (COUNT_H - f->height) / 2 + f->ascent,
                       label, TIKU_C_TEXT);
        tiku_clip_reset(s);
        if (pole) {
            draw_pole(a, count);
        }
    }
    {   /* The popup itself: a small raised button wearing the popup
         * triangle, beside the count. */
        tiku_rect_t b = a->viewbtn_rect;
        int cx = b.x + b.w / 2, cy = b.y + b.h / 2, i;

        tiku_fill(s, b, TIKU_C_PANEL);
        tiku_bevel(s, b, TIKU_RGB(255, 255, 255),
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
        for (i = 0; i < 4; i++) {
            tiku_hline(s, cx - 3 + i, cy - 2 + i, 7 - 2 * i,
                            TIKU_RGB(60, 60, 60));
        }
    }
}

/** @brief One icon: the node's own art, plus a centred label. */
static void
draw_pose_icon(app_t *a, const tiku_trk_pose_t *p, int x, int y, int size)
{
    tiku_surface_t *s = a->surf;
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t box;
    char label[64];
    int tw, lx, art = tiku_trk_poseview_art_size(a->view);

    /* The icon is drawn at the view's own size and CENTRED in the cell, so
     * changing icon size moves art within a stable grid rather than shifting
     * every label. */
    int drawn_sel = tiku_trk_poseview_draws_selected(a->view, p);
    int mini = (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_MINI);

    if (art > size) {
        art = size;
    }
    box.w = art;
    box.h = art;
    box.x = x + (size - art) / 2;
    box.y = y + 2;
    /* PVL-082: the icon has two states.  An inactive window without "show
     * selection when inactive" reverts to the normal art, which is what
     * makes an inactive window look inactive. */
    if (!((tiku_trk_clip_mode_of(&a->clip, p->model.node_id) ==
               TIKU_TRK_CLIP_MOVE)
            /* IV-023: cut poses are drawn faded in icon modes too. */
            ? tiku_icon_paint_dim(s, tiku_model_icon_name(&p->model),
                  box.x, box.y, art, 0.55f, TIKU_C_DOC)
            : drawn_sel
            ? tiku_icon_paint_selected(s,
                  tiku_model_icon_name(&p->model), box.x, box.y, art)
            : tiku_icon_paint(s, tiku_model_icon_name(&p->model),
                                  box.x, box.y, art))) {
        /* An icon the set does not carry still gets a cell of the right size,
         * so a gap in the art never disturbs the layout. */
        tiku_fill(s, box, TIKU_RGB(250, 250, 250));
        tiku_frame(s, box, tiku_tint(TIKU_C_PANEL, 1.40f));
    }

    /* A name wider than its cell loses its MIDDLE, not its tail: icons sit on
     * a grid, and a column of names sharing a prefix is unreadable when every
     * one of them is cut at the same point.  The view owns the budget, so
     * the box that selects a label and the text drawn in it are measured
     * from the same string in both shells (IV-047). */
    (void)tiku_trk_poseview_fit_label(a->view, p, label, sizeof label);
    tw = tiku_text_width(f, label);
    if (mini) {
        /* PVL-064/IV-048: in mini-icon mode the label sits to the RIGHT of
         * the icon, which is what the hit rect already assumes -- drawn
         * underneath, the text and the rectangle that selects it were in
         * different places. */
        lx = box.x + art + 4;
    } else {
        lx = x + (size - tw) / 2;
    }
    {
        int ly = mini ? box.y + (box.h - f->height) / 2
                      : box.y + box.h + 2;

        if (drawn_sel) {
            tiku_rect_t hl = { lx - 2, ly, tw + 4, f->height + 1 };

            tiku_fill(s, hl, TIKU_C_SELECT);
        }
        tiku_text(s, f, lx, ly + f->ascent, label,
                       drawn_sel ? TIKU_C_SELTEXT : TIKU_C_TEXT);
        if (p->model.kind == TIKU_KIND_SYMLINK) {
            /* MA-075: a link's name is underlined, and only its name -- the
             * one marker in this Tracker that says what an entry IS without
             * a second column. */
            tiku_hline(s, lx, ly + f->ascent + 2, tw,
                            drawn_sel ? TIKU_C_SELTEXT
                                      : TIKU_C_TEXT);
        }
    }
}

/** @brief Icon mode: poses at their own positions, plus the rubber band. */
static void
draw_icons(app_t *a, tiku_rect_t list)
{
    tiku_surface_t *s = a->surf;
    int n = tiku_trk_poseview_count(a->view);
    /* The cell comes from the layout and the ART from the mode: in
     * mini-icon mode the two differ, which is the whole point of it. */
    int size = tiku_trk_poseview_art_size(a->view);
    int i;

    tiku_ui_sunken(s, list, a->read_only
                                 ? tiku_tint(TIKU_C_DOC, 0.94f)
                                 : TIKU_C_DOC);
    if (a->active) {
        tiku_frame(s, list, TIKU_C_FOCUS);
    }
    tiku_clip_set(s, tiku_inset(list, 2));
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
        int px, py;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_location(a->view, p, &px, &py);
        draw_pose_icon(a, p, list.x + px, list.y + py, size);
    }
    if (a->band_active) {
        /* Tinted, the way Tracker draws it when the setting is left alone:
         * this shell carries no settings of its own, so it gets the
         * default rather than a third style nobody asked for. */
        tiku_rect_t r;
        int lox = (a->band_x0 < a->band_x1) ? a->band_x0 : a->band_x1;
        int loy = (a->band_y0 < a->band_y1) ? a->band_y0 : a->band_y1;

        r.x = lox;
        r.y = loy;
        r.w = (a->band_x0 > a->band_x1 ? a->band_x0 - a->band_x1
                                       : a->band_x1 - a->band_x0);
        r.h = (a->band_y0 > a->band_y1 ? a->band_y0 - a->band_y1
                                       : a->band_y1 - a->band_y0);
        tiku_paint_band(s, r, 1);
    }
    tiku_clip_reset(s);
}

/** @brief Menus sit above every other part of the window. */
static void
draw_popup(app_t *a)
{
    if (a->popup == NULL) {
        return;
    }
    /* Re-aimed here because the menus are rebuilt per draw, which clears
     * the hot row a moment after the motion set it: the last pointer the
     * window saw is where the highlight belongs. */
    (void)tiku_trk_menu_track(a->popup, a->popup_frame, a->ptr_x,
                              a->ptr_y);
    tiku_trk_menu_draw(a->popup, a->surf, a->popup_frame);
    if (a->popup_sub != NULL) {
        (void)tiku_trk_menu_track(a->popup_sub, a->popup_sub_frame,
                                  a->ptr_x, a->ptr_y);
        tiku_trk_menu_draw(a->popup_sub, a->surf, a->popup_sub_frame);
    }
}

/** @brief The browser's Back, Forward, Up and editable location controls. */
static void
draw_navigator(app_t *a)
{
    tiku_rect_t back, forward, up, field;
    unsigned back_state = tiku_trk_navigator_can_back(&a->nav)
                              ? 0u : TIKU_S_DISABLED;
    unsigned forward_state = tiku_trk_navigator_can_forward(&a->nav)
                                 ? 0u : TIKU_S_DISABLED;
    unsigned up_state = strcmp(tiku_trk_navigator_current(&a->nav), "/")
                            ? 0u : TIKU_S_DISABLED;

    if (!a->show_nav) {
        return;
    }
    navigator_layout(a, &back, &forward, &up, &field);
    tiku_ui_panel(a->surf, navigator_rect(a));
    tiku_ui_button(a->surf, back, "<", back_state);
    tiku_ui_button(a->surf, forward, ">", forward_state);
    tiku_ui_button(a->surf, up, "Up", up_state);
    if (a->nav_editing) {
        tiku_ui_textfield_sel(a->surf, field, a->nav_edit.text,
                                   a->nav_edit.caret, a->nav_edit.sel_a,
                                   a->nav_edit.sel_b, TIKU_S_FOCUS);
    } else {
        tiku_ui_textfield(a->surf, field,
                               tiku_trk_navigator_current(&a->nav), -1, 0u);
    }
}

static void
draw(app_t *a)
{
    tiku_surface_t *s = a->surf;
    const tiku_font_t *f = tiku_font_plain();
    const tiku_trk_column_t *cols;
    tiku_rect_t win = { 0, 0, s->w, s->h };
    tiku_rect_t list, title, bar, count;
    tiku_trk_sc_ctx_t mc;
    int ncol = tiku_trk_poseview_columns(a->view, &cols);
    int rows = visible_rows(a);
    int scroll = tiku_trk_poseview_scroll(a->view);
    int total = tiku_trk_poseview_count(a->view);
    int i;

    (void)tiku_ui_window(s, win, a->title, 1);
    mc = menu_ctx(a, 0u);
    tiku_trk_cwmenu_rebuild(&a->menus, &mc);
    a->menus.bar.open = a->bar_open;
    bar = menubar_rect(a);
    tiku_trk_menubar_draw(&a->menus.bar, s, bar);
    draw_folder_icon(a, bar);
    draw_navigator(a);
    list = list_rect(a);
    /* The view cannot see its own window.  Without this a page is one row
     * and the scroll is never clamped -- both stated in terms of what is
     * VISIBLE, which only the window knows. */
    tiku_trk_poseview_set_visible_rows(a->view, rows);

    /* The title strip.  Every title is placed from its column's OFFSET, so
     * a title and the cells beneath it cannot drift apart. */
    title = (tiku_rect_t){ list.x,
                                bar.y + bar.h + navigator_height(a),
                                list.w, TITLE_H };
    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        tiku_fill(s, title, TIKU_C_PANEL);
        tiku_clip_set(s, title);
        {
        int dragged, drag_dx = 0;

        dragged = tiku_trk_titleview_dragged(&a->tv, &drag_dx);
        for (i = 0; i < ncol; i++) {
            if (cols[i].field != dragged) {
                draw_title(a, title, &cols[i], 0, 0);
            }
        }
        /* The dragged title goes on top.  Drawn in place it would be
         * partly erased by whichever neighbour it is sliding over, since
         * the next title's rect begins exactly where this one ends. */
        for (i = 0; i < ncol; i++) {
            if (cols[i].field == dragged) {
                draw_title(a, title, &cols[i], 1, drag_dx);
            }
        }
        }
        tiku_clip_reset(s);
    }

    if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        draw_icons(a, list);
        draw_count(a, list);
        draw_popup(a);
        tiku_trk_status_overlay_draw(&a->status_overlay);
        a->dirty = 0;
        return;
    }

    /* Rows.  A store that cannot be written to is tinted, so a window that
     * will refuse every edit says so before one is attempted (PVL-076). */
    tiku_ui_sunken(s, list, a->read_only
                                 ? tiku_tint(TIKU_C_DOC, 0.94f)
                                 : TIKU_C_DOC);
    if (a->active) {
        /* CW-019: the focus border says which view the keyboard is aimed
         * at, which matters the moment there is more than one. */
        tiku_frame(s, list, TIKU_C_FOCUS);
    }
    tiku_clip_set(s, tiku_inset(list, 2));
    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, scroll + i);
        tiku_rect_t row = { list.x + 2, list.y + 2 + i * a->row_h,
                                 list.w - 4, a->row_h };
        int j;

        int drawn_sel;

        if (p == NULL) {
            break;
        }
        /* PVS-023/PVS-031: membership is the pose's flag, but whether it is
         * PAINTED depends on the window -- except during a band, which
         * paints its sweep whatever the window is doing. */
        drawn_sel = tiku_trk_poseview_draws_selected(a->view, p);
        tiku_fill(s, row, TIKU_C_DOC);
        if (drawn_sel && ncol > 0) {
            /* IV-024: selection belongs to the Name cell.  Keep the icon
             * selected, but leave Kind/Value/Location cells on document
             * ground instead of turning the whole row into one banner. */
            tiku_rect_t name = { col_x(a, &cols[0]), row.y,
                                      cols[0].width, row.h };
            tiku_rect_t inner = tiku_inset(list, 2);
            int x0 = (name.x > inner.x) ? name.x : inner.x;
            int x1 = (name.x + name.w < inner.x + inner.w)
                         ? name.x + name.w : inner.x + inner.w;

            name.x = x0;
            name.w = (x1 > x0) ? x1 - x0 : 0;
            if (name.w > 0) {
                tiku_fill(s, name, TIKU_C_SELECT);
            }
        }
        {   /* The icon owns the strip LEFT of the first column, which is
             * what StartOffset reserves and why a column's x is absolute
             * rather than a running total. */
            int mini = a->row_h - 2;
            int ix = list.x + 2 - tiku_trk_poseview_hscroll(a->view);

            if (mini > MINI_ICON_MAX) {
                mini = MINI_ICON_MAX;
            }
            (void)((tiku_trk_clip_mode_of(&a->clip, p->model.node_id) ==
                        TIKU_TRK_CLIP_MOVE)
                   /* IV-023: a pose cut to the clipboard is drawn faded,
                    * the same dim the other shell draws it with. */
                   ? tiku_icon_paint_dim(s,
                         tiku_model_icon_name(&p->model), ix,
                         row.y + (a->row_h - mini) / 2, mini, 0.55f,
                         TIKU_C_DOC)
                   : drawn_sel
                   ? tiku_icon_paint_selected(s,
                         tiku_model_icon_name(&p->model), ix,
                         row.y + (a->row_h - mini) / 2, mini)
                   : tiku_icon_paint(s,
                         tiku_model_icon_name(&p->model), ix,
                         row.y + (a->row_h - mini) / 2, mini));
        }
        for (j = 0; j < ncol; j++) {
            char tmp[160];
            /* Through the ENGINE's formatter, not a second copy: two
             * implementations of "what does this cell say" is how a column
             * comes to be fitted to text the row does not draw. */
            const char *txt = tiku_trk_poseview_cell_text(a->view, p,
                                                          cols[j].field, tmp,
                                                          sizeof tmp);
            int text_x = col_x(a, &cols[j]);
            tiku_rect_t cell = { text_x, row.y, cols[j].width,
                                      a->row_h };
            char fitted[80];
            int tw;

            {   /* Intersected with the list, not substituted for it: the
                 * clip is a replacement, so a cell scrolled left of the
                 * view would otherwise paint over the border and the
                 * window frame. */
                tiku_rect_t inner = tiku_inset(list, 2);
                int x0 = (cell.x > inner.x) ? cell.x : inner.x;
                int x1 = (cell.x + cell.w < inner.x + inner.w)
                         ? cell.x + cell.w : inner.x + inner.w;

                cell.x = x0;
                cell.w = (x1 > x0) ? x1 - x0 : 0;
                tiku_clip_set(s, cell);
            }
            (void)tiku_text_fit(txt, cols[j].width,
                                    tiku_trk_col_trunc_end(cols[j].field)
                                        ? TIKU_TRUNC_END
                                        : TIKU_TRUNC_MIDDLE,
                                    measure_text, (void *)f, fitted,
                                    sizeof fitted);
            tw = tiku_text_width(f, fitted);
            if (cols[j].alignment == TIKU_TRK_ALIGN_RIGHT) {
                text_x += cols[j].width - tw;
            }
            tiku_text(s, f, text_x,
                           row.y + (a->row_h - f->height) / 2 + f->ascent,
                           fitted, (drawn_sel && j == 0)
                                      ? TIKU_C_SELTEXT
                                      : TIKU_C_TEXT);
            if (cols[j].field == TIKU_TRK_COL_NAME &&
                p->model.kind == TIKU_KIND_SYMLINK) {
                /* MA-076/IV-043: under the TEXT only, and only in the first
                 * column -- a rule that underlined the whole cell would
                 * read as a divider rather than as a property of the name. */
                tiku_hline(s, text_x,
                                row.y + (a->row_h - f->height) / 2 +
                                f->ascent + 2, tw,
                                (drawn_sel && j == 0) ? TIKU_C_SELTEXT
                                                      : TIKU_C_TEXT);
            }
            tiku_clip_set(s, tiku_inset(list, 2));
        }
    }
    tiku_clip_reset(s);

    /* Scrollbar.  Drawn FROM the bar rather than from the view: it is a
     * thing that can be held, and while it is held the range it shows is
     * the one it was holding (PVL-051, PVL-052). */
    {
        tiku_rect_t sb = { list.x + list.w, list.y, SCROLL_W, list.h };
        int tpos = 0, tlen = 0;
        float frac, pos;

        (void)tiku_trk_scroll_set_range(&a->sb, total, rows);
        if (!a->sb.tracking) {
            (void)tiku_trk_scroll_to(&a->sb, scroll);
        }
        if (tiku_trk_scroll_thumb(&a->sb, list.h, &tpos, &tlen)) {
            frac = (float)tlen / (float)list.h;
            pos = (list.h > tlen)
                      ? (float)tpos / (float)(list.h - tlen) : 0.0f;
        } else {
            frac = 1.0f;
            pos = 0.0f;
        }
        a->sb_rect = sb;
        tiku_ui_scrollbar(s, sb, pos, frac, 0);
        /* CW-019: the bar highlights with the focus border -- except in a
         * query window, where Haiku keeps the highlight off. */
        if (a->active && !tiku_trk_poseview_is_query(a->view)) {
            tiku_frame(s, sb, TIKU_C_FOCUS);
        }
    }

    if (a->tv.track == TIKU_TRK_TRACK_RESIZE && a->tv.line_x >= 0) {
        /* TS-011: a full-height line down the LIST, not a mark on the
         * strip -- what the user is judging is where the column will fall
         * against the rows, and the strip cannot show that. */
        tiku_clip_set(s, tiku_inset(list, 2));
        tiku_vline(s, list.x + a->tv.line_x -
                        tiku_trk_poseview_hscroll(a->view), list.y + 2,
                        list.h - 4,
                        tiku_tint(TIKU_C_DOC, 1.40f));
        tiku_clip_reset(s);
    }
    if (a->editing) {
        /* The editor sits where the name is, as an editable field: the row
         * stays visible around it so the context is not lost. */
        int i, n = tiku_trk_poseview_count(a->view);
        int scroll = tiku_trk_poseview_scroll(a->view);

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
            tiku_rect_t box;

            if (p == NULL || strcmp(p->model.path, a->edit_path) != 0) {
                continue;
            }
            {
                /* The editor grows with the text and never past the name
                 * column; past the cap the text slides so the caret stays
                 * visible (MA-049). */
                int bw = cols[0].width, bs = 0;

                tiku_trk_edit_metrics(&a->ed, 60, cols[0].width,
                                      measure_prefix_px, NULL, &bw, &bs);
                box.x = col_x(a, &cols[0]);
                box.y = list.y + 2 + (i - scroll) * a->row_h;
                box.w = bw;
                box.h = a->row_h;
                tiku_ui_textfield_scroll(s, box, a->ed.text,
                                              a->ed.caret, a->ed.sel_a,
                                              a->ed.sel_b,
                                              TIKU_S_FOCUS, bs);
            }
            break;
        }
    }
    draw_count(a, list);
    draw_popup(a);
    tiku_trk_status_overlay_draw(&a->status_overlay);
    (void)count;
    a->dirty = 0;
}

static void
blit(app_t *a)
{
    if (a->host == NULL) {
        return;                     /* displayless: the surface IS the
                                     * output, and -shot writes it */
    }
    (void)tiku_host_present(a->host, a->surf);
}

static void
retitle(app_t *a)
{
    snprintf(a->title, sizeof a->title, "%s%s%s",
             (a->backend->devid[0] != '\0') ? "device:" : "",
             (a->backend->devid[0] != '\0') ? a->backend->devid : "",
             tiku_trk_poseview_path(a->view));
    if (a->backend->devid[0] == '\0') {
        snprintf(a->title, sizeof a->title, "%s",
                 tiku_trk_poseview_path(a->view));
    }
    tiku_host_set_title(a->host, a->title);
}

/**
 * @brief Place anything the restore did not position.
 *
 * Saved positions cover only what the user arranged; everything else -- new
 * entries, and icons auto-placed last time -- must be laid out once the
 * folder has finished loading, or it is simply invisible (PVL-020).
 */
static void
settle_layout(app_t *a)
{
    tiku_rect_t lr;

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        return;
    }
    lr = list_rect(a);
    (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
}

/** @brief Apply the shared ShowNavigator preference when its file changes. */
static void
sync_navigator_preference(app_t *a, int force)
{
    struct stat st;
    int present, visible = 1, hide_dotfiles = 0;
    int folders_first = 1, filter = 0;
    int64_t now = tiku_trk_now();
    time_t mtime = 0;
    off_t size = 0;

    if (!force && now - a->prefs_checked < 100000) {
        return;
    }
    a->prefs_checked = now;
    present = a->prefs_path[0] != '\0' && stat(a->prefs_path, &st) == 0;
    if (present) {
        mtime = st.st_mtime;
        size = st.st_size;
    }
    if (!force && present == a->prefs_present &&
        (!present || (mtime == a->prefs_mtime && size == a->prefs_size))) {
        return;
    }
    if (present) {
        tiku_trk_prefs_t prefs;
        int dark;

        tiku_trk_prefs_init(&prefs);
        (void)tiku_trk_prefs_load(&prefs, a->prefs_path);
        visible = tiku_trk_prefs_get(&prefs, "ShowNavigator") != 0;
        hide_dotfiles = tiku_trk_prefs_get(&prefs, "HideDotFiles") != 0;
        folders_first = tiku_trk_prefs_get(&prefs, "SortFolderNamesFirst") != 0;
        filter = tiku_trk_prefs_get(&prefs, "TypeAheadFiltering") != 0;
        dark = tiku_trk_prefs_get(&prefs, "DarkMode") != 0;
        if (dark != a->theme_dark) {
            /* The theme swap, live (settings semantics): the browser
             * repaints whole frames, so a flip costs one redraw. */
            a->theme_dark = dark;
            tiku_theme_set(dark ? tiku_theme_dark() : NULL);
            a->dirty = 1;
        }
        tiku_trk_prefs_free(&prefs);
    }
    a->prefs_present = present;
    a->prefs_mtime = mtime;
    a->prefs_size = size;
    if (visible != a->show_nav) {
        a->show_nav = visible;
        tiku_trk_poseview_set_visible_rows(a->view, visible_rows(a));
        settle_layout(a);
        a->dirty = 1;
    }
    tiku_trk_poseview_set_hide_dotfiles(a->view, hide_dotfiles);
    tiku_trk_poseview_set_folders_first(a->view, folders_first);
    a->filter_pref = filter;
    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST &&
        filter != tiku_trk_poseview_is_filtering(a->view)) {
        /* Live in both directions: on engages filtering for the next
         * keystroke, off drops the terms and restores the listing
         * (TS-052).  Guarded on the current state so an unrelated
         * settings change cannot wipe a filter mid-use. */
        tiku_trk_poseview_set_filtering(a->view, filter);
        a->dirty = 1;
    }
}

/**
 * @brief Gather the selection as paths for a file operation.
 *
 * @return Count written.
 */
static int
selection_paths(app_t *a, char paths[][TIKU_PATH_MAX],
                const char *out[], int max)
{
    int i, n = tiku_trk_poseview_count(a->view), k = 0;

    for (i = 0; i < n && k < max; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
        if (p == NULL || !p->selected) {
            continue;
        }
        snprintf(paths[k], TIKU_PATH_MAX, "%s", p->model.path);
        out[k] = paths[k];
        k++;
    }
    return k;
}

/**
 * @brief Ask the store whether this folder can be written to (PVL-076).
 *
 * Asked of the FOLDER rather than assumed from the backend: a device
 * namespace has writable and read-only regions side by side, and the tint
 * has to follow the one being shown.
 */
static void
settle_writability(app_t *a)
{
    tiku_model_t m;

    a->read_only = 0;
    if (a->backend == NULL || a->backend->ops->stat == NULL) {
        return;
    }
    if (a->backend->ops->stat(a->backend, tiku_trk_poseview_path(a->view),
                              &m) == 0) {
        a->read_only = ((m.facts.perm & TIKU_P_WRITE) == 0u);
    }
}

/** @brief Refresh the listing after an operation changed the folder. */
/**
 * @brief Let queued file work finish before the process goes.
 *
 * Bounded, because a job that cannot finish must not hold the application
 * open forever: the original gives up after five seconds and so does this.
 * What is already done stays done either way -- the engine has no
 * transaction above one item.
 */
static void
drain_fs(tiku_fs_t *fs)
{
    int guard = 0;

    if (fs == NULL) {
        return;
    }
    /* 50 turns of 100 ms, the original's own budget. */
    while (tiku_fs_busy(fs) && guard++ < 50) {
        if (tiku_fs_step(fs, 16) == 0) {
            const tiku_progress_t *pr = tiku_fs_progress(fs);

            /* A question stops the queue and there is nobody left to
             * answer it, so answer it the way the running shell would and
             * keep going rather than spinning out the whole budget. */
            if (pr->conflict) {
                tiku_fs_resolve(fs, TIKU_CONFLICT_RENAME);
            } else if (pr->error_ask) {
                tiku_fs_resolve_error(fs, 1);
            } else {
                break;
            }
        }
        usleep(100000);
    }
}

static void
after_op(app_t *a)
{
    (void)tiku_trk_poseview_reload(a->view);
    (void)tiku_trk_poseview_restore_state(a->view);
    if (a->select_next >= 0) {
        /* The row that has taken the deleted one's place, so a run of
         * deletes can be done with one key held down (FS-050). */
        (void)tiku_trk_poseview_select_after_removal(a->view, a->select_next);
        a->select_next = -1;
    }
    settle_layout(a);
    a->dirty = 1;
}

/** @brief Begin inline rename on the active pose (F2). */
static void
begin_rename(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL || a->fs == NULL) {
        return;
    }
    /* A namespace node has no name of its own to change; refuse up front
     * rather than letting the device say no after the fact. */
    if (p->model.kind == TIKU_KIND_DEVICE_NODE ||
        p->model.kind == TIKU_KIND_VIRTUAL_DIR) {
        snprintf(a->status, sizeof a->status,
                 "'%s' is part of the device namespace and cannot be renamed",
                 p->model.name);
        a->dirty = 1;
        return;
    }
    a->editing = 1;
    a->edit_row = pose_index(a, p);
    tiku_trk_edit_start(&a->ed, p->model.name);
    /* A character the store will refuse must never reach the field. */
    tiku_trk_edit_deny(&a->ed, tiku_fs_name_deny());
    snprintf(a->edit_path, sizeof a->edit_path, "%s", p->model.path);
    a->status[0] = '\0';
    a->dirty = 1;
}

/** @brief Begin the rename the slow-second-click timer just armed. */
static void
begin_rename_row(app_t *a, int row)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, row);

    if (p == NULL) {
        return;
    }
    /* The engine armed it on a row; the editor works on the ACTIVE pose, so
     * the two are made to agree before it opens. */
    tiku_trk_poseview_click(a->view, row, 0u);
    begin_rename(a);
}

/** @brief Commit or abandon the inline edit. */
static void
end_rename(app_t *a, int commit)
{
    char err[200];

    if (!a->editing) {
        return;
    }
    {   /* MA-050: a name that was not changed is not a rename.  Sent
         * anyway, the store finds the file already there and reports a
         * collision with itself -- an error where the rule wants silence. */
        const char *slash = strrchr(a->edit_path, '/');
        const char *was = (slash != NULL) ? slash + 1 : a->edit_path;

        if (commit && strcmp(was, a->ed.text) == 0) {
            commit = 0;
        }
    }
    if (commit && a->ed.len > 0 &&
        tiku_fs_rename(a->fs, a->edit_path, a->ed.text, err,
                           sizeof err) != 0) {
        /* A rejected name keeps the EDITOR, not just the reason: the text
         * stays, selected whole, so the user retypes immediately instead
         * of starting the rename over (NeXT UIG ch7: a rejected entry is
         * selected and highlighted for retyping). */
        snprintf(a->status, sizeof a->status, "%s", err);
        a->ed.active = 1;               /* Return had already closed it */
        a->ed.sel_a = 0;
        a->ed.sel_b = a->ed.len;
        a->ed.caret = a->ed.len;
        a->dirty = 1;
        return;
    }
    if (commit && a->ed.len > 0) {
        char renamed[TIKU_PATH_MAX];
        char parent[TIKU_PATH_MAX];
        char *slash;
        int row;

        a->status[0] = '\0';
        after_op(a);
        /* The rename re-sorted the row; the application moved it, so the
         * application keeps it visible (NeXT UIG ch3). */
        snprintf(parent, sizeof parent, "%s", a->edit_path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            *slash = '\0';
            snprintf(renamed, sizeof renamed, "%s/%s", parent, a->ed.text);
            row = index_of_path(a, renamed);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                tiku_trk_poseview_scroll_into_view(a->view, row,
                                                   visible_rows(a));
            }
        }
    }
    a->editing = 0;
    tiku_trk_edit_stop(&a->ed);
    a->dirty = 1;
}

/**
 * @brief The expanding-rectangle animation over a pose being opened.
 *
 * Seven steps, ten milliseconds apart, growing two pixels each time and
 * inverted so no colour has to be chosen or restored (PVS-013, PVL-073).
 * Drawn only for a MOUSE open: a keyboard open has no clicked rectangle to
 * grow out of.
 */
static void
open_animation(app_t *a, tiku_rect_t from)
{
    int k;

    for (k = 0; k < 7; k++) {
        tiku_rect_t r = from;

        r.x -= 2 * k;
        r.y -= 2 * k;
        r.w += 4 * k;
        r.h += 4 * k;
        tiku_invert_frame(a->surf, r);
        blit(a);
        usleep(10000);
        tiku_invert_frame(a->surf, r);   /* inversion undoes itself */
    }
    blit(a);
}

/** @brief Change this browser window to @p path and restore its view state. */
static int
navigate_to(app_t *a, const char *path, int record)
{
    char dest[TIKU_PATH_MAX];
    tiku_model_t m;

    if (path == NULL || path[0] == '\0' ||
        (a->backend->ops->stat != NULL &&
         (a->backend->ops->stat(a->backend, path, &m) != 0 ||
          !tiku_model_is_container(&m)))) {
        return 0;
    }
    /* Opening a pose passes storage owned by the view.  Switching folders
     * replaces that pose array, so keep the destination independent of it. */
    snprintf(dest, sizeof dest, "%s", path);
    if (strcmp(dest, tiku_trk_poseview_path(a->view)) == 0) {
        return 1;
    }
    tiku_trk_scroll_detach(&a->sb);
    (void)tiku_trk_poseview_save_state(a->view);
    (void)tiku_trk_poseview_save_columns(a->view);
    /* switch_dir returns the new row count, and only a negative value is an
     * error.  A non-empty directory is therefore a successful switch. */
    if (tiku_trk_poseview_switch_dir(a->view, dest) < 0) {
        tiku_trk_scroll_attach(&a->sb);
        return 0;
    }
    (void)tiku_trk_poseview_load_begin(a->view, tiku_trk_now());
    tiku_trk_scroll_attach(&a->sb);
    (void)tiku_trk_poseview_load_step(a->view, tiku_trk_now());
    (void)tiku_trk_poseview_restore_state(a->view);
    (void)tiku_trk_poseview_restore_columns(a->view);
    settle_layout(a);
    settle_writability(a);
    if (record) {
        tiku_trk_navigator_set(&a->nav, dest);
    }
    retitle(a);
    a->dirty = 1;
    return 1;
}

/** @brief Step to the preceding history entry. */
static void
navigate_back(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_back(&a->nav, path, sizeof path) &&
        !navigate_to(a, path, 0)) {
        (void)tiku_trk_navigator_forward(&a->nav, path, sizeof path);
    }
}

/** @brief Step to the history entry most recently left by Back. */
static void
navigate_forward(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_forward(&a->nav, path, sizeof path) &&
        !navigate_to(a, path, 0)) {
        (void)tiku_trk_navigator_back(&a->nav, path, sizeof path);
    }
}

/** @brief Navigate to the enclosing folder and record that new branch. */
static void
navigate_up(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_parent(tiku_trk_poseview_path(a->view), path,
                                  sizeof path)) {
        (void)navigate_to(a, path, 1);
    }
}

/** @brief Give the location field the keyboard with its path selected. */
static void
begin_location(app_t *a)
{
    end_rename(a, 1);
    a->nav_editing = 1;
    tiku_trk_edit_start(&a->nav_edit,
                        tiku_trk_navigator_current(&a->nav));
    a->dirty = 1;
}

/** @brief Apply or abandon the location field without surfacing bad paths. */
static void
end_location(app_t *a, int apply)
{
    char path[TIKU_PATH_MAX];

    if (!a->nav_editing) {
        return;
    }
    snprintf(path, sizeof path, "%s",
             (a->nav_edit.text[0] != '\0') ? a->nav_edit.text : "/");
    tiku_trk_edit_stop(&a->nav_edit);
    a->nav_editing = 0;
    if (apply) {
        (void)navigate_to(a, path, 1);
    }
    a->dirty = 1;
}

/** @brief Open the active pose: a container navigates, a value is reported. */
static void
open_selection(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL) {
        return;
    }
    if (tiku_model_is_container(&p->model)) {
        (void)navigate_to(a, p->model.path, 1);
    }
    a->dirty = 1;
}

/**
 * @brief The character a key event types, or 0.
 *
 * With no display there is no keyboard map to consult, so a Latin-1 keysym
 * stands for itself -- which is all a script needs in order to type.
 */
static char
key_char(const tiku_event_t *event)
{
    return (event != NULL) ? event->text[0] : (char)0;
}

/** @brief Row under a point, or -1 for empty space / the title bar. */
static int
row_at(app_t *a, int px, int py)
{
    tiku_rect_t list = list_rect(a);
    int rows, idx;

    if (py < list.y + 2 || py > list.y + list.h ||
        px < list.x || px > list.x + list.w) {
        return -1;
    }
    rows = (py - list.y - 2) / a->row_h;
    idx = tiku_trk_poseview_scroll(a->view) + rows;
    return (idx < tiku_trk_poseview_count(a->view)) ? idx : -1;
}

/** @brief Hit-test the count strip, kept separate from list rows (TS-043). */
static int
count_at(app_t *a, int px, int py)
{
    tiku_rect_t list = list_rect(a);

    return a != NULL && px >= list.x && px <= list.x + list.w &&
           py >= list.y + list.h && py <= list.y + list.h + COUNT_H;
}

/** @brief Resolve a visible list row to a point the script can click. */
static int
script_row_point(void *ctx, int row, int *x, int *y)
{
    app_t *a = (app_t *)ctx;
    tiku_rect_t list;
    int rel;

    if (a == NULL || x == NULL || y == NULL || row < 0 ||
        tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    rel = row - tiku_trk_poseview_scroll(a->view);
    if (rel < 0 || rel >= visible_rows(a) ||
        row >= tiku_trk_poseview_count(a->view)) {
        return 0;
    }
    list = list_rect(a);
    *x = list.x + 20;
    *y = list.y + 2 + rel * a->row_h + a->row_h / 2;
    return 1;
}

/** @brief Resolve a named icon to its view-space location (drop space). */
static int
script_pose_at(void *ctx, const char *name, int *x, int *y)
{
    app_t *a = (app_t *)ctx;
    int i, n;

    if (a == NULL || name == NULL || x == NULL || y == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(a->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);

        if (p != NULL && p->placed &&
            strcmp(p->model.name, name) == 0) {
            tiku_trk_poseview_pose_location(a->view, p, x, y);
            return 1;
        }
    }
    return 0;
}

/** @brief One pixel of the window, for a check that asks about ink. */
static unsigned
script_pixel_at(void *ctx, int x, int y)
{
    app_t *a = (app_t *)ctx;

    if (a->surf == NULL || a->surf->px == NULL || x < 0 || y < 0 ||
        x >= a->surf->w || y >= a->surf->h) {
        return 0u;
    }
    return (unsigned)tiku_peek(a->surf, x, y) & 0xffffffu;
}

/** @brief Resolve a named icon to the screen point it is drawn at. */
static int
script_pose_screen(void *ctx, const char *name, int *x, int *y)
{
    app_t *a = (app_t *)ctx;
    tiku_rect_t lr;

    if (!script_pose_at(ctx, name, x, y)) {
        return 0;
    }
    /* Converted the way draw_icons converts on the way out, so a test can
     * name a screen point instead of the chrome. */
    lr = list_rect(a);
    *x += lr.x;
    *y += lr.y;
    return 1;
}

/** @brief Whether a point is on the title strip, and where in its own space. */
static int
title_hit(app_t *a, int px, int py, int *cx, int *cy)
{
    tiku_rect_t list = list_rect(a);
    int top = TAB_H + BORDER + a->menus.bar.height + navigator_height(a);

    if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (cx != NULL) {
        *cx = px - list.x + tiku_trk_poseview_hscroll(a->view);
    }
    if (cy != NULL) {
        *cy = py - top;
    }
    return (py >= top && py <= top + TITLE_H);
}

static void
handle_key(app_t *a, const tiku_event_t *event)
{
    unsigned key = event->key;
    unsigned modifiers = event->modifiers;
    unsigned mods = 0;
    int nav = 0;

    if (tiku_trk_status_overlay_key(&a->status_overlay, key)) {
        /* A modal alert owns the keyboard: Escape is Cancel, Return the
         * default, and a letter presses the button it starts with. */
        a->dirty = 1;
        return;
    }
    if (a->nav_editing) {
        tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
        tiku_trk_edit_result_t result;
        int shift = (modifiers & TIKU_MOD_SHIFT) ? 1 : 0;
        char ch = '\0';

        if (key == ' ' && (modifiers & TIKU_MOD_CMD) != 0u) {
            /* Cmd-Space completes the last path component against the
             * folder the rest of the path names (NeXT UIG ch3). */
            char dir[TIKU_PATH_MAX], full[TIKU_PATH_MAX];
            char *slash;

            snprintf(dir, sizeof dir, "%s", a->nav_edit.text);
            slash = strrchr(dir, '/');
            if (slash != NULL) {
                char leaf[TIKU_NAME_MAX];

                snprintf(leaf, sizeof leaf, "%s", slash + 1);
                slash[(slash == dir) ? 1 : 0] = '\0';
                if (tiku_trk_panel_complete(a->backend, dir, leaf, leaf,
                                            sizeof leaf) > 0) {
                    snprintf(full, sizeof full, "%s%s%s", dir,
                             (dir[strlen(dir) - 1u] == '/') ? "" : "/",
                             leaf);
                    tiku_trk_edit_start(&a->nav_edit, full);
                    a->nav_edit.sel_a = a->nav_edit.sel_b =
                        a->nav_edit.caret = a->nav_edit.len;
                }
            }
            a->dirty = 1;
            return;
        }
        switch (key) {
        case TIKU_KEY_RETURN: k = TIKU_TRK_EDIT_COMMIT;     break;
        case TIKU_KEY_ESCAPE: k = TIKU_TRK_EDIT_CANCEL;     break;
        case TIKU_KEY_BACKSPACE: k = TIKU_TRK_EDIT_BACKSPACE; break;
        case TIKU_KEY_DELETE: k = TIKU_TRK_EDIT_DELETE;     break;
        case TIKU_KEY_LEFT:   k = TIKU_TRK_EDIT_LEFT;       break;
        case TIKU_KEY_RIGHT:  k = TIKU_TRK_EDIT_RIGHT;      break;
        case TIKU_KEY_HOME:   k = TIKU_TRK_EDIT_HOME;       break;
        case TIKU_KEY_END:    k = TIKU_TRK_EDIT_END;        break;
        case 'a':
            if (modifiers & TIKU_MOD_CMD) {
                k = TIKU_TRK_EDIT_SELECT_ALL;
            }
            break;
        default: break;
        }
        if (k == TIKU_TRK_EDIT_CHAR) {
            ch = key_char(event);
            if (ch == '\0') {
                return;
            }
        }
        result = tiku_trk_edit_key(&a->nav_edit, k, shift, ch);
        if (result == TIKU_TRK_EDIT_COMMITTED) {
            end_location(a, 1);
        } else if (result == TIKU_TRK_EDIT_CANCELLED) {
            end_location(a, 0);
        }
        a->dirty = 1;
        return;
    }
    if (a->editing) {
        /* While editing a name the view's keys are OFF: an arrow must move
         * the caret's row selection nowhere, and 'd' must type a letter,
         * not duplicate the file. */
        tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
        int shift = (modifiers & TIKU_MOD_SHIFT) ? 1 : 0;
        char ch = '\0';

        if ((modifiers & TIKU_MOD_CMD) &&
            (key == 'x' || key == 'c' || key == 'v')) {
            tiku_trk_edit_key_t text_key = (key == 'x') ? TIKU_TRK_EDIT_CUT
                                      : (key == 'c') ? TIKU_TRK_EDIT_COPY
                                                     : TIKU_TRK_EDIT_PASTE;
            (void)tiku_trk_edit_key(&a->ed, text_key, 0, '\0');
            a->dirty = 1;
            return;
        }
        if ((modifiers & TIKU_MOD_CMD) && key == 'n') {
            end_rename(a, 1);
            goto not_editing;
        }
        if (key == TIKU_KEY_TAB) {
            int row = next_edit_row(a, a->edit_row,
                                    (modifiers & TIKU_MOD_SHIFT) != 0u);

            end_rename(a, 1);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                begin_rename(a);
            }
            a->dirty = 1;
            return;
        }
        switch (key) {
        case TIKU_KEY_RETURN: k = TIKU_TRK_EDIT_COMMIT;    break;
        case TIKU_KEY_ESCAPE: k = TIKU_TRK_EDIT_CANCEL;    break;
        case TIKU_KEY_BACKSPACE: k = TIKU_TRK_EDIT_BACKSPACE; break;
        case TIKU_KEY_DELETE: k = TIKU_TRK_EDIT_DELETE;    break;
        case TIKU_KEY_LEFT:   k = TIKU_TRK_EDIT_LEFT;      break;
        case TIKU_KEY_RIGHT:  k = TIKU_TRK_EDIT_RIGHT;     break;
        case TIKU_KEY_HOME:   k = TIKU_TRK_EDIT_HOME;      break;
        case TIKU_KEY_END:    k = TIKU_TRK_EDIT_END;       break;
        case 'a':
            if (modifiers & TIKU_MOD_CMD) {
                k = TIKU_TRK_EDIT_SELECT_ALL;
            }
            break;
        default: break;
        }
        if (k == TIKU_TRK_EDIT_CHAR) {
            ch = key_char(event);
            if (ch == '\0') {
                return;
            }
        }
        switch (tiku_trk_edit_key(&a->ed, k, shift, ch)) {
        case TIKU_TRK_EDIT_COMMITTED: end_rename(a, 1); break;
        case TIKU_TRK_EDIT_CANCELLED: end_rename(a, 0); break;
        default: break;
        }
        a->dirty = 1;
        return;
    }

not_editing:
    if (modifiers & TIKU_MOD_SHIFT) { mods |= TIKU_TRK_MOD_SHIFT; }
    if (modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }

    if ((modifiers & TIKU_MOD_CMD) && key == 'l') {
        begin_location(a);
        return;
    }
    if ((modifiers & TIKU_MOD_CMD) &&
        (key == 'x' || key == 'c' || key == 'v')) {
        /* The clipboard verbs, through the shared command layer: this
         * shell had none at all, so Cut, Copy and Paste were three keys
         * that did nothing outside a text field. */
        int base = (key == 'x') ? TIKU_TRK_CMD_CUT
                 : (key == 'c') ? TIKU_TRK_CMD_COPY : TIKU_TRK_CMD_PASTE;

        menu_invoke(a, tiku_trk_sc_command(base, mods));
        return;
    }
    if (modifiers & TIKU_MOD_CMD) {
        if (key == TIKU_KEY_DOWN || key == TIKU_KEY_RETURN) {
            open_selection(a);
            return;
        }
        if (key == TIKU_KEY_LEFT) {
            navigate_back(a);
            return;
        }
        if (key == TIKU_KEY_RIGHT) {
            navigate_forward(a);
            return;
        }
        if (key == TIKU_KEY_UP) {
            navigate_up(a);
            return;
        }
    }
    if ((modifiers & TIKU_MOD_OPTION) && key == TIKU_KEY_RETURN) {
        open_selection(a);
        return;
    }

    switch (key) {
    case 'o':
        if (mods & TIKU_TRK_MOD_CMD) {
            if (tiku_trk_poseview_active(a->view) == NULL) {
                /* Grey in the menu; said out loud for the chord
                 * (NeXT UIG ch6). */
                snprintf(a->status, sizeof a->status,
                         "Open is not available here");
                a->dirty = 1;
                return;
            }
            open_selection(a);
            return;
        }
        break;
    case TIKU_KEY_UP:        nav = TIKU_TRK_KEY_UP;    break;
    case TIKU_KEY_DOWN:      nav = TIKU_TRK_KEY_DOWN;  break;
    case TIKU_KEY_LEFT:      nav = TIKU_TRK_KEY_LEFT;  break;
    case TIKU_KEY_RIGHT:     nav = TIKU_TRK_KEY_RIGHT; break;
    case TIKU_KEY_HOME:      nav = TIKU_TRK_KEY_HOME;  break;
    case TIKU_KEY_END:       nav = TIKU_TRK_KEY_END;   break;
    case TIKU_KEY_PAGE_UP:   nav = TIKU_TRK_KEY_PGUP;  break;
    case TIKU_KEY_PAGE_DOWN: nav = TIKU_TRK_KEY_PGDN;  break;
    default: break;
    }
    if (nav != 0) {
        int before = tiku_trk_poseview_scroll(a->view);
        (void)tiku_trk_poseview_key_nav(a->view, nav, mods);
        {   /* PVK-004: the arrow scrolls its target into view. */
            tiku_trk_pose_t *act = tiku_trk_poseview_active(a->view);
            int i, n = tiku_trk_poseview_count(a->view);
            for (i = 0; act != NULL && i < n; i++) {
                if (tiku_trk_poseview_at(a->view, i) == act) {
                    tiku_trk_poseview_scroll_into_view(a->view, i,
                                                       visible_rows(a));
                    break;
                }
            }
        }
        (void)before;
        a->dirty = 1;
        return;
    }
    switch (key) {
    case TIKU_KEY_RETURN:
        /* PVK-015/016: a filter that has already narrowed the listing to
         * what the user wants should not also demand an arrow key first. */
        if (tiku_trk_poseview_return_pressed(a->view,
                                             (mods & TIKU_TRK_MOD_SHIFT))) {
            open_selection(a);
            tiku_trk_poseview_set_filtering(a->view, 0);
            if (a->filter_pref) {
                /* The terms are spent; the MODE follows the preference,
                 * so the next keystroke filters again (TS-052). */
                tiku_trk_poseview_set_filtering(a->view, 1);
            }
        } else {
            open_selection(a);
        }
        a->dirty = 1;
        return;
    case ' ':
        if (mods & TIKU_TRK_MOD_SHIFT) {
            /* PVK-031: a second term, each free to match a different
             * column, which is what makes two short words selective. */
            if (tiku_trk_poseview_is_filtering(a->view)) {
                (void)tiku_trk_poseview_filter_term(a->view);
                a->dirty = 1;
                return;
            }
        }
        break;
    case TIKU_KEY_BACKSPACE:
        if (tiku_trk_poseview_type_string(a->view)[0] != '\0') {
            tiku_trk_poseview_type_backspace(a->view);
        } else {
            navigate_up(a);
        }
        a->dirty = 1;
        return;
    case TIKU_KEY_ESCAPE:
        if (mods & TIKU_TRK_MOD_SHIFT) {
            /* Shift+Escape removes the selection from the clipboard, or
             * every pose in the folder when nothing is selected
             * (PVK-034). */
            int i, n = tiku_trk_poseview_count(a->view);
            int any = tiku_trk_poseview_selection_count(a->view) > 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);

                if (p != NULL && (!any || p->selected)) {
                    (void)tiku_trk_clip_remove(&a->clip, &p->model, 1);
                }
            }
        } else {
            if (tiku_trk_clip_count(&a->clip) > 0) {
                /* A pending cut is aborted before the type-ahead goes,
                 * in the source's own order (PVK-034). */
                tiku_trk_clip_clear(&a->clip);
            }
            tiku_trk_poseview_type_clear(a->view);
        }
        a->dirty = 1;
        return;
    case TIKU_KEY_TAB:
        if (!tiku_trk_poseview_is_filtering(a->view)) {
            (void)tiku_trk_poseview_tab_next(a->view,
                (modifiers & TIKU_MOD_SHIFT) != 0u);
        }
        a->dirty = 1;
        return;
    case 'q':
        if (modifiers & TIKU_MOD_CMD) { a->running = 0; return; }
        break;
    case 'u':
        if (modifiers & TIKU_MOD_CMD) {
            /* Cmd+U, which the volume menus advertise (CW-034). */
            menu_invoke(a, TIKU_TRK_CMD_UNMOUNT);
            return;
        }
        break;
    case 'a':
        if (modifiers & TIKU_MOD_CMD) {
            tiku_trk_poseview_select_all(a->view);
            a->dirty = 1;
            return;
        }
        break;
    case 's':
        if (modifiers & TIKU_MOD_CMD) {
            /* PVK-046: invert, which is the only way to say "everything
             * except these" without clicking each of the rest. */
            tiku_trk_poseview_invert_selection(a->view);
            a->dirty = 1;
            return;
        }
        break;
    case '+':
    case '=':
        if (modifiers & TIKU_MOD_CMD) {
            (void)tiku_trk_poseview_step_icon_size(a->view, 1);
            settle_layout(a);
            a->dirty = 1;
            return;
        }
        break;
    case '-':
        if (modifiers & TIKU_MOD_CMD) {
            (void)tiku_trk_poseview_step_icon_size(a->view, 0);
            settle_layout(a);
            a->dirty = 1;
            return;
        }
        break;
    case 'r':
        if (modifiers & TIKU_MOD_CMD) {
            (void)tiku_trk_poseview_reload(a->view);
            (void)tiku_trk_poseview_restore_state(a->view);
            settle_layout(a);
            a->dirty = 1;
            return;
        }
        break;
    case '1':
    case '2':
    case '3':
        if (modifiers & TIKU_MOD_CMD) {
            tiku_rect_t lr = list_rect(a);
            tiku_trk_poseview_set_mode(a->view,
                (key == '1') ? TIKU_TRK_MODE_ICON :
                (key == '2') ? TIKU_TRK_MODE_MINI : TIKU_TRK_MODE_LIST);
            if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
                (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
            } else if (a->filter_pref &&
                       !tiku_trk_poseview_is_filtering(a->view)) {
                /* Leaving list mode tore filtering down; coming back the
                 * preference still says filter (TS-052). */
                tiku_trk_poseview_set_filtering(a->view, 1);
            }
            (void)tiku_trk_poseview_save_state(a->view);
            a->dirty = 1;
            return;
        }
        break;
    case TIKU_KEY_F2:
        begin_rename(a);
        return;
    case 'e':
        if (modifiers & TIKU_MOD_CMD) {
            begin_rename(a);
            return;
        }
        break;
    case TIKU_KEY_DELETE: {
        /* What the key MEANS depends on where the items are: a move that
         * can be undone where there is a Trash to move them to, and a
         * deletion inside it or when Shift says the user has already
         * decided (PVK-038).  The key asks the same controller the menu
         * item does, so the question a delete puts up is one rule and not
         * one per way of reaching it (FS-046). */
        /* Noted BEFORE the listing changes: afterwards the index is
         * the only thing left pointing at where the item was (FS-050). */
        a->select_next = first_selected_index(a);
        menu_invoke(a, (modifiers & TIKU_MOD_SHIFT)
                           ? TIKU_TRK_CMD_DELETE
                           : TIKU_TRK_CMD_MOVE_TO_TRASH);
        a->dirty = 1;
        return;
    }
    case 'd':
        if (modifiers & TIKU_MOD_CMD) {
            char paths[64][TIKU_PATH_MAX];
            const char *sel[64];
            int n = selection_paths(a, paths, sel, 64);

            if (n > 0 && a->fs != NULL) {
                (void)tiku_fs_duplicate(a->fs, sel, n);
                snprintf(a->status, sizeof a->status, "duplicating %d item%s",
                         n, (n == 1) ? "" : "s");
            }
            a->dirty = 1;
            return;
        }
        break;
    case 'z':
        if ((modifiers & TIKU_MOD_CMD) &&
            (modifiers & TIKU_MOD_SHIFT)) {
            char what[64], err[200];

            if (a->fs != NULL && tiku_fs_can_redo(a->fs, what,
                                                      sizeof what)) {
                int n = tiku_fs_redo(a->fs, err, sizeof err);

                snprintf(a->status, sizeof a->status, "redid %s (%d item%s)",
                         what, n, (n == 1) ? "" : "s");
                after_op(a);
            } else {
                snprintf(a->status, sizeof a->status, "nothing to redo");
            }
            a->dirty = 1;
            return;
        }
        if (modifiers & TIKU_MOD_CMD) {
            char what[64], err[200];

            if (a->fs != NULL && tiku_fs_can_undo(a->fs, what,
                                                      sizeof what)) {
                int n = tiku_fs_undo(a->fs, err, sizeof err);
                snprintf(a->status, sizeof a->status, "undid %s (%d item%s)",
                         what, n, (n == 1) ? "" : "s");
                after_op(a);
            } else {
                snprintf(a->status, sizeof a->status, "nothing to undo");
            }
            a->dirty = 1;
            return;
        }
        break;
    case 'n':
        if (modifiers & TIKU_MOD_CMD) {
            char made[TIKU_PATH_MAX], err[200];

            if (a->fs != NULL &&
                tiku_fs_new_folder(a->fs, tiku_trk_poseview_path(a->view), made, sizeof made, err,
                                       sizeof err) == 0) {
                const char *slash = strrchr(made, '/');

                snprintf(a->status, sizeof a->status, "created '%.180s'",
                         (slash != NULL) ? slash + 1 : made);
                after_op(a);
                /* Tracker opens the name for editing straight away; here
                 * the folder is at least selected, so the rename is the
                 * next keystroke rather than a hunt down the list. */
                {
                    int i = index_of_path(a, made);

                    if (i >= 0) {
                        tiku_trk_poseview_click(a->view, i, 0u);
                        begin_rename(a);
                        snprintf(a->status, sizeof a->status,
                                 "created '%.180s'",
                                 (slash != NULL) ? slash + 1 : made);
                    }
                }
            } else {
                snprintf(a->status, sizeof a->status, "%s",
                         err[0] ? err : "could not create a new folder");
            }
            a->dirty = 1;
            return;
        }
        break;
    case 'k':
        if (modifiers & TIKU_MOD_CMD) {
            tiku_rect_t lr = list_rect(a);

            /* Cmd+K snaps what is there; Shift re-lays it out in sort
             * order.  Straightening an arrangement and replacing it are
             * two different commands (PVL-021). */
            tiku_trk_scroll_detach(&a->sb);
            if (modifiers & TIKU_MOD_SHIFT) {
                tiku_trk_poseview_cleanup(a->view, lr.w - 4);
            } else {
                (void)tiku_trk_poseview_snap_to_grid(a->view);
            }
            tiku_trk_scroll_attach(&a->sb);
            (void)tiku_trk_poseview_save_state(a->view);
            a->dirty = 1;
            return;
        }
        break;
    default:
        break;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u) {
        /* The standard set's tail, from the same table the menus read:
         * a chord the menu advertises must answer here too (PVK-042). */
        int fcmd = tiku_trk_sc_command_for_key(key, modifiers);

        if (fcmd != 0) {
            menu_invoke(a, fcmd);
            a->dirty = 1;
            return;
        }
    }
    {
        char typed = key_char(event);

        if (typed >= 0x20 && typed < 0x7f) {
            tiku_trk_poseview_type_ahead(a->view, typed);
            a->dirty = 1;
        }
    }
}

/** @brief Send a menu action through the keyboard path it shares. */
static void
menu_key(app_t *a, unsigned key, unsigned modifiers)
{
    tiku_event_t event;

    memset(&event, 0, sizeof event);
    event.type = TIKU_EVENT_KEY_DOWN;
    event.key = key;
    event.modifiers = modifiers;
    if (key >= 0x20u && key < 0x7fu) {
        event.text[0] = (char)key;
    }
    handle_key(a, &event);
}

/** @brief Save what is open on the leaving volume while it is still there
 *         to save to (AW-096). */
static void
save_before_unmount(dev_t dev, void *context)
{
    app_t *a = context;

    if (tiku_trk_poseview_on_device(a->view, (uint64_t)dev)) {
        (void)tiku_trk_poseview_save_state(a->view);
    }
}

/**
 * @brief Point a new entry at where it was asked for (PVL-025, PVL-098).
 *
 * The menu's own corner is where it was opened, which is the click that
 * asked for the folder; a keyboard shortcut has no menu, so the last press
 * answers instead, and only while it is over the view.
 */
static int
seed_new_entry(app_t *a, int *vx, int *vy)
{
    tiku_rect_t lr = list_rect(a);
    int x, y;

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (a->popup != NULL) {
        x = a->popup_frame.x;
        y = a->popup_frame.y;
    } else {
        x = a->press_x;
        y = a->press_y;
    }
    if (x < lr.x || x >= lr.x + lr.w || y < lr.y || y >= lr.y + lr.h) {
        return 0;
    }
    *vx = x - lr.x;
    *vy = y - lr.y;
    return 1;
}

/** @brief Carry out a command from the shared container-window menus. */
static void
menu_invoke(app_t *a, int command)
{
    const char *template_path;
    const char *template_parent;
    int field = -1;

    {
        /* The one enablement the menus use gates EVERY route here: a
         * chord for a command the menu would show grey answers out loud
         * instead of being silently swallowed (NeXT UIG ch6). */
        tiku_trk_sc_ctx_t gc = menu_ctx(a, 0u);

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
            snprintf(a->status, sizeof a->status,
                     "%s is not available here",
                     tiku_trk_sc_label(command, 0u));
            a->dirty = 1;
            return;
        }
    }
    if (command == TIKU_TRK_CMD_MOUNT_SETTINGS) {
        /* The preferences page for it lives in the desktop shell. */
        snprintf(a->status, sizeof a->status,
                 "mount settings live in the desktop shell's preferences");
        a->dirty = 1;
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
            tiku_partition_t part[TIKU_TRK_MOUNT_PART_MAX];
            int pn = tiku_partitions_scan(part,
                                              TIKU_TRK_MOUNT_PART_MAX);
            int k;

            for (k = 0; k < pn; k++) {
                if (part[k].mounted) {
                    continue;
                }
                tried++;
                if (tiku_volume_mount(part[k].name, why,
                                          sizeof why) != 0) {
                    failed++;
                    break;
                }
            }
        } else {
            int idx = command - TIKU_TRK_CMD_MOUNT_PART_BASE;

            if (idx < a->menus.mount_parts) {
                tried++;
                failed = tiku_volume_mount(a->menus.mount_part[idx],
                                               why, sizeof why) != 0;
            }
        }
        if (failed && why[0] != '\0') {
            tiku_trk_alert_open(
                tiku_trk_status_overlay_alert(&a->status_overlay),
                TIKU_TRK_ALERT_STOP, TIKU_TRK_ALERT_TAG_REFUSAL, why,
                "Cancel");
        } else if (tried == 0) {
            snprintf(a->status, sizeof a->status,
                     "every partition is already mounted");
        }
        a->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_UNMOUNT) {
        /* The selection names the volume; with none, the volume ROOT being
         * shown is the one the gesture means (CW-034).  Everything open on
         * it saves itself first, while there is still somewhere to save
         * to, and a refusal or failure is said out loud (AW-096). */
        char paths[1][TIKU_PATH_MAX];
        const char *sel[1];
        const char *path = (selection_paths(a, paths, sel, 1) > 0)
                               ? sel[0] : tiku_trk_poseview_path(a->view);
        tiku_volumes_t vols;
        char why[200];

        why[0] = '\0';
        if (tiku_volumes_scan(&vols) > 0 &&
            tiku_volume_is_root(&vols, path)) {
            const tiku_volume_t *vol = tiku_volume_of(&vols, path);

            if (tiku_volume_unmount(&vols, path, save_before_unmount,
                                        a, why, sizeof why) ==
                    TIKU_UNMOUNT_OK) {
                snprintf(a->status, sizeof a->status, "unmounted \"%.60s\"",
                         (vol != NULL) ? vol->name : path);
            } else if (why[0] != '\0') {
                tiku_trk_alert_open(
                    tiku_trk_status_overlay_alert(&a->status_overlay),
                    TIKU_TRK_ALERT_STOP, TIKU_TRK_ALERT_TAG_REFUSAL, why,
                    "Cancel");
            }
        } else {
            /* Nothing here IS a volume: said, not swallowed (NeXT UIG
             * ch6). */
            snprintf(a->status, sizeof a->status,
                     "Unmount is not available here");
        }
        a->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_NEW_FOLDER) {
        /* Made HERE rather than in the shared controller: only the shell
         * knows where the asking click landed, and the folder must appear
         * there rather than in the next free slot (PVL-025, PVL-098). */
        char made[TIKU_PATH_MAX], error[200];
        int seeded, sx = 0, sy = 0, row;
        const char *slash;

        error[0] = '\0';
        seeded = seed_new_entry(a, &sx, &sy);
        if (a->fs != NULL && tiku_fs_new_folder(a->fs,
                tiku_trk_poseview_path(a->view), made, sizeof made,
                error, sizeof error) == 0) {
            slash = strrchr(made, '/');
            if (seeded) {
                tiku_trk_poseview_drop_land(a->view,
                    (slash != NULL) ? slash + 1 : made, sx, sy);
            }
            (void)tiku_trk_poseview_reload(a->view);
            /* A reload drains in one go, so the loading loop never runs
             * for it: the landing recorded above is applied here or not
             * at all. */
            settle_layout(a);
            row = index_of_path(a, made);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                begin_rename(a);
            }
            snprintf(a->status, sizeof a->status, "created '%.180s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(a->status, sizeof a->status, "%.200s",
                error[0] != '\0' ? error : "could not create a new folder");
        }
        a->dirty = 1;
        return;
    }
    /* Through the shared controller first: what Delete, Duplicate, Create
     * link, Identify and the clipboard verbs mean is one implementation,
     * not one per shell. */
    a->folder.view = a->view;
    snprintf(a->folder.path, sizeof a->folder.path, "%s",
             tiku_trk_poseview_path(a->view));
    if (tiku_trk_folder_file_actions_invoke(&a->file_actions, &a->folder,
                                            command)) {
        /* No reload here: what a command changed arrives as change notices
         * and through after_op when the engine goes idle, as it does in
         * the other shell.  A reload at this point cleared the selection
         * the command left behind -- the cut it had just tinted, and the
         * rows the modal delete question re-reads when it is answered
         * (FS-046, FS-060). */
        a->dirty = 1;
        return;
    }

    template_path = tiku_trk_cwmenu_template_path(&a->menus, command);
    if (template_path != NULL) {
        char made[TIKU_PATH_MAX], error[200];
        const char *slash;
        int row;

        if (tiku_fs_new_from_template(
                a->fs, template_path, tiku_trk_poseview_path(a->view),
                made, sizeof made, error, sizeof error) == 0) {
            slash = strrchr(made, '/');
            (void)tiku_trk_poseview_reload(a->view);
            row = index_of_path(a, made);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                begin_rename(a);
            }
            snprintf(a->status, sizeof a->status, "created '%.180s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(a->status, sizeof a->status, "%.200s",
                     error[0] != '\0' ? error : "could not create from template");
        }
        a->dirty = 1;
        return;
    }
    template_parent = tiku_trk_cwmenu_template_add_path(&a->menus, command);
    if (template_parent != NULL) {
        char made[TIKU_PATH_MAX], error[200];

        if (tiku_fs_new_folder(a->fs, template_parent, made,
                                   sizeof made, error, sizeof error) == 0) {
            const char *root = tiku_trk_cwmenu_templates_root(&a->menus);

            snprintf(a->status, sizeof a->status,
                     "created template submenu '%.150s'", strrchr(made, '/')
                     != NULL ? strrchr(made, '/') + 1 : made);
            if (root != NULL && navigate_to(a, template_parent, 1)) {
                int row = index_of_path(a, made);

                if (row >= 0) {
                    tiku_trk_poseview_click(a->view, row, 0u);
                }
            }
        } else {
            snprintf(a->status, sizeof a->status, "%.200s",
                     error[0] != '\0' ? error :
                     "could not create template submenu");
        }
        a->dirty = 1;
        return;
    }
    if (command == TIKU_TRK_CMD_EDIT_TEMPLATES) {
        const char *root = tiku_trk_cwmenu_templates_root(&a->menus);

        if (root != NULL) {
            (void)navigate_to(a, root, 1);
        }
        a->dirty = 1;
        return;
    }
    switch (command) {
    case TIKU_TRK_CMD_COPY_LAYOUT:
        (void)tiku_trk_poseview_copy_layout(a->view);
        a->dirty = 1;
        return;
    case TIKU_TRK_CMD_PASTE_LAYOUT:
        if (tiku_trk_poseview_paste_layout(a->view) > 0) {
            (void)tiku_trk_poseview_save_columns(a->view);
            snprintf(a->status, sizeof a->status, "pasted layout");
        }
        a->dirty = 1;
        return;
    case TIKU_TRK_CMD_NEW_FOLDER:
        menu_key(a, 'n', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_OPEN:
        menu_key(a, TIKU_KEY_RETURN, 0u); return;
    case TIKU_TRK_CMD_EDIT_NAME:
        menu_key(a, TIKU_KEY_F2, 0u); return;
    case TIKU_TRK_CMD_DUPLICATE:
        menu_key(a, 'd', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        menu_key(a, TIKU_KEY_DELETE, 0u); return;
    case TIKU_TRK_CMD_DELETE:
        menu_key(a, TIKU_KEY_DELETE, TIKU_MOD_SHIFT); return;
    case TIKU_TRK_CMD_SELECT_ALL:
        menu_key(a, 'a', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_INVERT_SELECTION:
        menu_key(a, 's', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_OPEN_PARENT:
        menu_key(a, TIKU_KEY_UP, TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_ICON_VIEW:
        menu_key(a, '1', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
        menu_key(a, '2', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_LIST_VIEW:
        menu_key(a, '3', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_CLEANUP:
        menu_key(a, 'k', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_CLEANUP_ALL:
        menu_key(a, 'k', TIKU_MOD_CMD | TIKU_MOD_SHIFT); return;
    case TIKU_TRK_CMD_CLOSE:
    case TIKU_TRK_CMD_CLOSE_ALL:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE:
        a->running = 0;
        return;
    case TIKU_TRK_CMD_ICON_SIZE_32:  field = 32; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_40:  field = 40; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_48:  field = 48; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_64:  field = 64; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_96:  field = 96; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_128: field = 128; goto icon_size;
    case TIKU_TRK_CMD_SORT_BY_NAME:  field = TIKU_TRK_COL_NAME; break;
    case TIKU_TRK_CMD_SORT_BY_KIND:  field = TIKU_TRK_COL_KIND; break;
    case TIKU_TRK_CMD_SORT_BY_SIZE:  field = TIKU_TRK_COL_SIZE; break;
    case TIKU_TRK_CMD_SORT_BY_VALUE: field = TIKU_TRK_COL_VALUE; break;
    case TIKU_TRK_CMD_SORT_BY_CAP:   field = TIKU_TRK_COL_CAP; break;
    case TIKU_TRK_CMD_REVERSE_ORDER:
        field = tiku_trk_poseview_sort_field(a->view);
        break;
    case TIKU_TRK_CMD_COL_KIND:  field = TIKU_TRK_COL_KIND; goto column;
    case TIKU_TRK_CMD_COL_VALUE: field = TIKU_TRK_COL_VALUE; goto column;
    case TIKU_TRK_CMD_COL_SIZE:  field = TIKU_TRK_COL_SIZE; goto column;
    case TIKU_TRK_CMD_COL_CAP:   field = TIKU_TRK_COL_CAP; goto column;
    case TIKU_TRK_CMD_COL_DATE:  field = TIKU_TRK_COL_DATE; goto column;
    case TIKU_TRK_CMD_COL_PATH:  field = TIKU_TRK_COL_PATH; goto column;
    case TIKU_TRK_CMD_COL_PERM:  field = TIKU_TRK_COL_PERM; goto column;
    case TIKU_TRK_CMD_COL_ORIGINAL: field = TIKU_TRK_COL_ORIGINAL; goto column;
    case TIKU_TRK_CMD_FIND:
    case TIKU_TRK_CMD_GET_INFO:
    case TIKU_TRK_CMD_PREFERENCES:
        snprintf(a->status, sizeof a->status, "%s is not available in this shell",
                 tiku_trk_sc_label(command, 0u));
        break;
    default:
        break;
    }
    if (field >= 0) {
        tiku_trk_poseview_sort_by(a->view, field);
    }
    settle_layout(a);
    (void)tiku_trk_poseview_save_state(a->view);
    a->dirty = 1;
    return;

column:
    (void)tiku_trk_poseview_toggle_column(a->view, field);
    (void)tiku_trk_poseview_save_columns(a->view);
    a->dirty = 1;
    return;

icon_size:
    tiku_trk_poseview_set_mode(a->view, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(a->view, field);
    settle_layout(a);
    (void)tiku_trk_poseview_save_state(a->view);
    a->dirty = 1;
}

/** @brief Open a title from the in-window bar when the point names one. */
static int
menubar_press(app_t *a, int x, int y, unsigned state)
{
    tiku_rect_t bar = menubar_rect(a);
    tiku_trk_sc_ctx_t c;
    unsigned mods = 0u;
    int which;

    which = tiku_trk_menubar_title_at(&a->menus.bar, bar, x, y);
    if (which < 0) {
        return 0;
    }
    if (state & TIKU_MOD_SHIFT) { mods |= TIKU_TRK_MENU_MOD_SHIFT; }
    if (state & TIKU_MOD_CMD) { mods |= TIKU_TRK_MENU_MOD_CMD; }
    c = menu_ctx(a, mods);
    tiku_trk_cwmenu_rebuild(&a->menus, &c);
    a->bar_open = which;
    a->menus.bar.open = which;
    a->popup = a->menus.bar.menu[which];
    a->popup_sub = NULL;
    a->popup_frame = tiku_trk_menubar_panel(&a->menus.bar, bar,
                         (tiku_rect_t){ 0, 0, a->surf->w, a->surf->h });
    a->dirty = 1;
    return 1;
}

/** @brief Route a press through an open menu or its first submenu. */
static int
popup_press(app_t *a, int x, int y)
{
    tiku_trk_menu_t *m;
    tiku_rect_t frame;
    int item;

    if (a->popup == NULL) {
        return 0;
    }
    m = (a->popup_sub != NULL) ? a->popup_sub : a->popup;
    frame = (a->popup_sub != NULL) ? a->popup_sub_frame : a->popup_frame;
    item = tiku_trk_menu_item_at(m, frame, x, y);
    if (item >= 0 && m->item[item].enabled) {
        if (m->item[item].submenu != NULL) {
            m->open_index = item;
            a->popup_sub = m->item[item].submenu;
            a->popup_sub_frame = tiku_trk_menu_place_sub(
                m, item, frame,
                (tiku_rect_t){ 0, 0, a->surf->w, a->surf->h });
            a->dirty = 1;
            return 1;
        }
        menu_invoke(a, m->item[item].command);
    }
    a->popup = NULL;
    a->popup_sub = NULL;
    a->bar_open = -1;
    a->menus.bar.open = -1;
    a->dirty = 1;
    return 1;
}

/** @brief Build the pose/background context menu at a completed click. */
static void
open_context_menu(app_t *a, int row, int x, int y, unsigned state)
{
    tiku_trk_pose_t *p = (row >= 0)
        ? tiku_trk_poseview_at(a->view, row) : NULL;
    unsigned mods = 0u;
    tiku_trk_sc_ctx_t c;

    if (state & TIKU_MOD_SHIFT) { mods |= TIKU_TRK_MENU_MOD_SHIFT; }
    if (state & TIKU_MOD_CMD) { mods |= TIKU_TRK_MENU_MOD_CMD; }
    c = menu_ctx(a, mods);
    tiku_trk_cwmenu_rebuild(&a->menus, &c);
    a->popup = tiku_trk_cwmenu_context(&a->menus,
                                       (p != NULL) ? &p->model : NULL, &c);
    a->popup_sub = NULL;
    a->bar_open = -1;
    a->menus.bar.open = -1;
    a->popup_frame = tiku_trk_menu_place(a->popup, x, y,
        (tiku_rect_t){ 0, 0, a->surf->w, a->surf->h });
    a->dirty = 1;
}

/** @brief Route a click inside the browser navigator. */
static int
navigator_press(app_t *a, int x, int y)
{
    tiku_rect_t nav = navigator_rect(a);
    tiku_rect_t back, forward, up, field;

#define INSIDE(r) (x >= (r).x && x < (r).x + (r).w && \
                   y >= (r).y && y < (r).y + (r).h)
    if (!a->show_nav || !INSIDE(nav)) {
        return 0;
    }
    navigator_layout(a, &back, &forward, &up, &field);
    if (INSIDE(field)) {
        begin_location(a);
    } else {
        end_location(a, 0);
        if (INSIDE(back) && tiku_trk_navigator_can_back(&a->nav)) {
            navigate_back(a);
        } else if (INSIDE(forward) &&
                   tiku_trk_navigator_can_forward(&a->nav)) {
            navigate_forward(a);
        } else if (INSIDE(up) &&
                   strcmp(tiku_trk_navigator_current(&a->nav), "/") != 0) {
            navigate_up(a);
        }
    }
    a->dirty = 1;
#undef INSIDE
    return 1;
}

static int
tracker_run(int argc, char **argv)
{
    app_t a;
    const char *port = NULL, *start = NULL;
    int baud = 115200, use_device = 0, i;
    int64_t deadline_us = 0;
    tiku_trk_click_t click = { 0 };
    const char *shot = NULL;
    const char *script_path = NULL;
    tiku_trk_script_t *sc = NULL;
    int rc = 0;
    int64_t shot_at_us = 0;

    memset(&a, 0, sizeof a);
    (void)tiku_strings_init(NULL, NULL);
    tiku_trk_cwmenu_init(&a.menus);
    a.bar_open = -1;
    a.show_nav = 1;
    /* Zero is a valid row, so the "nothing to select" state has to be set
     * rather than zeroed: left at 0 it would re-select the first row after
     * every operation (FS-050). */
    a.select_next = -1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0)                      { use_device = 1; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { port = argv[++i]; use_device = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { shot = argv[++i]; }
        else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc) { script_path = argv[++i]; }
        else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) { deadline_us = tiku_trk_now() + (int64_t)atoi(argv[++i]) * 1000000; }
        else                                                 { start = argv[i]; }
    }

    if (use_device) {
        a.backend = tiku_backend_tiku_open(port, baud);
        if (a.backend == NULL) {
            fprintf(stderr, "tracker: no device answered\n");
            return 1;
        }
        if (start == NULL) { start = "/"; }
    } else {
        a.backend = tiku_backend_posix_open();
        if (start == NULL) { start = getenv("HOME"); }
        if (start == NULL) { start = "/"; }
    }

    tiku_trk_menu_set_icon_painter(paint_menu_icon);
    a.loop = tiku_trk_loop_new();
    tiku_trk_scroll_init(&a.sb, 1);
    tiku_trk_scroll_init(&a.hsb, 20);
    a.view = tiku_trk_poseview_new(a.backend, start);
    if (a.view != NULL) {
        (void)tiku_trk_poseview_restore_state(a.view);
    }
    if (a.view == NULL) {
        fprintf(stderr, "tracker: cannot open %s\n", start);
        return 1;
    }
    tiku_trk_navigator_init(&a.nav, tiku_trk_poseview_path(a.view));
    a.fs = tiku_fs_new(a.backend);
    (void)tiku_fs_prepare_trash(a.fs);
    tiku_trk_clip_init(&a.clip);
    a.surf = tiku_surface_new(WIN_W, WIN_H, TIKU_C_PANEL);
    tiku_trk_status_overlay_init(&a.status_overlay, a.surf, WIN_W, WIN_H,
                                 &a.dirty, app_alert_answer, app_alert_stop,
                                 &a);
    /* No preferences window in this shell, but the command layer now has
     * somewhere to ASK: the overlay's alert, so the delete question and a
     * collision are put up here exactly as the desktop puts them up
     * (FS-046, FEATURES rule 6). */
    tiku_trk_folder_file_actions_init(&a.file_actions, a.fs, a.backend,
        &a.clip, NULL, NULL,
        tiku_trk_status_overlay_alert(&a.status_overlay), NULL, NULL,
        a.status, sizeof a.status, NULL, NULL);
    /* Derived from the live face, not a constant: the row grows with
     * the UI font the way the original's font metric did (IV-013). */
    a.row_h = tiku_font_plain()->height + 5;
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(a.prefs_path, sizeof a.prefs_path,
                     "%s/.config/TrackerSettings", home);
        }
        sync_navigator_preference(&a, 1);
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
        a.host = tiku_host_open(WIN_W, WIN_H, "Tracker");
        if (a.host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
        if (tiku_host_scale(a.host) > 1) {
            (void)tiku_surface_rescale(a.surf,
                tiku_host_scale(a.host), TIKU_C_PANEL);
        }
    }
    /* The window measures its own labels, which is what lets a pose's rect
     * grow to fit a long name instead of staying icon-sized (PVL-065), and
     * what "fit this column to its widest value" is measured with. */
    tiku_trk_poseview_set_measure(a.view, measure_text,
                                  (void *)tiku_font_plain());
    tiku_trk_poseview_set_font_size(a.view,
                                    tiku_font_plain()->height);
    /* And the metrics the icon-mode CELLS are derived from, which is a
     * different question from the width columns are scaled against. */
    tiku_trk_poseview_set_font_metrics(a.view, tiku_font_plain()->height,
                                       tiku_font_plain()->ascent);
    (void)tiku_trk_poseview_restore_columns(a.view);
    tiku_trk_titleview_init(&a.tv, a.view, TITLE_H);
    settle_layout(&a);
    settle_writability(&a);
    retitle(&a);
    if (getenv("TRK_DEMO_OPS") != NULL) {
        /* Scripted: select everything and duplicate, so the capture shows a
         * real operation and its result. */
        char paths[64][TIKU_PATH_MAX];
        const char *sel[64];
        int n;

        tiku_trk_poseview_select_all(a.view);
        n = selection_paths(&a, paths, sel, 64);
        if (n > 0) {
            (void)tiku_fs_duplicate(a.fs, sel, n);
            snprintf(a.status, sizeof a.status, "duplicating %d items", n);
        }
    }
    if (getenv("TRK_DEMO_ICON") != NULL) {
        /* Scripted: icon mode, auto-place, then move two icons the way a
         * user would drag them, so the capture shows real spatial state. */
        tiku_rect_t lr = list_rect(&a);
        tiku_trk_scroll_detach(&a.sb);
        tiku_trk_poseview_set_mode(a.view, TIKU_TRK_MODE_ICON);
        tiku_trk_scroll_attach(&a.sb);
        (void)tiku_trk_poseview_place_unplaced(a.view, lr.w - 4);
        {
            tiku_trk_pose_t *p0 = tiku_trk_poseview_at(a.view, 0);
            tiku_trk_pose_t *p1 = tiku_trk_poseview_at(a.view, 1);
            if (p0 != NULL) { tiku_trk_poseview_move_pose(a.view, p0, 300, 180, 1); }
            if (p1 != NULL) { tiku_trk_poseview_move_pose(a.view, p1, 300, 260, 1); p1->selected = 1; }
        }
        (void)tiku_trk_poseview_save_state(a.view);
    }
    if (getenv("TRK_DEMO_MINI") != NULL) {
        tiku_rect_t lr = list_rect(&a);

        tiku_trk_scroll_detach(&a.sb);
        tiku_trk_poseview_set_mode(a.view, TIKU_TRK_MODE_MINI);
        tiku_trk_scroll_attach(&a.sb);
        (void)tiku_trk_poseview_place_unplaced(a.view, lr.w - 4);
        tiku_trk_poseview_click(a.view, 2, 0);
    }
    if (getenv("TRK_DEMO_COLS") != NULL) {
        /* Scripted: a resize in progress, so the capture shows the guide
         * line standing where the column will fall, and the pressed title. */
        int r = 0;

        (void)tiku_trk_titleview_bounds(&a.tv, TIKU_TRK_COL_NAME, NULL, &r);
        (void)tiku_trk_titleview_mouse_down(&a.tv, r, 9, 1, 1,
                                            tiku_trk_now());
        (void)tiku_trk_titleview_mouse_moved(&a.tv, r - 90, 9,
                                             tiku_trk_now() +
                                             2 * TIKU_TRK_CLICK_SPEED_US);
    }
    if (getenv("TRK_DEMO_FILTER") != NULL) {
        /* Scripted: two filter terms and a selection, so a capture shows the
         * count view carrying "a|m" and the rows that match both. */
        tiku_trk_poseview_set_filtering(a.view, 1);
        tiku_trk_poseview_type_ahead(a.view, 'e');
        (void)tiku_trk_poseview_filter_term(a.view);
        tiku_trk_poseview_type_ahead(a.view, 's');
        tiku_trk_poseview_click(a.view, 0, 0);
    }
    if (getenv("TRK_DEMO_RENAME") != NULL) {
        /* Scripted: the inline editor open on a row, with the whole name
         * selected as it is when it opens. */
        tiku_trk_poseview_click(a.view, 1, 0);
        begin_rename(&a);
    }
    if (getenv("TRK_DEMO_SELECT") != NULL) {
        /* Scripted interaction for a reviewable screenshot: select a range
         * the way a user would, without needing someone at the keyboard. */
        tiku_trk_poseview_click(a.view, 16, 0);
        tiku_trk_poseview_click(a.view, 18, TIKU_TRK_MOD_SHIFT);
    }
    a.running = 1;
    a.dirty = 1;
    a.active = 1;
    if (shot != NULL) {
        /* Give the first values time to arrive before capturing. */
        shot_at_us = tiku_trk_now() + 1500000;
    }

    while (a.running) {
        sync_navigator_preference(&a, 0);
        /* Events first, then the device: the window stays responsive while a
         * slow link is being read, which is the whole point of pumping here
         * rather than blocking on either one. */
        for (;;) {
            tiku_event_t ev;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (sc != NULL) {
                /* One instruction per turn, so the deferred work between
                 * turns -- a stepped copy, the armed rename -- happens
                 * where it happens on a real clock. */
                tiku_trk_script_step_t sp;
                tiku_trk_script_state_t sst;

                memset(&sst, 0, sizeof sst);
                sst.status = a.status;
                sst.title = a.title;
                sst.alert =
                    tiku_trk_status_overlay_alert(&a.status_overlay)->open
                        ? tiku_trk_status_overlay_alert(&a.status_overlay)
                              ->text
                        : NULL;
                sst.scroll = tiku_trk_poseview_scroll(a.view);
                sst.rows = tiku_trk_poseview_count(a.view);
                {
                    static char script_count[240];

                    count_label(&a, list_rect(&a).w, script_count,
                                sizeof script_count);
                    sst.count = script_count;
                }
                sst.renaming = a.editing;
                sst.location = tiku_trk_navigator_current(&a.nav);
                sst.nav_back = tiku_trk_navigator_can_back(&a.nav);
                sst.nav_forward = tiku_trk_navigator_can_forward(&a.nav);
                sst.nav_up = strcmp(sst.location, "/") != 0;
                sst.nav_visible = a.show_nav;
                sst.viewport_rows = visible_rows(&a);
                {
                    const tiku_trk_column_t *script_cols = NULL;

                    sst.columns = tiku_trk_poseview_columns(a.view,
                                                            &script_cols);
                }
                {
                    static char labels[512];
                    tiku_trk_menu_t *open = (a.popup_sub != NULL)
                                                  ? a.popup_sub : a.popup;
                    int mi;

                    labels[0] = '\0';
                    for (mi = 0; open != NULL && mi < open->count; mi++) {
                        size_t at = strlen(labels);

                        snprintf(labels + at, sizeof labels - at, "%s|",
                                 open->item[mi].label);
                    }
                    sst.menu = labels;
                }
                {
                    int si, sn = tiku_trk_poseview_count(a.view);

                    for (si = 0; si < sn; si++) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_at(a.view, si);

                        if (p != NULL && p->selected) {
                            if (sst.sel_name == NULL) {
                                sst.sel_name = p->model.name;
                            }
                            sst.selected++;
                        }
                    }
                }
                sst.row_point = script_row_point;
                sst.pose_at = script_pose_at;
                sst.pose_screen = script_pose_screen;
                sst.pixel_at = script_pixel_at;
                sst.pose_ctx = &a;
                if (!tiku_trk_script_pump(sc, &sst, &sp)) {
                    a.running = 0;
                    break;
                }
                switch (sp.what) {
                case TIKU_TRK_SC_KEY:
                    ev.type = TIKU_EVENT_KEY_DOWN;
                    ev.key = sp.key;
                    ev.modifiers = sp.modifiers;
                    if (sp.key >= 0x20u && sp.key < 0x7fu) {
                        ev.text[0] = (char)sp.key;
                        if ((sp.modifiers & TIKU_MOD_SHIFT) != 0u &&
                            ev.text[0] >= 'a' && ev.text[0] <= 'z') {
                            ev.text[0] = (char)(ev.text[0] - 'a' + 'A');
                        }
                    }
                    have = 1;
                    break;
                case TIKU_TRK_SC_MOVE:
                    ev.type = TIKU_EVENT_POINTER_MOVE;
                    ev.x = sp.x;
                    ev.y = sp.y;
                    ev.modifiers = sp.modifiers;
                    have = 1;
                    break;
                case TIKU_TRK_SC_PRESS:
                case TIKU_TRK_SC_RELEASE:
                    ev.type = (sp.what == TIKU_TRK_SC_PRESS)
                                  ? TIKU_EVENT_POINTER_DOWN
                                  : TIKU_EVENT_POINTER_UP;
                    ev.x = sp.x;
                    ev.y = sp.y;
                    ev.modifiers = sp.modifiers;
                    ev.button = sp.button;
                    ev.time_us = tiku_trk_now();
                    have = 1;
                    break;
                case TIKU_TRK_SC_SHOT:
                    draw(&a);
                    (void)tiku_surface_png(a.surf, sp.path);
                    break;
                case TIKU_TRK_SC_MENU_PICK: {
                    tiku_trk_menu_t *m = (a.popup_sub != NULL)
                                             ? a.popup_sub : a.popup;
                    tiku_rect_t fr = (a.popup_sub != NULL)
                                              ? a.popup_sub_frame
                                              : a.popup_frame;
                    int k;

                    if (m == NULL) {
                        fprintf(stderr, "script: no menu is open for '%s'\n",
                                sp.path);
                        break;
                    }
                    for (k = 0; k < m->count; k++) {
                        if (!m->item[k].separator &&
                            strstr(m->item[k].label, sp.path) != NULL) {
                            ev.type = TIKU_EVENT_POINTER_DOWN;
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
                    break;
                }
                case TIKU_TRK_SC_QUIT:
                    a.running = 0;
                    break;
                default:
                    break;
                }
            } else if (tiku_host_poll(a.host, &ev)) {
                have = 1;
            }
            if (!have) {
                break;
            }

            switch (ev.type) {
            case TIKU_EVENT_RESIZE:
                if (ev.width > 0 && ev.height > 0 &&
                    (ev.width != a.surf->w || ev.height != a.surf->h)) {
                    (void)tiku_surface_resize(a.surf, ev.width,
                                                   ev.height,
                                                   TIKU_C_PANEL);
                    /* The overlay centres its alert on these; stale ones
                     * would put the question off a shrunken window. */
                    a.status_overlay.screen_w = ev.width;
                    a.status_overlay.screen_h = ev.height;
                    {
                        /* Both axes re-anchor to the new window: the
                         * vertical clamp always ran through visible_rows,
                         * and the horizontal one runs here (PVL-058). */
                        tiku_rect_t lr = list_rect(&a);

                        tiku_trk_poseview_set_viewport(a.view, lr.w - 4,
                                                       lr.h - 4);
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_poseview_hscroll(a.view));
                    }
                }
                a.dirty = 1;
                break;
            case TIKU_EVENT_EXPOSE:
                a.dirty = 1;
                break;
            case TIKU_EVENT_KEY_DOWN:
                tiku_trk_loop_note_activity(a.loop);
                handle_key(&a, &ev);
                break;
            case TIKU_EVENT_POINTER_DOWN: {
                tiku_trk_loop_note_activity(a.loop);
                if (tiku_trk_status_overlay_pointer(&a.status_overlay,
                                                    ev.x, ev.y)) {
                    /* A modal alert eats the click wherever it lands. */
                    a.dirty = 1;
                    break;
                }
                if (ev.button == 1 &&
                    menubar_press(&a, ev.x, ev.y,
                                  ev.modifiers)) {
                    end_rename(&a, 1);
                    break;
                }
                if (popup_press(&a, ev.x, ev.y)) {
                    break;
                }
                if (ev.button == 1 &&
                    navigator_press(&a, ev.x, ev.y)) {
                    break;
                }
                if (ev.button == 4 || ev.button == 5) {
                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        /* TS-020: one value moves the titles and the rows
                         * together, because they read the same one. */
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_poseview_hscroll(a.view) +
                            ((ev.button == 4) ? -24 : 24));
                    } else {
                        tiku_trk_poseview_scroll_to(a.view,
                            tiku_trk_poseview_scroll(a.view) +
                            ((ev.button == 4) ? -3 : 3));
                    }
                    a.dirty = 1;
                    break;
                }
                if (ev.x >= a.sb_rect.x &&
                    ev.x < a.sb_rect.x + a.sb_rect.w &&
                    ev.y >= a.sb_rect.y &&
                    ev.y < a.sb_rect.y + a.sb_rect.h) {
                    /* The bar takes it: an ARROW steps half a line, a
                     * press in the track pages or takes hold of the
                     * thumb, and while it is held the view follows the
                     * bar rather than the other way round (PVL-051). */
                    int at = ev.y - a.sb_rect.y;
                    int btn = a.sb_rect.w;

                    if (at < btn) {
                        tiku_trk_scroll_arrow_press(&a.sb, -1);
                        tiku_trk_poseview_scroll_to(a.view,
                            tiku_trk_poseview_scroll(a.view) -
                                tiku_trk_scroll_line(&a.sb));
                    } else if (at >= a.sb_rect.h - btn) {
                        tiku_trk_scroll_arrow_press(&a.sb, 1);
                        tiku_trk_poseview_scroll_to(a.view,
                            tiku_trk_poseview_scroll(a.view) +
                                tiku_trk_scroll_line(&a.sb));
                    } else {
                        tiku_trk_poseview_scroll_to(a.view,
                            tiku_trk_scroll_press(&a.sb,
                                a.sb_rect.h - 2 * btn, at - btn));
                    }
                    a.dirty = 1;
                    break;
                }
                if (a.viewbtn_rect.w > 0 && ev.x >= a.viewbtn_rect.x &&
                    ev.x < a.viewbtn_rect.x + a.viewbtn_rect.w &&
                    ev.y >= a.viewbtn_rect.y &&
                    ev.y < a.viewbtn_rect.y + a.viewbtn_rect.h) {
                    /* The gutter's view-mode popup (NeXT UIG ch7). */
                    tiku_trk_sc_ctx_t vc = menu_ctx(&a, 0u);

                    a.popup = tiku_trk_cwmenu_view_popup(&a.menus, &vc);
                    a.popup_sub = NULL;
                    a.popup_frame = tiku_trk_menu_place(a.popup,
                        a.viewbtn_rect.x, a.viewbtn_rect.y,
                        (tiku_rect_t){ 0, 0, a.surf->w, a.surf->h });
                    a.dirty = 1;
                    break;
                }
                if (a.hsb_rect.w > 0 && ev.x >= a.hsb_rect.x &&
                    ev.x < a.hsb_rect.x + a.hsb_rect.w &&
                    ev.y >= a.hsb_rect.y &&
                    ev.y < a.hsb_rect.y + a.hsb_rect.h) {
                    /* The horizontal bar, sharing the count row (TS-044):
                     * the same arrows, track and thumb, sideways. */
                    int at = ev.x - a.hsb_rect.x;
                    int btn = a.hsb_rect.h;

                    if (at < btn) {
                        tiku_trk_scroll_arrow_press(&a.hsb, -1);
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_poseview_hscroll(a.view) -
                                tiku_trk_scroll_line(&a.hsb));
                    } else if (at >= a.hsb_rect.w - btn) {
                        tiku_trk_scroll_arrow_press(&a.hsb, 1);
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_poseview_hscroll(a.view) +
                                tiku_trk_scroll_line(&a.hsb));
                    } else {
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_scroll_press(&a.hsb,
                                a.hsb_rect.w - 2 * btn, at - btn));
                    }
                    a.dirty = 1;
                    break;
                }
                {
                    int cx = 0, cy = 0;

                    if (title_hit(&a, ev.x, ev.y, &cx,
                                  &cy)) {
                        int btn = (ev.button == 3) ? 2
                                : (ev.button == 2) ? 3 : 1;

                        /* TS-015: whatever the press turns out to mean, an
                         * edit in progress is committed first -- the strip
                         * is outside the row being renamed. */
                        end_rename(&a, 1);
                        tiku_trk_poseview_cancel_edit_wait(a.view);
                        {
                            unsigned tf = tiku_trk_titleview_mouse_down(
                                &a.tv, cx, cy, btn, a.active,
                                tiku_trk_now());

                            if ((tf & TIKU_TRK_TT_MENU) != 0u) {
                                tiku_trk_sc_ctx_t tc = menu_ctx(&a, 0u);

                                tiku_trk_cwmenu_rebuild(&a.menus, &tc);
                                a.bar_open = -1;
                                a.menus.bar.open = -1;
                                a.popup_sub = NULL;
                                a.popup = &a.menus.attributes;
                                a.popup_frame = tiku_trk_menu_place(
                                    a.popup, ev.x, ev.y,
                                    (tiku_rect_t){ 0, 0, a.surf->w,
                                                        a.surf->h });
                            }
                        }
                        a.dirty = 1;
                        break;
                    }
                }
                if (tiku_trk_poseview_mode(a.view) != TIKU_TRK_MODE_LIST) {
                    tiku_rect_t lr = list_rect(&a);
                    int lx = ev.x - lr.x, ly = ev.y - lr.y;
                    tiku_trk_pose_t *hit =
                        tiku_trk_poseview_pose_at(a.view, lx, ly);
                    unsigned mods = 0;
                    unsigned click_flags = 0;
                    int64_t now = tiku_trk_now();

                    if (ev.modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                    if (ev.modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        click_flags |= TIKU_TRK_CLICK_EXTEND;
                    }
                    if (ev.modifiers & TIKU_MOD_CMD) {
                        click_flags |= TIKU_TRK_CLICK_EXTEND |
                                       TIKU_TRK_CLICK_CONTROL;
                    }

                    if (hit != NULL) {
                        int idx = -1, k, cnt = tiku_trk_poseview_count(a.view);
                        int twice;
                        for (k = 0; k < cnt; k++) {
                            if (tiku_trk_poseview_at(a.view, k) == hit) { idx = k; break; }
                        }
                        if (ev.button == 3u) {
                            tiku_trk_poseview_cancel_edit_wait(a.view);
                            (void)tiku_trk_poseview_context_click(a.view,
                                                                  idx);
                            a.press_at = now;
                            a.press_x = ev.x;
                            a.press_y = ev.y;
                            a.press_row = idx;
                            a.press_down = 1;
                            (void)tiku_trk_click_press(&click, NULL, 0, 0,
                                                       3u, 0u, now);
                            a.dirty = 1;
                            break;
                        }
                        twice = tiku_trk_click_press(&click, hit,
                            ev.x, ev.y, ev.button,
                            click_flags, now);
                        tiku_trk_poseview_click_at(a.view, idx, lx, ly, mods);
                        if (twice) {
                            open_selection(&a);
                        } else {
                            /* A press on an icon begins a move; the drag
                             * carries the whole selection, as Tracker's does. */
                            {
                                int k, cnt = tiku_trk_poseview_count(a.view);

                                for (k = 0; k < cnt; k++) {
                                    tiku_trk_pose_t *q =
                                        tiku_trk_poseview_at(a.view, k);

                                    if (q != NULL && q->selected) {
                                        tiku_trk_poseview_pose_location(
                                            a.view, q, &q->drag_x0,
                                            &q->drag_y0);
                                    }
                                }
                            }
                            a.drag_active = 1;
                            a.drag_x = lx;
                            a.drag_y = ly;
                        }
                    } else {
                        (void)tiku_trk_click_press(&click, NULL, 0, 0,
                                                   ev.button, 0u,
                                                   now);
                        if (ev.button == 3u) {
                            /* The background asks for its own menu; the
                             * band is a selection gesture and belongs to
                             * the primary button (CW-042). */
                            tiku_trk_poseview_cancel_edit_wait(a.view);
                            a.press_at = now;
                            a.press_x = ev.x;
                            a.press_y = ev.y;
                            a.press_row = -1;
                            a.press_down = 1;
                            a.dirty = 1;
                            break;
                        }
                        /* Empty space starts a rubber band.  Without a
                         * modifier the selection is cleared FIRST, so the
                         * band's toggle rule starts from nothing; with one
                         * the existing selection is kept and the sweep
                         * toggles on top of it (PVS-027). */
                        if (!(mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD))) {
                            tiku_trk_poseview_select_none(a.view);
                        }
                        tiku_trk_poseview_cancel_edit_wait(a.view);
                        tiku_trk_poseview_set_band(a.view, 1);
                        a.band_active = 1;
                        a.band_x0 = a.band_x1 = ev.x;
                        a.band_y0 = a.band_y1 = ev.y;
                    }
                    a.dirty = 1;
                    break;
                }
                {
                    if (ev.button == 1u && count_at(&a, ev.x, ev.y)) {
                        /* This shell has one content surface, so choosing
                         * the first hierarchy entry is represented by the
                         * immediate-parent navigation action. */
                        navigate_up(&a);
                        a.dirty = 1;
                        break;
                    }
                    int row = row_at(&a, ev.x, ev.y);
                    unsigned mods = 0;
                    unsigned click_flags = 0;
                    int64_t now = tiku_trk_now();
                    int on_name = 0;
                    tiku_trk_pose_t *pose = (row >= 0)
                        ? tiku_trk_poseview_at(a.view, row) : NULL;
                    int twice;

                    if (ev.modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                    if (ev.modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        click_flags |= TIKU_TRK_CLICK_EXTEND;
                    }
                    if (ev.modifiers & TIKU_MOD_CMD) {
                        click_flags |= TIKU_TRK_CLICK_EXTEND |
                                       TIKU_TRK_CLICK_CONTROL;
                    }
                    if (ev.button == 3) {
                        /* PVS-015: a right-click disarms a pending rename;
                         * PVS-018: and normalizes the selection first. */
                        tiku_trk_poseview_cancel_edit_wait(a.view);
                        (void)tiku_trk_poseview_context_click(a.view, row);
                        a.press_at = now;
                        a.press_x = ev.x;
                        a.press_y = ev.y;
                        a.press_row = row;
                        a.press_down = 1;
                        (void)tiku_trk_click_press(&click, NULL, 0, 0, 3u,
                                                   0u, now);
                        a.dirty = 1;
                        break;
                    }
                    twice = tiku_trk_click_press(&click, pose,
                        ev.x, ev.y, ev.button,
                        click_flags, now);
                    /* Whether the press landed on the NAME, which is the
                     * only widget a rename can start from. */
                    {
                        const tiku_trk_column_t *cs;
                        int nn = tiku_trk_poseview_columns(a.view, &cs);
                        int cx = BORDER, k;

                        for (k = 0; k < nn; k++) {
                            if (ev.x >= cx &&
                                ev.x < cx + cs[k].width) {
                                on_name = (cs[k].field == TIKU_TRK_COL_NAME);
                                break;
                            }
                            cx += cs[k].width;
                        }
                    }
                    {
                        tiku_rect_t lr = list_rect(&a);
                        int ox = 0, oy = 0;

                        tiku_trk_poseview_origin(a.view, &ox, &oy);
                        tiku_trk_poseview_click_at(a.view, row,
                            ev.x - lr.x + ox,
                            ev.y - lr.y + oy, mods);
                    }
                    /* PVS-010: a strict second click on this same pose
                     * opens it. */
                    if (twice) {
                        tiku_rect_t lr = list_rect(&a);
                        tiku_rect_t from;

                        {
                            int ax = 0, ay = 0, aw = 0, ah = 0;
                            int ox = 0, oy = 0;

                            if (tiku_trk_poseview_open_anim_rect(a.view,
                                    row, a.row_h, &ax, &ay, &aw, &ah)) {
                                tiku_trk_poseview_origin(a.view, &ox, &oy);
                                from.x = lr.x + ax - ox;
                                from.y = lr.y + ay - oy;
                                from.w = aw;
                                from.h = ah;
                                open_animation(&a, from);
                            }
                        }
                        {
                            /* PVS-011: a double click INSIDE the Location
                             * column opens the folder the path names --
                             * the column's answer, not the row's. */
                            const tiku_trk_column_t *cols = NULL;
                            int nc = tiku_trk_poseview_columns(a.view,
                                                               &cols);
                            int j, in_path = 0;

                            for (j = 0; j < nc; j++) {
                                int cx = col_x(&a, &cols[j]);

                                if (cols[j].field == TIKU_TRK_COL_PATH &&
                                    ev.x >= cx &&
                                    ev.x < cx + cols[j].width) {
                                    in_path = 1;
                                }
                            }
                            if (in_path && pose != NULL) {
                                char parent[TIKU_PATH_MAX];
                                char *slash;

                                snprintf(parent, sizeof parent, "%s",
                                         pose->model.path);
                                slash = strrchr(parent, '/');
                                if (slash != NULL) {
                                    if (slash == parent) {
                                        parent[1] = '\0';
                                    } else {
                                        *slash = '\0';
                                    }
                                    (void)navigate_to(&a, parent, 1);
                                }
                            } else {
                                open_selection(&a);
                            }
                        }
                    }
                    if (on_name && row >= 0) {
                        /* Not started here: armed.  The rename begins only
                         * once the double-click window has passed with no
                         * further click (PVS-014, MA-043). */
                        (void)tiku_trk_poseview_name_clicked(a.view, row, now);
                    } else {
                        tiku_trk_poseview_cancel_edit_wait(a.view);
                    }
                    a.press_at = now;
                    a.press_x = ev.x;
                    a.press_y = ev.y;
                    a.press_row = row;
                    a.press_down = 1;
                    a.dirty = 1;
                }
                break;
            }
            case TIKU_EVENT_POINTER_MOVE: {
                a.ptr_x = ev.x;
                a.ptr_y = ev.y;
                if (a.popup != NULL) {
                    /* The hot row follows the pointer, in the level it is
                     * over: without this a menu gives no sign of which
                     * item a pick would take. */
                    int moved = 0;

                    moved |= tiku_trk_menu_track(a.popup, a.popup_frame,
                                                 ev.x, ev.y);
                    if (a.popup_sub != NULL) {
                        moved |= tiku_trk_menu_track(a.popup_sub,
                                                     a.popup_sub_frame,
                                                     ev.x, ev.y);
                    }
                    if (moved) {
                        a.dirty = 1;
                    }
                    break;
                }
                if (a.sb.tracking) {
                    tiku_trk_poseview_scroll_to(a.view,
                        tiku_trk_scroll_drag(&a.sb,
                            a.sb_rect.h - 2 * a.sb_rect.w,
                            ev.y - a.sb_rect.y - a.sb_rect.w));
                    a.dirty = 1;
                    break;
                }
                if (a.hsb.tracking) {
                    tiku_trk_poseview_set_hscroll(a.view,
                        tiku_trk_scroll_drag(&a.hsb,
                            a.hsb_rect.w - 2 * a.hsb_rect.h,
                            ev.x - a.hsb_rect.x - a.hsb_rect.h));
                    a.dirty = 1;
                    break;
                }
                int cx = 0, cy = 0;

                if (tiku_trk_titleview_tracking(&a.tv) !=
                        TIKU_TRK_TRACK_NONE ||
                    title_hit(&a, ev.x, ev.y, NULL, NULL)) {
                    unsigned tf;

                    (void)title_hit(&a, ev.x, ev.y, &cx,
                                    &cy);
                    tf = tiku_trk_titleview_mouse_moved(&a.tv, cx, cy,
                                                        tiku_trk_now());
                    {   /* TS-013: the pointer says what it is over, and
                         * only while nothing is being tracked. */
                        /* An inactive window offers no resize cursor: the
                         * click it would invite is spent activating. */
                        int want = ((tf & TIKU_TRK_TT_CURSOR) != 0u) &&
                                   a.active;

                        if (want != a.cursor_ew) {
                            a.cursor_ew = want;
                            tiku_host_set_resize_cursor(a.host, want);
                        }
                    }
                    if ((tf & TIKU_TRK_TT_REDRAW) != 0u) {
                        a.dirty = 1;
                    }
                    if (tiku_trk_titleview_tracking(&a.tv) !=
                            TIKU_TRK_TRACK_NONE) {
                        break;
                    }
                }
                if (a.press_down &&
                    (ev.x - a.press_x > 4 ||
                     a.press_x - ev.x > 4 ||
                     ev.y - a.press_y > 4 ||
                     a.press_y - ev.y > 4)) {
                    /* PVS-015: a drag is not a slow click. */
                    tiku_trk_poseview_cancel_edit_wait(a.view);
                    a.press_down = 0;
                }
                if (a.band_active) {
                    unsigned mods = 0;
                    tiku_rect_t lr = list_rect(&a);

                    if (ev.modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                    if (ev.modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                    a.band_x1 = ev.x;
                    a.band_y1 = ev.y;
                    (void)tiku_trk_poseview_select_rect(a.view,
                        a.band_x0 - lr.x, a.band_y0 - lr.y,
                        a.band_x1 - lr.x, a.band_y1 - lr.y, mods);
                    a.dirty = 1;
                } else if (a.drag_active) {
                    tiku_rect_t lr = list_rect(&a);
                    int lx = ev.x - lr.x, ly = ev.y - lr.y;
                    int dx = lx - a.drag_x, dy = ly - a.drag_y;
                    int i, n = tiku_trk_poseview_count(a.view);
                    int snap = (ev.modifiers & TIKU_MOD_CMD) ? 1 : 0;

                    if (dx != 0 || dy != 0) {
                        /* Every selected pose moves by the same delta from
                         * where it started, so a group keeps its shape and a
                         * snapped drag still crosses cell boundaries; Ctrl
                         * snaps to the grid. */
                        for (i = 0; i < n; i++) {
                            tiku_trk_pose_t *p = tiku_trk_poseview_at(a.view, i);
                            if (p == NULL || !p->selected) { continue; }
                            tiku_trk_poseview_move_pose(a.view, p,
                                                        p->drag_x0 + dx,
                                                        p->drag_y0 + dy, snap);
                        }
                        a.dirty = 1;
                    }
                }
                break;
            }
            case TIKU_EVENT_POINTER_UP:
                /* Letting go applies whatever range arrived while the bar
                 * was held (PVL-052). */
                tiku_trk_scroll_release(&a.sb);
                tiku_trk_scroll_release(&a.hsb);
                if (tiku_trk_titleview_tracking(&a.tv) !=
                        TIKU_TRK_TRACK_NONE) {
                    int cx = 0, cy = 0;

                    (void)title_hit(&a, ev.x, ev.y, &cx,
                                    &cy);
                    (void)tiku_trk_titleview_mouse_up(&a.tv, cx, cy,
                        (ev.modifiers & TIKU_MOD_SHIFT) ? 1 : 0,
                        tiku_trk_now());
                    (void)tiku_trk_poseview_save_columns(a.view);
                    a.dirty = 1;
                    break;
                }
                if (ev.button == 3u && a.press_down) {
                    open_context_menu(&a, a.press_row, ev.x,
                                      ev.y, ev.modifiers);
                }
                a.press_down = 0;
                if (a.drag_active) {
                    tiku_rect_t nr = navigator_rect(&a);

                    if (ev.x >= nr.x &&
                        ev.x < nr.x + nr.w &&
                        ev.y >= nr.y &&
                        ev.y < nr.y + nr.h) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_active(a.view);
                        char dest[TIKU_PATH_MAX];

                        a.drag_active = 0;
                        if (p != NULL && tiku_trk_navigator_drop_path(
                                &p->model, dest, sizeof dest)) {
                            (void)navigate_to(&a, dest, 1);
                        }
                        a.dirty = 1;
                        break;
                    }
                }
                if (a.drag_active &&
                    (ev.modifiers & TIKU_MOD_OPTION) != 0u) {
                    /* Option makes the same-folder drag a DUPLICATE, not a
                     * reposition (FS-004): the poses go back where they
                     * were picked up and the copies are made instead. */
                    int i, n = tiku_trk_poseview_count(a.view);
                    int bx = a.press_x - ev.x, by = a.press_y - ev.y;

                    for (i = 0; i < n; i++) {
                        tiku_trk_pose_t *p = tiku_trk_poseview_at(a.view, i);

                        if (p != NULL && p->selected) {
                            tiku_trk_poseview_move_pose(a.view, p,
                                p->loc_x + bx, p->loc_y + by, 0);
                        }
                    }
                    a.drag_active = 0;
                    menu_key(&a, 'd', TIKU_MOD_CMD);
                }
                if (a.band_active || a.drag_active) {
                    a.band_active = 0;
                    a.drag_active = 0;
                    tiku_trk_poseview_set_band(a.view, 0);
                    /* Persist where things ended up: the arrangement is the
                     * user's, and it belongs on the objects. */
                    (void)tiku_trk_poseview_save_state(a.view);
                    a.dirty = 1;
                }
                break;
            case TIKU_EVENT_ACTIVATED:
            case TIKU_EVENT_DEACTIVATED:
                /* PVS-023: the selection stops being PAINTED while the
                 * window is inactive, and the pivot is parked so a
                 * shift-click on return still extends from where the user
                 * left off.  A pending rename does not survive the trip. */
                a.active = (ev.type == TIKU_EVENT_ACTIVATED);
                tiku_trk_poseview_window_activated(a.view, a.active);
                if (!a.active) {
                    tiku_trk_poseview_cancel_edit_wait(a.view);
                    if (a.editing) {
                        end_rename(&a, 1);   /* deactivation COMMITS */
                    }
                }
                a.dirty = 1;
                break;
            case TIKU_EVENT_CLOSE:
                a.running = 0;
                break;
            default:
                break;
            }
            if (sc != NULL) {
                break;              /* one instruction per turn */
            }
        }
        if (a.fs != NULL && tiku_fs_busy(a.fs)) {
            const tiku_progress_t *pr;

            /* Stepped, not blocking: the window keeps drawing while a copy
             * runs, which is the whole reason the engine has a cursor. */
            (void)tiku_fs_step(a.fs, 4);
            /* A mixed paste left its second half waiting for the engine to
             * go idle; this is where it gets to start (FS-055). */
            (void)tiku_trk_folder_file_actions_pump(&a.file_actions);
            pr = tiku_fs_progress(a.fs);
            if (pr->conflict) {
                /* Asked, not assumed.  The engine worked out which form
                 * the question takes; this puts it up and waits. */
                if (!tiku_trk_status_overlay_alert(&a.status_overlay)->open) {
                    tiku_trk_alert_for_conflict(
                        tiku_trk_status_overlay_alert(&a.status_overlay), pr);
                }
            } else if (pr->error_ask) {
                if (!tiku_trk_status_overlay_alert(&a.status_overlay)->open) {
                    tiku_trk_alert_for_error(
                        tiku_trk_status_overlay_alert(&a.status_overlay), pr);
                }
            } else {
                snprintf(a.status, sizeof a.status, "%d of %d%s%s",
                         pr->done + pr->skipped, pr->total,
                         pr->skipped ? " (some skipped)" : "",
                         pr->failed ? " -- some failed" : "");
            }
            if (!tiku_fs_busy(a.fs)) {
                after_op(&a);
            }
            a.dirty = 1;
        }
        {   /* Tracker's Pulse: the armed rename starts here, not on the
             * click, which is what makes it a slow second click and not a
             * second click (MA-043). */
            int due = tiku_trk_poseview_edit_due(a.view, tiku_trk_now());

            if (due >= 0 && !a.editing) {
                begin_rename_row(&a, due);
            }
        }
        if (tiku_trk_poseview_loading(a.view)) {
            /* One batch per turn round the loop, so the window keeps
             * drawing and answering the mouse while a folder arrives. */
            (void)tiku_trk_poseview_load_step(a.view, tiku_trk_now());
            a.dirty = 1;
        }
        tiku_trk_poseview_type_tick(a.view);
        if (tiku_trk_poseview_pump(a.view, 40) > 0) {
            a.dirty = 1;                /* a live value moved */
        }
        if (shot != NULL && shot_at_us > 0 &&
            tiku_trk_now() >= shot_at_us) {
            a.dirty = 1;              /* force one draw to capture */
        }
        if (a.dirty) {
            draw(&a);
            blit(&a);
            if (shot != NULL && shot_at_us > 0 &&
                tiku_trk_now() >= shot_at_us) {
                /* Dump what was just drawn: the window is the deliverable,
                 * so it must be reviewable without someone watching it. */
                (void)tiku_surface_png(a.surf, shot);
                printf("wrote %s\n", shot);
                fflush(stdout);
                shot_at_us = 0;
            }
        }
        if (deadline_us > 0 && tiku_trk_now() > deadline_us) {
            a.running = 0;
        }
        /* A script still lets the clock run, just faster: the notice
         * poll, the armed rename and the barber pole are all timed, and a
         * loop that spun without time passing would test none of them. */
        if (a.loop != NULL) {
            /* A task that changed something on screen dirties the surface
             * itself; running is not, by itself, a repaint. */
            (void)tiku_trk_loop_pulse(a.loop);
            {   /* A held arrow keeps stepping (NeXT UIG ch3). */
                int rep = tiku_trk_scroll_arrow_repeat(&a.sb);

                if (rep != 0) {
                    tiku_trk_poseview_scroll_to(a.view,
                        tiku_trk_poseview_scroll(a.view) +
                            rep * tiku_trk_scroll_line(&a.sb));
                    a.dirty = 1;
                }
                rep = tiku_trk_scroll_arrow_repeat(&a.hsb);
                if (rep != 0) {
                    tiku_trk_poseview_set_hscroll(a.view,
                        tiku_trk_poseview_hscroll(a.view) +
                            rep * tiku_trk_scroll_line(&a.hsb));
                    a.dirty = 1;
                }
            }
        }
        {
            /* Sleep until the next task is due, or one heartbeat,
             * whichever is sooner (AW-086). */
            int64_t nap = (sc != NULL) ? 20000 : 10000;
            int64_t due = (a.loop != NULL)
                              ? tiku_trk_loop_next_due(a.loop) : -1;

            if (due >= 0 && due < nap) {
                nap = (due < 1000) ? 1000 : due;
            }
            usleep((useconds_t)nap);
        }
    }

    tiku_host_close(a.host);
    tiku_surface_free(a.surf);
    /* Quitting waits for work already under way, up to five seconds
     * (AW-009): the alternative is a copy abandoned mid-file with
     * nothing to say so. */
    drain_fs(a.fs);
    if (sc != NULL) {
        rc = tiku_trk_script_report(sc, "tracker");
        tiku_trk_script_free(sc);
    }
    tiku_fs_free(a.fs);
    tiku_trk_poseview_free(a.view);
    tiku_backend_close(a.backend);
    return rc;
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker.browser",
    .name = "Tracker",
    .run = tracker_run
};
