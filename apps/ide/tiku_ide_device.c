/* tiku_ide_device.c -- execution target, upload preflight and board callbacks.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/**
 * @brief Put the desktop's default device into @p out: where every new
 *        run and board save goes.
 *
 * A project file's `board` line is not consulted.
 *
 * @return nonzero when there is a default device; 0 means this machine.
 */
int
tiku_ide_dest_board(ide_t *st, char *out, size_t max)
{
    out[0] = '\0';
    if (st->services->current_device != NULL) {
        (void)st->services->current_device(st->services->ctx, out, max);
    }
    return out[0] != '\0';
}

/**
 * @brief Run @p path on the desktop's default device, or on this machine
 *        when there is none.
 *
 * A page holding changes to the file is saved first, and the run is
 * cancelled when it cannot be.  When the default device is disconnected,
 * the run is refused.
 */
void
tiku_ide_run_file_where(ide_t *st, const char *path)
{
    char dest[64];
    edit_t *e = tiku_ide_edit_of_path(st, path);

    if (st->running) { return; }
    if (e && (e->load_failed ||
        (tiku_editdoc_modified(e->doc) && !tiku_ide_edit_save(e)))) {
        snprintf(st->note, sizeof st->note, "Run cancelled: source could not be saved");
        tiku_ide_project_paint(st);
        return;
    }

    if (tiku_ide_dest_board(st, dest, sizeof dest)) {
        tiku_ide_run_file_on_board(st, path, dest);
    } else {
        tiku_ide_run_file(st, path);
    }
}

/**
 * @brief Run the file at @p path on @p board through the shell, and open
 *        the console for it.
 *
 * There is no child and no pipe: the shell runs the program, and each
 * line it prints arrives through tiku_ide_board_said(), which passes it
 * to tiku_ide_message_take().
 */
void
tiku_ide_run_file_on_board(ide_t *st, const char *path, const char *board)
{
    char program[8192];

    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        tiku_ide_project_paint(st);
        return;
    }
    if (st->services->board_run == NULL || board == NULL ||
        board[0] == '\0') {
        snprintf(st->note, sizeof st->note, "no board is connected");
        tiku_ide_project_paint(st);
        return;
    }
    if (!tiku_ide_board_connected(st, board)) {
        snprintf(st->note, sizeof st->note, "Device disconnected; choose it in TikuDesktop");
        tiku_ide_project_paint(st);
        return;
    }
    if (!tiku_ide_upload_read(st, path, program, sizeof program) ||
        !tiku_ide_basic_fits(st, program, strlen(program))) {
        edit_t *e = tiku_ide_edit_of_path(st, path);
        if (e) { snprintf(e->feedback, sizeof e->feedback, "%s", st->note); }
        tiku_ide_project_paint(st);
        return;
    }

            snprintf(st->child_file, sizeof st->child_file, "%s", path);
    snprintf(st->board, sizeof st->board, "%s", board);
    st->child_numbered = 1;          /* BASIC names its own lines */
    st->child_fd = -1;               /* no pipe: see board_said   */
    st->nmsg = 0;
    st->msg_problems = 1;
    st->partial[0] = '\0';
    (void)tiku_list_set_count(&st->msg_list, 0);
    tiku_ide_problems_show(st);
    if (!st->services->board_run(st->services->ctx, st->board, program)) {
        st->board[0] = '\0';
        snprintf(st->note, sizeof st->note, "the board would not take it");
        tiku_ide_project_paint(st);
        return;
    }
        st->running = 1;
    st->run_from = 0;
    st->stop_asked = 0;
    tiku_ide_messages_paint(st);
    tiku_ide_console_open(st, path, st->board);
}

/**
 * @brief Ask to keep the file at @p path on the default device under its
 *        own name; tiku_ide_upload_confirm() sends it once confirmed.
 *
 * The name is the file's without its extension, cut to seven characters,
 * which must be letters, digits or underscores; the board keeps it as
 * /data/<name>.bas.  A page holding changes is saved first.
 */
void
tiku_ide_save_file_on_board(ide_t *st, const char *path)
{
    char boards[256];
    char name[8];
    char question[512];
    const char *base, *dot;
    size_t k, i;
    edit_t *e = tiku_ide_edit_of_path(st, path);
    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        tiku_ide_project_paint(st);
        return;
    }
    if (st->ask.open) { return; }
    if (e && (e->load_failed ||
        (tiku_editdoc_modified(e->doc) && !tiku_ide_edit_save(e)))) {
        snprintf(st->note, sizeof st->note, "Upload cancelled: source save failed");
        tiku_ide_project_paint(st);
        return;
    }
            if (st->services->board_save == NULL ||
        !tiku_ide_dest_board(st, boards, sizeof boards)) {
        snprintf(st->note, sizeof st->note, "no board is connected");
        tiku_ide_project_paint(st);
        return;
    }
    base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;
    dot = strrchr(base, '.');
    k = (dot != NULL) ? (size_t)(dot - base) : strlen(base);
    if (k > 7u) {
        k = 7u;
    }
    memcpy(name, base, k);
    name[k] = '\0';
    for (i = 0; i < k; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '_')) { break; }
    }
    if (!k || i != k) {
        snprintf(st->note, sizeof st->note,
                 "Board names need letters, digits or underscore. Rename the file.");
        tiku_ide_project_paint(st);
        return;
    }
    if (!tiku_ide_upload_read(st, path, st->upload_program,
                              sizeof st->upload_program) ||
        !tiku_ide_basic_fits(st, st->upload_program,
                             strlen(st->upload_program))) {
        if (e) { snprintf(e->feedback, sizeof e->feedback, "%s", st->note); }
        tiku_ide_project_paint(st);
        return;
    }
    snprintf(st->upload_board, sizeof st->upload_board, "%s", boards);
    snprintf(st->upload_name, sizeof st->upload_name, "%s", name);
    snprintf(question, sizeof question,
             "Replace any existing program? "
             "Save /data/%s.bas on %.63s. %zu bytes.%s",
             name, boards, strlen(st->upload_program),
             ((dot ? (size_t)(dot - base) : strlen(base)) > 7) ?
             " Filename shortened to seven characters; check for name collisions." : "");
    tiku_alert_open(&st->ask, TIKU_ALERT_WARN, CMD_SAVEBOARD,
                    question, "Cancel|Save / Replace");
    st->ask.dflt = 0;
    if (st->services->raise_window) {
        st->services->raise_window(st->services->ctx, st->proj_id);
    }
    tiku_ide_project_paint(st);
}

/** @brief Save the program the question was asked about on the device it
 *         named, even if the default device has changed since.  No run or
 *         save starts until the shell reports how the save went. */
void
tiku_ide_upload_confirm(ide_t *st)
{
    if (st->running || !tiku_ide_board_connected(st, st->upload_board)) {
        snprintf(st->note, sizeof st->note, "Upload cancelled: device busy or disconnected");
        tiku_ide_project_paint(st);
        return;
    }
    snprintf(st->board, sizeof st->board, "%s", st->upload_board);
    st->child_fd = -1;
    if (!st->services->board_save(st->services->ctx, st->board,
                                  st->upload_name, st->upload_program)) {
        st->board[0] = '\0';
        snprintf(st->note, sizeof st->note, "the board would not take it");
        tiku_ide_project_paint(st);
        return;
    }
    st->running = 1;
    st->saving = 1;
    st->run_from = 0;
    st->stop_asked = 0;
    snprintf(st->note, sizeof st->note, "keeping %s on %s…", st->upload_name,
             st->board);
    tiku_ide_project_paint(st);
}

/**
 * @brief Release a board run whose Stop the board never answered.
 *
 * The program may still be running on the board; this clears only the
 * IDE's running state, so the page can be edited again.
 */
void
tiku_ide_board_run_released(ide_t *st)
{
    edit_t *source = tiku_ide_edit_of_path(st, st->child_file);

    st->stop_asked = 0;
    st->running = st->asking = 0;
    st->board[0] = '\0';
    if (st->debugging) {
        st->debugging = st->debug_paused = 0;
    }
    if (source != NULL) {
        snprintf(source->feedback, sizeof source->feedback,
                 "The board did not answer Stop; the page is yours again"
                 " (the program may still be running there)");
        tiku_ide_edit_paint(st, source);
        tiku_ide_edit_publish(st, source);
    }
    tiku_ide_message_take(st, "? the board did not answer the stop");
    tiku_ide_project_paint(st);
    tiku_ide_project_publish(st);
}

/**
 * @brief Take a report from the shell: a new default device, a line the
 *        board printed, a request for input, or the end of a run or save.
 *
 * Printed lines go to the messages and the console; the end of a save
 * goes to the Project window's note.
 */
void
tiku_ide_board_said(void *state, int what, const char *text)
{
    ide_t *st = state;
    if (st == NULL) {
        return;
    }
    if (what == TIKU_APP_BOARD_CURRENT) {
        int i;

        if (st->proj_id != 0u) {
            tiku_ide_project_publish(st);
        }
        for (i = 0; i < IDE_EDITS; i++) {
            if (st->edit[i].surface != NULL) {
                tiku_ide_edit_publish(st, &st->edit[i]);
            }
        }
        return;
    }
    if (!st->running || st->board[0] == '\0') {
        return;
    }
    if (what == TIKU_APP_BOARD_LINE && text && tiku_ide_debug_line(st, text)) { return; }
        if (what == TIKU_APP_BOARD_RUNNING) {
        tiku_ide_console_take(st, "running", 1);
        tiku_ide_console_paint(st);
        return;
    }
    if (what == TIKU_APP_BOARD_LINE) {
        if (text != NULL && text[0] != '\0') {
            tiku_ide_message_take(st, text);
            tiku_ide_console_take(st, text, 0);
            tiku_ide_console_paint(st);
        }
        return;
    }
    if (what == TIKU_APP_BOARD_ASKS) {
        /* The program wants a line.  It asks in the console when that is
         * open, otherwise in the Messages window, opened now if need be. */
        st->asking = 1;
        snprintf(st->ask_prompt, sizeof st->ask_prompt, "%s",
                 (text != NULL) ? text : "? ");
                tiku_field_start(&st->answer, "");
        if (st->con_surface != NULL) {
            /* The console is open: the program asks there. */
            tiku_ide_console_paint(st);
            if (st->services->raise_window != NULL) {
                (void)st->services->raise_window(st->services->ctx,
                                                 st->con_id);
            }
            return;
        }
        tiku_ide_messages_open(st);
        (void)tiku_list_set_count(&st->msg_list, st->nmsg);
        tiku_ide_messages_paint(st);
        tiku_ide_messages_publish(st);
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx, st->msg_id);
        }
        return;
    }
    st->running = 0;
    st->asking = 0;
    st->stop_asked = 0;
    if (st->debugging) {
        edit_t *source = tiku_ide_edit_of_path(st, st->child_file);
        st->debugging = st->debug_paused = 0;
        if (source) {
            snprintf(source->feedback, sizeof source->feedback, "%s",
                     what == TIKU_APP_BOARD_FAILED ?
                     "Debug failed: firmware must support DEBUG v1; check setup limits" :
                     "Debug run ended");
            tiku_ide_edit_paint(st, source); tiku_ide_edit_publish(st, source);
        }
    }
    if (what == TIKU_APP_BOARD_SAVED || st->saving) {
        /* A save is reported in the Project window's note. */
        if (what == TIKU_APP_BOARD_SAVED) {
            snprintf(st->note, sizeof st->note, "kept on %s as %s",
                     st->board, (text != NULL) ? text : "");
        } else {
            snprintf(st->note, sizeof st->note, "%s would not keep it",
                     st->board);
        }
        st->saving = 0;
        st->board[0] = '\0';
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);
        return;
    }
        st->board[0] = '\0';
    if (what == TIKU_APP_BOARD_FAILED) {
        tiku_ide_message_take(st, "? the board would not run it");
    } else if (what == TIKU_APP_BOARD_STOPPED) {
        tiku_ide_message_take(st, "? the run was stopped by hand");
    }
    if (st->con_surface != NULL) {
        tiku_ide_console_take(st, "", 1);
        tiku_ide_console_take(st, (what == TIKU_APP_BOARD_FAILED)  ? "failed"
                       : (what == TIKU_APP_BOARD_STOPPED) ? "stopped"
                                                          : "ended", 1);
        tiku_ide_console_paint(st);
        tiku_ide_console_publish(st);
    } else {
        /* The console already holds the errors and their line numbers;
         * the Messages window is shown only when there is no console. */
        tiku_ide_messages_show(st);
    }
    tiku_ide_project_publish(st);
}

/** @brief Whether @p board is among the boards the desktop has connected;
 *         1 when the desktop cannot say. */
int
tiku_ide_board_connected(ide_t *st, const char *board)
{
    char names[4096], *p;
    size_t n = strlen(board);

    if (st->services->board_names == NULL) {
        return 1; /* this host cannot report connections */
    }
    names[0] = '\0';
    (void)st->services->board_names(st->services->ctx, names, sizeof names);
    names[sizeof names - 1] = '\0';
    for (p = names; *p; ) {
        char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == n && memcmp(p, board, n) == 0) { return 1; }
        if (!end) { break; }
        p = end + 1;
    }
    return 0;
}

/** @brief The target strip's text: what is running and where, or where
 *         Run will go and whether that device is ready. */
void
tiku_ide_target_text(ide_t *st, char *out, size_t max)
{
    char board[64];

    if (st->running) {
        snprintf(out, max, "%s: %s", st->debugging && st->debug_paused ? "Debug paused" : st->saving ? "Saving" :
                 st->asking ? "Waiting for input" : "Running",
                 st->board[0] ? st->board : "This computer");
    } else if (tiku_ide_dest_board(st, board, sizeof board)) {
        snprintf(out, max, "Run on: %s — %s", board,
                 tiku_ide_board_connected(st, board) ? "Ready" : "Disconnected");
    } else {
        snprintf(out, max, "Run on: This computer — no default device");
    }
}

/** @brief Paint the target strip under page @p e's toolbar, its text cut
 *         to the window's width. */
void
tiku_ide_target_paint(ide_t *st, edit_t *e)
{
    char label[160];
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, TOOLBAR_H, e->w, TARGET_H };

    tiku_ide_target_text(st, label, sizeof label);
    while (strlen(label) && tiku_text_width(f, label) > e->w - 12) {
        label[strlen(label) - 1] = '\0';
    }
    tiku_fill(e->surface, r, TIKU_C_PANEL);
    tiku_text(e->surface, f, 6, r.y + f->ascent + 4, label, TIKU_C_TEXT);
}

/** @brief Read the program at @p path into @p out for the board.  A file
 *         too large for @p out, or holding a NUL byte, is refused.
 *         @return 0 with the reason in the note. */
int
tiku_ide_upload_read(ide_t *st, const char *path, char *out, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    int extra, bad;

    if (!f) {
        snprintf(st->note, sizeof st->note, "Cannot read the program");
        return 0;
    }
    n = fread(out, 1, max - 1, f);
    extra = fgetc(f);
    bad = ferror(f);
    if (fclose(f) != 0) { bad = 1; }
    if (bad || extra != EOF || memchr(out, '\0', n)) {
        snprintf(st->note, sizeof st->note, "%s", bad ? "Program read failed" :
                 extra != EOF ? "Upload too large (maximum 8191 bytes)" :
                 "Upload rejected: program contains a NUL byte");
        return 0;
    }
    out[n] = '\0';
    return 1;
}

/** @brief Where a scan of a program's line has got to. */
typedef struct {
    long line;                  /* the line, counted from 1             */
    size_t len;                 /* its bytes so far                     */
    size_t body;                /* its bytes after a number and blanks  */
    int at;                     /* 0 start, 1 blanks, 2 number, 3 blanks
                                   after it, 4 body, 5 unnumbered       */
    int cr;                     /* the last byte was a CR               */
} basic_scan_t;

/** @brief Scan @p n bytes of a program, @p s keeping its place across
 *         calls.  @return 0, with the reason in the note, at the first
 *         byte BASIC would take differently from the page. */
static int
basic_scan(ide_t *st, basic_scan_t *s, const char *bytes, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)bytes[i];

        if (c == '\n' && s->cr) {
            s->cr = 0;
            continue;
        }
        s->cr = c == '\r';
        if (c == '\n' || c == '\r') {
            s->line++;
            s->len = s->body = 0;
            s->at = 0;
            continue;
        }
        if ((c < 32 && c != '\t') || c == 127) {
            snprintf(st->note, sizeof st->note,
                     "Line %ld holds a control character (^%c) "
                     "BASIC cannot take",
                     s->line, c == 127 ? '?' : c + 64);
            return 0;
        }
        if (++s->len > IDE_BASIC_LINE) {
            snprintf(st->note, sizeof st->note,
                     "Line %ld is longer than the %d bytes BASIC reads",
                     s->line, IDE_BASIC_LINE);
            return 0;
        }
        if (c == ' ' || c == '\t') {
            s->at = s->at == 0 ? 1 : s->at == 2 ? 3 : s->at;
        } else if (c >= '0' && c <= '9' && s->at <= 2) {
            s->at = 2;
        } else if (s->at < 4) {
            s->at = s->at >= 2 ? 4 : 5;
        }
        if (s->at == 4 && ++s->body > IDE_BASIC_BODY) {
            snprintf(st->note, sizeof st->note,
                     "Line %ld has more than the %d bytes BASIC keeps "
                     "after its number",
                     s->line, IDE_BASIC_BODY);
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Whether BASIC takes the @p n bytes of @p text as the page has
 *        them: no control character but tab, CR and LF, no line longer
 *        than it reads, and no more after a line's number than it keeps.
 *
 * The page holds any text; this is checked where a program leaves it,
 * for a run, a board save or a debug run.  @return 0 with the reason in
 * the note.
 */
int
tiku_ide_basic_fits(ide_t *st, const char *text, size_t n)
{
    basic_scan_t s;

    memset(&s, 0, sizeof s);
    s.line = 1;
    return basic_scan(st, &s, text, n);
}

/** @brief tiku_ide_basic_fits() over the file at @p path, read in
 *         pieces. */
int
tiku_ide_basic_file_fits(ide_t *st, const char *path)
{
    char chunk[4096];
    basic_scan_t s;
    FILE *f = fopen(path, "rb");
    size_t n;
    int ok = 1;

    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "Cannot read the program");
        return 0;
    }
    memset(&s, 0, sizeof s);
    s.line = 1;
    while (ok && (n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        ok = basic_scan(st, &s, chunk, n);
    }
    if (ok && ferror(f)) {
        snprintf(st->note, sizeof st->note, "Program read failed");
        ok = 0;
    }
    fclose(f);
    return ok;
}
