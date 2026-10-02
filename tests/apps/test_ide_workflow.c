/* Host regressions for upload preflight, search, recovery and target state. */
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include "tiku_dl.h"
#include "../../apps/ide/tiku_ide_private.h"

static const char *selected = "board-a";
static int saves, runs, window_opens;
static int frames_without_window;
static uint32_t last_frame;
static char last_window[128];
static char sent_board[64], sent_name[8], sent_program[8192];

static int current(void *ctx, char *out, size_t max)
{ (void)ctx; return snprintf(out, max, "%s", selected); }
static int names(void *ctx, char *out, size_t max)
{ (void)ctx; return snprintf(out, max, "board-a\nboard-b\n"); }
static char last_menu_item[128];
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    (void)ctx; (void)id;
    if (set && set->nmenu > 0 && set->menu[0].nitem > 0)
        snprintf(last_menu_item, sizeof last_menu_item, "%s",
                 set->menu[0].item[0].label);
    return 0;
}
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx; (void)px; (void)w; (void)h;
    if (!id) frames_without_window++;
    last_frame = id;
    return 0;
}
static uint32_t open_window(void *ctx, const char *title, int w, int h)
{
    (void)ctx; (void)w; (void)h;
    window_opens++;
    snprintf(last_window, sizeof last_window, "%s", title);
    return (uint32_t)(98 + window_opens);
}
static uint32_t closed_id[8];   /* the windows the IDE gave back, in order */
static int closed_n;
static void close_window(void *ctx, uint32_t id)
{ (void)ctx; if (closed_n < 8) { closed_id[closed_n] = id; } closed_n++; }
static int save(void *ctx, const char *board, const char *name, const char *text)
{
    (void)ctx;
    saves++;
    snprintf(sent_board, sizeof sent_board, "%s", board);
    snprintf(sent_name, sizeof sent_name, "%s", name);
    snprintf(sent_program, sizeof sent_program, "%s", text);
    return 1;
}
static int run(void *ctx, const char *board, const char *text)
{ (void)ctx; (void)board; (void)text; runs++; return 0; }
static char desk_clip[512];     /* the desk's clipboard, as the services hold it */
static int clip_set(void *ctx, const char *text)
{ (void)ctx; snprintf(desk_clip, sizeof desk_clip, "%s", text); return 0; }
static const char *clip_text(void *ctx, unsigned *stamp)
{ (void)ctx; if (stamp) { *stamp = 0; } return desk_clip; }

/* Line @p line of the page, in a buffer the next call reuses. */
static const char *line_of(const edit_t *e, int64_t line)
{
    static char *held;

    free(held);
    held = tiku_ide_line_dup(e, line, NULL);
    return held != NULL ? held : "";
}

/* Whether line @p line of the page reads @p want. */
static int line_is(const edit_t *e, int64_t line, const char *want)
{
    return strcmp(line_of(e, line), want) == 0;
}

/* The page holding @p text, as a load leaves it: no history, saved. */
static void set_text(edit_t *e, const char *text)
{
    assert(tiku_ide_text_load(e, text, strlen(text)) == TIKU_EDIT_OK);
}

/* The page's whole text into @p out. */
static void text_of(const edit_t *e, char *out, size_t max)
{
    tiku_edit_range_t all = { 0, tiku_editdoc_length(e->doc) };
    size_t n = 0;

    assert(tiku_editdoc_read(e->doc, all, out, max, &n) == TIKU_EDIT_OK);
}

/* Whether the caret stands alone at byte @p column of line @p line. */
static int caret_at(const edit_t *e, int64_t line, int64_t column)
{
    tiku_edit_pos_t anchor = -1, caret = -2;
    int64_t y, x;

    (void)tiku_codeview_selection(e->view, &anchor, &caret);
    (void)tiku_ide_caret(e, &y, &x);
    return anchor == caret && y == line && x == column;
}

/* Whether the page holds a selection, and where it starts. */
static int selection_at(const edit_t *e, int64_t *line, int64_t *column)
{
    tiku_edit_pos_t anchor = 0, caret = 0, lo;
    int64_t y;

    (void)tiku_codeview_selection(e->view, &anchor, &caret);
    lo = anchor < caret ? anchor : caret;
    y = tiku_editdoc_line_of(e->doc, lo);
    if (line != NULL) { *line = y; }
    if (column != NULL) { *column = lo - tiku_editdoc_line_start(e->doc, y); }
    return anchor != caret;
}

/* Type @p text at the caret, as one key does. */
static void type_text(edit_t *e, const char *text)
{
    assert(tiku_codeview_insert(e->view, text, strlen(text), 1) ==
           TIKU_EDIT_OK);
}

static void file_write(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(text, 1, n, f) == n);
    assert(fclose(f) == 0);
}

static void test_upload(ide_t *st, const char *dir)
{
    char path[512], out[8192], large[8193];
    tiku_event_t enter = { .type = TIKU_EVENT_KEY_DOWN,
                           .key = TIKU_KEY_RETURN };

    snprintf(path, sizeof path, "%s/longfilename.bas", dir);
    memset(large, 'x', sizeof large);
    file_write(path, large, 8191);
    assert(tiku_ide_upload_read(st, path, out, sizeof out));
    file_write(path, large, 8192);
    assert(!tiku_ide_upload_read(st, path, out, sizeof out));
    assert(strstr(st->note, "too large"));
    tiku_ide_run_file_on_board(st, path, "board-a");
    assert(!runs);
    file_write(path, "x\0y", 3);
    assert(!tiku_ide_upload_read(st, path, out, sizeof out));
    file_write(path, "10 PRINT 42\n", 12);
    tiku_ide_save_file_on_board(st, path);
    assert(st->ask.open && !saves);
    assert(strstr(st->ask.text, "/data/longfil.bas"));
    assert(strstr(st->ask.text, "Replace any existing"));
    assert(st->ask.dflt == 0);
    tiku_ide_event(st, st->proj_id, &enter);
    assert(!saves && !st->ask.open);
    tiku_ide_save_file_on_board(st, path);
    selected = "board-b";
    file_write(path, "10 END\n", 7);
    tiku_ide_upload_confirm(st);
    assert(saves == 1 && !strcmp(sent_board, "board-a"));
    assert(!strcmp(sent_name, "longfil"));
    assert(!strcmp(sent_program, "10 PRINT 42\n"));
    st->running = st->saving = 0;
    tiku_alert_reset(&st->ask);
    snprintf(st->upload_board, sizeof st->upload_board, "offline");
    tiku_ide_upload_confirm(st);
    assert(saves == 1);
    unlink(path);
}

static void test_search(void)
{
    edit_t *e = calloc(1, sizeof *e);
    char original[4096], actual[4096], wide[1300];
    int i;

    assert(e);
    e->w = 620; e->h = 420;
    e->lang = TIKU_SYNTAX_BASIC;
    set_text(e, "10 PRINT 10\n20 PRINT 10\n\tREM café");
    text_of(e, original, sizeof original);
    tiku_ide_search_open(e, 1);
    tiku_field_start(&e->find, "10");
    tiku_field_start(&e->replacement, "100");
    tiku_ide_search_replace(e, 0); /* Select only first. */
    assert(!tiku_editdoc_modified(e->doc));
    tiku_ide_search_replace(e, 1);
    assert(line_is(e, 0, "100 PRINT 100"));
    assert(line_is(e, 2, "\tREM café"));
    assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK);
    text_of(e, actual, sizeof actual);
    assert(!strcmp(actual, original));
    assert(tiku_codeview_redo(e->view) == TIKU_EDIT_OK);
    tiku_field_start(&e->find, "100");
    tiku_field_start(&e->replacement, "");
    tiku_ide_search_replace(e, 1);
    assert(line_is(e, 0, " PRINT "));
    assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK);
    for (i = 0; i < 600; i++) { original[i] = 'a'; }
    original[600] = '\0';
    set_text(e, original);
    tiku_field_start(&e->find, "a");
    tiku_field_start(&e->replacement, "aa");
    /* No line is too long for the page: All doubles the 600 bytes. */
    tiku_ide_search_replace(e, 1);
    memset(wide, 'a', 1200);
    wide[1200] = '\0';
    assert(line_is(e, 0, wide) && tiku_editdoc_modified(e->doc));
    assert(strstr(e->feedback, "600 replacements"));
    assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK);
    assert(line_is(e, 0, original) && !tiku_editdoc_modified(e->doc));
    tiku_field_start(&e->replacement, "b");
    tiku_ide_search_replace(e, 1); /* 600 matches, one undo step. */
    assert(line_of(e, 0)[599] == 'b');
    assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK);
    assert(line_is(e, 0, original));
    tiku_ide_text_free(e);
    free(e);
}

static void test_recovery(const char *dir)
{
    edit_t *e = calloc(1, sizeof *e);
    char draft[640], original[32];
    FILE *f;

    assert(e);
    snprintf(e->path, sizeof e->path, "%s/source.bas", dir);
    snprintf(e->recovery, sizeof e->recovery, "%s/source.draft", dir);
    snprintf(draft, sizeof draft, "%s", e->recovery);
    e->recovery_fd = open(dir, O_RDONLY);
    assert(e->recovery_fd >= 0);
    file_write(e->path, "10 END\n", 7);
    set_text(e, "10 PRINT 42");
    tiku_editdoc_saved(e->doc, 0);
    assert(tiku_ide_recovery_write(e));
    f = fopen(e->path, "r");
    assert(f && fgets(original, sizeof original, f));
    fclose(f);
    assert(!strcmp(original, "10 END\n"));
    set_text(e, "10 END");
    tiku_ide_recovery_restore(e);
    assert(line_is(e, 0, "10 PRINT 42"));
    assert(tiku_editdoc_modified(e->doc));
    assert(strstr(e->feedback, "Recovered"));
    assert(tiku_ide_edit_save(e));
    assert(access(draft, F_OK) != 0);
    assert(!tiku_editdoc_modified(e->doc));
    assert(tiku_codeview_select_all(e->view) == TIKU_EDIT_OK);
    type_text(e, "10 END\r\n");
    assert(tiku_ide_recovery_write(e));
    set_text(e, "other text");
    tiku_ide_recovery_restore(e);
    /* The draft gives back every byte, the final CRLF included. */
    text_of(e, original, sizeof original);
    assert(!strcmp(original, "10 END\r\n") && tiku_editdoc_lines(e->doc) == 2);
    type_text(e, "abc");
    assert(tiku_ide_recovery_write(e));
    tiku_ide_recovery_release(e, 1);
    assert(access(draft, F_OK) != 0);
    unlink(e->path);
    tiku_ide_text_free(e);
    free(e);
}

static void test_ui(ide_t *st, const char *dir)
{
    edit_t *e = &st->edit[0];
    tiku_event_t ev = { .type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_TAB };
    char missing[512];
    int previous_runs = runs;

    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->services = st->services;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->name, sizeof e->name, "sensor.bas");
    set_text(e, "10 PRINT \"Temperature\"\n20 PRINT 42\n30 END");
    {
        tiku_text_input_t input;
        tiku_event_t text;
        const char *phrase = "REM café 日本語";
        assert(tiku_codeview_select_all(e->view) == TIKU_EDIT_OK);
        assert(tiku_text_input_set(&input, phrase, strlen(phrase)));
        while (tiku_text_input_next(&input, &text)) {
            tiku_ide_event(st, e->id, &text);
            if (!(text.button & TIKU_TEXT_END)) {
                assert(strstr(line_of(e, 0), "Temperature"));
            }
        }
        assert(line_is(e, 0, phrase) && runs == previous_runs);
        assert(tiku_codeview_undo(e->view) == TIKU_EDIT_OK);
        assert(strstr(line_of(e, 0), "Temperature"));
        tiku_ide_place(e, 0, 0);
        tiku_ide_search_open(e, 0);
        assert(tiku_text_input_set(&input, "café", 5));
        while (tiku_text_input_next(&input, &text)) tiku_ide_event(st, e->id, &text);
        assert(!strcmp(e->find.text, "café"));
        e->finding = 0;
    }
    tiku_ide_pick(st, e->id, CMD_REPLACE);
    tiku_field_start(&e->find, "PRINT");
    tiku_field_start(&e->replacement, "REM");
    tiku_ide_event(st, e->id, &ev);
    assert(e->find_focus == 1);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.x = tiku_ide_search_button(2).x + 8;
    ev.y = tiku_ide_search_button(2).y + 8;
    tiku_ide_event(st, e->id, &ev);
    assert(strstr(line_of(e, 0), "REM"));
    assert(tiku_ide_pick(st, e->id, CMD_UNDO) == 0);
    assert(strstr(line_of(e, 0), "PRINT"));
    snprintf(st->proj.row[0].path, sizeof st->proj.row[0].path, "%s/open.bas", dir);
    snprintf(e->path, sizeof e->path, "%s", st->proj.row[0].path);
    snprintf(st->proj.row[0].name, sizeof st->proj.row[0].name, "open.bas");
    st->proj.nrow = 1;
    tiku_list_init(&st->msg_list, 1);
    tiku_ide_search_project(st, e);
    assert(st->nmsg == 2 && st->msg[0].line == 1 && !st->msg[0].numbered);
    tiku_field_start(&e->find, "missing needle");
    tiku_ide_search_project(st, e);
    assert(!st->nmsg);
    tiku_field_start(&e->find, "PRINT");
    st->proj.nrow = 0;
    selected = "nrf54l15";
    tiku_ide_edit_paint(st, e);
    assert(tiku_surface_png(e->surface, "/tmp/tikubasic-find-replace.png") == 0);
    st->proj_surface = tiku_surface_new(IDE_W, IDE_H, TIKU_C_PANEL);
    assert(st->proj_surface);
    st->proj_w = IDE_W; st->proj_h = IDE_H;
    tiku_list_init(&st->tv_list, 1);
    snprintf(missing, sizeof missing, "%s/longfilename.bas", dir);
    file_write(missing, "10 PRINT 42\n", 12);
    selected = "board-a";
    tiku_ide_save_file_on_board(st, missing);
    tiku_ide_project_paint(st);
    assert(tiku_surface_png(st->proj_surface, "/tmp/tikubasic-upload.png") == 0);
    tiku_alert_reset(&st->ask);
    unlink(missing);
    snprintf(missing, sizeof missing, "%s/missing/source.bas", dir);
    snprintf(e->path, sizeof e->path, "%s", missing);
    type_text(e, "x");
    selected = "board-a";
    tiku_ide_run_file_where(st, e->path);
    assert(runs == previous_runs);
    assert(strstr(st->note, "cancelled"));
    snprintf(e->path, sizeof e->path, "%s/oversized.bas", dir);
    {
        /* A line far past what BASIC reads opens and edits; the run is
         * what refuses it. */
        char long_line[1100];
        memset(long_line, 'x', sizeof long_line);
        file_write(e->path, long_line, sizeof long_line);
        tiku_ide_edit_load(e);
        assert(!e->load_failed && strlen(line_of(e, 0)) == sizeof long_line);
        tiku_ide_run_file_where(st, e->path);
        assert(runs == previous_runs &&
               strstr(st->note, "159 bytes BASIC reads"));
        assert(strstr(e->feedback, "Line 1 is longer"));
        /* A file the page cannot hold blocks saving and running. */
        file_write(e->path, "10 PRINT \xff\n", 11);
        tiku_ide_edit_load(e);
        assert(e->load_failed && !tiku_ide_edit_save(e));
        assert(tiku_editdoc_length(e->doc) == 0);
        tiku_ide_run_file_where(st, e->path);
        assert(runs == previous_runs && strstr(st->note, "cancelled"));
        unlink(e->path);
        e->load_failed = 0;
        type_text(e, "10 END");
    }
    tiku_ide_edit_drop(st, e);
    assert(e->surface && st->ask.open && st->ask.tag == CMD_QUIT);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = TIKU_KEY_ESCAPE;
    tiku_ide_event(st, st->proj_id, &ev);
    assert(e->surface && !st->ask.open);
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
    tiku_surface_free(st->proj_surface);
    st->proj_surface = NULL;
    tiku_surface_free(st->msg_surface);
    st->msg_surface = NULL;
}

/** A plain click in a page leaves no selection behind: a later caret
 *  move selects nothing, and both letters typed land where it was. */
static void test_click_then_type(ide_t *st)
{
    static const char *text = "10 PRINT 42";
    edit_t *e = &st->edit[0];
    tiku_event_t ev = { .type = TIKU_EVENT_POINTER_DOWN, .button = 1 };
    char want[64];
    int64_t line, col;

    memset(e, 0, sizeof *e);            /* test_ui left its find bar open */
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->name, sizeof e->name, "click.bas");
    set_text(e, "10 PRINT 42\n20 END");
    ev.x = 60;
    ev.y = tiku_ide_edit_page_top(e) + 8;
    tiku_ide_event(st, e->id, &ev);
    ev.type = TIKU_EVENT_POINTER_UP;
    tiku_ide_event(st, e->id, &ev);
    (void)tiku_ide_caret(e, &line, &col);
    assert(line == 0 && col > 0 && col < (int)strlen(text));
    /* A click leaves no selection: moving the caret, as following a
     * message does, selects nothing. */
    assert(!selection_at(e, NULL, NULL));
    tiku_ide_place(e, 1, 0);
    assert(!selection_at(e, NULL, NULL));
    tiku_ide_place(e, 0, col);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = 'a';
    tiku_ide_event(st, e->id, &ev);
    ev.key = 'b';
    tiku_ide_event(st, e->id, &ev);
    snprintf(want, sizeof want, "%.*sab%s", (int)col, text, text + col);
    assert(line_is(e, 0, want));
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** Send @p key with @p modifiers to page @p e. */
static void key_to(ide_t *st, edit_t *e, unsigned key, unsigned modifiers)
{
    tiku_event_t ev = { .type = TIKU_EVENT_KEY_DOWN };

    ev.key = key;
    ev.modifiers = modifiers;
    tiku_ide_event(st, e->id, &ev);
}

/** A key that ends a keyword over a held selection replaces the selection
 *  and nothing else; Return over one numbers from the line it lands on. */
static void test_keyword_over_selection(ide_t *st)
{
    edit_t *e = &st->edit[0];

    memset(e, 0, sizeof *e);
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    set_text(e, "print x");
    key_to(st, e, TIKU_KEY_END, 0);
    key_to(st, e, TIKU_KEY_LEFT, TIKU_MOD_SHIFT);
    key_to(st, e, TIKU_KEY_LEFT, TIKU_MOD_SHIFT);
    key_to(st, e, ' ', 0);
    assert(line_is(e, 0, "PRINT "));
    set_text(e, "10 end");
    key_to(st, e, 'a', TIKU_MOD_CMD);
    key_to(st, e, ' ', 0);
    assert(line_is(e, 0, " "));
    set_text(e, "10 a\n20 b\n30 c\n40 d");
    key_to(st, e, TIKU_KEY_END, 0);
    key_to(st, e, TIKU_KEY_DOWN, TIKU_MOD_SHIFT);
    key_to(st, e, TIKU_KEY_DOWN, TIKU_MOD_SHIFT);
    key_to(st, e, TIKU_KEY_RETURN, 0);
    assert(line_is(e, 0, "10 a") && line_is(e, 1, "20 ") &&
           line_is(e, 2, "40 d"));
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** Following a message drops a held selection before moving the caret,
 *  so nothing between the selection and the target becomes selected. */
static void test_follow_drops_selection(ide_t *st)
{
    edit_t *e = &st->edit[0];

    memset(e, 0, sizeof *e);
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->path, sizeof e->path, "/tmp/tiku-follow-test.bas");
    set_text(e, "10 PRINT 1\n20 PRINT 2\n30 PRINT 3");
    assert(tiku_codeview_select(e->view, 3, 8) == TIKU_EDIT_OK);
    memset(&st->msg[0], 0, sizeof st->msg[0]);
    snprintf(st->msg[0].file, sizeof st->msg[0].file, "%s", e->path);
    st->msg[0].line = 3;
    st->nmsg = 1;
    tiku_list_init(&st->msg_list, 1);
    (void)tiku_list_set_count(&st->msg_list, 1);
    tiku_list_select_only(&st->msg_list, 0);
    tiku_ide_message_follow(st);
    assert(caret_at(e, 2, 0));
    assert(!selection_at(e, NULL, NULL));
    st->nmsg = 0;
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** Return on a page of 4,000 lines makes the break and its number, as on
 *  any page; a break the document refuses types no number either. */
static void test_return_makes_the_break(ide_t *st)
{
    edit_t *e = &st->edit[0];
    size_t cap = 4000u * 8u + 1u, at = 0;
    char *text = malloc(cap);
    int i;

    assert(text);
    for (i = 0; i < 4000; i++) {
        at += (size_t)snprintf(text + at, cap - at, "%d a\n", 10 * (i % 999 + 1));
    }
    memset(e, 0, sizeof *e);
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    set_text(e, text);
    assert(tiku_editdoc_lines(e->doc) == 4001);
    tiku_ide_place(e, 0, 4);
    key_to(st, e, TIKU_KEY_RETURN, 0);
    assert(tiku_editdoc_lines(e->doc) == 4002);
    assert(line_is(e, 0, "10 a") && line_is(e, 1, "20 ") && caret_at(e, 1, 3));
    /* A held page refuses the break, and so types no number. */
    st->running = st->debugging = 1;
    snprintf(st->child_file, sizeof st->child_file, "%s", e->path);
    tiku_ide_place(e, 0, 4);
    key_to(st, e, TIKU_KEY_RETURN, 0);
    tiku_ide_edit_lock(st, e);
    assert(tiku_editdoc_read_only(e->doc));
    assert(tiku_codeview_insert(e->view, "\n", 1, 1) == TIKU_EDIT_READ_ONLY);
    assert(tiku_editdoc_lines(e->doc) == 4002 && line_is(e, 0, "10 a"));
    st->running = st->debugging = 0;
    st->child_file[0] = '\0';
    tiku_ide_edit_lock(st, e);
    assert(!tiku_editdoc_read_only(e->doc));
    free(text);
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** A Cmd chord that no binding takes types nothing, whatever its text. */
static void test_unbound_cmd_chords(ide_t *st)
{
    edit_t *e = &st->edit[0];
    static const struct { int lang; unsigned key; const char *text; } chord[] = {
        {TIKU_SYNTAX_BASIC, '1', "1"}, {TIKU_SYNTAX_BASIC, 'e', "e"},
        {TIKU_SYNTAX_BASIC, 'e', ""}, {TIKU_SYNTAX_C, '.', "."},
        {TIKU_SYNTAX_C, ' ', " "}, {TIKU_SYNTAX_NONE, '0', "0"},
    };

    memset(e, 0, sizeof *e);
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    for (size_t i = 0; i < sizeof chord / sizeof chord[0]; i++) {
        tiku_event_t ev = { .type = TIKU_EVENT_KEY_DOWN, .key = chord[i].key,
                            .modifiers = TIKU_MOD_CMD };

        e->lang = chord[i].lang;
        set_text(e, "10 x");
        tiku_ide_place(e, 0, 4);
        snprintf(ev.text, sizeof ev.text, "%s", chord[i].text);
        tiku_ide_event(st, e->id, &ev);
        assert(line_is(e, 0, "10 x"));
    }
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** The find bar keeps its query when it opens again, moves past a match
 *  whose replacement is the same text, and reads a chord by its key, as
 *  X11 sends a control byte for Ctrl+A. */
static void test_find_bar(ide_t *st)
{
    edit_t *e = &st->edit[0];
    int64_t x0;
    tiku_event_t ev = { .type = TIKU_EVENT_KEY_DOWN, .key = 'a',
                        .modifiers = TIKU_MOD_CMD, .text = "\x01" };

    memset(e, 0, sizeof *e);
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_C;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    set_text(e, "ab ab ab");
    tiku_ide_search_open(e, 0);
    tiku_field_start(&e->find, "ab");
    e->finding = 0;
    tiku_ide_search_open(e, 0);
    assert(!strcmp(e->find.text, "ab") && e->find.sel_a == 0 && e->find.sel_b == 2);
    tiku_field_start(&e->replacement, "ab");
    tiku_ide_place(e, 0, 0);
    tiku_ide_search_replace(e, 0);
    assert(selection_at(e, NULL, &x0) && x0 == 0);
    tiku_ide_search_replace(e, 0);
    assert(!strstr(e->feedback, "replacement"));
    tiku_ide_search_replace(e, 0);
    assert(selection_at(e, NULL, &x0) && x0 == 3);
    tiku_ide_search_replace(e, 1);
    assert(!strstr(e->feedback, "replacement") &&
           !tiku_editdoc_modified(e->doc));
    e->find_focus = 0;
    tiku_field_start(&e->find, "xy");
    e->find.sel_a = e->find.sel_b = e->find.caret = 2;
    tiku_ide_event(st, e->id, &ev);
    assert(e->find.sel_a == 0 && e->find.sel_b == 2);
    ev.key = 'c'; snprintf(ev.text, sizeof ev.text, "\x03");
    tiku_ide_event(st, e->id, &ev);
    assert(!strcmp(desk_clip, "xy"));
    ev.key = 'x'; snprintf(ev.text, sizeof ev.text, "\x18");
    tiku_ide_event(st, e->id, &ev);
    assert(!strcmp(desk_clip, "xy") && !e->find.text[0]);
    snprintf(desk_clip, sizeof desk_clip, "pasted");
    ev.key = 'v'; snprintf(ev.text, sizeof ev.text, "\x16");
    tiku_ide_event(st, e->id, &ev);
    assert(!strcmp(e->find.text, "pasted"));
    e->finding = 0;
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** Renumber rewrites a line crowded with branch targets exactly, and a
 *  line far longer than BASIC keeps is rewritten too: the page has no
 *  line limit, the run boundary has. */
static void test_renumber_bounds(void)
{
    edit_t e;
    char line[1300], want[1400];
    size_t n, m;
    int i;

    memset(&e, 0, sizeof e);
    e.w = 620; e.h = 420; e.id = 2; e.tip = -1;
    e.lang = TIKU_SYNTAX_BASIC;
    n = (size_t)snprintf(line, sizeof line, "1goto 1");
    m = (size_t)snprintf(want, sizeof want, "10goto 10");
    for (i = 0; i < 78; i++) {
        n += (size_t)snprintf(line + n, sizeof line - n, " goto 1");
        m += (size_t)snprintf(want + m, sizeof want - m, " goto 10");
    }
    assert(n == 553);
    set_text(&e, line);
    tiku_ide_renumber_basic(&e);
    assert(line_is(&e, 0, want));
    while (n + 7 <= 1200) {
        n += (size_t)snprintf(line + n, sizeof line - n, " goto 1");
        m += (size_t)snprintf(want + m, sizeof want - m, " goto 10");
    }
    set_text(&e, line);
    tiku_ide_renumber_basic(&e);
    assert(line_is(&e, 0, want) && strlen(want) > 1300 - 100);
    assert(tiku_editdoc_modified(e.doc));
    assert(tiku_codeview_undo(e.view) == TIKU_EDIT_OK && line_is(&e, 0, line));
    tiku_ide_text_free(&e);
}

/** Renumber follows every target in a GOTO or GOSUB list and the one
 *  after THEN or ELSE, and leaves quoted text, remarks and DATA alone. */
static void test_renumber_targets(void)
{
    static const char *const before[] = {
        "100 PRINT \"GOTO 100\"", "200 REM GOTO 100",
        "300 ON A GOTO 100, 200 , 300", "400 IF A THEN GOTO 100 ELSE 200",
        "500 PRINT 1: ' GOTO 100", "600 DATA GOTO 100",
        "700 PRINT \"a\\\"GOTO 100\": GOSUB 700",
    };
    static const char *const after[] = {
        "10 PRINT \"GOTO 100\"", "20 REM GOTO 100",
        "30 ON A GOTO 10, 20 , 30", "40 IF A THEN GOTO 10 ELSE 20",
        "50 PRINT 1: ' GOTO 100", "60 DATA GOTO 100",
        "70 PRINT \"a\\\"GOTO 100\": GOSUB 70",
    };
    char text[512];
    size_t n = 0;
    edit_t e;
    int i;

    for (i = 0; i < 7; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "%s\n", before[i]);
    }
    memset(&e, 0, sizeof e);
    e.w = 620; e.h = 420; e.id = 2; e.tip = -1;
    e.lang = TIKU_SYNTAX_BASIC;
    set_text(&e, text);
    tiku_ide_renumber_basic(&e);
    for (i = 0; i < 7; i++) {
        assert(line_is(&e, i, after[i]));
    }
    tiku_ide_text_free(&e);
}

/** Past byte 255 of a line the caret is still drawn where the text before
 *  it ends, the page slid left to show it: its whole column on the
 *  caret's row is the text colour. */
static void test_caret_past_255(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    edit_t *e = &st->edit[0];
    char line[401];
    int64_t x, y, row;

    memset(line, 'm', 400); line[400] = '\0';
    memset(e, 0, sizeof *e);
    e->w = 620; e->h = 420; e->id = 2; e->tip = -1;
    e->lang = TIKU_SYNTAX_NONE;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    set_text(e, line);
    tiku_ide_place(e, 0, 400);
    tiku_ide_edit_reveal(e);
    tiku_ide_edit_paint(st, e);
    assert(tiku_codeview_point(e->view, 400, &x, &y) == TIKU_EDIT_OK);
    assert(tiku_codeview_left(e->view) > 0 && x > MARGIN && x < e->w - PAGE_SB);
    assert(x == MARGIN + tiku_text_width(f, line) -
                tiku_codeview_left(e->view));
    assert(y == tiku_ide_edit_page_top(e) + MARGIN);
    for (row = y; row < y + f->height + 2; row++) {
        assert(e->surface->px[row * e->surface->w + x] == TIKU_C_TEXT);
    }
    tiku_ide_text_free(e);
    tiku_surface_free(e->surface);
    e->surface = NULL;
}

/** Add File ends a project file's unterminated last line before its own
 *  entry, and a name holding a blank is listed and opened whole. */
static void test_add_file(ide_t *st, const char *dir)
{
    char folder[512], proj[600], added[600], back[256];
    edit_t *opened;
    FILE *f;
    size_t n;

    snprintf(folder, sizeof folder, "%s/addfile", dir);
    assert(mkdir(folder, 0700) == 0);
    snprintf(proj, sizeof proj, "%s/project.tiku", folder);
    file_write(proj, "title T\nfile a.bas", 18);
    tiku_ide_project_take(st, proj);
    assert(st->proj.nrow == 1);
    snprintf(added, sizeof added, "%s/b c.bas", folder);
    tiku_ide_project_add_file(st, added);
    f = fopen(proj, "rb");
    assert(f);
    n = fread(back, 1, sizeof back - 1, f);
    back[n] = '\0';
    fclose(f);
    assert(!strcmp(back, "title T\nfile a.bas\nfile b c.bas\n"));
    assert(st->proj.nrow == 2 && !strcmp(st->proj.row[1].name, "b c.bas"));
    opened = tiku_ide_edit_of_path(st, added);
    assert(opened != NULL);
    tiku_ide_edit_drop(st, opened);
    st->proj_file[0] = '\0';
    memset(&st->proj, 0, sizeof st->proj);
    assert(unlink(added) == 0 && unlink(proj) == 0 && rmdir(folder) == 0);
}

/** Remove File keeps every other byte of a project file of any size and
 *  takes out an entry the way the project reads it, a sub-folder, blanks
 *  and a CR included, replacing the file whole. */
static void test_remove_file(ide_t *st, const char *dir)
{
    static char text[16384], back[16384];
    char folder[512], proj[600];
    size_t n = 0, cut, got;
    struct dirent *ent;
    DIR *d;
    FILE *f;
    int i, entries = 0;

    snprintf(folder, sizeof folder, "%s/removefile", dir);
    assert(mkdir(folder, 0700) == 0);
    snprintf(proj, sizeof proj, "%s/project.tiku", folder);
    n += (size_t)snprintf(text + n, sizeof text - n, "title Big\nfile a.bas\n");
    for (i = 0; i < 300; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n,
                              "# padding line %03d of a long description\n", i);
    }
    n += (size_t)snprintf(text + n, sizeof text - n,
                          "file sub/b c.bas \r\nfile z.bas\n");
    assert(n > 8192);
    file_write(proj, text, n);
    tiku_ide_project_take(st, proj);
    assert(st->proj.nrow == 3 && !strcmp(st->proj.row[1].name, "sub/b c.bas"));
    tiku_list_select_only(&st->tv_list, 0);
    tiku_ide_project_remove_file(st);
    cut = strlen("title Big\n");
    memmove(text + cut, text + cut + strlen("file a.bas\n"), n - cut - strlen("file a.bas\n"));
    n -= strlen("file a.bas\n");
    f = fopen(proj, "rb");
    assert(f);
    got = fread(back, 1, sizeof back, f);
    fclose(f);
    assert(got == n && !memcmp(back, text, n));
    assert(st->proj.nrow == 2 && !strcmp(st->proj.row[0].name, "sub/b c.bas"));
    tiku_list_select_only(&st->tv_list, 0);
    tiku_ide_project_remove_file(st);
    assert(st->proj.nrow == 1 && !strcmp(st->proj.row[0].name, "z.bas"));
    d = opendir(folder);
    assert(d);
    while ((ent = readdir(d)) != NULL) {
        entries += ent->d_name[0] != '.';
    }
    closedir(d);
    assert(entries == 1);
    st->proj_file[0] = '\0';
    memset(&st->proj, 0, sizeof st->proj);
    assert(unlink(proj) == 0 && rmdir(folder) == 0);
}

static void test_empty_startup(const tiku_app_services_t *services,
                               const char *snapshot)
{
    /* A saved draft must not open an editor on either launch. */
    for (int launch = 0; launch < 2; launch++) {
        void *state = NULL;
        int opened = window_opens;
        assert(access(snapshot, F_OK) == 0);
        assert(tiku_ide_start(&state, services) == 0);
        frames_without_window = 0;
        ide_t *fresh = state;
        tiku_ide_tick(fresh, 1);
        assert(!frames_without_window);
        if (!launch) {
            /* Dismiss before the next splash stage: the former grey box. */
            tiku_event_t dismiss = {.type = TIKU_EVENT_KEY_DOWN,
                                     .key = TIKU_KEY_ESCAPE};
            tiku_ide_event(fresh, fresh->splash_id, &dismiss);
        }
        tiku_ide_tick(fresh, SPLASH_US + 2);
        tiku_ide_tick(fresh, SPLASH_US + 4000000);
        assert(fresh->proj_id && !fresh->splash_surface);
        assert(!frames_without_window && last_frame == fresh->proj_id);
        assert((fresh->proj_surface->px[100 * IDE_W + 100] & 0xffffffu) ==
               (TIKU_C_DOC & 0xffffffu));
        assert(!fresh->proj.nrow && !fresh->proj.dir[0]);
        assert(window_opens == opened + 2);
        assert(!strcmp(last_window, "Project"));
        for (int i = 0; i < IDE_EDITS; i++) {
            assert(!fresh->edit[i].surface && !fresh->edit[i].path[0]);
        }
        tiku_ide_stop(fresh);
        assert(access(snapshot, F_OK) == 0);
    }
}

/** Closing the Project window ends the IDE: each page goes the way a
 *  desktop close takes it, a modified page's draft kept, and no question
 *  is left open on a window that is gone. */
static void test_project_window_closed(const tiku_app_services_t *services,
                                       const char *dir)
{
    tiku_event_t dismiss = { .type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE };
    void *state = NULL;
    char draft[sizeof ((ide_t *)0)->edit[0].recovery];
    ide_t *fresh;
    edit_t *e;

    assert(tiku_ide_start(&state, services) == 0);
    fresh = state;
    tiku_ide_tick(fresh, 1);
    tiku_ide_event(fresh, fresh->splash_id, &dismiss);
    tiku_ide_tick(fresh, SPLASH_US + 2);
    assert(fresh->proj_id);
    e = &fresh->edit[0];
    e->w = 620; e->h = 420; e->id = 77; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->path, sizeof e->path, "%s/page.bas", dir);
    snprintf(e->recovery, sizeof e->recovery, "%s/page.draft", dir);
    snprintf(draft, sizeof draft, "%s", e->recovery);
    e->recovery_fd = open(dir, O_RDONLY);
    assert(e->recovery_fd >= 0);
    set_text(e, "10 PRINT 1");
    tiku_ide_place(e, 0, 10);
    type_text(e, "2");
    closed_n = 0;
    tiku_ide_app.closed(fresh, fresh->proj_id);
    assert(!e->surface && closed_n >= 1 && closed_id[0] == 77);
    assert(access(draft, F_OK) == 0 && !fresh->ask.open && !fresh->proj_id);
    tiku_ide_stop(fresh);
    assert(unlink(draft) == 0);
}

/** Open @p path on a page of @p st, typing @p add at the end of its text. */
static edit_t *page_with_edit(ide_t *st, const char *path, char add)
{
    prow_t row;
    edit_t *e;
    const char *leaf = strrchr(path, '/');

    memset(&row, 0, sizeof row);
    snprintf(row.path, sizeof row.path, "%s", path);
    snprintf(row.name, sizeof row.name, "%s", leaf ? leaf + 1 : path);
    tiku_ide_edit_open(st, &row);
    e = tiku_ide_edit_of_path(st, path);
    assert(e != NULL && !tiku_editdoc_modified(e->doc));
    tiku_ide_place(e, 0, INT64_MAX);
    {
        char one[2] = { add, '\0' };

        type_text(e, one);
    }
    return e;
}

/** What @p path holds, NUL-closed, read into @p out. */
static const char *file_text(const char *path, char *out, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    assert(f);
    n = fread(out, 1, max - 1, f);
    out[n] = '\0';
    fclose(f);
    return out;
}

/** Save leaves a read-only or hard-linked file alone, and asks before
 *  saving over a file that changed on disk since the page read it. */
static void test_safe_save(const tiku_app_services_t *services,
                           const char *dir)
{
    tiku_event_t save = { .type = TIKU_EVENT_KEY_DOWN, .key = 's',
                          .modifiers = TIKU_MOD_CMD };
    tiku_event_t esc = { .type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE };
    tiku_event_t press = { .type = TIKU_EVENT_POINTER_DOWN, .button = 1 };
    char drafts[600], ro[600], hl[600], alias[600], chg[600], back[64];
    void *state = NULL;
    ide_t *fresh;
    edit_t *e;
    int x, y, w, h;

    snprintf(drafts, sizeof drafts, "%s/drafts", dir);
    assert(mkdir(drafts, 0700) == 0);
    assert(setenv("TIKU_IDE_RECOVERY_DIR", drafts, 1) == 0);
    assert(tiku_ide_start(&state, services) == 0);
    fresh = state;
    tiku_ide_tick(fresh, 1);
    tiku_ide_event(fresh, fresh->splash_id, &esc);
    tiku_ide_tick(fresh, SPLASH_US + 2);
    assert(fresh->proj_id && fresh->proj_surface);

    snprintf(ro, sizeof ro, "%s/ro.bas", dir);
    file_write(ro, "10 A\n", 5);
    assert(chmod(ro, 0444) == 0);
    e = page_with_edit(fresh, ro, 'x');
    tiku_ide_event(fresh, e->id, &save);
    assert(strstr(e->feedback, "read-only") && !fresh->ask.open);
    assert(!strcmp(file_text(ro, back, sizeof back), "10 A\n"));
    assert(tiku_editdoc_modified(e->doc));
    tiku_editdoc_saved(e->doc, tiku_editdoc_state(e->doc));
    tiku_ide_edit_drop(fresh, e);
    assert(chmod(ro, 0644) == 0 && unlink(ro) == 0);

    snprintf(hl, sizeof hl, "%s/hl.bas", dir);
    snprintf(alias, sizeof alias, "%s/alias.bas", dir);
    file_write(hl, "10 A\n", 5);
    assert(link(hl, alias) == 0);
    e = page_with_edit(fresh, hl, 'x');
    tiku_ide_event(fresh, e->id, &save);
    assert(strstr(e->feedback, "hard link") && !fresh->ask.open);
    assert(!strcmp(file_text(hl, back, sizeof back), "10 A\n"));
    tiku_editdoc_saved(e->doc, tiku_editdoc_state(e->doc));
    tiku_ide_edit_drop(fresh, e);
    assert(unlink(hl) == 0 && unlink(alias) == 0);

    snprintf(chg, sizeof chg, "%s/chg.bas", dir);
    file_write(chg, "10 A\n", 5);
    e = page_with_edit(fresh, chg, 'x');
    file_write(chg, "10 EXTERNAL\n", 12);
    tiku_ide_event(fresh, e->id, &save);
    assert(fresh->ask.open && strstr(fresh->ask.text, "changed on disk"));
    assert(!strcmp(file_text(chg, back, sizeof back), "10 EXTERNAL\n"));
    tiku_ide_event(fresh, fresh->proj_id, &esc);
    assert(!fresh->ask.open && tiku_editdoc_modified(e->doc));
    assert(!strcmp(file_text(chg, back, sizeof back), "10 EXTERNAL\n"));
    tiku_ide_event(fresh, e->id, &save);
    assert(fresh->ask.open);
    tiku_alert_button_rect(&fresh->ask, 1, fresh->ask_frame.w,
                           fresh->ask_frame.h, &x, &y, &w, &h);
    press.x = fresh->ask_frame.x + x + w / 2;
    press.y = fresh->ask_frame.y + y + h / 2;
    tiku_ide_event(fresh, fresh->proj_id, &press);
    press.type = TIKU_EVENT_POINTER_UP;
    tiku_ide_event(fresh, fresh->proj_id, &press);
    assert(!fresh->ask.open && !tiku_editdoc_modified(e->doc));
    assert(!strcmp(file_text(chg, back, sizeof back), "10 Ax\n"));
    type_text(e, "y");
    tiku_ide_event(fresh, e->id, &save);
    assert(!fresh->ask.open && !tiku_editdoc_modified(e->doc));
    assert(!strcmp(file_text(chg, back, sizeof back), "10 Axy\n"));
    tiku_ide_edit_drop(fresh, e);
    tiku_ide_stop(fresh);
    assert(unlink(chg) == 0 && rmdir(drafts) == 0);
    unsetenv("TIKU_IDE_RECOVERY_DIR");
}

static void test_reopen(ide_t *st, const char *dir)
{
    char recovery[512], lock[680], path[512], snapshot[680];
    edit_t *first = calloc(1, sizeof *first), *other = calloc(1, sizeof *other);
    edit_t *restored;
    FILE *f;
    char disk[32];

    assert(first && other);
    snprintf(recovery, sizeof recovery, "%s/recovery", dir);
    assert(setenv("TIKU_IDE_RECOVERY_DIR", recovery, 1) == 0);
    snprintf(path, sizeof path, "%s/reopen.bas", dir);
    file_write(path, "10 END\n", 7);
    snprintf(first->path, sizeof first->path, "%s", path);
    tiku_ide_edit_load(first);
    tiku_ide_recovery_open(first);
    assert(first->recovery_fd >= 0);
    tiku_ide_place(first, 0, 6);
    type_text(first, " ' unsaved");
    assert(tiku_ide_recovery_write(first));
    snprintf(snapshot, sizeof snapshot, "%s", first->recovery);
    snprintf(lock, sizeof lock, "%s.lock", first->recovery);
    snprintf(other->path, sizeof other->path, "%s", path);
    tiku_ide_edit_load(other);
    tiku_ide_recovery_open(other);
    assert(other->recovery_fd == -1); /* Never steal another editor's checkpoint. */
    tiku_ide_recovery_scan(st);
    assert(!tiku_ide_edit_of_path(st, path)); /* Explicit recovery skips active instances. */
    tiku_ide_recovery_release(first, 0);
    test_empty_startup(st->services, snapshot);
    tiku_ide_pick(st, st->proj_id, CMD_RECOVER);
    restored = tiku_ide_edit_of_path(st, path);
    assert(restored && strstr(restored->feedback, "Recovered"));
    assert(tiku_editdoc_modified(restored->doc));
    assert(strstr(line_of(restored, 0), "unsaved"));
    f = fopen(path, "r");
    assert(f && fgets(disk, sizeof disk, f));
    fclose(f);
    assert(!strcmp(disk, "10 END\n"));
    file_write(restored->recovery, "broken header\n", 14);
    tiku_ide_recovery_restore(restored);
    assert(restored->recovery_blocked);
    assert(!tiku_ide_recovery_write(restored));
    assert(access(restored->recovery, F_OK) == 0);
    tiku_ide_recovery_release(restored, 1);
    tiku_ide_text_free(restored);
    tiku_surface_free(restored->surface);
    restored->surface = NULL;
    tiku_ide_text_free(first);
    tiku_ide_text_free(other);
    free(first); free(other);
    unlink(lock);
    unlink(path);
    assert(rmdir(recovery) == 0);
    unsetenv("TIKU_IDE_RECOVERY_DIR");
}

static void test_console_errors(const tiku_app_services_t *services)
{
    static const int endings[] = { TIKU_APP_BOARD_ENDED,
        TIKU_APP_BOARD_FAILED, TIKU_APP_BOARD_STOPPED };
    static const char *const status[] = { "ended", "failed", "stopped" };
    ide_t *st = calloc(1, sizeof *st);

    assert(st);
    st->services = services;
    st->proj_id = 1;
    for (int i = 0; i < 3; i++) {
        st->running = 1;
        st->nmsg = 0;
        snprintf(st->board, sizeof st->board, "board-a");
        tiku_ide_console_open(st, "main.bas", st->board);
        int opened = window_opens;
        int start = st->ncon;
        tiku_ide_board_said(st, TIKU_APP_BOARD_LINE, "? syntax");
        tiku_ide_board_said(st, TIKU_APP_BOARD_LINE, "at line 20");
        tiku_ide_board_said(st, endings[i], NULL);
        assert(!st->running && !st->board[0]);
        assert(!st->msg_surface && !st->msg_id);
        assert(window_opens == opened);
        assert(!strcmp(st->con[start], "? syntax"));
        assert(!strcmp(st->con[start + 1], "at line 20"));
        assert(!strcmp(st->con[st->ncon - 1], status[i]));
    }
    tiku_surface_free(st->con_surface);
    st->con_surface = NULL;
    /* If no console is available, diagnostics must still be visible. */
    st->running = 1;
    st->nmsg = 0;
    snprintf(st->board, sizeof st->board, "board-a");
    tiku_ide_board_said(st, TIKU_APP_BOARD_FAILED, NULL);
    assert(st->msg_surface && st->nmsg == 1);
    assert(!strcmp(last_window, "Messages"));
    tiku_ide_stop(st);
}

/*
 * What a compiler says, read the same way whichever one said it.
 *
 * The two the tree is built with lay a complaint out differently: gcc
 * names the function before saying anything is wrong, clang counts the
 * errors up after, and both quote the source underneath.  So the error
 * is never the same ROW twice, and a window that opened on a row number
 * would open on the function name under one compiler and the complaint
 * under the other.  It opens on the first row that NAMES A PLACE, which
 * is the same row under both -- proved here against the real output of
 * each, so neither has to be installed to prove the other's.
 */
static void test_diagnostics(const tiku_app_services_t *services)
{
    static const char *const GCC[] = {
        "bad.c: In function 'main':",
        "bad.c:4:12: error: 'nope' undeclared (first use in this function)",
        "    4 |     return nope;",
        "      |            ^~~~",
        "bad.c:4:12: note: each undeclared identifier is reported only once",
        NULL
    };
    static const char *const CLANG[] = {
        "bad.c:4:12: error: use of undeclared identifier 'nope'",
        "    4 |     return nope;",
        "      |            ^~~~",
        "1 error generated.",
        NULL
    };
    static const char *const *const SAID[] = { GCC, CLANG };
    static const char *const WHO[] = { "gcc", "clang" };

    for (int which = 0; which < 2; which++) {
        ide_t *st = calloc(1, sizeof *st);
        int i;

        assert(st);
        st->services = services;
        st->proj_id = 1;
        snprintf(st->child_file, sizeof st->child_file, "bad.c");
        for (i = 0; SAID[which][i] != NULL; i++)
            tiku_ide_message_take(st, SAID[which][i]);
        /* The quoted source and the caret are not rows: a person cannot
         * act on them and the complaint above already names the line. */
        for (i = 0; i < st->nmsg; i++)
            assert(!strstr(st->msg[i].text, "|"));
        assert(st->nmsg == (which == 0 ? 3 : 2));
        last_menu_item[0] = '\0';
        tiku_ide_messages_show(st);
        assert(st->msg_surface != NULL);
        /* Not the first row under either compiler, and the same one. */
        assert(tiku_list_chosen(&st->msg_list) == (which == 0 ? 1 : 0));
        assert(!strcmp(last_menu_item, "Go to line 4"));
        (void)WHO[which];
        /* A second look keeps what the person was reading. */
        tiku_list_select_only(&st->msg_list, st->nmsg - 1);
        tiku_ide_messages_show(st);
        assert(tiku_list_chosen(&st->msg_list) == st->nmsg - 1);
        tiku_ide_stop(st);
    }
}

static void test_splash_license(void)
{
    for (int size = 12; size <= 16; size += 4) {
        tiku_font_set_size(size);
        tiku_surface_t *s = tiku_surface_new(SPLASH_W, SPLASH_H, TIKU_C_DOC);
        tiku_dl_t *dl = tiku_dl_new();
        unsigned char wire[16384];
        char words[8192], path[100];
        size_t n = 0, at = 0;
        tiku_dl_fact_t fact;
        assert(s && dl);
        tiku_ide_announce_paint_art(s, "Opening the project", NULL);
        snprintf(path, sizeof path, "/tmp/tikubasic-splash-license-%d.png", size);
        assert(!tiku_surface_png(s, path));
        s->record = dl;
        tiku_ide_announce_license(s);
        s->record = NULL;
        tiku_dl_say(dl, words, sizeof words);
        assert(strstr(words, "Apache License, Version 2.0"));
        assert(strstr(words, "WITHOUT WARRANTIES OR CONDITIONS OF"));
        assert(strstr(words, "permissions and limitations under the License."));
        assert(tiku_dl_flatten(dl, wire, sizeof wire, &n));
        int lines = 0;
        const tiku_font_t *font = tiku_font_at(10);
        while (tiku_dl_read(wire, n, &at, &fact)) {
            if (fact.kind != TIKU_DL_FACT_TEXT) continue;
            assert(fact.rect.x == 18 && fact.rect.y > SPLASH_H / 2);
            assert(fact.rect.x + tiku_text_width(font, fact.text) < SPLASH_W - 18);
            assert(fact.rect.y + font->height - font->ascent < SPLASH_H - 1);
            lines++;
        }
        assert(lines == 7 && tiku_font_size() == size);
        tiku_dl_free(dl);
        tiku_surface_free(s);
    }
    tiku_font_set_size(12);
}

/** @brief Only an explicit launch file populates a fresh TikuBasic session. */
static void test_launch_document(tiku_app_services_t *services, const char *dir)
{
    char source[512], manifest[512];
    snprintf(source, sizeof source, "%s/literal file;name.BAS", dir);
    snprintf(manifest, sizeof manifest, "%s/project.TIKU", dir);
    const char *program = "10 PRINT 9876\n20 END\n";
    const char *project = "title Launch Test\ngroup Sources\nfile literal file;name.BAS\n";
    file_write(source, program, strlen(program));
    file_write(manifest, project, strlen(project));
    int before_runs = runs, before_saves = saves;
    void *state = NULL;
    services->startup_document = source;
    assert(!tiku_ide_start(&state, services));
    ide_t *opened = state;
    edit_t *e = tiku_ide_edit_of_path(opened, source);
    assert(e && !e->load_failed && !tiku_editdoc_modified(e->doc));
    assert(line_is(e, 0, "10 PRINT 9876"));
    assert(!opened->proj_file[0] && !opened->running);
    tiku_ide_stop(opened);
    services->startup_document = manifest;
    assert(!tiku_ide_start(&state, services));
    opened = state;
    assert(!strcmp(opened->proj_file, manifest));
    assert(!strcmp(opened->proj.title, "Launch Test"));
    assert(opened->proj.nrow == 2 && !tiku_ide_edit_of_path(opened, source));
    tiku_ide_stop(opened);
    services->startup_document = NULL;
    assert(!tiku_ide_start(&state, services));
    opened = state;
    assert(!opened->proj_file[0] && !opened->proj.nrow);
    assert(!tiku_ide_edit_of_path(opened, source));
    tiku_ide_stop(opened);
    assert(runs == before_runs && saves == before_saves);
    assert(!unlink(source) && !unlink(manifest));
}

int main(void)
{
    char dir[TIKU_PATH_MAX] = "/tmp/tikubasic-test-XXXXXX";
    char real[PATH_MAX], label[160];
    tiku_app_services_t services = { .current_device = current,
        .board_names = names, .board_save = save, .board_run = run,
        .menus = menus, .frame = frame, .open = open_window, .close = close_window,
        .clip_set = clip_set, .clip_text = clip_text };
    ide_t *st = calloc(1, sizeof *st);

    assert(st && mkdtemp(dir));
    /* Resolved, because the code under test resolves what it is given:
     * on a host where /tmp is a symlink (macOS points it at
     * /private/tmp) every assertion below would be comparing the name
     * handed in against the name handed back. */
    assert(realpath(dir, real));
    snprintf(dir, sizeof dir, "%s", real);
    st->proj_w = IDE_W; st->proj_h = IDE_H;   /* as ide_start leaves them */
    assert(tiku_ide_project_body(st).y == MARGIN);
    assert(tiku_ide_project_body(st).h == IDE_H - STRIP_H - 2 * MARGIN);
    st->services = &services;
    st->proj_id = 1;
    tiku_ide_target_text(st, label, sizeof label);
    assert(strstr(label, "board-a") && strstr(label, "Ready"));
    selected = "offline";
    tiku_ide_target_text(st, label, sizeof label);
    assert(strstr(label, "Disconnected"));
    selected = "";
    tiku_ide_target_text(st, label, sizeof label);
    assert(strstr(label, "This computer"));
    st->running = 1;
    snprintf(st->board, sizeof st->board, "board-a");
    selected = "board-b";
    tiku_ide_target_text(st, label, sizeof label);
    assert(strstr(label, "Running: board-a"));
    st->running = 0;
    selected = "board-a";
    test_upload(st, dir);
    test_search();
    test_recovery(dir);
    test_ui(st, dir);
    test_click_then_type(st);
    test_renumber_bounds();
    test_keyword_over_selection(st);
    test_follow_drops_selection(st);
    test_return_makes_the_break(st);
    test_unbound_cmd_chords(st);
    test_find_bar(st);
    test_renumber_targets();
    test_caret_past_255(st);
    test_add_file(st, dir);
    test_remove_file(st, dir);
    test_reopen(st, dir);
    test_console_errors(&services);
    test_diagnostics(&services);
    test_splash_license();
    test_launch_document(&services, dir);
    test_project_window_closed(&services, dir);
    test_safe_save(&services, dir);
    assert(rmdir(dir) == 0);
    free(st);
    puts("PASS: upload boundaries/confirmation, target state, replacement/undo, recovery");
    return 0;
}
