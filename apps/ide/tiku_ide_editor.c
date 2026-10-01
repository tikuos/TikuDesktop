/* tiku_ide_editor.c -- source-file windows, clipboard and file lifecycle.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static void
msep(tiku_menu_list_t *m);

/** @brief The editor holding @p path, or NULL. */
edit_t *
tiku_ide_edit_of_path(ide_t *st, const char *path)
{
    int i;
    char *absolute = realpath(path, NULL);
    const char *want = absolute ? absolute : path;

    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL &&
            (strcmp(st->edit[i].path, want) == 0 ||
             strcmp(st->edit[i].path, path) == 0)) {
            free(absolute);
            return &st->edit[i];
        }
    }
    free(absolute);
    return NULL;
}

/** @brief The editor in window @p id, or NULL. */
edit_t *
tiku_ide_edit_of_id(ide_t *st, uint32_t id)
{
    int i;

    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL && st->edit[i].id == id) {
            return &st->edit[i];
        }
    }
    return NULL;
}

/**
 * @brief Cut, copy or paste at this editor, over the DESK's clipboard.
 *
 * Copy puts the selection on the desk; cut is a copy and then the one
 * backspace that removes a held selection; paste types what the desk
 * holds, replacing a selection the way any typing does.  All three
 * through the services, so the same page behaves the same hosted in
 * the shell or across the wire.
 */
int
tiku_ide_edit_clip(ide_t *st, edit_t *e, int command)
{
    size_t n = tiku_textview_selected_size(&e->tv);
    const char *held;
    char *text;

    switch (command) {
    case CMD_COPY:
    case CMD_CUT:
        if (n == 0u) {
            return 0;           /* nothing held: nothing to take */
        }
        if (n > TIKU_CLIPBOARD_LIMIT || (text = malloc(n + 1u)) == NULL) {
            snprintf(e->feedback, sizeof e->feedback,
                     "The selection is more than the clipboard holds.");
            return 0;
        }
        (void)tiku_textview_selected(&e->tv, text, n + 1u);
        if (st->services->clip_set != NULL) {
            (void)st->services->clip_set(st->services->ctx, text);
        }
        free(text);
        if (command == CMD_CUT) {
            tiku_textview_backspace(&e->tv);
        }
        return 1;
    case CMD_PASTE:
        held = st->services->clip_text != NULL
               ? st->services->clip_text(st->services->ctx, NULL) : "";
        if (held[0] == '\0') {
            return 0;
        }
        tiku_textview_insert_text(&e->tv, held);
        return 1;
    case CMD_SELALL:
        tiku_textview_select_all(&e->tv);
        return 1;
    case CMD_UNDO:
        e->feedback[0] = '\0';
        return tiku_textview_undo(&e->tv);
    case CMD_REDO:
        e->feedback[0] = '\0';
        return tiku_textview_redo(&e->tv);
    default:
        return 0;
    }
}

/** @brief Append one row: the label, its command, and the KEY it says. */
void
tiku_ide_mrow(tiku_menu_list_t *m, const char *label, int command,
     char sc, unsigned mods, int enabled)
{
    tiku_menu_entry_t *it = &m->item[m->nitem];

    if (m->nitem >= TIKU_MENUSET_ITEMS) {
        return;
    }
    memset(it, 0, sizeof *it);
    snprintf(it->label, sizeof it->label, "%s", label);
    it->command = command;
    it->sc = sc;
    it->mods = mods;
    it->enabled = (unsigned char)enabled;
    m->nitem++;
}

/** @brief A line between the groups, the way the desk's menus keep them. */
static void
msep(tiku_menu_list_t *m)
{
    if (m->nitem >= TIKU_MENUSET_ITEMS) {
        return;
    }
    memset(&m->item[m->nitem], 0, sizeof m->item[0]);
    m->item[m->nitem].separator = 1;
    m->nitem++;
}

/**
 * @brief The Window menu, the same on EVERY window of the program.
 *
 * A person in one editor reaches the other from where they are; making
 * them return to the project window first was a detour every proving
 * script had memorised, which is how a missing road shows itself.
 */
void
tiku_ide_window_menu(ide_t *st, tiku_menu_list_t *m)
{
    int i, n = 0;
    char row[64];

    snprintf(m->title, sizeof m->title, "Window");
    for (i = 0; i < IDE_EDITS; i++) {
        edit_t *e = &st->edit[i];

        if (e->surface == NULL) {
            continue;
        }
        /* The mark is the DOCUMENT's, not the window's: a person
         * looking for where their unsaved work is should find it in the
         * list of windows rather than by opening each one. */
        snprintf(row, sizeof row, "%s%s", e->name,
                 tiku_textview_modified(&e->tv) ? " (changed)" : "");
        tiku_ide_mrow(m, row, CMD_WINDOW + i, 0, 0u, 1);
        n++;
    }
    if (n == 0) {
        tiku_ide_mrow(m, "No open files", 0, 0, 0u, 0);
    }
}

void
tiku_ide_edit_publish(ide_t *st, edit_t *e)
{
    tiku_menuset_t menus;
    char row[64];

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 3;
    /* File: the page's own file, its run, and its leaving -- each row
     * saying the key that is the same gesture. */
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    snprintf(row, sizeof row, "Save %s", e->name);
    tiku_ide_mrow(&menus.menu[0], row, CMD_SAVE, 's', TIKU_MOD_CMD,
         tiku_textview_modified(&e->tv));
    if (e->lang == TIKU_SYNTAX_BASIC) {
        char dest[64];

        snprintf(row, sizeof row, "Run %s", e->name);
        if (tiku_ide_dest_board(st, dest, sizeof dest)) {
            snprintf(row, sizeof row, "Run on %.40s", dest);
        }
        tiku_ide_mrow(&menus.menu[0], row, CMD_RUN, 'r', TIKU_MOD_CMD,
             !st->running);
        tiku_ide_mrow(&menus.menu[0], "Stop", CMD_STOP, '.', TIKU_MOD_CMD,
             st->running);
    }
    msep(&menus.menu[0]);
    snprintf(row, sizeof row, "Close %s", e->name);
    tiku_ide_mrow(&menus.menu[0], row, CMD_QUIT, 'w', TIKU_MOD_CMD, 1);
    /* Edit: the desk's editing rows, grouped, each wearing its key. */
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "Edit");
    tiku_ide_mrow(&menus.menu[1], "Undo", CMD_UNDO, 'z', TIKU_MOD_CMD,
         tiku_textview_can_undo(&e->tv));
    tiku_ide_mrow(&menus.menu[1], "Redo", CMD_REDO, 'z',
         TIKU_MOD_CMD | TIKU_MOD_SHIFT, tiku_textview_can_redo(&e->tv));
    msep(&menus.menu[1]);
    tiku_ide_mrow(&menus.menu[1], "Cut", CMD_CUT, 'x', TIKU_MOD_CMD, 1);
    tiku_ide_mrow(&menus.menu[1], "Copy", CMD_COPY, 'c', TIKU_MOD_CMD, 1);
    tiku_ide_mrow(&menus.menu[1], "Paste", CMD_PASTE, 'v', TIKU_MOD_CMD, 1);
    tiku_ide_mrow(&menus.menu[1], "Select all", CMD_SELALL, 'a', TIKU_MOD_CMD, 1);
    msep(&menus.menu[1]);
    tiku_ide_mrow(&menus.menu[1], "Find…", CMD_FIND, 'f', TIKU_MOD_CMD, 1);
    tiku_ide_mrow(&menus.menu[1], "Find and Replace…", CMD_REPLACE, 'h',
         TIKU_MOD_CMD, 1);
    tiku_ide_mrow(&menus.menu[1], "Find in project", CMD_PROJECT_FIND, 0, 0u,
         st->proj.nrow > 0 && !st->running);
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* The tidy that renumbers by tens, for the language whose
         * lines wear their numbers. */
        tiku_ide_mrow(&menus.menu[1], "Renumber", CMD_RENUM, 0, 0u, 1);
        tiku_ide_mrow(&menus.menu[1], "Complete keyword", CMD_COMPLETE, ' ', TIKU_MOD_CMD, !tiku_ide_debug_locked(st, e));
        tiku_ide_mrow(&menus.menu[1], "Keyword help", CMD_HELPWORD, 'k', TIKU_MOD_CMD, 1);
        menus.nmenu = 4;
        snprintf(menus.menu[3].title, sizeof menus.menu[3].title, "Debug");
        tiku_ide_mrow(&menus.menu[3], "Start debugging", CMD_DEBUG, 'd', TIKU_MOD_CMD, !st->running);
        tiku_ide_mrow(&menus.menu[3], "Toggle breakpoint at caret", CMD_BREAKPOINT, 'b', TIKU_MOD_CMD, !st->running);
        tiku_ide_mrow(&menus.menu[3], "Clear breakpoints", CMD_CLEARBREAKS, 0, 0u, !st->running);
        tiku_ide_mrow(&menus.menu[3], "Step line", CMD_DEBUG_STEP, 'j', TIKU_MOD_CMD, tiku_ide_debug_locked(st, e) && st->debug_paused);
        tiku_ide_mrow(&menus.menu[3], "Continue", CMD_DEBUG_CONT, 'g', TIKU_MOD_CMD, tiku_ide_debug_locked(st, e) && st->debug_paused);
        tiku_ide_mrow(&menus.menu[3], "Stop", CMD_STOP, '.', TIKU_MOD_CMD, tiku_ide_debug_locked(st, e));
        tiku_ide_mrow(&menus.menu[3], "Show debugger / watches", CMD_DEBUG_PANEL, 0, 0u, 1);
    }
    tiku_ide_window_menu(st, &menus.menu[2]);
    (void)st->services->menus(st->services->ctx, e->id, &menus);
}

/** @brief Read @p e's file in.  A missing file is an empty one. */
void
tiku_ide_edit_load(edit_t *e)
{
    size_t limit = TIKU_TEXTVIEW_LINES_MAX * TIKU_TEXTVIEW_LINE_MAX;
    char *whole = malloc(limit + 1);
    FILE *f = fopen(e->path, "r");
    size_t got = 0;
    int ok = whole != NULL;

    tiku_textview_init(&e->tv);
    e->disk_known = e->disk_changed = 0;
    if (f == NULL) {
        tiku_textview_set(&e->tv, "");
        if (errno != ENOENT) { e->load_failed = 1; }
        free(whole);
        return;
    }
    e->disk_known = fstat(fileno(f), &e->disk) == 0;
    if (whole) {
        got = fread(whole, 1, limit, f);
        whole[got] = '\0';
        ok = !ferror(f) && got < limit && !memchr(whole, '\0', got) &&
             tiku_ide_document_fits(whole);
    }
    if (fclose(f) != 0) { ok = 0; }
    if (ok) { tiku_textview_set(&e->tv, whole); }
    else {
        e->load_failed = 1;
        snprintf(e->feedback, sizeof e->feedback,
                 "File could not be loaded safely; saving and running are blocked");
    }
    free(whole);
}

int
tiku_ide_edit_save(edit_t *e)
{
    FILE *f;
    int i, fd, ok = 1;
    char temp[560];
    struct stat sb;
    mode_t mode = 0600;

    e->disk_changed = 0;
    if (e->load_failed) { return 0; }
    if (lstat(e->path, &sb) == 0) {
        if (!S_ISREG(sb.st_mode)) {
            snprintf(e->feedback, sizeof e->feedback, "Save failed: not a regular file");
            return 0;
        }
        /* Replacing the file would leave its other names on the old text. */
        if (sb.st_nlink > 1) {
            snprintf(e->feedback, sizeof e->feedback,
                     "Save refused: the file has other hard links");
            return 0;
        }
        if (!(sb.st_mode & 0222) || access(e->path, W_OK) != 0) {
            snprintf(e->feedback, sizeof e->feedback, "Save refused: the file is read-only");
            return 0;
        }
        mode = sb.st_mode & 0777;
    }
    if (e->disk_known &&
        (lstat(e->path, &sb) != 0 || sb.st_dev != e->disk.st_dev ||
         sb.st_ino != e->disk.st_ino || sb.st_size != e->disk.st_size ||
         sb.st_mtim.tv_sec != e->disk.st_mtim.tv_sec ||
         sb.st_mtim.tv_nsec != e->disk.st_mtim.tv_nsec ||
         sb.st_ctim.tv_sec != e->disk.st_ctim.tv_sec ||
         sb.st_ctim.tv_nsec != e->disk.st_ctim.tv_nsec)) {
        e->disk_changed = 1;
        snprintf(e->feedback, sizeof e->feedback, "Not saved: the file changed on disk");
        return 0;
    }
    snprintf(temp, sizeof temp, "%s.XXXXXX", e->path);
    fd = mkstemp(temp);
    f = fd < 0 ? NULL : fdopen(fd, "w");
    if (f == NULL) {
        if (fd >= 0) { close(fd); unlink(temp); }
        snprintf(e->feedback, sizeof e->feedback, "Save failed: %s", strerror(errno));
        return 0;
    }
    for (i = 0; i < tiku_textview_lines(&e->tv); i++) {
        if (fprintf(f, "%s\n", tiku_textview_line(&e->tv, i)) < 0) { ok = 0; }
    }
    if (fchmod(fd, mode) != 0 || fflush(f) != 0 || fsync(fd) != 0) { ok = 0; }
    if (fclose(f) != 0) { ok = 0; }
    if (!ok || rename(temp, e->path) != 0) {
        unlink(temp);
        snprintf(e->feedback, sizeof e->feedback, "Save failed; unsaved work retained");
        return 0;
    }
    e->disk_known = stat(e->path, &e->disk) == 0;
    tiku_textview_saved(&e->tv);
    (void)tiku_ide_recovery_write(e);
    snprintf(e->feedback, sizeof e->feedback, "Saved");
    return 1;
}

/**
 * @brief Save @p e as a person asked to, which is tiku_ide_edit_save()
 *        except that a file changed on disk since the page read or saved
 *        it is not refused outright: the Project window asks whether to
 *        save the page over it, Cancel being the default.
 *
 * @return 1 when the page was saved.
 */
int
tiku_ide_edit_save_or_ask(ide_t *st, edit_t *e)
{
    char text[256];

    if (tiku_ide_edit_save(e)) {
        return 1;
    }
    if (!e->disk_changed) {
        return 0;
    }
    snprintf(text, sizeof text,
             "%s changed on disk since this page read it. Save this page over it?",
             e->name);
    st->overwrite_editor = e->id;
    tiku_alert_open(&st->ask, TIKU_ALERT_WARN, CMD_SAVE, text,
                    "Cancel|Save Over It");
    st->ask.dflt = 0;
    if (st->services->raise_window != NULL) {
        st->services->raise_window(st->services->ctx, st->proj_id);
    }
    tiku_ide_project_paint(st);
    return 0;
}

/**
 * @brief Put the digits of a line number, and its blank, into @p tv.
 */
void
tiku_ide_type_number(tiku_textview_t *tv, int n)
{
    char digits[16];
    int i;

    snprintf(digits, sizeof digits, "%d ", n);
    for (i = 0; digits[i] != '\0'; i++) {
        tiku_textview_insert(tv, digits[i]);
    }
}

/**
 * @brief The number the line AFTER line @p at should wear.
 *
 * The interpreter's own habit: count by tens, so a line can always be
 * put between two others later.  A line wearing no number offers none.
 *
 * @return the next number, or 0 for none.
 */
int
tiku_ide_next_number(const tiku_textview_t *tv, int at)
{
    const char *p = tiku_textview_line(tv, at);
    long n;
    char *end;

    if (p == NULL) {
        return 0;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    n = strtol(p, &end, 10);
    return (end != p && n > 0 && n < 99990) ? (int)n + 10 : 0;
}

/**
 * @brief Open @p row's file, or bring its window forward.
 *
 * A file already open is RAISED: opening it twice would give a person
 * two windows onto one document, and whichever they typed into last
 * would be the one the other quietly forgot.
 */
void
tiku_ide_edit_open(ide_t *st, const prow_t *row)
{
    edit_t *e = tiku_ide_edit_of_path(st, row->path);
    int i;

    if (e != NULL) {
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx, e->id);
        }
        return;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface == NULL) {
            e = &st->edit[i];
            break;
        }
    }
    if (e == NULL) {
        snprintf(st->note, sizeof st->note,
                 "no room for another window");
        tiku_ide_project_paint(st);
        return;
    }
    memset(e, 0, sizeof *e);
    e->tip = -1;
    snprintf(e->path, sizeof e->path, "%s", row->path);
    {
        char *absolute = realpath(row->path, NULL);
        if (absolute && strlen(absolute) < sizeof e->path) {
            snprintf(e->path, sizeof e->path, "%s", absolute);
        }
        free(absolute);
    }
    snprintf(e->name, sizeof e->name, "%s", row->name);
    e->lang = tiku_ide_lang_of(e->name);
    tiku_ide_edit_load(e);
    tiku_ide_recovery_open(e);
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* A program is CONTINUED, not reread from the top: the caret
         * lands at the end of the last line, so the first Return is
         * already the next numbered line. */
        int last = tiku_textview_lines(&e->tv) - 1;

        tiku_textview_place(&e->tv, last,
                            tiku_textview_line_len(&e->tv, last));
    }
    if (e->lang == TIKU_SYNTAX_BASIC &&
        !tiku_textview_modified(&e->tv) && !e->load_failed &&
        tiku_textview_lines(&e->tv) <= 1 &&
        tiku_textview_line_len(&e->tv, 0) == 0) {
        /* A fresh page of BASIC begins where every line of it must:
         * with a number.  Counted by tens, the interpreter's own
         * habit, so a line can always be put between two others. */
        tiku_ide_type_number(&e->tv, 10);
        tiku_textview_saved(&e->tv);    /* offered, not yet work */
    }
    e->w = EDIT_W;
    e->h = EDIT_H;
    /* The caret was placed at the end of a file that may run past a
     * screenful; bring the page to it, so opening a long program does
     * not begin with the caret lost below the bottom edge. */
    (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
    e->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_PANEL);
    if (e->surface == NULL) {
        tiku_ide_recovery_release(e, 0);
        tiku_textview_free(&e->tv);
        return;
    }
    e->id = st->services->open(st->services->ctx, e->name, EDIT_W,
                               EDIT_H);
    if (e->id == 0u) {
        tiku_ide_recovery_release(e, 0);
        tiku_textview_free(&e->tv);
        tiku_surface_free(e->surface);
        e->surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for another window");
        tiku_ide_project_paint(st);
        return;
    }
    tiku_ide_edit_paint(st, e);
    tiku_ide_edit_publish(st, e);
    if (st->services->sizeable != NULL) {
        /* An editor's page is the one window a person grows. */
        (void)st->services->sizeable(st->services->ctx, e->id);
    }
    tiku_ide_project_paint(st);          /* the Window menu grew */
    tiku_ide_project_publish(st);
}

/** @brief Let one editor go; the project window stays. */
void
tiku_ide_edit_drop(ide_t *st, edit_t *e)
{
    int i;

    if (tiku_ide_debug_locked(st, e)) {
        snprintf(e->feedback, sizeof e->feedback, "Stop the debug run before closing its source");
        tiku_ide_edit_paint(st, e);
        return;
    }
    if (tiku_textview_modified(&e->tv)) {
        char text[256];
        snprintf(text, sizeof text, "Save changes to %s before closing?", e->name);
        st->closing_editor = e->id;
        tiku_alert_open(&st->ask, TIKU_ALERT_WARN, CMD_QUIT,
                        text, "Cancel|Discard|Save");
        if (st->services->raise_window) {
            st->services->raise_window(st->services->ctx, st->proj_id);
        }
        tiku_ide_project_paint(st);
        return;
    }
    tiku_ide_recovery_release(e, 0);
    tiku_textview_free(&e->tv);
    st->services->close(st->services->ctx, e->id);
    tiku_surface_free(e->surface);
    e->surface = NULL;
    tiku_ide_project_paint(st);
    tiku_ide_project_publish(st);
    /* Every surviving editor's Window menu named the one that left:
     * republished, or the stale row would offer a window that is not
     * there to raise. */
    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL) {
            tiku_ide_edit_publish(st, &st->edit[i]);
        }
    }
}

int
tiku_ide_document_fits(const char *text)
{
    int lines = 1, col = 0;

    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if ((ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch == 127) {
            return 0;
        }
        if (*text == '\n') { col = 0; if (text[1]) { lines++; } }
        else { col++; }
        if (lines > TIKU_TEXTVIEW_LINES_MAX ||
            col >= TIKU_TEXTVIEW_LINE_MAX) { return 0; }
    }
    return 1;
}
