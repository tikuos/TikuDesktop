/* Host regressions for upload preflight, search, recovery and target state. */
#include <assert.h>
#include "tiku_dl.h"
#define TIKU_APP_EMBED
#include "../../apps/ide/tiku_ide.c"

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
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{ (void)ctx; (void)id; (void)set; return 0; }
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
static void close_window(void *ctx, uint32_t id)
{ (void)ctx; (void)id; }
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
    assert(upload_read(st, path, out, sizeof out));
    file_write(path, large, 8192);
    assert(!upload_read(st, path, out, sizeof out));
    assert(strstr(st->note, "too large"));
    run_file_on_board(st, path, "board-a");
    assert(!runs);
    file_write(path, "x\0y", 3);
    assert(!upload_read(st, path, out, sizeof out));
    file_write(path, "10 PRINT 42\n", 12);
    save_file_on_board(st, path);
    assert(st->ask.open && !saves);
    assert(strstr(st->ask.text, "/data/longfil.bas"));
    assert(strstr(st->ask.text, "Replace any existing"));
    assert(st->ask.dflt == 0);
    ide_event(st, st->proj_id, &enter);
    assert(!saves && !st->ask.open);
    save_file_on_board(st, path);
    selected = "board-b";
    file_write(path, "10 END\n", 7);
    upload_confirm(st);
    assert(saves == 1 && !strcmp(sent_board, "board-a"));
    assert(!strcmp(sent_name, "longfil"));
    assert(!strcmp(sent_program, "10 PRINT 42\n"));
    st->running = st->saving = 0;
    tiku_alert_reset(&st->ask);
    snprintf(st->upload_board, sizeof st->upload_board, "offline");
    upload_confirm(st);
    assert(saves == 1);
    unlink(path);
}

static void test_search(void)
{
    edit_t *e = calloc(1, sizeof *e);
    char original[4096], actual[4096];
    int i;

    assert(e);
    e->w = 620; e->h = 420;
    e->lang = TIKU_SYNTAX_BASIC;
    tiku_textview_init(&e->tv);
    tiku_textview_set(&e->tv, "10 PRINT 10\n20 PRINT 10\n\tREM café");
    tiku_textview_text(&e->tv, original, sizeof original);
    search_open(e, 1);
    tiku_field_start(&e->find, "10");
    tiku_field_start(&e->replacement, "100");
    search_replace(e, 0); /* Select only first. */
    assert(!tiku_textview_modified(&e->tv));
    search_replace(e, 1);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "100 PRINT 100"));
    assert(!strcmp(tiku_textview_line(&e->tv, 2), "\tREM café"));
    assert(tiku_textview_undo(&e->tv));
    tiku_textview_text(&e->tv, actual, sizeof actual);
    assert(!strcmp(actual, original));
    assert(tiku_textview_redo(&e->tv));
    tiku_field_start(&e->find, "100");
    tiku_field_start(&e->replacement, "");
    search_replace(e, 1);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), " PRINT "));
    assert(tiku_textview_undo(&e->tv));
    for (i = 0; i < 600; i++) { original[i] = 'a'; }
    original[600] = '\0';
    tiku_textview_set(&e->tv, original);
    tiku_field_start(&e->find, "a");
    tiku_field_start(&e->replacement, "aa");
    search_replace(e, 1);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), original));
    assert(!tiku_textview_modified(&e->tv));
    tiku_field_start(&e->replacement, "b");
    search_replace(e, 1); /* More matches than the undo journal's capacity. */
    assert(tiku_textview_line(&e->tv, 0)[599] == 'b');
    assert(tiku_textview_undo(&e->tv));
    assert(!strcmp(tiku_textview_line(&e->tv, 0), original));
    tiku_textview_free(&e->tv);
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
    tiku_textview_init(&e->tv);
    tiku_textview_set(&e->tv, "10 PRINT 42");
    e->tv.saved_at = -1;
    assert(recovery_write(e));
    f = fopen(e->path, "r");
    assert(f && fgets(original, sizeof original, f));
    fclose(f);
    assert(!strcmp(original, "10 END\n"));
    tiku_textview_set(&e->tv, "10 END");
    recovery_restore(e);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 PRINT 42"));
    assert(tiku_textview_modified(&e->tv));
    assert(strstr(e->feedback, "Recovered"));
    assert(edit_save(e));
    assert(access(draft, F_OK) != 0);
    assert(!tiku_textview_modified(&e->tv));
    tiku_textview_select_all(&e->tv);
    tiku_textview_insert_text(&e->tv, "10 END\n");
    assert(recovery_write(e));
    tiku_textview_set(&e->tv, "other text");
    recovery_restore(e);
    assert(tiku_textview_lines(&e->tv) == 2); /* Preserve a blank final line. */
    tiku_textview_insert_text(&e->tv, "abc");
    assert(recovery_write(e));
    recovery_release(e, 1);
    assert(access(draft, F_OK) != 0);
    unlink(e->path);
    tiku_textview_free(&e->tv);
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
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->name, sizeof e->name, "sensor.bas");
    tiku_textview_init(&e->tv);
    tiku_textview_set(&e->tv, "10 PRINT \"Temperature\"\n20 PRINT 42\n30 END");
    {
        tiku_text_input_t input;
        tiku_event_t text;
        const char *phrase = "REM café 日本語";
        tiku_textview_select_all(&e->tv);
        assert(tiku_text_input_set(&input, phrase, strlen(phrase)));
        while (tiku_text_input_next(&input, &text)) {
            ide_event(st, e->id, &text);
            if (!(text.button & TIKU_TEXT_END)) assert(strstr(tiku_textview_line(&e->tv, 0), "Temperature"));
        }
        assert(!strcmp(tiku_textview_line(&e->tv, 0), phrase) && runs == previous_runs);
        assert(tiku_textview_undo(&e->tv));
        assert(strstr(tiku_textview_line(&e->tv, 0), "Temperature"));
        tiku_textview_select_none(&e->tv);
        search_open(e, 0);
        assert(tiku_text_input_set(&input, "café", 5));
        while (tiku_text_input_next(&input, &text)) ide_event(st, e->id, &text);
        assert(!strcmp(e->find.text, "café"));
        e->finding = 0;
    }
    ide_pick(st, e->id, CMD_REPLACE);
    tiku_field_start(&e->find, "PRINT");
    tiku_field_start(&e->replacement, "REM");
    ide_event(st, e->id, &ev);
    assert(e->find_focus == 1);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.x = search_button(2).x + 8;
    ev.y = search_button(2).y + 8;
    ide_event(st, e->id, &ev);
    assert(strstr(tiku_textview_line(&e->tv, 0), "REM"));
    assert(ide_pick(st, e->id, CMD_UNDO) == 0);
    assert(strstr(tiku_textview_line(&e->tv, 0), "PRINT"));
    snprintf(st->proj.row[0].path, sizeof st->proj.row[0].path, "%s/open.bas", dir);
    snprintf(e->path, sizeof e->path, "%s", st->proj.row[0].path);
    snprintf(st->proj.row[0].name, sizeof st->proj.row[0].name, "open.bas");
    st->proj.nrow = 1;
    tiku_list_init(&st->msg_list, 1);
    search_project(st, e);
    assert(st->nmsg == 2 && st->msg[0].line == 1 && !st->msg[0].numbered);
    tiku_field_start(&e->find, "missing needle");
    search_project(st, e);
    assert(!st->nmsg);
    tiku_field_start(&e->find, "PRINT");
    st->proj.nrow = 0;
    selected = "nrf54l15";
    edit_paint(st, e);
    assert(tiku_surface_png(e->surface, "/tmp/tikubasic-find-replace.png") == 0);
    st->proj_surface = tiku_surface_new(IDE_W, IDE_H, TIKU_C_PANEL);
    assert(st->proj_surface);
    tiku_list_init(&st->tv_list, 1);
    snprintf(missing, sizeof missing, "%s/longfilename.bas", dir);
    file_write(missing, "10 PRINT 42\n", 12);
    selected = "board-a";
    save_file_on_board(st, missing);
    project_paint(st);
    assert(tiku_surface_png(st->proj_surface, "/tmp/tikubasic-upload.png") == 0);
    tiku_alert_reset(&st->ask);
    unlink(missing);
    snprintf(missing, sizeof missing, "%s/missing/source.bas", dir);
    snprintf(e->path, sizeof e->path, "%s", missing);
    tiku_textview_insert_text(&e->tv, "x");
    selected = "board-a";
    run_file_where(st, e->path);
    assert(runs == previous_runs);
    assert(strstr(st->note, "cancelled"));
    snprintf(e->path, sizeof e->path, "%s/oversized.bas", dir);
    {
        char long_line[1100];
        memset(long_line, 'x', sizeof long_line);
        file_write(e->path, long_line, sizeof long_line);
        tiku_textview_free(&e->tv);
        edit_load(e);
        assert(e->load_failed && !edit_save(e));
        run_file_where(st, e->path);
        assert(runs == previous_runs);
        unlink(e->path);
        e->load_failed = 0;
        tiku_textview_insert_text(&e->tv, "10 END");
    }
    edit_drop(st, e);
    assert(e->surface && st->ask.open && st->ask.tag == CMD_QUIT);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = TIKU_KEY_ESCAPE;
    ide_event(st, st->proj_id, &ev);
    assert(e->surface && !st->ask.open);
    tiku_textview_free(&e->tv);
    tiku_surface_free(e->surface);
    e->surface = NULL;
    tiku_surface_free(st->proj_surface);
    st->proj_surface = NULL;
    tiku_surface_free(st->msg_surface);
    st->msg_surface = NULL;
}

static void test_empty_startup(const tiku_app_services_t *services,
                               const char *snapshot)
{
    /* A saved draft must not open an editor on either launch. */
    for (int launch = 0; launch < 2; launch++) {
        void *state = NULL;
        int opened = window_opens;
        assert(access(snapshot, F_OK) == 0);
        assert(ide_start(&state, services) == 0);
        frames_without_window = 0;
        ide_t *fresh = state;
        ide_tick(fresh, 1);
        assert(!frames_without_window);
        if (!launch) {
            /* Dismiss before the next splash stage: the former grey box. */
            tiku_event_t dismiss = {.type = TIKU_EVENT_KEY_DOWN,
                                     .key = TIKU_KEY_ESCAPE};
            ide_event(fresh, fresh->splash_id, &dismiss);
        }
        ide_tick(fresh, SPLASH_US + 2);
        ide_tick(fresh, SPLASH_US + 4000000);
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
        ide_stop(fresh);
        assert(access(snapshot, F_OK) == 0);
    }
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
    edit_load(first);
    recovery_open(first);
    assert(first->recovery_fd >= 0);
    tiku_textview_place(&first->tv, 0, 6);
    tiku_textview_insert_text(&first->tv, " ' unsaved");
    assert(recovery_write(first));
    snprintf(snapshot, sizeof snapshot, "%s", first->recovery);
    snprintf(lock, sizeof lock, "%s.lock", first->recovery);
    snprintf(other->path, sizeof other->path, "%s", path);
    edit_load(other);
    recovery_open(other);
    assert(other->recovery_fd == -1); /* Never steal another editor's checkpoint. */
    recovery_scan(st);
    assert(!edit_of_path(st, path)); /* Explicit recovery skips active instances. */
    recovery_release(first, 0);
    test_empty_startup(st->services, snapshot);
    ide_pick(st, st->proj_id, CMD_RECOVER);
    restored = edit_of_path(st, path);
    assert(restored && strstr(restored->feedback, "Recovered"));
    assert(tiku_textview_modified(&restored->tv));
    assert(strstr(tiku_textview_line(&restored->tv, 0), "unsaved"));
    f = fopen(path, "r");
    assert(f && fgets(disk, sizeof disk, f));
    fclose(f);
    assert(!strcmp(disk, "10 END\n"));
    file_write(restored->recovery, "broken header\n", 14);
    recovery_restore(restored);
    assert(restored->recovery_blocked);
    assert(!recovery_write(restored));
    assert(access(restored->recovery, F_OK) == 0);
    recovery_release(restored, 1);
    tiku_textview_free(&restored->tv);
    tiku_surface_free(restored->surface);
    restored->surface = NULL;
    tiku_textview_free(&first->tv);
    tiku_textview_free(&other->tv);
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
        console_open(st, "main.bas", st->board);
        int opened = window_opens;
        int start = st->ncon;
        ide_board_said(st, TIKU_APP_BOARD_LINE, "? syntax");
        ide_board_said(st, TIKU_APP_BOARD_LINE, "at line 20");
        ide_board_said(st, endings[i], NULL);
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
    ide_board_said(st, TIKU_APP_BOARD_FAILED, NULL);
    assert(st->msg_surface && st->nmsg == 1);
    assert(!strcmp(last_window, "Messages"));
    ide_stop(st);
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
        announce_paint_art(s, "Opening the project", NULL);
        snprintf(path, sizeof path, "/tmp/tikubasic-splash-license-%d.png", size);
        assert(!tiku_surface_png(s, path));
        s->record = dl;
        announce_license(s);
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
    assert(!ide_start(&state, services));
    ide_t *opened = state;
    edit_t *e = edit_of_path(opened, source);
    assert(e && !e->load_failed && !tiku_textview_modified(&e->tv));
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 PRINT 9876"));
    assert(!opened->proj_file[0] && !opened->running);
    ide_stop(opened);
    services->startup_document = manifest;
    assert(!ide_start(&state, services));
    opened = state;
    assert(!strcmp(opened->proj_file, manifest));
    assert(!strcmp(opened->proj.title, "Launch Test"));
    assert(opened->proj.nrow == 2 && !edit_of_path(opened, source));
    ide_stop(opened);
    services->startup_document = NULL;
    assert(!ide_start(&state, services));
    opened = state;
    assert(!opened->proj_file[0] && !opened->proj.nrow);
    assert(!edit_of_path(opened, source));
    ide_stop(opened);
    assert(runs == before_runs && saves == before_saves);
    assert(!unlink(source) && !unlink(manifest));
}

int main(void)
{
    char dir[] = "/tmp/tikubasic-test-XXXXXX", label[160];
    tiku_app_services_t services = { .current_device = current,
        .board_names = names, .board_save = save, .board_run = run,
        .menus = menus, .frame = frame, .open = open_window, .close = close_window };
    ide_t *st = calloc(1, sizeof *st);

    assert(st && mkdtemp(dir));
    assert(project_body().y == MARGIN);
    assert(project_body().h == IDE_H - STRIP_H - 2 * MARGIN);
    st->services = &services;
    st->proj_id = 1;
    target_text(st, label, sizeof label);
    assert(strstr(label, "board-a") && strstr(label, "Ready"));
    selected = "offline";
    target_text(st, label, sizeof label);
    assert(strstr(label, "Disconnected"));
    selected = "";
    target_text(st, label, sizeof label);
    assert(strstr(label, "This computer"));
    st->running = 1;
    snprintf(st->board, sizeof st->board, "board-a");
    selected = "board-b";
    target_text(st, label, sizeof label);
    assert(strstr(label, "Running: board-a"));
    st->running = 0;
    selected = "board-a";
    test_upload(st, dir);
    test_search();
    test_recovery(dir);
    test_ui(st, dir);
    test_reopen(st, dir);
    test_console_errors(&services);
    test_splash_license();
    test_launch_document(&services, dir);
    assert(rmdir(dir) == 0);
    free(st);
    puts("PASS: upload boundaries/confirmation, target state, replacement/undo, recovery");
    return 0;
}
