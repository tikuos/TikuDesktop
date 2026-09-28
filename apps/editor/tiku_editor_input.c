/* tiku_editor_input.c -- input.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static int inside(tiku_rect_t r, int x, int y);
static int scrollbar_press(tiku_scroll_t *bar, int length, int at);
static void pointer_caret(edit_state_t *st, const tiku_event_t *e);
static int answer_question(edit_state_t *st, const tiku_event_t *e);
static int search_event(edit_state_t *st, const tiku_event_t *e);

static const int tool_commands[] = {CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_UNDO, CMD_REDO, CMD_FIND};

/* Pointer, keyboard and close routes for the editor.
 * SPDX-License-Identifier: Apache-2.0 */

/** Half-open rectangles are shared by rendering and hit testing. */
static int inside(tiku_rect_t r, int x, int y)
{ return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/** End arrows and track clicks use the painter's exact geometry. */
static int scrollbar_press(tiku_scroll_t *bar, int length, int at)
{
    if (at < 14) return tiku_scroll_to(bar, bar->value - bar->step);
    if (at >= length - 14) return tiku_scroll_to(bar, bar->value + bar->step);
    return tiku_scroll_press(bar, length - 28, at - 14);
}

/** Place or extend a selection using actual UTF-8 prefix metrics. */
static void pointer_caret(edit_state_t *st, const tiku_event_t *e)
{
    tiku_rect_t page = tiku_editor_page(st);
    int row = (e->y - page.y) / (tiku_font_mono(0)->height + 2);
    if (e->y < page.y) row = -1;
    int y = st->tv.top + row;
    if (y < 0) y = 0;
    if (y >= st->tv.nline) y = st->tv.nline - 1;
    const char *text = tiku_editor_buffer_line(&st->tv, y);
    int x = e->x - page.x + st->left, col = 0, measured = 0;
    for (int i = 0; i < st->tv.line[y].len;) {
        char glyph[5]; int width;
        int next = tiku_editor_glyph_at(text, i, measured, glyph, &width);
        if (x < measured + width / 2) break;
        measured += width;
        col = next; i = next;
    }
    tiku_editor_buffer_place(&st->tv, y, col);
    tiku_editor_reveal(st);
}

/** Save/discard/cancel all continue the exact action originally requested. */
static int answer_question(edit_state_t *st, const tiku_event_t *e)
{
    int at = -1;
    if (e->type == TIKU_EVENT_KEY_DOWN) at = tiku_alert_key(&st->ask, e->key);
    if (e->type == TIKU_EVENT_POINTER_DOWN)
        at = tiku_alert_button_at(&st->ask, st->ask_frame.w, st->ask_frame.h,
                                  e->x - st->ask_frame.x, e->y - st->ask_frame.y);
    if (at < 0) return 0;
    int pending = st->pending;
    tiku_alert_reset(&st->ask); st->pending = 0;
    if (at == 1) {
        st->allow_open = pending == CMD_OPEN;
        if (tiku_editor_document_action(st, pending)) return 1;
    }
    if (at == 2) {
        if (!st->path[0]) { st->pending = pending; tiku_editor_file_panel(st, 1); }
        else if (tiku_editor_save(st) && tiku_editor_document_action(st, pending)) return 1;
    }
    tiku_editor_publish(st); tiku_editor_paint(st); return 0;
}

/** Search accepts ordinary native field input and explicit navigation keys. */
static int search_event(edit_state_t *st, const tiku_event_t *e)
{
    if (!st->finding || !st->search_active) return 0;
    if (e->type == TIKU_EVENT_POINTER_DOWN) {
        if (st->finding == 2 && inside(tiku_editor_search_button(4), e->x, e->y)) {
            tiku_editor_search_close(st); tiku_editor_publish(st); tiku_editor_paint(st); return 1;
        }
        if (st->finding == 2 && inside(tiku_editor_search_button(3), e->x, e->y)) {
            tiku_event_t enter = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
            return search_event(st, &enter);
        }
        for (int i = 0; i < 3; i++) {
            if ((st->finding == 2) != (i == 2)) continue;
            if (inside(tiku_editor_search_field(st, i), e->x, e->y)) st->find_focus = i;
        }
        if (st->finding == 1 && inside((tiku_rect_t){84, 80, 150, 22}, e->x, e->y)) {
            st->match_case = !st->match_case; tiku_editor_paint(st); return 1;
        }
        if (st->finding == 1 && inside((tiku_rect_t){250, 80, 160, 22}, e->x, e->y)) {
            st->whole_word = !st->whole_word; tiku_editor_paint(st); return 1;
        }
        if (st->finding == 1) for (int i = 0; i < 5; i++) {
            if (!inside(tiku_editor_search_button(i), e->x, e->y)) continue;
            if (i == 0 || i == 1) tiku_editor_find_match(st, i == 0);
            if (i == 2) tiku_editor_replace_match(st);
            if (i == 3) tiku_editor_replace_all(st);
            if (i == 4) tiku_editor_search_close(st);
            tiku_editor_publish(st); tiku_editor_paint(st); return 1;
        }
    }
    if (st->find_focus < 0) return 0;
    if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_ESCAPE) {
        tiku_editor_search_close(st); tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 1;
    }
    if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_TAB) {
        if (st->finding == 1) st->find_focus = !st->find_focus;
        tiku_editor_paint(st); return 1;
    }
    if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_RETURN) {
        if (st->finding == 2) {
            char *end; errno = 0;
            long line = strtol(st->field[2].text, &end, 10);
            if (errno || end == st->field[2].text || *end || line < 1 || line > st->tv.nline)
                snprintf(st->note, sizeof st->note, "Enter a line from 1 to %d.", st->tv.nline);
            else {
                tiku_editor_buffer_select_none(&st->tv);
                tiku_editor_buffer_place(&st->tv, (int)line - 1, 0);
                tiku_editor_search_close(st); tiku_editor_reveal(st);
            }
        } else if (st->find_focus == 1) tiku_editor_replace_match(st);
        else tiku_editor_find_match(st, (e->modifiers & TIKU_MOD_SHIFT) != 0);
        tiku_editor_publish(st); tiku_editor_paint(st); return 1;
    }
    /* App services carry the system clipboard; field input uses its kit mirror. */
    unsigned stamp = tiku_clipboard_stamp();
    if (e->type == TIKU_EVENT_KEY_DOWN && (e->modifiers & TIKU_MOD_CMD) &&
        (e->key == 'v' || e->key == 'V') && st->services->clip_text) {
        tiku_clipboard_set(st->services->clip_text(st->services->ctx, NULL));
        stamp = tiku_clipboard_stamp();
    }
    int i = st->find_focus;
    tiku_field_result_t result = tiku_field_event(&st->field[i], &st->field_input[i],
                                                 e, tiku_editor_search_field(st, i), 1);
    if (tiku_clipboard_stamp() != stamp && st->services->clip_set)
        st->services->clip_set(st->services->ctx, tiku_clipboard_text());
    if (result == TIKU_FIELD_REJECTED)
        snprintf(st->note, sizeof st->note, "Text does not fit this field.");
    if (result != TIKU_FIELD_IGNORED) { tiku_editor_publish(st); tiku_editor_paint(st); }
    /* Auxiliary-window coordinates must never reach document hit testing. */
    return 1;
}

/** Input respects modal questions, fields, scrollbars, then document editing. */
int tiku_editor_event(void *state, const tiku_event_t *e)
{
    edit_state_t *st = state;
    if (e->type == TIKU_EVENT_RESIZE) {
        int w = e->width < 480 ? 480 : e->width;
        int h = e->height < 260 ? 260 : e->height;
        if (w > 8192 || h > 8192) return 0;
        tiku_surface_t *surface = tiku_surface_new(w, h, TIKU_C_PANEL);
        if (surface) {
            tiku_surface_free(st->surface); st->surface = surface;
            st->w = w; st->h = h; tiku_editor_reveal(st); tiku_editor_paint(st);
        }
        return 0;
    }
    if (st->ask.open) return answer_question(st, e);
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button > 1) return 0;
    /* Document commands remain available while a search field has focus. */
    if (st->finding && st->search_active && st->find_focus >= 0 && e->type == TIKU_EVENT_KEY_DOWN &&
        (e->modifiers & TIKU_MOD_CMD)) {
        unsigned k = e->key >= 'A' && e->key <= 'Z' ? e->key + 32 : e->key;
        int cmd = k == 's' ? (e->modifiers & TIKU_MOD_SHIFT ? CMD_SAVEAS : CMD_SAVE) :
            k == 'o' ? CMD_OPEN : k == 'n' ? CMD_NEW :
            k == 'w' || k == 'q' ? CMD_QUIT : k == 'f' ? CMD_FIND :
            k == 'h' ? CMD_REPLACE : k == 'g' ? CMD_GOTO :
            k == ']' ? CMD_NEXT : k == '[' ? CMD_PREV : 0;
        if (cmd) return tiku_editor_pick(st, st->search_id, cmd);
    }
    if (e->type == TIKU_EVENT_CLOSE) return tiku_editor_pick(st, st->id, CMD_QUIT);
    if (e->type == TIKU_EVENT_EXPOSE) { tiku_editor_paint(st); return 0; }
    if (e->type == TIKU_EVENT_DEACTIVATED) {
        st->dragging = 0;
        for (int i = 0; i < 3; i++) st->field_input[i].dragging = 0;
        st->hover_tool = st->pressed_tool = -1;
        tiku_scroll_release(&st->vertical); tiku_scroll_release(&st->horizontal);
        tiku_text_input_reset(&st->commit);
        return 0;
    }
    if (search_event(st, e)) return 0;
    tiku_rect_t page = tiku_editor_page(st);
    if (e->type == TIKU_EVENT_WHEEL) {
        if (e->modifiers & TIKU_MOD_SHIFT) st->left += e->dy * tiku_font_mono_cell(0);
        else tiku_editor_buffer_scroll_to(&st->tv, st->tv.top + e->dy, tiku_editor_rows_visible_of(st));
        st->left += e->dx * tiku_font_mono_cell(0);
        tiku_editor_paint(st); return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN) {
        tiku_text_input_reset(&st->commit);
        for (int i = 0; i < 6; i++) if (inside(tiku_editor_tool_rect(i), e->x, e->y)) {
            if (tiku_editor_tool_enabled(st, i)) st->pressed_tool = i;
            tiku_editor_paint(st); return 0;
        }
        if (inside((tiku_rect_t){st->w - 15, page.y - 3, 15, page.h + 3}, e->x, e->y)) {
            st->tv.top = scrollbar_press(&st->vertical, page.h + 3, e->y - page.y + 3);
        } else if (inside((tiku_rect_t){208, st->h - EDIT_FOOT_H + 2, st->w - 224, 16}, e->x, e->y)) {
            st->left = scrollbar_press(&st->horizontal, st->w - 224, e->x - 208);
        } else if (inside(page, e->x, e->y)) {
            if (e->modifiers & TIKU_MOD_SHIFT) tiku_editor_buffer_select_begin(&st->tv);
            else tiku_editor_buffer_select_none(&st->tv);
            pointer_caret(st, e); tiku_editor_buffer_select_begin(&st->tv); st->dragging = 1;
        }
        tiku_editor_publish(st); tiku_editor_paint(st); return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE) {
        if (st->vertical.tracking)
            st->tv.top = tiku_scroll_drag(&st->vertical, page.h - 25, e->y - page.y - 11);
        else if (st->horizontal.tracking)
            st->left = tiku_scroll_drag(&st->horizontal, st->w - 252, e->x - 222);
        else if (st->dragging) pointer_caret(st, e);
        else {
            int hover = -1;
            for (int i = 0; i < 6; i++) if (inside(tiku_editor_tool_rect(i), e->x, e->y)) hover = i;
            if (hover == st->hover_tool) return 0;
            st->hover_tool = hover;
        }
        tiku_editor_publish(st); tiku_editor_paint(st); return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_UP) {
        int pressed = st->pressed_tool; st->pressed_tool = -1;
        if (pressed >= 0 && inside(tiku_editor_tool_rect(pressed), e->x, e->y) && tiku_editor_tool_enabled(st, pressed))
            return tiku_editor_pick(st, st->id, tool_commands[pressed]);
        st->dragging = 0;
        tiku_scroll_release(&st->vertical); tiku_scroll_release(&st->horizontal);
        if (pressed >= 0) tiku_editor_paint(st);
        return 0;
    }
    int commit = tiku_text_input_event(&st->commit, e);
    if (e->type == TIKU_EVENT_TEXT_INPUT) {
        if (commit == TIKU_TEXT_COMPLETE) tiku_editor_insert_checked(st, st->commit.text);
        else if (commit == TIKU_TEXT_REJECTED)
            snprintf(st->note, sizeof st->note, "Invalid or oversized text input; nothing changed.");
        tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
    }
    if (e->type != TIKU_EVENT_KEY_DOWN) return 0;
    unsigned key = e->key;
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    if (e->modifiers & TIKU_MOD_CMD) {
        int cmd = 0;
        switch (key) {
        case 'n': cmd = CMD_NEW; break; case 'o': cmd = CMD_OPEN; break;
        case 's': cmd = e->modifiers & TIKU_MOD_SHIFT ? CMD_SAVEAS : CMD_SAVE; break;
        case 'w': case 'q': cmd = CMD_QUIT; break;
        case 'z': cmd = e->modifiers & TIKU_MOD_SHIFT ? CMD_REDO : CMD_UNDO; break;
        case 'y': cmd = CMD_REDO; break; case 'x': cmd = CMD_CUT; break;
        case 'c': cmd = CMD_COPY; break; case 'v': cmd = CMD_PASTE; break;
        case 'a': cmd = CMD_ALL; break; case 'f': cmd = CMD_FIND; break;
        case 'h': cmd = CMD_REPLACE; break; case 'g': cmd = CMD_GOTO; break;
        case 'r': cmd = CMD_RUN; break;
        case ']': cmd = CMD_NEXT; break; case '[': cmd = CMD_PREV; break;
        case TIKU_KEY_HOME: case TIKU_KEY_END:
            if (e->modifiers & TIKU_MOD_SHIFT) tiku_editor_buffer_select_begin(&st->tv);
            else tiku_editor_buffer_select_none(&st->tv);
            tiku_editor_buffer_place(&st->tv, key == TIKU_KEY_HOME ? 0 : st->tv.nline - 1,
                               key == TIKU_KEY_HOME ? 0 : INT_MAX);
            tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
        }
        return cmd ? tiku_editor_pick(st, st->id, cmd) : 0;
    }
    if (key == TIKU_KEY_ESCAPE) {
        if (st->finding) { tiku_editor_search_close(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0; }
        return tiku_editor_pick(st, st->id, CMD_QUIT);
    }
    if (key >= TIKU_KEY_UP && key <= TIKU_KEY_PAGE_DOWN) {
        if (e->modifiers & TIKU_MOD_SHIFT) tiku_editor_buffer_select_begin(&st->tv);
        else tiku_editor_buffer_select_none(&st->tv);
        const tiku_textview_move_t moves[] = {TIKU_TEXTVIEW_UP, TIKU_TEXTVIEW_DOWN,
            TIKU_TEXTVIEW_LEFT, TIKU_TEXTVIEW_RIGHT, TIKU_TEXTVIEW_HOME, TIKU_TEXTVIEW_END};
        if (key <= TIKU_KEY_END) tiku_editor_buffer_move(&st->tv, moves[key - 1]);
        else tiku_editor_buffer_place(&st->tv,
            st->tv.cy + (key == TIKU_KEY_PAGE_UP ? -1 : 1) * tiku_editor_rows_visible_of(st), st->tv.cx);
    } else if (key == TIKU_KEY_BACKSPACE || key == TIKU_KEY_DELETE) {
        /* Select the deletion first, so all mutations get the same preflight. */
        int a, b, c, d;
        if (!tiku_editor_buffer_selection(&st->tv, &a, &b, &c, &d)) {
            tiku_editor_buffer_select_begin(&st->tv);
            tiku_editor_buffer_move(&st->tv, key == TIKU_KEY_BACKSPACE ? TIKU_TEXTVIEW_LEFT : TIKU_TEXTVIEW_RIGHT);
        }
        tiku_editor_insert_checked(st, "");
    } else if (key == TIKU_KEY_RETURN) tiku_editor_insert_checked(st, "\n");
    else if (key == TIKU_KEY_TAB) tiku_editor_insert_checked(st, "\t");
    else {
        char text[2] = {(char)(e->text[0] ? (unsigned char)e->text[0] : e->key), 0};
        if ((unsigned char)text[0] < 32 || (unsigned char)text[0] >= 127) return 0;
        tiku_editor_insert_checked(st, text);
    }
    tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
}

/** Route the auxiliary Find window without ever treating its text as a document edit. */
int tiku_editor_event_in(void *state, uint32_t window, const tiku_event_t *e)
{
    edit_state_t *st = state;
    if (window != st->id && window != st->search_id) return 0;
    /* Background repaint/resize does not change the active editing target. */
    if (e->type == TIKU_EVENT_RESIZE || e->type == TIKU_EVENT_EXPOSE) {
        if (window == st->search_id) { tiku_editor_search_paint(st); return 0; }
        return tiku_editor_event(st, e);
    }
    st->search_active = window == st->search_id && st->search_id != 0;
    if (st->search_active) {
        if (e->type == TIKU_EVENT_CLOSE || (e->type == TIKU_EVENT_KEY_DOWN &&
            (e->modifiers & TIKU_MOD_CMD) && (e->key == 'w' || e->key == 'W'))) {
            tiku_editor_search_close(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
        }
    }
    if (e->type == TIKU_EVENT_ACTIVATED) tiku_editor_publish(st);
    return tiku_editor_event(st, e);
}

/** The runtime has already removed the frame; reopen dirty buffers to ask. */
void tiku_editor_closed(void *state, uint32_t window)
{
    edit_state_t *st = state;
    if (window == st->search_id) {
        st->search_id = 0; st->finding = 0; st->find_focus = -1; st->search_active = 0;
        tiku_editor_publish(st); return;
    }
    if (window == st->id) tiku_editor_search_close(st);
    if (window != st->id || !tiku_editor_buffer_modified(&st->tv)) return;
    st->id = st->services->open(st->services->ctx, "Edit — Unsaved changes", st->w, st->h);
    if (!st->id) return;
    memset(&st->last_menus, 0, sizeof st->last_menus);
    if (st->services->sizeable) st->services->sizeable(st->services->ctx, st->id);
    tiku_editor_guarded_action(st, CMD_QUIT); tiku_editor_publish(st); tiku_editor_paint(st);
}
