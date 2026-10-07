/*
 * TikuOS applications -- test_ide_page.c: the BASIC/C IDE's page on the
 * editor kit.  Files keep their bytes; BASIC's limits are checked where a
 * program leaves the page; breakpoints are line markers that follow their
 * lines and are checked against BASIC numbers when a debug run starts; the
 * paused line is marked only for the text the run was given; Renumber
 * edits only the numbers it changes; completion and keyword help are a
 * list and a tip at the caret; the page keeps the view's caret and
 * clipboard rules; a Command click on a jump target goes to its line;
 * Return over a selection numbers the new line from the selection's first
 * line; and the page's scrollbar pages by the thumb's side and drags.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "../../apps/ide/tiku_ide_private.h"
#include "tiku_host.h"
#include "tiku_msg.h"
#include "tiku_remote.h"

static const char *selected = "";
static int runs, refuse_clip;
static char run_program[8192], desk_clip[4096];
static unsigned menu_publications[512];

static int current(void *ctx, char *out, size_t max)
{ (void)ctx; return snprintf(out, max, "%s", selected); }
static int names(void *ctx, char *out, size_t max)
{ (void)ctx; return snprintf(out, max, "board-a\n"); }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    (void)ctx; (void)set;
    if (id < 512) menu_publications[id]++;
    return 0;
}
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; return 0; }
static uint32_t opened(void *ctx, const char *title, int w, int h)
{
    static uint32_t next = 10;

    (void)ctx; (void)title; (void)w; (void)h;
    return next++;
}
static void closed(void *ctx, uint32_t id) { (void)ctx; (void)id; }
static int run(void *ctx, const char *board, const char *text)
{
    (void)ctx; (void)board;
    runs++;
    snprintf(run_program, sizeof run_program, "%s", text);
    return 1;
}
static int save(void *ctx, const char *board, const char *name,
                const char *text)
{ (void)ctx; (void)board; (void)name; (void)text; return 1; }
static int input(void *ctx, const char *line)
{ (void)ctx; (void)line; return 1; }
static int clip_set(void *ctx, const char *text)
{
    (void)ctx;
    if (refuse_clip) { return -1; }
    snprintf(desk_clip, sizeof desk_clip, "%s", text);
    return 0;
}
static const char *clip_text(void *ctx, unsigned *stamp)
{ (void)ctx; if (stamp) { *stamp = 0; } return desk_clip; }

/* The pointer shape last asked for. */
static int shape_asked = -1;
static int send_message(void *ctx, const struct tiku_msg *m)
{
    int32_t shape = -1;

    (void)ctx;
    if (tiku_msg_what(m) == TIKU_MSG_CURSOR &&
        tiku_msg_find_int32(m, "shape", 0, &shape)) {
        shape_asked = shape;
    }
    return 1;
}

static tiku_app_services_t services = {
    .current_device = current, .board_names = names, .board_run = run,
    .board_save = save, .board_input = input, .menus = menus,
    .frame = frame, .open = opened,
    .close = closed, .clip_set = clip_set, .clip_text = clip_text,
    .send_message = send_message
};
static char dir[] = "/tmp/tikuide-page-XXXXXX";

static void file_write(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb");

    assert(f && fwrite(text, 1, n, f) == n && fclose(f) == 0);
}

/* The file at @p path, which must be @p want exactly. */
static int file_is(const char *path, const char *want, size_t n)
{
    char got[512];
    FILE *f = fopen(path, "rb");
    size_t k;

    assert(f);
    k = fread(got, 1, sizeof got, f);
    fclose(f);
    return k == n && memcmp(got, want, n) == 0;
}

/* The page's whole text is @p want, @p n bytes. */
static int holds(const edit_t *e, const char *want, size_t n)
{
    tiku_edit_range_t all = { 0, tiku_editdoc_length(e->doc) };
    char got[4096];
    size_t k = 0;

    return tiku_editdoc_read(e->doc, all, got, sizeof got, &k) == TIKU_EDIT_OK
           && k == n && memcmp(got, want, n) == 0;
}

/* Line @p line of the page, in a buffer the next call reuses. */
static const char *line_of(const edit_t *e, int64_t line)
{
    static char *held;

    free(held);
    held = tiku_ide_line_dup(e, line, NULL);
    return held != NULL ? held : "";
}

/* An IDE with a Project window and no project. */
static ide_t *ide(void)
{
    ide_t *st = calloc(1, sizeof *st);

    assert(st);
    st->services = &services;
    st->proj_id = 1;
    st->proj_w = IDE_W;
    st->proj_h = IDE_H;
    st->proj_surface = tiku_surface_new(IDE_W, IDE_H, TIKU_C_PANEL);
    tiku_list_init(&st->tv_list, 1);
    tiku_list_init(&st->msg_list, 1);
    return st;
}

/* A page on the file @p name, written with @p n bytes of @p text. */
static edit_t *page(ide_t *st, const char *name, const char *text, size_t n)
{
    prow_t row;
    edit_t *e;

    memset(&row, 0, sizeof row);
    snprintf(row.path, sizeof row.path, "%s/%s", dir, name);
    snprintf(row.name, sizeof row.name, "%s", name);
    file_write(row.path, text, n);
    tiku_ide_edit_open(st, &row);
    e = tiku_ide_edit_of_path(st, row.path);
    assert(e != NULL);
    return e;
}

/* Close the page without a question, its draft discarded. */
static void unpage(ide_t *st, edit_t *e)
{
    tiku_ide_recovery_release(e, 1);
    tiku_editdoc_saved(e->doc, tiku_editdoc_state(e->doc));
    tiku_ide_edit_drop(st, e);
    assert(e->surface == NULL);
}

/* A key on the page. */
static void key(ide_t *st, edit_t *e, unsigned k, unsigned mods,
                const char *text)
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = k;
    ev.modifiers = mods;
    if (text != NULL) { snprintf(ev.text, sizeof ev.text, "%s", text); }
    (void)tiku_ide_event(st, e->id, &ev);
}

/* A press of @p button at @p x, @p y at @p when microseconds, let go. */
static void click(ide_t *st, edit_t *e, int x, int y, int button,
                  unsigned mods, int64_t when)
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.x = x;
    ev.y = y;
    ev.button = button;
    ev.modifiers = mods;
    ev.time_us = when;
    (void)tiku_ide_event(st, e->id, &ev);
    ev.type = TIKU_EVENT_POINTER_UP;
    (void)tiku_ide_event(st, e->id, &ev);
}

/* The selection's ends in order. */
static void selection(const edit_t *e, tiku_edit_pos_t *lo, tiku_edit_pos_t *hi)
{
    tiku_edit_pos_t a = 0, c = 0;

    (void)tiku_codeview_selection(e->view, &a, &c);
    *lo = a < c ? a : c;
    *hi = a < c ? c : a;
}

/** Files keep their bytes; Save ends an unended last line with the
 *  file's own break; Return types that break. */
static void kept_bytes(ide_t *st)
{
    static const char crlf[] = "10 PRINT 1\r\n20 REM \f\r\n30 END\r\n";
    edit_t *e = page(st, "crlf.bas", crlf, sizeof crlf - 1);
    char path[600];

    assert(!e->load_failed && holds(e, crlf, sizeof crlf - 1));
    assert(!strcmp(e->newline, "\r\n"));
    /* The caret stands at the end of the last line with text. */
    assert(tiku_ide_caret(e, NULL, NULL) == (tiku_edit_pos_t)sizeof crlf - 3);
    tiku_ide_place(e, 0, 10);
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(holds(e, "10 PRINT 1\r\n20 \r\n20 REM \f\r\n30 END\r\n", 35));
    /* The break, its number and what is typed after them are one step. */
    key(st, e, 'X', 0, "X");
    key(st, e, 'z', TIKU_MOD_CMD, "z");
    assert(holds(e, crlf, sizeof crlf - 1));
    key(st, e, 'z', TIKU_MOD_CMD | TIKU_MOD_SHIFT, "Z");
    assert(holds(e, "10 PRINT 1\r\n20 X\r\n20 REM \f\r\n30 END\r\n", 36));
    key(st, e, TIKU_KEY_BACKSPACE, 0, NULL);
    key(st, e, 's', TIKU_MOD_CMD, "s");
    snprintf(path, sizeof path, "%s/crlf.bas", dir);
    assert(file_is(path, "10 PRINT 1\r\n20 \r\n20 REM \f\r\n30 END\r\n", 35));
    unpage(st, e);

    e = page(st, "cr.bas", "10 A\r20 B", 9);
    assert(!strcmp(e->newline, "\r"));
    key(st, e, 'x', 0, "x");
    key(st, e, 's', TIKU_MOD_CMD, "s");
    snprintf(path, sizeof path, "%s/cr.bas", dir);
    assert(file_is(path, "10 A\r20 Bx\r", 11));
    unpage(st, e);

    e = page(st, "nul.bas", "10 A\0", 5);
    assert(e->load_failed && tiku_editdoc_length(e->doc) == 0);
    assert(strstr(e->feedback, "could not be loaded safely"));
    assert(!tiku_ide_edit_save(e));
    unpage(st, e);
}

/** BASIC's limits are checked where a program leaves the page. */
static void basic_limits(ide_t *st)
{
    char text[400];
    edit_t *e;
    int before;

    /* A control character, on a local run: refused before the run. */
    selected = "";
    e = page(st, "ctl.bas", "10 PRINT 1\n20 REM \x01\n", 20);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(!st->running);
    assert(strstr(st->note, "Line 2 holds a control character (^A)"));
    assert(strstr(e->feedback, "Line 2 holds a control character"));
    unpage(st, e);

    /* On a board: a line longer than BASIC reads, and one with more
     * after its number than BASIC keeps. */
    selected = "board-a";
    before = runs;
    memset(text, 'x', 160);
    text[160] = '\n';
    e = page(st, "wide.bas", text, 161);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(runs == before &&
           strstr(st->note, "Line 1 is longer than the 159 bytes"));
    unpage(st, e);
    memcpy(text, "10 ", 3);
    memset(text + 3, 'x', 144);
    text[147] = '\n';
    e = page(st, "body.bas", text, 148);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(runs == before &&
           strstr(st->note, "more than the 143 bytes BASIC keeps"));
    /* 143 bytes after the number go. */
    tiku_ide_place(e, 0, 4);
    key(st, e, TIKU_KEY_BACKSPACE, 0, NULL);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(runs == before + 1 && st->running);
    st->running = 0;
    st->board[0] = '\0';
    unpage(st, e);

    /* An unnumbered line of 159 bytes goes, CRLF counting as one break. */
    memset(text, 'y', 159);
    memcpy(text + 159, "\r\n10 END\r\n", 10);
    e = page(st, "loose.bas", text, 169);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(runs == before + 2);
    st->running = 0;
    st->board[0] = '\0';
    unpage(st, e);

    /* Lines are counted across CRLF as one break each, and a tab is
     * ordinary text. */
    e = page(st, "count.bas", "10 A\r\n20 B\r\n30 \x01\r\n", 18);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(runs == before + 2 && strstr(st->note, "Line 3 holds"));
    unpage(st, e);
    e = page(st, "tab.bas", "10 PRINT\t1\n", 11);
    key(st, e, 'r', TIKU_MOD_CMD, "r");
    assert(runs == before + 3);
    st->running = 0;
    st->board[0] = '\0';
    unpage(st, e);

    /* Saving on a board, and a debug run, are refused the same way. */
    e = page(st, "up.bas", "10 PRINT 1\n20 REM \x7f\n", 20);
    tiku_ide_save_file_on_board(st, e->path);
    assert(!st->ask.open && strstr(st->note, "control character (^?)"));
    tiku_ide_debug_start(st, e);
    assert(!st->running && strstr(e->feedback, "control character (^?)"));
    unpage(st, e);
    selected = "";
}

/** Breakpoints mark lines, follow them, and are checked against BASIC
 *  numbers when debugging starts. */
static void breakpoints(ide_t *st)
{
    edit_t *e = page(st, "brk.bas", "10 A=1\n20 B=2\n30 C=3\n40 END\n", 28);
    uint32_t brk = 1u << IDE_MARK_BREAK;
    int i;

    tiku_ide_place(e, 1, 0);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    assert(tiku_editdoc_markers_on(e->doc, 1) == brk);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    assert(tiku_editdoc_markers_next(e->doc, 0, brk) == -1);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    /* A line typed above moves the breakpoint down with its line. */
    tiku_ide_place(e, 0, 6);
    assert(tiku_codeview_insert(e->view, "\n15 X", 5, 1) == TIKU_EDIT_OK);
    assert(!strcmp(line_of(e, 1), "15 X") && !strcmp(line_of(e, 2), "20 B=2"));
    assert(tiku_editdoc_markers_next(e->doc, 0, brk) == 2);
    /* Renumber keeps it on its line. */
    tiku_ide_pick(st, e->id, CMD_RENUM);
    assert(!strcmp(line_of(e, 2), "30 B=2"));
    assert(tiku_editdoc_markers_next(e->doc, 0, brk) == 2);
    /* A line without a number takes none. */
    tiku_ide_place(e, 5, 0);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    assert(tiku_editdoc_markers_on(e->doc, 5) == 0);
    tiku_ide_pick(st, e->id, CMD_CLEARBREAKS);
    assert(tiku_editdoc_markers_next(e->doc, 0, brk) == -1);
    unpage(st, e);

    {
        char many[400];
        size_t n = 0;

        for (i = 1; i <= 17; i++) {
            n += (size_t)snprintf(many + n, sizeof many - n, "%d REM\n",
                                  i * 10);
        }
        e = page(st, "many.bas", many, n);
        for (i = 0; i < 17; i++) {
            tiku_ide_place(e, i, 0);
            tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
        }
        assert(tiku_editdoc_markers_on(e->doc, 15) == brk &&
               tiku_editdoc_markers_on(e->doc, 16) == 0);
        assert(strstr(e->feedback, "Maximum 16 breakpoints"));
        unpage(st, e);
    }

    /* At debug start: a marked line that lost its number refuses, and so
     * does one a later line of the same number replaces. */
    selected = "board-a";
    e = page(st, "check.bas", "10 A\n20 B\n20 C\n30 D\n", 20);
    tiku_ide_place(e, 3, 0);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    assert(tiku_codeview_select(e->view, tiku_editdoc_line_start(e->doc, 3),
                                tiku_editdoc_line_end(e->doc, 3)) ==
           TIKU_EDIT_OK);
    key(st, e, TIKU_KEY_BACKSPACE, 0, NULL);
    assert(!strcmp(line_of(e, 3), "") &&
           tiku_editdoc_markers_on(e->doc, 3) == brk);
    tiku_ide_debug_start(st, e);
    assert(!st->running &&
           strstr(e->feedback, "Breakpoint on line 4 has no BASIC number"));
    tiku_ide_pick(st, e->id, CMD_CLEARBREAKS);
    tiku_ide_place(e, 1, 0);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    tiku_ide_debug_start(st, e);
    assert(strstr(e->feedback,
                  "Breakpoint on line 2 is not run: a later line is also 20"));
    tiku_ide_pick(st, e->id, CMD_CLEARBREAKS);
    tiku_ide_place(e, 2, 0);
    tiku_ide_pick(st, e->id, CMD_BREAKPOINT);
    run_program[0] = '\0';
    tiku_ide_debug_start(st, e);
    assert(strstr(run_program, "DEBUG ON\n") &&
           strstr(run_program, "DEBUG BREAK 20\n"));
    assert(!strstr(run_program, "DEBUG BREAK 10") && st->running &&
           st->debugging);
    st->running = st->debugging = 0;
    st->board[0] = '\0';
    unpage(st, e);
    selected = "";
}

/** The paused line is marked for the text the run was given, and the
 *  source refuses changes while the run holds it. */
static void execution_line(ide_t *st)
{
    edit_t *e = page(st, "exec.bas", "10 A=1\n20 B=2\n30 C=3\n", 21);
    int64_t line = -1;
    int y;

    st->running = st->debugging = 1;
    snprintf(st->child_file, sizeof st->child_file, "%s", e->path);
    snprintf(st->board, sizeof st->board, "board-a");
    st->debug_state = tiku_editdoc_state(e->doc);
    tiku_ide_debug_line(st, "[TDBG PAUSED 20]");
    tiku_ide_debug_line(st, "[TDBG READY]");
    assert(tiku_editdoc_execution(e->doc, &line) == TIKU_EDIT_OK && line == 1);
    assert(tiku_ide_caret(e, NULL, NULL) == tiku_editdoc_line_start(e->doc, 1));
    assert(strstr(e->feedback, "Paused before BASIC line 20"));
    assert(tiku_editdoc_read_only(e->doc));
    key(st, e, 'q', 0, "q");
    assert(!strcmp(line_of(e, 1), "20 B=2"));
    tiku_ide_edit_paint(st, e);
    y = tiku_ide_edit_page_top(e) + MARGIN +
        tiku_codeview_row_height(e->view) + 2;
    assert((e->surface->px[y * e->w + e->w - PAGE_SB - 4] & 0xffffffu) ==
           (TIKU_RGB(211, 238, 185) & 0xffffffu));
    tiku_ide_debug_line(st, "[TDBG RUNNING]");
    assert(tiku_editdoc_execution(e->doc, &line) == TIKU_EDIT_NOTHING);
    /* A run given other text: no line is marked, and the page says so. */
    st->debug_state = 0;
    tiku_ide_debug_line(st, "[TDBG PAUSED 30]");
    tiku_ide_debug_line(st, "[TDBG READY]");
    assert(tiku_editdoc_execution(e->doc, &line) == TIKU_EDIT_NOTHING);
    assert(strstr(e->feedback, "the source changed since the run started"));
    /* The run over, the page is the person's again, unmarked. */
    st->debug_state = tiku_editdoc_state(e->doc);
    tiku_ide_debug_line(st, "[TDBG PAUSED 20]");
    tiku_ide_debug_line(st, "[TDBG READY]");
    assert(tiku_editdoc_execution(e->doc, &line) == TIKU_EDIT_OK);
    st->running = st->debugging = st->debug_paused = 0;
    st->board[0] = '\0';
    tiku_ide_edit_paint(st, e);
    assert(tiku_editdoc_execution(e->doc, &line) == TIKU_EDIT_NOTHING);
    key(st, e, 'q', 0, "q");
    assert(!tiku_editdoc_read_only(e->doc) &&
           !strcmp(line_of(e, 1), "q20 B=2"));
    unpage(st, e);
}

/** Renumber edits only what changes, so the caret and a selection stay
 *  on their text, and a program in tens records nothing. */
static void renumber_edits(ide_t *st)
{
    edit_t *e = page(st, "ren.bas", "5 A\n7 GOTO 23\n23 B\n", 19);
    tiku_edit_pos_t lo, hi;
    uint64_t state;

    assert(tiku_codeview_select(e->view, 4 + 2, 4 + 6) == TIKU_EDIT_OK);
    tiku_ide_pick(st, e->id, CMD_RENUM);
    assert(holds(e, "10 A\n20 GOTO 30\n30 B\n", 21));
    selection(e, &lo, &hi);
    assert(lo == 5 + 3 && hi == 5 + 7);
    assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK &&
           holds(e, "5 A\n7 GOTO 23\n23 B\n", 19));
    assert(tiku_codeview_redo(e->view) == TIKU_EDIT_OK);
    state = tiku_editdoc_state(e->doc);
    tiku_ide_pick(st, e->id, CMD_RENUM);
    assert(tiku_editdoc_state(e->doc) == state);
    unpage(st, e);
    /* A target no line wears keeps its value without leading zeros, and
     * lines sharing a number take the first one's new number. */
    e = page(st, "dup.bas", "5 A\n5 B\n7 GOTO 0090\n9 GOTO 5\n", 29);
    tiku_ide_pick(st, e->id, CMD_RENUM);
    assert(holds(e, "10 A\n10 B\n30 GOTO 90\n40 GOTO 10\n", 32));
    unpage(st, e);
}

/** Completion and keyword help: a list at the word, a tip beside it. */
static void assistance(ide_t *st)
{
    edit_t *e = page(st, "assist.bas", "10 p", 4);
    size_t current = 9;

    key(st, e, ' ', TIKU_MOD_CMD, " ");
    assert(e->assist_open == 2 && e->assist_n == 7);
    assert(tiku_codeview_completion_selected(e->view) == 0);
    assert(tiku_codeview_list_shown(e->view, &current) && current == 0);
    assert(tiku_codeview_tip_shown(e->view));
    key(st, e, TIKU_KEY_DOWN, 0, NULL);
    key(st, e, TIKU_KEY_DOWN, 0, NULL);
    key(st, e, TIKU_KEY_UP, 0, NULL);
    assert(tiku_codeview_list_shown(e->view, &current) && current == 1);
    assert(!strcmp(line_of(e, 0), "10 p"));
    key(st, e, 'r', 0, "r");
    tiku_ide_edit_paint(st, e);
    assert(e->assist_n == 1 && tiku_codeview_list_shown(e->view, &current) &&
           current == 0);
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(!strcmp(line_of(e, 0), "10 PRINT") && !e->assist_open);
    assert(!tiku_codeview_list_shown(e->view, NULL) &&
           !tiku_codeview_tip_shown(e->view));
    assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK &&
           !strcmp(line_of(e, 0), "10 pr"));
    /* Help: a list that only chooses, and Return is the page's. */
    tiku_ide_place(e, 0, 5);
    key(st, e, 'k', TIKU_MOD_CMD, "k");
    assert(e->assist_open == 1 && tiku_codeview_list_shown(e->view, NULL));
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(!strcmp(line_of(e, 1), "20 ") && e->assist_open == 1);
    {
        /* A press on a word of the help list chooses it and types
         * nothing. */
        int64_t x, y;
        size_t chosen = 9;

        assert(e->assist_n > 2);
        assert(tiku_codeview_point(e->view, tiku_ide_caret(e, NULL, NULL),
                                   &x, &y) == TIKU_EDIT_OK);
        click(st, e, (int)x, (int)y + tiku_codeview_row_height(e->view) + 1 +
              2 * (tiku_font_plain()->height + 4) + 3, 1, 0, 20000000);
        tiku_ide_edit_paint(st, e);
        assert(tiku_codeview_list_shown(e->view, &chosen) && chosen == 2);
        assert(!strcmp(line_of(e, 0), "10 pr") &&
               !strcmp(line_of(e, 1), "20 "));
        assert(e->assist_open == 1);
    }
    key(st, e, TIKU_KEY_ESCAPE, 0, NULL);
    assert(!e->assist_open && !tiku_codeview_list_shown(e->view, NULL) &&
           !tiku_codeview_tip_shown(e->view));
    key(st, e, ' ', TIKU_MOD_CMD, " ");
    key(st, e, 'f', TIKU_MOD_CMD, "f");
    assert(e->finding && !e->assist_open &&
           !tiku_codeview_list_shown(e->view, NULL));
    e->finding = 0;
    unpage(st, e);
    const char *call = "10 PRINT MID$(\"a,b\", MIN(1,2), 3)";
    e = page(st, "signature.bas", call, strlen(call));
    tiku_ide_place(e, 0, (int)(strstr(call, "MIN") - call));
    key(st, e, 'k', TIKU_MOD_CMD, "k");
    assert(e->assist_open == 3 && !tiku_codeview_list_shown(e->view, NULL));
    assert(tiku_codeview_signature_argument(e->view) == 1);
    tiku_ide_place(e, 0, (int)(strstr(call, "3)") - call));
    tiku_ide_edit_paint(st, e);
    assert(tiku_codeview_signature_argument(e->view) == 2);
    key(st, e, TIKU_KEY_ESCAPE, 0, NULL);
    assert(!e->assist_open && !tiku_codeview_tip_shown(e->view));
    unpage(st, e);
}

/** On a BASIC page an all-digit Find goes to the line wearing that
 *  number before it is looked for as text. */
static void find_by_number(ide_t *st)
{
    edit_t *e = page(st, "find.bas", "10 PRINT 20\n20 END\n", 19);
    tiku_edit_pos_t lo, hi;

    tiku_ide_place(e, 0, 0);
    tiku_ide_search_open(e, 0);
    tiku_field_start(&e->find, "20");
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    selection(e, &lo, &hi);
    assert(lo == 12 && hi == 14);
    e->finding = 0;
    unpage(st, e);
}

/** The page takes the view's caret rules. */
static void caret_rules(ide_t *st)
{
    edit_t *e = page(st, "caret.c", "int alpha = beta;\nreturn gamma;\n", 32);
    tiku_edit_pos_t lo, hi;
    int64_t x, y;

    assert(tiku_codeview_point(e->view, 6, &x, &y) == TIKU_EDIT_OK);
    click(st, e, (int)x, (int)y + 4, 1, 0, 1000000);
    click(st, e, (int)x, (int)y + 4, 1, 0, 1100000);
    selection(e, &lo, &hi);
    assert(lo == 4 && hi == 9);                 /* two clicks: "alpha" */
    tiku_ide_place(e, 0, 0);
    click(st, e, (int)x, (int)y + 4, 1, TIKU_MOD_SHIFT, 5000000);
    selection(e, &lo, &hi);
    assert(lo == 0 && hi == 6);                 /* Shift extends */
    tiku_ide_place(e, 0, 0);
    click(st, e, (int)x, (int)y + 4, 3, 0, 9000000);
    assert(tiku_ide_caret(e, NULL, NULL) == 0); /* only button 1 */
    key(st, e, TIKU_KEY_RIGHT, TIKU_MOD_CMD, NULL);
    assert(tiku_ide_caret(e, NULL, NULL) == 4); /* a word */
    key(st, e, TIKU_KEY_DOWN, TIKU_MOD_CMD, NULL);
    assert(tiku_editdoc_line_of(e->doc, tiku_ide_caret(e, NULL, NULL)) == 1);
    key(st, e, TIKU_KEY_UP, TIKU_MOD_OPTION, NULL);
    assert(tiku_editdoc_line_of(e->doc, tiku_ide_caret(e, NULL, NULL)) == 0);
    key(st, e, TIKU_KEY_END, TIKU_MOD_CMD, NULL);
    assert(tiku_ide_caret(e, NULL, NULL) == 32);
    /* Tab types nothing, and Return on a C page brings no number. */
    tiku_ide_place(e, 0, 17);
    key(st, e, TIKU_KEY_TAB, 0, "\t");
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(holds(e, "int alpha = beta;\n\nreturn gamma;\n", 33));
    unpage(st, e);
    e = page(st, "num.c", "10 + x;\n", 8);
    tiku_ide_place(e, 0, 7);
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(holds(e, "10 + x;\n\n", 9));
    unpage(st, e);
}

/** Copies carry LF, pastes take the page's break, and Cut keeps what the
 *  clipboard would not take. */
static void clipboard(ide_t *st)
{
    edit_t *e = page(st, "clip.bas", "10 A\r\n20 B\r\n", 12);

    assert(tiku_codeview_select(e->view, 0, 8) == TIKU_EDIT_OK);
    key(st, e, 'c', TIKU_MOD_CMD, "c");
    assert(!strcmp(desk_clip, "10 A\n20"));
    snprintf(desk_clip, sizeof desk_clip, "x\ny\x0c");
    tiku_ide_place(e, 1, 4);
    key(st, e, 'v', TIKU_MOD_CMD, "v");
    assert(holds(e, "10 A\r\n20 Bx\r\ny\x0c\r\n", 17));
    refuse_clip = 1;
    assert(tiku_codeview_select(e->view, 0, 4) == TIKU_EDIT_OK);
    key(st, e, 'x', TIKU_MOD_CMD, "x");
    assert(holds(e, "10 A\r\n20 Bx\r\ny\x0c\r\n", 17));
    assert(strstr(e->feedback, "more than the clipboard holds"));
    refuse_clip = 0;
    key(st, e, 'x', TIKU_MOD_CMD, "x");
    assert(holds(e, "\r\n20 Bx\r\ny\x0c\r\n", 13) &&
           !strcmp(desk_clip, "10 A"));
    unpage(st, e);
    e = page(st, "cronly.bas", "10 A\r20 B\r", 10);
    assert(tiku_codeview_select(e->view, 0, 7) == TIKU_EDIT_OK);
    key(st, e, 'c', TIKU_MOD_CMD, "c");
    assert(!strcmp(desk_clip, "10 A\n20"));
    unpage(st, e);
}

/** A C page keeps its levels on Return and a closer goes back to its
 *  opener's indentation; Toggle comment uses "//" in C and REM after the
 *  number in BASIC; pages mark the bracket by the caret. */
static void line_commands(ide_t *st)
{
    edit_t *e = page(st, "levels.c", "int f(void) {", 13);

    assert(tiku_codeview_options(e->view) & TIKU_CODEVIEW_BRACES);
    tiku_ide_place(e, 0, 13);
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(holds(e, "int f(void) {\n\t", 15));
    key(st, e, '}', 0, "}");
    assert(holds(e, "int f(void) {\n}", 15));
    key(st, e, '/', TIKU_MOD_CMD, NULL);
    assert(holds(e, "int f(void) {\n// }", 18));
    unpage(st, e);
    e = page(st, "rem.bas", "10 PRINT 1\n20 END", 17);
    assert(tiku_codeview_select(e->view, 0, 17) == TIKU_EDIT_OK);
    tiku_ide_pick(st, e->id, CMD_COMMENT);
    assert(holds(e, "10 REM PRINT 1\n20 REM END", 25));
    key(st, e, '/', TIKU_MOD_CMD, NULL);
    assert(holds(e, "10 PRINT 1\n20 END", 17));
    unpage(st, e);
}

/** What a run or build says about a line shows on its page: the name at
 *  the column underlined, by kind, and the words under the line; Next
 *  and Previous problem go round them, a new run clears them, search
 *  hits are not problems, and a BASIC line is found by its number. */
static void problems(ide_t *st)
{
    static const char text[] = "int f(void)\n{\n    return nope;\n}\n";
    edit_t *e = page(st, "bad.c", text, sizeof text - 1);
    tiku_edit_range_t r;
    tiku_edit_pos_t lo, hi;
    unsigned kind = 9;
    char said[700];

    st->nmsg = 0; st->msg_problems = 1; st->child_numbered = 0;
    snprintf(said, sizeof said, "%s:3:12: error: use of undeclared "
             "identifier 'nope'", e->path);
    tiku_ide_message_take(st, said);
    snprintf(said, sizeof said, "%s:1:5: warning: f is unused", e->path);
    tiku_ide_message_take(st, said);
    tiku_ide_messages_show(st);
    assert(tiku_codeview_options(e->view) & TIKU_CODEVIEW_ANNOTATIONS);
    assert(tiku_editdoc_indicator_next(e->doc, -1, 0x7, 0, &r, &kind) ==
           TIKU_EDIT_OK && r.start == 4 && r.end == 5 && kind == 1);
    assert(tiku_editdoc_indicator_next(e->doc, 4, 0x7, 0, &r, &kind) ==
           TIKU_EDIT_OK && r.start == 25 && r.end == 29 && kind == 0);
    assert(!strcmp(tiku_editdoc_annotation(e->doc, 2, 0, &kind),
                   "error: use of undeclared identifier 'nope'") &&
           kind == 0 && tiku_ide_problems_any(e));
    tiku_ide_place(e, 0, 0);
    key(st, e, '\'', TIKU_MOD_CMD, NULL);
    selection(e, &lo, &hi); assert(lo == 4 && hi == 5);
    key(st, e, '\'', TIKU_MOD_CMD, NULL);
    selection(e, &lo, &hi); assert(lo == 25 && hi == 29);
    key(st, e, '\'', TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL);
    selection(e, &lo, &hi); assert(lo == 4 && hi == 5);
    tiku_ide_pick(st, e->id, CMD_PREV_PROBLEM);
    selection(e, &lo, &hi); assert(lo == 25 && hi == 29);
    /* A page opened after the build gets the build's problems. */
    unpage(st, e);
    e = page(st, "bad.c", text, sizeof text - 1);
    assert(tiku_ide_problems_any(e) &&
           tiku_editdoc_annotations(e->doc, NULL) == 2);
    /* The next run starts clean; search hits draw nothing. */
    st->nmsg = 0; st->msg_problems = 1;
    tiku_ide_problems_show(st);
    assert(!tiku_ide_problems_any(e) &&
           tiku_editdoc_annotations(e->doc, NULL) == 0);
    st->msg_problems = 0;
    tiku_ide_message_take(st, said);
    tiku_ide_messages_show(st);
    assert(!tiku_ide_problems_any(e));
    unpage(st, e);
    e = page(st, "p.bas", "10 PRINT 1\n20 PRNT 2\n", 21);
    st->nmsg = 0; st->msg_problems = 1; st->child_numbered = 1;
    snprintf(st->child_file, sizeof st->child_file, "%s", e->path);
    tiku_ide_message_take(st, "? syntax");
    tiku_ide_message_take(st, "at line 20");
    tiku_ide_messages_show(st);
    assert(tiku_editdoc_indicator_next(e->doc, -1, 0x7, 0, &r, &kind) ==
           TIKU_EDIT_OK && r.start == 11 && r.end == 20 && kind == 0);
    assert(!strcmp(tiku_editdoc_annotation(e->doc, 1, 0, NULL),
                   "? syntax, at line 20"));
    st->nmsg = 0;
    tiku_ide_problems_show(st);
    st->msg_problems = 0;
    unpage(st, e);
}

/* A pointer event over the middle of the character at @p pos. */
static void point(ide_t *st, edit_t *e, tiku_event_type_t type,
                  tiku_edit_pos_t pos, unsigned mods)
{
    tiku_event_t ev;
    int64_t x = 0, y = 0, x1 = 0, y1 = 0;

    assert(tiku_codeview_point(e->view, pos, &x, &y) == TIKU_EDIT_OK);
    assert(tiku_codeview_point(e->view, pos + 1, &x1, &y1) == TIKU_EDIT_OK);
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.button = type == TIKU_EVENT_POINTER_MOVE ? 0 : 1;
    ev.x = (int)(x + x1) / 2;
    ev.y = (int)y + tiku_codeview_row_height(e->view) / 2;
    ev.modifiers = mods;
    (void)tiku_ide_event(st, e->id, &ev);
}

/* Jump targets on a BASIC page are underlined and live under Command:
 * the pointer a hand over one, a click going to the line it names, a
 * number or a label, or the page saying there is none; a plain click
 * puts the caret as ever, and Command alone changes nothing but the
 * pointer. */
static void jumps(ide_t *st)
{
    static const char text[] = "10 GOSUB 30\n20 GOTO 99\n30 GOTO loop\n"
                               "40 loop: END\n";
    edit_t *e = page(st, "jumps.bas", text, sizeof text - 1);
    tiku_edit_pos_t lo, hi;
    tiku_event_t ev;

    assert(tiku_codeview_ink_style(e->view, TIKU_INK_LINK) ==
           (TIKU_CODEVIEW_UNDERLINE | TIKU_CODEVIEW_HOTSPOT));
    tiku_ide_edit_paint(st, e);
    point(st, e, TIKU_EVENT_POINTER_MOVE, 9, 0);
    assert(!tiku_codeview_hotspot(e->view, NULL) &&
           e->cursor == TIKU_CURSOR_ARROW);
    point(st, e, TIKU_EVENT_POINTER_MOVE, 9, TIKU_MOD_CMD);
    assert(tiku_codeview_hotspot(e->view, NULL) &&
           e->cursor == TIKU_CURSOR_HAND && shape_asked == TIKU_CURSOR_HAND);
    point(st, e, TIKU_EVENT_POINTER_DOWN, 9, TIKU_MOD_CMD);
    point(st, e, TIKU_EVENT_POINTER_UP, 9, TIKU_MOD_CMD);
    selection(e, &lo, &hi);
    assert(lo == 23 && hi == 23);               /* "30 GOTO loop" */
    point(st, e, TIKU_EVENT_POINTER_DOWN, 32, TIKU_MOD_CMD);
    point(st, e, TIKU_EVENT_POINTER_UP, 32, TIKU_MOD_CMD);
    selection(e, &lo, &hi);
    assert(lo == 36 && hi == 36);               /* "40 loop: END" */
    point(st, e, TIKU_EVENT_POINTER_DOWN, 20, TIKU_MOD_CMD);
    point(st, e, TIKU_EVENT_POINTER_UP, 20, TIKU_MOD_CMD);
    selection(e, &lo, &hi);
    assert(lo == 36 && !strcmp(e->feedback, "No line 99 in this program."));
    point(st, e, TIKU_EVENT_POINTER_DOWN, 9, 0);
    point(st, e, TIKU_EVENT_POINTER_UP, 9, 0);
    selection(e, &lo, &hi);
    assert(lo == hi && lo >= 9 && lo <= 10);    /* a plain click edits */
    point(st, e, TIKU_EVENT_POINTER_MOVE, 9, 0);
    assert(e->cursor == TIKU_CURSOR_ARROW);
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = TIKU_KEY_CMD;
    (void)tiku_ide_event(st, e->id, &ev);
    assert(e->cursor == TIKU_CURSOR_HAND && shape_asked == TIKU_CURSOR_HAND);
    selection(e, &lo, &hi);
    assert(lo == hi && lo >= 9 && lo <= 10);    /* nothing moved */
    ev.type = TIKU_EVENT_KEY_UP;
    ev.modifiers = TIKU_MOD_CMD;
    (void)tiku_ide_event(st, e->id, &ev);
    assert(e->cursor == TIKU_CURSOR_ARROW && shape_asked == TIKU_CURSOR_ARROW);
    assert(holds(e, text, sizeof text - 1));
    unpage(st, e);
}

/** Return over a selection in BASIC numbers the new line from the line
 *  the selection starts in, whichever way it was made. */
static void return_over_selection(ide_t *st)
{
    static const char text[] = "10 PRINT 1\n20 PRINT 2\n30 PRINT 3\n";
    edit_t *e = page(st, "sel.bas", text, sizeof text - 1);

    /* Made downward: the caret is in line 30. */
    assert(tiku_codeview_select(e->view, 5, 22 + 5) == TIKU_EDIT_OK);
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(holds(e, "10 PR\n20 INT 3\n", 15));
    assert(tiku_ide_text_load(e, text, sizeof text - 1) == TIKU_EDIT_OK);
    /* Made upward: the caret is in line 10. */
    assert(tiku_codeview_select(e->view, 22 + 5, 5) == TIKU_EDIT_OK);
    key(st, e, TIKU_KEY_RETURN, 0, NULL);
    assert(holds(e, "10 PR\n20 INT 3\n", 15));
    unpage(st, e);
}

/* A pointer event of @p type at @p x, @p y with button 1. */
static void at(ide_t *st, edit_t *e, tiku_event_type_t type, int x, int y)
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.button = 1;
    ev.x = x;
    ev.y = y;
    (void)tiku_ide_event(st, e->id, &ev);
}

/* Where the page's scrollbar draws its thumb, from the view's extent;
 * the first row shown goes to @p top. */
static tiku_rect_t page_thumb(edit_t *e, int64_t *top)
{
    int ptop = tiku_ide_edit_page_top(e);
    tiku_rect_t bar = { e->w - PAGE_SB, ptop, PAGE_SB,
                        tiku_ide_edit_page_bottom(e) - ptop };
    int64_t total, rows;

    tiku_codeview_extent(e->view, 0, &total, &rows, top);
    assert(total > rows && rows > 0);
    return tiku_ui_scrollbar_thumb(bar, (float)*top / (float)(total - rows),
                                   (float)rows / (float)total, 0);
}

/** A press on the page's scrollbar track pages toward the side of the
 *  thumb it is on; a press on the thumb takes hold of it, and the thumb
 *  follows the pointer until it is let go. */
static void page_scrollbar(ide_t *st)
{
    char text[4000];
    size_t n = 0;
    int i, x, ptop, mid, grab;
    int64_t top, now;
    tiku_rect_t th, moved;
    edit_t *e;

    for (i = 1; i <= 200; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "%d REM\n", i * 10);
    }
    e = page(st, "long.bas", text, n);
    x = e->w - PAGE_SB / 2;
    ptop = tiku_ide_edit_page_top(e);
    mid = ptop + (tiku_ide_edit_page_bottom(e) - ptop) / 2;

    /* The page opens at its end; a press just above the thumb, below the
     * track's middle, pages up. */
    th = page_thumb(e, &top);
    assert(th.y - 2 > mid);
    at(st, e, TIKU_EVENT_POINTER_DOWN, x, th.y - 2);
    at(st, e, TIKU_EVENT_POINTER_UP, x, th.y - 2);
    th = page_thumb(e, &now);
    assert(now < top);
    top = now;

    /* A press on the thumb does not page; a drag moves the thumb with
     * the pointer, and a move after the release does not. */
    grab = th.y + th.h / 2;
    at(st, e, TIKU_EVENT_POINTER_DOWN, x, grab);
    (void)page_thumb(e, &now);
    assert(now == top);
    at(st, e, TIKU_EVENT_POINTER_MOVE, x, grab - 40);
    moved = page_thumb(e, &now);
    assert(now < top && moved.y >= th.y - 41 && moved.y <= th.y - 39);
    at(st, e, TIKU_EVENT_POINTER_UP, x, grab - 40);
    top = now;
    at(st, e, TIKU_EVENT_POINTER_MOVE, x, grab - 80);
    (void)page_thumb(e, &now);
    assert(now == top);
    unpage(st, e);
}

static void splash_events(ide_t *st)
{
    st->splash_id = 499;
    st->splash_surface = tiku_surface_new(100, 100, 0);
    const int types[] = {TIKU_EVENT_POINTER_MOVE, TIKU_EVENT_POINTER_UP,
                        TIKU_EVENT_WHEEL, TIKU_EVENT_RESIZE, TIKU_EVENT_KEY_UP};
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        tiku_event_t ev = {.type = types[i], .key = TIKU_KEY_SHIFT};
        tiku_ide_event(st, 499, &ev);
        assert(st->splash_surface != NULL);
    }
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN};
    tiku_ide_event(st, 499, &ev);
    assert(st->splash_surface == NULL);
}
static void page_menus(ide_t *st)
{
    edit_t *a = page(st, "menus-a.bas", "10 END\n", 7);
    unsigned before = menu_publications[a->id];
    edit_t *b = page(st, "menus-b.bas", "10 END\n", 7);
    assert(menu_publications[a->id] > before);
    tiku_ide_edit_drop(st, b);
    tiku_ide_edit_drop(st, a);
}
int main(int argc, char **argv)
{
    char drafts[600];
    ide_t *st;

    assert(mkdtemp(dir));
    snprintf(drafts, sizeof drafts, "%s/drafts", dir);
    assert(mkdir(drafts, 0700) == 0);
    assert(setenv("TIKU_IDE_RECOVERY_DIR", drafts, 1) == 0);
    st = ide();
    if (argc > 1 && !strcmp(argv[1], "splash")) {
        splash_events(st);
    } else if (argc > 1 && !strcmp(argv[1], "menus")) {
        page_menus(st);
    } else {
    kept_bytes(st);
    basic_limits(st);
    breakpoints(st);
    execution_line(st);
    renumber_edits(st);
    assistance(st);
    find_by_number(st);
    caret_rules(st);
    clipboard(st);
    line_commands(st);
    problems(st);
    jumps(st);
    return_over_selection(st);
    page_scrollbar(st);
    splash_events(st);
    page_menus(st);
    }
    tiku_surface_free(st->proj_surface);
    tiku_surface_free(st->msg_surface);
    tiku_surface_free(st->con_surface);
    free(st);
    puts("PASS: kept bytes, BASIC limits at the run, breakpoint markers, "
         "the paused line, renumber edits, completion and help, caret and "
         "clipboard rules, line commands, problems, jump targets, Return "
         "over a selection, the page's scrollbar");
    return 0;
}
