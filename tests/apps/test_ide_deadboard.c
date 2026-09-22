/* A board that stops answering must not keep the page. */
#include <assert.h>
#define TIKU_APP_EMBED
#include "../../apps/ide/tiku_ide.c"

static int stops;
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; return 0; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{ (void)ctx; (void)id; (void)m; return 0; }
static uint32_t opened(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)title; (void)w; (void)h; return 3; }
static void closed(void *ctx, uint32_t id) { (void)ctx; (void)id; }
/* The board took the ask and was never heard from again. */
static int dead_stop(void *ctx) { (void)ctx; stops++; return 0; }

int main(void)
{
    tiku_app_services_t svc = { .frame = frame, .menus = menus,
                                .open = opened, .close = closed,
                                .board_stop = dead_stop };
    ide_t *st = calloc(1, sizeof *st);
    edit_t *e;
    tiku_event_t key = { .type = TIKU_EVENT_KEY_DOWN, .key = 'x',
                         .text = "x" };

    assert(st);
    st->services = &svc;
    st->proj_id = 1;
    e = &st->edit[0];
    e->id = 2; e->w = 620; e->h = 540; e->tip = -1;
    e->lang = TIKU_SYNTAX_BASIC;
    e->surface = tiku_surface_new(e->w, e->h, TIKU_C_PANEL);
    assert(e->surface);
    snprintf(e->path, sizeof e->path, "/tmp/tikubasic-dead.bas");
    snprintf(e->name, sizeof e->name, "dead.bas");
    tiku_textview_init(&e->tv);
    tiku_textview_set(&e->tv, "10 PRINT 1");

    /* A run is under way on a board, with the source locked to it. */
    st->running = 1;
    st->debugging = 1;
    snprintf(st->board, sizeof st->board, "nrf54lm20b");
    snprintf(st->child_file, sizeof st->child_file, "%s", e->path);
    assert(debug_locked(st, e));
    (void)ide_event(st, e->id, &key);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 PRINT 1"));

    /* Stop is asked; the board says nothing at all. */
    run_stop(st, "? the run was stopped by hand");
    assert(stops == 1 && st->running);
    ide_tick(st, 1000000);
    assert(st->running);                /* still its own for now */
    ide_tick(st, 1000000 + IDE_BOARD_STOP_US);
    assert(st->running);                /* the grace has not elapsed */
    ide_tick(st, 1000000 + IDE_BOARD_STOP_US + 1);

    /* The page is the person's again: not running, not locked, and it
     * takes a keystroke. */
    assert(!st->running && !st->debugging && st->board[0] == '\0');
    assert(!debug_locked(st, e));
    assert(strstr(e->feedback, "did not answer") != NULL);
    (void)ide_event(st, e->id, &key);
    assert(!strcmp(tiku_textview_line(&e->tv, 0), "10 PRINT 1x") ||
           strchr(tiku_textview_line(&e->tv, 0), 'x') != NULL);
    puts("PASS: a board that never answers Stop gives the page back");
    return 0;
}
