/* tiku_sdr.h -- TikuSDR: a board's radio receiver as a spectrum analyser.
 * The board (an ESP32-C61 with the receiver built in) computes each
 * spectrum itself and sends one line; this window keeps and stitches them,
 * shows them as traces on a graticule under a spectrogram, puts markers on
 * their peaks, records them to a file it can play back, and exports them
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
#include "tiku_choice.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_gfx.h"

#define SDR_W        940
#define SDR_H        600
#define SDR_SPLASH_W 560            /* the splash and the About box,   */
#define SDR_SPLASH_H 300            /* TikuBasic's size                */
#define SDR_SPLASH_US 2500000       /* how long the splash stands      */
#define SDR_BINS     512            /* the most one trace holds        */
#define SDR_ROWS     480            /* spectrogram rows remembered     */
#define SDR_RX_MAX   8192           /* bytes waiting for a newline     */
#define SDR_PATH     512            /* a recording's path              */
#define SDR_LINE     2048           /* a recording's longest line      */
#define SDR_MARKERS  4              /* markers at once                 */
#define SDR_TRACES   3              /* traces at once                  */
#define SDR_EXCURSION 6.0f          /* how far a peak rises, in dB     */
#define SDR_DIVS     10             /* graticule divisions, each way   */

/* A view is a range, start to stop in MHz.  The board tunes whole MHz from
 * 2200 to 2700.  One spectrum holds up to 24 MHz, all a 40 MS/s capture
 * keeps flat (beyond it, 15 to 30 dB down); a wider view is swept in 40
 * MS/s slices 16 MHz apart.  Live and Band are two ranges: 24 MHz about a
 * centre, and the 2.4 GHz band.  A recording of version 1 held up to 40. */
#define SDR_TUNE_LO      2200.0
#define SDR_TUNE_HI      2700.0
#define SDR_SPAN_MIN     1.0
#define SDR_LIVE_SPAN    24.0
#define SDR_ONE_V1       40.0
#define SDR_BAND_START   2396.0
#define SDR_BAND_STOP    2492.0
#define SDR_SLICE_STEP   16

/* The panel's fields, in its order: the view's range, the spectra an
 * average spans, and the selected marker's frequency. */
enum { SDR_F_CENTER, SDR_F_SPAN, SDR_F_START, SDR_F_STOP, SDR_F_AVG,
       SDR_F_MARK, SDR_FIELDS };

/* What a trace shows, as an analyser's trace types. */
enum { SDR_T_OFF, SDR_T_WRITE, SDR_T_AVG, SDR_T_MAX, SDR_T_MIN, SDR_T_TYPES };

/* The panel's drop-down lists: the trace being set and its type, the
 * marker being set and the trace it stands on. */
enum { SDR_C_TRACE, SDR_C_TYPE, SDR_C_MARKER, SDR_C_ON, SDR_CHOICES };

enum {
    SDR_IDLE, SDR_RELEASING, SDR_OPENING, SDR_STARTING, SDR_RUNNING,
    SDR_STOPPING, SDR_RECLAIMING
};

/* Commands: the panel's buttons, menus and keys share them. */
enum {
    CMD_CONNECT = 1,                /* the SDR menu's alone             */
    CMD_LIVE,                       /* Frequency: live, or the wide view */
    CMD_CLEAR, CMD_EXPORT,          /* Traces                           */
    CMD_MARK_ACTIVE, CMD_TRACK,     /* Markers: its two checkboxes      */
    CMD_PEAK_SEARCH, CMD_NEXT_PEAK, CMD_PEAK_LEFT, CMD_PEAK_RIGHT,
    CMD_MARK_CENTER, CMD_MARK_CLEAR,            /* and its buttons      */
    CMD_WATERFALL, CMD_PLANS,       /* Display: its two switches        */
    CMD_RECORD, CMD_PAUSE,          /* the bar at the window's foot     */
    CMD_QUIT, CMD_REPLAY, CMD_PICTURE, CMD_PEAK, CMD_OVERLAY,
    CMD_MARK_REMOVE, CMD_ABOUT,     /* menus and keys alone             */
    CMD_TYPE = 60,                  /* + SDR_T_*: the selected trace's  */
    CMD_TRACE_SEL = 70,             /* + trace: the one being set       */
    CMD_MARK_SEL = 80,              /* + marker: the one being set      */
    CMD_TUNE = 100,                 /* + MHz offset from 2400: Tune menu */
    CMD_CHOICE = 240,               /* + SDR_C_*: the drop-down lists   */
    CMD_FIELD = 250,                /* + SDR_F_*: the fields            */
    CMD_BOARD = 300                 /* + index: Device menu             */
};

/* Where marker peak moves go: the highest peak, the highest below the
 * marker, or the nearest to its left or right. */
enum { SDR_TO_HIGHEST, SDR_TO_NEXT, SDR_TO_LEFT, SDR_TO_RIGHT };

/** @brief A marker: whether it is out, the trace it reads, and where it
 *         stands as a frequency, so a new view keeps it in place. */
typedef struct {
    int    on;
    int    trace;                   /* 0 to SDR_TRACES - 1             */
    double mhz;                     /* a bin's centre                  */
} sdr_marker_t;

/** @brief What is on screen: the latest spectrum, what each trace type
 *         keeps of them, the markers, the scale, and the spectrogram's
 *         history, every value in decibels. */
typedef struct {
    double start, stop;             /* the view, MHz                   */
    double one_max;                 /* the widest view one spectrum shows */
    int    sweep;                   /* swept in slices, or one spectrum */
    int    tune, rate, nfft;        /* tuned (MHz), rate code, bins    */
    int    k0;                      /* one spectrum: its first bin shown */
    int    first, last;             /* a sweep: its slices' centres    */
    uint64_t got;                   /* a sweep: slices heard since SWEEP */
    int    gain;                    /* the latest line's gain index    */
    int    held;                    /* ... held by the board, not the AGC's */
    int    n;                       /* bins in the trace               */
    double lo_mhz, bin_mhz;         /* bin 0's centre, the spacing     */
    float  db[SDR_BINS], avg[SDR_BINS], peak[SDR_BINS], low[SDR_BINS];
    unsigned char seen[SDR_BINS];   /* a sweep: bins heard this sweep  */
    int    fresh;
    int    type[SDR_TRACES];        /* each trace's SDR_T_*            */
    int    avg_count;               /* spectra an average spans        */
    sdr_marker_t mark[SDR_MARKERS];
    int    mark_sel;                /* the one the panel and keys set  */
    int    mark_held;               /* the one the pointer drags, or -1 */
    int    track;                   /* markers follow the peak they are on */
    float  scale_lo, scale_hi;      /* the graticule's foot and top, dB */
    float  div;                     /* dB a division                   */
    float  floor_db;                /* the spectrogram's foot: just under
                                       the noise, in 5 dB steps        */
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
    tiku_field_t field[SDR_FIELDS]; /* typed in while focused, else what
                                       each says of the view */
    tiku_field_input_t input[SDR_FIELDS];
    tiku_choice_t choice[SDR_CHOICES];  /* the panel's drop-down lists */
    int trace_sel;                  /* the trace the panel sets        */
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
    int fall_on;                    /* the spectrogram shown           */
    double live_view[2], wide_view[2];  /* the last view of each kind,
                                           start and stop: Live swaps them */
    int plans_on;                   /* the band's channel plans shown  */
    int64_t painted_at, names_at;
    uint32_t *pic;                  /* the spectrogram's bitmap        */
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
    /* The splash, alone while TikuSDR arrives (id is 0 until it leaves),
     * and the About box: the same card, standing until it is read. */
    tiku_surface_t *splash, *about;
    uint32_t splash_id, about_id;
    int64_t splash_from;
    int splash_stage;
} sdr_t;

/* tiku_sdr_model.c */
/** @brief Live at 2437 MHz, 256 bins; trace 1 the average of four
 *         spectra, the others off; no marker out, each following peaks. */
void sdr_model_init(sdr_model_t *m);
/** @brief @p start and @p stop made a view the board can give: in order,
 *         at least 1 MHz apart, inside 2200 to 2700 MHz (moved, not cut,
 *         where they fit), to the kHz. */
void sdr_range_fit(double *start, double *stop);
/** @brief Show @p start to @p stop, fitted: one spectrum at the narrowest
 *         rate that holds it up to one_max MHz, a sweep of slices beyond;
 *         the traces and the spectrogram start afresh. */
void sdr_model_range(sdr_model_t *m, double start, double stop);
/** @brief Whether the view is live: one spectrum caught at once, up to
 *         one_max MHz wide, rather than a sweep of slices. */
int  sdr_model_is_live(const sdr_model_t *m);
/** @brief @p mhz, at least 0, as a field shows it: to the kHz, with no
 *         trailing zeros and a point whatever the locale. */
void sdr_mhz_text(double mhz, char *out, size_t max);
/** @brief Field @p field (SDR_F_*) as its text: the view's range in MHz,
 *         the average's count, the selected marker's MHz ("" when in). */
void sdr_model_field(const sdr_model_t *m, int field, char *out, size_t max);
/** @brief A frequency as typed: MHz, or k, M or G with an optional Hz
 *         after it, any case, the point always '.'.  @return 1 with the
 *         MHz in @p mhz, or 0 for what is not one. */
int  sdr_mhz_parse(const char *text, double *mhz);
/** @brief Take one line from the board.  @return 1 when it finished a
 *         frame, 0 when it was kept or set aside, -1 when it was no SPEC */
int  sdr_model_line(sdr_model_t *m, const char *line);
/** @brief Every trace starts again from the latest spectrum. */
void sdr_model_clear(sdr_model_t *m);
/** @brief What a trace of type @p type (SDR_T_*) shows, or NULL for Off. */
const float *sdr_trace(const sdr_model_t *m, int type);
/** @brief The frequency of @p bin's centre, in MHz. */
double sdr_model_freq(const sdr_model_t *m, int bin);
/** @brief The bin nearest @p mhz, or -1 outside the trace. */
int  sdr_model_bin(const sdr_model_t *m, double mhz);
/** @brief The graticule's top and dB a division: whole tens over the
 *         traces shown, 2, 5, 10 or 20 dB a division down ten of them,
 *         moved only when the traces leave the room it gives them. */
void sdr_model_scale(sdr_model_t *m);
/** @brief The spectrogram's colour at @p t, 0 to 1: blue through green
 *         and yellow to red. */
tiku_rgb_t sdr_palette(float t);
/** @brief Which Wi-Fi, BLE and 802.15.4 channels hold @p mhz, as words. */
int  sdr_channels(double mhz, char *out, size_t max);
/** @brief The command that asks the board for the next frame. */
const char *sdr_command(const sdr_model_t *m, char *out, size_t max);

/* tiku_sdr_marker.c */
/** @brief What marker @p k reads: its trace's values, or the latest
 *         spectrum while that trace is off. */
const float *sdr_mark_trace(const sdr_model_t *m, int k);
/** @brief The peaks of @p v, left to right, at most @p max: each rises
 *         SDR_EXCURSION dB over the lowest ground between it and anything
 *         higher on either side; a flat top counts once, at its middle. */
int  sdr_peaks(const sdr_model_t *m, const float *v, int *bins, int max);
/** @brief Marker @p k moved by @p how (SDR_TO_*), a placed one from where
 *         it stands.  @return 1 when it moved, 0 when no peak was there. */
int  sdr_mark_peak(sdr_model_t *m, int k, int how);
/** @brief Marker @p k out on the highest peak no other marker stands on,
 *         or the view's middle with none free or no spectrum yet. */
void sdr_mark_on(sdr_model_t *m, int k);
/** @brief Marker @p k taken in, and let go if the pointer held it. */
void sdr_mark_off(sdr_model_t *m, int k);
/** @brief Whether any marker is out. */
int  sdr_marks_out(const sdr_model_t *m);
/** @brief Marker @p k on the bin nearest @p mhz in the view, and with
 *         @p snap while markers follow peaks, on top of the peak there. */
void sdr_mark_at(sdr_model_t *m, int k, double mhz, int snap);
/** @brief After a frame: markers that follow peaks, to their tops. */
void sdr_mark_follow(sdr_model_t *m);
/** @brief The bin marker @p k stands on, or -1 when it is outside the
 *         view or not placed. */
int  sdr_mark_bin(const sdr_model_t *m, int k);
/** @brief Marker @p k's readout: frequency and level, and after the
 *         first, how far it is from M1 in both. */
void sdr_mark_text(const sdr_model_t *m, int k, char *out, size_t max);

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

/* tiku_sdr_app.c */
/** @brief The window, its menus and its first frame.  @return 0, or -1
 *         when the desktop gave none. */
int  sdr_window_open(sdr_t *st);

/* tiku_sdr_splash.c */
/** @brief The splash, alone: TikuSDR's name and what is being gathered.
 *         @return 0, or -1 when it could not be put up. */
int  sdr_splash_open(sdr_t *st);
/** @brief The splash's status moved on to @p now, and the splash let go
 *         once it has stood its time. */
void sdr_splash_tick(sdr_t *st, int64_t now);
/** @brief The splash gone, whoever asked, and the window open after it. */
void sdr_splash_drop(sdr_t *st);
/** @brief The About box, or the one up brought forward. */
void sdr_about_open(sdr_t *st);
/** @brief The About box put away. */
void sdr_about_drop(sdr_t *st);

/* tiku_sdr_view.c */
/** @brief Where the traces go -- the graticule -- and the spectrogram
 *         above it, of no height while it is off. */
void sdr_layout(const sdr_t *st, tiku_rect_t *plot, tiku_rect_t *fall);
/** @brief Where the alert goes: centred, in the window's upper third. */
tiku_rect_t sdr_alert_rect(const sdr_t *st);
/** @brief Where the panel's control @p id is: a CMD_* button, checkbox or
 *         switch, CMD_FIELD + SDR_F_* or CMD_CHOICE + SDR_C_*; empty for
 *         anything else. */
tiku_rect_t sdr_control_rect(const sdr_t *st, int id);
/** @brief The panel's controls, registered for input and paint alike,
 *         its lists showing what they set unless one is open. */
void sdr_controls(sdr_t *st);
/** @brief The whole window, handed to the desktop. */
void sdr_paint(sdr_t *st);
/** @brief The words for @p command, on a button or in a menu; @p st may be
 *         NULL for any but Connect, Pause and Record, whose words change. */
const char *sdr_label(const sdr_t *st, int command);
/** @brief Whether @p command can be used now: a recording being played
 *         says what is shown, only a running board records, Connect wants
 *         a radio there, marker moves want one out over a spectrum, and
 *         an alert holds everything but Quit. */
int  sdr_enabled(const sdr_t *st, int command);
/** @brief The spectrogram and the spectrum, with their annotations, as a
 *         PNG at @p path.  @return 0, or -1 with the reason in the note. */
int  sdr_picture(sdr_t *st, const char *path);

#endif /* TIKU_SDR_H_ */
