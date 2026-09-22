/* tiku_console_app.c -- app.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_console_private.h"

static int console_pick(void *state, uint32_t window, int command);

/** Start with no serial I/O; the person explicitly opens the selected console. */
int tiku_console_start(void **state, const tiku_app_services_t *services)
{
    console_t *st = calloc(1, sizeof *st);
    if (!st) return -1;
    st->services = services; st->fd = -1; st->follow = 1;
    st->ink = 16; st->count = 1;
    st->ping_id = (uint16_t)getpid();
    st->surface = tiku_surface_new(CW, CH, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->id = services->open(services->ctx, "TikuConsole", CW, CH);
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/console.html");
    }
    if (!st->id) { tiku_surface_free(st->surface); free(st); return -1; }
    tiku_dialog_init(&st->dialog); tiku_tabs_init(&st->tabs);
    tiku_tabs_add(&st->tabs, "Console"); tiku_tabs_add(&st->tabs, "Network");
    tiku_tabs_add(&st->tabs, "Device"); tiku_choice_init(&st->targets);
    tiku_list_init(&st->list, 1); tiku_text_input_reset(&st->direct_commit);
    const char *values[] = {"", "", "115200", "172.16.7.2", "4"};
    for (int i = 0; i < 5; i++) {
        tiku_field_start(&st->field[i], values[i]); tiku_field_input_init(&st->input[i]);
    }
    tiku_history_init(&st->rtts, st->samples, 100);
    st->plot.autoscale = 1;
    st->plot.count = 1; st->plot.trace[0].history = &st->rtts;
    st->plot.trace[0].label = "Round-trip time"; st->plot.unit = "ms";
    st->plot.trace[0].ink = TIKU_RGB(30, 110, 180);
    st->plot.gap_us = 3000000;
    tiku_console_targets_refresh(st);
    snprintf(st->note, sizeof st->note, "Select a desktop device, then Open console. No port is opened at startup.");
    snprintf(st->net_note, sizeof st->net_note, "Rootless ICMP over SLIP; no host network changes.");
    tiku_console_paint(st); tiku_console_publish(st); *state = st; return 0;
}

/** Close this application's descriptor before the desktop takes the wire. */
void tiku_console_stop(void *state)
{
    console_t *st = state;
    if (!st) return;
    if (st->fd >= 0) close(st->fd);
    if (st->log) fclose(st->log);
    if (st->state != IDLE && st->state != RECLAIMING && st->services->board_reclaim)
        st->services->board_reclaim(st->services->ctx, st->board);
    tiku_surface_free(st->surface); free(st);
}

/** Bounded serial work on each beat; no loops waiting for a device response. */
void tiku_console_tick(void *state, int64_t now)
{
    console_t *st = state; st->now = now;
    tiku_console_io_poll(st);
    if (st->state == ACTIVE) tiku_console_ping_tick(st);
    if ((st->state == RELEASING || st->state == RECLAIMING) && now >= st->deadline) {
        if (st->state == RELEASING) { st->cancelled = 1; tiku_console_reclaim(st); }
        else { st->state = IDLE; st->files_pending = 0;
            snprintf(st->note, sizeof st->note, "Device did not return. Reconnect it through Devices."); }
        st->dirty = 1;
    }
    if (st->state == IDLE && now >= st->poll_at) {
        tiku_console_targets_refresh(st); st->poll_at = now + 1000000;
    }
    if (st->dirty) { tiku_console_paint(st); tiku_console_publish(st); st->dirty = 0; }
    if (st->closing && st->state == IDLE) st->services->close(st->services->ctx, st->id);
}
static int console_pick(void *state, uint32_t window, int command)
{ (void)window; return tiku_console_action(state, command); }
const tiku_app_descriptor_t tiku_console_app = {
    .id = "tiku.console", .name = "TikuConsole", .start = tiku_console_start,
    .stop = tiku_console_stop, .tick = tiku_console_tick, .pick = console_pick,
    .event_in = tiku_console_event, .board_said = tiku_console_board_said
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_console_app
};
#endif
