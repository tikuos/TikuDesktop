/* tiku_editor_view.c -- the Editor's window around the code view: where
 * the panes, toolbar and footer go, the Find and Go to Line window, and
 * painting.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"
#include "tiku_host.h"
#include <strings.h>

/* The outline live search draws round each match. */
#define MATCH_OUTLINE TIKU_RGB(175, 125, 20)

static int out_pane_h(const edit_state_t *st);
static void toolbar_paint(edit_state_t *st);
static void draw_scroll(edit_state_t *st, tiku_scroll_t *bar, tiku_rect_t r, int horiz);

/** The run pane's height: 0 while there is no run and no output, else
 *  room for seven lines. */
static int
out_pane_h(const edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_plain();

    if (st == NULL || (st->out_count == 0 && !st->running)) {
        return 0;
    }
    return 7 * (f->height + 2) + 8;
}

/** The footer box a message or a tool's label is drawn in, over the
 *  encoding label and the horizontal scrollbar. */
tiku_rect_t tiku_editor_note_box(const edit_state_t *st)
{ return (tiku_rect_t){112, st->h - EDIT_FOOT_H + 1, st->w - 129, EDIT_FOOT_H - 1}; }

/** Where the code view sits: from the window's left edge to the vertical
 *  scrollbar, from under the toolbar to the run pane or the footer. */
tiku_rect_t tiku_editor_view_box(const edit_state_t *st)
{
    return (tiku_rect_t){0, EDIT_TOOL_H + 1, st->w - 15,
        st->h - EDIT_TOOL_H - EDIT_FOOT_H - 1 - out_pane_h(st)};
}

/** The upper pane's height while split, so each pane keeps its least. */
static int upper_h(const edit_state_t *st)
{
    int most = tiku_editor_view_box(st).h - EDIT_SPLIT_GAP - EDIT_PANE_MIN;
    int h = st->split_at < most ? st->split_at : most;
    return h > EDIT_PANE_MIN ? h : EDIT_PANE_MIN;
}

/** Where pane @p pane sits: the whole view box, or while split the part
 *  above the divider or below it. */
tiku_rect_t tiku_editor_pane_box(const edit_state_t *st, int pane)
{
    tiku_rect_t box = tiku_editor_view_box(st);
    int top = upper_h(st);
    if (!st->panes[1]) return box;
    if (pane == 0) { box.h = top; return box; }
    box.y += top + EDIT_SPLIT_GAP; box.h -= top + EDIT_SPLIT_GAP;
    if (box.h < 0) box.h = 0;
    return box;
}

/** The divider across the window between split panes; empty unsplit. */
tiku_rect_t tiku_editor_divider(const edit_state_t *st)
{
    tiku_rect_t box = tiku_editor_view_box(st);
    if (!st->panes[1]) return (tiku_rect_t){0, 0, 0, 0};
    return (tiku_rect_t){box.x, box.y + upper_h(st), st->w, EDIT_SPLIT_GAP};
}

/** Give pane @p pane the keys: its caret is drawn and the other's not. */
void tiku_editor_focus_pane(edit_state_t *st, int pane)
{
    if (pane < 0 || pane > 1 || !st->panes[pane]) return;
    st->pane = pane; st->view = st->panes[pane];
    tiku_editor_view_sync(st);
}

/** The text's page inside the view: its first row's top and line x 0,
 *  past the line numbers when they are shown. */
tiku_rect_t tiku_editor_page(const edit_state_t *st)
{
    tiku_rect_t box = tiku_editor_view_box(st);
    int64_t x, y;
    if (tiku_codeview_point(st->view, 0, &x, &y) != TIKU_EDIT_OK) x = 5;
    x += tiku_codeview_left(st->view);
    return (tiku_rect_t){(int)x, box.y + 3, st->w - (int)x - 16, box.h - 3};
}

/** Hand each pane its box, the View menu's options, the document's
 *  language, colours and line break, and whether it has the keys.  JSON
 *  is coloured and folded when chosen, or under Automatic for a name
 *  ending ".json" in any case. */
void tiku_editor_view_sync(edit_state_t *st)
{
    const char *ext = strrchr(st->path, '.');
    int json = st->language_mode == EDIT_LANG_JSON ||
               (st->language_mode == EDIT_LANG_AUTO &&
                st->lang == TIKU_SYNTAX_NONE && ext != NULL &&
                !strcasecmp(ext, ".json"));
    const tiku_codeview_lexer_t *lexer = json ? tiku_codeview_json_lexer()
                                              : NULL;
    unsigned options = TIKU_CODEVIEW_BRACES | TIKU_CODEVIEW_FOLDS |
        (st->numbers ? TIKU_CODEVIEW_LINE_NUMBERS : 0) |
        (st->show_invisibles ? TIKU_CODEVIEW_INVISIBLES : 0) |
        (st->wrap ? TIKU_CODEVIEW_WRAP : 0) |
        (st->caret_line ? TIKU_CODEVIEW_CARET_LINE : 0) |
        (st->block_caret ? TIKU_CODEVIEW_BLOCK_CARET : 0) |
        (st->indent_guides ? TIKU_CODEVIEW_INDENT_GUIDES : 0);
    for (int i = 0; i < 2 && st->panes[i]; i++) {
        tiku_codeview_t *pane = st->panes[i];

        tiku_codeview_set_bounds(pane, tiku_editor_pane_box(st, i));
        tiku_codeview_set_options(pane, options);
        tiku_codeview_set_language(pane, st->lang);
        (void)tiku_codeview_set_lexer(pane, lexer);
        if (lexer)
            (void)tiku_codeview_json_folds(pane);
        (void)tiku_codeview_set_ruler(pane, st->ruler ?
                                      (unsigned)st->preferences.column : 0u);
        (void)tiku_codeview_set_mark(pane, EDIT_SEARCH_KIND,
                                     TIKU_CODEVIEW_MARK_BOX, MATCH_OUTLINE);
        tiku_editor_preferences_colours(pane, st->preferences.colors);
        (void)tiku_codeview_set_newline(pane, st->newline);
        tiku_codeview_set_focus(pane, i == st->pane);
    }
}

/** Scroll as little as shows the caret, in the box the window has now. */
void tiku_editor_reveal(edit_state_t *st)
{
    tiku_editor_view_sync(st);
    tiku_codeview_reveal(st->view);
}

/** The rectangle of toolbar tool @p i: New, Open, Save, Undo, Redo and
 *  Find, left to right. */
tiku_rect_t tiku_editor_tool_rect(int i)
{ return (tiku_rect_t){4 + 27 * i + (i >= 3 ? 7 : 0), 2, 25, 25}; }
/* Each tool's label, shown in the footer while it is hovered. */
static const char *tool_labels[] = {"New (Ctrl+N)", "Open (Ctrl+O)", "Save (Ctrl+S)",
    "Undo (Ctrl+Z)", "Redo (Ctrl+Y)", "Find / Replace (Ctrl+F)"};
/** Whether tool @p i can be pressed: none while the question is open,
 *  Open with a file panel, Save with unsaved changes, and Undo and Redo
 *  as the view's own commands allow. */
int tiku_editor_tool_enabled(edit_state_t *st, int i)
{
    if (st->ask.open) return 0;
    if (i == 1) return st->services->pick != NULL;
    if (i == 2) return tiku_editor_modified(st);
    if (i == 3)
        return tiku_codeview_command_enabled(st->view, TIKU_CODEVIEW_CMD_UNDO);
    if (i == 4)
        return tiku_codeview_command_enabled(st->view, TIKU_CODEVIEW_CMD_REDO);
    return 1;
}
/** Paint the toolbar: each tool flat until hovered (a file, a folder, a
 *  disk, two arrows and a magnifier), then the document's name, with a
 *  "* " in front while it holds unsaved changes. */
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
    snprintf(label, sizeof label, "%s%s", tiku_editor_modified(st) ? "* " : "",
             *name ? name : "Untitled");
    tiku_clip_set(s, (tiku_rect_t){187, 2, st->w - 195, 24});
    tiku_text(s, tiku_font_plain(), 191, 8 + tiku_font_plain()->ascent, label, TIKU_C_TEXT);
    tiku_clip_reset(s);
    tiku_hline(s, 0, EDIT_TOOL_H - 1, st->w, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

/** The rectangle of the Find window's field @p i: 0 Find, 1 Replace, 2
 *  the Go to Line field. */
tiku_rect_t tiku_editor_search_field(const edit_state_t *st, int i)
{ (void)st; return (tiku_rect_t){84, 12 + (i == 1 ? 32 : 0), SEARCH_W - 100, 25}; }
/** The rectangle of the Find window's button @p i, left to right. */
tiku_rect_t tiku_editor_search_button(int i)
{ return (tiku_rect_t){12 + 92 * i, 126, 86, 25}; }
/** Close the Find or Go to Line window and drop the match outlines. */
void tiku_editor_search_close(edit_state_t *st)
{
    tiku_editor_search_clear(st);
    uint32_t id = st->search_id;
    st->search_id = 0; st->finding = 0; st->find_focus = -1; st->search_active = 0;
    if (id && st->services->close) st->services->close(st->services->ctx, id);
}
/** Open the Find and Replace window (@p mode 1) or the Go to Line window
 *  (2), or raise it; one open in the other mode is closed first. */
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
/** Paint the Find window, its count line under the buttons, or the Go
 *  to Line window. */
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
        tiku_ui_checkbox(s, (tiku_rect_t){84, 103, 180, 20},
                         "Regular expression", st->regex ? TIKU_S_ON : 0);
        if (st->regex)
            tiku_text(s, tiku_font_at(10), 276, 118,
                      "Replace groups: $1, $2", TIKU_C_TEXT);
        const char *labels[] = {"Previous", "Find next", "Replace", "Replace all", "Done"};
        for (int i = 0; i < 5; i++) tiku_ui_button(s, tiku_editor_search_button(i), labels[i], 0);
    } else {
        tiku_text(s, f, 84, 68, "Enter a line number and press Return.", TIKU_C_TEXT);
        tiku_ui_button(s, tiku_editor_search_button(3), "Go", TIKU_S_DEFAULT);
        tiku_ui_button(s, tiku_editor_search_button(4), "Cancel", 0);
    }
    if (st->finding == 1)
        tiku_text(s, tiku_font_at(10), 12, 177, st->search_message,
                  TIKU_C_TEXT);
    st->services->frame(st->services->ctx, st->search_id, s->px, SEARCH_W, SEARCH_H);
}

/** Draw scrollbar @p bar in @p r, across with @p horiz: its end arrows,
 *  the track and the thumb. */
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

/** The panes, each with its vertical scrollbar beside it, and while split
 *  the divider between them; the horizontal scrollbar follows the pane
 *  with the keys, an arrow stepping one character of that pane's face as
 *  zoomed. */
static void paint_panes(edit_state_t *st)
{
    int64_t total, shown, at;
    int cell;
    for (int i = 0; i < 2 && st->panes[i]; i++) {
        tiku_rect_t box = tiku_editor_pane_box(st, i);
        tiku_scroll_t *bar = i ? &st->lower : &st->vertical;
        tiku_codeview_paint(st->panes[i], st->surface);
        tiku_codeview_extent(st->panes[i], 0, &total, &shown, &at);
        tiku_scroll_set_range(bar, total > INT_MAX ? INT_MAX : (int)total, (int)shown);
        tiku_scroll_to(bar, at > INT_MAX ? INT_MAX : (int)at);
        draw_scroll(st, bar, (tiku_rect_t){st->w - 15, box.y, 15, box.h}, 0);
    }
    if (st->panes[1]) {
        tiku_rect_t d = tiku_editor_divider(st);
        tiku_rgb_t rule = tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2);
        tiku_fill(st->surface, d, TIKU_C_PANEL);
        tiku_hline(st->surface, d.x, d.y, d.w, rule);
        tiku_hline(st->surface, d.x, d.y + d.h - 1, d.w, rule);
    }
    cell = tiku_text_advance(tiku_codeview_font(st->view), ' ');
    if (cell > 0)
        st->horizontal.step = cell;
    tiku_codeview_extent(st->view, 1, &total, &shown, &at);
    tiku_scroll_set_range(&st->horizontal, total > INT_MAX ? INT_MAX : (int)total, (int)shown);
    tiku_scroll_to(&st->horizontal, at > INT_MAX ? INT_MAX : (int)at);
}

/** Paint the document window and the Find window.  The panes' changes
 *  up to this frame are taken in here, so the tick repaints only for
 *  later ones; document and command changes among them leave the menus
 *  due for publishing. */
void tiku_editor_paint(edit_state_t *st)
{
    tiku_editor_search_refresh(st);
    const tiku_font_t *f = tiku_font_mono(0), *small = tiku_font_at(11);
    int step = f->height + 2;
    tiku_edit_pos_t anchor, caret;
    tiku_editor_view_sync(st);
    if (st->dl) tiku_dl_clear(st->dl);
    st->surface->record = st->dl;
    tiku_fill(st->surface, (tiku_rect_t){0, 0, st->w, st->h}, TIKU_C_PANEL);
    paint_panes(st);
    for (int i = 0; i < 2 && st->panes[i]; i++) {
        st->menus_due |= tiku_codeview_changes(st->panes[i]) &
                         (TIKU_CODEVIEW_CHANGE_DOCUMENT |
                          TIKU_CODEVIEW_CHANGE_COMMANDS);
    }
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
    /* The footer: the caret's line and column, the line-break label and
     * the horizontal scrollbar. */
    int footer = st->h - EDIT_FOOT_H;
    tiku_hline(st->surface, 0, footer, st->w, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    char position[96];
    (void)tiku_codeview_selection(st->view, &anchor, &caret);
    int64_t line = tiku_editdoc_line_of(st->doc, caret);
    snprintf(position, sizeof position, "%lld : %lld", (long long)line + 1,
             (long long)(caret - tiku_editdoc_line_start(st->doc, line)) + 1);
    tiku_text(st->surface, small, 6, footer + 14, position, TIKU_C_TEXT);
    const char *mode = !strcmp(st->newline, "\r\n") ? "UTF-8  CRLF" :
                       !strcmp(st->newline, "\r") ? "UTF-8  CR" : "UTF-8  LF";
    tiku_text(st->surface, small, 118, footer + 14, mode, TIKU_C_TEXT);
    draw_scroll(st, &st->horizontal, (tiku_rect_t){208, footer + 2, st->w - 224, 16}, 1);
    if (st->note[0] || st->hover_tool >= 0) {
        const char *message = st->hover_tool >= 0 ? tool_labels[st->hover_tool] : st->note;
        tiku_rect_t box = tiku_editor_note_box(st);
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
    if (st->context_open)
        tiku_menu_draw(&st->context, st->surface, st->context_frame);
    st->surface->record = NULL;
    if (st->services->present) st->services->present(st->services->ctx, st->id, st->dl, st->surface->px, st->w, st->h);
    else st->services->frame(st->services->ctx, st->id, st->surface->px, st->w, st->h);
    tiku_editor_search_paint(st);
    /* The pointer is a hand while it is over a jump target a click would
     * follow. */
    int shape = tiku_codeview_hotspot(st->view, NULL) ? TIKU_CURSOR_HAND
                                                      : TIKU_CURSOR_ARROW;
    if (shape != st->cursor &&
        tiku_app_cursor_set(st->services, st->id, shape))
        st->cursor = shape;
}
