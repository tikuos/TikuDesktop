/* tiku_editor_commands.c -- the Editor's commands and menus: Find and
 * Replace, the clipboard, the Language and Text menus, and New, Open,
 * Revert and Close, which wait on the unsaved-changes question.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static tiku_edit_query_t query(const edit_state_t *st);
static int count_crlf(void *ctx, const char *bytes, size_t n);
static void clipboard_command(edit_state_t *st, int command);

/** Whether the text differs from what was last loaded or saved. */
int tiku_editor_modified(const edit_state_t *st)
{ return tiku_editdoc_modified(st->doc); }

/** Note a successful edit: clear the footer message and drop the actions
 *  that waited on the text before it, the pending command and an Open
 *  that Discard allowed. */
void tiku_editor_edited(edit_state_t *st)
{ st->note[0] = 0; st->pending = 0; st->allow_open = 0; }

/** Say why an edit changed nothing. */
void tiku_editor_refused(edit_state_t *st, tiku_edit_status_t status)
{
    snprintf(st->note, sizeof st->note, "%s", status == TIKU_EDIT_LIMIT
        ? "Cannot apply edit: the document would pass its size limit. Text kept."
        : status == TIKU_EDIT_READ_ONLY
        ? "This document is read-only. Text kept."
        : status == TIKU_EDIT_BAD_PATTERN
        ? "Invalid regular expression. Text kept."
        : status == TIKU_EDIT_BAD_REPLACEMENT
        ? "Invalid replacement: use $1, ${2}, $$ or \\n, \\t, \\\\. "
          "Text kept."
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

/** The Find field's text under the window's Match case, Whole words and
 *  Regular expression. */
static tiku_edit_query_t query(const edit_state_t *st)
{
    tiku_edit_query_t q = {st->field[0].text, strlen(st->field[0].text),
        (st->match_case ? TIKU_EDIT_MATCH_CASE : 0u) |
        (st->whole_word ? TIKU_EDIT_WHOLE_WORD : 0u) |
        (st->regex ? TIKU_EDIT_REGEX : 0u)};
    return q;
}

/** Select the next match, or with @p backward the previous one, wrapping
 *  once through the document. */
void tiku_editor_find_match(edit_state_t *st, int backward)
{
    tiku_edit_query_t q = query(st);
    tiku_codeview_found_t found;
    if (!q.length) { snprintf(st->note, sizeof st->note, "Enter text to find."); return; }
    tiku_edit_status_t status = tiku_codeview_find(st->view, &q, backward,
                                                   &found);
    if (status != TIKU_EDIT_OK && status != TIKU_EDIT_NOTHING) {
        tiku_editor_refused(st, status);
        return;
    }
    if (status != TIKU_EDIT_OK) {
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
    if (status == TIKU_EDIT_BAD_PATTERN ||
        status == TIKU_EDIT_BAD_REPLACEMENT) {
        tiku_editor_refused(st, status);
        return;
    }
    if (status != TIKU_EDIT_OK) {
        snprintf(st->note, sizeof st->note,
                 "Replace All exceeds available memory or the document's size limit. Text kept.");
        return;
    }
    tiku_editor_edited(st);
    snprintf(st->note, sizeof st->note, "%zu replacements. Undo restores the original text.",
             found.replaced);
}

/** Ask the desktop's file panel for a file to open, or with @p saving for
 *  a name to save as, starting in the document's folder; with no panel
 *  the note says so. */
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

/** Run New, Open, Revert or Close (CMD_QUIT).  @return 1 when the window
 *  is to close.
 *  @note Call it through tiku_editor_guarded_action() unless the changes
 *        were saved or discarded. */
int tiku_editor_document_action(edit_state_t *st, int command)
{
    st->pending = 0;
    if (command == CMD_QUIT) {
        tiku_editor_search_close(st);
        tiku_editor_preferences_close(st);
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
        st->language_mode = EDIT_LANG_AUTO;
        tiku_codeview_set_language(st->view, st->lang);
    }
    return 0;
}

/** Run New, Open, Revert or Close at once when nothing is unsaved;
 *  otherwise ask Cancel, Discard or Save, Cancel the default, and keep
 *  the command pending.  @return 1 when the window is to close. */
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

/** Cut, copy or paste through the view.  A selection over the
 *  clipboard's limit, a CRLF counted as one byte, is not copied, and the
 *  note says so. */
static void clipboard_command(edit_state_t *st, int command)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_status_t status;
    if (command == CMD_PASTE) {
        status = tiku_codeview_execute(st->view, TIKU_CODEVIEW_CMD_PASTE, 0, 0);
        if (status == TIKU_EDIT_OK) tiku_editor_edited(st);
        else if (status != TIKU_EDIT_NOTHING && status != TIKU_EDIT_INVALID)
            tiku_editor_refused(st, status);
        return;
    }
    if (!tiku_codeview_has_selection(st->view) ||
        tiku_codeview_selection(st->view, &anchor, &caret) != TIKU_EDIT_OK)
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
    status = tiku_codeview_execute(st->view, command == CMD_CUT ?
        TIKU_CODEVIEW_CMD_CUT : TIKU_CODEVIEW_CMD_COPY, 0, 0);
    if (status == TIKU_EDIT_NO_MEMORY)
        snprintf(st->note, sizeof st->note, "Not enough memory to copy the selection.");
    else if (status == TIKU_EDIT_LIMIT)
        snprintf(st->note, sizeof st->note, "%s",
                 "Clipboard refused the selected text. Nothing was removed.");
    else if (status == TIKU_EDIT_OK && command == CMD_CUT) tiku_editor_edited(st);
}

/** Whether @p view's main selection holds text: what case conversion
 *  acts on, whatever the other selections hold. */
static int main_selected(const tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor = 0, caret = 0;

    return tiku_codeview_selection(view, &anchor, &caret) == TIKU_EDIT_OK &&
           anchor != caret;
}

/** Whether menu command @p cmd can run now.  @p field says a Find window
 *  field has the keys, and @p selected whether the text with the keys has
 *  a selection. */
static int command_enabled(const edit_state_t *st, int cmd, int field,
                           int selected)
{
    const tiku_codeview_t *v = st->view;
    int writable = !tiku_editdoc_read_only(st->doc);

    if (st->ask.open) {
        return 0;
    }
    switch (cmd) {
    case CMD_UNDO:
        return field ? st->field_input[st->find_focus].nundo > 0
               : tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_UNDO);
    case CMD_REDO:
        return field ? st->field_input[st->find_focus].nredo > 0
               : tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_REDO);
    case CMD_COPY:
        return selected && st->services->clip_set != NULL;
    case CMD_CUT:
        return selected && st->services->clip_set != NULL &&
               (field ||
                tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_CUT));
    case CMD_PASTE:
        return st->services->clip_text != NULL &&
               (field ||
                tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_PASTE));
    case CMD_OPEN: case CMD_SAVEAS:
        return st->services->pick != NULL;
    case CMD_REVERT:
        return st->path[0] != 0;
    case CMD_RUN:
        return st->lang == TIKU_SYNTAX_BASIC && !st->running;
    case CMD_COMMENT:
        return tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_COMMENT);
    case CMD_ZOOM_IN:
        return tiku_codeview_zoom(v) < EDIT_ZOOM_MAX;
    case CMD_ZOOM_OUT:
        return tiku_codeview_zoom(v) > EDIT_ZOOM_MIN;
    case CMD_ZOOM_RESET:
        return tiku_codeview_zoom(v) != 0;
    case CMD_UPPER: case CMD_LOWER:
        return writable && main_selected(v);
    case CMD_TRIM: case CMD_SPACES: case CMD_TABS: case CMD_SORT:
        return writable;
    case CMD_SELECT_MATCHES:
        return st->field[0].text[0] != 0;
    default:
        return 1;
    }
}

/** Whether menu row @p cmd wears a mark: a View switch that is on, Split
 *  Pane while split, or the Language menu's choice. */
static int command_marked(const edit_state_t *st, int cmd)
{
    switch (cmd) {
    case CMD_NUMBERS: return st->numbers;
    case CMD_INVISIBLES: return st->show_invisibles;
    case CMD_WRAP: return st->wrap;
    case CMD_CARET_LINE: return st->caret_line;
    case CMD_BLOCK_CARET: return st->block_caret;
    case CMD_INDENT_GUIDES: return st->indent_guides;
    case CMD_RULER: return st->ruler;
    case CMD_SPLIT: return st->panes[1] != NULL;
    default:
        return cmd >= CMD_LANG_AUTO && cmd <= CMD_LANG_JSON &&
               st->language_mode == cmd - CMD_LANG_AUTO;
    }
}

/** Publish the File, Edit, Search, View, Text and Language menus, with
 *  their shortcuts, enabled rows and marks, when they changed; the Find
 *  window gets them too. */
void tiku_editor_publish(edit_state_t *st)
{
    static const char *const titles[] = {"File", "Edit", "Search", "View",
                                         "Text", "Language"};
    static const char *const labels[][20] = {
        {"New", "Open…", "Save", "Save As…", "Revert", "Close"},
        {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All"},
        {"Find…", "Find Next", "Find Previous", "Replace…", "Go to Line…",
         "Replace All", "Select All Matches"},
        {"Line Numbers", "Preferences…", "Run", "Show Invisibles",
         "Wrap Lines", "Split Pane", "Highlight Caret Line", "Block Caret",
         "Indentation Guides", "80-column Ruler", "Zoom In", "Zoom Out",
         "Reset Zoom"},
        {"Indent Lines", "Unindent Lines", "Toggle Comment", "Duplicate Lines",
         "Move Lines Up", "Move Lines Down", "Delete Lines",
         "Go to Matching Bracket", "Select to Matching Bracket", "Fold",
         "Unfold", "Fold All", "Unfold All", "Carets to Line Ends",
         "Trim Trailing Whitespace", "Indentation to Spaces",
         "Indentation to Tabs", "Uppercase (ASCII)", "Lowercase (ASCII)",
         "Sort Lines"},
        {"Automatic", "Plain Text", "BASIC", "C", "JSON"}};
    static const int commands[][20] = {
        {CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_REVERT, CMD_QUIT},
        {CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_ALL},
        {CMD_FIND, CMD_NEXT, CMD_PREV, CMD_REPLACE, CMD_GOTO,
         CMD_REPLACE_ALL, CMD_SELECT_MATCHES},
        {CMD_NUMBERS, CMD_PREFS, CMD_RUN, CMD_INVISIBLES, CMD_WRAP,
         CMD_SPLIT, CMD_CARET_LINE, CMD_BLOCK_CARET, CMD_INDENT_GUIDES,
         CMD_RULER, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_RESET},
        {CMD_INDENT, CMD_UNINDENT, CMD_COMMENT, CMD_DUPLICATE, CMD_LINES_UP,
         CMD_LINES_DOWN, CMD_DELETE_LINES, CMD_BRACKET, CMD_SELECT_BRACKET,
         CMD_FOLD, CMD_UNFOLD, CMD_FOLD_ALL, CMD_UNFOLD_ALL, CMD_LINE_ENDS,
         CMD_TRIM, CMD_SPACES, CMD_TABS, CMD_UPPER, CMD_LOWER, CMD_SORT},
        {CMD_LANG_AUTO, CMD_LANG_TEXT, CMD_LANG_BASIC, CMD_LANG_C,
         CMD_LANG_JSON}};
    static const char *const keys[] = {"nos  w", "zyxcva", "f][hg  ",
                                       " ,r       =-0",
                                       "  /d  kmm[]  l      ", "     "};
    static const int counts[] = {6, 6, 7, 13, 20, 5};
    tiku_menuset_t menus = {0};
    int field = st->finding && st->search_active && st->find_focus >= 0;
    int selected = tiku_codeview_has_selection(st->view);
    if (field) {
        tiku_field_t *f = &st->field[st->find_focus];
        selected = f->sel_a != f->sel_b;
    }
    menus.nmenu = (int)(sizeof counts / sizeof counts[0]);
    for (int m = 0; m < menus.nmenu; m++) {
        snprintf(menus.menu[m].title, sizeof menus.menu[m].title, "%s",
                 titles[m]);
        menus.menu[m].nitem = counts[m];
        for (int i = 0; i < counts[m]; i++) {
            tiku_menu_entry_t *item = &menus.menu[m].item[i];
            int cmd = commands[m][i];
            if (cmd == CMD_RULER)
                snprintf(item->label, sizeof item->label, "%d-column Ruler",
                         st->preferences.column);
            else
                snprintf(item->label, sizeof item->label, "%s", labels[m][i]);
            item->command = cmd;
            item->enabled = command_enabled(st, cmd, field, selected);
            item->sc = keys[m][i] == ' ' ? 0 : keys[m][i];
            if (cmd == CMD_DELETE_LINES || cmd == CMD_SELECT_BRACKET ||
                cmd == CMD_FOLD || cmd == CMD_UNFOLD || cmd == CMD_LINE_ENDS)
                item->mods = TIKU_MOD_CMD | TIKU_MOD_SHIFT;
            item->marked = command_marked(st, cmd);
        }
    }
    if (memcmp(&menus, &st->last_menus, sizeof menus) &&
        st->services->menus(st->services->ctx, st->id, &menus) >= 0)
        st->last_menus = menus;
    if (st->search_id)
        st->services->menus(st->services->ctx, st->search_id, &menus);
    st->menus_due = 0;
}

/** A jump target followed: the caret goes to the start of the line it
 *  names, a BASIC line number or a label heading a line, in the pane it
 *  was clicked in; a target no line has is said in the footer. */
void tiku_editor_follow(edit_state_t *st, tiku_codeview_t *view,
                        tiku_edit_range_t target)
{
    char name[64], head[96];
    size_t n = 0, got = 0;
    int64_t lines = tiku_editdoc_lines(st->doc);
    if (target.end - target.start >= (int64_t)sizeof name ||
        tiku_editdoc_read(st->doc, target, name, sizeof name, &n) !=
            TIKU_EDIT_OK)
        return;
    for (int64_t line = 0; line < lines; line++) {
        tiku_edit_range_t r = {tiku_editdoc_line_start(st->doc, line),
                               tiku_editdoc_line_end(st->doc, line)};
        if (r.end - r.start > (int64_t)sizeof head - 1)
            r.end = r.start + (int64_t)sizeof head - 1;
        if (tiku_editdoc_read(st->doc, r, head, sizeof head, &got) ==
                TIKU_EDIT_OK && tiku_syntax_basic_names(head, name, n)) {
            (void)tiku_codeview_select(view, r.start, r.start);
            tiku_codeview_reveal(view);
            st->note[0] = 0;
            return;
        }
    }
    snprintf(st->note, sizeof st->note, "No line %s in this program.", name);
}

/** Report what a Text command did: an edit clears the note, a refusal
 *  says why, and nothing to do says nothing. */
static void text_command(edit_state_t *st, tiku_edit_status_t status)
{
    if (status == TIKU_EDIT_OK) tiku_editor_edited(st);
    else if (status != TIKU_EDIT_NOTHING) tiku_editor_refused(st, status);
}

/** Run code view command @p command, reported as a Text command. */
static void view_command(edit_state_t *st, tiku_codeview_command_t command)
{
    text_command(st, tiku_codeview_execute(st->view, command, 0, 0));
}

/** Take Language menu choice @p mode: Automatic goes by the file's name,
 *  BASIC and C colour as those languages, and Plain Text and JSON as no
 *  built-in one, JSON through the view's JSON lexer. */
static void language_choice(edit_state_t *st, int mode)
{
    st->language_mode = mode;
    st->lang = mode == EDIT_LANG_BASIC ? TIKU_SYNTAX_BASIC
             : mode == EDIT_LANG_C ? TIKU_SYNTAX_C
             : mode == EDIT_LANG_AUTO ? tiku_syntax_of_path(st->path)
             : TIKU_SYNTAX_NONE;
}

/** Run menu command @p command, picked in @p window or sent by a key.
 *  With a Find window field focused, the editing commands go to the
 *  field.  @return 1 when the window is to close. */
int tiku_editor_pick(void *state, uint32_t window, int command)
{
    edit_state_t *st = state;
    st->context_open = 0;
    if (window && window == st->prefs_id) return 0;
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
    case CMD_PREFS: tiku_editor_preferences_open(st); break;
    case CMD_LANG_AUTO: case CMD_LANG_TEXT: case CMD_LANG_BASIC:
    case CMD_LANG_C: case CMD_LANG_JSON:
        language_choice(st, command - CMD_LANG_AUTO);
        break;
    case CMD_TRIM: case CMD_SPACES: case CMD_TABS: case CMD_UPPER:
    case CMD_LOWER: case CMD_SORT:
        text_command(st, tiku_codeview_transform(st->view,
            (tiku_codeview_transform_t)(command - CMD_TRIM)));
        break;
    case CMD_SELECT_MATCHES: tiku_editor_select_matches(st); break;
    case CMD_NEW: case CMD_OPEN: case CMD_REVERT: case CMD_QUIT:
        if (tiku_editor_guarded_action(st, command)) return 1;
        break;
    case CMD_SAVE:
        if (!st->path[0]) tiku_editor_file_panel(st, 1); else tiku_editor_save(st);
        break;
    case CMD_SAVEAS: tiku_editor_file_panel(st, 1); break;
    case CMD_UNDO: case CMD_REDO:
        (void)tiku_codeview_execute(st->view, command == CMD_UNDO ?
            TIKU_CODEVIEW_CMD_UNDO : TIKU_CODEVIEW_CMD_REDO, 0, 0);
        st->note[0] = 0;
        break;
    case CMD_CUT: case CMD_COPY: case CMD_PASTE: clipboard_command(st, command); break;
    case CMD_ALL:
        (void)tiku_codeview_execute(st->view, TIKU_CODEVIEW_CMD_SELECT_ALL,
                                    0, 0);
        break;
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
    case CMD_INDENT_GUIDES: st->indent_guides = !st->indent_guides; break;
    case CMD_RULER: st->ruler = !st->ruler; break;
    case CMD_ZOOM_IN: case CMD_ZOOM_OUT: case CMD_ZOOM_RESET:
        (void)tiku_codeview_set_zoom(st->view, command == CMD_ZOOM_RESET ? 0 :
            tiku_codeview_zoom(st->view) + (command == CMD_ZOOM_IN ? 1 : -1));
        break;
    case CMD_SPLIT:
        if (tiku_editor_split(st))
            snprintf(st->note, sizeof st->note, "Not enough memory to split the page.");
        break;
    case CMD_SYNTAX:
        st->lang = st->lang == TIKU_SYNTAX_BASIC ? TIKU_SYNTAX_NONE : TIKU_SYNTAX_BASIC;
        st->language_mode = st->lang == TIKU_SYNTAX_BASIC ? EDIT_LANG_BASIC
                                                          : EDIT_LANG_TEXT;
        tiku_codeview_set_language(st->view, st->lang);
        break;
    case CMD_RUN: if (st->lang == TIKU_SYNTAX_BASIC) tiku_editor_run_program(st); break;
    case CMD_INDENT: case CMD_UNINDENT:
        view_command(st, command == CMD_UNINDENT ? TIKU_CODEVIEW_CMD_UNINDENT
                                                 : TIKU_CODEVIEW_CMD_INDENT);
        break;
    case CMD_COMMENT: view_command(st, TIKU_CODEVIEW_CMD_COMMENT); break;
    case CMD_DUPLICATE: view_command(st, TIKU_CODEVIEW_CMD_DUPLICATE); break;
    case CMD_LINES_UP: case CMD_LINES_DOWN:
        text_command(st, tiku_codeview_move_lines(st->view,
                                                  command == CMD_LINES_DOWN));
        break;
    case CMD_DELETE_LINES:
        view_command(st, TIKU_CODEVIEW_CMD_DELETE_LINES);
        break;
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

/** Take the file panel's answer: save to the path, then run a pending
 *  command; or open the path, unless the document holds unsaved edits
 *  that Discard did not allow. */
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
