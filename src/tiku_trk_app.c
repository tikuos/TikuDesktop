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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_attr.h"
#include "tiku_trk_edit.h"
#include "tiku_trk_model.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_titleview.h"
#include "tiku_trk_script.h"
#include "tiku_trk_task.h"

#define WIN_W 760
#define WIN_H 520
#define TAB_H 21
#define BORDER 5
#define TITLE_H 18
#define COUNT_H 16
#define SCROLL_W 15

/* List mode uses Tracker's mini icon size; a taller row does not grow it. */
#define MINI_ICON_MAX 16

/* Tracker's own list row: ceilf(plain font size * 1.65) = 20 at 12 px. */
#define LIST_ROW_H 20

typedef struct {
    Display             *dpy;
    Window               win;
    GC                   gc;
    XImage              *img;
    tiku_desk_surface_t *surf;
    Atom                 del;
    tiku_trk_poseview_t *view;
    tiku_trk_backend_t  *backend;
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
    tiku_trk_fs_t       *fs;
    char                 status[220];
    /* inline rename: the editor is a mode, as Tracker's TextWidget is */
    int                  editing;
    tiku_trk_edit_t      ed;
    char                 edit_path[TIKU_TRK_PATH_MAX];
    /* The window's own activation state, which decides whether a selection
     * is PAINTED; and where the last press landed, for the long press. */
    int                  active;
    int64_t              press_at;
    int                  press_x, press_y;
    int                  press_row;
    int                  press_down;
    int                  read_only;  /* the store refuses writes           */
    tiku_trk_titleview_t tv;         /* the column strip's tracking state  */
    Cursor               resize_cur;
    int                  cursor_ew;  /* which cursor the window is showing */
    /* The list row to select once a delete has finished; -1 when the last
     * operation was not one that removes rows (FS-050). */
    int                  select_next;
} app_t;

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

static void blit(app_t *a);

/** @brief Geometry of the row area, derived once per draw. */
static tiku_desk_rect_t
list_rect(const app_t *a)
{
    tiku_desk_rect_t r;

    int titled = (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST);

    r.x = BORDER;
    /* The column title bar belongs to list mode; icon mode gets the space
     * back, and hit-testing must use the same rectangle drawing does. */
    r.y = TAB_H + BORDER + (titled ? TITLE_H : 0);
    r.w = a->surf->w - 2 * BORDER - (titled ? SCROLL_W : 0);
    r.h = a->surf->h - r.y - BORDER - COUNT_H;
    return r;
}

static int
visible_rows(const app_t *a)
{
    tiku_desk_rect_t r = list_rect(a);

    return (a->row_h > 0) ? (r.h - 4) / a->row_h : 0;
}

/** @brief Width of @p s in the font @p ctx, for the text fitter. */
static int
measure_text(const char *s, void *ctx)
{
    return tiku_desk_text_width(ctx, s);
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
draw_title(app_t *a, tiku_desk_rect_t strip, const tiku_trk_column_t *c,
           int pressed, int drag_dx)
{
    tiku_desk_surface_t *s = a->surf;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t r;
    char fit[64];
    int tw, tx, data_x = col_x(a, c) + drag_dx;

    r.x = data_x - TIKU_TRK_COL_LEFT_MARGIN;
    r.y = strip.y;
    r.w = c->width + TIKU_TRK_COL_MARGIN;
    r.h = strip.h;
    tiku_desk_fill(s, r, pressed ? tiku_desk_tint(TIKU_DESK_C_PANEL, 1.15f)
                                 : TIKU_DESK_C_PANEL);
    tiku_desk_bevel(s, r, tiku_desk_tint(TIKU_DESK_C_PANEL, 0.0f),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 1.06f));
    /* Truncated against the column's own width -- the margins are not
     * text space -- and losing its END, because the head of a column name
     * is what identifies it. */
    (void)tiku_trk_text_fit(c->title, c->width, TIKU_TRK_TRUNC_END,
                            measure_text, (void *)f, fit, sizeof fit);
    tw = tiku_desk_text_width(f, fit);
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
    tiku_desk_text(s, f, tx, r.y + (r.h - f->height) / 2 + f->ascent, fit,
                   pressed ? tiku_desk_tint(TIKU_DESK_C_TEXT, 1.20f)
                           : TIKU_DESK_C_TEXT);
    if (pressed && drag_dx != 0) {
        /* A dragged title is outlined where it will LAND, which is the only
         * thing on screen that says the drag is going to move a column
         * rather than sort it. */
        tiku_desk_frame(s, r, TIKU_DESK_C_FOCUS);
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
        tiku_desk_rgb_t ink =
            tiku_desk_tint(TIKU_DESK_C_PANEL, primary ? 1.60f : 1.30f);

        if (primary || secondary) {
            /* Ascending points DOWN and reverse points UP, which is the way
             * round the original draws it -- the widest row is the base, so
             * the base has to be at the top for a downward apex. */
            for (k = 0; k < 3; k++) {
                int wgt = up ? k : 2 - k;

                tiku_desk_hline(s, cx - wgt, cy - 1 + k, 2 * wgt + 1, ink);
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
draw_pole(app_t *a, tiku_desk_rect_t count)
{
    tiku_desk_surface_t *s = a->surf;
    tiku_desk_rect_t r;
    int phase = (int)((tiku_trk_now() / 60000) % 6);
    int i;

    r.w = 7;
    r.h = 7;
    r.x = count.x + count.w - r.w - 3;
    r.y = count.y + (COUNT_H - r.h) / 2;
    tiku_desk_fill(s, r, TIKU_DESK_C_DOC);
    tiku_desk_frame(s, r, tiku_desk_tint(TIKU_DESK_C_PANEL, 1.30f));
    for (i = -6; i < r.h + 6; i += 3) {
        int y = r.y + i + phase;
        int k;

        for (k = 0; k < 2; k++) {
            if (y + k >= r.y + 1 && y + k < r.y + r.h - 1) {
                tiku_desk_hline(s, r.x + 1, y + k, r.w - 2,
                                tiku_desk_tint(TIKU_DESK_C_PANEL, 1.45f));
            }
        }
    }
}

/** @brief The count view, shared by both modes. */
static void
draw_count(app_t *a, tiku_desk_rect_t list)
{
    tiku_desk_surface_t *s = a->surf;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t count = { list.x, list.y + list.h, list.w, COUNT_H };
    const char *typed = tiku_trk_poseview_type_string(a->view);
    int total = tiku_trk_poseview_count(a->view);
    char label[240];

    /* While typing, the string replaces the count on a document ground --
     * the user needs to see what they have typed, not how many items. */
    if (a->status[0] != '\0') {
        /* An operation's progress, a refusal, or a conflict question: the
         * count can wait, this is what the user needs to read. */
        tiku_desk_fill(s, count, TIKU_DESK_C_PANEL);
        snprintf(label, sizeof label, "%s", a->status);
    } else if (typed[0] != '\0') {
        tiku_desk_ui_sunken(s, count, TIKU_DESK_C_DOC);
        snprintf(label, sizeof label, "%s", typed);
    } else if (tiku_trk_poseview_pole(a->view, tiku_trk_now())) {
        /* TS-038: a load long enough to notice says so.  The count is not
         * final yet, so it is not shown as though it were. */
        tiku_desk_fill(s, count, TIKU_DESK_C_PANEL);
        snprintf(label, sizeof label, "%d item%s so far", total,
                 (total == 1) ? "" : "s");
    } else {
        tiku_desk_fill(s, count, TIKU_DESK_C_PANEL);
        /* TS-037: an empty folder says so in words.  "0 items" reads like
         * a count that failed rather than a folder with nothing in it. */
        if (total == 0) {
            snprintf(label, sizeof label, "%s",
                     tiku_trk_poseview_is_filtering(a->view)
                         ? "nothing matches" : "empty folder");
        } else {
            snprintf(label, sizeof label, "%d item%s%s", total,
                     (total == 1) ? "" : "s",
                     tiku_trk_poseview_is_filtering(a->view)
                         ? " (filtered)" : "");
        }
    }
    {   /* TS-039: while the pole is up the text gives up the strip it
         * occupies, so the two never overlap. */
        int pole = tiku_trk_poseview_pole(a->view, tiku_trk_now());
        tiku_desk_rect_t clip = count;

        if (pole) {
            clip.w -= POLE_W;
        }
        tiku_desk_clip_set(s, clip);
        tiku_desk_text(s, f, count.x + 5,
                       count.y + (COUNT_H - f->height) / 2 + f->ascent,
                       label, TIKU_DESK_C_TEXT);
        tiku_desk_clip_reset(s);
        if (pole) {
            draw_pole(a, count);
        }
    }
}

/** @brief One icon: the node's own art, plus a centred label. */
static void
draw_pose_icon(app_t *a, const tiku_trk_pose_t *p, int x, int y, int size)
{
    tiku_desk_surface_t *s = a->surf;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t box;
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
    if (!(drawn_sel
            ? tiku_trk_icon_paint_selected(s,
                  tiku_trk_model_icon_name(&p->model), box.x, box.y, art)
            : tiku_trk_icon_paint(s, tiku_trk_model_icon_name(&p->model),
                                  box.x, box.y, art))) {
        /* An icon the set does not carry still gets a cell of the right size,
         * so a gap in the art never disturbs the layout. */
        tiku_desk_fill(s, box, TIKU_DESK_RGB(250, 250, 250));
        tiku_desk_frame(s, box, tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f));
    }

    /* A name wider than its cell loses its MIDDLE, not its tail: icons sit on
     * a grid, and a column of names sharing a prefix is unreadable when every
     * one of them is cut at the same point.  The budget is about thirty
     * CHARACTERS, not a multiple of the icon -- a name is as long as it is,
     * and tying the allowance to the art means a mini icon shows a quarter
     * of every name (MA-027). */
    {
        int budget = 30 * tiku_desk_text_width(f, "n");

        (void)tiku_trk_text_fit(p->model.name, budget, TIKU_TRK_TRUNC_MIDDLE,
                                measure_text, (void *)f, label, sizeof label);
    }
    tw = tiku_desk_text_width(f, label);
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
            tiku_desk_rect_t hl = { lx - 2, ly, tw + 4, f->height + 1 };

            tiku_desk_fill(s, hl, TIKU_DESK_C_SELECT);
        }
        tiku_desk_text(s, f, lx, ly + f->ascent, label,
                       drawn_sel ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
        if (p->model.kind == TIKU_TRK_KIND_SYMLINK) {
            /* MA-075: a link's name is underlined, and only its name -- the
             * one marker in this Tracker that says what an entry IS without
             * a second column. */
            tiku_desk_hline(s, lx, ly + f->ascent + 2, tw,
                            drawn_sel ? TIKU_DESK_C_SELTEXT
                                      : TIKU_DESK_C_TEXT);
        }
    }
}

/** @brief Icon mode: poses at their own positions, plus the rubber band. */
static void
draw_icons(app_t *a, tiku_desk_rect_t list)
{
    tiku_desk_surface_t *s = a->surf;
    int n = tiku_trk_poseview_count(a->view);
    /* The cell comes from the layout and the ART from the mode: in
     * mini-icon mode the two differ, which is the whole point of it. */
    int size = tiku_trk_poseview_art_size(a->view);
    int i;

    tiku_desk_ui_sunken(s, list, a->read_only
                                 ? tiku_desk_tint(TIKU_DESK_C_DOC, 0.94f)
                                 : TIKU_DESK_C_DOC);
    if (a->active) {
        tiku_desk_frame(s, list, TIKU_DESK_C_FOCUS);
    }
    tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
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
        /* The band is drawn as an outline over the icons rather than a
         * filled tint, which is the cheap half of Tracker's two styles. */
        tiku_desk_rect_t r;
        int lox = (a->band_x0 < a->band_x1) ? a->band_x0 : a->band_x1;
        int loy = (a->band_y0 < a->band_y1) ? a->band_y0 : a->band_y1;

        r.x = lox;
        r.y = loy;
        r.w = (a->band_x0 > a->band_x1 ? a->band_x0 - a->band_x1
                                       : a->band_x1 - a->band_x0);
        r.h = (a->band_y0 > a->band_y1 ? a->band_y0 - a->band_y1
                                       : a->band_y1 - a->band_y0);
        tiku_desk_frame(s, r, TIKU_DESK_C_SELECT);
    }
    tiku_desk_clip_reset(s);
}

static void
draw(app_t *a)
{
    tiku_desk_surface_t *s = a->surf;
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    const tiku_trk_column_t *cols;
    tiku_desk_rect_t win = { 0, 0, s->w, s->h };
    tiku_desk_rect_t list, title, count;
    int ncol = tiku_trk_poseview_columns(a->view, &cols);
    int rows = visible_rows(a);
    int scroll = tiku_trk_poseview_scroll(a->view);
    int total = tiku_trk_poseview_count(a->view);
    int i;

    (void)tiku_desk_ui_window(s, win, a->title, 1);
    list = list_rect(a);
    /* The view cannot see its own window.  Without this a page is one row
     * and the scroll is never clamped -- both stated in terms of what is
     * VISIBLE, which only the window knows. */
    tiku_trk_poseview_set_visible_rows(a->view, rows);

    /* The title strip.  Every title is placed from its column's OFFSET, so
     * a title and the cells beneath it cannot drift apart. */
    title = (tiku_desk_rect_t){ list.x, TAB_H + BORDER, list.w, TITLE_H };
    tiku_desk_fill(s, title, TIKU_DESK_C_PANEL);
    tiku_desk_clip_set(s, title);
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
    tiku_desk_clip_reset(s);

    if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        draw_icons(a, list);
        draw_count(a, list);
        a->dirty = 0;
        return;
    }

    /* Rows.  A store that cannot be written to is tinted, so a window that
     * will refuse every edit says so before one is attempted (PVL-076). */
    tiku_desk_ui_sunken(s, list, a->read_only
                                 ? tiku_desk_tint(TIKU_DESK_C_DOC, 0.94f)
                                 : TIKU_DESK_C_DOC);
    if (a->active) {
        /* CW-019: the focus border says which view the keyboard is aimed
         * at, which matters the moment there is more than one. */
        tiku_desk_frame(s, list, TIKU_DESK_C_FOCUS);
    }
    tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, scroll + i);
        tiku_desk_rect_t row = { list.x + 2, list.y + 2 + i * a->row_h,
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
        tiku_desk_fill(s, row, drawn_sel ? TIKU_DESK_C_SELECT
                                         : TIKU_DESK_C_DOC);
        {   /* The icon owns the strip LEFT of the first column, which is
             * what StartOffset reserves and why a column's x is absolute
             * rather than a running total. */
            int mini = a->row_h - 2;
            int ix = list.x + 2 - tiku_trk_poseview_hscroll(a->view);

            if (mini > MINI_ICON_MAX) {
                mini = MINI_ICON_MAX;
            }
            (void)(drawn_sel
                   ? tiku_trk_icon_paint_selected(s,
                         tiku_trk_model_icon_name(&p->model), ix,
                         row.y + (a->row_h - mini) / 2, mini)
                   : tiku_trk_icon_paint(s,
                         tiku_trk_model_icon_name(&p->model), ix,
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
            tiku_desk_rect_t cell = { text_x, row.y, cols[j].width,
                                      a->row_h };
            char fitted[80];
            int tw;

            {   /* Intersected with the list, not substituted for it: the
                 * clip is a replacement, so a cell scrolled left of the
                 * view would otherwise paint over the border and the
                 * window frame. */
                tiku_desk_rect_t inner = tiku_desk_inset(list, 2);
                int x0 = (cell.x > inner.x) ? cell.x : inner.x;
                int x1 = (cell.x + cell.w < inner.x + inner.w)
                         ? cell.x + cell.w : inner.x + inner.w;

                cell.x = x0;
                cell.w = (x1 > x0) ? x1 - x0 : 0;
                tiku_desk_clip_set(s, cell);
            }
            (void)tiku_trk_text_fit(txt, cols[j].width,
                                    (cols[j].field == TIKU_TRK_COL_NAME)
                                        ? TIKU_TRK_TRUNC_MIDDLE
                                        : TIKU_TRK_TRUNC_END,
                                    measure_text, (void *)f, fitted,
                                    sizeof fitted);
            tw = tiku_desk_text_width(f, fitted);
            if (cols[j].alignment == TIKU_TRK_ALIGN_RIGHT) {
                text_x += cols[j].width - tw;
            }
            tiku_desk_text(s, f, text_x,
                           row.y + (a->row_h - f->height) / 2 + f->ascent,
                           fitted, drawn_sel ? TIKU_DESK_C_SELTEXT
                                             : TIKU_DESK_C_TEXT);
            if (cols[j].field == TIKU_TRK_COL_NAME &&
                p->model.kind == TIKU_TRK_KIND_SYMLINK) {
                /* MA-076/IV-043: under the TEXT only, and only in the first
                 * column -- a rule that underlined the whole cell would
                 * read as a divider rather than as a property of the name. */
                tiku_desk_hline(s, text_x,
                                row.y + (a->row_h - f->height) / 2 +
                                f->ascent + 2, tw,
                                drawn_sel ? TIKU_DESK_C_SELTEXT
                                          : TIKU_DESK_C_TEXT);
            }
            tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
        }
    }
    tiku_desk_clip_reset(s);

    /* Scrollbar. */
    {
        float frac = (total > 0) ? (float)rows / (float)total : 1.0f;
        float pos = (total > rows) ? (float)scroll / (float)(total - rows)
                                   : 0.0f;
        tiku_desk_rect_t sb = { list.x + list.w, list.y, SCROLL_W, list.h };
        tiku_desk_ui_scrollbar(s, sb, pos, frac, 0);
    }

    if (a->tv.track == TIKU_TRK_TRACK_RESIZE && a->tv.line_x >= 0) {
        /* TS-011: a full-height line down the LIST, not a mark on the
         * strip -- what the user is judging is where the column will fall
         * against the rows, and the strip cannot show that. */
        tiku_desk_clip_set(s, tiku_desk_inset(list, 2));
        tiku_desk_vline(s, list.x + a->tv.line_x -
                        tiku_trk_poseview_hscroll(a->view), list.y + 2,
                        list.h - 4,
                        tiku_desk_tint(TIKU_DESK_C_DOC, 1.40f));
        tiku_desk_clip_reset(s);
    }
    if (a->editing) {
        /* The editor sits where the name is, as an editable field: the row
         * stays visible around it so the context is not lost. */
        int i, n = tiku_trk_poseview_count(a->view);
        int scroll = tiku_trk_poseview_scroll(a->view);

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
            tiku_desk_rect_t box;

            if (p == NULL || strcmp(p->model.path, a->edit_path) != 0) {
                continue;
            }
            box.x = col_x(a, &cols[0]);
            box.y = list.y + 2 + (i - scroll) * a->row_h;
            box.w = cols[0].width;
            box.h = a->row_h;
            tiku_desk_ui_textfield_sel(s, box, a->ed.text, a->ed.caret,
                                       a->ed.sel_a, a->ed.sel_b,
                                       TIKU_DESK_S_FOCUS);
            break;
        }
    }
    draw_count(a, list);
    (void)count;
    a->dirty = 0;
}

static void
blit(app_t *a)
{
    if (a->dpy == NULL) {
        return;                     /* displayless: the surface IS the
                                     * output, and -shot writes it */
    }
    memcpy(a->img->data, a->surf->px,
           (size_t)a->surf->w * (size_t)a->surf->h * 4u);
    XPutImage(a->dpy, a->win, a->gc, a->img, 0, 0, 0, 0,
              (unsigned)a->surf->w, (unsigned)a->surf->h);
    XFlush(a->dpy);
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
    if (a->dpy != NULL) {
        XStoreName(a->dpy, a->win, a->title);
    }
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
    tiku_desk_rect_t lr;

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        return;
    }
    lr = list_rect(a);
    (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
}

/**
 * @brief Gather the selection as paths for a file operation.
 *
 * @return Count written.
 */
static int
selection_paths(app_t *a, char paths[][TIKU_TRK_PATH_MAX],
                const char *out[], int max)
{
    int i, n = tiku_trk_poseview_count(a->view), k = 0;

    for (i = 0; i < n && k < max; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
        if (p == NULL || !p->selected) {
            continue;
        }
        snprintf(paths[k], TIKU_TRK_PATH_MAX, "%s", p->model.path);
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
    tiku_trk_model_t m;

    a->read_only = 0;
    if (a->backend == NULL || a->backend->ops->stat == NULL) {
        return;
    }
    if (a->backend->ops->stat(a->backend, tiku_trk_poseview_path(a->view),
                              &m) == 0) {
        a->read_only = ((m.facts.perm & TIKU_TRK_P_WRITE) == 0u);
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
    if (p->model.kind == TIKU_TRK_KIND_DEVICE_NODE ||
        p->model.kind == TIKU_TRK_KIND_VIRTUAL_DIR) {
        snprintf(a->status, sizeof a->status,
                 "'%s' is part of the device namespace and cannot be renamed",
                 p->model.name);
        a->dirty = 1;
        return;
    }
    a->editing = 1;
    tiku_trk_edit_start(&a->ed, p->model.name);
    /* A character the store will refuse must never reach the field. */
    tiku_trk_edit_deny(&a->ed, tiku_trk_fs_name_deny());
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
    a->editing = 0;
    tiku_trk_edit_stop(&a->ed);
    {   /* MA-050: a name that was not changed is not a rename.  Sent
         * anyway, the store finds the file already there and reports a
         * collision with itself -- an error where the rule wants silence. */
        const char *slash = strrchr(a->edit_path, '/');
        const char *was = (slash != NULL) ? slash + 1 : a->edit_path;

        if (commit && strcmp(was, a->ed.text) == 0) {
            commit = 0;
        }
    }
    if (commit && a->ed.len > 0) {
        if (tiku_trk_fs_rename(a->fs, a->edit_path, a->ed.text, err,
                               sizeof err) != 0) {
            snprintf(a->status, sizeof a->status, "%s", err);
        } else {
            a->status[0] = '\0';
            after_op(a);
        }
    }
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
open_animation(app_t *a, tiku_desk_rect_t from)
{
    int k;

    for (k = 0; k < 7; k++) {
        tiku_desk_rect_t r = from;

        r.x -= 2 * k;
        r.y -= 2 * k;
        r.w += 4 * k;
        r.h += 4 * k;
        tiku_desk_invert_frame(a->surf, r);
        blit(a);
        usleep(10000);
        tiku_desk_invert_frame(a->surf, r);   /* inversion undoes itself */
    }
    blit(a);
}

/** @brief Open the active pose: a container navigates, a value is reported. */
static void
open_selection(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL) {
        return;
    }
    if (tiku_trk_model_is_container(&p->model)) {
        (void)tiku_trk_poseview_save_state(a->view);
        (void)tiku_trk_poseview_switch_dir(a->view, p->model.path);
        /* Opened as a BATCHED load: a folder with thousands of entries
         * shows its first rows at once instead of freezing the window
         * until the last one is read. */
        (void)tiku_trk_poseview_load_begin(a->view, tiku_trk_now());
        (void)tiku_trk_poseview_load_step(a->view, tiku_trk_now());
        (void)tiku_trk_poseview_restore_state(a->view);
        settle_layout(a);
        settle_writability(a);
        retitle(a);
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
key_char(app_t *a, XKeyEvent *ev, KeySym ks)
{
    char buf[8];

    if (a->dpy != NULL) {
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

/** @brief Row under a point, or -1 for empty space / the title bar. */
static int
row_at(app_t *a, int px, int py)
{
    tiku_desk_rect_t list = list_rect(a);
    int rows, idx;

    if (py < list.y + 2 || py > list.y + list.h ||
        px < list.x || px > list.x + list.w) {
        return -1;
    }
    rows = (py - list.y - 2) / a->row_h;
    idx = tiku_trk_poseview_scroll(a->view) + rows;
    return (idx < tiku_trk_poseview_count(a->view)) ? idx : -1;
}

/** @brief Whether a point is on the title strip, and where in its own space. */
static int
title_hit(app_t *a, int px, int py, int *cx, int *cy)
{
    tiku_desk_rect_t list = list_rect(a);

    if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (cx != NULL) {
        *cx = px - list.x + tiku_trk_poseview_hscroll(a->view);
    }
    if (cy != NULL) {
        *cy = py - (TAB_H + BORDER);
    }
    return (py >= TAB_H + BORDER && py <= TAB_H + BORDER + TITLE_H);
}

static void
handle_key(app_t *a, XKeyEvent *ev, KeySym ks)
{
    unsigned mods = 0;
    int nav = 0;

    if (a->editing) {
        /* While editing a name the view's keys are OFF: an arrow must move
         * the caret's row selection nowhere, and 'd' must type a letter,
         * not duplicate the file. */
        tiku_trk_edit_key_t k = TIKU_TRK_EDIT_CHAR;
        int shift = (ev->state & ShiftMask) ? 1 : 0;
        char ch = '\0';

        switch (ks) {
        case XK_Return:
        case XK_KP_Enter: k = TIKU_TRK_EDIT_COMMIT;    break;
        case XK_Escape:   k = TIKU_TRK_EDIT_CANCEL;    break;
        case XK_BackSpace: k = TIKU_TRK_EDIT_BACKSPACE; break;
        case XK_Delete:   k = TIKU_TRK_EDIT_DELETE;    break;
        case XK_Left:     k = TIKU_TRK_EDIT_LEFT;      break;
        case XK_Right:    k = TIKU_TRK_EDIT_RIGHT;     break;
        case XK_Home:     k = TIKU_TRK_EDIT_HOME;      break;
        case XK_End:      k = TIKU_TRK_EDIT_END;       break;
        case XK_a:
            if (ev->state & ControlMask) { k = TIKU_TRK_EDIT_SELECT_ALL; }
            break;
        default: break;
        }
        if (k == TIKU_TRK_EDIT_CHAR) {
            ch = key_char(a, ev, ks);
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

    if (ev->state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
    if (ev->state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }

    switch (ks) {
    case XK_Up:        nav = TIKU_TRK_KEY_UP;    break;
    case XK_Down:      nav = TIKU_TRK_KEY_DOWN;  break;
    case XK_Left:      nav = TIKU_TRK_KEY_LEFT;  break;
    case XK_Right:     nav = TIKU_TRK_KEY_RIGHT; break;
    case XK_Home:      nav = TIKU_TRK_KEY_HOME;  break;
    case XK_End:       nav = TIKU_TRK_KEY_END;   break;
    case XK_Prior:     nav = TIKU_TRK_KEY_PGUP;  break;
    case XK_Next:      nav = TIKU_TRK_KEY_PGDN;  break;
    default: break;
    }
    if (nav == TIKU_TRK_KEY_UP && (mods & TIKU_TRK_MOD_CMD)) {
        /* PVK-040: Cmd+Up is Open parent.  The direction IS the meaning,
         * which is why it is an arrow and not a letter. */
        (void)tiku_trk_poseview_go_up(a->view);
        settle_layout(a);
        settle_writability(a);
        retitle(a);
        a->dirty = 1;
        return;
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
    switch (ks) {
    case XK_Return:
    case XK_KP_Enter:
        /* PVK-015/016: a filter that has already narrowed the listing to
         * what the user wants should not also demand an arrow key first. */
        if (tiku_trk_poseview_return_pressed(a->view,
                                             (mods & TIKU_TRK_MOD_SHIFT))) {
            open_selection(a);
            tiku_trk_poseview_set_filtering(a->view, 0);
        } else {
            open_selection(a);
        }
        a->dirty = 1;
        return;
    case XK_space:
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
    case XK_BackSpace:
        if (tiku_trk_poseview_type_string(a->view)[0] != '\0') {
            tiku_trk_poseview_type_backspace(a->view);
        } else {
            (void)tiku_trk_poseview_go_up(a->view);
            retitle(a);
        }
        a->dirty = 1;
        return;
    case XK_Escape:
        tiku_trk_poseview_type_clear(a->view);
        a->dirty = 1;
        return;
    case XK_Tab:
        tiku_trk_poseview_set_filtering(a->view,
            !tiku_trk_poseview_is_filtering(a->view));
        a->dirty = 1;
        return;
    case XK_q:
        if (ev->state & ControlMask) { a->running = 0; return; }
        break;
    case XK_a:
        if (ev->state & ControlMask) {
            tiku_trk_poseview_select_all(a->view);
            a->dirty = 1;
            return;
        }
        break;
    case XK_s:
        if (ev->state & ControlMask) {
            /* PVK-046: invert, which is the only way to say "everything
             * except these" without clicking each of the rest. */
            tiku_trk_poseview_invert_selection(a->view);
            a->dirty = 1;
            return;
        }
        break;
    case XK_plus:
    case XK_equal:
    case XK_KP_Add:
        if (ev->state & ControlMask) {
            (void)tiku_trk_poseview_step_icon_size(a->view, 1);
            settle_layout(a);
            a->dirty = 1;
            return;
        }
        break;
    case XK_minus:
    case XK_KP_Subtract:
        if (ev->state & ControlMask) {
            (void)tiku_trk_poseview_step_icon_size(a->view, 0);
            settle_layout(a);
            a->dirty = 1;
            return;
        }
        break;
    case XK_r:
        if (ev->state & ControlMask) {
            (void)tiku_trk_poseview_reload(a->view);
            (void)tiku_trk_poseview_restore_state(a->view);
            settle_layout(a);
            a->dirty = 1;
            return;
        }
        break;
    case XK_1:
    case XK_2:
    case XK_3:
        if (ev->state & ControlMask) {
            tiku_desk_rect_t lr = list_rect(a);
            tiku_trk_poseview_set_mode(a->view,
                (ks == XK_1) ? TIKU_TRK_MODE_ICON :
                (ks == XK_2) ? TIKU_TRK_MODE_MINI : TIKU_TRK_MODE_LIST);
            if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
                (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
            }
            (void)tiku_trk_poseview_save_state(a->view);
            a->dirty = 1;
            return;
        }
        break;
    case XK_F2:
        begin_rename(a);
        return;
    case XK_Delete: {
        char paths[64][TIKU_TRK_PATH_MAX];
        const char *sel[64];
        int n = selection_paths(a, paths, sel, 64);

        if (n > 0 && a->fs != NULL) {
            /* FS-046/FS-047: what the key MEANS depends on where the items
             * are.  Somewhere with a Trash it is a move that can be undone;
             * inside the Trash, or on a store that has none, it is final --
             * and the wording says which. */
            int shift = (ev->state & ShiftMask) ? 1 : 0;
            tiku_trk_delete_q_t q =
                tiku_trk_fs_delete_question(a->fs, sel[0], shift);

            /* Noted BEFORE the listing changes: afterwards the index is
             * the only thing left pointing at where the item was. */
            a->select_next = first_selected_index(a);

            if (q == TIKU_TRK_DELETE_STRAIGHT) {
                snprintf(a->status, sizeof a->status,
                         "deleting %d item%s -- this cannot be undone", n,
                         (n == 1) ? "" : "s");
                (void)tiku_trk_fs_delete(a->fs, sel, n);
            } else {
                snprintf(a->status, sizeof a->status,
                         "moving %d item%s to the Trash", n,
                         (n == 1) ? "" : "s");
                (void)tiku_trk_fs_trash(a->fs, sel, n);
            }
        }
        a->dirty = 1;
        return;
    }
    case XK_d:
        if (ev->state & ControlMask) {
            char paths[64][TIKU_TRK_PATH_MAX];
            const char *sel[64];
            int n = selection_paths(a, paths, sel, 64);

            if (n > 0 && a->fs != NULL) {
                (void)tiku_trk_fs_duplicate(a->fs, sel, n);
                snprintf(a->status, sizeof a->status, "duplicating %d item%s",
                         n, (n == 1) ? "" : "s");
            }
            a->dirty = 1;
            return;
        }
        break;
    case XK_z:
        if ((ev->state & ControlMask) && (ev->state & ShiftMask)) {
            char what[64], err[200];

            if (a->fs != NULL && tiku_trk_fs_can_redo(a->fs, what,
                                                      sizeof what)) {
                int n = tiku_trk_fs_redo(a->fs, err, sizeof err);

                snprintf(a->status, sizeof a->status, "redid %s (%d item%s)",
                         what, n, (n == 1) ? "" : "s");
                after_op(a);
            } else {
                snprintf(a->status, sizeof a->status, "nothing to redo");
            }
            a->dirty = 1;
            return;
        }
        if (ev->state & ControlMask) {
            char what[64], err[200];

            if (a->fs != NULL && tiku_trk_fs_can_undo(a->fs, what,
                                                      sizeof what)) {
                int n = tiku_trk_fs_undo(a->fs, err, sizeof err);
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
    case XK_n:
        if (ev->state & ControlMask) {
            char made[TIKU_TRK_PATH_MAX], err[200];

            if (a->fs != NULL &&
                tiku_trk_fs_new_folder(a->fs, tiku_trk_poseview_path(a->view), made, sizeof made, err,
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
    case XK_k:
        if (ev->state & ControlMask) {
            tiku_desk_rect_t lr = list_rect(a);
            tiku_trk_poseview_cleanup(a->view, lr.w - 4);
            (void)tiku_trk_poseview_save_state(a->view);
            a->dirty = 1;
            return;
        }
        break;
    default:
        break;
    }
    {
        char typed = key_char(a, ev, ks);

        if (typed >= 0x20 && typed < 0x7f) {
            tiku_trk_poseview_type_ahead(a->view, typed);
            a->dirty = 1;
        }
    }
}

int
main(int argc, char **argv)
{
    app_t a;
    const char *port = NULL, *start = NULL;
    int baud = 115200, use_device = 0, i;
    int64_t last_click_us = 0, deadline_us = 0;
    int last_click_row = -1;
    const char *shot = NULL;
    const char *script_path = NULL;
    tiku_trk_script_t *sc = NULL;
    int rc = 0;
    int64_t shot_at_us = 0;

    memset(&a, 0, sizeof a);
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
        a.backend = tiku_trk_backend_tiku_open(port, baud);
        if (a.backend == NULL) {
            fprintf(stderr, "tracker: no device answered\n");
            return 1;
        }
        if (start == NULL) { start = "/"; }
    } else {
        a.backend = tiku_trk_backend_posix_open();
        if (start == NULL) { start = getenv("HOME"); }
        if (start == NULL) { start = "/"; }
    }

    a.view = tiku_trk_poseview_new(a.backend, start);
    if (a.view != NULL) {
        (void)tiku_trk_poseview_restore_state(a.view);
    }
    if (a.view == NULL) {
        fprintf(stderr, "tracker: cannot open %s\n", start);
        return 1;
    }
    a.fs = tiku_trk_fs_new(a.backend);
    a.surf = tiku_desk_surface_new(WIN_W, WIN_H, TIKU_DESK_C_PANEL);
    a.row_h = LIST_ROW_H;

    if (script_path != NULL) {
        /* Displayless.  The shell draws into the same surface either way,
         * so a scripted run exercises the real draw path -- it just has
         * nobody to show it to (see tiku_trk_script.h). */
        sc = tiku_trk_script_open(script_path);
        if (sc == NULL) {
            return 2;
        }
    } else {
        a.dpy = XOpenDisplay(NULL);
        if (a.dpy == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
    }
    if (a.dpy != NULL) {
        int scr = DefaultScreen(a.dpy);
        a.win = XCreateSimpleWindow(a.dpy, RootWindow(a.dpy, scr), 0, 0,
                                    WIN_W, WIN_H, 0, BlackPixel(a.dpy, scr),
                                    WhitePixel(a.dpy, scr));
        XSelectInput(a.dpy, a.win, ExposureMask | KeyPressMask |
                     ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask |
                     FocusChangeMask);
        a.del = XInternAtom(a.dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(a.dpy, a.win, &a.del, 1);
        XMapWindow(a.dpy, a.win);
        a.gc = XCreateGC(a.dpy, a.win, 0, NULL);
        {
            char *px = malloc((size_t)WIN_W * WIN_H * 4u);
            a.img = XCreateImage(a.dpy, DefaultVisual(a.dpy, scr),
                                 (unsigned)DefaultDepth(a.dpy, scr), ZPixmap,
                                 0, px, WIN_W, WIN_H, 32, 0);
        }
    }
    /* The window measures its own labels, which is what lets a pose's rect
     * grow to fit a long name instead of staying icon-sized (PVL-065), and
     * what "fit this column to its widest value" is measured with. */
    tiku_trk_poseview_set_measure(a.view, measure_text,
                                  (void *)tiku_desk_font_plain());
    tiku_trk_poseview_set_font_size(a.view,
                                    tiku_desk_font_plain()->height);
    (void)tiku_trk_poseview_restore_columns(a.view);
    tiku_trk_titleview_init(&a.tv, a.view, TITLE_H);
    if (a.dpy != NULL) {
        a.resize_cur = XCreateFontCursor(a.dpy, XC_sb_h_double_arrow);
    }
    settle_layout(&a);
    settle_writability(&a);
    retitle(&a);
    if (getenv("TRK_DEMO_OPS") != NULL) {
        /* Scripted: select everything and duplicate, so the capture shows a
         * real operation and its result. */
        char paths[64][TIKU_TRK_PATH_MAX];
        const char *sel[64];
        int n;

        tiku_trk_poseview_select_all(a.view);
        n = selection_paths(&a, paths, sel, 64);
        if (n > 0) {
            (void)tiku_trk_fs_duplicate(a.fs, sel, n);
            snprintf(a.status, sizeof a.status, "duplicating %d items", n);
        }
    }
    if (getenv("TRK_DEMO_ICON") != NULL) {
        /* Scripted: icon mode, auto-place, then move two icons the way a
         * user would drag them, so the capture shows real spatial state. */
        tiku_desk_rect_t lr = list_rect(&a);
        tiku_trk_poseview_set_mode(a.view, TIKU_TRK_MODE_ICON);
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
        tiku_desk_rect_t lr = list_rect(&a);

        tiku_trk_poseview_set_mode(a.view, TIKU_TRK_MODE_MINI);
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
        /* Events first, then the device: the window stays responsive while a
         * slow link is being read, which is the whole point of pumping here
         * rather than blocking on either one. */
        for (;;) {
            XEvent ev;
            KeySym ev_ks = NoSymbol;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (sc != NULL) {
                /* One instruction per turn, so the deferred work between
                 * turns -- a stepped copy, the armed rename -- happens
                 * where it happens on a real clock. */
                tiku_trk_script_step_t sp;
                tiku_trk_script_state_t sst;

                sst.status = a.status;
                sst.title = a.title;
                if (!tiku_trk_script_pump(sc, &sst, &sp)) {
                    a.running = 0;
                    break;
                }
                switch (sp.what) {
                case TIKU_TRK_SC_KEY:
                    ev.type = KeyPress;
                    ev.xkey.state = sp.state;
                    ev_ks = sp.ks;
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
                case TIKU_TRK_SC_SHOT:
                    draw(&a);
                    (void)tiku_desk_surface_png(a.surf, sp.path);
                    break;
                case TIKU_TRK_SC_QUIT:
                    a.running = 0;
                    break;
                default:
                    break;
                }
            } else if (a.dpy != NULL && XPending(a.dpy) > 0) {
                XNextEvent(a.dpy, &ev);
                if (ev.type == KeyPress) {
                    ev_ks = XLookupKeysym(&ev.xkey, 0);
                }
                have = 1;
            }
            if (!have) {
                break;
            }

            switch (ev.type) {
            case Expose:
                a.dirty = 1;
                break;
            case KeyPress:
                handle_key(&a, &ev.xkey, ev_ks);
                break;
            case ButtonPress: {
                if (ev.xbutton.button == 4 || ev.xbutton.button == 5) {
                    if (ev.xbutton.state & ShiftMask) {
                        /* TS-020: one value moves the titles and the rows
                         * together, because they read the same one. */
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_poseview_hscroll(a.view) +
                            ((ev.xbutton.button == 4) ? -24 : 24));
                    } else {
                        tiku_trk_poseview_scroll_to(a.view,
                            tiku_trk_poseview_scroll(a.view) +
                            ((ev.xbutton.button == 4) ? -3 : 3));
                    }
                    a.dirty = 1;
                    break;
                }
                {
                    int cx = 0, cy = 0;

                    if (title_hit(&a, ev.xbutton.x, ev.xbutton.y, &cx,
                                  &cy)) {
                        int btn = (ev.xbutton.button == 3) ? 2
                                : (ev.xbutton.button == 2) ? 3 : 1;

                        /* TS-015: whatever the press turns out to mean, an
                         * edit in progress is committed first -- the strip
                         * is outside the row being renamed. */
                        end_rename(&a, 1);
                        tiku_trk_poseview_cancel_edit_wait(a.view);
                        (void)tiku_trk_titleview_mouse_down(&a.tv, cx, cy,
                                                            btn, a.active,
                                                            tiku_trk_now());
                        a.dirty = 1;
                        break;
                    }
                }
                if (tiku_trk_poseview_mode(a.view) != TIKU_TRK_MODE_LIST) {
                    tiku_desk_rect_t lr = list_rect(&a);
                    int lx = ev.xbutton.x - lr.x, ly = ev.xbutton.y - lr.y;
                    tiku_trk_pose_t *hit =
                        tiku_trk_poseview_pose_at(a.view, lx, ly);
                    unsigned mods = 0;
                    int64_t now = tiku_trk_now();

                    if (ev.xbutton.state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
                    if (ev.xbutton.state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }

                    if (hit != NULL) {
                        int idx = -1, k, cnt = tiku_trk_poseview_count(a.view);
                        for (k = 0; k < cnt; k++) {
                            if (tiku_trk_poseview_at(a.view, k) == hit) { idx = k; break; }
                        }
                        tiku_trk_poseview_click(a.view, idx, mods);
                        if (idx >= 0 && idx == last_click_row &&
                            now - last_click_us < 400000) {
                            open_selection(&a);
                            last_click_row = -1;
                        } else {
                            last_click_row = idx;
                            last_click_us = now;
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
                        a.band_x0 = a.band_x1 = ev.xbutton.x;
                        a.band_y0 = a.band_y1 = ev.xbutton.y;
                    }
                    a.dirty = 1;
                    break;
                }
                {
                    int row = row_at(&a, ev.xbutton.x, ev.xbutton.y);
                    unsigned mods = 0;
                    int64_t now = tiku_trk_now();
                    int on_name = 0;

                    if (ev.xbutton.state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
                    if (ev.xbutton.state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }
                    if (ev.xbutton.button == 3) {
                        /* PVS-015: a right-click disarms a pending rename;
                         * PVS-018: and normalizes the selection first. */
                        tiku_trk_poseview_cancel_edit_wait(a.view);
                        (void)tiku_trk_poseview_context_click(a.view, row);
                        a.dirty = 1;
                        break;
                    }
                    /* Whether the press landed on the NAME, which is the
                     * only widget a rename can start from. */
                    {
                        const tiku_trk_column_t *cs;
                        int nn = tiku_trk_poseview_columns(a.view, &cs);
                        int cx = BORDER, k;

                        for (k = 0; k < nn; k++) {
                            if (ev.xbutton.x >= cx &&
                                ev.xbutton.x < cx + cs[k].width) {
                                on_name = (cs[k].field == TIKU_TRK_COL_NAME);
                                break;
                            }
                            cx += cs[k].width;
                        }
                    }
                    tiku_trk_poseview_click(a.view, row, mods);
                    /* PVS-010: a second click on the same row within the
                     * double-click window opens it. */
                    if (row >= 0 && row == last_click_row &&
                        now - last_click_us < 400000) {
                        tiku_desk_rect_t lr = list_rect(&a);
                        tiku_desk_rect_t from;

                        from.x = lr.x + 2;
                        from.y = lr.y + 2 + (row -
                                    tiku_trk_poseview_scroll(a.view)) *
                                    a.row_h;
                        from.w = 200;
                        from.h = a.row_h;
                        open_animation(&a, from);
                        open_selection(&a);
                        last_click_row = -1;
                    } else {
                        last_click_row = row;
                        last_click_us = now;
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
                    a.press_x = ev.xbutton.x;
                    a.press_y = ev.xbutton.y;
                    a.press_row = row;
                    a.press_down = 1;
                    a.dirty = 1;
                }
                break;
            }
            case MotionNotify: {
                int cx = 0, cy = 0;

                if (tiku_trk_titleview_tracking(&a.tv) !=
                        TIKU_TRK_TRACK_NONE ||
                    title_hit(&a, ev.xmotion.x, ev.xmotion.y, NULL, NULL)) {
                    unsigned tf;

                    (void)title_hit(&a, ev.xmotion.x, ev.xmotion.y, &cx,
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
                            if (a.dpy == NULL) {
                                /* No pointer to dress, but the STATE is
                                 * still the rule being tested. */
                            } else if (want) {
                                XDefineCursor(a.dpy, a.win, a.resize_cur);
                            } else {
                                XUndefineCursor(a.dpy, a.win);
                            }
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
                    (ev.xmotion.x - a.press_x > 4 ||
                     a.press_x - ev.xmotion.x > 4 ||
                     ev.xmotion.y - a.press_y > 4 ||
                     a.press_y - ev.xmotion.y > 4)) {
                    /* PVS-015: a drag is not a slow click. */
                    tiku_trk_poseview_cancel_edit_wait(a.view);
                    a.press_down = 0;
                }
                if (a.band_active) {
                    unsigned mods = 0;
                    tiku_desk_rect_t lr = list_rect(&a);

                    if (ev.xmotion.state & ShiftMask)   { mods |= TIKU_TRK_MOD_SHIFT; }
                    if (ev.xmotion.state & ControlMask) { mods |= TIKU_TRK_MOD_CMD; }
                    a.band_x1 = ev.xmotion.x;
                    a.band_y1 = ev.xmotion.y;
                    (void)tiku_trk_poseview_select_rect(a.view,
                        a.band_x0 - lr.x, a.band_y0 - lr.y,
                        a.band_x1 - lr.x, a.band_y1 - lr.y, mods);
                    a.dirty = 1;
                } else if (a.drag_active) {
                    tiku_desk_rect_t lr = list_rect(&a);
                    int lx = ev.xmotion.x - lr.x, ly = ev.xmotion.y - lr.y;
                    int dx = lx - a.drag_x, dy = ly - a.drag_y;
                    int i, n = tiku_trk_poseview_count(a.view);
                    int snap = (ev.xmotion.state & ControlMask) ? 1 : 0;

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
            case ButtonRelease:
                if (tiku_trk_titleview_tracking(&a.tv) !=
                        TIKU_TRK_TRACK_NONE) {
                    int cx = 0, cy = 0;

                    (void)title_hit(&a, ev.xbutton.x, ev.xbutton.y, &cx,
                                    &cy);
                    (void)tiku_trk_titleview_mouse_up(&a.tv, cx, cy,
                        (ev.xbutton.state & ShiftMask) ? 1 : 0,
                        tiku_trk_now());
                    (void)tiku_trk_poseview_save_columns(a.view);
                    a.dirty = 1;
                    break;
                }
                a.press_down = 0;
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
            case FocusIn:
            case FocusOut:
                /* PVS-023: the selection stops being PAINTED while the
                 * window is inactive, and the pivot is parked so a
                 * shift-click on return still extends from where the user
                 * left off.  A pending rename does not survive the trip. */
                a.active = (ev.type == FocusIn);
                tiku_trk_poseview_window_activated(a.view, a.active);
                if (!a.active) {
                    tiku_trk_poseview_cancel_edit_wait(a.view);
                    if (a.editing) {
                        end_rename(&a, 1);   /* deactivation COMMITS */
                    }
                }
                a.dirty = 1;
                break;
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == a.del) { a.running = 0; }
                break;
            default:
                break;
            }
            if (sc != NULL) {
                break;              /* one instruction per turn */
            }
        }
        if (a.fs != NULL && tiku_trk_fs_busy(a.fs)) {
            const tiku_trk_progress_t *pr;

            /* Stepped, not blocking: the window keeps drawing while a copy
             * runs, which is the whole reason the engine has a cursor. */
            (void)tiku_trk_fs_step(a.fs, 4);
            pr = tiku_trk_fs_progress(a.fs);
            if (pr->conflict) {
                /* A collision waits for an answer; with no dialog yet, the
                 * safe answer is to keep both rather than overwrite. */
                {   /* Show the leaf: the full path does not fit the
                     * status line and the name is what identifies it.
                     * Bounded by hand so a long name shortens rather than
                     * pushing the sentence off the end. */
                    const char *slash = strrchr(pr->conflict_path, '/');
                    const char *leaf = (slash != NULL) ? slash + 1
                                                       : pr->conflict_path;
                    char shown[80];
                    size_t ll = strlen(leaf);

                    if (ll >= sizeof shown) { ll = sizeof shown - 1u; }
                    memcpy(shown, leaf, ll);
                    shown[ll] = '\0';
                    snprintf(a.status, sizeof a.status,
                             "'%s' exists -- keeping both", shown);
                }
                tiku_trk_fs_resolve(a.fs, TIKU_TRK_CONFLICT_RENAME);
            } else if (pr->error_ask) {
                /* The original puts up a Cancel/OK alert here.  With no
                 * alert layer, the message is shown and the job carries on
                 * -- the answer that loses least, since cancelling would
                 * throw away the items that were about to work. */
                snprintf(a.status, sizeof a.status, "%.200s", pr->error);
                tiku_trk_fs_resolve_error(a.fs, 1);
            } else {
                snprintf(a.status, sizeof a.status, "%d of %d%s%s",
                         pr->done + pr->skipped, pr->total,
                         pr->skipped ? " (some skipped)" : "",
                         pr->failed ? " -- some failed" : "");
            }
            if (!tiku_trk_fs_busy(a.fs)) {
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
                (void)tiku_desk_surface_png(a.surf, shot);
                printf("wrote %s\n", shot);
                fflush(stdout);
                shot_at_us = 0;
            }
        }
        if (deadline_us > 0 && tiku_trk_now() > deadline_us) {
            a.running = 0;
        }
        if (sc == NULL) {
            usleep(10000);          /* a script has no reason to wait */
        }
    }

    if (a.dpy != NULL) {
        XDestroyImage(a.img);
        XFreeGC(a.dpy, a.gc);
        XCloseDisplay(a.dpy);
    }
    tiku_desk_surface_free(a.surf);
    /* Quitting waits for work already under way, up to five seconds
     * (AW-009): the alternative is a copy abandoned mid-file with
     * nothing to say so. */
    drain_fs(a.fs);
    if (sc != NULL) {
        rc = tiku_trk_script_report(sc, "tracker");
        tiku_trk_script_free(sc);
    }
    tiku_trk_fs_free(a.fs);
    tiku_trk_poseview_free(a.view);
    tiku_trk_backend_close(a.backend);
    return rc;
}
