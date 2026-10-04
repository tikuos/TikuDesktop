/* tiku_editor_commands.c -- commands.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static tiku_edit_query_t query(const edit_state_t *st);
static int count_crlf(void *ctx, const char *bytes, size_t n);
static void clipboard_command(edit_state_t *st, int command);

/* Editor commands and guarded document transitions.
 * SPDX-License-Identifier: Apache-2.0 */

/** Whether the text differs from what was last loaded or saved. */
int tiku_editor_modified(const edit_state_t *st)
{ return tiku_editdoc_modified(st->doc); }

/** A successful edit clears the footer message and drops the actions
 *  that waited on the text before it: the pending one, and an Open that
 *  Discard allowed. */
void tiku_editor_edited(edit_state_t *st)
{ st->note[0] = 0; st->pending = 0; st->allow_open = 0; }

/** Say why an edit changed nothing. */
void tiku_editor_refused(edit_state_t *st, tiku_edit_status_t status)
{
    snprintf(st->note, sizeof st->note, "%s", status == TIKU_EDIT_LIMIT
        ? "Cannot apply edit: the document would pass its size limit. Text kept."
        : "Cannot apply edit: invalid UTF-8 or not enough memory. Text kept.");
}

/** Replace the selection with @p text as one undo step of its own. */
int tiku_editor_insert_checked(edit_state_t *st, const char *text)
{
    tiku_edit_status_t status = tiku_codeview_insert(st->view, text, strlen(text), 0);
    if (status != TIKU_EDIT_OK) { tiku_editor_refused(st, status); return 0; }
    tiku_editor_edited(st);
    return 1;
}

/** The Find field's text under the window's Match case and Whole words. */
static tiku_edit_query_t query(const edit_state_t *st)
{
    tiku_edit_query_t q = {st->field[0].text, strlen(st->field[0].text),
        (st->match_case ? TIKU_EDIT_MATCH_CASE : 0u) |
        (st->whole_word ? TIKU_EDIT_WHOLE_WORD : 0u)};
    return q;
}

/** Select the next match, or with @p backward the previous one, wrapping
 *  once through the document. */
void tiku_editor_find_match(edit_state_t *st, int backward)
{
    tiku_edit_query_t q = query(st);
    tiku_codeview_found_t found;
    if (!q.length) { snprintf(st->note, sizeof st->note, "Enter text to find."); return; }
    if (tiku_codeview_find(st->view, &q, backward, &found) != TIKU_EDIT_OK) {
        snprintf(st->note, sizeof st->note, "No matches.");
        return;
    }
    snprintf(st->note, sizeof st->note, "%s", found.wrapped ? "Search wrapped." : "Match selected.");
}

/** Replace only a selected exact match; otherwise select the next match. */
void tiku_editor_replace_match(edit_state_t *st)
{
    tiku_edit_query_t q = query(st);
    tiku_codeview_found_t found;
    const char *with = st->field[1].text;
    if (!q.length) return;
    tiku_edit_status_t status = tiku_codeview_replace(st->view, &q, with, strlen(with), &found);
    if (found.replaced) {
        tiku_editor_edited(st);
        snprintf(st->note, sizeof st->note, "Replaced one match. Undo is available.");
    } else if (status == TIKU_EDIT_OK)
        snprintf(st->note, sizeof st->note, "%s", found.wrapped ? "Search wrapped." : "Match selected.");
    else if (status == TIKU_EDIT_NOTHING)
        snprintf(st->note, sizeof st->note, "No matches.");
    else tiku_editor_refused(st, status);
}

/** Replace every match as one undo step; each selection stays on the text
 *  it was on. */
void tiku_editor_replace_all(edit_state_t *st)
{
    tiku_edit_query_t q = query(st);
    tiku_codeview_found_t found;
    const char *with = st->field[1].text;
    if (!q.length) return;
    tiku_edit_status_t status = tiku_codeview_replace_all(st->view, &q, with, strlen(with), &found);
    if (status == TIKU_EDIT_NOTHING) { snprintf(st->note, sizeof st->note, "No matches."); return; }
    if (status != TIKU_EDIT_OK) {
        snprintf(st->note, sizeof st->note,
                 "Replace All exceeds available memory or the document's size limit. Text kept.");
        return;
    }
    tiku_editor_edited(st);
    snprintf(st->note, sizeof st->note, "%zu replacements. Undo restores the original text.",
             found.replaced);
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
    if (command == CMD_QUIT) {
        tiku_editor_search_close(st);
        tiku_editdoc_saved(st->doc, tiku_editdoc_state(st->doc));
        return 1;
    }
    if (command == CMD_OPEN) tiku_editor_file_panel(st, 0);
    if (command == CMD_REVERT) {
        char path[512]; snprintf(path, sizeof path, "%s", st->path);
        tiku_editor_read_document(st, path);
    }
    if (command == CMD_NEW) {
        if (tiku_editdoc_load(st->doc, "", 0) != TIKU_EDIT_OK) {
            snprintf(st->note, sizeof st->note, "Not enough memory; document kept.");
            return 0;
        }
        st->path[0] = st->note[0] = 0;
        st->disk_known = st->load_failed = st->bom = 0;
        snprintf(st->newline, sizeof st->newline, "\n");
        (void)tiku_codeview_set_newline(st->view, st->newline);
        st->lang = TIKU_SYNTAX_NONE;
        tiku_codeview_set_language(st->view, st->lang);
    }
    return 0;
}

/** Every destructive UI route uses the same Cancel-first question. */
int tiku_editor_guarded_action(edit_state_t *st, int command)
{
    if (!tiku_editor_modified(st)) return tiku_editor_document_action(st, command);
    tiku_editor_search_close(st);
    st->pending = command;
    tiku_alert_open(&st->ask, TIKU_ALERT_WARN, command,
        "This document has unsaved changes. Save them before continuing?",
        "Cancel|Discard|Save");
    st->ask.dflt = 0;
    return 0;
}

/** Count the CRLF pairs in runs of the document; @p ctx holds the count
 *  and the run before's last byte. */
static int count_crlf(void *ctx, const char *bytes, size_t n)
{
    int64_t *seen = ctx;
    for (size_t i = 0; i < n; i++) {
        seen[0] += bytes[i] == '\n' && seen[1] == '\r';
        seen[1] = (unsigned char)bytes[i];
    }
    return 0;
}

/** Bounded clipboard transfers never silently cut a selection short. */
static void clipboard_command(edit_state_t *st, int command)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_status_t status;
    if (command == CMD_PASTE) {
        status = tiku_codeview_paste(st->view);
        if (status == TIKU_EDIT_OK) tiku_editor_edited(st);
        else if (status != TIKU_EDIT_NOTHING && status != TIKU_EDIT_INVALID)
            tiku_editor_refused(st, status);
        return;
    }
    if (tiku_codeview_selection(st->view, &anchor, &caret) != TIKU_EDIT_OK || anchor == caret)
        return;
    tiku_edit_range_t r = {anchor < caret ? anchor : caret, anchor < caret ? caret : anchor};
    int64_t seen[2] = {0, 0};
    if (r.end - r.start > (int64_t)TIKU_CLIPBOARD_LIMIT)
        (void)tiku_editdoc_each(st->doc, r, count_crlf, seen);
    if (r.end - r.start - seen[0] > (int64_t)TIKU_CLIPBOARD_LIMIT) {
        snprintf(st->note, sizeof st->note, "Selection exceeds the desktop clipboard's %u MB limit.",
                 TIKU_CLIPBOARD_LIMIT / (1024u * 1024u));
        return;
    }
    status = command == CMD_CUT ? tiku_codeview_cut(st->view) : tiku_codeview_copy(st->view);
    if (status == TIKU_EDIT_NO_MEMORY)
        snprintf(st->note, sizeof st->note, "Not enough memory to copy the selection.");
    else if (status == TIKU_EDIT_OK && command == CMD_CUT) tiku_editor_edited(st);
}

/** Menus expose the same commands and shortcut routes as the keyboard. */
void tiku_editor_publish(edit_state_t *st)
{
    tiku_menuset_t menus = {0}; menus.nmenu = 5;
    const char *titles[] = {"File", "Edit", "Search", "View", "Text"};
    const char *labels[][14] = {
        {"New", "Open…", "Save", "Save As…", "Revert", "Close"},
        {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All"},
        {"Find…", "Find Next", "Find Previous", "Replace…", "Go to Line…", "Replace All"},
        {"Line Numbers", "Colour as BASIC", "Run", "Show Invisibles",
         "Wrap Lines", "Split Pane", "Highlight Caret Line", "Block Caret"},
        {"Indent Lines", "Unindent Lines", "Toggle Comment", "Duplicate Lines",
         "Move Lines Up", "Move Lines Down", "Delete Lines",
         "Go to Matching Bracket", "Select to Matching Bracket", "Fold",
         "Unfold", "Fold All", "Unfold All", "Carets to Line Ends"}};
    const int commands[][14] = {
        {CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_REVERT, CMD_QUIT},
        {CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_ALL},
        {CMD_FIND, CMD_NEXT, CMD_PREV, CMD_REPLACE, CMD_GOTO, CMD_REPLACE_ALL},
        {CMD_NUMBERS, CMD_SYNTAX, CMD_RUN, CMD_INVISIBLES, CMD_WRAP,
         CMD_SPLIT, CMD_CARET_LINE, CMD_BLOCK_CARET},
        {CMD_INDENT, CMD_UNINDENT, CMD_COMMENT, CMD_DUPLICATE, CMD_LINES_UP,
         CMD_LINES_DOWN, CMD_DELETE_LINES, CMD_BRACKET, CMD_SELECT_BRACKET,
         CMD_FOLD, CMD_UNFOLD, CMD_FOLD_ALL, CMD_UNFOLD_ALL, CMD_LINE_ENDS}};
    const char *keys[] = {"nos  w", "zyxcva", "f][hg ", "  r     ",
                          "  /d  kmm[]  l"};
    const int counts[] = {6, 6, 6, 8, 14};
    tiku_edit_pos_t anchor = 0, caret = 0;
    (void)tiku_codeview_selection(st->view, &anchor, &caret);
    int selected = anchor != caret;
    int field = st->finding && st->search_active && st->find_focus >= 0;
    if (field) {
        tiku_field_t *f = &st->field[st->find_focus];
        selected = f->sel_a != f->sel_b;
    }
    for (int m = 0; m < 5; m++) {
        snprintf(menus.menu[m].title, sizeof menus.menu[m].title, "%s", titles[m]);
        menus.menu[m].nitem = counts[m];
        for (int i = 0; i < counts[m]; i++) {
            int cmd = commands[m][i], enabled = !st->ask.open;
            if (cmd == CMD_UNDO) enabled &= field ?
                st->field_input[st->find_focus].nundo > 0 : tiku_editdoc_can_undo(st->doc);
            if (cmd == CMD_REDO) enabled &= field ?
                st->field_input[st->find_focus].nredo > 0 : tiku_editdoc_can_redo(st->doc);
            if (cmd == CMD_COPY || cmd == CMD_CUT) enabled &= selected && st->services->clip_set != NULL;
            if (cmd == CMD_PASTE) enabled &= st->services->clip_text != NULL;
            if (cmd == CMD_OPEN || cmd == CMD_SAVEAS) enabled &= st->services->pick != NULL;
            if (cmd == CMD_REVERT) enabled &= st->path[0] != 0;
            if (cmd == CMD_RUN) enabled &= st->lang == TIKU_SYNTAX_BASIC && !st->running;
            if (cmd == CMD_COMMENT)
                enabled &= tiku_codeview_rules(st->view)->comment != NULL;
            snprintf(menus.menu[m].item[i].label, sizeof menus.menu[m].item[i].label, "%s", labels[m][i]);
            menus.menu[m].item[i].command = cmd;
            menus.menu[m].item[i].enabled = enabled;
            menus.menu[m].item[i].sc = keys[m][i] == ' ' ? 0 : keys[m][i];
            if (cmd == CMD_DELETE_LINES || cmd == CMD_SELECT_BRACKET ||
                cmd == CMD_FOLD || cmd == CMD_UNFOLD || cmd == CMD_LINE_ENDS)
                menus.menu[m].item[i].mods = TIKU_MOD_CMD | TIKU_MOD_SHIFT;
            menus.menu[m].item[i].marked = cmd == CMD_NUMBERS ? st->numbers :
                cmd == CMD_SYNTAX ? st->lang == TIKU_SYNTAX_BASIC :
                cmd == CMD_INVISIBLES ? st->show_invisibles :
                cmd == CMD_WRAP ? st->wrap :
                cmd == CMD_CARET_LINE ? st->caret_line :
                cmd == CMD_BLOCK_CARET ? st->block_caret :
                cmd == CMD_SPLIT ? st->panes[1] != NULL : 0;
        }
    }
    if (memcmp(&menus, &st->last_menus, sizeof menus) &&
        st->services->menus(st->services->ctx, st->id, &menus) >= 0)
        st->last_menus = menus;
    if (st->search_id) st->services->menus(st->services->ctx, st->search_id, &menus);
}

/** Report what a Text command did: an edit clears the note, a refusal
 *  says why, and nothing to do says nothing. */
static void text_command(edit_state_t *st, tiku_edit_status_t status)
{
    if (status == TIKU_EDIT_OK) tiku_editor_edited(st);
    else if (status != TIKU_EDIT_NOTHING) tiku_editor_refused(st, status);
}

/** Dispatch commands once; menu actions and key bindings cannot diverge. */
int tiku_editor_pick(void *state, uint32_t window, int command)
{
    edit_state_t *st = state;
    if (window == st->id || (window && window == st->search_id))
        st->search_active = window == st->search_id;
    if (st->ask.open) return 0;
    if (st->finding && st->search_active && st->find_focus >= 0 && command >= CMD_UNDO && command <= CMD_ALL) {
        /* A field redoes on Cmd+Shift+Z, so Redo is sent as that chord. */
        static const unsigned keys[] = {'z', 'z', 'x', 'c', 'v', 'a'};
        unsigned stamp = tiku_clipboard_stamp();
        if (command == CMD_PASTE && st->services->clip_text) {
            tiku_clipboard_set(st->services->clip_text(st->services->ctx, NULL));
            stamp = tiku_clipboard_stamp();
        }
        tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = keys[command - CMD_UNDO],
            .modifiers = TIKU_MOD_CMD | (command == CMD_REDO ? TIKU_MOD_SHIFT : 0)};
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
    case CMD_UNDO: (void)tiku_codeview_undo(st->view); st->note[0] = 0; break;
    case CMD_REDO: (void)tiku_codeview_redo(st->view); st->note[0] = 0; break;
    case CMD_CUT: case CMD_COPY: case CMD_PASTE: clipboard_command(st, command); break;
    case CMD_ALL: (void)tiku_codeview_select_all(st->view); break;
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
    case CMD_WRAP: st->wrap = !st->wrap; break;
    case CMD_CARET_LINE: st->caret_line = !st->caret_line; break;
    case CMD_BLOCK_CARET: st->block_caret = !st->block_caret; break;
    case CMD_SPLIT:
        if (tiku_editor_split(st))
            snprintf(st->note, sizeof st->note, "Not enough memory to split the page.");
        break;
    case CMD_SYNTAX:
        st->lang = st->lang == TIKU_SYNTAX_BASIC ? TIKU_SYNTAX_NONE : TIKU_SYNTAX_BASIC;
        tiku_codeview_set_language(st->view, st->lang);
        break;
    case CMD_RUN: if (st->lang == TIKU_SYNTAX_BASIC) tiku_editor_run_program(st); break;
    case CMD_INDENT: case CMD_UNINDENT:
        text_command(st, tiku_codeview_indent(st->view,
                                              command == CMD_UNINDENT));
        break;
    case CMD_COMMENT:
        text_command(st, tiku_codeview_toggle_comment(st->view)); break;
    case CMD_DUPLICATE:
        text_command(st, tiku_codeview_duplicate_lines(st->view)); break;
    case CMD_LINES_UP: case CMD_LINES_DOWN:
        text_command(st, tiku_codeview_move_lines(st->view,
                                                  command == CMD_LINES_DOWN));
        break;
    case CMD_DELETE_LINES:
        text_command(st, tiku_codeview_delete_lines(st->view)); break;
    case CMD_WORD_BACK: case CMD_WORD_ON:
        text_command(st, tiku_codeview_delete_word(st->view,
                                                   command == CMD_WORD_ON));
        break;
    case CMD_FOLD: case CMD_UNFOLD:
        (void)tiku_codeview_fold_at(st->view, command == CMD_UNFOLD); break;
    case CMD_FOLD_ALL: case CMD_UNFOLD_ALL:
        (void)tiku_codeview_fold_all(st->view, command == CMD_UNFOLD_ALL);
        break;
    case CMD_BRACKET: case CMD_SELECT_BRACKET:
        (void)tiku_codeview_goto_bracket(st->view,
                                         command == CMD_SELECT_BRACKET);
        break;
    case CMD_LINE_ENDS:
        if (tiku_codeview_carets_to_line_ends(st->view) == TIKU_EDIT_NO_MEMORY)
            snprintf(st->note, sizeof st->note, "Not enough memory for a caret on each line.");
        break;
    default: return 0;
    }
    tiku_editor_reveal(st); tiku_editor_publish(st); tiku_editor_paint(st); return 0;
}

/** File-panel answers never replace a document edited while the panel was open. */
void tiku_editor_picked(void *state, uint32_t window, const char *path)
{
    edit_state_t *st = state; (void)window;
    if (st->ask.open) { /* the question's answer runs the pending action */
        if (path && *path)
            snprintf(st->note, sizeof st->note,
                     "%s cancelled: answer the question first.",
                     st->pick_save ? "Save" : "Open");
        tiku_editor_paint(st); return;
    }
    if (!path || !*path) { st->pending = 0; return; }
    if (st->pick_save) {
        int pending = st->pending; st->pending = 0;
        if (tiku_editor_save_document(st, path) && pending && tiku_editor_document_action(st, pending)) {
            if (st->services->close) st->services->close(st->services->ctx, st->id);
            return;
        }
    } else if (tiku_editor_modified(st) && !st->allow_open) {
        snprintf(st->note, sizeof st->note, "Open cancelled: the document has unsaved edits.");
    } else tiku_editor_read_document(st, path);
    st->allow_open = 0;
    tiku_editor_publish(st); tiku_editor_paint(st);
}
