/* tiku_console_private.h -- private application interface.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CONSOLE_PRIVATE_H_
#define TIKU_CONSOLE_PRIVATE_H_

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
#define FRAME_TTL_US 30000000
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

extern const tiku_app_descriptor_t tiku_console_app;

#pragma GCC visibility push(hidden)

extern const tiku_rect_t TIKU_CONSOLE_TARGET_RECT;
extern const tiku_rect_t TIKU_CONSOLE_TABS_RECT;
extern const tiku_rect_t TIKU_CONSOLE_BODY;

/* App. */
int tiku_console_start(void **state, const tiku_app_services_t *services);
void tiku_console_stop(void *state);
void tiku_console_tick(void *state, int64_t now);

/* Terminal. */
line_t *tiku_console_last_line(console_t *st);
void tiku_console_text_byte(console_t *st, unsigned char ch);
void tiku_console_line_string(const line_t *l, char *out, size_t cap);
int tiku_console_contains(const char *text, const char *needle);
void tiku_console_rebuild(console_t *st);
void tiku_console_message(console_t *st, const char *value);
void tiku_console_history_add(console_t *st, const char *command);
void tiku_console_history_move(console_t *st, int by);

/* Device. */
void tiku_console_target_capture(console_t *st);
void tiku_console_target_changed(console_t *st);
void tiku_console_targets_refresh(console_t *st);
int tiku_console_queue_bytes(console_t *st, const void *bytes, size_t n);
void tiku_console_files_open(console_t *st);
void tiku_console_reclaim(console_t *st);
void tiku_console_begin(console_t *st);
void tiku_console_board_said(void *state, int what, const char *value);
void tiku_console_io_poll(console_t *st);
void tiku_console_record_toggle(console_t *st);

/* Network. */
unsigned tiku_console_checksum(const unsigned char *p, size_t n);
void tiku_console_put16(unsigned char *p, unsigned n);
size_t tiku_console_slip_encode(const unsigned char *p, size_t n, unsigned char *out, size_t cap);
void tiku_console_ping_summary(console_t *st);
void tiku_console_ip_packet(console_t *st, const unsigned char *p, size_t n);
void tiku_console_wire_byte(console_t *st, unsigned char b);
void tiku_console_ping_start(console_t *st);
void tiku_console_ping_tick(console_t *st);

/* View. */
const char *tiku_console_label(const console_t *st, int id);
int tiku_console_enabled(const console_t *st, int id);
void tiku_console_controls(console_t *st);
void tiku_console_paint(console_t *st);

/* Commands. */
void tiku_console_publish(console_t *st);
void tiku_console_copy_history(console_t *st, int selected);
int tiku_console_action(console_t *st, int id);
int tiku_console_direct_event(console_t *st, const tiku_event_t *ev);
int tiku_console_event(void *state, uint32_t window, const tiku_event_t *ev);

#pragma GCC visibility pop

#endif /* TIKU_CONSOLE_PRIVATE_H_ */
