/* Desktop Bench, isolated metadata/runner fixtures, no device or display. */
#include <assert.h>
#include "../../apps/bench/tiku_bench_private.h"

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
        tiku_bench_tick(st, st->now + 10000);
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
    assert(!tiku_bench_start(&state, &svc));
    bench_t *st = state;
    assert(st->loading);
    pump(st);
    assert(st->metadata_ok && st->nsuite == 4 && st->nleaf == 5);
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
    assert(tiku_bench_csv_has("any", "nrf54l15") && !tiku_bench_csv_has("nrf54l15", "nrf54l"));
    st->suites[0].selected = st->suites[1].selected = 1;
    assert(tiku_bench_queue_build(st) && st->njob == 2 && !*st->jobs[0].only);
    tiku_bench_plan_build(st);
    assert(st->nresult == 2); /* Explicit diagnostics never join a full sweep. */
    tiku_bench_clear_selection(st); st->leaves[1].selected = 1;
    assert(tiku_bench_queue_build(st) && !strcmp(st->jobs[0].only, "reset"));
    snprintf(st->mcu, sizeof st->mcu, "rp2350"); st->usb = 1; tiku_bench_profile_update(st);
    assert(!tiku_bench_leaf_supported(st, &st->leaves[1], 0));
    char reason[256];
    assert(strstr(tiku_bench_leaf_reason(st, &st->leaves[1], reason, sizeof reason), "cannot survive"));
    assert(!tiku_bench_queue_build(st));
    snprintf(st->mcu, sizeof st->mcu, "msp430fr5994"); tiku_bench_profile_update(st);
    assert(!tiku_bench_suite_supported(st, &st->suites[2]));
    st->large = 1;
    assert(tiku_bench_suite_supported(st, &st->suites[2]));
    tiku_bench_target_capture(st); tiku_bench_clear_selection(st); st->suites[0].selected = 1;
    assert(tiku_bench_queue_build(st));
    command_t c;
    assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    assert(arg_value(&c, "--skip-build") && !arg_value(&c, "--flash"));
    assert(!strcmp(arg_value(&c, "--port"), "/dev/fixture-only"));
    st->usb = st->flash = st->pk = st->epaper = 1;
    assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    /* The ePaper presets land in the one extra-cflags argument, ahead of
     * the free-text flags: a panel by its value, a pattern by its index. */
    st->epanel.selected = 1; st->epattern.selected = 2;
    tiku_field_start(&st->field[3], "-DX=1");
    assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    assert(!strcmp(arg_value(&c, "--extra-cflags"),
                   "-DEPAPER_KIT_PANEL=2 -DEPAPER_KIT_PATTERN=2 -DX=1"));
    st->epaper = 0; assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    assert(!strcmp(arg_value(&c, "--extra-cflags"), "-DX=1"));
    tiku_field_start(&st->field[3], ""); st->epaper = 1;
    st->epanel.selected = 0; st->epattern.selected = 0;   /* as they start */
    assert(!arg_value(&c, "--port") && !arg_value(&c, "--baud"));
    assert(!strcmp(arg_value(&c, "--console"), "usb"));
    st->usb = st->flash = st->pk = st->epaper = 0;
    tiku_field_start(&st->field[2], "BAD"); assert(!tiku_bench_command_build(st, &st->jobs[0], &c));
    tiku_field_start(&st->field[2], "MEMORY_MODEL=large"); assert(!tiku_bench_command_build(st, &st->jobs[0], &c));
    tiku_field_start(&st->field[2], "FOO=1 BAR=2"); assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    tiku_field_start(&st->field[2], "");
    puts("PASS: canonical eligibility, diagnostics, multi-suite selection and argv");
}

static void result_test(bench_t *st)
{
    tiku_bench_clear_selection(st); st->suites[0].selected = 1; assert(tiku_bench_queue_build(st)); tiku_bench_plan_build(st);
    char green[] = "[TC:END] common pass=5 fail=0";
    tiku_bench_consume_line(st, green);
    assert(st->results[0].done && st->results[0].pass == 5);
    char red[] = "[TC:END] common pass=5 fail=0 skip=0 harness_fail=1";
    tiku_bench_consume_line(st, red);
    assert(st->nresult == 1 && st->results[0].harness);
    char zero[] = "[TC:END] common pass=0 fail=0";
    tiku_bench_consume_line(st, zero); assert(st->results[0].skip);
    assert(st->nhistory == 2 && !strcmp(st->history[0].kind, "full"));
    assert(!strcmp(st->history[0].verdict, "fail") && !strcmp(st->history[1].verdict, "pass"));
    /* The verdict card follows the full record for the profile: one
     * older than a day is stale, one within it is fresh, none is unproven. */
    {
        char v[80], perf[64];
        const char *profile = st->profile;
        (void)tiku_bench_history_verdict(st, v, sizeof v, perf, sizeof perf);
        int has_full = 0; long age = 0;
        for (int i = 0; i < st->nhistory; i++)
            if (!strcmp(st->history[i].kind, "full") &&
                !strcmp(st->history[i].suite, "kernel") &&
                !strcmp(st->history[i].profile, profile)) {
                has_full = 1; age = st->history[i].age;
            }
        assert(!has_full ? !strncmp(v, "UNPROVEN", 8) :
               age > 86400 ? !strncmp(v, "STALE", 5) : !strncmp(v, "Fresh", 5));
        assert(!strncmp(perf, "performance: ", 13));
    }
    puts("PASS: authoritative correction, zero-test skip, full/selected history separation");
}

static void run_test(bench_t *st, const char *mode, int expected_bad)
{
    setenv("TIKU_BENCH_TEST_MODE", mode, 1);
    tiku_bench_clear_selection(st); st->suites[0].selected = st->suites[1].selected = 1;
    assert(tiku_bench_queue_build(st));
    int before = releases, returned = reclaims;
    tiku_bench_run_confirmed(st);
    assert(st->waiting && !st->child && releases == before + 1);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RELEASED, "another-board");
    assert(st->waiting && !st->child);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    assert(st->child && !strcmp(st->port, "/dev/fixture-only"));
    pump(st);
    assert(st->batch_failed == expected_bad && st->job == 2);
    assert(reclaims == returned + 1 && st->reclaiming);
    assert(st->nresult >= 2 && st->results[0].done);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-results.png"));
    assert(unlink(st->log_path) == 0);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    assert(!st->busy);
}

static void cancel_test(bench_t *st)
{
    tiku_bench_clear_selection(st); st->suites[0].selected = 1; assert(tiku_bench_queue_build(st));
    tiku_bench_run_confirmed(st); tiku_bench_stop_run(st);
    assert(st->waiting && st->cancelled && !st->child);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    assert(st->reclaiming && !st->child);
    unlink(st->log_path);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    puts("PASS: stop while releasing never starts a runner");
}

static void host_run_test(bench_t *st)
{
    tiku_bench_clear_selection(st);
    setenv("TIKU_BENCH_TEST_MODE", "normal", 1);
    st->suites[3].selected = 1;
    assert(st->suites[3].host_only);
    st->target_name[0] = st->mcu[0] = st->port[0] = 0;
    tiku_bench_profile_update(st);
    assert(!st->supported && tiku_bench_suite_supported(st, &st->suites[3]));
    st->flash = st->large = st->usb = st->pk = st->epaper = 1;
    tiku_field_start(&st->field[1], "invalid");
    tiku_field_start(&st->field[2], "not device make variables");
    assert(tiku_bench_queue_build(st) && !tiku_bench_queue_needs_device(st));
    command_t c;
    assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    assert(!arg_value(&c, "--board") && !arg_value(&c, "--port") &&
           !arg_value(&c, "--baud") && !arg_value(&c, "--flash") &&
           !arg_value(&c, "--make-var") && !arg_value(&c, "--console"));
    {
        /* A device job behind a host one is checked before the batch
         * starts, while the board is still held. */
        command_t probe;
        int held = st->njob;
        assert(tiku_bench_queue_first_invalid(st, &probe) < 0);
        st->jobs[st->njob].suite = 0;
        st->jobs[st->njob].only[0] = 0;
        snprintf(st->jobs[st->njob].key, sizeof st->jobs[0].key, "%s",
                 st->suites[0].key);
        st->njob++;
        assert(!st->suites[0].host_only);
        assert(tiku_bench_queue_first_invalid(st, &probe) == 1);
        st->njob = held;
    }
    int before = releases, returned = reclaims;
    tiku_bench_run_confirmed(st);
    assert(st->child && !st->waiting && releases == before);
    pump(st);
    assert(!st->busy && !st->batch_failed && reclaims == returned);
    assert(st->nresult == 1 && st->results[0].done && st->results[0].pass == 3);
    assert(!unlink(st->log_path));
    st->flash = st->large = st->usb = st->pk = st->epaper = 0;
    tiku_field_start(&st->field[1], "115200");
    tiku_field_start(&st->field[2], "");
    snprintf(st->target_name, sizeof st->target_name, "%s", device);
    tiku_bench_target_capture(st);
    /* Host-only still must not release a board when one IS connected. */
    assert(tiku_bench_queue_build(st)); tiku_bench_run_confirmed(st); pump(st);
    assert(!st->busy && !st->batch_failed && releases == before && reclaims == returned);
    assert(!unlink(st->log_path));
    /* A mixed queue still hands the port over once for the device work. */
    st->suites[0].selected = 1;
    assert(tiku_bench_queue_build(st) && tiku_bench_queue_needs_device(st) && st->njob == 2);
    tiku_bench_run_confirmed(st);
    assert(st->waiting && !st->child && releases == before + 1);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RELEASED, device); pump(st);
    assert(!st->batch_failed && st->job == 2 && st->reclaiming);
    assert(releases == before + 1 && reclaims == returned + 1);
    assert(st->nresult == 2 && st->results[0].done && st->results[1].done);
    assert(!unlink(st->log_path));
    tiku_bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    assert(!st->busy);
    tiku_bench_clear_selection(st);
    puts("PASS: host-only runs without device handoff; mixed queues release/reclaim once");
}

static void interactive_test(bench_t *st)
{
    setenv("TIKU_BENCH_TEST_MODE", "wait", 1);
    tiku_bench_clear_selection(st); st->suites[0].selected = 1; assert(tiku_bench_queue_build(st));
    tiku_bench_run_confirmed(st); tiku_bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    tiku_field_start(&st->field[4], "continue"); tiku_bench_action(st, SEND);
    assert(st->send_n && !*st->field[4].text);
    pump(st); assert(!st->batch_failed && st->reclaiming);
    unlink(st->log_path);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
    setenv("TIKU_BENCH_TEST_MODE", "hang", 1);
    tiku_bench_clear_selection(st); st->suites[0].selected = 1; assert(tiku_bench_queue_build(st));
    tiku_bench_run_confirmed(st); tiku_bench_board_said(st, TIKU_APP_BOARD_RELEASED, device);
    int ready = 0;
    for (int i = 0; i < 3000 && !ready; i++) {
        tiku_bench_tick(st, st->now + 10000); usleep(1000);
        for (int j = 0; j < st->nlog; j++) ready |= !strcmp(st->logs[j].text, "READY");
    }
    assert(ready); int before = reclaims;
    tiku_bench_stop_run(st); assert(st->busy && reclaims == before);
    pump(st); assert(st->cancelled && st->reclaiming && reclaims == before + 1);
    unlink(st->log_path);
    tiku_bench_board_said(st, TIKU_APP_BOARD_RECLAIMED, device); pump(st);
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
    void *state = NULL; assert(!tiku_bench_start(&state, &svc));
    /* Minutes, not the fixtures' seconds: the reader imports every
     * suite module in the checkout to list it. */
    bench_t *st = state; pump_for(st, 120);
    if (!st->metadata_ok) fprintf(stderr, "%s\n", st->note);
    assert(st->metadata_ok && st->nsuite > 80 && st->nleaf > 400);
    int recovery = 0, memory = 0;
    for (int i = 0; i < st->nsuite; i++) if (!strcmp(st->suites[i].key, "kernel")) {
        st->suites[i].expanded = 1; st->suites[i].selected = 1;
    } else if (!strcmp(st->suites[i].key, "vfs-recovery-host")) {
        recovery++;
        assert(st->suites[i].host_only && !st->suites[i].large && st->suites[i].count == 7);
    } else if (!strcmp(st->suites[i].key, "memory-host")) {
        memory++;
        assert(st->suites[i].host_only && !st->suites[i].large);
        int layout = 0, lending = 0, spans = 0, requests = 0;
        for (int j = st->suites[i].first;
             j < st->suites[i].first + st->suites[i].count; j++) {
            layout += !strcmp(st->leaves[j].id, "layout");
            lending += !strcmp(st->leaves[j].id, "lending");
            spans += !strcmp(st->leaves[j].id, "spans");
            requests += !strcmp(st->leaves[j].id, "requests");
        }
        assert(layout == 1 && lending == 1 && spans == 1 && requests == 1);
    }
    assert(recovery == 1 && memory == 1);
    tiku_bench_entries_build(st);
    /* The list is category cards: a heading over each group, in the
     * catalogue's order, counting its suites; headings are not pickable;
     * folding one takes its suites out and leaves the heading. */
    {
        int headings = 0, first_heading = -1, kernel_group = -1;
        for (int i = 0; i < st->nentry; i++) {
            if (st->entries[i].suite >= 0) continue;
            headings++;
            if (first_heading < 0) first_heading = i;
            assert(tiku_list_is_heading(&st->list[TAB_SUITES], i));
            assert(st->groups[st->entries[i].group].count > 0);
            if (!strcmp(st->groups[st->entries[i].group].name, "Kernel"))
                kernel_group = st->entries[i].group;
        }
        assert(headings == st->ngroup && headings >= 8);
        assert(first_heading == 0);
        assert(kernel_group >= 0);
        {
            int before = st->nentry, row = -1;
            for (int i = 0; i < st->nentry; i++)
                if (st->entries[i].suite < 0 &&
                    st->entries[i].group == kernel_group) row = i;
            tiku_bench_toggle_entry(st, row, 1);
            assert(st->groups[kernel_group].folded && st->nentry < before);
            for (int i = 0; i < st->nentry; i++)
                assert(st->entries[i].suite < 0 ||
                       st->entries[i].group != kernel_group);
            tiku_bench_toggle_entry(st, row, 1);
            assert(!st->groups[kernel_group].folded && st->nentry == before);
        }
    }
    /* Did not run: every leaf the profile excludes, none it runs. */
    tiku_bench_notrun_build(st);
    {
        int excluded = 0;
        for (int i = 0; i < st->nleaf; i++)
            excluded += !tiku_bench_leaf_supported(st, &st->leaves[i], 0);
        assert(st->list[TAB_NOTRUN].count == excluded);
        st->tabs.current = TAB_NOTRUN;
        if (excluded) assert(tiku_bench_details_row(st, 0) != NULL &&
                             tiku_bench_details_row(st, excluded) == NULL);
        st->tabs.current = TAB_SUITES;
    }
    tiku_bench_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-catalogue.png"));
    tiku_tabs_select(&st->tabs, TAB_HISTORY); tiku_bench_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-history.png"));
    tiku_tabs_select(&st->tabs, TAB_SUITES);
    tiku_font_set_size(16); tiku_bench_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-catalogue-large.png"));
    tiku_font_set_size(12);
    /* The actual shared catalogue, not a second desktop list: select and run
     * just the new host group without releasing even a connected device. */
    tiku_bench_clear_selection(st);
    for (int i = 0; i < st->nsuite; i++)
        if (!strcmp(st->suites[i].key, "memory-host"))
            for (int j = st->suites[i].first;
                 j < st->suites[i].first + st->suites[i].count; j++)
                if (!strcmp(st->leaves[j].id, "requests")) st->leaves[j].selected = 1;
    assert(tiku_bench_queue_build(st) && st->njob == 1);
    assert(!strcmp(st->jobs[0].only, "requests") && !tiku_bench_queue_needs_device(st));
    {
        command_t c;
        int before = releases, returned = reclaims;
        assert(tiku_bench_command_build(st, &st->jobs[0], &c));
        assert(!strcmp(arg_value(&c, "--only"), "requests"));
        assert(!arg_value(&c, "--port") && !arg_value(&c, "--board") &&
               !arg_value(&c, "--flash"));
        tiku_bench_run_confirmed(st); pump_for(st, 120);
        assert(!st->busy && !st->batch_failed && releases == before && reclaims == returned);
        assert(st->nresult == 1 && st->results[0].done && st->results[0].pass > 0);
        assert(!unlink(st->log_path));
        /* A child failure marker must turn this same selected leaf red. */
        tiku_bench_plan_build(st);
        st->job = 0;
        char failed[] = "[TC:END] requests pass=0 fail=1";
        tiku_bench_consume_line(st, failed);
        assert(st->nresult == 1 && st->results[0].done && st->results[0].fail == 1);
    }
    tiku_bench_stop(st);
    puts("PASS: native registry import and selected memory requests run (no hardware)");
}

/** A destructive leaf: not in a full sweep, asked about on its own before
 *  the run's question, and the flag given only for that run. */
static void destructive_test(bench_t *st)
{
    command_t c;
    int wipe = -1;
    for (int i = 0; i < st->nleaf; i++)
        if (!strcmp(st->leaves[i].id, "wipe")) wipe = i;
    assert(wipe >= 0 && !strcmp(st->leaves[wipe].kind, "destructive"));
    assert(tiku_bench_leaf_supported(st, &st->leaves[wipe], 0));
    assert(!tiku_bench_leaf_supported(st, &st->leaves[wipe], 1));
    tiku_bench_clear_selection(st); st->suites[1].selected = 1;
    assert(tiku_bench_queue_build(st) && !tiku_bench_queue_destructive(st));
    tiku_bench_clear_selection(st); st->leaves[wipe].selected = 1;
    assert(tiku_bench_queue_build(st) && tiku_bench_queue_destructive(st) == 1);
    assert(!strcmp(st->jobs[0].only, "wipe") && st->jobs[0].destructive);
    assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    assert(!strcmp(arg_value(&c, "--only"), "wipe"));
    for (int i = 0; i < c.argc; i++) assert(strcmp(c.argv[i], "--allow-destructive"));
    /* Run: the warning first, Cancel the default; Return alone cancels. */
    tiku_bench_action(st, RUN);
    assert(st->confirm.open && st->confirm.tag == RUN_DESTRUCTIVE && !st->confirm.dflt);
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    tiku_bench_event(st, st->id, &ev);
    assert(!st->confirm.open && !st->busy && !st->allow_destructive);
    /* Allowed: the run's own question follows, and the flag is in argv. */
    tiku_bench_action(st, RUN);
    ev.key = TIKU_KEY_TAB; tiku_bench_event(st, st->id, &ev);
    ev.key = TIKU_KEY_RETURN; tiku_bench_event(st, st->id, &ev);
    assert(st->confirm.open && st->confirm.tag == RUN && st->allow_destructive);
    assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    {
        int found = 0;
        for (int i = 0; i < c.argc; i++) found |= !strcmp(c.argv[i], "--allow-destructive");
        assert(found);
    }
    ev.key = TIKU_KEY_RETURN; tiku_bench_event(st, st->id, &ev);  /* Cancel */
    assert(!st->confirm.open && !st->busy);
    /* The next run asks again: consent is never remembered. */
    tiku_bench_action(st, RUN);
    assert(st->confirm.open && st->confirm.tag == RUN_DESTRUCTIVE && !st->allow_destructive);
    ev.key = TIKU_KEY_RETURN; tiku_bench_event(st, st->id, &ev);
    tiku_bench_clear_selection(st);
    puts("PASS: a destructive leaf is asked about on its own, for one run");
}

static void ui_test(bench_t *st)
{
    tiku_tabs_select(&st->tabs, TAB_SUITES); tiku_bench_paint(st);
    /* Row 0 is the first group's heading, not pickable; the first suite
     * is the row under it. */
    assert(st->nentry > 1 && st->entries[0].suite < 0 &&
           st->entries[1].suite == 0);
    int row_h = tiku_list_row_h(&st->list[TAB_SUITES]);
    tiku_rect_t body = tiku_bench_list_rect(st, TAB_SUITES);
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = body.x + 100, .y = body.y + row_h + 13};
    tiku_bench_event(st, st->id, &ev);
    ev.type = TIKU_EVENT_KEY_DOWN; ev.key = TIKU_KEY_RETURN;
    tiku_bench_event(st, st->id, &ev);
    assert(st->suites[0].expanded);
    ev.key = ' '; tiku_bench_event(st, st->id, &ev);
    assert(st->suites[0].selected);
    tiku_bench_paint(st); assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-suites.png"));
    tiku_bench_action(st, RUN); assert(st->confirm.open && !st->busy);
    ev.key = TIKU_KEY_RETURN; tiku_bench_event(st, st->id, &ev);
    assert(!st->confirm.open && !st->busy); /* Default is Cancel. */
    /* The cards are always in view: the ePaper presets wake with the kit. */
    st->epaper = 1; tiku_bench_paint(st);
    assert(st->epanel.count == 2 && st->epattern.count == 4);
    assert(st->epanel.selected < st->epanel.count &&
           st->epattern.selected < st->epattern.count);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-options.png"));
    st->epaper = 0;
    tiku_tabs_select(&st->tabs, TAB_OUTPUT); tiku_bench_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-output.png"));
    tiku_tabs_select(&st->tabs, TAB_SUITES);
    /* The corner: the views take the room, the cards keep their column,
     * and the window never shrinks under what it opened with. */
    ev = (tiku_event_t){.type = TIKU_EVENT_RESIZE, .width = 1200, .height = 760};
    tiku_bench_event(st, st->id, &ev); tiku_bench_paint(st);
    assert(st->w == 1200 && st->h == 760 && st->surface->w == 1200);
    assert(tiku_bench_list_rect(st, TAB_SUITES).w == body.w + (1200 - BW));
    assert(tiku_bench_list_rect(st, TAB_SUITES).h == body.h + (760 - BH));
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-bench-wide.png"));
    /* Every view ends above the detail line, the footer and the note,
     * at this size and at the opening one: a list that ran past them
     * once covered the note with its rows. */
    for (int pass = 0; pass < 2; pass++) {
        for (int t = 0; t < TAB_COUNT; t++) {
            tiku_rect_t r = tiku_bench_list_rect(st, t);
            int foot = t == TAB_SUITES ? st->h - 92 :
                       t == TAB_OUTPUT || t == TAB_CONSOLE ? st->h - 84 :
                       st->h - 54;
            assert(r.y + r.h <= foot && r.y + r.h <= st->h - 28);
        }
        ev = (tiku_event_t){.type = TIKU_EVENT_RESIZE, .width = 500, .height = 300};
        tiku_bench_event(st, st->id, &ev); tiku_bench_paint(st);
    }
    assert(st->w == BW && st->h == BH);
    puts("PASS: list keyboard selection, native controls and safe confirmation");
}

/** One file under a fresh directory: the options round-trip through it. */
static void settings_test(char *sandbox)
{
    char path[512], text[1024] = "";
    snprintf(path, sizeof path, "%s/config/tikubench-desktop/settings",
             sandbox);
    setenv("TIKU_BENCH_SETTINGS", path, 1);
    bench_t *st = start();
    /* No file: the defaults, and nothing written until an option moves. */
    assert(!st->flash && !st->pk && !st->epaper && !*st->wanted);
    tiku_bench_tick(st, st->now + 10000);
    assert(access(path, F_OK) != 0);
    tiku_bench_action(st, FLASH); tiku_bench_action(st, PK); tiku_bench_action(st, EPAPER);
    st->epanel.selected = 1; st->epattern.selected = 3;
    tiku_field_start(&st->field[1], "921600");
    tiku_field_start(&st->field[2], "BOARD=x");
    tiku_field_start(&st->field[3], "-DY=2");
    tiku_bench_tick(st, st->now + 10000);
    FILE *f = fopen(path, "r"); assert(f);
    size_t n = fread(text, 1, sizeof text - 1, f); text[n] = '\0'; fclose(f);
    assert(strstr(text, "board=laboratory\n") && strstr(text, "baud=921600\n"));
    assert(strstr(text, "flash=true\n") && strstr(text, "pk_hw=true\n"));
    assert(strstr(text, "make_vars=BOARD=x\n"));
    assert(strstr(text, "extra_cflags=-DY=2\n"));
    assert(strstr(text, "mem_model=small\n") && strstr(text, "console=uart\n"));
    assert(strstr(text,
                  "epaper_enable=true\nepaper_panel=1\nepaper_pattern=3\n"));
    tiku_bench_stop(st);
    /* A second session starts where the first left off, down to argv. */
    st = start();
    assert(st->flash && st->pk && st->epaper);
    assert(st->epanel.selected == 1 && st->epattern.selected == 3);
    assert(!strcmp(st->field[1].text, "921600"));
    assert(!strcmp(st->field[2].text, "BOARD=x"));
    assert(!strcmp(st->field[3].text, "-DY=2") && !*st->wanted);
    st->suites[0].selected = 1; assert(tiku_bench_queue_build(st));
    command_t c; assert(tiku_bench_command_build(st, &st->jobs[0], &c));
    assert(!strcmp(arg_value(&c, "--baud"), "921600"));
    assert(arg_value(&c, "--flash"));
    assert(!strcmp(arg_value(&c, "--extra-cflags"),
                   "-DEPAPER_KIT_PANEL=2 -DEPAPER_KIT_PATTERN=3 -DY=2"));
    tiku_bench_stop(st);
    /* A remembered board that is not mounted: the shell's device runs,
     * the memory waits; a preset out of range and a stray line are
     * skipped. */
    f = fopen(path, "w"); assert(f);
    fputs("board=elsewhere\nepaper_panel=9\nnonsense\nflash=1\n", f); fclose(f);
    st = start();
    assert(!strcmp(st->wanted, "elsewhere") && st->flash);
    assert(st->epanel.selected == 0);
    tiku_bench_stop(st);
    unlink(path); *strrchr(path, '/') = '\0'; rmdir(path);
    *strrchr(path, '/') = '\0'; rmdir(path);
    puts("PASS: run options remembered between sessions");
}

int main(void)
{
    char *dir = realpath("applications/tests/fixtures/bench", NULL);
    if (!dir) dir = realpath("tests/fixtures/bench", NULL);
    assert(dir); setenv("TIKU_BENCH", dir, 1); free(dir);
    /* The user's own settings stay out of it. */
    char sandbox[] = "/tmp/tiku-bench-settings-XXXXXX";
    assert(mkdtemp(sandbox));
    char none[512]; snprintf(none, sizeof none, "%s/none", sandbox);
    setenv("TIKU_BENCH_SETTINGS", none, 1);
    bench_t *st = start();
    model_test(st); result_test(st);
    assert(st->nresult > 0);
    result_t saved = st->results[0];
    strcpy(st->results[0].id, "<sensor&>");
    int before_release = releases;
    tiku_bench_report_open(st);
    assert(strstr(report_body, "&lt;sensor&amp;&gt;") &&
        !strstr(report_body, "<sensor&>") && releases == before_release);
    st->results[0] = saved;
    run_test(st, "normal", 0); run_test(st, "nonzero", 1); run_test(st, "empty", 1);
    cancel_test(st); interactive_test(st); host_run_test(st); destructive_test(st);
    ui_test(st);
    tiku_bench_stop(st);
    assert(windows == 1 && frames > 0);
    settings_test(sandbox);
    setenv("TIKU_BENCH_SETTINGS", none, 1); unlink(none); rmdir(sandbox);
    real_catalogue_test();
    puts("PASS: queued subprocesses, final partial line, exit codes and release/reclaim");
    return 0;
}
