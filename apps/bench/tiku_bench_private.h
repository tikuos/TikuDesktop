/* tiku_bench_private.h -- the Bench family's shared state, layout
 * constants, control ids and internal functions.
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
#include "tiku_msg.h"
#include "tiku_tabs_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

#define BW 960                      /**< opening and least width */
#define BH 620                      /**< opening and least height: under
                                         the bar on a 680-px desk */
#define NS 192                      /**< the most suites */
#define NL 1024                     /**< the most leaves, in all suites */
#define NR 1024                     /**< the most result rows */
#define NH 256                      /**< the most history and benchmark
                                         records */
#define ONLY_MAX 8192               /**< bytes of a job's --only list */
#define LINE_MAXIMUM 8192           /**< bytes of a runner output line */
/** The tabs over the views, in order. */
enum { TAB_SUITES, TAB_RESULTS, TAB_OUTPUT, TAB_CONSOLE,
       TAB_HISTORY, TAB_METRICS, TAB_NOTRUN, TAB_FIRMWARE, TAB_COUNT };
/** Command and control ids: menu commands and buttons, then the dialog's
 *  dropdowns, list and fields; F_FILTER .. F_FWVARS are the fields. */
enum { RUN = 1, STOP, REFRESH, SELECT_ALL, CLEAR, OPEN, TOGGLE, COPY_LOG,
       CLEAR_LOG, SEND, FLASH, LARGE, USB, PK, EPAPER, FOLLOW, OPEN_LOG,
       FIND_SUITES, FOCUS_LIST, REPORT = 20, MONITOR = 21, RUN_DESTRUCTIVE = 22,
       FW_PREVIEW = 23, FW_INSTALL = 24, FW_CONFIRM = 25,
       TARGET = 30,
       PANEL = 32, PATTERN = 33, FW_PRESET = 34,
       LIST = 31, F_FILTER = 40, F_BAUD, F_MAKE, F_FLAGS, F_INPUT, F_FWVARS };
#define NFIELD 6                    /**< F_FILTER .. F_FWVARS */
/** @brief A suite of the catalogue. */
typedef struct {
    /** its key, group, display name, the MCUs it runs on (comma-separated)
     *  and its description */
    char key[48], group[64], name[128], boards[512], description[768];
    /** needs the MSP430 large memory model; runs on this computer only;
     *  its first leaf in leaves[] and how many; selected whole; open in
     *  the list */
    int large, host_only, first, count, selected, expanded;
} suite_t;
/** @brief A leaf of a suite: one case tikubench can run on its own. */
typedef struct {
    /** its id, its label, the profiles it runs under, its group and its
     *  kind ("functional", "destructive" and others) */
    char id[96], label[160], profiles[512], group[64], kind[64];
    /** why it does not run, as "profile=reason" items split by ";;";
     *  the profiles whose full run includes it */
    char reasons[2048], full[512];
    int selected;                   /**< chosen for the next run */
} leaf_t;
/** @brief A board the catalogue knows. */
typedef struct {
    char key[64];                   /**< its MCU, as the catalogue names it */
    int baud, large, usb;           /**< its default baud; whether it has
                                         the large memory model and a USB
                                         console */
} board_t;
/** @brief A row of the suite list: a suite, one of its leaves, or a
 *         heading over a group of suites. */
typedef struct {
    int suite, leaf, group;         /**< indexes: suite -1 for a heading,
                                         leaf -1 for a suite or heading */
} entry_t;
/** @brief A group the catalogue puts suites in, in the order first
 *         named. */
typedef struct {
    char name[64];                  /**< the group's name */
    int folded, count;              /**< only its heading shows; its
                                         suites that match the filter */
} group_t;
#define NG 16                       /**< the most suite groups */
/** @brief A result row of the run. */
typedef struct {
    char id[160], text[256];        /**< the case or suite; the row's text */
    /** its job; the passes and failures counted; harness validation
     *  failed; settled; ran nothing */
    int job, pass, fail, harness, done, skip;
} result_t;
/** @brief A past run of a suite, from tikubench's history. */
typedef struct {
    /** the board, suite, kind ("full", "selected" or "diagnostic"),
     *  verdict and profile */
    char board[64], suite[48], kind[32], verdict[24], profile[64];
    long age;                       /**< seconds since the run */
} past_t;
/** @brief A benchmark record: field 1 is the profile, then the metric's
 *         name, unit, value, baseline, verdict and sample count. */
typedef struct {
    char field[8][128];             /**< the record's fields, 0 unused */
} metric_t;
/** @brief A job: one suite, the leaves it is narrowed to, and how many
 *         of them may erase the store, which needs the person's consent. */
typedef struct {
    int suite, destructive;         /**< the suite's index; how many
                                         destructive leaves are selected */
    char key[48], only[ONLY_MAX];   /**< the suite's key; its --only list,
                                         empty for the whole suite */
} job_t;
/** @brief A line of the run's log. */
typedef struct {
    char text[LINE_MAXIMUM];        /**< the line */
    int output;                     /**< the Output tab shows it */
} logline_t;
/* The Firmware tab: what `tikubench firmware` offers for the board, what
 * its runs said, and which run is going.  A row of the option list is an
 * option's index, or -1 - n for the heading over group n. */
#define NFWP 16                     /**< the most firmware presets */
#define NFWO 64                     /**< the most firmware options */
/** The firmware tool's state: idle, asking, previewing or flashing. */
enum { FWS_IDLE, FWS_QUERY, FWS_PREVIEW, FWS_FLASH };
/** A firmware run's steps, in order. */
enum { FW_CHECK, FW_BUILD, FW_FLASH, FW_VERIFY, FW_NSTAGE };
/** @brief A preset as the tool offers it. */
typedef struct {
    /** its key, label and note, and the Makefile's refusal, empty when
     *  it builds */
    char key[24], label[64], note[256], error[256];
} fw_preset_t;
/** @brief An option that applies to the part, its value as make resolved
 *         it. */
typedef struct {
    /** its name, value, kind ("bool" for a switch), group and help */
    char name[48], value[24], kind[8], group[24], help[96];
} fw_option_t;
/* An option the person changed: kept apart from the options, which each
 * answer replaces, and sent with every run as --set NAME=VALUE. */
typedef struct {
    char name[48];                  /**< the knob */
    int value;                      /**< what it is set to: 0 or 1 */
} fw_set_t;
/** @brief The Firmware tab's state. */
typedef struct {
    fw_preset_t preset[NFWP]; int npreset;  /**< what the tool offers */
    fw_option_t option[NFWO]; int noption;  /**< the options that apply */
    fw_set_t set[24]; int nset;             /**< what the person changed */
    int row[NFWO + 16]; int nrow;           /**< the option list's rows */
    char group[16][24]; int ngroup;         /**< the headings they sit under */
    tiku_choice_t choice;           /**< the preset dropdown */
    /** each step's state ("start", "ok", "fail", "skip") and its words */
    char stage[FW_NSTAGE][8], stage_text[FW_NSTAGE][200];
    /** the board key the presets named; the MCU, port and image they
     *  were asked for; why there are no presets or options; what the
     *  preview says the update keeps, and its note */
    char board[32], asked[256], error[512], preview[512], pnote[256];
    char keep[24];                  /**< the preset to choose again */
    char image[64], status[256];    /**< the image's build; the board's state */
    /** which step is going, which query, whether it installs, whether the
     *  options must be asked again, its verdict, the option row pressed */
    int step, query, install, want_options, ok, pressed;
    int handoff;                    /**< the board is released for it */
    int asking;                     /**< the install question is open */
} fw_t;
/** @brief Bench's state: the window, the catalogue, the run and the
 *         Firmware tab. */
typedef struct {
    const tiku_app_services_t *services;    /**< the desktop's calls */
    tiku_surface_t *surface; uint32_t id;   /**< the window's pixels; its id */
    /** the tabs, the dialog controls, the device dropdown */
    tiku_tabs_t tabs; tiku_dialog_t dialog; tiku_choice_t target;
    tiku_choice_t epanel, epattern;   /**< the ePaper presets, GTK's names */
    /** the text fields, F_FILTER .. F_FWVARS, and their input state */
    tiku_field_t field[NFIELD]; tiku_field_input_t input[NFIELD];
    /** each tab's list; the open question */
    tiku_list_t list[TAB_COUNT]; tiku_alert_t confirm;
    suite_t suites[NS]; int nsuite;         /**< the catalogue's suites */
    leaf_t leaves[NL]; int nleaf;           /**< their leaves */
    board_t boards[32]; int nboard;         /**< the boards it knows */
    entry_t entries[NS + NL + NG]; int nentry;  /**< the suite list */
    group_t groups[NG]; int ngroup;         /**< the suite groups */
    past_t history[NH]; int nhistory;       /**< past runs */
    metric_t metrics[NH]; int nmetric;      /**< benchmark records */
    result_t results[NR]; int nresult;      /**< the run's result rows */
    /** the log ring of the last 256 lines: lines, count, next slot */
    logline_t logs[256]; int nlog, log_head;
    job_t jobs[NS]; int njob, job;          /**< the queue; the job going */
    /** the chosen board: its name, MCU (lower case), port and profile */
    char target_name[64], mcu[64], port[256], profile[64];
    /** the board list as last read; the TikuBench checkout; the status
     *  line; the run log's path */
    char mounted[1024], directory[512], note[512], log_path[512];
    /** the remembered board, chosen once mounted; the options file; the
     *  options text last written */
    char wanted[64], settings[512], saved[1024];
    int w, h;                   /**< the window's content, BW x BH at least */
    int refreshed;              /**< the board list has been read once */
    /** build and flash (else monitor); the MSP430 large model; the USB
     *  console; CRACEN PK; the ePaper kit; the catalogue knows the
     *  board; a repaint is due */
    int flash, large, usb, pk, epaper, supported, dirty;
    /** a run goes; its release is awaited; the board is released; its
     *  return is awaited; Stop was pressed; the window is closing */
    int busy, waiting, released, reclaiming, cancelled, closing;
    int allow_destructive;      /**< said for this run, never remembered */
    /** reading the catalogue; it loaded whole; it was malformed or
     *  overflowed; output is in a build or flash; a suite failed; no
     *  test passed or failed */
    int loading, metadata_ok, metadata_bad, in_build, batch_failed, no_tests;
    /** the runner's output pipe and input socket; its output ended; it
     *  was reaped; its exit status */
    int child_fd, input_fd, eof, reaped, exit_status;
    pid_t child;                /**< the runner's process, 0 for none */
    /** the last tick's time; when a handoff times out; when a stopped
     *  runner is killed; when the board list is next read */
    int64_t now, deadline, stop_at, board_poll;
    /** the output line being read, its length, and whether it overflowed */
    char partial[LINE_MAXIMUM]; size_t partial_n; int overflow;
    /** console input to send, its length, and how much has gone */
    char sendbuf[TIKU_FIELD_MAX + 2]; size_t send_n, send_at;
    /** the case running; an action prompt from the runner; Output and
     *  Console follow new lines; the question's button pressed; the
     *  suite-list row pressed, and whether on its arrow */
    char active[160], prompt[256]; int follow, confirm_button, pressed_row, pressed_expand;
    FILE *log;                  /**< the run's log file */
    fw_t fw;                    /**< the Firmware tab */
} bench_t;

/** @brief A runner command: its argv and the storage for words built for
 *         it. */
typedef struct {
    char *argv[128];                /**< NULL-terminated */
    char words[32][256];            /**< words built for argv */
    int argc, nword;                /**< argv's length; words used */
} command_t;

#define RX 260                      /**< the views' column, to the right edge */
#define RW (st->w - RX - 10)        /**< the views' width */

/** @brief Bench's descriptor. */
extern const tiku_app_descriptor_t tiku_bench_app;

#pragma GCC visibility push(hidden)

/* Where the question and the cards are drawn, in window coordinates. */
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
/** How many selected leaves of the queued suites may erase the store;
 *  the run asks about them before it starts. */
int tiku_bench_queue_destructive(const bench_t *st);

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
/** Ask for the released board back, or settle when it was never let go. */
void tiku_bench_reclaim(bench_t *st);
int tiku_bench_process_spawn(bench_t *st, char *const argv[]);
void tiku_bench_run_next(bench_t *st);
void tiku_bench_process_poll(bench_t *st);
void tiku_bench_stop_run(bench_t *st);
void tiku_bench_run_confirmed(bench_t *st);
/** The run's question: the queue, the device and the mode, Cancel first. */
void tiku_bench_ask_run(bench_t *st);

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

/* Firmware. */
/** On the Firmware tab with the runner free, ask for the presets when the
 *  board, its port or its image changed, or for the options when wanted. */
void tiku_bench_fw_poll(bench_t *st);
/** Take one line a `tikubench firmware --events tsv` run printed: a record
 *  updates the tab, any other line goes to the log. */
void tiku_bench_fw_line(bench_t *st, char *line);
/** Start a preview, or with @p install a preview that then asks to
 *  install; the board is released to the tool first. */
void tiku_bench_fw_start(bench_t *st, int install);
/** With the board released, start the firmware runner for the step asked
 *  for: preview or flash. */
void tiku_bench_fw_spawn(bench_t *st);
/** The firmware runner has exited: take up the presets or options it
 *  answered, ask the install question after a clean preview, or report
 *  the result and ask for the board back. */
void tiku_bench_fw_exit(bench_t *st);
/** The answer to the install question: 1 installs, 0 hands the board back. */
void tiku_bench_fw_confirmed(bench_t *st, int install);
/** Flip the switch on row @p row of the option list; an option that takes
 *  a value is set in the Extra field instead. */
void tiku_bench_fw_toggle(bench_t *st, int row);
/** A different preset was chosen: clear the last run's report and ask
 *  for its options. */
void tiku_bench_fw_preset_changed(bench_t *st);
/** Whether Preview and Install can start: a board, a preset, a free runner. */
int tiku_bench_fw_ready(const bench_t *st);
/** The preset dropdown on the Firmware tab. */
tiku_rect_t tiku_bench_fw_preset_rect(const bench_t *st);
/** Paint the Firmware tab: what the board runs, the preset, the option
 *  list, the Extra field's label, the step pills and the status. */
void tiku_bench_fw_draw(bench_t *st, tiku_rect_t body);

/* Commands. */
void tiku_bench_publish(bench_t *st);
int tiku_bench_action(bench_t *st, int command);
int tiku_bench_event(void *state, uint32_t window, const tiku_event_t *ev);

#pragma GCC visibility pop

#endif /* TIKU_BENCH_PRIVATE_H_ */
