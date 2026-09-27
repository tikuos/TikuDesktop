/* tiku_bench_app.c -- app.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

static int bench_pick(void *state, uint32_t window, int command);

/** Pump bounded process output without blocking the desktop. */
void tiku_bench_tick(void *state, int64_t now_us)
{
    bench_t *st = state;
    st->now = now_us;
    tiku_bench_process_poll(st);
    tiku_bench_fw_poll(st);
    if (st->stop_at && st->now >= st->stop_at && st->child > 0) {
        (void)kill(-st->child, SIGKILL); st->stop_at = 0;
    }
    if (st->waiting && st->now >= st->deadline) {
        st->cancelled = 1; st->waiting = 0; st->released = 1;
        tiku_bench_release_finish(st);
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
        tiku_bench_targets_refresh(st); st->board_poll = now_us + 1000000;
    }
    if (st->dirty) {
        tiku_bench_paint(st); tiku_bench_publish(st); tiku_bench_settings_flush(st); st->dirty = 0;
    }
    if (st->closing && !st->busy && !st->child && !st->loading)
        st->services->close(st->services->ctx, st->id);
}

/** Never launch until the shell has acknowledged exclusive port ownership. */
void tiku_bench_board_said(void *state, int what, const char *text)
{
    bench_t *st = state;
    if ((what == TIKU_APP_BOARD_RELEASED || what == TIKU_APP_BOARD_RECLAIMED) &&
        (!text || strcmp(text, st->target_name))) return;
    if (what == TIKU_APP_BOARD_RELEASED && st->waiting) {
        st->waiting = 0; st->released = 1;
        if (st->cancelled) tiku_bench_release_finish(st); else tiku_bench_run_next(st);
    } else if (what == TIKU_APP_BOARD_RECLAIMED && st->reclaiming) {
        st->reclaiming = st->released = st->busy = 0;
        /* A firmware run changes the board, not the catalogue; the tab
         * asks again when what the board says about itself changes. */
        if (st->fw.handoff) st->fw.handoff = 0;
        else tiku_bench_metadata_start(st);
    } else if (what == TIKU_APP_BOARD_FAILED && (st->waiting || st->reclaiming)) {
        st->waiting = st->reclaiming = st->released = st->busy = 0;
        st->fw.handoff = 0; st->fw.step = FWS_IDLE; st->fw.install = 0;
        snprintf(st->note, sizeof st->note,
                 "Device handoff failed; check Devices before retrying.");
    }
    st->dirty = 1;
}
static int bench_pick(void *state, uint32_t window, int command)
{ (void)window; return tiku_bench_action(state, command); }

int tiku_bench_start(void **state, const tiku_app_services_t *services)
{
    bench_t *st = calloc(1, sizeof *st);
    if (!st) return -1;
    st->services = services; st->child_fd = st->input_fd = -1; st->follow = 1; st->pressed_row = st->fw.pressed = -1;
    st->w = BW; st->h = BH;
    st->surface = tiku_surface_new(BW, BH, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->id = services->open(services->ctx, "Bench", BW, BH);
    /* The views grow with the window; the cards keep their column. */
    if (st->id && services->sizeable != NULL)
        (void)services->sizeable(services->ctx, st->id);
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/bench.html");
    }
    if (!st->id) { tiku_surface_free(st->surface); free(st); return -1; }
    tiku_tabs_init(&st->tabs);
    const char *tabs[] = {"Suites", "Results", "Output", "Console",
                          "History", "Benchmarks", "Did not run", "Firmware"};
    for (int i = 0; i < TAB_COUNT; i++) {
        tiku_tabs_add(&st->tabs, tabs[i]); tiku_list_init(&st->list[i], 1);
    }
    tiku_dialog_init(&st->dialog); tiku_choice_init(&st->target);
    /* The ePaper presets, the GTK frontend's own tables: a panel's value
     * is what EPAPER_KIT_PANEL takes, a pattern's index is its value. */
    tiku_choice_init(&st->epanel); tiku_choice_init(&st->epattern);
    tiku_choice_add(&st->epanel, "E2370JS0C1 - 3.70\" BWR", 1);
    tiku_choice_add(&st->epanel, "E2266KS0C1 - 2.66\" BW", 1);
    for (int i = 0; i < 4; i++)
        tiku_choice_add(&st->epattern, (const char *[]){"White", "Black",
                        "Checkerboard", "Text"}[i], 1);
    st->epanel.selected = 0; st->epattern.selected = 0;
    for (int i = 0; i < NFIELD; i++) {
        tiku_field_start(&st->field[i], ""); tiku_field_input_init(&st->input[i]);
    }
    tiku_choice_init(&st->fw.choice);
    tiku_field_start(&st->field[1], "115200");
    tiku_bench_locate_bench(st); tiku_bench_settings_load(st);
    /* Opened for one board's firmware (Device Preferences' Update): that
     * board, when it is mounted, and the Firmware tab. */
    if (services->startup_message) {
        const char *tab = tiku_msg_find_string(services->startup_message, "tab", 0);
        const char *board = tiku_msg_find_string(services->startup_message, "board", 0);
        if (tab && !strcmp(tab, "firmware")) tiku_tabs_select(&st->tabs, TAB_FIRMWARE);
        if (board && *board) snprintf(st->wanted, sizeof st->wanted, "%s", board);
    }
    tiku_bench_targets_refresh(st);
    tiku_bench_metadata_start(st);
    /* Where the options stand now is the file's baseline: nothing is
     * written until the user moves one. */
    tiku_bench_settings_text(st, st->saved, sizeof st->saved);
    tiku_bench_paint(st); tiku_bench_publish(st); *state = st;
    return 0;
}

/** Shutdown cannot hand the serial port back while test descendants own it. */
void tiku_bench_stop(void *state)
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
    tiku_bench_settings_flush(st);
    if ((st->released || st->waiting) && st->services->board_reclaim)
        st->services->board_reclaim(st->services->ctx, st->target_name);
    tiku_surface_free(st->surface); free(st);
}
const tiku_app_descriptor_t tiku_bench_app = {
    .id = "tiku.bench", .name = "Bench", .start = tiku_bench_start,
    .stop = tiku_bench_stop, .tick = tiku_bench_tick, .pick = bench_pick,
    .event_in = tiku_bench_event, .board_said = tiku_bench_board_said
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_bench_app
};
#endif
