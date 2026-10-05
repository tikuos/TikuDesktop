/* tiku_sdr.h -- TikuSDR: a board's radio receiver as a spectrum and a
 * waterfall.  The board (an ESP32-C61 with the receiver built in) computes
 * each spectrum itself and sends one line; this window keeps, stitches and
 * draws them, records them to a file it can play back, and exports them
 * as CSV or a picture.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_SDR_H_
#define TIKU_SDR_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "tiku_alert.h"
#include "tiku_app.h"
#include "tiku_dialog.h"
#include "tiku_gfx.h"

#define SDR_W        940
#define SDR_H        600
#define SDR_BINS     512            /* the most one trace holds        */
#define SDR_ROWS     480            /* waterfall rows remembered       */
#define SDR_RX_MAX   8192           /* bytes waiting for a newline     */
#define SDR_PATH     512            /* a recording's path              */
#define SDR_LINE     2048           /* a recording's longest line      */

/* The band view: 2396 to 2492 MHz in 312.5 kHz bins, six 16 MHz slices of
 * 40 MS/s spectra, each centred where the receiver's filter is flat. */
#define SDR_BAND_LO      2396.0
#define SDR_BAND_BIN     0.3125
#define SDR_BAND_N       307
#define SDR_BAND_FIRST   2404
#define SDR_BAND_LAST    2484
#define SDR_BAND_STEP    16

enum { SDR_LIVE, SDR_BAND };

enum {
    SDR_IDLE, SDR_RELEASING, SDR_OPENING, SDR_STARTING, SDR_RUNNING,
    SDR_STOPPING, SDR_RECLAIMING
};

/* Commands: buttons, menus and keys share them. */
enum {
    CMD_CONNECT = 1, CMD_LIVE, CMD_BAND, CMD_DOWN, CMD_UP, CMD_PEAK,
    CMD_PAUSE, CMD_RECORD,          /* the toolbar's, in its order      */
    CMD_CLEAR, CMD_AVERAGE, CMD_QUIT, CMD_REPLAY, CMD_EXPORT, CMD_PICTURE,
    CMD_TUNE = 100,                 /* + MHz offset from 2400: Tune menu */
    CMD_BOARD = 300                 /* + index: Device menu             */
};
#define SDR_BUTTONS CMD_RECORD

/** @brief What is on screen: the latest trace, its average and peaks, and
 *         the waterfall's history, every value in decibels. */
typedef struct {
    int    mode, centre, rate, nfft, gain;
    int    n;                       /* bins in the trace               */
    double lo_mhz, bin_mhz;         /* bin 0's centre, the spacing     */
    float  db[SDR_BINS], avg[SDR_BINS], peak[SDR_BINS];
    unsigned char seen[SDR_BINS];   /* band: slices heard this sweep   */
    int    peak_on, avg_on, fresh;
    float  scale_lo, scale_hi;      /* the display's range, smoothed   */
    unsigned char wf[SDR_ROWS][SDR_BINS];  /* half-dB, newest at head  */
    double wf_at[SDR_ROWS];         /* each row's time, in seconds     */
    double clock;                   /* the time a frame finished now   */
    int    wf_head, wf_count;
    unsigned spectra;               /* lines taken since connecting    */
} sdr_model_t;

/** @brief A recording being made: each line the board sends, timed. */
typedef struct {
    FILE    *file;                  /* open while recording            */
    char     path[SDR_PATH];
    int64_t  began;                 /* the tick's clock at its start   */
    unsigned frames;                /* spectra and sweeps finished     */
    long     bytes;                 /* written so far                  */
    int      failed;                /* a write did not go through      */
} sdr_rec_t;

/** @brief A recording played back at the pace it was made. */
typedef struct {
    FILE    *file;                  /* open while playing              */
    char     path[SDR_PATH];
    char     line[SDR_LINE];        /* the next line, waiting its time */
    char     from[96];              /* its board and when, from its head */
    int      ready;                 /* line holds a line               */
    double   at, total;             /* line's time and the last, in s  */
    int64_t  clock, last;           /* played so far (us), and the
                                       tick's clock when it last moved */
} sdr_play_t;

/** @brief Spectra written as CSV: a recording converted a slice a tick. */
typedef struct {
    FILE    *in, *out;              /* open while converting           */
    sdr_model_t *model;             /* the recording's, as it plays    */
    char     path[SDR_PATH];        /* the CSV being written           */
    char     line[SDR_LINE];
    long     size, rows;            /* the recording's bytes; rows out */
    unsigned frames;                /* spectra written                 */
} sdr_csv_t;

/** @brief The window, its controls, and the link to the board. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface;
    uint32_t id;
    int w, h;
    tiku_dialog_t dialog;
    tiku_alert_t alert;             /* no ESP32-C61: said over the window */
    sdr_model_t model;
    /* The board: names holds the mounted ESP32-C61s alone. */
    char board[64], port[160], mcu[48], names[1024];
    int looking;                    /* the first look at them is due   */
    int state, fd, paused, pending, closing, stopped;
    int64_t now, deadline, sent_at, rate_at;
    unsigned rate_count;
    float rate_hz;
    char rx[SDR_RX_MAX];
    size_t rx_n;
    char note[160], hover[160];
    int dirty;
    int64_t painted_at, names_at;
    uint32_t *pic;                  /* the waterfall's bitmap          */
    size_t pic_n;
    /* Recordings. */
    sdr_rec_t rec;
    sdr_play_t play;
    int asking;                     /* the file panel up: CMD_RECORD or
                                       CMD_REPLAY                      */
    char tape_dir[SDR_PATH];        /* where the last one was, or ""   */
    char saved[200];                /* what the last recording became  */
    char shown[SDR_PATH];           /* the recording on screen, or ""  */
    sdr_csv_t csv;
    unsigned menu_state;            /* what the menus last showed      */
} sdr_t;

/* tiku_sdr_model.c */
/** @brief Live at 2437 MHz, 256 bins, peaks and averages on. */
void sdr_model_init(sdr_model_t *m);
/** @brief Switch to @p mode (Live at @p centre, or Band), the trace and the
 *         waterfall started afresh. */
void sdr_model_mode(sdr_model_t *m, int mode, int centre);
/** @brief Take one line from the board.  @return 1 when it finished a
 *         frame, 0 when it was kept or set aside, -1 when it was no SPEC */
int  sdr_model_line(sdr_model_t *m, const char *line);
/** @brief Peaks and the average restart from the latest trace. */
void sdr_model_clear(sdr_model_t *m);
/** @brief The frequency of @p bin's centre, in MHz. */
double sdr_model_freq(const sdr_model_t *m, int bin);
/** @brief The bin nearest @p mhz, or -1 outside the trace. */
int  sdr_model_bin(const sdr_model_t *m, double mhz);
/** @brief Follow the trace with the display's range, smoothly. */
void sdr_model_scale(sdr_model_t *m);
/** @brief The waterfall's colour at @p t, 0 to 1. */
tiku_rgb_t sdr_palette(float t);
/** @brief Which Wi-Fi, BLE and 802.15.4 channels hold @p mhz, as words. */
int  sdr_channels(double mhz, char *out, size_t max);
/** @brief The command that asks the board for the next frame. */
const char *sdr_command(const sdr_model_t *m, char *out, size_t max);

/* tiku_sdr_link.c */
/** @brief The mounted ESP32-C61s, the one part with the receiver, and the
 *         one meant when none is chosen: the desktop's current device if
 *         it is one, else the first.  @return 1 once the desktop has said
 *         which boards it has, 0 before -- then none is not yet none. */
int  sdr_targets(sdr_t *st);
/** @brief Ask the desktop to let go of the chosen board.  @return 0, or
 *         -1 with nothing done when no ESP32-C61 is mounted. */
int  sdr_connect(sdr_t *st);
/** @brief Tell the board to stop and give it back, over the next ticks. */
void sdr_disconnect(sdr_t *st);
/** @brief The same, now: the window is closing and there are no ticks. */
void sdr_disconnect_now(sdr_t *st);
/** @brief The desktop's answers about boards: let go, taken back, failed,
 *         or a new current device. */
void sdr_board_said(void *state, int what, const char *text);
/** @brief Deadlines, what the board said, and the next ask. */
void sdr_link_tick(sdr_t *st);

/* tiku_sdr_tape.c */
/** @brief A name to offer for a new file: the board and the time, then
 *         @p ext; a recording on screen lends its own name instead. */
void sdr_tape_name(const sdr_t *st, const char *ext, char *out, size_t max);
/** @brief Record to @p path from now, its header first.  @return 0, or -1
 *         with the reason in the note. */
int  sdr_rec_start(sdr_t *st, const char *path);
/** @brief One line from the board, timed; @p frame when it finished one. */
void sdr_rec_line(sdr_t *st, const char *line, int frame);
/** @brief The window's view, written when it changes. */
void sdr_rec_view(sdr_t *st);
/** @brief Close the recording and say what it became. */
void sdr_rec_stop(sdr_t *st);
/** @brief Play @p path from its start.  @return 0, or -1 with the reason
 *         in the note. */
int  sdr_play_start(sdr_t *st, const char *path);
/** @brief The lines whose time has come, unless paused. */
void sdr_play_tick(sdr_t *st);
/** @brief Close the recording being played. */
void sdr_play_stop(sdr_t *st);
/** @brief @p path's last part, the file's own name. */
const char *sdr_tape_leaf(const char *path);
/** @brief @p seconds as "m:ss", or "h:mm:ss" past an hour. */
void sdr_tape_clock(double seconds, char *out, size_t max);
/** @brief @p bytes as "840 KB" or "12.3 MB". */
void sdr_tape_size(long bytes, char *out, size_t max);
/** @brief Spectra as CSV rows of time, frequency and level at @p path: the
 *         whole recording on screen, a slice each tick, or else the
 *         spectra the waterfall holds, at once.  @return 0, or -1 with the
 *         reason in the note. */
int  sdr_csv_start(sdr_t *st, const char *path);
/** @brief The conversion's next slice; the note says how far it is. */
void sdr_csv_tick(sdr_t *st);
/** @brief A conversion abandoned: the window is closing. */
void sdr_csv_stop(sdr_t *st);

/* tiku_sdr_view.c */
/** @brief Where the spectrum and the waterfall go in the window. */
void sdr_layout(const sdr_t *st, tiku_rect_t *plot, tiku_rect_t *fall);
/** @brief Where the alert goes: centred, in the window's upper third. */
tiku_rect_t sdr_alert_rect(const sdr_t *st);
/** @brief The toolbar's buttons, registered for input and paint alike. */
void sdr_controls(sdr_t *st);
/** @brief The whole window, handed to the desktop. */
void sdr_paint(sdr_t *st);
/** @brief The words for @p command, on a button or in a menu. */
const char *sdr_label(const sdr_t *st, int command);
/** @brief Whether @p command can be used now: a recording being played
 *         says what is shown, only a running board records, and an alert
 *         holds everything but Quit. */
int  sdr_enabled(const sdr_t *st, int command);
/** @brief The window below its toolbar, as a PNG at @p path.  @return 0,
 *         or -1 with the reason in the note. */
int  sdr_picture(sdr_t *st, const char *path);

#endif /* TIKU_SDR_H_ */
