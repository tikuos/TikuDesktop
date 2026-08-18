/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_window.c - folder-window state and rendering.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_trk_attr.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_volume.h"

#define COUNT_HEIGHT 16
#define TITLE_HEIGHT 20
#define MINI_ICON    16
#define VALUE_GAP   120

static int
measure_label(const char *text, void *context)
{
    (void)context;
    return tiku_desk_text_width(tiku_desk_font_plain(), text);
}

static int
icon_hit(const tiku_trk_model_t *model, int size, int x, int y,
         void *context)
{
    tiku_trk_folder_render_t *render = context;
    char art[TIKU_TRK_NAME_MAX];

    (void)tiku_trk_nodeicon_resolve(render->icons, render->store, model,
                                    art, sizeof art);
    return tiku_trk_icon_hit(art, size, x, y);
}

void
tiku_trk_folder_window_configure(tiku_trk_folder_window_t *folder,
                                  tiku_trk_folder_render_t *render)
{
    const tiku_desk_font_t *font = tiku_desk_font_plain();

    folder->render = render;
    folder->count_last_total = -1;
    folder->count_last_selected = -1;
    tiku_trk_poseview_set_icons(folder->view, render->icons);
    tiku_trk_poseview_set_measure(folder->view, measure_label, render);
    tiku_trk_poseview_set_font_metrics(folder->view, font->height,
                                       font->ascent);
    tiku_trk_poseview_set_icon_hit(folder->view, icon_hit, render);
}

tiku_desk_rect_t
tiku_trk_folder_window_content(const tiku_trk_folder_window_t *folder)
{
    tiku_trk_folder_render_t *render = folder->render;

    if (folder->win != NULL) {
        tiku_desk_rect_t content = tiku_desk_window_content(folder->win);

        content.y += render->menus->bar.height;
        content.h -= render->menus->bar.height;
        if (tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
            content.y += TITLE_HEIGHT;
            content.h -= TITLE_HEIGHT;
        }
        if (content.h < 0) {
            content.h = 0;
        }
        return content;
    }
    return (tiku_desk_rect_t){ 0, 0, render->screen_w, render->screen_h };
}

tiku_desk_rect_t
tiku_trk_folder_window_title(const tiku_trk_folder_window_t *folder)
{
    tiku_desk_rect_t title = { 0, 0, 0, 0 };

    if (folder != NULL && folder->win != NULL &&
        tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        title = tiku_desk_window_content(folder->win);
        title.y += folder->render->menus->bar.height;
        title.h = TITLE_HEIGHT;
    }
    return title;
}

tiku_desk_rect_t
tiku_trk_folder_window_icon(const tiku_trk_folder_window_t *folder)
{
    tiku_desk_rect_t bar = tiku_trk_folder_window_bar(folder);
    int size = bar.h - 4;

    if (size > 24) {
        size = 24;
    }
    if (size < 12) {
        size = 12;
    }
    return (tiku_desk_rect_t){ bar.x + bar.w - size - 3, bar.y + 2,
                               size, size };
}

tiku_desk_rect_t
tiku_trk_folder_window_bar(const tiku_trk_folder_window_t *folder)
{
    tiku_desk_rect_t bar = { 0, 0, 0, 0 };

    if (folder != NULL && folder->win != NULL) {
        bar = tiku_desk_window_content(folder->win);
        bar.h = folder->render->menus->bar.height;
    }
    return bar;
}

static void
draw_space_bar(tiku_trk_folder_render_t *render, tiku_desk_surface_t *surface,
               const tiku_trk_model_t *model, int x, int y, int size,
               int vertical)
{
    tiku_desk_rect_t bar;
    int used = 0;
    tiku_trk_space_t space = tiku_trk_volume_space_facts(model->facts.total,
                                                         model->facts.avail,
                                                         &used);
    const char *key = space == TIKU_TRK_SPACE_WARNING
                          ? "WarningSpaceColor" : "UsedSpaceColor";
    int32_t used_color = tiku_trk_prefs_get(render->prefs, key);
    uint32_t free_color = (uint32_t)tiku_trk_prefs_get(
        render->prefs, "FreeSpaceColor");
    int thickness = size >= 32 ? (7 * (size + 1)) / 32 : 3;

    if (thickness < 2) {
        thickness = 2;
    }
    if (vertical) {
        bar = (tiku_desk_rect_t){ x + size - thickness, y,
                                  thickness, size };
    } else {
        bar = (tiku_desk_rect_t){ x, y + size - 3, size, 3 };
    }
    tiku_desk_fill(surface, bar,
        TIKU_DESK_RGB((free_color >> 16) & 0xffu,
                      (free_color >> 8) & 0xffu, free_color & 0xffu));
    if (vertical) {
        bar.h = size * used / 100;
    } else {
        bar.w = size * used / 100;
    }
    if (bar.w > 0 && bar.h > 0) {
        tiku_desk_fill(surface, bar,
            TIKU_DESK_RGB(((uint32_t)used_color >> 16) & 0xffu,
                          ((uint32_t)used_color >> 8) & 0xffu,
                          (uint32_t)used_color & 0xffu));
    }
}

static void
draw_pose_icon(tiku_trk_folder_window_t *folder,
               tiku_desk_surface_t *surface, tiku_trk_pose_t *pose,
               int origin_x, int origin_y)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    int ix, iy, iw, ih, lx, ly, lw, lh;
    int selected = tiku_trk_poseview_draws_selected(folder->view, pose);
    char art[TIKU_TRK_NAME_MAX];
    tiku_desk_rect_t label;

    tiku_trk_poseview_pose_parts(folder->view, pose, &ix, &iy, &iw, &ih,
                                 &lx, &ly, &lw, &lh);
    ix -= origin_x;
    iy -= origin_y;
    lx -= origin_x;
    ly -= origin_y;
    (void)tiku_trk_nodeicon_resolve(render->icons, render->store,
                                    &pose->model, art, sizeof art);
    if (selected) {
        (void)tiku_trk_icon_paint_selected(surface, art, ix, iy, iw);
    } else if (pose->model.facts.link_broken) {
        (void)tiku_trk_icon_paint_dim(surface, art, ix, iy, iw, 0.55f,
                                      TIKU_DESK_C_DOC);
    } else {
        (void)tiku_trk_icon_paint(surface, art, ix, iy, iw);
    }
    if ((pose->model.kind == TIKU_TRK_KIND_VOLUME ||
         pose->model.kind == TIKU_TRK_KIND_ROOT) &&
        tiku_trk_prefs_get(render->prefs, "ShowVolumeSpaceBar")) {
        draw_space_bar(render, surface, &pose->model, ix, iy, iw, 1);
    }
    label = (tiku_desk_rect_t){ lx - 2, ly, lw + 4,
                                 lh > 0 ? lh : font->height };
    if (selected) {
        tiku_desk_fill(surface, label, TIKU_DESK_C_SELECT);
    }
    {
        tiku_desk_rgb_t ink = TIKU_DESK_C_TEXT;

        if (folder->win == NULL) {
            tiku_desk_rgb_t background = TIKU_DESK_C_BACKDROP;
            int luminance = ((int)((background >> 16) & 0xffu) * 299 +
                             (int)((background >> 8) & 0xffu) * 587 +
                             (int)(background & 0xffu) * 114) / 1000;

            ink = luminance <= 127 ? TIKU_DESK_RGB(255, 255, 255)
                                   : TIKU_DESK_RGB(0, 0, 0);
        }
        char fit[TIKU_TRK_NAME_MAX * 2];

        (void)tiku_trk_poseview_fit_label(folder->view, pose, fit,
                                          sizeof fit);
        if (folder->bg_drawn && !selected) {
            /* Over a picture the ink alone is unreadable wherever the
             * picture is the same colour, so the label carries a halo of
             * the opposite one (PVL-071).  A selected label has its own
             * filled box and needs none. */
            tiku_trk_paint_text_halo(surface, font, lx, ly + font->ascent,
                                     fit, ink);
        } else {
            tiku_desk_text(surface, font, lx, ly + font->ascent, fit,
                           selected ? TIKU_DESK_C_SELTEXT : ink);
        }
    }
}

int
tiku_trk_folder_window_draw_background(tiku_trk_folder_window_t *folder,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content)
{
    tiku_trk_folder_render_t *render = folder->render;
    tiku_trk_store_t *store;
    tiku_trk_bg_t bg;
    int i, dx, dy, dw, dh;

    /* The BACKEND's store, where the folder's other per-node state lives:
     * the session store is per device and knows nothing about one folder. */
    if (render == NULL || render->backend == NULL ||
        render->backend->ops->state_store == NULL) {
        return 0;
    }
    store = render->backend->ops->state_store(render->backend);
    if (store == NULL) {
        return 0;
    }
    if (!tiku_trk_bg_load(store, folder->path, &bg) &&
        !tiku_trk_bg_load(store, TIKU_TRK_BG_TEMPLATE_NODE, &bg)) {
        return 0;
    }
    if (strcmp(bg.path, folder->bg_shown) != 0) {
        tiku_trk_image_free(&folder->bg_image);
        folder->bg_shown[0] = '\0';
        if (tiku_trk_image_load(bg.path, &folder->bg_image) !=
            TIKU_TRK_IMG_OK) {
            return 0;
        }
        snprintf(folder->bg_shown, sizeof folder->bg_shown, "%s", bg.path);
    }
    for (i = 0; tiku_trk_bg_place(&bg, content.w, content.h,
                                  folder->bg_image.w, folder->bg_image.h,
                                  i, &dx, &dy, &dw, &dh); i++) {
        tiku_desk_rect_t dst = { content.x + dx, content.y + dy, dw, dh };

        tiku_trk_paint_image(surface, &folder->bg_image, dst, content);
    }
    return 1;
}

void
tiku_trk_folder_window_draw_icons(tiku_trk_folder_window_t *folder,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content)
{
    int first = -1, last = -1, i, origin_x = 0, origin_y = 0;

    tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
    origin_x -= content.x + 2;
    origin_y -= content.y + 2;
    if (!tiku_trk_poseview_visible_range(folder->view, &first, &last)) {
        return;
    }
    for (i = first; i <= last; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose != NULL && pose->placed) {
            draw_pose_icon(folder, surface, pose, origin_x, origin_y);
        }
    }
}

/** @brief Ask for this row's picture if it wants one and has none yet. */
static void
queue_thumbnail(tiku_trk_folder_window_t *folder, tiku_trk_pose_t *pose)
{
    tiku_trk_folder_render_t *render = folder->render;
    char art[TIKU_TRK_NAME_MAX];
    int mini = render->row_h - 2;

    if (mini > MINI_ICON) {
        mini = MINI_ICON;
    }
    if (tiku_trk_nodeicon_resolve(render->icons, render->store, &pose->model,
                                  art, sizeof art) != TIKU_TRK_ICON_FROM_THUMB) {
        return;
    }
    if (!tiku_trk_thumb_fresh(render->store, pose->model.path,
                              pose->model.facts.mtime)) {
        (void)tiku_trk_thumb_queue(render->thumbs, pose->model.path,
                                   pose->model.facts.size, mini);
    }
}

/** @brief Fold @p n bytes into a running hash (FNV-1a). */
static uint32_t
sig_bytes(uint32_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    size_t i;

    for (i = 0u; i < n; i++) {
        h = (h ^ b[i]) * UINT32_C(16777619);
    }
    return h;
}

static uint32_t
sig_text(uint32_t h, const char *t)
{
    return sig_bytes(h, t, strlen(t) + 1u);
}

/**
 * @brief What row @p slot would draw, in one number.
 *
 * Everything the row's appearance turns on, and nothing else: two frames
 * that hash the same put the same pixels there, so the second need not
 * draw.  A field the drawing reads and this does not is a row that stops
 * refreshing -- when the drawing gains one, it gains one here.
 */
static uint32_t
row_signature(tiku_trk_folder_window_t *folder, tiku_trk_pose_t *pose,
              int slot, int selected, int cut)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_trk_column_t *cols = NULL;
    uint32_t h = UINT32_C(2166136261);
    char art[TIKU_TRK_NAME_MAX];
    int ncol = tiku_trk_poseview_columns(folder->view, &cols);
    int hscroll = tiku_trk_poseview_hscroll(folder->view);
    int j;

    h = sig_bytes(h, &pose->model.node_id, sizeof pose->model.node_id);
    h = sig_bytes(h, &selected, sizeof selected);
    h = sig_bytes(h, &cut, sizeof cut);
    h = sig_bytes(h, &pose->model.kind, sizeof pose->model.kind);
    h = sig_bytes(h, &pose->model.facts.link_broken,
                  sizeof pose->model.facts.link_broken);
    h = sig_bytes(h, &pose->model.facts.avail, sizeof pose->model.facts.avail);
    h = sig_bytes(h, &hscroll, sizeof hscroll);
    h = sig_bytes(h, &render->row_h, sizeof render->row_h);
    /* The editor draws over the row it is on, so that row differs from the
     * same row without it. */
    if (*render->renaming && *render->rename_folder == folder &&
        *render->rename_row == pose->index) {
        h = sig_bytes(h, "edit", 5u);
    }
    (void)tiku_trk_nodeicon_resolve(render->icons, render->store,
                                    &pose->model, art, sizeof art);
    h = sig_text(h, art);
    if (tiku_trk_thumb_fresh(render->store, pose->model.path,
                             pose->model.facts.mtime)) {
        h = sig_bytes(h, "thumb", 6u);
    }
    for (j = 0; j < ncol; j++) {
        char tmp[160];
        const char *txt = tiku_trk_poseview_cell_text(folder->view, pose,
                              cols[j].field, tmp, sizeof tmp);

        h = sig_text(h, txt != NULL ? txt : "");
        h = sig_bytes(h, &cols[j].offset, sizeof cols[j].offset);
        h = sig_bytes(h, &cols[j].width, sizeof cols[j].width);
    }
    (void)slot;
    return h;
}

static void
draw_list(tiku_trk_folder_window_t *folder, tiku_desk_surface_t *surface,
          tiku_desk_rect_t list, int rows, int scroll, int total)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    const tiku_trk_repaint_rect_t *damage = folder->render->damage;
    int ndamage = folder->render->ndamage;
    int i;

    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view,
                                                     scroll + i);
        tiku_desk_rect_t row = { list.x + 2,
            list.y + 2 + i * render->row_h, list.w - 4, render->row_h };
        int text_y = row.y + (render->row_h - font->height) / 2 +
                     font->ascent;
        int selected, cut;

        if (pose == NULL) {
            break;
        }
        selected = tiku_trk_poseview_draws_selected(folder->view, pose);
        cut = tiku_trk_clip_mode_of(render->clip, pose->model.node_id) ==
              TIKU_TRK_CLIP_MOVE;
        if (*render->drop_folder == folder && *render->drop_row == i) {
            selected = 1;
        }
        if (ndamage > 0 && i < TIKU_TRK_ROWS_KEPT) {
            /* Two reasons to paint a row: the place needs it, or the row
             * looks different from what is there.  Damage alone would
             * leave a selection nobody thought to damage unpainted. */
            uint32_t sig = row_signature(folder, pose, i, selected, cut);
            int d, hit = 0;

            for (d = 0; d < ndamage && !hit; d++) {
                int top = row.y - list.y - 2;

                hit = damage[d].y < top + render->row_h &&
                      damage[d].y + damage[d].h > top;
            }
            if (!hit && folder->row_sig[i] == sig) {
                /* Skipping the PAINT must not skip the asking: a row's
                 * thumbnail is queued while it is drawn, and a row that
                 * stops being drawn would stop ever being asked for. */
                queue_thumbnail(folder, pose);
                continue;
            }
            folder->row_sig[i] = sig;
        } else if (i < TIKU_TRK_ROWS_KEPT) {
            folder->row_sig[i] = row_signature(folder, pose, i, selected,
                                               cut);
        }
        render->rows_drawn++;
        tiku_desk_fill(surface, row, TIKU_DESK_C_DOC);
        {
            const tiku_trk_column_t *cols = NULL;
            int ncol = tiku_trk_poseview_columns(folder->view, &cols);
            int hscroll = tiku_trk_poseview_hscroll(folder->view);
            int mini = render->row_h - 2;
            int name_end = row.x + row.w;
            int thumbnail = 0, j;
            char art[TIKU_TRK_NAME_MAX];
            tiku_trk_icon_from_t source = tiku_trk_nodeicon_resolve(
                render->icons, render->store, &pose->model, art, sizeof art);

            if (mini > MINI_ICON) {
                mini = MINI_ICON;
            }
            /* IV-024: the selection covers the icon and the NAME column,
             * and stops there -- the other columns stay normal, which is
             * what tells a reader they are other columns. */
            if (ncol > 0) {
                name_end = list.x + 2 + cols[0].offset + cols[0].width -
                           hscroll;
            }
            if (selected) {
                tiku_desk_rect_t name = { row.x, row.y,
                                          name_end - row.x, row.h };

                if (name.w < 0) {
                    name.w = 0;
                }
                tiku_desk_fill(surface, name, TIKU_DESK_C_SELECT);
            }
            if ((pose->model.kind == TIKU_TRK_KIND_VOLUME ||
                 pose->model.kind == TIKU_TRK_KIND_ROOT) &&
                tiku_trk_prefs_get(render->prefs, "ShowVolumeSpaceBar")) {
                draw_space_bar(render, surface, &pose->model, row.x + 2,
                    row.y + (render->row_h - mini) / 2 + 2, mini, 0);
            }
            if (source == TIKU_TRK_ICON_FROM_THUMB) {
                tiku_trk_image_t image;

                if (tiku_trk_thumb_fresh(render->store, pose->model.path,
                                         pose->model.facts.mtime) &&
                    tiku_trk_thumb_load(render->store, pose->model.path,
                                        &image) == 0) {
                    tiku_trk_thumb_paint(surface, &image, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini);
                    tiku_trk_image_free(&image);
                    thumbnail = 1;
                } else {
                    (void)tiku_trk_thumb_queue(render->thumbs,
                        pose->model.path, pose->model.facts.size, mini);
                }
            }
            if (!thumbnail) {
                (void)((pose->model.facts.link_broken || cut)
                    ? tiku_trk_icon_paint_dim(surface, art, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini, 0.55f,
                        TIKU_DESK_C_DOC)
                    : selected
                    ? tiku_trk_icon_paint_selected(surface, art, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini)
                    : tiku_trk_icon_paint(surface, art, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini));
            }
            /* A row IS its columns.  Through the ENGINE's formatter, not a
             * second copy: two implementations of "what does this cell
             * say" is how a column comes to be fitted to text the row does
             * not draw (PVL-032, TS-005). */
            for (j = 0; j < ncol; j++) {
                char tmp[160], fitted[96];
                const char *txt = tiku_trk_poseview_cell_text(folder->view,
                    pose, cols[j].field, tmp, sizeof tmp);
                int cell_x = list.x + 2 + cols[j].offset - hscroll;
                tiku_desk_rect_t cell = { cell_x, row.y, cols[j].width,
                                          row.h };
                int tw;

                {   /* Intersected with the list, not substituted for it:
                     * a cell scrolled left of the view would otherwise
                     * paint over the border and the window frame. */
                    tiku_desk_rect_t inner = tiku_desk_inset(list, 2);
                    int x0 = (cell.x > inner.x) ? cell.x : inner.x;
                    int x1 = (cell.x + cell.w < inner.x + inner.w)
                             ? cell.x + cell.w : inner.x + inner.w;

                    cell.x = x0;
                    cell.w = (x1 > x0) ? x1 - x0 : 0;
                    tiku_desk_clip_set(surface, cell);
                }
                (void)tiku_trk_text_fit(txt, cols[j].width,
                    (cols[j].field == TIKU_TRK_COL_NAME)
                        ? TIKU_TRK_TRUNC_MIDDLE : TIKU_TRK_TRUNC_END,
                    measure_label, NULL, fitted, sizeof fitted);
                tw = tiku_desk_text_width(font, fitted);
                if (cols[j].alignment == TIKU_TRK_ALIGN_RIGHT) {
                    cell_x += cols[j].width - tw;
                }
                tiku_desk_text(surface, font, cell_x, text_y, fitted,
                    (selected && j == 0)
                        ? TIKU_DESK_C_SELTEXT
                        : (cut ? tiku_desk_tint(TIKU_DESK_C_TEXT,
                                                TIKU_DESK_LIGHTEN_2)
                               : TIKU_DESK_C_TEXT));
                if (cols[j].field == TIKU_TRK_COL_NAME &&
                    pose->model.kind == TIKU_TRK_KIND_SYMLINK) {
                    /* MA-076/IV-043: under the TEXT only, and only in the
                     * first column -- a rule that underlined the whole
                     * cell would read as a divider. */
                    tiku_desk_hline(surface, cell_x, text_y + 2, tw,
                                    TIKU_DESK_C_TEXT);
                }
            }
            tiku_desk_clip_set(surface, tiku_desk_inset(list, 2));
        }
    }
}

static void
draw_title(tiku_trk_folder_window_t *folder, tiku_desk_surface_t *surface,
           tiku_desk_rect_t title)
{
    const tiku_trk_column_t *columns;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    int n, i;

    tiku_desk_fill(surface, title, TIKU_DESK_C_PANEL);
    n = tiku_trk_poseview_columns(folder->view, &columns);
    tiku_desk_clip_set(surface, title);
    for (i = 0; i < n; i++) {
        const tiku_trk_column_t *c = &columns[i];
        tiku_desk_rect_t r = { title.x + c->offset - TIKU_TRK_COL_LEFT_MARGIN,
                               title.y, c->width + TIKU_TRK_COL_MARGIN,
                               title.h };
        char fit[64];
        int tw, tx;

        tiku_desk_fill(surface, r, TIKU_DESK_C_PANEL);
        tiku_desk_bevel(surface, r,
                        tiku_desk_tint(TIKU_DESK_C_PANEL, 0.0f),
                        tiku_desk_tint(TIKU_DESK_C_PANEL, 1.06f));
        (void)tiku_trk_text_fit(c->title, c->width, TIKU_TRK_TRUNC_END,
                                measure_label, NULL, fit, sizeof fit);
        tw = tiku_desk_text_width(font, fit);
        if (c->alignment == TIKU_TRK_ALIGN_RIGHT) {
            tx = title.x + c->offset + c->width - tw;
        } else if (c->alignment == TIKU_TRK_ALIGN_CENTER) {
            tx = r.x + (r.w - tw) / 2;
        } else {
            tx = title.x + c->offset + 1;
        }
        tiku_desk_text(surface, font, tx,
                       title.y + (title.h - font->height) / 2 + font->ascent,
                       fit, TIKU_DESK_C_TEXT);
    }
    tiku_desk_clip_reset(surface);
    tiku_desk_hline(surface, title.x, title.y + title.h - 1, title.w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
}

static void
draw_folder_icon(tiku_trk_folder_window_t *folder,
                 tiku_desk_surface_t *surface)
{
    tiku_trk_folder_render_t *render = folder->render;
    tiku_trk_model_t model;
    char art[TIKU_TRK_NAME_MAX];
    tiku_desk_rect_t icon;

    if (render->backend == NULL || render->backend->ops == NULL ||
        render->backend->ops->stat(render->backend, folder->path,
                                   &model) != 0) {
        return;
    }
    (void)tiku_trk_nodeicon_resolve(render->icons, render->store, &model,
                                    art, sizeof art);
    icon = tiku_trk_folder_window_icon(folder);
    (void)tiku_trk_icon_paint(surface, art, icon.x, icon.y, icon.w);
}

/**
 * @brief The window's own pixels, sized to the rows it shows.
 *
 * A new or resized surface holds nothing, so the frame that makes one
 * paints all of it.
 */
static int
keep_pixels(tiku_trk_folder_window_t *folder, int w, int h, int scale)
{
    if (w <= 0 || h <= 0) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    if (folder->kept.px == NULL || folder->kept.w != w ||
        folder->kept.h != h || folder->kept.scale != scale) {
        tiku_desk_rgb_t *px = realloc(folder->kept.px,
                                      (size_t)w * (size_t)scale *
                                      (size_t)h * (size_t)scale * sizeof *px);

        if (px == NULL) {
            return 0;
        }
        folder->kept.px = px;
        folder->kept.w = w;
        folder->kept.h = h;
        folder->kept.scale = scale;
        folder->kept_ready = 0;
    }
    folder->kept.clip = (tiku_desk_rect_t){ 0, 0, w, h };
    return 1;
}

void
tiku_trk_folder_window_forget(tiku_trk_folder_window_t *folder)
{
    if (folder != NULL) {
        free(folder->kept.px);
        folder->kept.px = NULL;
        folder->kept.w = 0;
        folder->kept.h = 0;
        folder->kept_ready = 0;
    }
}

static void
draw_transients(tiku_trk_folder_window_t *folder,
                tiku_desk_surface_t *surface, tiku_desk_rect_t list,
                int off_x, int off_y, int icons, int rows, int scroll)
{
    tiku_trk_folder_render_t *render = folder->render;

    if (*render->band_folder == folder) {
        tiku_desk_rect_t band;
        int x0 = *render->band_x0, y0 = *render->band_y0;
        int x1 = *render->band_x1, y1 = *render->band_y1;

        /* The band is where the pointer has been, which is the screen's
         * coordinates and not the surface being painted. */
        x0 -= off_x;
        x1 -= off_x;
        y0 -= off_y;
        y1 -= off_y;
        band = (tiku_desk_rect_t){ x0 < x1 ? x0 : x1,
                                   y0 < y1 ? y0 : y1,
                                   x0 > x1 ? x0 - x1 : x1 - x0,
                                   y0 > y1 ? y0 - y1 : y1 - y0 };
        if (band.w > 0 && band.h > 0) {
            tiku_trk_paint_band(surface, band,
                tiku_trk_prefs_get(render->prefs, "TransparentSelection"));
        }
    }
    if (*render->renaming && *render->rename_folder == folder && !icons) {
        int at = *render->rename_row - scroll;

        if (at >= 0 && at < rows) {
            tiku_desk_rect_t box = { list.x + 2,
                list.y + 2 + at * render->row_h,
                list.w - 4 > 260 ? 260 : list.w - 4, render->row_h };

            tiku_desk_ui_textfield_sel(surface, box, render->rename_edit->text,
                render->rename_edit->caret, render->rename_edit->sel_a,
                render->rename_edit->sel_b, TIKU_DESK_S_FOCUS);
        }
    }
}

static void
draw_count(tiku_trk_folder_window_t *folder, tiku_desk_surface_t *surface,
           tiku_desk_rect_t content, int total)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    tiku_desk_rect_t count = { content.x,
        content.y + content.h - COUNT_HEIGHT, content.w, COUNT_HEIGHT };
    const char *typed = tiku_trk_poseview_type_string(folder->view);
    char label[256];
    int focused = folder->win ==
                  tiku_desk_workspace_focused(render->workspace);

    if (*render->finding && focused) {
        tiku_desk_ui_sunken(surface, count, TIKU_DESK_C_DOC);
        snprintf(label, sizeof label, "find: %.60s_", render->find_text);
    } else if (*render->editing && focused) {
        const char *slash = strrchr(render->edit_path, '/');

        tiku_desk_ui_sunken(surface, count, TIKU_DESK_C_DOC);
        snprintf(label, sizeof label, "%.100s = %.64s_",
                 slash != NULL ? slash + 1 : render->edit_path,
                 render->value_edit->text);
    } else if (render->status[0] != '\0') {
        tiku_desk_fill(surface, count, TIKU_DESK_C_PANEL);
        snprintf(label, sizeof label, "%s", render->status);
    } else if (typed[0] != '\0') {
        tiku_desk_fill(surface, count, TIKU_DESK_C_PANEL);
        snprintf(label, sizeof label, "%s", typed);
    } else {
        int selected = tiku_trk_poseview_selection_count(folder->view);

        tiku_desk_fill(surface, count, TIKU_DESK_C_PANEL);
        if (total == folder->count_last_total &&
            selected == folder->count_last_selected &&
            folder->count_text[0] != '\0') {
            snprintf(label, sizeof label, "%s", folder->count_text);
        } else {
            if (total == 0) {
                snprintf(label, sizeof label, "empty folder");
            } else if (selected > 0) {
                snprintf(label, sizeof label, "%d/%d selected", selected,
                         total);
            } else {
                snprintf(label, sizeof label, "%d item%s", total,
                         total == 1 ? "" : "s");
            }
            folder->count_last_total = total;
            folder->count_last_selected = selected;
            snprintf(folder->count_text, sizeof folder->count_text, "%s",
                     label);
        }
        /* The shell reads one count line, and the one it means is the
         * front window's: draw order is z-order, so the last window to
         * write here is the one on top. */
        snprintf(render->count_text, render->count_text_size, "%s", label);
    }
    tiku_desk_text(surface, font, count.x + 4,
        count.y + (COUNT_HEIGHT - font->height) / 2 + font->ascent,
        label, TIKU_DESK_C_TEXT);
}

void
tiku_trk_folder_window_draw(tiku_desk_window_t *window,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content, void *context)
{
    tiku_trk_folder_window_t *folder = context;
    tiku_trk_folder_render_t *render = folder->render;
    tiku_desk_rect_t list = content;
    tiku_desk_rect_t bar = content;
    tiku_desk_rect_t title;
    tiku_desk_surface_t *paint;
    tiku_desk_rect_t area;
    int rows, scroll, total, icons;
    int slide, slide_y = 0;

    (void)window;
    if (render->prepare_menu != NULL) {
        render->prepare_menu(render->prepare_context, folder);
    }
    bar.h = render->menus->bar.height;
    tiku_trk_menubar_draw(&render->menus->bar, surface, bar);
    draw_folder_icon(folder, surface);
    list.y += bar.h;
    list.h -= bar.h;
    title = (tiku_desk_rect_t){ list.x, list.y, list.w, TITLE_HEIGHT };
    if (tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        draw_title(folder, surface, title);
        list.y += TITLE_HEIGHT;
        list.h -= TITLE_HEIGHT;
    }
    list.h -= COUNT_HEIGHT;
    icons = tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST;
    /* Where the rows go.  In list mode that is the window's own kept
     * pixels, drawn at its own origin and handed to the frame at the end;
     * the icon modes draw into the frame directly, having no damage to
     * spare. */
    paint = surface;
    area = list;
    if (!icons && keep_pixels(folder, list.w, list.h, surface->scale)) {
        paint = &folder->kept;
        area = (tiku_desk_rect_t){ 0, 0, list.w, list.h };
    }
    /* The ground and the rows are one decision.  Filling the whole list
     * and then painting only the damaged rows leaves the others as bare
     * ground: what a frame does not repaint, it must not erase.  Nothing
     * has to be told which rows a fill took with it, because every fill
     * here is a damage rectangle and every row a damage rectangle touches
     * is painted below in the same frame. */
    slide = tiku_trk_poseview_repaint_take_slide(folder->view, &slide_y);
    render->ndamage = (paint != surface && folder->kept_ready)
                          ? tiku_trk_poseview_repaint_take(folder->view,
                                render->damage, TIKU_TRK_REPAINT_MAX)
                          : 0;
    if (render->ndamage > 0) {
        tiku_desk_rect_t inner = tiku_desk_inset(area, 2);
        int d;

        if (slide != 0) {
            /* The rows the insertion or removal only MOVED: they are
             * carried to their new place rather than drawn again, which is
             * what leaves a single row to paint. */
            int rh = render->row_h > 0 ? render->row_h : 1;
            int by = slide / rh;
            int from = slide_y / rh;
            int k;

            /* Inside the rows and no further: the border below them is
             * not a row and must not be slid over. */
            tiku_desk_clip_set(paint, inner);
            tiku_desk_copy_bits(paint,
                                (tiku_desk_rect_t){ inner.x,
                                                    inner.y + slide_y,
                                                    inner.w,
                                                    inner.h - slide_y },
                                0, slide);
            tiku_desk_clip_reset(paint);
            /* What each slot holds moved with the pixels. */
            if (by > 0) {
                for (k = TIKU_TRK_ROWS_KEPT - 1; k >= from; k--) {
                    folder->row_sig[k] = (k - by >= from)
                        ? folder->row_sig[k - by] : 0u;
                }
            } else {
                for (k = from + by; k < TIKU_TRK_ROWS_KEPT; k++) {
                    folder->row_sig[k] = (k - by < TIKU_TRK_ROWS_KEPT)
                        ? folder->row_sig[k - by] : 0u;
                }
            }
        }
        for (d = 0; d < render->ndamage; d++) {
            tiku_desk_rect_t strip = { inner.x, inner.y + render->damage[d].y,
                                       inner.w, render->damage[d].h };

            if (strip.y < inner.y) {
                strip.h -= inner.y - strip.y;
                strip.y = inner.y;
            }
            if (strip.y + strip.h > inner.y + inner.h) {
                strip.h = inner.y + inner.h - strip.y;
            }
            if (strip.h > 0) {
                tiku_desk_fill(paint, strip,
                               tiku_trk_poseview_background(folder->view));
            }
        }
    } else {
        tiku_desk_ui_sunken(paint, area,
                            tiku_trk_poseview_background(folder->view));
    }
    tiku_trk_poseview_set_viewport(folder->view, list.w - 4, list.h - 4);
    tiku_trk_poseview_set_row_height(folder->view, render->row_h);
    if (icons) {
        (void)tiku_trk_poseview_place_unplaced(folder->view, list.w - 4);
    }
    rows = (list.h - 4) / render->row_h;
    tiku_trk_poseview_set_visible_rows(folder->view, rows);
    scroll = tiku_trk_poseview_scroll(folder->view);
    total = tiku_trk_poseview_count(folder->view);
    tiku_desk_clip_set(paint, tiku_desk_inset(area, 2));
    if (icons) {
        folder->bg_drawn = tiku_trk_folder_window_draw_background(
            folder, paint, tiku_desk_inset(area, 2));
        tiku_trk_folder_window_draw_icons(folder, paint, area);
    } else {
        draw_list(folder, paint, area, rows, scroll, total);
    }
    draw_transients(folder, paint, area, list.x - area.x, list.y - area.y,
                    icons, rows, scroll);
    tiku_desk_clip_reset(paint);
    if (paint != surface) {
        /* The frame gets the window's pixels whole, painted this time or
         * five frames ago. */
        tiku_desk_blit(surface, list.x, list.y, &folder->kept);
        folder->kept_ready = 1;
    }
    draw_count(folder, surface, content, total);
}

int
tiku_trk_folder_window_fit_width(const tiku_trk_folder_window_t *folder)
{
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    int widest = 0, i, count = tiku_trk_poseview_count(folder->view);
    int query = tiku_trk_poseview_is_query(folder->view);

    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
        char value[64];
        int width;

        if (pose == NULL) {
            continue;
        }
        width = MINI_ICON + 6 + tiku_desk_text_width(font,
            query ? pose->model.path : pose->model.name);
        if (!tiku_trk_model_is_container(&pose->model)) {
            int needed;

            (void)tiku_trk_attr_format(&pose->desc,
                pose->value_valid ? pose->value : "", value, sizeof value);
            needed = tiku_desk_text_width(font, value) + VALUE_GAP + 8;
            if (needed > width) {
                width = needed;
            }
        }
        if (width > widest) {
            widest = width;
        }
    }
    return widest < 200 ? 200 : widest + 12;
}

int
tiku_trk_folder_window_fit_height(const tiku_trk_folder_window_t *folder)
{
    int title = tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST
                    ? TITLE_HEIGHT : 0;

    return tiku_trk_poseview_count(folder->view) * folder->render->row_h +
           4 + COUNT_HEIGHT + title;
}

int
tiku_trk_folder_window_row_at(const tiku_trk_folder_window_t *folder,
                              int x, int y)
{
    tiku_desk_rect_t content = tiku_trk_folder_window_content(folder);
    int relative;

    if (x < content.x || x > content.x + content.w || y < content.y + 2 ||
        y > content.y + content.h - COUNT_HEIGHT) {
        return -1;
    }
    if (tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
        int origin_x = 0, origin_y = 0;
        tiku_trk_pose_t *pose;

        tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
        pose = tiku_trk_poseview_pose_at(folder->view,
            x - (content.x + 2) + origin_x,
            y - (content.y + 2) + origin_y);
        return pose != NULL ? pose->index : -1;
    }
    relative = (y - content.y - 2) / folder->render->row_h;
    relative += tiku_trk_poseview_scroll(folder->view);
    return relative < tiku_trk_poseview_count(folder->view) ? relative : -1;
}

int
tiku_trk_folder_window_count_at(const tiku_trk_folder_window_t *folder,
                                int x, int y)
{
    tiku_desk_rect_t content;

    if (folder == NULL) {
        return 0;
    }
    content = tiku_trk_folder_window_content(folder);
    return x >= content.x && x <= content.x + content.w &&
           y >= content.y + content.h - COUNT_HEIGHT &&
           y <= content.y + content.h;
}

int
tiku_trk_folder_window_name_at(const tiku_trk_folder_window_t *folder,
                               int row, int x, int y)
{
    tiku_desk_rect_t content = tiku_trk_folder_window_content(folder);

    if (row < 0) {
        return 0;
    }
    if (tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        return x < content.x + (content.w * 2) / 3;
    }
    {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, row);
        int origin_x = 0, origin_y = 0;
        int ix, iy, iw, ih, lx, ly, lw, lh;
        int view_x, view_y;

        if (pose == NULL) {
            return 0;
        }
        tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
        view_x = x - (content.x + 2) + origin_x;
        view_y = y - (content.y + 2) + origin_y;
        tiku_trk_poseview_pose_parts(folder->view, pose, &ix, &iy, &iw, &ih,
                                     &lx, &ly, &lw, &lh);
        return view_x >= lx && view_x < lx + lw &&
               view_y >= ly && view_y < ly + lh;
    }
}
