/* TikuBench in the desktop kit. The registry and runner remain TikuBench's.
 * SPDX-License-Identifier: Apache-2.0 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tiku_sys.h"
#include "tiku_app.h"
#include "tiku_alert.h"
#include "tiku_choice_paint.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_font.h"
#include "tiku_list_paint.h"
#include "tiku_tabs_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

#define BW 960
#define BH 640
#define NS 192
#define NL 1024
#define NR 1024
#define NH 256
#define ONLY_MAX 8192
#define LINE_MAXIMUM 8192
enum { TAB_SUITES, TAB_OPTIONS, TAB_RESULTS, TAB_OUTPUT, TAB_CONSOLE,
       TAB_HISTORY, TAB_METRICS };
enum { RUN = 1, STOP, REFRESH, SELECT_ALL, CLEAR, OPEN, TOGGLE, COPY_LOG,
       CLEAR_LOG, SEND, FLASH, LARGE, USB, PK, EPAPER, FOLLOW, OPEN_LOG,
       FIND_SUITES, FOCUS_LIST, REPORT = 20, TARGET = 30,
       LIST = 31, F_FILTER = 40, F_BAUD, F_MAKE, F_FLAGS, F_INPUT };
typedef struct {
    char key[48], group[64], name[128], boards[512], description[768];
    int large, host_only, first, count, selected, expanded;
} suite_t;
typedef struct {
    char id[96], label[160], profiles[512], group[64], kind[64];
    char reasons[2048], full[512];
    int selected;
} leaf_t;
typedef struct { char key[64]; int baud, large, usb; } board_t;
typedef struct { int suite, leaf; } entry_t;
typedef struct {
    char id[160], text[256];
    int job, pass, fail, harness, done, skip;
} result_t;
typedef struct {
    char board[64], suite[48], kind[32], verdict[24], profile[64];
    long age;
} past_t;
typedef struct { char field[8][128]; } metric_t;
typedef struct { int suite; char key[48], only[ONLY_MAX]; } job_t;
typedef struct { char text[LINE_MAXIMUM]; int output; } logline_t;
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface; uint32_t id;
    tiku_tabs_t tabs; tiku_dialog_t dialog; tiku_choice_t target;
    tiku_field_t field[5]; tiku_field_input_t input[5];
    tiku_list_t list[7]; tiku_alert_t confirm;
    suite_t suites[NS]; int nsuite;
    leaf_t leaves[NL]; int nleaf;
    board_t boards[32]; int nboard;
    entry_t entries[NS + NL]; int nentry;
    past_t history[NH]; int nhistory;
    metric_t metrics[NH]; int nmetric;
    result_t results[NR]; int nresult;
    logline_t logs[256]; int nlog, log_head;
    job_t jobs[NS]; int njob, job;
    char target_name[64], mcu[64], port[256], profile[64];
    char mounted[1024], directory[512], note[512], log_path[512];
    int refreshed;              /* the board list has been read once */
    int flash, large, usb, pk, epaper, supported, dirty;
    int busy, waiting, released, reclaiming, cancelled, closing;
    int loading, metadata_ok, metadata_bad, in_build, batch_failed, no_tests;
    int child_fd, input_fd, eof, reaped, exit_status;
    pid_t child;
    int64_t now, deadline, stop_at, board_poll;
    char partial[LINE_MAXIMUM]; size_t partial_n; int overflow;
    char sendbuf[TIKU_FIELD_MAX + 2]; size_t send_n, send_at;
    char active[160], prompt[256]; int follow, confirm_button, pressed_row, pressed_expand;
    FILE *log;
} bench_t;
static void paint(bench_t *st);
static void publish(bench_t *st);
static void entries_build(bench_t *st);
static void metadata_start(bench_t *st);
static void run_next(bench_t *st);
static void release_finish(bench_t *st);
static void stop_run(bench_t *st);
static int action(bench_t *st, int command);
#include "tiku_bench_model.inl"
#include "tiku_bench_process.inl"
#include "tiku_bench_report.inl"
#include "tiku_bench_ui.inl"

/** Pump bounded process output without blocking the desktop. */
static void bench_tick(void *state, int64_t now_us)
{
    bench_t *st = state;
    st->now = now_us;
    process_poll(st);
    if (st->stop_at && st->now >= st->stop_at && st->child > 0) {
        (void)kill(-st->child, SIGKILL); st->stop_at = 0;
    }
    if (st->waiting && st->now >= st->deadline) {
        st->cancelled = 1; st->waiting = 0; st->released = 1;
        release_finish(st);
        snprintf(st->note, sizeof st->note, "Release timed out; reconnecting the device");
        st->dirty = 1;
    }
    if (st->reclaiming && st->now >= st->deadline) {
        st->reclaiming = st->released = st->busy = 0;
        snprintf(st->note, sizeof st->note,
                 "Device has not returned. Reconnect it in Devices.");
        st->dirty = 1;
    }
    if (!st->busy && !st->loading && !st->confirm.open && now_us >= st->board_poll) {
        targets_refresh(st); st->board_poll = now_us + 1000000;
    }
    if (st->dirty) { paint(st); publish(st); st->dirty = 0; }
    if (st->closing && !st->busy && !st->child && !st->loading)
        st->services->close(st->services->ctx, st->id);
}

/** Never launch until the shell has acknowledged exclusive port ownership. */
static void bench_board_said(void *state, int what, const char *text)
{
    bench_t *st = state;
    if ((what == TIKU_APP_BOARD_RELEASED || what == TIKU_APP_BOARD_RECLAIMED) &&
        (!text || strcmp(text, st->target_name))) return;
    if (what == TIKU_APP_BOARD_RELEASED && st->waiting) {
        st->waiting = 0; st->released = 1;
        if (st->cancelled) release_finish(st); else run_next(st);
    } else if (what == TIKU_APP_BOARD_RECLAIMED && st->reclaiming) {
        st->reclaiming = st->released = st->busy = 0;
        metadata_start(st);
    } else if (what == TIKU_APP_BOARD_FAILED && (st->waiting || st->reclaiming)) {
        st->waiting = st->reclaiming = st->released = st->busy = 0;
        snprintf(st->note, sizeof st->note,
                 "Device handoff failed; check Devices before retrying.");
    }
    st->dirty = 1;
}
static int bench_pick(void *state, uint32_t window, int command)
{ (void)window; return action(state, command); }

static int bench_start(void **state, const tiku_app_services_t *services)
{
    bench_t *st = calloc(1, sizeof *st);
    if (!st) return -1;
    st->services = services; st->child_fd = st->input_fd = -1; st->follow = 1; st->pressed_row = -1;
    st->surface = tiku_surface_new(BW, BH, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->id = services->open(services->ctx, "Bench", BW, BH);
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/bench.html");
    }
    if (!st->id) { tiku_surface_free(st->surface); free(st); return -1; }
    tiku_tabs_init(&st->tabs);
    const char *tabs[] = {"Suites", "Run options", "Results", "Output", "Console",
                          "History", "Benchmarks"};
    for (int i = 0; i < 7; i++) {
        tiku_tabs_add(&st->tabs, tabs[i]); tiku_list_init(&st->list[i], 1);
    }
    tiku_dialog_init(&st->dialog); tiku_choice_init(&st->target);
    for (int i = 0; i < 5; i++) {
        tiku_field_start(&st->field[i], ""); tiku_field_input_init(&st->input[i]);
    }
    tiku_field_start(&st->field[1], "115200");
    locate_bench(st); targets_refresh(st); metadata_start(st);
    paint(st); publish(st); *state = st;
    return 0;
}

/** Shutdown cannot hand the serial port back while test descendants own it. */
static void bench_stop(void *state)
{
    bench_t *st = state;
    if (!st) return;
    if (st->child > 0) {
        (void)kill(-st->child, SIGKILL);
        while (waitpid(st->child, NULL, 0) < 0 && errno == EINTR) {}
    }
    if (st->child_fd >= 0) close(st->child_fd);
    if (st->input_fd >= 0) close(st->input_fd);
    if (st->log) fclose(st->log);
    if ((st->released || st->waiting) && st->services->board_reclaim)
        st->services->board_reclaim(st->services->ctx, st->target_name);
    tiku_surface_free(st->surface); free(st);
}
const tiku_app_descriptor_t tiku_bench_app = {
    .id = "tiku.bench", .name = "Bench", .start = bench_start,
    .stop = bench_stop, .tick = bench_tick, .pick = bench_pick,
    .event_in = bench_event, .board_said = bench_board_said
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_bench_app
};
#endif
