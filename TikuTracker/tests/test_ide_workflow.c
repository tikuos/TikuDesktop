/* Host regressions for upload preflight, search, recovery and target state. */
#include <assert.h>
#define TIKU_APP_EMBED
#include "../../apps/ide/tiku_ide.c"

static const char *selected = "board-a";
static int saves, runs;
static char sent_board[64], sent_name[8], sent_program[8192];

static int current(void *ctx, char *out, size_t max)
{ (void)ctx; return snprintf(out, max, "%s", selected); }
static int names(void *ctx, char *out, size_t max)
{ (void)ctx; return snprintf(out, max, "board-a\nboard-b\n"); }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{ (void)ctx; (void)id; (void)set; return 0; }
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; return 0; }
static uint32_t open_window(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)title; (void)w; (void)h; return 99; }
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

static void test_reopen(ide_t *st, const char *dir)
{
    char recovery[512], lock[680], path[512];
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
    snprintf(lock, sizeof lock, "%s.lock", first->recovery);
    snprintf(other->path, sizeof other->path, "%s", path);
    edit_load(other);
    recovery_open(other);
    assert(other->recovery_fd == -1); /* Never steal another editor's checkpoint. */
    recovery_scan(st);
    assert(!edit_of_path(st, path)); /* Active instance is skipped on startup. */
    recovery_release(first, 0);
    recovery_scan(st);
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
    assert(rmdir(dir) == 0);
    free(st);
    puts("PASS: upload boundaries/confirmation, target state, replacement/undo, recovery");
    return 0;
}
