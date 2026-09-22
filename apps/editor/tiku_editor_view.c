/* tiku_editor_view.c -- view.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static int
out_pane_h(const edit_state_t *st);
static void toolbar_paint(edit_state_t *st);
static void draw_scroll(edit_state_t *st, tiku_scroll_t *bar, tiku_rect_t r, int horiz);
static void paint_line(edit_state_t *st, int row, int py, tiku_span_t *spans, int ns);

static int
out_pane_h(const edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_plain();

    if (st == NULL || (st->out_count == 0 && !st->running)) {
        return 0;               /* no run yet: the page is all editor */
    }
    return 7 * (f->height + 2) + 8;
}

/* Pe-inspired compact native chrome and a viewport-only document painter.
 * SPDX-License-Identifier: Apache-2.0 */

/** The document never moves when Find is opened in its own window. */
tiku_rect_t tiku_editor_page(const edit_state_t *st)
{
    int digits = 1;
    for (int n = st->tv.nline; n >= 10; n /= 10) digits++;
    int gutter = st->numbers ? 16 + digits * tiku_font_mono_cell(0) : 5;
    return (tiku_rect_t){gutter, EDIT_TOOL_H + 4, st->w - gutter - 16,
        st->h - EDIT_TOOL_H - EDIT_FOOT_H - 4 - out_pane_h(st)};
}
int tiku_editor_rows_visible_of(const edit_state_t *st)
{
    int n = tiku_editor_page(st).h / (tiku_font_mono(0)->height + 2);
    return n > 0 ? n : 1;
}

/** Iterate one visible glyph, preserving byte offsets and four-cell tab stops. */
int tiku_editor_glyph_at(const char *text, int at, int x, char glyph[5], int *advance)
{
    int end = at + 1;
    if (text[at] == '\t') {
        glyph[0] = 0;
        int tab = 4 * tiku_font_mono_cell(0);
        *advance = tab - x % tab;
    } else {
        while (text[end] && ((unsigned char)text[end] & 0xc0) == 0x80) end++;
        memcpy(glyph, text + at, (size_t)(end - at)); glyph[end - at] = 0;
        *advance = tiku_text_width(tiku_font_mono(0), glyph);
    }
    return end;
}
int tiku_editor_prefix_width(const char *text, int n)
{
    int x = 0;
    for (int i = 0; i < n && text[i];) {
        char glyph[5]; int advance;
        i = tiku_editor_glyph_at(text, i, x, glyph, &advance);
        if (x > INT_MAX / 2 - advance) return INT_MAX / 2;
        x += advance;
    }
    return x;
}
void tiku_editor_reveal(edit_state_t *st)
{
    tiku_rect_t page = tiku_editor_page(st);
    tiku_editor_buffer_reveal(&st->tv, tiku_editor_rows_visible_of(st));
    int x = tiku_editor_prefix_width(tiku_editor_buffer_line(&st->tv, st->tv.cy), st->tv.cx);
    if (x < st->left) st->left = x;
    if (x >= st->left + page.w - 4) st->left = x - page.w + 8;
    if (st->left < 0) st->left = 0;
}

/** Flat until hovered, with familiar file/folder/save/search symbols. */
tiku_rect_t tiku_editor_tool_rect(int i)
{ return (tiku_rect_t){4 + 27 * i + (i >= 3 ? 7 : 0), 2, 25, 25}; }
static const char *tool_labels[] = {"New (Ctrl+N)", "Open (Ctrl+O)", "Save (Ctrl+S)",
    "Undo (Ctrl+Z)", "Redo (Ctrl+Y)", "Find / Replace (Ctrl+F)"};
int tiku_editor_tool_enabled(edit_state_t *st, int i)
{
    if (st->ask.open) return 0;
    if (i == 1) return st->services->pick != NULL;
    if (i == 2) return tiku_editor_buffer_modified(&st->tv);
    if (i == 3) return tiku_editor_buffer_can_undo(&st->tv);
    if (i == 4) return tiku_editor_buffer_can_redo(&st->tv);
    return 1;
}
static void toolbar_paint(edit_state_t *st)
{
    tiku_surface_t *s = st->surface;
    for (int i = 0; i < 6; i++) {
        tiku_rect_t r = tiku_editor_tool_rect(i);
        int enabled = tiku_editor_tool_enabled(st, i);
        if (enabled && (st->hover_tool == i || st->pressed_tool == i))
            tiku_ui_button(s, r, "", st->pressed_tool == i ? TIKU_S_PRESSED : 0);
        int x = r.x + 4, y = r.y + 4;
        tiku_rgb_t ink = enabled ? TIKU_C_TEXT : tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL);
        if (i == 0 || i == 1 || i == 5)
            tiku_icon_paint(s, i == 0 ? "file" : i == 1 ? "folder" : "magnifier", x, y, 17);
        else if (i == 2) {
            tiku_rect_t disk = {x + 1, y, 15, 17};
            tiku_fill(s, disk, enabled ? TIKU_RGB(85, 110, 151) : TIKU_RGB(157, 161, 168));
            tiku_frame(s, disk, ink);
            tiku_fill(s, (tiku_rect_t){x + 4, y + 1, 8, 5}, TIKU_RGB(228, 230, 232));
            tiku_fill(s, (tiku_rect_t){x + 3, y + 10, 11, 6}, TIKU_C_DOC);
            tiku_hline(s, x + 5, y + 12, 7, ink);
        } else {
            tiku_ui_arrow(s, (tiku_rect_t){x, y + 1, 10, 11}, i == 3 ? 2 : 3, enabled);
            tiku_hline(s, x + 4, y + 6, 10, ink);
            tiku_vline(s, i == 3 ? x + 13 : x + 3, y + 6, 8, ink);
            tiku_hline(s, x + 4, y + 13, 10, ink);
        }
    }
    tiku_vline(s, 87, 6, 17, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    const char *name = strrchr(st->path, '/'); name = name ? name + 1 : st->path;
    char label[550];
    snprintf(label, sizeof label, "%s%s", tiku_editor_buffer_modified(&st->tv) ? "* " : "",
             *name ? name : "Untitled");
    tiku_clip_set(s, (tiku_rect_t){187, 2, st->w - 195, 24});
    tiku_text(s, tiku_font_plain(), 191, 8 + tiku_font_plain()->ascent, label, TIKU_C_TEXT);
    tiku_clip_reset(s);
    tiku_hline(s, 0, EDIT_TOOL_H - 1, st->w, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

tiku_rect_t tiku_editor_search_field(const edit_state_t *st, int i)
{ (void)st; return (tiku_rect_t){84, 12 + (i == 1 ? 32 : 0), SEARCH_W - 100, 25}; }
tiku_rect_t tiku_editor_search_button(int i)
{ return (tiku_rect_t){12 + 92 * i, 126, 86, 25}; }
void tiku_editor_search_close(edit_state_t *st)
{
    uint32_t id = st->search_id;
    st->search_id = 0; st->finding = 0; st->find_focus = -1; st->search_active = 0;
    if (id && st->services->close) st->services->close(st->services->ctx, id);
}
void tiku_editor_search_open_window(edit_state_t *st, int mode)
{
    if (st->search_id && mode != st->finding) tiku_editor_search_close(st);
    st->finding = mode; st->search_active = 1;
    if (!st->search_surface) st->search_surface = tiku_surface_new(SEARCH_W, SEARCH_H, TIKU_C_PANEL);
    if (!st->search_surface) { st->finding = 0; return; }
    if (!st->search_id) st->search_id = st->services->open(st->services->ctx,
        mode == 2 ? "Go to Line" : "Find and Replace", SEARCH_W, SEARCH_H);
    if (!st->search_id) { st->finding = 0; st->search_active = 0; }
    else if (st->services->raise_window) st->services->raise_window(st->services->ctx, st->search_id);
}
void tiku_editor_search_paint(edit_state_t *st)
{
    if (!st->search_id || !st->search_surface) return;
    tiku_surface_t *s = st->search_surface;
    const tiku_font_t *f = tiku_font_plain();
    tiku_fill(s, (tiku_rect_t){0, 0, SEARCH_W, SEARCH_H}, TIKU_C_PANEL);
    int count = st->finding == 1 ? 2 : 1;
    for (int k = 0; k < count; k++) {
        int i = st->finding == 2 ? 2 : k; tiku_rect_t r = tiku_editor_search_field(st, i);
        tiku_text(s, f, 12, r.y + 5 + f->ascent,
                  i == 0 ? "Find:" : i == 1 ? "Replace:" : "Line:", TIKU_C_TEXT);
        tiku_field_t *v = &st->field[i];
        tiku_ui_textfield_scroll(s, r, v->text,
            st->find_focus == i ? v->caret : -1, v->sel_a, v->sel_b,
            st->find_focus == i ? TIKU_S_FOCUS : 0, st->field_input[i].scroll);
    }
    if (st->finding == 1) {
        tiku_ui_checkbox(s, (tiku_rect_t){84, 80, 150, 22}, "Match case", st->match_case ? TIKU_S_ON : 0);
        tiku_ui_checkbox(s, (tiku_rect_t){250, 80, 160, 22}, "Whole words", st->whole_word ? TIKU_S_ON : 0);
        const char *labels[] = {"Previous", "Find next", "Replace", "Replace all", "Done"};
        for (int i = 0; i < 5; i++) tiku_ui_button(s, tiku_editor_search_button(i), labels[i], 0);
    } else {
        tiku_text(s, f, 84, 68, "Enter a line number and press Return.", TIKU_C_TEXT);
        tiku_ui_button(s, tiku_editor_search_button(3), "Go", TIKU_S_DEFAULT);
        tiku_ui_button(s, tiku_editor_search_button(4), "Cancel", 0);
    }
    st->services->frame(st->services->ctx, st->search_id, s->px, SEARCH_W, SEARCH_H);
}

/** Ordinary scrollbar: end arrows, page track and grippable thumb. */
static void draw_scroll(edit_state_t *st, tiku_scroll_t *bar, tiku_rect_t r, int horiz)
{
    int length = (horiz ? r.w : r.h) - 28, pos, thick;
    tiku_ui_sunken(st->surface, r, TIKU_C_PANEL);
    for (int i = 0; i < 2; i++) {
        tiku_rect_t button = horiz ? (tiku_rect_t){r.x + (i ? r.w - 14 : 0), r.y, 14, r.h} :
            (tiku_rect_t){r.x, r.y + (i ? r.h - 14 : 0), r.w, 14};
        tiku_ui_button(st->surface, button, "", 0);
        tiku_ui_arrow(st->surface, button, horiz ? 2 + i : i, tiku_scroll_max(bar) > 0);
    }
    if (length > 0 && tiku_scroll_thumb(bar, length, &pos, &thick)) {
        tiku_rect_t thumb = horiz ? (tiku_rect_t){r.x + 14 + pos, r.y, thick, r.h} :
            (tiku_rect_t){r.x, r.y + 14 + pos, r.w, thick};
        tiku_ui_button(st->surface, thumb, "", 0);
    }
}

/** Paint glyphs only inside the viewport; long lines never enter fixed text arrays. */
static void paint_line(edit_state_t *st, int row, int py, tiku_span_t *spans, int ns)
{
    const char *text = tiku_editor_buffer_line(&st->tv, row);
    const tiku_font_t *f = tiku_font_mono(0);
    tiku_rect_t page = tiku_editor_page(st);
    int sy, sx, ey, ex, selected = tiku_editor_buffer_selection(&st->tv, &sy, &sx, &ey, &ex);
    int x = 0, span = 0, limit = ns ? spans[0].len : 0;
    for (int i = 0; text[i];) {
        char glyph[5]; int advance, next = tiku_editor_glyph_at(text, i, x, glyph, &advance);
        if (x > st->left + page.w) break;
        if (x + advance >= st->left) {
            while (span < ns && i >= limit) { span++; if (span < ns) limit += spans[span].len; }
            int chosen = selected && row >= sy && row <= ey &&
                (row != sy || i >= sx) && (row != ey || i < ex);
            tiku_rgb_t ink = chosen ? TIKU_C_SELTEXT : span < ns ? tiku_ink(spans[span].ink) : TIKU_C_TEXT;
            tiku_rect_t cell = {page.x + x - st->left, py, advance, f->height + 2};
            if (chosen) tiku_fill(st->surface, cell, TIKU_C_SELECT);
            if (*glyph) tiku_text(st->surface, f, cell.x, py + f->ascent, glyph, ink);
            if (st->show_invisibles && (text[i] == ' ' || text[i] == '\t')) {
                tiku_hline(st->surface, cell.x + advance / 2, py + f->height / 2,
                           text[i] == '\t' ? 3 : 1, tiku_dim(ink, TIKU_C_DOC));
            }
        }
        if (x > INT_MAX / 2 - advance) break;
        x += advance; i = next;
    }
    if (selected && row >= sy && row < ey)
        tiku_fill(st->surface, (tiku_rect_t){page.x + x - st->left, py, tiku_font_mono_cell(0), f->height + 2}, TIKU_C_SELECT);
}

void tiku_editor_paint(edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_mono(0), *small = tiku_font_at(11);
    int step = f->height + 2, rows = tiku_editor_rows_visible_of(st);
    tiku_rect_t page = tiku_editor_page(st);
    if (st->dl) tiku_dl_clear(st->dl);
    st->surface->record = st->dl;
    tiku_fill(st->surface, (tiku_rect_t){0, 0, st->w, st->h}, TIKU_C_PANEL);
    tiku_fill(st->surface, (tiku_rect_t){0, page.y - 3, st->w - 15, page.h + 3}, TIKU_C_DOC);
    if (st->numbers) {
        tiku_fill(st->surface, (tiku_rect_t){0, page.y - 3, page.x - 4, page.h + 3}, TIKU_RGB(245, 245, 244));
        tiku_vline(st->surface, page.x - 5, page.y - 3, page.h + 3, TIKU_RGB(224, 224, 221));
    }
    int cell = tiku_font_mono_cell(0), font_changed = st->measured_cell != cell || st->measured_height != f->height;
    if (st->width_dirty || font_changed) {
        st->max_width = 0;
        for (int y = 0; y < st->tv.nline; y++) {
            if (font_changed || st->tv.line[y].width < 0)
                st->tv.line[y].width = tiku_editor_prefix_width(st->tv.line[y].text, st->tv.line[y].len);
            if (st->tv.line[y].width > st->max_width) st->max_width = st->tv.line[y].width;
        }
        st->width_dirty = 0; st->measured_cell = cell; st->measured_height = f->height;
    }
    tiku_scroll_set_range(&st->vertical, st->tv.nline, rows);
    tiku_editor_buffer_scroll_to(&st->tv, st->tv.top, rows);
    tiku_scroll_to(&st->vertical, st->tv.top);
    tiku_scroll_set_range(&st->horizontal, st->max_width + 12, page.w);
    st->left = tiku_scroll_to(&st->horizontal, st->left);
    tiku_syntax_state_t carry = TIKU_SYNTAX_OPEN;
    for (int y = st->lang == TIKU_SYNTAX_C ? 0 : st->tv.top;
         y < st->tv.top + rows && y < st->tv.nline; y++) {
        tiku_span_t spans[128]; int ns = 0;
        if (st->lang != TIKU_SYNTAX_NONE)
            ns = tiku_syntax_spans_on(st->lang, st->tv.line[y].text, carry, &carry, spans, 128);
        if (y < st->tv.top) continue;
        int py = page.y + (y - st->tv.top) * step;
        if (st->numbers) {
            char number[16]; snprintf(number, sizeof number, "%d", y + 1);
            tiku_text(st->surface, small, page.x - 10 - tiku_text_width(small, number),
                py + f->ascent, number, tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
        }
        tiku_clip_set(st->surface, page); paint_line(st, y, py, spans, ns); tiku_clip_reset(st->surface);
    }
    if (st->tv.cy >= st->tv.top && st->tv.cy < st->tv.top + rows) {
        tiku_clip_set(st->surface, page);
        tiku_vline(st->surface, page.x - st->left +
            tiku_editor_prefix_width(tiku_editor_buffer_line(&st->tv, st->tv.cy), st->tv.cx),
            page.y + (st->tv.cy - st->tv.top) * step, step, TIKU_C_TEXT);
        tiku_clip_reset(st->surface);
    }
    draw_scroll(st, &st->vertical, (tiku_rect_t){st->w - 15, page.y - 3, 15, page.h + 3}, 0);
    int pane_h = out_pane_h(st);
    if (pane_h) {
        tiku_rect_t pane = {0, st->h - EDIT_FOOT_H - pane_h, st->w - 15, pane_h};
        tiku_ui_sunken(st->surface, pane, TIKU_C_DOC);
        tiku_clip_set(st->surface, tiku_inset(pane, 3));
        int first = st->out_count > 7 ? st->out_count - 7 : 0;
        for (int i = first; i < st->out_count; i++)
            tiku_text(st->surface, f, 6, pane.y + 4 + (i - first) * step + f->ascent,
                      st->out[i], tiku_ink(TIKU_INK_REMARK));
        tiku_clip_reset(st->surface);
    }
    toolbar_paint(st);
    /* Caret pane shares the bottom edge with horizontal scrolling, as in Pe. */
    int footer = st->h - EDIT_FOOT_H;
    tiku_hline(st->surface, 0, footer, st->w, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    char position[96];
    snprintf(position, sizeof position, "%d : %d", st->tv.cy + 1, st->tv.cx + 1);
    tiku_text(st->surface, small, 6, footer + 14, position, TIKU_C_TEXT);
    const char *mode = st->crlf ? "UTF-8  CRLF" : "UTF-8  LF";
    tiku_text(st->surface, small, 118, footer + 14, mode, TIKU_C_TEXT);
    draw_scroll(st, &st->horizontal, (tiku_rect_t){208, footer + 2, st->w - 224, 16}, 1);
    if (st->note[0] || st->hover_tool >= 0) {
        const char *message = st->hover_tool >= 0 ? tool_labels[st->hover_tool] : st->note;
        tiku_rect_t box = {112, footer + 1, st->w - 129, EDIT_FOOT_H - 1};
        tiku_fill(st->surface, box, TIKU_C_PANEL);
        tiku_clip_set(st->surface, tiku_inset(box, 3));
        tiku_text(st->surface, small, box.x + 5, box.y + 13, message, TIKU_C_TEXT);
        tiku_clip_reset(st->surface);
    }
    if (st->ask.open) {
        int w, h; tiku_alert_size(&w, &h);
        st->ask_frame = (tiku_rect_t){(st->w - w) / 2, (st->h - h) / 3, w, h};
        tiku_alert_draw(&st->ask, st->surface, st->ask_frame);
    }
    st->surface->record = NULL;
    if (st->services->present) st->services->present(st->services->ctx, st->id, st->dl, st->surface->px, st->w, st->h);
    else st->services->frame(st->services->ctx, st->id, st->surface->px, st->w, st->h);
    tiku_editor_search_paint(st);
}
