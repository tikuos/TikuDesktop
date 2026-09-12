/* TikuConsole: the desktop-kit serial console and rootless SLIP diagnostics.
 * Device discovery and ownership belong to TikuDesktop.
 * SPDX-License-Identifier: Apache-2.0 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "tiku_app.h"
#include "tiku_clipboard.h"
#include "tiku_choice_paint.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_font.h"
#include "tiku_list_paint.h"
#include "tiku_sensor_paint.h"
#include "tiku_tabs_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

#define CW 960
#define CH 640
#define ROWS 1024
#define COLS 80
#define OUT_MAX 8192
enum { START = 1, RETURN, FILES, SEND, BREAK, CLEAR, COPY, FOLLOW,
       RECORD, LOG_PATH, PING, PING_STOP, SLIP_ON, SLIP_OFF, INFO,
       MEMORY, WIFI_SCAN, WIFI_STATUS, DIRECT, COPY_LINE,
       TARGET = 30, LIST, FIELD = 40 };
enum { IDLE, RELEASING, ACTIVE, RECLAIMING };
enum { CONSOLE, NETWORK, DEVICE };
typedef struct { char glyph[5]; unsigned char ink, bold; } glyph_t;
typedef struct { glyph_t cell[COLS]; int length; } line_t;
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface; uint32_t id;
    tiku_dialog_t dialog; tiku_tabs_t tabs; tiku_choice_t targets;
    tiku_list_t list;
    tiku_field_t field[5]; tiku_field_input_t input[5];
    tiku_text_input_t direct_commit;
    line_t lines[ROWS]; int head, count, column, index[ROWS], visible;
    int scan, param[8], nparam, ink, bold, utf_need, utf_n;
    char utf[5];
    char history[32][TIKU_FIELD_MAX], draft[TIKU_FIELD_MAX];
    int nhistory, history_at;
    char names[1024], board[64], port[256], mcu[64], stats[4096];
    char note[512], log_path[256]; FILE *log;
    int state, fd, dirty, follow, direct, closing, cancelled, files_pending;
    int64_t now, poll_at, deadline;
    unsigned char output[OUT_MAX]; size_t out_n, out_at;
    unsigned long long received, sent;
    unsigned char packet[2048]; size_t packet_n;
    int slip_state, slip_escape, slip_bad, gui_frame;
    int64_t frame_at;           /* when the open frame began, for its TTL */
    int ping_active, ping_count, ping_sent, ping_received;
    uint16_t ping_id; uint32_t source, destination;
    int64_t ping_next, ping_times[100]; unsigned char ping_seen[100];
    double rtt_min, rtt_max, rtt_sum;
    char net_note[256];
    tiku_history_t rtts; tiku_sample_t samples[100]; tiku_sensor_plot_t plot;
} console_t;
static void paint(console_t *st);
static void publish(console_t *st);
static void rebuild(console_t *st);
static int action(console_t *st, int command);
static void reclaim(console_t *st);
static int queue_bytes(console_t *st, const void *bytes, size_t n);
#include "tiku_console_model.inl"
#include "tiku_console_network.inl"
#include "tiku_console_io.inl"
#include "tiku_console_ui.inl"

/** Start with no serial I/O; the person explicitly opens the selected console. */
static int console_start(void **state, const tiku_app_services_t *services)
{
    console_t *st = calloc(1, sizeof *st);
    if (!st) return -1;
    st->services = services; st->fd = -1; st->follow = 1;
    st->ink = 16; st->count = 1;
    st->ping_id = (uint16_t)getpid();
    st->surface = tiku_surface_new(CW, CH, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->id = services->open(services->ctx, "TikuConsole", CW, CH);
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
    targets_refresh(st);
    snprintf(st->note, sizeof st->note, "Select a desktop device, then Open console. No port is opened at startup.");
    snprintf(st->net_note, sizeof st->net_note, "Rootless ICMP over SLIP; no host network changes.");
    paint(st); publish(st); *state = st; return 0;
}

/** Close this application's descriptor before the desktop takes the wire. */
static void console_stop(void *state)
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
static void console_tick(void *state, int64_t now)
{
    console_t *st = state; st->now = now;
    io_poll(st);
    if (st->state == ACTIVE) ping_tick(st);
    if ((st->state == RELEASING || st->state == RECLAIMING) && now >= st->deadline) {
        if (st->state == RELEASING) { st->cancelled = 1; reclaim(st); }
        else { st->state = IDLE; st->files_pending = 0;
            snprintf(st->note, sizeof st->note, "Device did not return. Reconnect it through Devices."); }
        st->dirty = 1;
    }
    if (st->state == IDLE && now >= st->poll_at) {
        targets_refresh(st); st->poll_at = now + 1000000;
    }
    if (st->dirty) { paint(st); publish(st); st->dirty = 0; }
    if (st->closing && st->state == IDLE) st->services->close(st->services->ctx, st->id);
}
static int console_pick(void *state, uint32_t window, int command)
{ (void)window; return action(state, command); }
const tiku_app_descriptor_t tiku_console_app = {
    .id = "tiku.console", .name = "TikuConsole", .start = console_start,
    .stop = console_stop, .tick = console_tick, .pick = console_pick,
    .event_in = console_event, .board_said = console_board_said
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_console_app
};
#endif
