/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_draw.c - drawing the window: the list, the navigator, the
 * count, the popup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

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
void
browser_count_label(app_t *a, int width, char *label, size_t max)
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
                              browser_measure_text, (void *)f, fit,
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
                              browser_measure_text, (void *)f, label,
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
        (void)tiku_scroll_set_range(&a->hsb, content, list.w);
        if (!a->hsb.tracking) {
            (void)tiku_scroll_to(&a->hsb,
                                     tiku_trk_poseview_hscroll(a->view));
        }
        if (tiku_scroll_thumb(&a->hsb, a->hsb_rect.w, &tpos, &tlen)) {
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
    browser_count_label(a, count.w, label, sizeof label);
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
void
browser_draw_icons(app_t *a, tiku_rect_t list)
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
    (void)tiku_menu_track(a->popup, a->popup_frame, a->ptr_x,
                              a->ptr_y);
    tiku_menu_draw(a->popup, a->surf, a->popup_frame);
    if (a->popup_sub != NULL) {
        (void)tiku_menu_track(a->popup_sub, a->popup_sub_frame,
                                  a->ptr_x, a->ptr_y);
        tiku_menu_draw(a->popup_sub, a->surf, a->popup_sub_frame);
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
    browser_navigator_layout(a, &back, &forward, &up, &field);
    tiku_ui_panel(a->surf, browser_navigator_rect(a));
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

void
browser_draw(app_t *a)
{
    tiku_surface_t *s = a->surf;
    const tiku_font_t *f = tiku_font_plain();
    const tiku_trk_column_t *cols;
    tiku_rect_t win = { 0, 0, s->w, s->h };
    tiku_rect_t list, title, bar, count;
    tiku_trk_sc_ctx_t mc;
    int ncol = tiku_trk_poseview_columns(a->view, &cols);
    int rows = browser_visible_rows(a);
    int scroll = tiku_trk_poseview_scroll(a->view);
    int total = tiku_trk_poseview_count(a->view);
    int i;

    (void)tiku_ui_window(s, win, a->title, 1);
    mc = browser_menu_ctx(a, 0u);
    tiku_trk_cwmenu_rebuild(&a->menus, &mc);
    a->menus.bar.open = a->bar_open;
    bar = browser_menubar_rect(a);
    tiku_menubar_draw(&a->menus.bar, s, bar);
    browser_draw_folder_icon(a, bar);
    draw_navigator(a);
    list = browser_list_rect(a);
    /* The view cannot see its own window.  Without this a page is one row
     * and the scroll is never clamped -- both stated in terms of what is
     * VISIBLE, which only the window knows. */
    tiku_trk_poseview_set_visible_rows(a->view, rows);

    /* The title strip.  Every title is placed from its column's OFFSET, so
     * a title and the cells beneath it cannot drift apart. */
    title = (tiku_rect_t){ list.x,
                                bar.y + bar.h + browser_navigator_height(a),
                                list.w, TITLE_H };
    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        tiku_fill(s, title, TIKU_C_PANEL);
        tiku_clip_set(s, title);
        {
        int dragged, drag_dx = 0;

        dragged = tiku_trk_titleview_dragged(&a->tv, &drag_dx);
        for (i = 0; i < ncol; i++) {
            if (cols[i].field != dragged) {
                browser_draw_title(a, title, &cols[i], 0, 0);
            }
        }
        /* The dragged title goes on top.  Drawn in place it would be
         * partly erased by whichever neighbour it is sliding over, since
         * the next title's rect begins exactly where this one ends. */
        for (i = 0; i < ncol; i++) {
            if (cols[i].field == dragged) {
                browser_draw_title(a, title, &cols[i], 1, drag_dx);
            }
        }
        }
        tiku_clip_reset(s);
    }

    if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        browser_draw_icons(a, list);
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
            tiku_rect_t name = { browser_col_x(a, &cols[0]), row.y,
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
            int text_x = browser_col_x(a, &cols[j]);
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
                                    browser_measure_text, (void *)f, fitted,
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

        (void)tiku_scroll_set_range(&a->sb, total, rows);
        if (!a->sb.tracking) {
            (void)tiku_scroll_to(&a->sb, scroll);
        }
        if (tiku_scroll_thumb(&a->sb, list.h, &tpos, &tlen)) {
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

                tiku_field_metrics(&a->ed, 60, cols[0].width,
                                   browser_measure_prefix_px, NULL, &bw,
                                   &bs);
                box.x = browser_col_x(a, &cols[0]);
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

void
browser_blit(app_t *a)
{
    if (a->host == NULL) {
        return;                     /* displayless: the surface IS the
                                     * output, and -shot writes it */
    }
    (void)tiku_host_present(a->host, a->surf);
}
