/* tiku_console_private.h -- TikuConsole's shared state, layout constants,
 * control ids and internal functions.
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

#define CW 960                  /**< the window's width */
#define CH 640                  /**< and height */
#define ROWS 1024               /**< lines the console keeps */
#define COLS 80                 /**< glyphs in a line */
#define OUT_MAX 8192            /**< bytes queued for the board */
#define FRAME_TTL_US 30000000   /**< how long a frame may stay open */
/** Command and control ids: commands and buttons, then the device
 *  dropdown, the output list, and the fields from FIELD on. */
enum { START = 1, RETURN, FILES, SEND, BREAK, CLEAR, COPY, FOLLOW,
       RECORD, LOG_PATH, PING, PING_STOP, SLIP_ON, SLIP_OFF, INFO,
       MEMORY, WIFI_SCAN, WIFI_STATUS, DIRECT, COPY_LINE,
       TARGET = 30, LIST, FIELD = 40 };
/** The board's handoff: on the desktop, being released, the console's,
 *  being returned. */
enum { IDLE, RELEASING, ACTIVE, RECLAIMING };
/** The tabs. */
enum { CONSOLE, NETWORK, DEVICE };
/** @brief One cell of a console line. */
typedef struct {
    char glyph[5];                  /**< its UTF-8 text; empty for none */
    unsigned char ink, bold;        /**< its ANSI colour, 16 the default;
                                         whether it is bold */
} glyph_t;
/** @brief One console line. */
typedef struct {
    glyph_t cell[COLS];             /**< its cells */
    int length;                     /**< cells written */
} line_t;
/** @brief TikuConsole's state. */
typedef struct {
    const tiku_app_services_t *services;    /**< the desktop's calls */
    tiku_surface_t *surface; uint32_t id;   /**< the window's pixels; its id */
    /** the dialog controls, the tabs, the device dropdown */
    tiku_dialog_t dialog; tiku_tabs_t tabs; tiku_choice_t targets;
    tiku_list_t list;                       /**< the Console tab's lines */
    /** the fields (command line, Find, baud, ping address, probes) and
     *  their input state */
    tiku_field_t field[5]; tiku_field_input_t input[5];
    tiku_text_input_t direct_commit;        /**< stages typed text */
    /** the ring of lines; the oldest; lines held; the cursor's column;
     *  the lines that pass the Find filter, and how many */
    line_t lines[ROWS]; int head, count, column, index[ROWS], visible;
    /** the decoder: 0 text, 1 after ESC, 2 in CSI, 3 in OSC, 4 ESC in
     *  OSC; the CSI parameters and their count; the ink and bold in use;
     *  UTF-8 bytes awaited and held */
    int scan, param[8], nparam, ink, bold, utf_need, utf_n;
    char utf[5];                            /**< the UTF-8 bytes held */
    /** the last 32 commands; the line being typed while moving through
     *  them */
    char history[32][TIKU_FIELD_MAX], draft[TIKU_FIELD_MAX];
    int nhistory, history_at;               /**< commands kept; the one shown */
    /** the board list as last read; the chosen board; its port, MCU and
     *  stats */
    char names[1024], board[64], port[256], mcu[64], stats[4096];
    /** the status line; the recording's path; the recording */
    char note[512], log_path[256]; FILE *log;
    /** the handoff (IDLE ..); the port's descriptor; a repaint is due;
     *  follow new lines; direct typing; the window is closing; the
     *  handoff was cancelled; Files waits for the board's return */
    int state, fd, dirty, follow, direct, closing, cancelled, files_pending;
    /** the last tick's time; the next board-list read; when a handoff
     *  times out */
    int64_t now, poll_at, deadline;
    /** bytes queued for the board; queued; sent */
    unsigned char output[OUT_MAX]; size_t out_n, out_at;
    unsigned long long received, sent;      /**< bytes over the port */
    unsigned char packet[2048]; size_t packet_n;  /**< an IPv4 packet read */
    /** the wire: 0 text, 1 after END, 2 in a packet; an ESC came; the
     *  packet is damaged; inside a window frame */
    int slip_state, slip_escape, slip_bad, gui_frame;
    int64_t frame_at;           /**< when the open frame began, for its TTL */
    /** a ping goes; probes asked for; sent; answered */
    int ping_active, ping_count, ping_sent, ping_received;
    /** the ping's ICMP id; this end's address and the board's */
    uint16_t ping_id; uint32_t source, destination;
    /** when the next probe goes; when each went; which were answered */
    int64_t ping_next, ping_times[100]; unsigned char ping_seen[100];
    double rtt_min, rtt_max, rtt_sum;       /**< round-trip times, in ms */
    char net_note[256];                     /**< the Network tab's summary */
    /** the round-trip times plotted: their history, its samples, the plot */
    tiku_history_t rtts; tiku_sample_t samples[100]; tiku_sensor_plot_t plot;
} console_t;

/** @brief TikuConsole's descriptor. */
extern const tiku_app_descriptor_t tiku_console_app;

#pragma GCC visibility push(hidden)

/* The device dropdown, the tabs and the output list, in window
 * coordinates. */
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
