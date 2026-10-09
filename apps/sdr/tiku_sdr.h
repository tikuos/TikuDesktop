/* tiku_sdr.h -- TikuSDR's shared types and functions.  TikuSDR shows a
 * board's radio receiver as a spectrum analyser: an ESP32-C5 or ESP32-C61
 * whose firmware has the receiver, computes each spectrum itself and sends
 * it as one line; TikuSDR stitches a sweep's slices into one trace, shows
 * the spectra as traces on a graticule under a spectrogram, puts markers
 * on their peaks, records the lines to a file it can play back, and
 * exports spectra as CSV or a picture.
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
#define SDR_SPLASH_W 560            /* the size of the splash and of   */
#define SDR_SPLASH_H 300            /* the About box                   */
#define SDR_SPLASH_US 2500000       /* the splash's time on screen, us */
#define SDR_BINS     512            /* the most one trace holds        */
#define SDR_ROWS     480            /* spectrogram rows remembered     */
#define SDR_RX_MAX   8192           /* bytes waiting for a newline     */
#define SDR_PATH     512            /* room for a file's path          */
#define SDR_LINE     2048           /* a recording's longest line      */
#define SDR_MARKERS  4              /* markers at once                 */
#define SDR_TRACES   3              /* traces at once                  */
#define SDR_EXCURSION 6.0f          /* the least rise a peak needs, dB */
#define SDR_DIVS     10             /* graticule divisions, each way   */
#define SDR_VIEWS    16             /* prior tuning ranges remembered */
#define SDR_SLICES_MAX 64           /* a sweep's slices: a bit each in got */
#define SDR_RBW_MAX  16             /* resolutions the RBW list offers */
/* The board windows each FFT with Hann, whose response is 1.44 bins wide
 * at -3 dB: the resolution bandwidth is that many bins. */
#define SDR_HANN_RBW 1.44

/* A view runs from start to stop in MHz, inside SDR_TUNE_LO to
 * SDR_TUNE_HI; the board tunes in whole MHz.  A view up to SDR_LIVE_SPAN
 * wide is one spectrum, since a 40 MS/s capture is flat over its middle
 * 24 MHz only; a wider one is swept in 40 MS/s slices SDR_SLICE_STEP MHz
 * apart.  In a sweep, a channel tuned or a point clicked is shown
 * SDR_LIVE_SPAN wide, and SDR_BAND_START to SDR_BAND_STOP, the 2.4 GHz
 * band, is the first wide view.  A version 1 recording plays views up to
 * SDR_ONE_V1 MHz wide as one spectrum. */
#define SDR_TUNE_LO      2200.0
#define SDR_TUNE_HI      5900.0
#define SDR_SPAN_MIN     1.0
#define SDR_LIVE_SPAN    24.0
#define SDR_ONE_V1       40.0
#define SDR_BAND_START   2396.0
#define SDR_BAND_STOP    2492.0
#define SDR_SLICE_STEP   16

/* The panel's fields, in its order: the view's range, the spectra an
 * average spans, and the selected marker's frequency. */
enum { SDR_F_CENTER, SDR_F_SPAN, SDR_F_START, SDR_F_STOP, SDR_F_AVG,
       SDR_F_MARK, SDR_F_REF, SDR_F_DIV, SDR_F_FLOOR, SDR_F_CEILING,
       SDR_F_GAIN, SDR_F_THRESHOLD, SDR_F_PRE, SDR_F_POST, SDR_F_BOOK,
       SDR_FIELDS };

enum { SDR_S_FREQ, SDR_S_TRACE, SDR_S_MARK, SDR_S_DISPLAY, SDR_S_RECEIVER,
       SDR_S_MEASURE,
       SDR_SECTIONS };

/* What a trace shows: nothing, the latest spectrum, the average, or the
 * highest or lowest level held at each bin. */
enum { SDR_T_OFF, SDR_T_WRITE, SDR_T_AVG, SDR_T_MAX, SDR_T_MIN, SDR_T_TYPES };

/* The panel's drop-down lists: the trace being set and its type, the
 * marker being set and the trace it reads, and the resolution bandwidth. */
enum { SDR_C_TRACE, SDR_C_TYPE, SDR_C_MARKER, SDR_C_ON, SDR_C_RBW,
       SDR_CHOICES };

/* The link's states: idle; waiting for the desktop to let go of the board;
 * the port open, waiting for a prompt; the receiver starting; running;
 * stopping; waiting for the desktop to take the board back. */
enum {
    SDR_IDLE, SDR_RELEASING, SDR_OPENING, SDR_STARTING, SDR_RUNNING,
    SDR_STOPPING, SDR_RECLAIMING
};

/* Commands: the panel's buttons, menus and keys share them. */
enum {
    CMD_CONNECT = 1,                /* SDR menu and transport bar       */
    CMD_LIVE,                       /* Frequency: live, or the wide view */
    CMD_CLEAR, CMD_EXPORT,          /* Traces                           */
    CMD_MARK_ACTIVE, CMD_TRACK,     /* Markers: its two checkboxes      */
    CMD_PEAK_SEARCH, CMD_NEXT_PEAK, CMD_PEAK_LEFT, CMD_PEAK_RIGHT,
    CMD_MARK_CENTER, CMD_MARK_CLEAR,            /* and its buttons      */
    CMD_WATERFALL,                  /* Display: its switch              */
    CMD_RECORD, CMD_PAUSE,          /* the bar at the window's foot     */
    CMD_QUIT, CMD_REPLAY, CMD_PICTURE, CMD_PEAK, CMD_OVERLAY,
    CMD_MARK_REMOVE, CMD_ABOUT,     /* in menus or keys only            */
    CMD_SCALE_AUTO, CMD_COLOR_AUTO, CMD_GAIN_AUTO, CMD_GAIN_READ,
    CMD_REWIND, CMD_FORWARD, CMD_STEP, CMD_SPEED, CMD_GAPS,
    CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_VIEW_BACK,
    CMD_HISTORY_OLDER, CMD_HISTORY_NEWER, CMD_HISTORY_LATEST,
    CMD_TYPE = 60,                  /* + SDR_T_*: the selected trace's type */
    CMD_TRACE_SEL = 70,             /* + trace: the one being set       */
    CMD_MARK_SEL = 80,              /* + marker: the one being set      */
    CMD_TUNE = 100,                 /* + MHz offset from 2400: Tune menu */
    CMD_CHOICE = 240,               /* + SDR_C_*: the drop-down lists   */
    CMD_FIELD = 250,                /* + SDR_F_*: the fields            */
    CMD_BOARD = 300,                /* + index: Device menu             */
    CMD_SECTION = 400,              /* + SDR_S_*: collapsible sections */
    CMD_PRESET_SAVE = 500, CMD_PRESET_OPEN, CMD_REF_SAVE, CMD_REF_OPEN,
    CMD_REF_SHOW, CMD_OCCUPANCY, CMD_OCC_PLAN, CMD_OCC_RESET, CMD_OCC_EXPORT,
    CMD_TRIGGER, CMD_CAPS, CMD_FFT, CMD_RATE, CMD_BOOK_ADD, CMD_BOOK_NEXT,
    CMD_BOOK_CLEAR, CMD_LOOP_A, CMD_LOOP_B, CMD_LOOP, CMD_OCC_PAGE, CMD_HEALTH,
    CMD_RBW,
    CMD_TUNE5 = 1000                /* + MHz offset from 5000 */
};

/* Where marker peak moves go: the highest peak, the highest peak lower
 * than the marker, or the nearest peak to its left or right. */
enum { SDR_TO_HIGHEST, SDR_TO_NEXT, SDR_TO_LEFT, SDR_TO_RIGHT };

/** @brief A marker: whether it is on, the trace it reads, and its
 *         frequency, which keeps it in place when the view changes. */
typedef struct {
    int    on;
    int    trace;                   /* 0 to SDR_TRACES - 1             */
    double mhz;                     /* the centre of the bin it was put on */
} sdr_marker_t;

/** @brief The model: the view and how the board is asked for it, the
 *         latest spectrum, what each trace type keeps of the spectra, the
 *         markers, the graticule's scale and the spectrogram's rows; levels
 *         are in decibels. */
typedef struct {
    double start, stop;             /* the view, MHz                   */
    double one_max;                 /* the widest view one spectrum shows */
    int    sweep;                   /* 1: swept in slices; 0: one spectrum */
    int    tune, rate, nfft;        /* tuned (MHz), rate code, bins    */
    int    fft_override, rate_override; /* 0: auto; rate code + 1 otherwise */
    double rbw_bin_khz;             /* the bin spacing the chosen RBW needs,
                                       0 for automatic: each view picks
                                       the overrides that give it      */
    unsigned caps_rates, caps_ffts; /* what the board takes: a bit per rate
                                       code, and per 64, 128 and 256 points */
    int    acq_dropped;             /* a chosen RBW or rate this view could
                                       not have went back to automatic */
    unsigned rejected, incomplete; /* local parser counters, not RF loss */
    int    k0;                      /* one spectrum: its first bin shown */
    int    first, last;             /* a sweep: its first and last
                                       slices' centres, MHz            */
    int    slice_step;              /* a sweep: MHz between slices     */
    double slice_half;              /* a sweep: MHz kept each side of a
                                       slice's centre                  */
    uint64_t got;                   /* a sweep: a bit for each slice heard
                                       since the last SWEEP line       */
    int    gain;                    /* the latest line's gain index    */
    int    held;                    /* 1 when the board held that gain,
                                       0 when its AGC set it           */
    int    n;                       /* bins in the trace               */
    double lo_mhz, bin_mhz;         /* bin 0's centre, the spacing     */
    float  db[SDR_BINS], avg[SDR_BINS], peak[SDR_BINS], low[SDR_BINS];
    unsigned char seen[SDR_BINS];   /* a sweep: bins heard this sweep  */
    int    fresh;                   /* a frame has finished in this view */
    int    type[SDR_TRACES];        /* each trace's SDR_T_*            */
    int    avg_count;               /* spectra an average spans: each
                                       frame weighs 1/avg_count        */
    sdr_marker_t mark[SDR_MARKERS];
    int    mark_sel;                /* the one the panel and keys set  */
    int    mark_held;               /* the one the pointer drags, or -1 */
    int    track;                   /* markers follow the peak they are on */
    float  scale_lo, scale_hi;      /* the graticule's foot and top, dB */
    float  div;                     /* dB a division                   */
    float  floor_db;                /* the spectrogram's lowest colour:
                                       under the noise, in 5 dB steps  */
    float  ceiling_db;              /* the spectrogram's highest colour */
    int    manual_scale, manual_color;
    unsigned char wf[SDR_ROWS][SDR_BINS];  /* half-dB, newest at head  */
    double wf_at[SDR_ROWS];         /* each row's time, in seconds     */
    double clock;                   /* seconds, given to a frame that
                                       finishes now                    */
    int    wf_head, wf_count;
    int    wf_offset, wf_selected;  /* ages in captured rows; -1: no selection */
    unsigned spectra;               /* frames finished since the board
                                       started or playing began        */
} sdr_model_t;

/** @brief A recording being made: the board's SPEC and SWEEP lines and
 *         the window's VIEW lines, each with its time. */
typedef struct {
    FILE    *file;                  /* open while recording            */
    char     path[SDR_PATH];
    int64_t  began;                 /* st->now at its start, us        */
    unsigned frames;                /* spectra and sweeps finished     */
    long     bytes;                 /* written so far                  */
    int      failed;                /* a write did not go through      */
} sdr_rec_t;

/** @brief A recording played back at the pace it was made, with long
 *         silences cut short. */
typedef struct {
    FILE    *file;                  /* open while playing              */
    char     path[SDR_PATH];
    char     line[SDR_LINE];        /* the next line, waiting its time */
    char     from[96];              /* its board and when, from its head */
    int      ready;                 /* line holds a line               */
    double   at, total;             /* line's time and the last, in s  */
    int64_t  clock, last;           /* played so far (us), and st->now
                                       at the last tick                */
    int      skip_gaps, seeking, stepping;
    double   speed, seek_to, initial_start, initial_stop;
    int loop, resume_seek;
    double loop_a, loop_b;
} sdr_play_t;

#define SDR_CHANNELS 69             /* Wi-Fi 13, BLE 40, 802.15.4 16 */
#define SDR_PRE_LINES 256           /* bounded pre-trigger protocol history */
#define SDR_BOOKS 32

/** @brief A reference retains its frequency grid and receiver metadata. */
typedef struct {
    int n, rate, nfft, gain, held, visible, valid;
    double lo, bin;
    float db[SDR_BINS];
    char name[96], board[64];
} sdr_reference_t;

/** @brief Measurement counts are per completed observation, not airtime. */
typedef struct {
    uint64_t seen[SDR_CHANNELS], busy[SDR_CHANNELS];
    double first[SDR_CHANNELS], last[SDR_CHANNELS];
    double start, stop;
    int rate, nfft, gain, held, initialized;
    float threshold;
    int visible, plan, page;
} sdr_occupancy_t;

/** @brief One-shot trigger: the destination is opened when armed. */
typedef struct {
    int armed, fired, count, head;
    int pre_seconds, post_seconds;
    int64_t fired_at;
    struct { int64_t at; int frame; char text[SDR_LINE]; } lines[SDR_PRE_LINES];
} sdr_trigger_t;

typedef struct {
    double at;
    char label[64];
} sdr_bookmark_t;

/** @brief An export to CSV: a recording on screen is converted over
 *         several ticks. */
typedef struct {
    FILE    *in, *out;              /* open while converting           */
    sdr_model_t *model;             /* the model the recording plays into */
    char     path[SDR_PATH];        /* the CSV being written           */
    char     line[SDR_LINE];
    long     size, rows;            /* the recording's bytes; rows out */
    unsigned frames;                /* spectra written                 */
} sdr_csv_t;

/** @brief TikuSDR's state: the window and its controls, the model, the
 *         link to the board, recordings, and the splash and About box. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface;
    uint32_t id;
    int w, h;
    tiku_dialog_t dialog;
    tiku_field_t field[SDR_FIELDS]; /* the text typed in a focused
                                       field; an unfocused one shows
                                       the model's value               */
    tiku_field_input_t input[SDR_FIELDS];
    tiku_choice_t choice[SDR_CHOICES];  /* the panel's drop-down lists */
    int trace_sel;                  /* the trace the panel sets        */
    tiku_alert_t alert;             /* the alert that no receiver is
                                       connected                       */
    sdr_model_t model;
    /* The chosen board, its port and its MCU; names lists the mounted
     * supported ESP32 receivers, one a line. */
    char board[64], port[160], mcu[48], names[1024];
    int looking;                    /* the first look at the boards is
                                       due                             */
    int state, fd, paused, pending, closing, stopped;
    int gain_known, gain_value, gain_queued, gain_inflight, gain_answered;
    int gain_request;               /* -2 queries; -1 automatic; 0..255 held */
    int caps_queued, caps_inflight, caps_known;
    unsigned caps_rates, caps_ffts;
    int caps_lo, caps_hi;
    int caps_band_count, caps_band_lo[4], caps_band_hi[4];
    double rbw_list[SDR_RBW_MAX];   /* the RBW list's bin spacings, kHz,
                                       after its Auto row              */
    int rbw_count;
    int64_t last_frame;
    unsigned rx_overflows;
    int health_visible;
    sdr_reference_t reference;
    sdr_occupancy_t occupancy;
    sdr_trigger_t trigger;
    sdr_bookmark_t books[SDR_BOOKS];
    int book_count;
    char book_label[64];
    int64_t now, deadline, sent_at, rate_at;
    unsigned rate_count;
    float rate_hz;
    char rx[SDR_RX_MAX];
    size_t rx_n;
    char note[160], hover[160];
    int hover_x, hover_y, hover_valid;
    int dirty;
    int fall_on;                    /* the spectrogram shown           */
    unsigned collapsed;            /* one bit per SDR_S_* section */
    int fall_percent, divider_drag;
    double previous[SDR_VIEWS][2];
    int previous_count;
    int nav_drag, nav_anchor, nav_x; /* 1: range selection; 2: pan */
    double nav_start, nav_stop;     /* range when the gesture began */
    double live_view[2], wide_view[2];  /* the last view of each kind,
                                           start and stop: Live shows the
                                           other kind's                */
    int64_t painted_at, names_at;
    uint32_t *pic;                  /* the spectrogram's bitmap        */
    size_t pic_n;
    /* Recordings. */
    sdr_rec_t rec;
    sdr_play_t play;
    int asking;                     /* the command the file panel is up
                                       for, or 0                       */
    char tape_dir[SDR_PATH];        /* the last file picked's folder   */
    char saved[200];                /* what the last recording became  */
    char shown[SDR_PATH];           /* the recording on screen, or ""  */
    sdr_csv_t csv;
    unsigned menu_state;            /* what the menus last showed      */
    /* The splash, shown alone at start (id is 0 until it closes), and the
     * About box, the same card, which stays until it is dismissed. */
    tiku_surface_t *splash, *about;
    uint32_t splash_id, about_id;
    int64_t splash_from;
    int splash_stage;
} sdr_t;

/* Saved workspace and measurement tools. Files are versioned text. */
/** @brief Atomically save a small text document using locale-independent numbers. */
int sdr_document_save(const char *path, int (*write_doc)(FILE *, void *), void *ctx);
/** @brief Save display preferences and range; do not store receiver commands. */
int sdr_setup_save(sdr_t *st, const char *path);
/** @brief Validate an entire setup before applying any of its settings. */
int sdr_setup_load(sdr_t *st, const char *path);
/** @brief Load or save the per-user setup under .config/TikuDesktop. */
void sdr_preferences(sdr_t *st, int save);
/** @brief Save the selected trace with its frequency grid and receiver metadata. */
int sdr_reference_save(sdr_t *st, const char *path);
/** @brief Load a validated reference, keeping the old one on failure. */
int sdr_reference_load(sdr_t *st, const char *path);
/** @brief Whether the reference can be overlaid without changing its grid. */
int sdr_reference_matches(const sdr_t *st);
/** @brief Read bookmarks and loop endpoints from the recording's sidecar. */
void sdr_books_load(sdr_t *st);
/** @brief Atomically save annotations without modifying the recording. */
int sdr_books_save(sdr_t *st);
/** @brief Bookmark the playback position with the label in Measurements. */
void sdr_book_add(sdr_t *st);
/** @brief Seek to the next bookmark, wrapping to the earliest one. */
void sdr_book_next(sdr_t *st);
/** @brief Initialize measurement and trigger defaults. */
void sdr_tools_init(sdr_t *st);
/** @brief Count fully covered channels after one complete spectrum or sweep. */
void sdr_observe(sdr_t *st);
/** @brief Clear observations without changing the threshold or selected plan. */
void sdr_occupancy_reset(sdr_t *st);
/** @brief Describe channel 0..68 across Wi-Fi, BLE and 802.15.4 plans. */
void sdr_channel(int index, char *name, size_t size, double *center, double *width);
/** @brief Export channel activity counts and observation spans as CSV. */
int sdr_occupancy_export(sdr_t *st, const char *path);
/** @brief Open the destination now, then arm a one-shot threshold trigger. */
int sdr_trigger_arm(sdr_t *st, const char *path);
/** @brief Buffer pre-trigger lines or append them to the active recording. */
void sdr_trigger_line(sdr_t *st, const char *line, int frame);
/** @brief Disarm or finish a triggered recording; ordinary recordings remain open. */
void sdr_trigger_cancel(sdr_t *st);
/** @brief Stop a triggered recording after its post-trigger deadline. */
void sdr_trigger_tick(sdr_t *st);
/** @brief Parse supported rates, FFT sizes and tuning limits from sdr info. */
int sdr_caps_parse(sdr_t *st, const char *line);
/** @brief Parse up to four ordered, disjoint ranges from sdr bands. */
int sdr_bands_parse(sdr_t *st, const char *line);
/** @brief Whether one reported band contains the complete tuning interval. */
int sdr_tuning_valid(const sdr_t *st, int first, int last);
/** @brief Cycle through supported FFT sizes or rates that fit the current view. */
void sdr_acquisition_cycle(sdr_t *st, int fft);
/** @brief Whether the requested acquisition fits reported receiver capabilities. */
int sdr_acquisition_valid(const sdr_t *st);
/** @brief Choose the resolution bandwidth by the bin spacing that gives it,
 *         @p bin_khz, or 0 for automatic; the view keeps its range. */
void sdr_rbw_choose(sdr_t *st, double bin_khz);
/** @brief Step the RBW to the next the RBW list offers, after the last back
 *         to automatic. */
void sdr_rbw_cycle(sdr_t *st);
/** @brief Explain local capture health without inventing firmware counters. */
void sdr_health_text(const sdr_t *st, char *out, size_t size);

/* tiku_sdr_navigation.c */
/** @brief Change the acquisition range; optionally remember its predecessor. */
void sdr_navigate(sdr_t *st, double start, double stop, int remember);
/** @brief Zoom around a fractional position in the plot (0 left, 1 right). */
void sdr_zoom(sdr_t *st, double fraction, double factor);
/** @brief Restore the most recently saved acquisition range. */
void sdr_view_back(sdr_t *st);
/** @brief Handle zoom, selection, pan, and waterfall history gestures. */
int sdr_navigation_event(sdr_t *st, const tiku_event_t *event);
/** @brief Ring index for a row's age, or -1 outside retained history. */
int sdr_history_row(const sdr_model_t *model, int age);
/** @brief Move the waterfall viewport by captured rows, clamped to history. */
void sdr_history_scroll(sdr_model_t *model, int rows);

/* tiku_sdr_model.c */
/** @brief Set @p m to its opening state: 24 MHz about 2437 MHz in
 *         256-point spectra; trace 1 the average of four spectra, the
 *         others off; no marker on, and markers set to follow peaks. */
void sdr_model_init(sdr_model_t *m);
/** @brief Fit an ordered view, at least 1 MHz wide, inside 2200–2700 or
 *         4900–5900 MHz. A lower endpoint at or above 3800 selects the
 *         upper band; firmware capabilities separately gate acquisition. */
void sdr_range_fit(double *start, double *stop);
/** @brief Set the view to @p start to @p stop, fitted: one spectrum at the
 *         narrowest rate that holds it up to one_max MHz, a sweep of slices
 *         beyond; the traces and the spectrogram start afresh. */
void sdr_model_range(sdr_model_t *m, double start, double stop);
/** @brief Whether the view is live: one spectrum caught at once, up to
 *         one_max MHz wide, rather than a sweep of slices. */
int  sdr_model_is_live(const sdr_model_t *m);
/** @brief The sample rate rate code @p code selects, in MS/s. */
double sdr_rate_mhz(int code);
/** @brief The spacing of the board's FFT bins in this view's acquisition,
 *         kHz; SDR_HANN_RBW times it is the resolution bandwidth. */
double sdr_model_bin_khz(const sdr_model_t *m);
/** @brief The bin spacing, kHz, the view has with no RBW, rate or size
 *         chosen. */
double sdr_model_auto_bin_khz(const sdr_model_t *m);
/** @brief How many slices the view's sweep takes; 1 for one spectrum. */
int  sdr_model_slices(const sdr_model_t *m);
/** @brief The rate code and FFT size that give the view bins @p bin_khz
 *         apart: one capture, at the narrowest rate that holds the view,
 *         else the sweep that sends the fewest bins, then the fewest slices,
 *         of the rates and sizes the board takes.  @return 0, or -1 when
 *         the view cannot have them. */
int  sdr_model_rbw_plan(const sdr_model_t *m, double bin_khz, int *rate,
                        int *nfft);
/** @brief The bin spacings, in kHz, coarsest first, that the view can be
 *         given at least ten bins across.  @return how many, at most @p max. */
int  sdr_model_rbw_options(const sdr_model_t *m, double *bin_khz, int max);
/** @brief Write @p mhz, which must be at least 0, as a field shows it: to
 *         the kHz, with no trailing zeros and '.' as the point in every
 *         locale. */
void sdr_mhz_text(double mhz, char *out, size_t max);
/** @brief Write the text of field @p field (SDR_F_*): the view's centre,
 *         span, start or stop in MHz, the average's count, or the selected
 *         marker's MHz, empty while that marker is off. */
void sdr_model_field(const sdr_model_t *m, int field, char *out, size_t max);
/** @brief Read a frequency as typed: a number of MHz, or a number with k,
 *         M or G after it for kHz, MHz or GHz, which Hz may follow; any
 *         case, the point always '.'.  @return 1 with the MHz in @p mhz,
 *         or 0 for text that is not a frequency. */
int  sdr_mhz_parse(const char *text, double *mhz);
/** @brief Take one line from the board: a SPEC line's spectrum or slice,
 *         or a SWEEP line that ends a sweep.  @return 1 when it finished a
 *         frame; 0 when it was kept, or dropped as belonging to another
 *         view or to a sweep with a slice missing; -1 when it is neither
 *         SWEEP nor a well-formed SPEC line. */
int  sdr_model_line(sdr_model_t *m, const char *line);
/** @brief Every trace starts again from the latest spectrum. */
void sdr_model_clear(sdr_model_t *m);
/** @brief What a trace of type @p type (SDR_T_*) shows, or NULL for Off. */
const float *sdr_trace(const sdr_model_t *m, int type);
/** @brief The frequency of @p bin's centre, in MHz. */
double sdr_model_freq(const sdr_model_t *m, int bin);
/** @brief The bin nearest @p mhz, or -1 outside the trace. */
int  sdr_model_bin(const sdr_model_t *m, double mhz);
/** @brief Fit the graticule to the traces shown: its top on a whole ten at
 *         least 3 dB over their highest level, at the finest of 2, 5, 10
 *         and 20 dB a division that reaches 6 dB under their noise.  A
 *         wider division or a higher top applies at once, a narrower one or
 *         a lower top only with room to spare.  Also sets floor_db.
 *         Nothing changes before the first frame or with every trace off. */
void sdr_model_scale(sdr_model_t *m);
/** @brief The spectrogram's colour at @p t, 0 to 1: blue through green
 *         and yellow to red. */
tiku_rgb_t sdr_palette(float t);
/** @brief Which Wi-Fi, BLE and 802.15.4 channels hold @p mhz, as words.
 *  @return the text's length, as snprintf() counts it. */
int  sdr_channels(double mhz, char *out, size_t max);
/** @brief The command that asks the board for the next frame. */
const char *sdr_command(const sdr_model_t *m, char *out, size_t max);

/* tiku_sdr_marker.c */
/** @brief What marker @p k reads: its trace's values, or the latest
 *         spectrum while that trace is off. */
const float *sdr_mark_trace(const sdr_model_t *m, int k);
/** @brief Find the peaks of @p v, left to right, at most @p max of them,
 *         into @p bins.  A peak rises at least SDR_EXCURSION dB over the
 *         lowest level between it and the next higher one, or the edge, on
 *         each side; a flat top counts once, at its middle.
 *         @return how many were found; 0 before the first frame. */
int  sdr_peaks(const sdr_model_t *m, const float *v, int *bins, int max);
/** @brief Move marker @p k to a peak of its trace as @p how (SDR_TO_*)
 *         says, counting from where it stands; a marker outside the view
 *         goes to the highest peak, and with no peak at all SDR_TO_HIGHEST
 *         takes the highest bin.  @return 1 when it was placed; 0 when it
 *         is off, before the first frame, or when no peak lies that way. */
int  sdr_mark_peak(sdr_model_t *m, int k, int how);
/** @brief Turn marker @p k on at the highest peak with no other marker
 *         within a bin of it; with no such peak, or before the first frame,
 *         at the free bin nearest the view's middle. */
void sdr_mark_on(sdr_model_t *m, int k);
/** @brief Turn marker @p k off, and release it if the pointer holds it. */
void sdr_mark_off(sdr_model_t *m, int k);
/** @brief Whether any marker is on. */
int  sdr_marks_out(const sdr_model_t *m);
/** @brief Turn marker @p k on at the bin nearest @p mhz, kept inside the
 *         view.  With @p snap, while markers follow peaks and a frame is
 *         shown, it moves to the highest bin within 2 + n/48 bins, the
 *         middle of a flat top. */
void sdr_mark_at(sdr_model_t *m, int k, double mhz, int snap);
/** @brief While markers follow peaks, move each marker in the view but the
 *         one the pointer holds to the top of its peak, when that is at
 *         least FOLLOW_DB higher; frame_done() calls it after each frame. */
void sdr_mark_follow(sdr_model_t *m);
/** @brief The bin marker @p k stands on, or -1 when it is outside the
 *         view or not placed. */
int  sdr_mark_bin(const sdr_model_t *m, int k);
/** @brief Write marker @p k's readout: its frequency and, once a frame is
 *         shown, its level or that it is outside the view; for markers 2
 *         to 4 while M1 is in the view too, how far it is from M1 in both.
 *         Empty while it is off. */
void sdr_mark_text(const sdr_model_t *m, int k, char *out, size_t max);

/* tiku_sdr_link.c */
/** @brief List the mounted ESP32-C61s in st->names, and with no board
 *         chosen choose one: the desktop's current device when it is
 *         listed, else the first.  @return 0 while the desktop has not
 *         yet sent its list of boards, else 1. */
int  sdr_targets(sdr_t *st);
/** @brief Ask the desktop to let go of the chosen board, closing a
 *         recording being played; nothing while the link is not idle.
 *         @return 0, with the note saying why when the board cannot be
 *         asked for; -1 with nothing done when no ESP32-C61 is mounted. */
int  sdr_connect(sdr_t *st);
/** @brief Tell a running board to stop and give it back over the next
 *         ticks; a board still opening or starting goes back at once, and
 *         one the desktop is still letting go of, as soon as it is let go. */
void sdr_disconnect(sdr_t *st);
/** @brief Give the board back now, for the window closing when no ticks
 *         follow: a running board is told to stop, with at most DRAIN_US
 *         for that to be sent; a board the desktop is still letting go of
 *         is marked to go back once it is let go. */
void sdr_disconnect_now(sdr_t *st);
/** @brief Act on what the desktop says about boards: a board let go of or
 *         taken back, a handoff that failed, or a new current device. */
void sdr_board_said(void *state, int what, const char *text);
/** @brief The link's work for one tick: give up on passed deadlines, act on
 *         what the board sent (up to READ_MAX bytes), ask for the next
 *         frame while the board runs, nothing is pending and the view is not
 *         paused, and count the frame rate each second. */
void sdr_link_tick(sdr_t *st);
/** @brief Queue a gain query (-2), automatic gain (-1), or held index.
 *         Writes wait for the preceding command's prompt. */
int sdr_gain_request(sdr_t *st, int gain);

/* tiku_sdr_tape.c */
/** @brief Write a name to offer for a new file: "TikuSDR", the board and
 *         the time, then @p ext; for a CSV or a picture while a recording
 *         is on screen, the recording's own name with @p ext instead. */
void sdr_tape_name(const sdr_t *st, const char *ext, char *out, size_t max);
/** @brief Start recording to @p path: its head, then the view.  @return 0;
 *         -1 when a recording is being made or the board is not running,
 *         or with the reason in the note when the file cannot be made. */
int  sdr_rec_start(sdr_t *st, const char *path);
/** @brief Write @p line with its time since the recording began, when one
 *         is being made; @p frame counts a finished frame.  A failed write
 *         stops the recording. */
void sdr_rec_line(sdr_t *st, const char *line, int frame);
/** @brief Write the view to the recording as "VIEW range <start> <stop>";
 *         nothing when none is being made. */
void sdr_rec_view(sdr_t *st);
/** @brief Close the recording, and say in st->saved and the note what was
 *         saved, or that a write failed. */
void sdr_rec_stop(sdr_t *st);
/** @brief Play @p path from its start.  @return 0, or -1 with the reason
 *         in the note. */
int  sdr_play_start(sdr_t *st, const char *path);
/** @brief Play the lines whose time has come, PLAY_BURST a tick at most;
 *         the play clock stands still while paused.  At the end the note
 *         says so and playing stops. */
void sdr_play_tick(sdr_t *st);
/** @brief Close the recording being played and set one_max back to
 *         SDR_LIVE_SPAN. */
void sdr_play_stop(sdr_t *st);
/** @brief Rebuild playback state up to a time, in bounded batches per tick.
 *         Seeking leaves playback paused. */
void sdr_play_seek(sdr_t *st, double seconds);
/** @brief Advance a paused recording through the next complete frame. */
void sdr_play_step(sdr_t *st);
/** @brief @p path's last part, the file's own name. */
const char *sdr_tape_leaf(const char *path);
/** @brief @p seconds as "m:ss", or "h:mm:ss" past an hour. */
void sdr_tape_clock(double seconds, char *out, size_t max);
/** @brief @p bytes as "840 KB" or "12.3 MB". */
void sdr_tape_size(long bytes, char *out, size_t max);
/** @brief Export spectra to @p path as CSV rows of time, frequency and
 *         level: the recording on screen, converted over the next ticks,
 *         or else the spectra the spectrogram holds, at once.  @return 0;
 *         -1 while an export runs, or with the reason in the note. */
int  sdr_csv_start(sdr_t *st, const char *path);
/** @brief Convert the next part of the recording, CSV_LINES lines or
 *         CSV_ROWS rows at most; the note says how far it is. */
void sdr_csv_tick(sdr_t *st);
/** @brief Abandon a running conversion, leaving the CSV as far as it got;
 *         for the window closing. */
void sdr_csv_stop(sdr_t *st);

/* tiku_sdr_app.c */
/** @brief Open TikuSDR's window, sizeable when the desktop allows it, and
 *         publish its menus and first frame.  @return 0, or -1 when the
 *         desktop gave no window. */
int  sdr_window_open(sdr_t *st);

/* tiku_sdr_splash.c */
/** @brief Open the splash: a card window with TikuSDR's name, a status
 *         line and the license notice.  @return 0, or -1 when it could not
 *         be opened. */
int  sdr_splash_open(sdr_t *st);
/** @brief Show the status line @p now has reached, and close the splash
 *         once SDR_SPLASH_US has passed since its first tick. */
void sdr_splash_tick(sdr_t *st, int64_t now);
/** @brief Close the splash, if it is up, and open the window if it is not
 *         open. */
void sdr_splash_drop(sdr_t *st);
/** @brief Open the About box, or bring it to the front when it is open. */
void sdr_about_open(sdr_t *st);
/** @brief Close the About box, if it is open. */
void sdr_about_drop(sdr_t *st);

/* tiku_sdr_view.c */
/** @brief Lay out the graticule, where the traces go, in @p plot and the
 *         spectrogram above it in @p fall, of no height while the
 *         spectrogram is off. */
void sdr_layout(const sdr_t *st, tiku_rect_t *plot, tiku_rect_t *fall);
/** @brief Where the alert goes: centred across the window, a third of the
 *         way down. */
tiku_rect_t sdr_alert_rect(const sdr_t *st);
/** @brief Where control @p id is: a CMD_* button, checkbox or switch of
 *         the panel or the bar, CMD_FIELD + SDR_F_* or CMD_CHOICE +
 *         SDR_C_*; an empty rectangle for anything else. */
tiku_rect_t sdr_control_rect(const sdr_t *st, int id);
/** @brief Toggle a section, collapsing others if needed to fit the panel. */
void sdr_section_toggle(sdr_t *st, int section);
/** @brief Rectangle of the recording timeline. */
tiku_rect_t sdr_timeline_rect(const sdr_t *st);
/** @brief Register the panel's and the bar's controls with the dialog, for
 *         input and painting alike, each enabled as sdr_enabled() says;
 *         each list that is not open shows what it sets. */
void sdr_controls(sdr_t *st);
/** @brief Current field text, including the receiver's confirmed gain. */
void sdr_field_text(const sdr_t *st, int field, char *out, size_t size);
/** @brief Paint the whole window and hand it to the desktop; nothing while
 *         the window is not open. */
void sdr_paint(sdr_t *st);
/** @brief The words for @p command, on a button or in a menu; @p st may be
 *         NULL for any but Connect, Pause and Record, whose words change. */
const char *sdr_label(const sdr_t *st, int command);
/**
 * @brief Whether @p command can be used now.
 *
 * An alert leaves only Quit, and a recording playing turns off the range
 * fields, Live and To Center.  Record needs a running board or a recording
 * being made, Connect an ESP32-C61, Open Recording an idle link, Export a
 * frame or a recording on screen and no export running, and Save Picture a
 * frame.  Marker Off, the marker's field, To Center and the peak moves need
 * the selected marker on, the last two a frame too.
 */
int  sdr_enabled(const sdr_t *st, int command);
/** @brief Save the spectrogram and the spectrum, with their annotations
 *         and without the hover text, as a PNG at @p path.  @return 0, or
 *         -1 with the reason in the note; on success the note names the
 *         file. */
int  sdr_picture(sdr_t *st, const char *path);

#endif /* TIKU_SDR_H_ */
