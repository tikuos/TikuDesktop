/* A line debugger carried by the existing BASIC run/input stream.
 * No extra serial owner and no evaluated watch expressions. */
static int
debug_locked(ide_t *st, edit_t *e)
{
    return st->running && st->debugging && !strcmp(st->child_file, e->path);
}

static void
debug_show(edit_t *e)
{
    e->debug_show = 1;
    e->assist_open = e->finding = 0;
    if (!e->watches.active) { tiku_field_start(&e->watches, "A,B,C"); }
}

static void
debug_break(edit_t *e)
{
    int y, x, i, empty = -1;
    long line;
    const char *p;
    char *end;
    tiku_textview_caret(&e->tv, &y, &x);
    p = tiku_textview_line(&e->tv, y);
    line = strtol(p, &end, 10);
    if (p == end || line <= 0 || line >= 65534) { return; }
    for (i = 0; i < 16; i++) {
        if (e->breaks[i] == line) { e->breaks[i] = 0; return; }
        if (!e->breaks[i]) { empty = i; }
    }
    if (empty >= 0) { e->breaks[empty] = (uint16_t)line; }
    else { snprintf(e->feedback, sizeof e->feedback, "Maximum 16 breakpoints"); }
}

/** Watch names only: eight scalars, seven letters/digits/underscores plus $. */
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

static int
debug_send(void *ctx, const void *bytes, size_t n)
{
    ide_t *st = ctx;
    ssize_t sent = send(st->debug_socket, bytes, n, MSG_NOSIGNAL);
    return sent == (ssize_t)n ? 0 : -1;
}

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

static void
debug_start(ide_t *st, edit_t *e)
{
    char program[8192], source[8192], watches[256], board[64];
    size_t used;
    int i;
    if (st->running || e->lang != TIKU_SYNTAX_BASIC) { return; }
    debug_show(e);
    if (!debug_watch_setup(e, watches, sizeof watches)) {
        snprintf(e->feedback, sizeof e->feedback, "Watch up to 8 scalar names (7 characters plus $)");
        return;
    }
    for (i = 0; i < e->tv.nline; i++) {
        const char *p = tiku_textview_line(&e->tv, i);
        char *end;
        long n;
        while (*p == ' ' || *p == '\t') { p++; }
        if (!*p) { continue; }
        n = strtol(p, &end, 10);
        if (end == p || n <= 0 || n >= 65534) {
            snprintf(e->feedback, sizeof e->feedback, "Debug requires numbered BASIC source lines");
            return;
        }
        while (*end == ' ' || *end == '\t') { end++; }
        if (strlen(end) >= 144) {
            snprintf(e->feedback, sizeof e->feedback, "Debug source line %ld exceeds the portable 143-byte body limit", n);
            return;
        }
    }
    if (!edit_save(e) || !upload_read(st, e->path, source, sizeof source)) { return; }
    used = (size_t)snprintf(program, sizeof program, "DEBUG ON\n%s", watches);
    for (i = 0; i < 16; i++) {
        if (e->breaks[i]) {
            if (line_of_number(&e->tv, e->breaks[i]) < 0) {
                snprintf(e->feedback, sizeof e->feedback,
                         "Breakpoint line %u no longer exists; clear breakpoints first", e->breaks[i]);
                return;
            }
            used += (size_t)snprintf(program + used, sizeof program - used,
                                     "DEBUG BREAK %u\n", e->breaks[i]);
        }
    }
    if (used + strlen(source) >= sizeof program) {
        snprintf(e->feedback, sizeof e->feedback, "Program plus debug setup exceeds 8191 bytes");
        return;
    }
    strcpy(program + used, source);
    board[0] = '\0';
    if (dest_board(st, board, sizeof board)) {
        if (!board_connected(st, board) || !st->services->board_run ||
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
    st->child_numbered = 1; st->run_from = 0;
    st->stop_asked = 0;
    tiku_textview_select_none(&e->tv);
    memset(e->watch_value, 0, sizeof e->watch_value);
    snprintf(e->feedback, sizeof e->feedback, "Starting debugger; source is read-only until stopped");
    console_open(st, e->path, board[0] ? board : "This computer");
}

static void
debug_command(ide_t *st, const char *command)
{
    int ok;
    if (!st->debugging || !st->debug_paused || !st->running) { return; }
    ok = st->local_debug ? tiku_board_basic_input(st->local_debug, command) == 0 :
         st->services->board_input && st->services->board_input(st->services->ctx, command);
    if (ok && (!strcmp(command, "DEBUG STEP") || !strcmp(command, "DEBUG CONT"))) {
        st->debug_paused = 0;
    }
}

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

/** Protocol lines are handled only for an explicit debug run. Strings are hex. */
static int
debug_line(ide_t *st, const char *line)
{
    edit_t *e = edit_of_path(st, st->child_file);
    unsigned pc;
    if (!st->debugging || strncmp(line, "[TDBG ", 6)) { return 0; }
    if (sscanf(line, "[TDBG PAUSED %u]", &pc) == 1) {
        st->debug_pc = (int)pc; st->debug_values = 0;
        if (e) { memset(e->watch_value, 0, sizeof e->watch_value); }
    } else if (!strcmp(line, "[TDBG READY]")) {
        st->debug_paused = st->debug_pc != 0;
        if (e) {
            int y = line_of_number(&e->tv, st->debug_pc);
            tiku_textview_select_none(&e->tv);
            if (y >= 0) { tiku_textview_place(&e->tv, y, 0); }
            tiku_textview_reveal(&e->tv, edit_rows(e));
            snprintf(e->feedback, sizeof e->feedback, st->debug_pc ?
                     "Paused before BASIC line %d (green). Source locked." :
                     "Debugger finished at line %d", st->debug_pc);
        }
    } else if (!strcmp(line, "[TDBG RUNNING]")) {
        st->debug_paused = 0;
    } else if (!strncmp(line, "[TDBG ERROR ", 12)) {
        if (e) { snprintf(e->feedback, sizeof e->feedback, "Debugger: %.100s", line + 6); }
    } else if (e && st->debug_values < 8 &&
               (!strncmp(line, "[TDBG VAR ", 10) ||
                !strncmp(line, "[TDBG STR ", 10) ||
                !strncmp(line, "[TDBG MISSING ", 14))) {
        char *out = e->watch_value[st->debug_values++];
        debug_value(out, 112, line);
    }
    if (e) { edit_paint(st, e); edit_publish(st, e); }
    return 1;
}

static void
debug_local_line(const char *line, void *ctx)
{
    ide_t *st = ctx;
    if (!debug_line(st, line)) { console_take(st, line, 0); }
}

static void
debug_cleanup(ide_t *st)
{
    if (st->local_debug) {
        tiku_board_basic_free(st->local_debug); st->local_debug = NULL;
        close(st->debug_socket); st->debug_socket = -1;
        kill(st->child, SIGTERM); waitpid(st->child, NULL, 0);
    }
    st->debugging = st->debug_paused = 0;
}

static void
debug_tick(ide_t *st)
{
    tiku_board_basic_state_t state;
    edit_t *e = edit_of_path(st, st->child_file);
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
        debug_cleanup(st); st->running = st->asking = 0;
        console_take(st, failed ? "Debugger failed (check interpreter version)" : "Debug run ended", 1);
        if (e) {
            snprintf(e->feedback, sizeof e->feedback, "%s", failed ?
                     "Debug failed: check the local interpreter version" : "Debug run ended");
        }
        project_publish(st);
    }
    if (e) { edit_paint(st, e); edit_publish(st, e); }
    console_paint(st);
}

static tiku_rect_t
debug_button(const edit_t *e, int i)
{
    int w = (e->w - 12) / 6;
    return (tiku_rect_t){ 6 + w * i, edit_page_bottom(e) + 4, w - 3, 23 };
}

static void
debug_paint(ide_t *st, edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, edit_page_bottom(e), e->w, DEBUG_H };
    static const char *const label[] = { "Start", "Step", "Continue", "Stop", "Break", "Hide" };
    int i, active = debug_locked(st, e);
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

static int
debug_event(ide_t *st, edit_t *e, const tiku_event_t *event)
{
    int i;
    if (!e->debug_show) { return 0; }
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        if (event->y < edit_page_bottom(e) || event->y >= e->h - STRIP_H) {
            e->watch_focus = 0; return 0;
        }
        for (i = 0; i < 6; i++) {
            tiku_rect_t r = debug_button(e, i);
            if (event->x < r.x || event->x >= r.x + r.w || event->y < r.y || event->y >= r.y + r.h) { continue; }
            if (i == 0) { debug_start(st, e); }
            if ((i == 1 || i == 2) && debug_locked(st, e)) {
                debug_command(st, i == 1 ? "DEBUG STEP" : "DEBUG CONT");
            }
            if (i == 3 && debug_locked(st, e)) { run_stop(st, "? debug run stopped"); }
            if (i == 4 && !st->running) { debug_break(e); }
            if (i == 5) { e->debug_show = e->watch_focus = 0; }
            return 1;
        }
        if (!st->running && event->y < edit_page_bottom(e) + 54) {
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
