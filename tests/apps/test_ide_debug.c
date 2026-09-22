/* Real local interpreter + GUI controller, without a display or a board. */
#include <assert.h>
#include <time.h>
#include "../../apps/ide/tiku_ide_private.h"

static int frames;
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 0; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{ (void)ctx; (void)id; (void)m; return 0; }
static uint32_t opened(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)title; (void)w; (void)h; return 3; }
static void closed(void *ctx, uint32_t id) { (void)ctx; (void)id; }

static void until_paused(ide_t *st, int pc)
{
    int ticks;
    for (ticks = 0; ticks < 2000; ticks++) {
        tiku_ide_debug_tick(st);
        if (pc ? st->debug_paused && st->debug_pc == pc : !st->running) { return; }
        usleep(1000);
    }
    fprintf(stderr, "debugger did not reach %d: %s\n", pc, st->edit[0].feedback);
    for (int i = 0; i < st->ncon; i++) { fprintf(stderr, "%s\n", st->con[i]); }
    assert(0);
}

int main(void)
{
    tiku_app_services_t svc = { .frame = frame, .menus = menus,
                                .open = opened, .close = closed };
    ide_t *st = calloc(1, sizeof *st);
    edit_t *e;
    char dir[] = "/tmp/tikubasic-debug-test-XXXXXX";
    char *binary = realpath("applications/build/runtime/apps/basic/tiku-basic", NULL);
    tiku_event_t key = { .type = TIKU_EVENT_KEY_DOWN, .key = 'x', .text = "x" };

    if (!binary) { binary = realpath("build/runtime/apps/basic/tiku-basic", NULL); }
    assert(st && binary && mkdtemp(dir));
    assert(setenv("TIKU_BASIC", binary, 1) == 0);
    free(binary);
    st->services = &svc; st->proj_id = 1;
    e = &st->edit[0]; e->id = 2; e->w = 620; e->h = 540; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->path, sizeof e->path, "%s/program.bas", dir);
    snprintf(e->name, sizeof e->name, "program.bas");
    tiku_textview_init(&e->tv);
    tiku_textview_set(&e->tv, "10 pri");
    tiku_textview_place(&e->tv, 0, 6);
    tiku_ide_assist_open(e, 1);
    assert(e->assist_n == 1);
    assert(!strcmp(tiku_syntax_basic_keyword(e->assist_match[0]), "PRINT"));
    tiku_ide_edit_paint(st, e);
    assert(tiku_surface_png(e->surface, "/tmp/tikubasic-completion.png") == 0);
    tiku_ide_assist_accept(e);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 PRINT"));
    assert(tiku_textview_undo(&e->tv));
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 pri"));
    tiku_textview_set(&e->tv, "10 REM pri");
    tiku_textview_place(&e->tv, 0, 10);
    tiku_ide_assist_open(e, 1);
    assert(!e->assist_n);
    tiku_textview_set(&e->tv, "10 PRINT \"pri\"");
    tiku_textview_place(&e->tv, 0, 12);
    tiku_ide_assist_open(e, 1);
    assert(!e->assist_n);
    tiku_textview_set(&e->tv,
        "10 A=1\n20 TEMP=42:A$=\"hello\":A=A+2\n30 PRINT A\n40 END");
    tiku_textview_place(&e->tv, 2, 0);
    tiku_ide_debug_break(e);
    tiku_ide_debug_show(e);
    tiku_field_start(&e->watches, "A,TEMP,A$");
    st->stop_asked = 1; /* A new session must not inherit a Stop deadline. */
    tiku_ide_debug_start(st, e);
    assert(st->running && st->local_debug);
    assert(!st->stop_asked);
    until_paused(st, 10);
    assert(!strcmp(e->watch_value[0], "A = 0"));
    assert(!strcmp(e->watch_value[1], "TEMP = <not defined>"));
    tiku_ide_event(st, e->id, &key);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 A=1"));
    tiku_ide_pick(st, e->id, CMD_RENUM);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 A=1"));
    tiku_ide_debug_command(st, "DEBUG STEP");
    until_paused(st, 20);
    assert(!strcmp(e->watch_value[0], "A = 1"));
    tiku_ide_debug_command(st, "DEBUG CONT");
    until_paused(st, 30);
    assert(!strcmp(e->watch_value[0], "A = 3"));
    assert(!strcmp(e->watch_value[1], "TEMP = 42"));
    assert(!strcmp(e->watch_value[2], "A$ = \"hello\""));
    assert(tiku_surface_png(e->surface, "/tmp/tikubasic-debugger.png") == 0);
    tiku_ide_debug_command(st, "DEBUG CONT");
    until_paused(st, 0);
    assert(!st->local_debug && !tiku_ide_debug_locked(st, e));
    tiku_ide_debug_start(st, e);
    until_paused(st, 10);
    tiku_ide_run_stop(st, "stop test");
    assert(!st->running && !st->local_debug);
    assert(frames > 0);
    unlink(e->path);
    assert(rmdir(dir) == 0);
    tiku_textview_free(&e->tv);
    tiku_surface_free(e->surface);
    tiku_surface_free(st->con_surface);
    free(st);
    puts("PASS: completion/undo, comment guards, real debugger, watches, source lock, stop");
    return 0;
}
