/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_context.c - the state, its geometry, and the context commands
 * run in
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/** @brief Hand a modal filesystem answer back to the operation engine. */
void
browser_app_alert_answer(void *context, const tiku_alert_t *alert)
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
void
browser_app_alert_stop(void *context)
{
    app_t *a = context;

    tiku_fs_cancel(a->fs);
}

/** @brief Index of the first selected row, or -1. */
int
browser_first_selected_index(app_t *a)
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

int
browser_pose_index(app_t *a, const tiku_trk_pose_t *wanted)
{
    int i, n = tiku_trk_poseview_count(a->view);

    for (i = 0; i < n; i++) {
        if (tiku_trk_poseview_at(a->view, i) == wanted) {
            return i;
        }
    }
    return -1;
}

int
browser_next_edit_row(app_t *a, int row, int backwards)
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
int
browser_index_of_path(app_t *a, const char *path)
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
int
browser_measure_prefix_px(const char *text, int n, void *ctx)
{
    char tmp[TIKU_FIELD_MAX];

    (void)ctx;
    if (n < 0) { n = 0; }
    if (n >= (int)sizeof tmp) { n = (int)sizeof tmp - 1; }
    memcpy(tmp, text, (size_t)n);
    tmp[n] = '\0';
    return tiku_text_width(tiku_font_plain(), tmp);
}

/** @brief The navigator takes no layout space while globally hidden. */
int
browser_navigator_height(const app_t *a)
{
    return a->show_nav ? NAV_H : 0;
}

/** @brief Geometry of the row area, derived once per draw. */
tiku_rect_t
browser_list_rect(const app_t *a)
{
    tiku_rect_t r;

    int titled = (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST);

    r.x = BORDER;
    /* The column title bar belongs to list mode; icon mode gets the space
     * back, and hit-testing must use the same rectangle drawing does. */
    r.y = TAB_H + BORDER + a->menus.bar.height + browser_navigator_height(a) +
          (titled ? TITLE_H : 0);
    r.w = a->surf->w - 2 * BORDER - (titled ? SCROLL_W : 0);
    r.h = a->surf->h - r.y - BORDER - COUNT_H;
    return r;
}

/** @brief Browser controls directly below the menu bar. */
tiku_rect_t
browser_navigator_rect(const app_t *a)
{
    tiku_rect_t bar = browser_menubar_rect(a);
    tiku_rect_t r = { BORDER, bar.y + bar.h,
                           a->surf->w - 2 * BORDER,
                           browser_navigator_height(a) };

    return r;
}

/** @brief Split the navigator into its three buttons and location field. */
void
browser_navigator_layout(const app_t *a, tiku_rect_t *back,
                 tiku_rect_t *forward, tiku_rect_t *up,
                 tiku_rect_t *field)
{
    tiku_rect_t r = browser_navigator_rect(a);

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
tiku_rect_t
browser_menubar_rect(const app_t *a)
{
    tiku_rect_t r = { BORDER, TAB_H + BORDER,
                           a->surf->w - 2 * BORDER,
                           a->menus.bar.height };

    return r;
}

void
browser_draw_folder_icon(app_t *a, tiku_rect_t bar)
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
tiku_trk_sc_ctx_t
browser_menu_ctx(app_t *a, unsigned mods)
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

int
browser_visible_rows(const app_t *a)
{
    tiku_rect_t r = browser_list_rect(a);

    return (a->row_h > 0) ? (r.h - 4) / a->row_h : 0;
}

/** @brief Width of @p s in the font @p ctx, for the text fitter. */
int
browser_measure_text(const char *s, void *ctx)
{
    return tiku_text_width(ctx, s);
}

/** @brief Where a column's data starts on screen, allowing for the scroll. */
int
browser_col_x(const app_t *a, const tiku_trk_column_t *c)
{
    return browser_list_rect(a).x + c->offset - tiku_trk_poseview_hscroll(a->view);
}

/**
 * @brief One column title (TS-017, TS-018).
 *
 * The rectangle is the column's, widened by both margins: the left margin is
 * where the sort mark lives, which is why the text starts one pixel inside
 * the DATA rather than inside the rectangle.
 */
void
browser_draw_title(app_t *a, tiku_rect_t strip, const tiku_trk_column_t *c,
           int pressed, int drag_dx)
{
    tiku_surface_t *s = a->surf;
    /* Three-quarters of the body text, the source's own strip proportion
     * (TS-019): the titles are labels ABOUT the rows, not rows. */
    const tiku_font_t *f =
        tiku_font_at(tiku_font_size() * 3 / 4);
    tiku_rect_t r;
    char fit[64];
    int tw, tx, data_x = browser_col_x(a, c) + drag_dx;

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
                            browser_measure_text, (void *)f, fit, sizeof fit);
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
