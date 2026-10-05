/* tiku_console_commands.c -- TikuConsole's menus and commands, typing
 * straight to the board, and the routing of the window's events.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_console_private.h"

/** Publish the Console and View menus. */
void tiku_console_publish(console_t *st)
{
    tiku_menuset_t menus = {0}; menus.nmenu = 2;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Console");
    const int ids[] = {START, RETURN, FILES, BREAK, COPY, COPY_LINE, CLEAR, FOLLOW, RECORD, LOG_PATH, DIRECT};
    menus.menu[0].nitem = sizeof ids / sizeof ids[0];
    for (int i = 0; i < menus.menu[0].nitem; i++) {
        snprintf(menus.menu[0].item[i].label, sizeof menus.menu[0].item[i].label, "%s", tiku_console_label(st, ids[i]));
        menus.menu[0].item[i].command = ids[i];
        menus.menu[0].item[i].enabled = ids[i] == FILES && st->state == ACTIVE ? 1 : tiku_console_enabled(st, ids[i]);
    }
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "View"); menus.menu[1].nitem = 3;
    for (int i = 0; i < 3; i++) {
        snprintf(menus.menu[1].item[i].label, sizeof menus.menu[1].item[i].label, "%s",
            i == 0 ? "Console" : i == 1 ? "Network" : "Device");
        menus.menu[1].item[i].command = 100 + i; menus.menu[1].item[i].enabled = 1;
    }
    if (st->services->menus) st->services->menus(st->services->ctx, st->id, &menus);
}

/** Copy the console's lines that pass the Find filter, or with @p selected
 *  only the selected ones, to the clipboard. */
void tiku_console_copy_history(console_t *st, int selected)
{
    if (!st->services->clip_set) return;
    tiku_console_rebuild(st);
    char *out = NULL; size_t used = 0, room = 0;
    for (int i = 0; i < st->visible; i++) {
        if (selected && !tiku_list_selected(&st->list, i)) continue;
        char line[COLS * 4 + 1]; tiku_console_line_string(&st->lines[st->index[i]], line, sizeof line);
        size_t n = strlen(line);
        if (used + n + 1 > TIKU_CLIPBOARD_LIMIT) {
            snprintf(st->note, sizeof st->note, "Too much text for the clipboard. Narrow Find or copy one selected line.");
            free(out); return;
        }
        if (used + n + 2 > room) {           /* the text, its newline, a NUL */
            size_t want = room ? room * 2 : 4096; char *bigger;
            while (want < used + n + 2) want *= 2;
            if (!(bigger = realloc(out, want))) {
                snprintf(st->note, sizeof st->note, "Not enough memory to copy the history.");
                free(out); return;
            }
            out = bigger; room = want;
        }
        memcpy(out + used, line, n); used += n; out[used++] = '\n'; out[used] = 0;
    }
    if (used) st->services->clip_set(st->services->ctx, out);
    free(out);
}

/** Run command @p id: a tab, opening or returning the console, Files,
 *  sending the command line, Break, the view and recording commands, and
 *  the Network and Device tabs' commands.  A disabled command does
 *  nothing.  @return 0. */
int tiku_console_action(console_t *st, int id)
{
    if (id >= 100 && id < 103) {
        tiku_tabs_select(&st->tabs, id - 100);
        if (id == 102 && st->state == IDLE && st->targets.selected >= 0) tiku_console_target_capture(st);
        st->dirty = 1; return 0;
    }
    if (!tiku_console_enabled(st, id) && !(id == FILES && st->state == ACTIVE)) return 0;
    if (id == START) tiku_console_begin(st);
    else if (id == RETURN || id == FILES) {
        st->files_pending = id == FILES;
        if (st->state == ACTIVE) tiku_console_reclaim(st);
        else if (st->state == RELEASING) st->cancelled = 1;
        else if (id == FILES && st->state == IDLE) tiku_console_files_open(st);
    } else if (id == SEND) {
        char command[TIKU_FIELD_MAX + 1]; snprintf(command, sizeof command, "%s\r", st->field[0].text);
        if (tiku_console_queue_bytes(st, command, strlen(command))) {
            if (*st->field[0].text) tiku_console_history_add(st, st->field[0].text);
            tiku_field_start(&st->field[0], ""); tiku_field_input_init(&st->input[0]);
        }
    } else if (id == BREAK) {
        st->out_n = st->out_at = 0; st->ping_active = 0; tiku_console_queue_bytes(st, "\x03", 1);
    } else if (id == CLEAR) {
        memset(st->lines, 0, sizeof st->lines); st->count = 1; st->head = st->column = 0;
        tiku_list_init(&st->list, 1);
    } else if (id == COPY || id == COPY_LINE) tiku_console_copy_history(st, id == COPY_LINE);
    else if (id == FOLLOW) st->follow = 1;
    else if (id == RECORD) tiku_console_record_toggle(st);
    else if (id == LOG_PATH && st->services->clip_set) {
        st->services->clip_set(st->services->ctx, st->log_path);
        snprintf(st->note, sizeof st->note, "Log path copied: %s", st->log_path);
    } else if (id == DIRECT) { st->direct = !st->direct; st->dialog.focus = st->direct ? LIST : FIELD; }
    else if (id == PING) tiku_console_ping_start(st);
    else if (id == PING_STOP) { st->ping_active = 0; tiku_console_ping_summary(st); }
    else {
        const char *cmd = id == SLIP_ON ? "slip on\r" : id == SLIP_OFF ? "slip off\r" :
            id == INFO ? "info\r" : id == MEMORY ? "free\r" :
            id == WIFI_SCAN ? "wifi scan\r" : id == WIFI_STATUS ? "wifi status\r" : NULL;
        if (cmd) { if (id == SLIP_OFF) st->ping_active = 0;
            tiku_console_queue_bytes(st, cmd, strlen(cmd)); tiku_tabs_select(&st->tabs, CONSOLE); }
    }
    st->dirty = 1; return 0;
}

/** Send a key typed in the output pane straight to the board: text,
 *  Return, Backspace, Escape and the arrows as ANSI sequences; Cmd-V
 *  pastes one line, Cmd-C sends Break, and Cmd with another letter its
 *  control byte.  @return 1 when the key was taken. */
int tiku_console_direct_event(console_t *st, const tiku_event_t *ev)
{
    int tr = tiku_text_input_event(&st->direct_commit, ev);
    if (tr == TIKU_TEXT_COMPLETE) {
        char plain[TIKU_TEXT_INPUT_MAX] = "";
        if (tiku_text_input_append(plain, sizeof plain, st->direct_commit.text))
            tiku_console_queue_bytes(st, plain, strlen(plain));
        else snprintf(st->note, sizeof st->note, "Text rejected: use one line without control characters.");
        return 1;
    }
    if (tr != TIKU_TEXT_IGNORED) return 1;
    if (ev->type != TIKU_EVENT_KEY_DOWN || ev->key == TIKU_KEY_TAB) return 0;
    unsigned k = ev->key;
    if ((ev->modifiers & TIKU_MOD_CMD) && (k == 'v' || k == 'V')) {
        char plain[TIKU_TEXT_INPUT_MAX] = "";
        const char *paste = st->services->clip_text ?
            st->services->clip_text(st->services->ctx, NULL) : "";
        if (strlen(paste) < sizeof plain - 1 && tiku_text_input_append(plain, sizeof plain, paste))
            tiku_console_queue_bytes(st, plain, strlen(plain));
        else snprintf(st->note, sizeof st->note, "Paste rejected: use one plain-text line without control characters.");
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && (k == 'c' || k == 'C')) { tiku_console_action(st, BREAK); return 1; }
    if (ev->modifiers & TIKU_MOD_CMD) {
        int lower = tolower((int)k);
        if (lower >= 'a' && lower <= 'z') {
            unsigned char control = (unsigned char)(lower - 'a' + 1);
            tiku_console_queue_bytes(st, &control, 1); return 1;
        }
        return 0;
    }
    const char *special = k == TIKU_KEY_RETURN ? "\r" : k == TIKU_KEY_BACKSPACE ? "\b" :
        k == TIKU_KEY_ESCAPE ? "\x1b" : k == TIKU_KEY_UP ? "\x1b[A" :
        k == TIKU_KEY_DOWN ? "\x1b[B" : k == TIKU_KEY_LEFT ? "\x1b[D" :
        k == TIKU_KEY_RIGHT ? "\x1b[C" : NULL;
    if (special) tiku_console_queue_bytes(st, special, strlen(special));
    else if (k >= 32 && k < 127) { char c = (char)k; tiku_console_queue_bytes(st, &c, 1); }
    else return 0;
    return 1;
}

/** Route an event in the TikuConsole window: a close, the device
 *  dropdown, direct typing, the history keys, the list, the controls and
 *  the fields.  @return 1 when the window may close; a close while the
 *  board is out asks for it back first. */
int tiku_console_event(void *state, uint32_t window, const tiku_event_t *ev)
{
    console_t *st = state; (void)window;
    if (ev->type == TIKU_EVENT_CLOSE) {
        if (st->state == IDLE) return 1;
        st->closing = st->cancelled = 1;
        if (st->state == ACTIVE) tiku_console_reclaim(st);
        return 0;
    }
    tiku_console_controls(st);
    tiku_choice_result_t cr = tiku_choice_event(&st->targets, ev, TIKU_CONSOLE_TARGET_RECT,
        (tiku_rect_t){0,0,CW,CH}, st->dialog.focus == TARGET);
    if (cr != TIKU_CHOICE_IGNORED) {
        if (cr == TIKU_CHOICE_CHANGED && st->state == IDLE && st->targets.selected >= 0) {
            snprintf(st->board, sizeof st->board, "%s", st->targets.item[st->targets.selected].label);
            tiku_console_target_changed(st);
        }
        st->dirty = 1; return 0;
    }
    if (st->tabs.current == CONSOLE && st->dialog.focus == LIST && st->direct && st->state == ACTIVE) {
        if (tiku_console_direct_event(st, ev)) { st->dirty = 1; return 0; }
    } else tiku_text_input_reset(&st->direct_commit);
    if (st->dialog.focus == FIELD && ev->type == TIKU_EVENT_KEY_DOWN &&
        (ev->key == TIKU_KEY_UP || ev->key == TIKU_KEY_DOWN)) {
        tiku_console_history_move(st, ev->key == TIKU_KEY_UP ? -1 : 1); st->dirty = 1; return 0;
    }
    if (st->dialog.focus == LIST && ev->type == TIKU_EVENT_KEY_DOWN && ev->key != TIKU_KEY_TAB) {
        st->follow = 0; tiku_list_key(&st->list, ev->key, ev->modifiers, TIKU_CONSOLE_BODY);
        st->dirty = 1; return 0;
    }
    tiku_dialog_result_t dr = tiku_dialog_event(&st->dialog, ev);
    if (dr.activated != TIKU_DIALOG_NONE) return tiku_console_action(st, dr.activated);
    for (int i = 0; i < st->dialog.count; i++) {
        const tiku_dialog_control_t *c = &st->dialog.control[i];
        if (c->id < FIELD || !c->enabled) continue;
        int f = c->id - FIELD;
        tiku_field_result_t fr = tiku_field_event(&st->field[f], &st->input[f], ev,
            c->rect, st->dialog.focus == c->id);
        if (fr != TIKU_FIELD_IGNORED) {
            if (f == 0 && fr == TIKU_FIELD_COMMITTED) tiku_console_action(st, SEND);
            if (f == 1) { st->follow = 0; st->list.top = 0; tiku_list_select_none(&st->list); }
            st->dirty = 1; return 0;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int tab = tiku_tabs_hit(&st->tabs, TIKU_CONSOLE_TABS_RECT, ev->x, ev->y);
        if (tab >= 0) tiku_console_action(st, 100 + tab);
        else if (st->tabs.current == CONSOLE) {
            int row = tiku_list_at(&st->list, TIKU_CONSOLE_BODY, ev->x, ev->y);
            if (row >= 0) { st->dialog.focus = LIST; st->follow = 0;
                tiku_list_click(&st->list, row, ev->modifiers); }
        }
    } else if (ev->type == TIKU_EVENT_WHEEL && st->tabs.current == CONSOLE) {
        st->follow = 0; tiku_list_scroll_to(&st->list, st->list.top + ev->dy * 3, TIKU_CONSOLE_BODY);
    }
    st->dirty = 1; return 0;
}
