/* tiku_bench_private.h -- private application interface.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_BENCH_PRIVATE_H_
#define TIKU_BENCH_PRIVATE_H_

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

#define BW 960                      /* the opening size, and the least: */
#define BH 620                      /* under the bar on a 680-px desk   */
#define NS 192
#define NL 1024
#define NR 1024
#define NH 256
#define ONLY_MAX 8192
#define LINE_MAXIMUM 8192
enum { TAB_SUITES, TAB_RESULTS, TAB_OUTPUT, TAB_CONSOLE,
       TAB_HISTORY, TAB_METRICS, TAB_NOTRUN, TAB_COUNT };
enum { RUN = 1, STOP, REFRESH, SELECT_ALL, CLEAR, OPEN, TOGGLE, COPY_LOG,
       CLEAR_LOG, SEND, FLASH, LARGE, USB, PK, EPAPER, FOLLOW, OPEN_LOG,
       FIND_SUITES, FOCUS_LIST, REPORT = 20, MONITOR = 21, TARGET = 30,
       PANEL = 32, PATTERN = 33,
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
/* A row of the suite list: a suite, one of its leaves, or a heading
 * over a group of suites (suite -1, group the index into groups[]). */
typedef struct { int suite, leaf, group; } entry_t;
/* A category the catalogue groups suites under, in its own order. */
typedef struct { char name[64]; int folded, count; } group_t;
#define NG 16
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
    tiku_choice_t epanel, epattern;   /* the ePaper presets, GTK's names */
    tiku_field_t field[5]; tiku_field_input_t input[5];
    tiku_list_t list[TAB_COUNT]; tiku_alert_t confirm;
    suite_t suites[NS]; int nsuite;
    leaf_t leaves[NL]; int nleaf;
    board_t boards[32]; int nboard;
    entry_t entries[NS + NL + NG]; int nentry;
    group_t groups[NG]; int ngroup;
    past_t history[NH]; int nhistory;
    metric_t metrics[NH]; int nmetric;
    result_t results[NR]; int nresult;
    logline_t logs[256]; int nlog, log_head;
    job_t jobs[NS]; int njob, job;
    char target_name[64], mcu[64], port[256], profile[64];
    char mounted[1024], directory[512], note[512], log_path[512];
    char wanted[64], settings[512], saved[1024]; /* the remembered options */
    int w, h;                   /* the window's content, BW x BH at least */
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

typedef struct { char *argv[128]; char words[32][256]; int argc, nword; } command_t;

#define RX 260                      /* the views' column, to the right edge */
#define RW (st->w - RX - 10)

extern const tiku_app_descriptor_t tiku_bench_app;

#pragma GCC visibility push(hidden)

extern const tiku_rect_t TIKU_BENCH_ALERT_RECT;
extern const tiku_rect_t TIKU_BENCH_CARD_DEVICE;
extern const tiku_rect_t TIKU_BENCH_CARD_HISTORY;
extern const tiku_rect_t TIKU_BENCH_CARD_LINK;
extern const tiku_rect_t TIKU_BENCH_CARD_MODE;
extern const tiku_rect_t TIKU_BENCH_CARD_OPTIONS;
extern const tiku_rect_t TIKU_BENCH_CARD_EPAPER;
extern const tiku_rect_t TIKU_BENCH_CARD_MAKE;

/* App. */
void tiku_bench_tick(void *state, int64_t now_us);
void tiku_bench_board_said(void *state, int what, const char *text);
int tiku_bench_start(void **state, const tiku_app_services_t *services);
void tiku_bench_stop(void *state);

/* Catalogue. */
int tiku_bench_csv_has(const char *csv, const char *word);
void tiku_bench_locate_bench(bench_t *st);
void tiku_bench_metadata_line(bench_t *st, char *line);
void tiku_bench_clear_selection(bench_t *st);
int tiku_bench_suite_supported(const bench_t *st, const suite_t *s);
int tiku_bench_leaf_supported(const bench_t *st, const leaf_t *l, int full);
const char *tiku_bench_leaf_reason(const bench_t *st, const leaf_t *l, char *out, size_t cap);
void tiku_bench_entries_build(bench_t *st);
void tiku_bench_notrun_build(bench_t *st);
void tiku_bench_toggle_entry(bench_t *st, int row, int expand);
int tiku_bench_queue_build(bench_t *st);
int tiku_bench_queue_needs_device(const bench_t *st);

/* Device. */
const board_t *tiku_bench_board_info(const bench_t *st);
void tiku_bench_profile_update(bench_t *st);
void tiku_bench_target_capture(bench_t *st);
void tiku_bench_targets_refresh(bench_t *st);

/* Runner. */
int tiku_bench_command_build(bench_t *st, const job_t *job, command_t *c);
int tiku_bench_queue_first_invalid(bench_t *st, command_t *c);
void tiku_bench_metadata_start(bench_t *st);
void tiku_bench_release_finish(bench_t *st);
void tiku_bench_run_next(bench_t *st);
void tiku_bench_process_poll(bench_t *st);
void tiku_bench_stop_run(bench_t *st);
void tiku_bench_run_confirmed(bench_t *st);

/* Results. */
void tiku_bench_log_line(bench_t *st, const char *text, int output);
result_t *tiku_bench_result_add(bench_t *st, int job, const char *id);
void tiku_bench_plan_build(bench_t *st);
void tiku_bench_consume_line(bench_t *st, char *line);
void tiku_bench_report_open(bench_t *st);

/* Settings. */
void tiku_bench_settings_text(const bench_t *st, char *out, size_t cap);
void tiku_bench_settings_flush(bench_t *st);
void tiku_bench_settings_load(bench_t *st);

/* Layout. */
tiku_rect_t tiku_bench_tabs_rect(const bench_t *st);
tiku_rect_t tiku_bench_screen_rect(const bench_t *st);
tiku_rect_t tiku_bench_target_rect(void);
tiku_rect_t tiku_bench_preset_rect(int pattern);
tiku_rect_t tiku_bench_field_rect(const bench_t *st, int i);
tiku_rect_t tiku_bench_list_rect(const bench_t *st, int tab);
void tiku_bench_text(bench_t *st, tiku_rect_t r, const char *value, int bold);
int tiku_bench_selected_count(const bench_t *st);
int tiku_bench_editable(const bench_t *st);
void tiku_bench_controls(bench_t *st);
void tiku_bench_controls_draw(bench_t *st);

/* View. */
const char *tiku_bench_details_row(void *ctx, int row);
const char *tiku_bench_history_verdict(const bench_t *st, char *out, size_t cap, char *perf, size_t perf_cap);
void tiku_bench_paint(bench_t *st);

/* Commands. */
void tiku_bench_publish(bench_t *st);
int tiku_bench_action(bench_t *st, int command);
int tiku_bench_event(void *state, uint32_t window, const tiku_event_t *ev);

#pragma GCC visibility pop

#endif /* TIKU_BENCH_PRIVATE_H_ */
