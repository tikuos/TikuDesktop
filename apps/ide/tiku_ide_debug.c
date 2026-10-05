/* tiku_ide_debug.c -- the BASIC line debugger: breakpoints, watches and
 * the [TDBG] protocol, carried by the BASIC run and input stream to the
 * default device or a local tiku-basic.  It opens no serial port of its
 * own; a watch names a variable.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static int debug_watch_setup(edit_t *e, char *out, size_t max);
static int debug_send(void *ctx, const void *bytes, size_t n);
static int debug_recv(void *ctx, void *bytes, size_t max);
static int debug_local_start(ide_t *st, const char *program,
                             const char *path);
static void debug_value(char *out, size_t max, const char *line);
static void debug_local_line(const char *line, void *ctx);
static tiku_rect_t debug_button(const edit_t *e, int i);

/** @brief Whether page @p e is the source of the running debug run, which
 *         keeps it read-only. */
int
tiku_ide_debug_locked(ide_t *st, edit_t *e)
{
    return st->running && st->debugging && !strcmp(st->child_file, e->path);
}

/** @brief Show the debug panel on page @p e, closing the keyword list and
 *         the find bar; the watch field starts as "A,B,C". */
void
tiku_ide_debug_show(edit_t *e)
{
    e->debug_show = 1;
    tiku_ide_assist_close(e);
    e->finding = 0;
    if (!e->watches.active) { tiku_field_start(&e->watches, "A,B,C"); }
}

/** @brief How many lines hold a breakpoint. */
static int
debug_breaks(const edit_t *e)
{
    int64_t line = tiku_editdoc_markers_next(e->doc, 0, 1u << IDE_MARK_BREAK);
    int n = 0;

    for (; line >= 0; line = tiku_editdoc_markers_next(e->doc, line + 1,
                                                      1u << IDE_MARK_BREAK)) {
        n++;
    }
    return n;
}

/**
 * @brief Toggle a breakpoint on the caret's line, which must start with a
 *        BASIC number from 1 to 65533.
 *
 * The breakpoint marks the line and follows it through edits; which
 * BASIC line it stops at is read when debugging starts.
 */
void
tiku_ide_debug_break(edit_t *e)
{
    int64_t y;
    long number;

    (void)tiku_ide_caret(e, &y, NULL);
    number = tiku_ide_line_number(e, y, 1);
    if (number <= 0 || number >= 65534) { return; }
    if (tiku_editdoc_markers_on(e->doc, y) & (1u << IDE_MARK_BREAK)) {
        (void)tiku_editdoc_markers_clear(e->doc, y, 1u << IDE_MARK_BREAK);
    } else if (debug_breaks(e) >= IDE_BREAKS) {
        snprintf(e->feedback, sizeof e->feedback, "Maximum 16 breakpoints");
    } else {
        (void)tiku_editdoc_marker_add(e->doc, y, IDE_MARK_BREAK, NULL);
    }
}

/** @brief Take every breakpoint off the page. */
void
tiku_ide_debug_clear(edit_t *e)
{
    (void)tiku_editdoc_markers_clear(e->doc, -1, 1u << IDE_MARK_BREAK);
}

/**
 * @brief The DEBUG BREAK lines for the page's breakpoints, in @p out.
 *
 * Each marked line must still start with a BASIC number and be the last
 * line with that number, the one the interpreter keeps; otherwise the
 * page says which line and @return 0.
 */
static int
debug_break_lines(edit_t *e, char *out, size_t max)
{
    int64_t line = tiku_editdoc_markers_next(e->doc, 0, 1u << IDE_MARK_BREAK);
    size_t used = 0;

    out[0] = '\0';
    for (; line >= 0; line = tiku_editdoc_markers_next(e->doc, line + 1,
                                                      1u << IDE_MARK_BREAK)) {
        long number = tiku_ide_line_number(e, line, 1);

        if (number <= 0 || number >= 65534) {
            snprintf(e->feedback, sizeof e->feedback,
                     "Breakpoint on line %lld has no BASIC number; "
                     "clear breakpoints first",
                     (long long)line + 1);
            return 0;
        }
        if (tiku_ide_line_of_number(e, number) != line) {
            snprintf(e->feedback, sizeof e->feedback,
                     "Breakpoint on line %lld is not run: "
                     "a later line is also %ld",
                     (long long)line + 1, number);
            return 0;
        }
        if (used + 24 >= max) { return 0; }
        used += (size_t)snprintf(out + used, max - used, "DEBUG BREAK %ld\n",
                                 number);
    }
    return 1;
}

/**
 * @brief The DEBUG WATCH lines for the watch field's names, in @p out.
 *
 * A name is a letter and then letters, digits or underscores, at most
 * seven in all, with an optional final $; at most eight names, and CLEAR
 * is refused.
 *
 * @return 0 when a name is not valid or @p out is too small.
 */
static int
debug_watch_setup(edit_t *e, char *out, size_t max)
{
    const char *p = e->watches.text;
    int count = 0;
    size_t used = 0;
    out[0] = '\0';
    while (*p) {
        char name[9];
        size_t n = 0;
        while (*p == ' ' || *p == ',') { p++; }
        if (!*p) { break; }
        while (*p && *p != ',' && *p != ' ') {
            if (n >= 8) { return 0; }
            name[n++] = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
            p++;
        }
        name[n] = '\0';
        if (name[0] < 'A' || name[0] > 'Z' || ++count > 8) { return 0; }
        for (size_t i = 1; i < n; i++) {
            if (!((name[i] >= 'A' && name[i] <= 'Z') ||
                  (name[i] >= '0' && name[i] <= '9') || name[i] == '_' ||
                  (name[i] == '$' && i == n - 1))) { return 0; }
        }
        if (n == 8 && name[n - 1] != '$') { return 0; }
        if (!strcmp(name, "CLEAR")) { return 0; }
        if (used + n + 14 >= max) { return 0; }
        used += (size_t)snprintf(out + used, max - used, "DEBUG WATCH %s\n", name);
    }
    return 1;
}

/** @brief Send @p n bytes to the local interpreter.  @return 0 when all
 *         of them went, else -1. */
static int
debug_send(void *ctx, const void *bytes, size_t n)
{
    ide_t *st = ctx;
    ssize_t sent = send(st->debug_socket, bytes, n, MSG_NOSIGNAL);
    return sent == (ssize_t)n ? 0 : -1;
}

/** @brief Read what the local interpreter wrote, without waiting; a
 *         closed or failed socket sets debug_eof.  @return the bytes
 *         read, or 0. */
static int
debug_recv(void *ctx, void *bytes, size_t max)
{
    ide_t *st = ctx;
    ssize_t n = recv(st->debug_socket, bytes, max, MSG_DONTWAIT);
    if (!n || (n < 0 && errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)) {
        st->debug_eof = 1;
    }
    return n > 0 ? (int)n : 0;
}

/** @brief Start tiku-basic --interactive on a socket pair, with the file's
 *         folder as its sandbox, and hand it @p program.  @return 0 when
 *         it could not start. */
static int
debug_local_start(ide_t *st, const char *program, const char *path)
{
    int sockets[2];
    const char *tool = getenv("TIKU_BASIC");
    char folder[512], *slash;
    tiku_board_basic_stream_t stream = { debug_send, debug_recv, NULL, st };
    if (!tool || !*tool) { tool = "tiku-basic"; }
    snprintf(folder, sizeof folder, "%s", path);
    slash = strrchr(folder, '/');
    if (slash && slash != folder) { *slash = '\0'; }
    else { snprintf(folder, sizeof folder, "/tmp"); }
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) { return 0; }
    st->child = fork();
    if (st->child < 0) { close(sockets[0]); close(sockets[1]); return 0; }
    if (!st->child) {
        dup2(sockets[1], STDIN_FILENO);
        dup2(sockets[1], STDOUT_FILENO);
        dup2(sockets[1], STDERR_FILENO);
        close(sockets[0]); close(sockets[1]);
        execlp(tool, tool, "--interactive", folder, (char *)NULL);
        _exit(127);
    }
    close(sockets[1]);
    st->debug_socket = sockets[0];
    fcntl(st->debug_socket, F_SETFL, O_NONBLOCK);
    st->debug_eof = 0;
    st->local_debug = tiku_board_basic_start_stream(&stream, program);
    if (!st->local_debug) {
        close(st->debug_socket); kill(st->child, SIGTERM); waitpid(st->child, NULL, 0);
        return 0;
    }
    st->child_fd = -1;
    return 1;
}

/**
 * @brief Start a debug run of page @p e on the default device, or on a
 *        local tiku-basic when there is none.
 *
 * The page is saved and checked first: every line numbered, no line body
 * over IDE_BASIC_BODY, the watch names and the breakpoints valid.  The
 * program goes with DEBUG ON, WATCH and BREAK lines ahead of it; the page
 * stays read-only while the run goes, and the console shows its output.
 */
void
tiku_ide_debug_start(ide_t *st, edit_t *e)
{
    char program[8192], source[8192], watches[256], board[64], breaks[512];
    size_t used;
    int64_t i;
    if (st->running || e->lang != TIKU_SYNTAX_BASIC) { return; }
    tiku_ide_debug_show(e);
    if (!debug_watch_setup(e, watches, sizeof watches)) {
        snprintf(e->feedback, sizeof e->feedback, "Watch up to 8 scalar names (7 characters plus $)");
        return;
    }
    for (i = 0; i < tiku_editdoc_lines(e->doc); i++) {
        size_t len = 0;
        char *text = tiku_ide_line_dup(e, i, &len), *end;
        const char *p = text;
        long n;

        if (text == NULL) { return; }
        while (*p == ' ' || *p == '\t') { p++; }
        if (!*p) { free(text); continue; }
        n = strtol(p, &end, 10);
        if (end == p || n <= 0 || n >= 65534 || !(*p >= '0' && *p <= '9')) {
            snprintf(e->feedback, sizeof e->feedback, "Debug requires numbered BASIC source lines");
            free(text);
            return;
        }
        while (*end == ' ' || *end == '\t') { end++; }
        if (strlen(end) > IDE_BASIC_BODY) {
            snprintf(e->feedback, sizeof e->feedback, "Debug source line %ld exceeds the portable 143-byte body limit", n);
            free(text);
            return;
        }
        free(text);
    }
    if (!tiku_ide_edit_save(e) || !tiku_ide_upload_read(st, e->path, source, sizeof source)) { return; }
    if (!tiku_ide_basic_fits(st, source, strlen(source))) {
        snprintf(e->feedback, sizeof e->feedback, "%s", st->note);
        return;
    }
    if (!debug_break_lines(e, breaks, sizeof breaks)) { return; }
    used = (size_t)snprintf(program, sizeof program, "DEBUG ON\n%s%s",
                            watches, breaks);
    if (used + strlen(source) >= sizeof program) {
        snprintf(e->feedback, sizeof e->feedback, "Program plus debug setup exceeds 8191 bytes");
        return;
    }
    strcpy(program + used, source);
    board[0] = '\0';
    if (tiku_ide_dest_board(st, board, sizeof board)) {
        if (!tiku_ide_board_connected(st, board) || !st->services->board_run ||
            !st->services->board_input ||
            !st->services->board_run(st->services->ctx, board, program)) {
            snprintf(e->feedback, sizeof e->feedback, "Cannot start debugger on the default board");
            return;
        }
    } else if (!debug_local_start(st, program, e->path)) {
        snprintf(e->feedback, sizeof e->feedback, "Cannot start the local BASIC debugger");
        return;
    }
    snprintf(st->board, sizeof st->board, "%s", board);
    snprintf(st->child_file, sizeof st->child_file, "%s", e->path);
    st->running = st->debugging = 1;
    st->debug_paused = st->debug_pc = st->debug_values = 0;
    st->asking = st->saving = st->nmsg = 0;
    st->msg_problems = 1;
    tiku_ide_problems_show(st);
    st->child_numbered = 1; st->run_from = 0;
    st->stop_asked = 0;
    st->debug_state = tiku_editdoc_state(e->doc);
    tiku_ide_edit_lock(st, e);
    {
        tiku_edit_pos_t caret = tiku_ide_caret(e, NULL, NULL);

        (void)tiku_codeview_select(e->view, caret, caret);
    }
    memset(e->watch_value, 0, sizeof e->watch_value);
    snprintf(e->feedback, sizeof e->feedback, "Starting debugger; source is read-only until stopped");
    tiku_ide_console_open(st, e->path, board[0] ? board : "This computer");
}

/** @brief Send a debugger command such as DEBUG STEP or DEBUG CONT while
 *         the run is paused; a step or continue marks it running. */
void
tiku_ide_debug_command(ide_t *st, const char *command)
{
    int ok;
    if (!st->debugging || !st->debug_paused || !st->running) { return; }
    ok = st->local_debug ? tiku_board_basic_input(st->local_debug, command) == 0 :
         st->services->board_input && st->services->board_input(st->services->ctx, command);
    if (ok && (!strcmp(command, "DEBUG STEP") || !strcmp(command, "DEBUG CONT"))) {
        st->debug_paused = 0;
    }
}

/** @brief A [TDBG VAR], [TDBG MISSING] or [TDBG STR] line as "NAME =
 *         value" in @p out; a string's hex bytes are decoded, and bytes
 *         that do not print are shown as \xHH. */
static void
debug_value(char *out, size_t max, const char *line)
{
    char name[16], value[80];
    size_t used;
    if (sscanf(line, "[TDBG VAR %15s %79[^]]", name, value) == 2) {
        snprintf(out, max, "%s = %s", name, value); return;
    }
    if (sscanf(line, "[TDBG MISSING %15[^]]", name) == 1) {
        snprintf(out, max, "%s = <not defined>", name); return;
    }
    if (sscanf(line, "[TDBG STR %15s", name) != 1) { out[0] = '\0'; return; }
    {
        const char *hex = strchr(line + 10, ' ');
        if (!hex) { out[0] = '\0'; return; }
        hex++;
        used = (size_t)snprintf(out, max, "%s = \"", name);
        while (hex[0] && hex[0] != ']' && hex[0] != '+' && hex[1] && used + 7 < max) {
            unsigned byte;
            if (sscanf(hex, "%2x", &byte) != 1) { break; }
            if (byte >= 32 && byte < 127 && byte != '"' && byte != '\\') { out[used++] = (char)byte; }
            else { used += (size_t)snprintf(out + used, max - used, "\\x%02X", byte); }
            hex += 2;
        }
        snprintf(out + used, max - used, "%s\"", *hex != ']' ? "..." : "");
    }
}

/** @brief Take one [TDBG ...] protocol line of a debug run: paused, ready,
 *         running, an error, or a watch value.  @return 0 when no debug
 *         run is going or the line is not a protocol line. */
int
tiku_ide_debug_line(ide_t *st, const char *line)
{
    edit_t *e = tiku_ide_edit_of_path(st, st->child_file);
    unsigned pc;
    if (!st->debugging || strncmp(line, "[TDBG ", 6)) { return 0; }
    if (sscanf(line, "[TDBG PAUSED %u]", &pc) == 1) {
        st->debug_pc = (int)pc; st->debug_values = 0;
        if (e) { memset(e->watch_value, 0, sizeof e->watch_value); }
    } else if (!strcmp(line, "[TDBG READY]")) {
        st->debug_paused = st->debug_pc != 0;
        if (e && tiku_editdoc_state(e->doc) != st->debug_state) {
            /* The run was given other text than the page holds now: its
             * line numbers would point at the wrong lines. */
            tiku_editdoc_execution_clear(e->doc);
            snprintf(e->feedback, sizeof e->feedback,
                     "Paused before BASIC line %d; "
                     "the source changed since the run started",
                     st->debug_pc);
        } else if (e) {
            int64_t y = st->debug_pc ?
                tiku_ide_line_of_number(e, st->debug_pc) : -1;
            tiku_edit_pos_t caret = tiku_ide_caret(e, NULL, NULL);

            tiku_editdoc_execution_clear(e->doc);
            if (y >= 0) {
                (void)tiku_editdoc_execution_set(e->doc, y);
                tiku_ide_place(e, y, 0);
            } else {
                (void)tiku_codeview_select(e->view, caret, caret);
            }
            tiku_ide_edit_reveal(e);
            snprintf(e->feedback, sizeof e->feedback, st->debug_pc ?
                     "Paused before BASIC line %d (green). Source locked." :
                     "Debugger finished at line %d", st->debug_pc);
        }
    } else if (!strcmp(line, "[TDBG RUNNING]")) {
        st->debug_paused = 0;
        if (e) { tiku_editdoc_execution_clear(e->doc); }
    } else if (!strncmp(line, "[TDBG ERROR ", 12)) {
        if (e) { snprintf(e->feedback, sizeof e->feedback, "Debugger: %.100s", line + 6); }
    } else if (e && st->debug_values < 8 &&
               (!strncmp(line, "[TDBG VAR ", 10) ||
                !strncmp(line, "[TDBG STR ", 10) ||
                !strncmp(line, "[TDBG MISSING ", 14))) {
        char *out = e->watch_value[st->debug_values++];
        debug_value(out, 112, line);
    }
    if (e) { tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); }
    return 1;
}

/** @brief A line the local interpreter printed: a protocol line goes to
 *         tiku_ide_debug_line(), any other line to the console. */
static void
debug_local_line(const char *line, void *ctx)
{
    ide_t *st = ctx;
    if (!tiku_ide_debug_line(st, line)) { tiku_ide_console_take(st, line, 0); }
}

/** @brief End the debugging state, stopping the local interpreter when
 *         there is one. */
void
tiku_ide_debug_cleanup(ide_t *st)
{
    if (st->local_debug) {
        tiku_board_basic_free(st->local_debug); st->local_debug = NULL;
        close(st->debug_socket); st->debug_socket = -1;
        kill(st->child, SIGTERM); waitpid(st->child, NULL, 0);
    }
    st->debugging = st->debug_paused = 0;
}

/** @brief Pump the local debug run: pass its output on, take up its INPUT
 *         prompt, and end the run when the interpreter stops. */
void
tiku_ide_debug_tick(ide_t *st)
{
    tiku_board_basic_state_t state;
    edit_t *e = tiku_ide_edit_of_path(st, st->child_file);
    if (!st->local_debug) { return; }
    state = tiku_board_basic_pump(st->local_debug, 0, debug_local_line, st);
    if (state == TIKU_BOARD_BASIC_ASKING && !st->asking) {
        st->asking = 1;
        snprintf(st->ask_prompt, sizeof st->ask_prompt, "%s",
                 tiku_board_basic_prompt(st->local_debug));
        tiku_field_start(&st->answer, "");
    }
    if (state >= TIKU_BOARD_BASIC_ENDED || st->debug_eof) {
        int failed = state == TIKU_BOARD_BASIC_FAILED || st->debug_eof;
        tiku_ide_debug_cleanup(st); st->running = st->asking = 0;
        tiku_ide_console_take(st, failed ? "Debugger failed (check interpreter version)" : "Debug run ended", 1);
        if (e) {
            snprintf(e->feedback, sizeof e->feedback, "%s", failed ?
                     "Debug failed: check the local interpreter version" : "Debug run ended");
        }
        tiku_ide_project_publish(st);
    }
    if (e) { tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); }
    tiku_ide_console_paint(st);
}

/** @brief The rectangle of debug-panel button @p i: Start, Step,
 *         Continue, Stop, Break and Hide, left to right. */
static tiku_rect_t
debug_button(const edit_t *e, int i)
{
    int w = (e->w - 12) / 6;
    return (tiku_rect_t){ 6 + w * i, tiku_ide_edit_page_bottom(e) + 4, w - 3, 23 };
}

/** @brief Paint the debug panel under page @p e: its buttons, the watch
 *         field and up to eight watch values. */
void
tiku_ide_debug_paint(ide_t *st, edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, tiku_ide_edit_page_bottom(e), e->w, DEBUG_H };
    static const char *const label[] = { "Start", "Step", "Continue", "Stop", "Break", "Hide" };
    int i, active = tiku_ide_debug_locked(st, e);
    tiku_fill(e->surface, r, TIKU_C_PANEL);
    tiku_hline(e->surface, 0, r.y, e->w, TIKU_C_TEXT);
    for (i = 0; i < 6; i++) {
        int enabled = i == 0 ? !st->running : i == 1 || i == 2 ? active && st->debug_paused :
                      i == 3 ? active : i == 4 ? !st->running : 1;
        tiku_ui_button(e->surface, debug_button(e, i), label[i], enabled ? 0 : TIKU_S_DISABLED);
    }
    r = (tiku_rect_t){ 60, r.y + 31, e->w - 68, 21 };
    tiku_text(e->surface, f, 6, r.y + f->ascent + 2, "Watch:", TIKU_C_TEXT);
    tiku_ui_textfield_sel(e->surface, r, e->watches.text,
                          e->watch_focus ? e->watches.caret : -1,
                          e->watches.sel_a, e->watches.sel_b,
                          st->running ? TIKU_S_DISABLED : 0);
    for (i = 0; i < 8; i++) {
        char text[112];
        snprintf(text, sizeof text, "%s", e->watch_value[i]);
        while (strlen(text) && tiku_text_width(f, text) > e->w / 2 - 12) {
            text[strlen(text) - 1] = '\0';
        }
        tiku_text(e->surface, f, 6 + (i / 4) * e->w / 2,
                  r.y + 26 + (i % 4) * 16 + f->ascent,
                  text, TIKU_C_TEXT);
    }
}

/** @brief A press on page @p e's debug panel, or a key for its watch
 *         field.  @return 1 when the panel took the event. */
int
tiku_ide_debug_event(ide_t *st, edit_t *e, const tiku_event_t *event)
{
    int i;
    if (!e->debug_show) { return 0; }
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        if (event->y < tiku_ide_edit_page_bottom(e) || event->y >= e->h - STRIP_H) {
            e->watch_focus = 0; return 0;
        }
        for (i = 0; i < 6; i++) {
            tiku_rect_t r = debug_button(e, i);
            if (event->x < r.x || event->x >= r.x + r.w || event->y < r.y || event->y >= r.y + r.h) { continue; }
            if (i == 0) { tiku_ide_debug_start(st, e); }
            if ((i == 1 || i == 2) && tiku_ide_debug_locked(st, e)) {
                tiku_ide_debug_command(st, i == 1 ? "DEBUG STEP" : "DEBUG CONT");
            }
            if (i == 3 && tiku_ide_debug_locked(st, e)) { tiku_ide_run_stop(st, "? debug run stopped"); }
            if (i == 4 && !st->running) { tiku_ide_debug_break(e); }
            if (i == 5) { e->debug_show = e->watch_focus = 0; }
            return 1;
        }
        if (!st->running && event->y < tiku_ide_edit_page_bottom(e) + 54) {
            e->watch_focus = 1;
            tiku_field_key(&e->watches, TIKU_FIELD_SELECT_ALL, 0, 0);
        }
        return 1;
    }
    if (event->type == TIKU_EVENT_KEY_DOWN && e->watch_focus && !st->running) {
        unsigned ch = event->text[0] ? (unsigned char)event->text[0] : event->key;
        if (event->key == TIKU_KEY_RETURN || event->key == TIKU_KEY_ESCAPE) { e->watch_focus = 0; }
        else if (event->key == TIKU_KEY_BACKSPACE) { tiku_field_key(&e->watches, TIKU_FIELD_BACKSPACE, 0, 0); }
        else if (event->key == TIKU_KEY_LEFT) { tiku_field_key(&e->watches, TIKU_FIELD_LEFT, 0, 0); }
        else if (event->key == TIKU_KEY_RIGHT) { tiku_field_key(&e->watches, TIKU_FIELD_RIGHT, 0, 0); }
        else if (!(event->modifiers & TIKU_MOD_CMD) && ch >= 32 && ch < 127) {
            tiku_field_key(&e->watches, TIKU_FIELD_CHAR, 0, (char)ch);
        }
        return 1;
    }
    return 0;
}
