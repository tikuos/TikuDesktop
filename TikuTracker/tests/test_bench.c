/* Desktop Bench, isolated metadata/runner fixtures, no device or display. */
#include <assert.h>
#include "../../apps/bench/tiku_bench.c"

static int releases, reclaims, port_released, windows, frames;
static const char *device = "laboratory";
static int names(void *ctx, char *out, size_t cap)
{ (void)ctx; return snprintf(out, cap, "%s\n", device); }
static int current(void *ctx, char *out, size_t cap)
{ (void)ctx; return snprintf(out, cap, "%s", device); }
static int mcu(void *ctx, const char *board, char *out, size_t cap)
{ (void)ctx; (void)board; return snprintf(out, cap, "%s", port_released ? "" : "nRF54L15"); }
static int port(void *ctx, const char *board, char *out, size_t cap)
{ (void)ctx; (void)board; return snprintf(out, cap, "%s", port_released ? "" : "/dev/fixture-only"); }
static int release(void *ctx, const char *board)
{ (void)ctx; assert(!strcmp(board, device)); releases++; port_released = 1; return 1; }
static int reclaim(void *ctx, const char *board)
{ (void)ctx; assert(!strcmp(board, device)); reclaims++; port_released = 0; return 1; }
static uint32_t opened(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)title; (void)w; (void)h; return ++windows; }
static void closed(void *ctx, uint32_t id) { (void)ctx; (void)id; }
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 0; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{ (void)ctx; (void)id; (void)m; return 0; }
static char report_body[15001];
static int report(void *ctx, const char *title, const char *html)
{
    (void)ctx; assert(!strcmp(title, "TikuBench results"));
    assert(strlen(html) <= 15000);
    strcpy(report_body, html); return 1;
}
static tiku_app_services_t svc = {.open = opened, .close = closed, .frame = frame,
    .menus = menus, .current_device = current, .board_names = names,
    .board_mcu = mcu, .board_port = port, .board_release = release, .board_reclaim = reclaim,
    .help_html = report};

/**
 * @brief Tick until the child is gone, giving it @p seconds to do it.
 *
 * A round is a millisecond of ours and ten of the app's clock, so the
 * escalation the cancel checks watch for arrives ten times sooner than
 * it would on a desk.  The budget is the caller's because the fixtures
 * answer at once while the real registry imports every suite it lists.
 */
static void pump_for(bench_t *st, int seconds)
{
    for (int i = 0; i < seconds * 1000 && (st->child || st->loading); i++) {
        bench_tick(st, st->now + 10000);
        usleep(1000);
    }
    if (st->child || st->loading) {
        fprintf(stderr, "pump stalled: child=%ld loading=%d job=%d send=%zu/%zu stop=%lld now=%lld reaped=%d eof=%d\n",
            (long)st->child, st->loading, st->job, st->send_at, st->send_n,
            (long long)st->stop_at, (long long)st->now, st->reaped, st->eof);
        for (int i = 0; i < st->nlog; i++) fprintf(stderr, "%s\n", st->logs[i].text);
    }
    assert(!st->child && !st->loading);
}

static void pump(bench_t *st)
{
    pump_for(st, 5);
}

static bench_t *start(void)
{
    void *state = NULL;
    port_released = 0;
    assert(!bench_start(&state, &svc));
    bench_t *st = state;
    assert(st->loading);
    pump(st);
    assert(st->metadata_ok && st->nsuite == 4 && st->nleaf == 4);
    assert(st->supported && !strcmp(st->target_name, device));
    assert(!strcmp(st->port, "/dev/fixture-only"));
    return st;
}

static const char *arg_value(const command_t *c, const char *key)
{
    for (int i = 0; i + 1 < c->argc; i++) if (!strcmp(c->argv[i], key)) return c->argv[i + 1];
    return NULL;
}

static void model_test(bench_t *st)
{
    assert(csv_has("any", "nrf54l15") && !csv_has("nrf54l15", "nrf54l"));
    st->suites[0].selected = st->suites[1].selected = 1;
    assert(queue_build(st) && st->njob == 2 && !*st->jobs[0].only);
    plan_build(st);
    assert(st->nresult == 2); /* Explicit diagnostics never join a full sweep. */
    clear_selection(st); st->leaves[1].selected = 1;
    assert(queue_build(st) && !strcmp(st->jobs[0].only, "reset"));
    snprintf(st->mcu, sizeof st->mcu, "rp2350"); st->usb = 1; profile_update(st);
    assert(!leaf_supported(st, &st->leaves[1], 0));
    char reason[256];
    assert(strstr(leaf_reason(st, &st->leaves[1], reason, sizeof reason), "cannot survive"));
    assert(!queue_build(st));
    snprintf(st->mcu, sizeof st->mcu, "msp430fr5994"); profile_update(st);
    assert(!suite_supported(st, &st->suites[2]));
    st->large = 1;
    assert(suite_supported(st, &st->suites[2]));
    target_capture(st); clear_selection(st); st->suites[0].selected = 1;
    assert(queue_build(st));
    command_t c;
    assert(command_build(st, &st->jobs[0], &c));
    assert(arg_value(&c, "--skip-build") && !arg_value(&c, "--flash"));
    assert(!strcmp(arg_value(&c, "--port"), "/dev/fixture-only"));
    st->usb = st->flash = st->pk = st->epaper = 1;
    assert(command_build(st, &st->jobs[0], &c));
    assert(!arg_value(&c, "--port") && !arg_value(&c, "--baud"));
    assert(!strcmp(arg_value(&c, "--console"), "usb"));
    st->usb = st->flash = st->pk = st->epaper = 0;
    tiku_field_start(&st->field[2], "BAD"); assert(!command_build(st, &st->jobs[0], &c));
    tiku_field_start(&st->field[2], "MEMORY_MODEL=large"); assert(!command_build(st, &st->jobs[0], &c));
    tiku_field_start(&st->field[2], "FOO=1 BAR=2"); assert(command_build(st, &st->jobs[0], &c));
    tiku_field_start(&st->field[2], "");
    puts("PASS: canonical eligibility, diagnostics, multi-suite selection and argv");
}

static void result_test(bench_t *st)
{
    clear_selection(st); st->suites[0].selected = 1; assert(queue_build(st)); plan_build(st);
    char green[] = "[TC:END] common pass=5 fail=0";
    consume_line(st, green);
    assert(st->results[0].done && st->results[0].pass == 5);
    char red[] = "[TC:END] common pass=5 fail=0 skip=0 harness_fail=1";
    consume_line(st, red);
    assert(st->nresult == 1 && st->results[0].harness);
    char zero[] = "[TC:END] common pass=0 fail=0";
    consume_line(st, zero); assert(st->results[0].skip);
    assert(st->nhistory == 2 && !strcmp(st->history[0].kind, "full"));
    assert(!strcmp(st->history[0].verdict, "fail") && !strcmp(st->history[1].verdict, "pass"));
    puts("PASS: authoritative correction, zero-test skip, full/selected history separation");
}

static void run_test(bench_t *st, const char *mode, int expected_bad)
{
    setenv("TIKU_BENCH_TEST_MODE", mode, 1);
    clear_selection(st); st->suites[0].selected = st->suites[1].selected = 1;
    assert(queue_build(st));
    int before = releases, returned = reclaims;
    run_confirmed(st);
    assert(st->waiting && !st->child && releases == before + 1);
    bench_board_said(st, TIKU_APP_BOARD_RELEASED, "another-board");
    assert(st->waiting && !st->child);
    bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    assert(st->child && !strcmp(st->port, "/dev/fixture-only"));
    pump(st);
    assert(st->batch_failed == expected_bad && st->job == 2);
    assert(reclaims == returned + 1 && st->reclaiming);
    assert(st->nresult >= 2 && st->results[0].done);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-results.png"));
    assert(unlink(st->log_path) == 0);
    bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    assert(!st->busy);
}

static void cancel_test(bench_t *st)
{
    clear_selection(st); st->suites[0].selected = 1; assert(queue_build(st));
    run_confirmed(st); stop_run(st);
    assert(st->waiting && st->cancelled && !st->child);
    bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    assert(st->reclaiming && !st->child);
    unlink(st->log_path);
    bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    puts("PASS: stop while releasing never starts a runner");
}

static void host_run_test(bench_t *st)
{
    clear_selection(st);
    setenv("TIKU_BENCH_TEST_MODE", "normal", 1);
    st->suites[3].selected = 1;
    assert(st->suites[3].host_only);
    st->target_name[0] = st->mcu[0] = st->port[0] = 0;
    profile_update(st);
    assert(!st->supported && suite_supported(st, &st->suites[3]));
    st->flash = st->large = st->usb = st->pk = st->epaper = 1;
    tiku_field_start(&st->field[1], "invalid");
    tiku_field_start(&st->field[2], "not device make variables");
    assert(queue_build(st) && !queue_needs_device(st));
    command_t c;
    assert(command_build(st, &st->jobs[0], &c));
    assert(!arg_value(&c, "--board") && !arg_value(&c, "--port") &&
           !arg_value(&c, "--baud") && !arg_value(&c, "--flash") &&
           !arg_value(&c, "--make-var") && !arg_value(&c, "--console"));
    {
        /* A device job behind a host one is checked before the batch
         * starts, while the board is still held. */
        command_t probe;
        int held = st->njob;
        assert(queue_first_invalid(st, &probe) < 0);
        st->jobs[st->njob].suite = 0;
        st->jobs[st->njob].only[0] = 0;
        snprintf(st->jobs[st->njob].key, sizeof st->jobs[0].key, "%s",
                 st->suites[0].key);
        st->njob++;
        assert(!st->suites[0].host_only);
        assert(queue_first_invalid(st, &probe) == 1);
        st->njob = held;
    }
    int before = releases, returned = reclaims;
    run_confirmed(st);
    assert(st->child && !st->waiting && releases == before);
    pump(st);
    assert(!st->busy && !st->batch_failed && reclaims == returned);
    assert(st->nresult == 1 && st->results[0].done && st->results[0].pass == 3);
    assert(!unlink(st->log_path));
    st->flash = st->large = st->usb = st->pk = st->epaper = 0;
    tiku_field_start(&st->field[1], "115200");
    tiku_field_start(&st->field[2], "");
    snprintf(st->target_name, sizeof st->target_name, "%s", device);
    target_capture(st);
    /* Host-only still must not release a board when one IS connected. */
    assert(queue_build(st)); run_confirmed(st); pump(st);
    assert(!st->busy && !st->batch_failed && releases == before && reclaims == returned);
    assert(!unlink(st->log_path));
    /* A mixed queue still hands the port over once for the device work. */
    st->suites[0].selected = 1;
    assert(queue_build(st) && queue_needs_device(st) && st->njob == 2);
    run_confirmed(st);
    assert(st->waiting && !st->child && releases == before + 1);
    bench_board_said(st, TIKU_APP_BOARD_RELEASED, device); pump(st);
    assert(!st->batch_failed && st->job == 2 && st->reclaiming);
    assert(releases == before + 1 && reclaims == returned + 1);
    assert(st->nresult == 2 && st->results[0].done && st->results[1].done);
    assert(!unlink(st->log_path));
    bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    assert(!st->busy);
    clear_selection(st);
    puts("PASS: host-only runs without device handoff; mixed queues release/reclaim once");
}

static void interactive_test(bench_t *st)
{
    setenv("TIKU_BENCH_TEST_MODE", "wait", 1);
    clear_selection(st); st->suites[0].selected = 1; assert(queue_build(st));
    run_confirmed(st); bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    tiku_field_start(&st->field[4], "continue"); action(st, SEND);
    assert(st->send_n && !*st->field[4].text);
    pump(st); assert(!st->batch_failed && st->reclaiming);
    unlink(st->log_path);
    bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    setenv("TIKU_BENCH_TEST_MODE", "hang", 1);
    clear_selection(st); st->suites[0].selected = 1; assert(queue_build(st));
    run_confirmed(st); bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    int ready = 0;
    for (int i = 0; i < 3000 && !ready; i++) {
        bench_tick(st, st->now + 10000); usleep(1000);
        for (int j = 0; j < st->nlog; j++) ready |= !strcmp(st->logs[j].text, "READY");
    }
    assert(ready); int before = reclaims;
    stop_run(st); assert(st->busy && reclaims == before);
    pump(st); assert(st->cancelled && st->reclaiming && reclaims == before + 1);
    unlink(st->log_path);
    bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    puts("PASS: interactive stdin and SIGINT-to-SIGKILL cancellation before reclaim");
}

static void real_catalogue_test(void)
{
    char *dir = realpath("TikuBench", NULL);
    if (!dir) dir = realpath("../TikuBench", NULL);
    if (!dir) {
        /* TikuBench is a repository of its own beside this one; a clone
         * without it still builds and tests, minus this one reading. */
        puts("SKIP: TikuBench is not beside this repository");
        return;
    }
    setenv("TIKU_BENCH", dir, 1); free(dir);
    void *state = NULL; assert(!bench_start(&state, &svc));
    /* Minutes, not the fixtures' seconds: the reader imports every
     * suite module in the checkout to list it. */
    bench_t *st = state; pump_for(st, 120);
    if (!st->metadata_ok) fprintf(stderr, "%s\n", st->note);
    assert(st->metadata_ok && st->nsuite > 80 && st->nleaf > 400);
    int recovery = 0;
    for (int i = 0; i < st->nsuite; i++) if (!strcmp(st->suites[i].key, "kernel")) {
        st->suites[i].expanded = 1; st->suites[i].selected = 1;
    } else if (!strcmp(st->suites[i].key, "vfs-recovery-host")) {
        recovery++;
        assert(st->suites[i].host_only && !st->suites[i].large && st->suites[i].count == 7);
    }
    assert(recovery == 1);
    entries_build(st); paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-catalogue.png"));
    tiku_font_set_size(16); paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-catalogue-large.png"));
    tiku_font_set_size(12);
    bench_stop(st);
    puts("PASS: full native TikuBench registry import (read-only, no hardware)");
}

static void ui_test(bench_t *st)
{
    tiku_tabs_select(&st->tabs, TAB_SUITES); paint(st);
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = 200, .y = 145};
    bench_event(st, st->id, &ev);
    ev.type = TIKU_EVENT_KEY_DOWN; ev.key = TIKU_KEY_RETURN;
    bench_event(st, st->id, &ev);
    assert(st->suites[0].expanded);
    ev.key = ' '; bench_event(st, st->id, &ev);
    assert(st->suites[0].selected);
    paint(st); assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-suites.png"));
    action(st, RUN); assert(st->confirm.open && !st->busy);
    ev.key = TIKU_KEY_RETURN; bench_event(st, st->id, &ev);
    assert(!st->confirm.open && !st->busy); /* Default is Cancel. */
    tiku_tabs_select(&st->tabs, TAB_OPTIONS); paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-options.png"));
    puts("PASS: list keyboard selection, native controls and safe confirmation");
}

int main(void)
{
    char *dir = realpath("applications/TikuTracker/tests/fixtures/bench", NULL);
    if (!dir) dir = realpath("TikuTracker/tests/fixtures/bench", NULL);
    assert(dir); setenv("TIKU_BENCH", dir, 1); free(dir);
    bench_t *st = start();
    model_test(st); result_test(st);
    assert(st->nresult > 0);
    result_t saved = st->results[0];
    strcpy(st->results[0].id, "<sensor&>");
    int before_release = releases;
    report_open(st);
    assert(strstr(report_body, "&lt;sensor&amp;&gt;") &&
        !strstr(report_body, "<sensor&>") && releases == before_release);
    st->results[0] = saved;
    run_test(st, "normal", 0); run_test(st, "nonzero", 1); run_test(st, "empty", 1);
    cancel_test(st); interactive_test(st); host_run_test(st); ui_test(st);
    bench_stop(st);
    assert(windows == 1 && frames > 0);
    real_catalogue_test();
    puts("PASS: queued subprocesses, final partial line, exit codes and release/reclaim");
    return 0;
}
