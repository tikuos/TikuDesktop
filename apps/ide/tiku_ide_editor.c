/* tiku_ide_editor.c -- source-file windows, clipboard and file lifecycle.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static void msep(tiku_menu_list_t *m);

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
 * @brief Cut, copy or paste at page @p e through the desk's clipboard,
 *        or select all, undo or redo.
 *
 * The clipboard is reached through the desktop's services.  Cut deletes
 * only what the clipboard took.
 *
 * @return 1 when the command did something.
 */
int
tiku_ide_edit_clip(ide_t *st, edit_t *e, int command)
{
    tiku_edit_status_t done;

    (void)st;
    switch (command) {
    case CMD_COPY:
    case CMD_CUT:
        done = command == CMD_CUT ? tiku_codeview_cut(e->view)
                                  : tiku_codeview_copy(e->view);
        if (done == TIKU_EDIT_LIMIT || done == TIKU_EDIT_INVALID) {
            snprintf(e->feedback, sizeof e->feedback,
                     "The selection is more than the clipboard holds.");
        }
        return done == TIKU_EDIT_OK;
    case CMD_PASTE:
        return tiku_codeview_paste(e->view) == TIKU_EDIT_OK;
    case CMD_SELALL:
        return tiku_codeview_select_all(e->view) == TIKU_EDIT_OK;
    case CMD_UNDO:
        e->feedback[0] = '\0';
        return tiku_codeview_undo(e->view) == TIKU_EDIT_OK;
    case CMD_REDO:
        e->feedback[0] = '\0';
        return tiku_codeview_redo(e->view) == TIKU_EDIT_OK;
    default:
        return 0;
    }
}

/** @brief Append a menu row: its label, command, shortcut key with its
 *         modifiers, and whether it is enabled.  A full menu takes none. */
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

/** @brief Append a separator row; a full menu takes none. */
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

/** @brief Fill the Window menu that every IDE window carries: a row per
 *         open page, marked "(changed)" when it holds unsaved changes. */
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
        snprintf(row, sizeof row, "%s%s", e->name,
                 tiku_editdoc_modified(e->doc) ? " (changed)" : "");
        tiku_ide_mrow(m, row, CMD_WINDOW + i, 0, 0u, 1);
        n++;
    }
    if (n == 0) {
        tiku_ide_mrow(m, "No open files", 0, 0, 0u, 0);
    }
}

/** @brief Publish page @p e's menus: File, Edit and Window, and for BASIC
 *         a Debug menu. */
void
tiku_ide_edit_publish(ide_t *st, edit_t *e)
{
    tiku_menuset_t menus;
    char row[64];

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 3;
    /* File: save the page, run it and stop the run (BASIC), close the
     * page; each row with its shortcut. */
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    snprintf(row, sizeof row, "Save %s", e->name);
    tiku_ide_mrow(&menus.menu[0], row, CMD_SAVE, 's', TIKU_MOD_CMD,
         tiku_editdoc_modified(e->doc));
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
    /* Edit: the editing rows, grouped, each with its shortcut. */
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "Edit");
    tiku_ide_mrow(&menus.menu[1], "Undo", CMD_UNDO, 'z', TIKU_MOD_CMD,
         tiku_editdoc_can_undo(e->doc));
    tiku_ide_mrow(&menus.menu[1], "Redo", CMD_REDO, 'z',
         TIKU_MOD_CMD | TIKU_MOD_SHIFT, tiku_editdoc_can_redo(e->doc));
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
    tiku_ide_mrow(&menus.menu[1], "Next problem", CMD_NEXT_PROBLEM, '\'',
         TIKU_MOD_CMD, tiku_ide_problems_any(e));
    tiku_ide_mrow(&menus.menu[1], "Previous problem", CMD_PREV_PROBLEM, '\'',
         TIKU_MOD_CMD | TIKU_MOD_SHIFT, tiku_ide_problems_any(e));
    msep(&menus.menu[1]);
    tiku_ide_mrow(&menus.menu[1], "Toggle comment", CMD_COMMENT, '/',
         TIKU_MOD_CMD, !tiku_ide_debug_locked(st, e));
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* Renumber by tens, for BASIC's numbered lines. */
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

/**
 * @brief Read @p e's file in, every byte as it is.  A missing file is an
 *        empty page.
 *
 * A NUL byte, malformed UTF-8, a file the page cannot hold or a read that
 * fails leaves an empty page whose saving and running are blocked.
 */
void
tiku_ide_edit_load(edit_t *e)
{
    FILE *f;
    char *whole = NULL;
    size_t got = 0, cap = 0;
    int ok = 1;

    e->disk_known = e->disk_changed = 0;
    if (!tiku_ide_text_new(e)) {
        e->load_failed = 1;
        return;
    }
    (void)tiku_ide_text_load(e, "", 0);
    f = fopen(e->path, "rb");
    if (f == NULL) {
        if (errno != ENOENT) { e->load_failed = 1; }
        return;
    }
    e->disk_known = fstat(fileno(f), &e->disk) == 0;
    for (;;) {
        size_t n;

        if (got == cap) {
            char *grown;

            if (cap > (size_t)INT_MAX ||
                (grown = realloc(whole, cap ? cap * 2 : 65536)) == NULL) {
                ok = 0;
                break;
            }
            whole = grown;
            cap = cap ? cap * 2 : 65536;
        }
        n = fread(whole + got, 1, cap - got, f);
        got += n;
        if (n == 0) {
            break;
        }
    }
    if (ferror(f)) { ok = 0; }
    if (fclose(f) != 0) { ok = 0; }
    if (ok && (got == 0 || memchr(whole, '\0', got) == NULL) &&
        tiku_ide_text_load(e, whole, got) == TIKU_EDIT_OK) {
        free(whole);
        return;
    }
    free(whole);
    (void)tiku_ide_text_load(e, "", 0);
    e->load_failed = 1;
    snprintf(e->feedback, sizeof e->feedback,
             "File could not be loaded safely; saving and running are blocked");
}

/** @brief Write a run of the page's bytes to the FILE @p ctx. */
static int
write_run(void *ctx, const char *bytes, size_t n)
{
    return fwrite(bytes, 1, n, ctx) != n;
}

/**
 * @brief Save page @p e over its file, through a temporary file renamed
 *        into place.
 *
 * Refused when the page failed to load; when the file is not a regular
 * file, has other hard links or is read-only; or when it changed on disk
 * since the page read or saved it, which sets e->disk_changed.
 *
 * @return 1 when saved; 0 with the reason in the page's feedback.
 */
int
tiku_ide_edit_save(edit_t *e)
{
    FILE *f;
    int fd, ok = 1;
    char temp[560];
    struct stat sb;
    mode_t mode = 0600;
    uint64_t state = tiku_editdoc_state(e->doc);

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
    {
        /* Every byte as it is, and the file's line break after a last
         * line that has none: both BASIC runners read a program line by
         * line, and would join RUN onto an unended last line. */
        tiku_edit_range_t all = { 0, tiku_editdoc_length(e->doc) };
        int last = tiku_editdoc_byte_at(e->doc, all.end - 1);

        if (tiku_editdoc_each(e->doc, all, write_run, f) != TIKU_EDIT_OK ||
            ferror(f)) { ok = 0; }
        if (last != '\n' && last != '\r' &&
            fputs(e->newline, f) == EOF) { ok = 0; }
    }
    if (fchmod(fd, mode) != 0 || fflush(f) != 0 || fsync(fd) != 0) { ok = 0; }
    if (fclose(f) != 0) { ok = 0; }
    if (!ok || rename(temp, e->path) != 0) {
        unlink(temp);
        snprintf(e->feedback, sizeof e->feedback, "Save failed; unsaved work retained");
        return 0;
    }
    e->disk_known = stat(e->path, &e->disk) == 0;
    tiku_editdoc_saved(e->doc, state);
    (void)tiku_ide_recovery_write(e);
    snprintf(e->feedback, sizeof e->feedback, "Saved");
    return 1;
}

/**
 * @brief Save page @p e; when its file changed on disk since the page
 *        read or saved it, ask on the Project window whether to save
 *        over it, Cancel being the default.
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
 * @brief The number for the line after @p line: its number plus ten.
 *
 * @return the next number, or 0 when @p line has no number or one of
 *         99990 or more.
 */
int
tiku_ide_next_number(const edit_t *e, int64_t line)
{
    long n = tiku_ide_line_number(e, line, 1);

    return (n > 0 && n < 99990) ? (int)n + 10 : 0;
}

/** @brief Open @p row's file in an editor page, or raise its page when
 *         the file is already open, so a file never has two pages. */
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
    e->services = st->services;
    tiku_ide_edit_load(e);
    if (e->view == NULL) {
        snprintf(st->note, sizeof st->note, "no room for another window");
        tiku_ide_project_paint(st);
        return;
    }
    tiku_ide_recovery_open(e);
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* The caret goes to the end of the last line with text, before
         * a final line break, so the first Return starts the next
         * numbered line. */
        int64_t last = tiku_editdoc_lines(e->doc) - 1;

        if (last > 0 && tiku_editdoc_line_start(e->doc, last) ==
                        tiku_editdoc_length(e->doc)) {
            last--;
        }
        tiku_ide_place(e, last, INT64_MAX);
    }
    if (e->lang == TIKU_SYNTAX_BASIC && !tiku_editdoc_modified(e->doc) &&
        !e->load_failed && tiku_editdoc_length(e->doc) == 0) {
        /* An empty BASIC page starts with line number 10. */
        (void)tiku_ide_replace(e, 0, 0, "10 ", 3, 0);
        tiku_editdoc_saved(e->doc, tiku_editdoc_state(e->doc));
    }
    e->w = EDIT_W;
    e->h = EDIT_H;
    /* The caret may be past the first screenful: scroll the page to
     * show it. */
    tiku_ide_edit_reveal(e);
    e->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_PANEL);
    if (e->surface == NULL) {
        tiku_ide_recovery_release(e, 0);
        tiku_ide_text_free(e);
        return;
    }
    e->id = st->services->open(st->services->ctx, e->name, EDIT_W,
                               EDIT_H);
    if (e->id == 0u) {
        tiku_ide_recovery_release(e, 0);
        tiku_ide_text_free(e);
        tiku_surface_free(e->surface);
        e->surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for another window");
        tiku_ide_project_paint(st);
        return;
    }
    tiku_ide_problems_page(st, e);
    tiku_ide_edit_paint(st, e);
    tiku_ide_edit_publish(st, e);
    if (st->services->sizeable != NULL) {
        (void)st->services->sizeable(st->services->ctx, e->id);
    }
    tiku_ide_project_paint(st);          /* the Window menu lists it */
    tiku_ide_project_publish(st);
}

/** @brief Close page @p e, first asking whether to save it when it holds
 *         changes; refused while a debug run uses the page. */
void
tiku_ide_edit_drop(ide_t *st, edit_t *e)
{
    int i;

    if (tiku_ide_debug_locked(st, e)) {
        snprintf(e->feedback, sizeof e->feedback, "Stop the debug run before closing its source");
        tiku_ide_edit_paint(st, e);
        return;
    }
    if (tiku_editdoc_modified(e->doc)) {
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
    tiku_ide_text_free(e);
    st->services->close(st->services->ctx, e->id);
    tiku_surface_free(e->surface);
    e->surface = NULL;
    tiku_ide_project_paint(st);
    tiku_ide_project_publish(st);
    /* The other pages' Window menus still list the closed page:
     * republish them. */
    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL) {
            tiku_ide_edit_publish(st, &st->edit[i]);
        }
    }
}
