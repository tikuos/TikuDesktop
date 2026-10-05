/* tiku_editor_input.c -- the Editor's input: the pointer and the keys,
 * the unsaved-changes question, the Find window's fields, and the
 * windows' close.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static int inside(tiku_rect_t r, int x, int y);
static int scrollbar_press(tiku_scroll_t *bar, int length, int at);
static void scroll_top(edit_state_t *st, int pane, int top);
static int pane_at(const edit_state_t *st, int x, int y);
static int pointer_down(edit_state_t *st, const tiku_event_t *e);
static void scroll_left(edit_state_t *st, int left);
static int edit_key(const tiku_event_t *e);
static int move_key(unsigned key);
static int view_key(edit_state_t *st, const tiku_event_t *e);
static int answer_question(edit_state_t *st, const tiku_event_t *e);
static int search_event(edit_state_t *st, const tiku_event_t *e);

static const int tool_commands[] = {CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_UNDO, CMD_REDO, CMD_FIND};

/** Whether @p x,@p y lies in @p r, its right and bottom edges excluded. */
static int inside(tiku_rect_t r, int x, int y)
{ return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/** The scroll value a press at @p at gives on a bar @p length long: its
 *  14-pixel end arrows step, and the track between them is pressed as
 *  tiku_scroll_press() takes it. */
static int scrollbar_press(tiku_scroll_t *bar, int length, int at)
{
    if (at < 14) return tiku_scroll_to(bar, bar->value - bar->step);
    if (at >= length - 14) return tiku_scroll_to(bar, bar->value + bar->step);
    return tiku_scroll_press(bar, length - 28, at - 14);
}

/** Scroll pane @p pane to row @p top, leaving the caret where it is. */
static void scroll_top(edit_state_t *st, int pane, int top)
{ tiku_codeview_scroll_to(st->panes[pane], top, tiku_codeview_left(st->panes[pane])); }

/** The pane holding @p x, @p y with its scrollbar, or -1 for none. */
static int pane_at(const edit_state_t *st, int x, int y)
{
    for (int i = 0; i < 2 && st->panes[i]; i++) {
        tiku_rect_t box = tiku_editor_pane_box(st, i);
        box.w += 15;
        if (inside(box, x, y)) return i;
    }
    return -1;
}

/** Button 1 down: a tool, a pane's scrollbar, the divider, the message
 *  over the footer, the horizontal scrollbar, or a pane, which takes the
 *  keys and the press.  @return 1 when a tool was pressed. */
static int pointer_down(edit_state_t *st, const tiku_event_t *e)
{
    int pane = pane_at(st, e->x, e->y);
    tiku_rect_t divider = tiku_editor_divider(st);
    for (int i = 0; i < 6; i++) if (inside(tiku_editor_tool_rect(i), e->x, e->y)) {
        if (tiku_editor_tool_enabled(st, i)) st->pressed_tool = i;
        return 1;
    }
    if (pane >= 0 && e->x >= st->w - 15) {
        tiku_rect_t box = tiku_editor_pane_box(st, pane);
        scroll_top(st, pane, scrollbar_press(pane ? &st->lower : &st->vertical,
                                              box.h, e->y - box.y));
    } else if (inside(divider, e->x, e->y)) {
        st->split_grab = e->y - divider.y;
    } else if ((st->note[0] || st->hover_tool >= 0) &&
               inside(tiku_editor_note_box(st), e->x, e->y)) {
        st->note[0] = 0; st->hover_tool = -1;   /* uncovers the scrollbar */
    } else if (inside((tiku_rect_t){208, st->h - EDIT_FOOT_H + 2, st->w - 224, 16}, e->x, e->y)) {
        scroll_left(st, scrollbar_press(&st->horizontal, st->w - 224, e->x - 208));
    } else {
        if (pane >= 0) tiku_editor_focus_pane(st, pane);
        tiku_editor_view_sync(st);
        (void)tiku_codeview_event(st->view, e);
    }
    return 0;
}
/** Scroll the pane with the keys to @p left across, leaving the caret
 *  where it is. */
static void scroll_left(edit_state_t *st, int left)
{ tiku_codeview_scroll_to(st->view, tiku_codeview_top(st->view), left); }

/** Whether @p e is a key the view edits by: text, Return, Tab, Backspace
 *  or Delete, pressed without Command. */
static int edit_key(const tiku_event_t *e)
{
    if (e->modifiers & TIKU_MOD_CMD) return 0;
    return e->key == TIKU_KEY_RETURN || e->key == TIKU_KEY_TAB ||
        e->key == TIKU_KEY_BACKSPACE || e->key == TIKU_KEY_DELETE ||
        e->text[0] != 0 || (e->key >= 32 && e->key < 127);
}

/** Whether @p key moves the caret by lines, line ends or pages. */
static int move_key(unsigned key)
{ return key == TIKU_KEY_UP || key == TIKU_KEY_DOWN || key == TIKU_KEY_HOME ||
         key == TIKU_KEY_END || key == TIKU_KEY_PAGE_UP || key == TIKU_KEY_PAGE_DOWN; }

/** Hand a key to the view.  Of the keys the view does not take, Option
 *  with a line move moves as the key alone does, and Return, Tab,
 *  Backspace and Delete with any modifier but Command still edit.
 *  @return 1 when the key edited or moved. */
static int view_key(edit_state_t *st, const tiku_event_t *e)
{
    tiku_event_t plain = *e;
    tiku_edit_status_t status = TIKU_EDIT_OK;
    int taken;
    tiku_editor_view_sync(st);
    taken = tiku_codeview_event(st->view, e);
    if (taken) status = tiku_codeview_status(st->view);
    else if (move_key(e->key) && (e->modifiers & TIKU_MOD_OPTION)) {
        plain.modifiers &= ~TIKU_MOD_OPTION;
        taken = tiku_codeview_event(st->view, &plain);
    } else if (e->key == TIKU_KEY_RETURN || e->key == TIKU_KEY_TAB) {
        const char *text = e->key == TIKU_KEY_TAB ? "\t" : st->newline;
        status = tiku_codeview_insert(st->view, text, strlen(text), 1);
        taken = 1;
    } else if (e->key == TIKU_KEY_BACKSPACE || e->key == TIKU_KEY_DELETE) {
        status = tiku_codeview_delete(st->view, e->key == TIKU_KEY_DELETE);
        if (status == TIKU_EDIT_NOTHING) status = TIKU_EDIT_OK;
        taken = 1;
    }
    if (taken && edit_key(e)) {
        if (status == TIKU_EDIT_OK) tiku_editor_edited(st);
        else tiku_editor_refused(st, status);
    }
    return taken;
}

/** Answer the unsaved-changes question from a key or a press: Discard
 *  runs the pending command; Save saves, through the file panel when the
 *  document has no name, then runs it; Cancel drops it.  @return 1 when
 *  the window is to close. */
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

/** Take an event for the Find window: its buttons and checkboxes,
 *  Escape, Tab, Return (find, replace or go to the line) and editing in
 *  the focused field.  @return 1 when the window took it. */
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
        if (st->finding == 1 &&
            inside((tiku_rect_t){84, 103, 180, 20}, e->x, e->y)) {
            st->regex = !st->regex;
            tiku_editor_paint(st);
            return 1;
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
            int64_t lines = tiku_editdoc_lines(st->doc);
            if (errno || end == st->field[2].text || *end || line < 1 || line > lines)
                snprintf(st->note, sizeof st->note, "Enter a line from 1 to %lld.",
                         (long long)lines);
            else {
                tiku_edit_pos_t at = tiku_editdoc_line_start(st->doc, line - 1);
                (void)tiku_codeview_select(st->view, at, at);
                tiku_editor_search_close(st); tiku_editor_reveal(st);
            }
        } else if (st->find_focus == 1) tiku_editor_replace_match(st);
        else tiku_editor_find_match(st, (e->modifiers & TIKU_MOD_SHIFT) != 0);
        tiku_editor_publish(st); tiku_editor_paint(st); return 1;
    }
    /* The desktop's clipboard comes through the services; the field
     * edits the kit's clipboard, synced with it around the edit. */
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
    /* An event for the Find window does not reach the document. */
    return 1;
}

/** Route an event for the document window: a resize, the open question,
 *  the context menu, the Find window's fields and Command keys, the
 *  pointer, then keys for the document.  @return 1 when the window is to
 *  close. */
int tiku_editor_event(void *state, const tiku_event_t *e)
{
    edit_state_t *st = state;
    if (e->type == TIKU_EVENT_RESIZE) {
        st->context_open = 0;
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
    if (st->context_open && tiku_editor_context_event(st, e)) return 0;
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button > 1) {
        /* Button 3 over a pane opens the context menu there unless the
         * Find window has the keys; other buttons do nothing. */
        int pane = pane_at(st, e->x, e->y);
        if (e->button == 3 && pane >= 0 && !st->search_active) {
            tiku_editor_focus_pane(st, pane);
            tiku_editor_context_open(st, e->x, e->y);
            tiku_editor_paint(st);
        }
        return 0;
    }
    /* With a Find window field focused, the document's Command keys
     * still work. */
    if (st->finding && st->search_active && st->find_focus >= 0 && e->type == TIKU_EVENT_KEY_DOWN &&
        (e->modifiers & TIKU_MOD_CMD)) {
        unsigned k = e->key >= 'A' && e->key <= 'Z' ? e->key + 32 : e->key;
        int cmd = k == 's' ? (e->modifiers & TIKU_MOD_SHIFT ? CMD_SAVEAS : CMD_SAVE) :
            k == 'o' ? CMD_OPEN : k == 'n' ? CMD_NEW :
            k == 'w' || k == 'q' ? CMD_QUIT : k == 'f' ? CMD_FIND :
            k == 'h' ? CMD_REPLACE : k == 'g' ? CMD_GOTO :
            k == ']' ? CMD_NEXT : k == '[' ? CMD_PREV : k == 'y' ? CMD_REDO : 0;
        if (cmd) return tiku_editor_pick(st, st->search_id, cmd);
    }
    if (e->type == TIKU_EVENT_CLOSE) return tiku_editor_pick(st, st->id, CMD_QUIT);
    if (e->type == TIKU_EVENT_EXPOSE) { tiku_editor_paint(st); return 0; }
    if (e->type == TIKU_EVENT_DEACTIVATED) {
        for (int i = 0; i < 3; i++) st->field_input[i].dragging = 0;
        st->hover_tool = st->pressed_tool = -1;
        tiku_scroll_release(&st->vertical); tiku_scroll_release(&st->horizontal);
        tiku_scroll_release(&st->lower); st->split_grab = -1;
        tiku_text_input_reset(&st->commit);
        for (int i = 0; i < 2 && st->panes[i]; i++) (void)tiku_codeview_event(st->panes[i], e);
        return 0;
    }
    if (search_event(st, e)) return 0;
    if (e->type == TIKU_EVENT_WHEEL) {
        int pane = pane_at(st, e->x, e->y);
        tiku_editor_view_sync(st);
        (void)tiku_codeview_event(st->panes[pane >= 0 ? pane : st->pane], e);
        tiku_editor_paint(st); return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN) {
        tiku_text_input_reset(&st->commit);
        if (pointer_down(st, e)) { tiku_editor_paint(st); return 0; }
        tiku_editor_publish(st); tiku_editor_paint(st); return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE) {
        tiku_rect_t upper = tiku_editor_pane_box(st, 0), lower = tiku_editor_pane_box(st, 1);
        if (st->split_grab >= 0)
            st->split_at = e->y - st->split_grab - upper.y;
        else if (st->vertical.tracking)
            scroll_top(st, 0, tiku_scroll_drag(&st->vertical, upper.h - 28, e->y - upper.y - 14));
        else if (st->lower.tracking)
            scroll_top(st, 1, tiku_scroll_drag(&st->lower, lower.h - 28, e->y - lower.y - 14));
        else if (st->horizontal.tracking)
            scroll_left(st, tiku_scroll_drag(&st->horizontal, st->w - 252, e->x - 222));
        else if (!tiku_codeview_event(st->view, e)) {
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
        tiku_scroll_release(&st->vertical); tiku_scroll_release(&st->horizontal);
        tiku_scroll_release(&st->lower);
        int held = st->split_grab >= 0;
        st->split_grab = -1;
        int dragged = tiku_codeview_event(st->view, e);
        if (dragged) tiku_editor_publish(st);
        if (pressed >= 0 || dragged || held) tiku_editor_paint(st);
        return 0;
    }
    if ((e->type == TIKU_EVENT_KEY_DOWN || e->type == TIKU_EVENT_KEY_UP) &&
        (e->key == TIKU_KEY_CMD || e->key == TIKU_KEY_SHIFT ||
         e->key == TIKU_KEY_OPTION)) {
        /* A modifier alone may make the jump target under the pointer
         * live or not; nothing moves. */
        tiku_editor_view_sync(st);
        if (tiku_codeview_event(st->view, e)) tiku_editor_paint(st);
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
        int cmd = 0, shift = (e->modifiers & TIKU_MOD_SHIFT) != 0;
        switch (key) {
        case 'n': cmd = CMD_NEW; break; case 'o': cmd = CMD_OPEN; break;
        case 's': cmd = e->modifiers & TIKU_MOD_SHIFT ? CMD_SAVEAS : CMD_SAVE; break;
        case 'w': case 'q': cmd = CMD_QUIT; break;
        case 'z': cmd = e->modifiers & TIKU_MOD_SHIFT ? CMD_REDO : CMD_UNDO; break;
        case 'y': cmd = CMD_REDO; break; case 'x': cmd = CMD_CUT; break;
        case 'c': cmd = CMD_COPY; break; case 'v': cmd = CMD_PASTE; break;
        case 'a': cmd = CMD_ALL; break; case 'f': cmd = CMD_FIND; break;
        case '=': case '+': cmd = CMD_ZOOM_IN; break;
        case '-': cmd = CMD_ZOOM_OUT; break;
        case '0': cmd = CMD_ZOOM_RESET; break;
        case ',': cmd = CMD_PREFS; break;
        case 'h': cmd = CMD_REPLACE; break; case 'g': cmd = CMD_GOTO; break;
        case 'r': cmd = CMD_RUN; break;
        case ']': cmd = shift ? CMD_UNFOLD : CMD_NEXT; break;
        case '[': cmd = shift ? CMD_FOLD : CMD_PREV; break;
        case '}': cmd = CMD_UNFOLD; break; case '{': cmd = CMD_FOLD; break;
        case '/': cmd = CMD_COMMENT; break;
        case 'd': cmd = shift ? 0 : CMD_DUPLICATE; break;
        case 'k': cmd = shift ? CMD_DELETE_LINES : 0; break;
        case 'l': cmd = shift ? CMD_LINE_ENDS : 0; break;
        case 'm': cmd = shift ? CMD_SELECT_BRACKET : CMD_BRACKET; break;
        case TIKU_KEY_UP: cmd = shift ? CMD_LINES_UP : 0; break;
        case TIKU_KEY_DOWN: cmd = shift ? CMD_LINES_DOWN : 0; break;
        case TIKU_KEY_BACKSPACE: cmd = CMD_WORD_BACK; break;
        case TIKU_KEY_DELETE: cmd = CMD_WORD_ON; break;
        case TIKU_KEY_HOME: case TIKU_KEY_END: case TIKU_KEY_LEFT: case TIKU_KEY_RIGHT:
            view_key(st, e);
            tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
        }
        return cmd ? tiku_editor_pick(st, st->id, cmd) : 0;
    }
    if (key == TIKU_KEY_ESCAPE) {
        if (st->finding) { tiku_editor_search_close(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0; }
        /* Several carets go back to the main one before Escape closes. */
        tiku_editor_view_sync(st);
        if (tiku_codeview_keep_main(st->view) == TIKU_EDIT_OK) {
            tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
        }
        return tiku_editor_pick(st, st->id, CMD_QUIT);
    }
    if (!view_key(st, e)) return 0;
    tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
}

/** Route an event by window: Preferences, the Find window or the document
 *  window.  @return 1 when the document window is to close. */
int tiku_editor_event_in(void *state, uint32_t window, const tiku_event_t *e)
{
    edit_state_t *st = state;
    if (window && window == st->prefs_id)
        return tiku_editor_preferences_event(st, e);
    if (window != st->id && window != st->search_id) return 0;
    /* A resize or expose does not change which window has the keys. */
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

/** The desktop closed @p window.  A document window closed with unsaved
 *  changes is opened again to ask whether to save them. */
void tiku_editor_closed(void *state, uint32_t window)
{
    edit_state_t *st = state;
    if (window == st->prefs_id) {
        st->prefs_id = 0;
        return;
    }
    if (window == st->search_id) {
        tiku_editor_search_clear(st);
        st->search_id = 0; st->finding = 0; st->find_focus = -1; st->search_active = 0;
        tiku_editor_publish(st); return;
    }
    if (window == st->id) {
        tiku_editor_search_close(st);
        tiku_editor_preferences_close(st);
    }
    if (window != st->id || !tiku_editor_modified(st)) return;
    st->id = st->services->open(st->services->ctx, "Edit — Unsaved changes", st->w, st->h);
    if (!st->id) return;
    memset(&st->last_menus, 0, sizeof st->last_menus);
    if (st->services->sizeable) st->services->sizeable(st->services->ctx, st->id);
    tiku_editor_guarded_action(st, CMD_QUIT); tiku_editor_publish(st); tiku_editor_paint(st);
}
