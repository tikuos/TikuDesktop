/* tiku_editor_commands.c -- commands.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static int word_byte(unsigned char c);
static int matches(edit_state_t *st, const char *line, const char *p);
static const char *next_match(edit_state_t *st, const char *line, const char *p);
static void clipboard_command(edit_state_t *st, int command);

/* Editor commands and guarded document transitions.
 * SPDX-License-Identifier: Apache-2.0 */

/** Commit without the old line-size or intermediate undo-buffer restrictions. */
int tiku_editor_insert_checked(edit_state_t *st, const char *text)
{
    if (!tiku_editor_buffer_replace(&st->tv, text)) {
        snprintf(st->note, sizeof st->note,
            "Cannot apply edit: invalid UTF-8 or not enough memory. Text kept.");
        return 0;
    }
    st->note[0] = 0; st->pending = 0; st->allow_open = 0;
    st->width_dirty = 1;
    return 1;
}

/** ASCII case folding and UTF-8-safe word boundaries for literal search. */
static int word_byte(unsigned char c)
{ return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static int matches(edit_state_t *st, const char *line, const char *p)
{
    size_t n = strlen(st->field[0].text);
    if (!n) return 0;
    if (st->match_case ? strncmp(p, st->field[0].text, n) : strncasecmp(p, st->field[0].text, n)) return 0;
    return !st->whole_word || ((p == line || !word_byte((unsigned char)p[-1])) && !word_byte((unsigned char)p[n]));
}
static const char *next_match(edit_state_t *st, const char *line, const char *p)
{
    for (; *p; p++) if (((unsigned char)*p & 0xc0) != 0x80 && matches(st, line, p)) return p;
    return NULL;
}
/** Find literal text, wrapping once through the document. */
void tiku_editor_find_match(edit_state_t *st, int backward)
{
    const char *query = st->field[0].text;
    size_t n = strlen(query);
    if (!n) { snprintf(st->note, sizeof st->note, "Enter text to find."); return; }
    int cy = st->tv.cy, cx = st->tv.cx, a, b, c, d;
    if (backward && tiku_editor_buffer_selection(&st->tv, &a, &b, &c, &d)) {
        cy = a; cx = b;
    }
    int best_y = -1, best_x = -1, wrap_y = -1, wrap_x = -1;
    for (int y = 0; y < st->tv.nline; y++) {
        const char *line = tiku_editor_buffer_line(&st->tv, y), *p = line;
        while ((p = next_match(st, line, p)) != NULL) {
            int x = (int)(p - line);
            if (!backward) {
                if (wrap_y < 0) { wrap_y = y; wrap_x = x; }
                if (best_y < 0 && (y > cy || (y == cy && x >= cx))) {
                    best_y = y; best_x = x;
                }
            } else {
                wrap_y = y; wrap_x = x;
                if (y < cy || (y == cy && x < cx)) { best_y = y; best_x = x; }
            }
            p += n;
        }
    }
    int wrapped = best_y < 0;
    if (wrapped) { best_y = wrap_y; best_x = wrap_x; }
    if (best_y < 0) { snprintf(st->note, sizeof st->note, "No matches."); return; }
    tiku_editor_buffer_select_none(&st->tv);
    tiku_editor_buffer_place(&st->tv, best_y, best_x);
    tiku_editor_buffer_select_begin(&st->tv);
    tiku_editor_buffer_place(&st->tv, best_y, best_x + (int)n);
    tiku_editor_reveal(st);
    snprintf(st->note, sizeof st->note, "%s", wrapped ? "Search wrapped." : "Match selected.");
}

/** Replace only a selected exact match; otherwise select the next match. */
void tiku_editor_replace_match(edit_state_t *st)
{
    int y0, x0, y1, x1;
    size_t n = strlen(st->field[0].text);
    if (!n) return;
    if (!tiku_editor_buffer_selection(&st->tv, &y0, &x0, &y1, &x1) || y0 != y1 ||
        x1 - x0 != (int)n ||
        !matches(st, tiku_editor_buffer_line(&st->tv, y0), tiku_editor_buffer_line(&st->tv, y0) + x0)) {
        tiku_editor_find_match(st, 0); return;
    }
    if (tiku_editor_insert_checked(st, st->field[1].text)) {
        tiku_editor_find_match(st, 0);
        snprintf(st->note, sizeof st->note, "Replaced one match. Undo is available.");
    }
}

/** Build Replace All first, then record it as one reversible transaction. */
void tiku_editor_replace_all(edit_state_t *st)
{
    size_t q = strlen(st->field[0].text), r = strlen(st->field[1].text);
    if (!q) return;
    size_t n = tiku_editor_buffer_text(&st->tv, NULL, 0), count = 0;
    if (n > INT_MAX - 1u) goto failed;
    char *source = malloc(n + 1);
    if (!source) goto failed;
    source[0] = 0;
    tiku_editor_buffer_text(&st->tv, source, n + 1);
    const char *p = source;
    while ((p = next_match(st, source, p))) { count++; p += q; }
    if (!count) { free(source); snprintf(st->note, sizeof st->note, "No matches."); return; }
    if (r > q && count > (INT_MAX - 1u - n) / (r - q)) { free(source); goto failed; }
    size_t size = r >= q ? n + count * (r - q) : n - count * (q - r);
    char *result = malloc(size + 1);
    if (!result) { free(source); goto failed; }
    size_t out = 0; p = source;
    const char *hit;
    while ((hit = next_match(st, source, p))) {
        size_t before = (size_t)(hit - p);
        memcpy(result + out, p, before); out += before;
        memcpy(result + out, st->field[1].text, r); out += r;
        p = hit + q;
    }
    strcpy(result + out, p);
    int cy = st->tv.cy, cx = st->tv.cx, ay = st->tv.ay, ax = st->tv.ax, sel = st->tv.selecting;
    tiku_editor_buffer_select_all(&st->tv);
    int ok = tiku_editor_insert_checked(st, result);
    free(source); free(result);
    if (!ok) {
        st->tv.cy = cy; st->tv.cx = cx; st->tv.ay = ay; st->tv.ax = ax; st->tv.selecting = sel;
        return;
    }
    tiku_editor_buffer_place(&st->tv, cy, cx); tiku_editor_reveal(st);
    snprintf(st->note, sizeof st->note, "%zu replacements. Undo restores the original text.", count);
    return;
failed:
    snprintf(st->note, sizeof st->note, "Replace All exceeds available memory or text indexes. Text kept.");
}

/** Open an optional native file panel beside the current document. */
void tiku_editor_file_panel(edit_state_t *st, int saving)
{
    if (!st->services->pick) {
        snprintf(st->note, sizeof st->note, "This host does not provide file panels.");
        st->pending = 0; return;
    }
    char dir[512]; snprintf(dir, sizeof dir, "%s", st->path);
    char *slash = strrchr(dir, '/');
    if (slash == dir) slash[1] = 0;
    else if (slash) *slash = 0;
    else dir[0] = 0;
    const char *name = strrchr(st->path, '/');
    name = name ? name + 1 : st->path;
    st->pick_save = saving;
    if (st->services->pick(st->services->ctx, st->id,
        saving ? TIKU_APP_PICK_SAVE : TIKU_APP_PICK_OPEN,
        *dir ? dir : NULL, saving ? (*name ? name : "Untitled.txt") : NULL) < 0) {
        st->pending = 0;
        snprintf(st->note, sizeof st->note, "Could not open the file panel.");
    }
}

/** Execute a document transition only after its unsaved-change guard. */
int tiku_editor_document_action(edit_state_t *st, int command)
{
    st->pending = 0;
    if (command == CMD_QUIT) { tiku_editor_search_close(st); tiku_editor_buffer_saved(&st->tv); return 1; }
    if (command == CMD_OPEN) tiku_editor_file_panel(st, 0);
    if (command == CMD_REVERT) {
        char path[512]; snprintf(path, sizeof path, "%s", st->path);
        tiku_editor_read_document(st, path);
    }
    if (command == CMD_NEW) {
        if (!tiku_editor_buffer_set(&st->tv, "")) {
            snprintf(st->note, sizeof st->note, "Not enough memory; document kept.");
            return 0;
        }
        st->width_dirty = 1;
        st->path[0] = st->note[0] = 0;
        st->disk_known = st->load_failed = st->bom = st->crlf = st->left = 0;
        st->lang = TIKU_SYNTAX_NONE;
    }
    return 0;
}

/** Every destructive UI route uses the same Cancel-first question. */
int tiku_editor_guarded_action(edit_state_t *st, int command)
{
    if (!tiku_editor_buffer_modified(&st->tv)) return tiku_editor_document_action(st, command);
    tiku_editor_search_close(st);
    st->pending = command;
    tiku_alert_open(&st->ask, TIKU_ALERT_WARN, command,
        "This document has unsaved changes. Save them before continuing?",
        "Cancel|Discard|Save");
    st->ask.dflt = 0;
    return 0;
}

/** Bounded clipboard transfers never silently cut a selection short. */
static void clipboard_command(edit_state_t *st, int command)
{
    char *text;
    if (command == CMD_PASTE) {
        const char *held = st->services->clip_text
                           ? st->services->clip_text(st->services->ctx, NULL) : "";
        if (held[0] && (text = malloc(strlen(held) + 1))) {
            size_t out = 0;
            for (size_t i = 0; held[i]; i++) {
                if (held[i] == '\r' && held[i + 1] == '\n') continue;
                text[out++] = held[i];
            }
            text[out] = 0;
            tiku_editor_insert_checked(st, text);
            free(text);
        }
        return;
    }
    int y0, x0, y1, x1;
    if (!tiku_editor_buffer_selection(&st->tv, &y0, &x0, &y1, &x1)) return;
    size_t needed = (size_t)(y1 - y0);
    for (int y = y0; y <= y1; y++)
        needed += (size_t)((y == y1 ? x1 : st->tv.line[y].len) - (y == y0 ? x0 : 0));
    if (needed > TIKU_CLIPBOARD_LIMIT) {
        snprintf(st->note, sizeof st->note, "Selection exceeds the desktop clipboard's %u MB limit.",
                 TIKU_CLIPBOARD_LIMIT / (1024u * 1024u));
        return;
    }
    if (!(text = malloc(needed + 1))) {
        snprintf(st->note, sizeof st->note, "Not enough memory to copy the selection.");
        return;
    }
    size_t n = tiku_editor_buffer_selected(&st->tv, text, needed + 1);
    if (n == needed && n && st->services->clip_set &&
        st->services->clip_set(st->services->ctx, text) >= 0 && command == CMD_CUT)
        tiku_editor_insert_checked(st, "");
    free(text);
}

/** Menus expose the same commands and shortcut routes as the keyboard. */
void tiku_editor_publish(edit_state_t *st)
{
    tiku_menuset_t menus = {0}; menus.nmenu = 4;
    const char *titles[] = {"File", "Edit", "Search", "View"};
    const char *labels[][7] = {
        {"New", "Open…", "Save", "Save As…", "Revert", "Close"},
        {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All"},
        {"Find…", "Find Next", "Find Previous", "Replace…", "Go to Line…", "Replace All"},
        {"Line Numbers", "Colour as BASIC", "Run", "Show Invisibles"}};
    const int commands[][7] = {
        {CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_REVERT, CMD_QUIT},
        {CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_ALL},
        {CMD_FIND, CMD_NEXT, CMD_PREV, CMD_REPLACE, CMD_GOTO, CMD_REPLACE_ALL},
        {CMD_NUMBERS, CMD_SYNTAX, CMD_RUN, CMD_INVISIBLES}};
    const char *keys[] = {"nos  w", "zyxcva", "f][hg ", "  r "};
    const int counts[] = {6, 6, 6, 4};
    int a, b, c, d;
    int selected = tiku_editor_buffer_selection(&st->tv, &a, &b, &c, &d);
    int field = st->finding && st->search_active && st->find_focus >= 0;
    if (field) {
        tiku_field_t *f = &st->field[st->find_focus];
        selected = f->sel_a != f->sel_b;
    }
    for (int m = 0; m < 4; m++) {
        snprintf(menus.menu[m].title, sizeof menus.menu[m].title, "%s", titles[m]);
        menus.menu[m].nitem = counts[m];
        for (int i = 0; i < counts[m]; i++) {
            int cmd = commands[m][i], enabled = !st->ask.open;
            if (cmd == CMD_UNDO) enabled &= field ?
                st->field_input[st->find_focus].nundo > 0 : tiku_editor_buffer_can_undo(&st->tv);
            if (cmd == CMD_REDO) enabled &= field ?
                st->field_input[st->find_focus].nredo > 0 : tiku_editor_buffer_can_redo(&st->tv);
            if (cmd == CMD_COPY || cmd == CMD_CUT) enabled &= selected && st->services->clip_set != NULL;
            if (cmd == CMD_PASTE) enabled &= st->services->clip_text != NULL;
            if (cmd == CMD_OPEN || cmd == CMD_SAVEAS) enabled &= st->services->pick != NULL;
            if (cmd == CMD_REVERT) enabled &= st->path[0] != 0;
            if (cmd == CMD_RUN) enabled &= st->lang == TIKU_SYNTAX_BASIC && !st->running;
            snprintf(menus.menu[m].item[i].label, sizeof menus.menu[m].item[i].label, "%s", labels[m][i]);
            menus.menu[m].item[i].command = cmd;
            menus.menu[m].item[i].enabled = enabled;
            menus.menu[m].item[i].sc = keys[m][i] == ' ' ? 0 : keys[m][i];
            menus.menu[m].item[i].marked = cmd == CMD_NUMBERS ? st->numbers :
                cmd == CMD_SYNTAX ? st->lang == TIKU_SYNTAX_BASIC :
                cmd == CMD_INVISIBLES ? st->show_invisibles : 0;
        }
    }
    if (memcmp(&menus, &st->last_menus, sizeof menus) &&
        st->services->menus(st->services->ctx, st->id, &menus) >= 0)
        st->last_menus = menus;
    if (st->search_id) st->services->menus(st->services->ctx, st->search_id, &menus);
}

/** Dispatch commands once; menu actions and key bindings cannot diverge. */
int tiku_editor_pick(void *state, uint32_t window, int command)
{
    edit_state_t *st = state;
    if (window == st->id || (window && window == st->search_id))
        st->search_active = window == st->search_id;
    if (st->ask.open) return 0;
    if (st->finding && st->search_active && st->find_focus >= 0 && command >= CMD_UNDO && command <= CMD_ALL) {
        static const unsigned keys[] = {'z', 'y', 'x', 'c', 'v', 'a'};
        unsigned stamp = tiku_clipboard_stamp();
        if (command == CMD_PASTE && st->services->clip_text) {
            tiku_clipboard_set(st->services->clip_text(st->services->ctx, NULL));
            stamp = tiku_clipboard_stamp();
        }
        tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .modifiers = TIKU_MOD_CMD,
            .key = keys[command - CMD_UNDO]};
        int i = st->find_focus;
        tiku_field_event(&st->field[i], &st->field_input[i], &e, tiku_editor_search_field(st, i), 1);
        if (stamp != tiku_clipboard_stamp() && st->services->clip_set)
            st->services->clip_set(st->services->ctx, tiku_clipboard_text());
        tiku_editor_publish(st); tiku_editor_paint(st); return 0;
    }
    st->pending = 0;
    st->allow_open = 0;
    switch (command) {
    case CMD_NEW: case CMD_OPEN: case CMD_REVERT: case CMD_QUIT:
        if (tiku_editor_guarded_action(st, command)) return 1;
        break;
    case CMD_SAVE:
        if (!st->path[0]) tiku_editor_file_panel(st, 1); else tiku_editor_save(st);
        break;
    case CMD_SAVEAS: tiku_editor_file_panel(st, 1); break;
    case CMD_UNDO: tiku_editor_buffer_undo(&st->tv); st->note[0] = 0; st->width_dirty = 1; break;
    case CMD_REDO: tiku_editor_buffer_redo(&st->tv); st->note[0] = 0; st->width_dirty = 1; break;
    case CMD_CUT: case CMD_COPY: case CMD_PASTE: clipboard_command(st, command); break;
    case CMD_ALL: tiku_editor_buffer_select_all(&st->tv); break;
    case CMD_FIND: case CMD_REPLACE: case CMD_GOTO:
        tiku_editor_search_open_window(st, command == CMD_GOTO ? 2 : 1);
        st->find_focus = command == CMD_GOTO ? 2 : 0;
        for (int i = 0; i < 3; i++) {
            st->field[i].sel_a = 0; st->field[i].sel_b = st->field[i].len;
            tiku_field_input_init(&st->field_input[i]);
        }
        st->note[0] = 0;
        break;
    case CMD_NEXT: tiku_editor_find_match(st, 0); break;
    case CMD_PREV: tiku_editor_find_match(st, 1); break;
    case CMD_REPLACE_ALL: tiku_editor_replace_all(st); break;
    case CMD_NUMBERS: st->numbers = !st->numbers; break;
    case CMD_INVISIBLES: st->show_invisibles = !st->show_invisibles; break;
    case CMD_SYNTAX:
        st->lang = st->lang == TIKU_SYNTAX_BASIC ? TIKU_SYNTAX_NONE : TIKU_SYNTAX_BASIC;
        break;
    case CMD_RUN: if (st->lang == TIKU_SYNTAX_BASIC) tiku_editor_run_program(st); break;
    default: return 0;
    }
    tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
}

/** File-panel answers never replace a document edited while the panel was open. */
void tiku_editor_picked(void *state, uint32_t window, const char *path)
{
    edit_state_t *st = state; (void)window;
    if (!path || !*path || st->ask.open) { st->pending = 0; return; }
    if (st->pick_save) {
        int pending = st->pending; st->pending = 0;
        if (tiku_editor_save_document(st, path) && pending && tiku_editor_document_action(st, pending)) {
            if (st->services->close) st->services->close(st->services->ctx, st->id);
            return;
        }
    } else if (tiku_editor_buffer_modified(&st->tv) && !st->allow_open) {
        snprintf(st->note, sizeof st->note, "Open cancelled: the document has unsaved edits.");
    } else tiku_editor_read_document(st, path);
    st->allow_open = 0;
    tiku_editor_publish(st); tiku_editor_paint(st);
}
