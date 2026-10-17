/* tiku_sdr_view.c -- TikuSDR's window, laid out as a spectrum analyser: a
 * panel of settings at the left in sections (Frequency, Traces, Markers,
 * Display), the spectrogram with its colour scale, the spectrum on a
 * graticule annotated with its scale and range, and a bar at the foot with
 * Record, Pause and the status line.  Also where each control is, which
 * commands can be used and their words, and the picture saved as a PNG.
 * No device.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_choice_paint.h"
#include "tiku_font.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_sdr.h"

#define PLOT_BG    TIKU_RGB(255, 255, 255)
#define FRAME_INK  TIKU_RGB(140, 140, 140)
#define GRID_INK   TIKU_RGB(205, 205, 205)
#define NOTE_INK   TIKU_RGB(28, 28, 28)
#define DIM_INK    TIKU_RGB(110, 110, 110)
#define MARK_INK   TIKU_RGB(0, 128, 96)
#define MARK_DIM   TIKU_RGB(70, 96, 88)
#define BLE_INK    TIKU_RGB(160, 80, 160)
#define REC_INK    TIKU_RGB(214, 48, 49)

#define PANEL_X    8
#define PANEL_W    200
#define LABEL_W    66               /* a row's name, before its control */
#define MODE_W     90               /* Mode, left of the RBW list       */
#define HEAD_H     20               /* a section's bar                  */
#define ROW_H      24
#define CTL_H      22               /* a field, list or button in a row */
#define AREA_X     (PANEL_X + PANEL_W + 10)  /* the display area's left edge */
#define AXIS_W     52               /* the level scale, left of the grid */
#define BAR_H      76               /* transport, status, pointer readout */
#define NOTE_H     16               /* an annotation row                */

/* The traces' colours, by trace: black, red and blue, on white. */
static const tiku_rgb_t trace_ink[SDR_TRACES] = {
    TIKU_RGB(16, 16, 16), TIKU_RGB(210, 50, 40), TIKU_RGB(30, 100, 210)
};
static const char *const type_short[SDR_T_TYPES] = {
    "Off", "Clr/Wr", "Avg", "Max", "Min"
};

/* The panel's sections: a bar, then rows. */
enum { S_FREQ, S_TRACE, S_MARK, S_SHOW, S_RECEIVER, S_MEASURE, S_TX, SECTIONS };
static const struct { const char *name; int rows; } section[SECTIONS] = {
    {"Frequency", 5}, {"Traces", 4}, {"Markers", 7}, {"Display", 7},
    {"Receiver", 6}, {"Measurements", 8}, {"Transmission", 8}
};
/* Each row's name, by section, "" for none. */
static const char *const row_name[SECTIONS][8] = {
    {"Center", "Span", "Start", "Stop", "", "", ""},
    {"Trace", "Type", "Avg count", "", "", "", ""},
    {"Marker", "On trace", "", "Freq", "", "", ""},
    {"", "", "Ref", "dB/div", "", "Floor", "Ceiling", ""},
    {"", "Index", "", "", "", ""},
    {"", "", "Threshold", "Pre (s)", "Post (s)", "", "Bookmark", ""},
    {"RF MHz", "Offset kHz", "Amplitude", "Time ms", "", "", "", ""}
};

/** @brief Section @p s's bar's top. */
static int section_y(const sdr_t *st, int s)
{
    int y = 6;

    for (int i = 0; i < s; i++)
        y += HEAD_H + 10 + ((st->collapsed & (1u << i)) ? 0 :
                            section[i].rows * ROW_H);
    return y;
}

/** @brief Row @p row of section @p s, from @p x, @p w wide. */
static tiku_rect_t row_at(const sdr_t *st, int s, int row, int x, int w)
{
    if (st->collapsed & (1u << s)) return (tiku_rect_t){0, 0, 0, 0};
    return (tiku_rect_t){x, section_y(st, s) + HEAD_H + 4 + row * ROW_H + 1, w,
                         CTL_H};
}

/** @brief Button @p col of @p cols sharing row @p row of section @p s. */
static tiku_rect_t button_at(const sdr_t *st, int s, int row, int col, int cols)
{
    int w = (PANEL_W - 4 * (cols - 1)) / cols;

    return row_at(st, s, row, PANEL_X + col * (w + 4), w);
}

void sdr_section_toggle(sdr_t *st, int sec)
{
    if (sec < 0 || sec >= SECTIONS) return;
    st->collapsed ^= 1u << sec;
    for (int i = SECTIONS - 1; i >= 0 &&
         section_y(st, SECTIONS) > st->h - BAR_H - 4; i--)
        if (i != sec) st->collapsed |= 1u << i;
    st->dialog.focus = TIKU_DIALOG_NONE;
    for (int c = 0; c < SDR_CHOICES; c++) st->choice[c].open = 0;
}

tiku_rect_t sdr_timeline_rect(const sdr_t *st)
{
    return (tiku_rect_t){250, st->h - BAR_H + 4, st->w - 265, CTL_H};
}

tiku_rect_t sdr_control_rect(const sdr_t *st, int id)
{
    int x = PANEL_X + LABEL_W, w = PANEL_W - LABEL_W, fw = w - 30;
    int bar = st->h - BAR_H + 4;

    if (id >= CMD_SECTION && id < CMD_SECTION + SECTIONS)
        return (tiku_rect_t){PANEL_X, section_y(st, id - CMD_SECTION),
                             PANEL_W, HEAD_H};

    switch (id) {
    case CMD_FIELD + SDR_F_CENTER: return row_at(st, S_FREQ, 0, x, fw);
    case CMD_FIELD + SDR_F_SPAN:   return row_at(st, S_FREQ, 1, x, fw);
    case CMD_FIELD + SDR_F_START:  return row_at(st, S_FREQ, 2, x, fw);
    case CMD_FIELD + SDR_F_STOP:   return row_at(st, S_FREQ, 3, x, fw);
    case CMD_LIVE: return row_at(st, S_FREQ, 4, PANEL_X, MODE_W);
    case CMD_CHOICE + SDR_C_RBW:
        return row_at(st, S_FREQ, 4, PANEL_X + MODE_W + 4,
                      PANEL_W - MODE_W - 4);
    case CMD_CHOICE + SDR_C_TRACE: return row_at(st, S_TRACE, 0, x, w);
    case CMD_CHOICE + SDR_C_TYPE:  return row_at(st, S_TRACE, 1, x, w);
    case CMD_FIELD + SDR_F_AVG:    return row_at(st, S_TRACE, 2, x, 48);
    case CMD_CLEAR:  return button_at(st, S_TRACE, 3, 0, 2);
    case CMD_EXPORT: return button_at(st, S_TRACE, 3, 1, 2);
    case CMD_CHOICE + SDR_C_MARKER: return row_at(st, S_MARK, 0, x, w);
    case CMD_CHOICE + SDR_C_ON: return row_at(st, S_MARK, 1, x, w);
    case CMD_MARK_ACTIVE: return row_at(st, S_MARK, 2, x, 64);
    case CMD_TRACK: return row_at(st, S_MARK, 2, x + 70, w - 70);
    case CMD_FIELD + SDR_F_MARK: return row_at(st, S_MARK, 3, x, fw);
    case CMD_PEAK_SEARCH: return button_at(st, S_MARK, 4, 0, 2);
    case CMD_NEXT_PEAK:   return button_at(st, S_MARK, 4, 1, 2);
    case CMD_PEAK_LEFT:   return button_at(st, S_MARK, 5, 0, 2);
    case CMD_PEAK_RIGHT:  return button_at(st, S_MARK, 5, 1, 2);
    case CMD_MARK_CENTER: return button_at(st, S_MARK, 6, 0, 2);
    case CMD_MARK_CLEAR:  return button_at(st, S_MARK, 6, 1, 2);
    case CMD_WATERFALL: return row_at(st, S_SHOW, 0, PANEL_X + 4, PANEL_W - 8);
    case CMD_SCALE_AUTO: return row_at(st, S_SHOW, 1, PANEL_X + 4, PANEL_W - 8);
    case CMD_FIELD + SDR_F_REF: return row_at(st, S_SHOW, 2, x, fw);
    case CMD_FIELD + SDR_F_DIV: return row_at(st, S_SHOW, 3, x, fw);
    case CMD_COLOR_AUTO: return row_at(st, S_SHOW, 4, PANEL_X + 4, PANEL_W - 8);
    case CMD_FIELD + SDR_F_FLOOR: return row_at(st, S_SHOW, 5, x, fw);
    case CMD_FIELD + SDR_F_CEILING: return row_at(st, S_SHOW, 6, x, fw);
    case CMD_GAIN_AUTO: return row_at(st, S_RECEIVER, 0, PANEL_X + 4, PANEL_W - 8);
    case CMD_FIELD + SDR_F_GAIN: return row_at(st, S_RECEIVER, 1, x, fw);
    case CMD_GAIN_READ: return button_at(st, S_RECEIVER, 2, 0, 1);
    case CMD_CAPS: return button_at(st, S_RECEIVER, 3, 0, 1);
    case CMD_FFT: return button_at(st, S_RECEIVER, 4, 0, 1);
    case CMD_RATE: return button_at(st, S_RECEIVER, 5, 0, 1);
    case CMD_OCCUPANCY: return button_at(st, S_MEASURE, 0, 0, 1);
    case CMD_OCC_PLAN: return row_at(st, S_MEASURE, 1, PANEL_X, 134);
    case CMD_OCC_PAGE: return row_at(st, S_MEASURE, 1, PANEL_X + 138, 62);
    case CMD_FIELD + SDR_F_THRESHOLD: return row_at(st, S_MEASURE, 2, x, w);
    case CMD_FIELD + SDR_F_PRE: return row_at(st, S_MEASURE, 3, x, w);
    case CMD_FIELD + SDR_F_POST: return row_at(st, S_MEASURE, 4, x, w);
    case CMD_TRIGGER: return button_at(st, S_MEASURE, 5, 0, 1);
    case CMD_FIELD + SDR_F_BOOK: return row_at(st, S_MEASURE, 6, x, w);
    case CMD_BOOK_ADD: return button_at(st, S_MEASURE, 7, 0, 2);
    case CMD_BOOK_NEXT: return button_at(st, S_MEASURE, 7, 1, 2);
    case CMD_FIELD + SDR_F_TX_FREQ: return row_at(st, S_TX, 0, x, w);
    case CMD_FIELD + SDR_F_TX_OFFSET: return row_at(st, S_TX, 1, x, w);
    case CMD_FIELD + SDR_F_TX_AMP: return row_at(st, S_TX, 2, x, w);
    case CMD_FIELD + SDR_F_TX_MS: return row_at(st, S_TX, 3, x, w);
    case CMD_TX_LOAD: return button_at(st, S_TX, 4, 0, 1);
    case CMD_TX_TONE: return button_at(st, S_TX, 5, 0, 1);
    case CMD_TX_SEND: return button_at(st, S_TX, 6, 0, 1);
    case CMD_TX_CAPS: return button_at(st, S_TX, 7, 0, 1);
    case CMD_RECORD: return (tiku_rect_t){PANEL_X, bar, 70, CTL_H};
    case CMD_PAUSE:  return (tiku_rect_t){PANEL_X + 74, bar, 66, CTL_H};
    case CMD_CONNECT: return (tiku_rect_t){PANEL_X + 144, bar, 88, CTL_H};
    default: return (tiku_rect_t){0, 0, 0, 0};
    }
}

/** @brief The spectrum's frame: the graticule with its annotations. */
static tiku_rect_t frame_rect(const sdr_t *st)
{
    tiku_rect_t plot, fall;

    sdr_layout(st, &plot, &fall);
    return (tiku_rect_t){AREA_X, plot.y - 2 * NOTE_H - 6, st->w - AREA_X - 8,
                         st->h - BAR_H - 4 - (plot.y - 2 * NOTE_H - 6)};
}

void sdr_layout(const sdr_t *st, tiku_rect_t *plot, tiku_rect_t *fall)
{
    int x = AREA_X + AXIS_W, w = st->w - x - 18;
    int top = 6, bottom = st->h - BAR_H - 4;
    int percent = st->fall_percent ? st->fall_percent : 36;
    int fh = st->fall_on ? (bottom - top) * percent / 100 : 0;
    if (st->fall_on && st->occupancy.visible && fh < 220) fh = 220;
    int ft = st->fall_on ? top + fh + 6 : top;  /* the frame's top */
    int py = ft + 2 * NOTE_H + 6;

    *fall = (tiku_rect_t){x, top, w, fh};
    *plot = (tiku_rect_t){x, py, w, bottom - 2 * NOTE_H - 6 - py};
}

const char *sdr_label(const sdr_t *st, int command)
{
    switch (command) {
    case CMD_TX_CAPS: return st->tx.known == 1 ? "TX: 80 MS/s (Read)" :
                             st->tx.known < 0 ? "TX unavailable (Read)" : "Read TX capabilities";
    case CMD_TX_SEND: return "Transmit burst";
    case CMD_TX_LOAD: return "Load IQ file...";
    case CMD_TX_TONE: return st->tx.samples ? "Use generated tone" : "Tone selected";
    case CMD_CONNECT:
        return st->state == SDR_IDLE ? "Connect" :
               st->state >= SDR_STOPPING ? "Returning" : "Disconnect";
    case CMD_LIVE: return sdr_model_is_live(&st->model) ? "Mode: Live" : "Mode: Sweep";
    case CMD_CLEAR: return "Clear";
    case CMD_EXPORT: return "Export";
    case CMD_MARK_ACTIVE: return "Active";
    case CMD_TRACK: return "Track";
    case CMD_PEAK_SEARCH: return "Peak Search";
    case CMD_NEXT_PEAK: return "Next Peak";
    case CMD_PEAK_LEFT: return "Peak Left";
    case CMD_PEAK_RIGHT: return "Peak Right";
    case CMD_MARK_CENTER: return "To Center";
    case CMD_MARK_CLEAR: return "All Off";
    case CMD_WATERFALL: return "Spectrogram";
    case CMD_PAUSE: return st->paused ||
        (st->state != SDR_RUNNING && !st->play.file) ? "Run" : "Pause";
    case CMD_RECORD: return st->rec.file ? "Recording" : "Record";
    case CMD_REPLAY: return "Open Recording";
    case CMD_PICTURE: return "Save Picture";
    case CMD_PEAK: return "Max Hold";
    case CMD_OVERLAY: return "Clear & Write";
    case CMD_MARK_REMOVE: return "Marker Off";
    case CMD_ABOUT: return "About TikuSDR";
    case CMD_SCALE_AUTO: return "Auto spectrum scale";
    case CMD_COLOR_AUTO: return "Auto waterfall scale";
    case CMD_GAIN_AUTO: return "Automatic gain";
    case CMD_GAIN_READ: return "Read receiver gain";
    case CMD_REWIND: return "Back 10 Seconds";
    case CMD_FORWARD: return "Forward 10 Seconds";
    case CMD_STEP: return "Next Frame";
    case CMD_SPEED: return "Playback Speed";
    case CMD_GAPS: return "Skip Long Gaps";
    case CMD_ZOOM_IN: return "Zoom In";
    case CMD_ZOOM_OUT: return "Zoom Out";
    case CMD_VIEW_BACK: return "Previous View";
    case CMD_HISTORY_OLDER: return "Older Waterfall Rows";
    case CMD_HISTORY_NEWER: return "Newer Waterfall Rows";
    case CMD_HISTORY_LATEST: return "Return to Latest";
    case CMD_PRESET_SAVE: return "Save Named Setup…";
    case CMD_PRESET_OPEN: return "Open Setup…";
    case CMD_REF_SAVE: return "Save Trace as Reference…";
    case CMD_REF_OPEN: return "Open Reference Trace…";
    case CMD_REF_SHOW: return "Show Reference Trace";
    case CMD_OCCUPANCY: return "Channel Activity";
    case CMD_OCC_PLAN: return "Channel Plan (Cycle)";
    case CMD_OCC_PAGE: return "Next Page";
    case CMD_OCC_RESET: return "Reset Activity Counts";
    case CMD_OCC_EXPORT: return "Export Activity CSV…";
    case CMD_TRIGGER: return st && (st->trigger.armed || st->trigger.fired) ?
        "Disarm / Finish Capture" : "Arm Trigger…";
    case CMD_CAPS: return "Read Capabilities";
    case CMD_FFT: return "FFT Size (Cycle)";
    case CMD_RBW: return "RBW (Cycle)";
    case CMD_RATE: return "Sample Rate (Cycle)";
    case CMD_BOOK_ADD: return "Bookmark";
    case CMD_BOOK_NEXT: return "Next Event";
    case CMD_BOOK_CLEAR: return "Clear Bookmarks";
    case CMD_LOOP_A: return "Set Loop Start Here";
    case CMD_LOOP_B: return "Set Loop End Here";
    case CMD_LOOP: return "Loop Selected Interval";
    case CMD_HEALTH: return "Show Capture Health";
    default: return "Quit";
    }
}

tiku_rect_t sdr_alert_rect(const sdr_t *st)
{
    int w, h;

    tiku_alert_size(&w, &h);
    return (tiku_rect_t){(st->w - w) / 2, (st->h - h) / 3, w, h};
}

int sdr_enabled(const sdr_t *st, int command)
{
    const sdr_model_t *m = &st->model;
    int out = m->mark[m->mark_sel].on, peaks = out && m->fresh;

    if (st->alert.open) return command == CMD_QUIT;
    if (sdr_tx_busy(st) && command != CMD_CONNECT && command != CMD_QUIT &&
        !(command >= CMD_SECTION && command < CMD_SECTION + SDR_SECTIONS)) return 0;
    if (command == CMD_TX_CAPS)
        return st->state == SDR_RUNNING && st->tx.known != -2 && !st->caps_queued && !st->caps_inflight &&
               !st->gain_queued && !st->gain_inflight;
    if ((command >= CMD_TX_SEND && command <= CMD_TX_TONE) ||
        (command >= CMD_FIELD + SDR_F_TX_FREQ && command <= CMD_FIELD + SDR_F_TX_MS)) {
        if (st->state != SDR_RUNNING || st->tx.known != 1 || st->asking) return 0;
        if (command == CMD_TX_SEND)
            return !st->rec.file && !st->play.file && !st->trigger.armed && !st->trigger.fired &&
                   !st->caps_queued && !st->caps_inflight && !st->gain_queued && !st->gain_inflight;
        if (command == CMD_FIELD + SDR_F_TX_OFFSET || command == CMD_FIELD + SDR_F_TX_AMP)
            return st->tx.samples == NULL;
        return 1;
    }
    if (command == CMD_CHOICE + SDR_C_RBW)
        return st->state == SDR_RUNNING && st->caps_known == 1;
    if (command >= CMD_PRESET_SAVE && command <= CMD_RBW) {
        switch (command) {
        case CMD_PRESET_OPEN: case CMD_PRESET_SAVE:
        case CMD_REF_OPEN: case CMD_OCC_EXPORT: return !st->asking;
        case CMD_REF_SAVE: return m->fresh && m->type[st->trace_sel] != SDR_T_OFF && !st->asking;
        case CMD_REF_SHOW: return st->reference.valid;
        case CMD_CAPS: return st->state == SDR_RUNNING && !st->caps_queued && !st->caps_inflight;
        case CMD_FFT: case CMD_RATE: case CMD_RBW:
            return st->state == SDR_RUNNING && st->caps_known == 1;
        case CMD_TRIGGER: return st->trigger.armed || st->trigger.fired ||
            (st->state == SDR_RUNNING && !st->paused && !st->rec.file && !st->asking);
        case CMD_BOOK_ADD: return st->shown[0] && st->book_count < SDR_BOOKS && !st->play.seeking;
        case CMD_BOOK_NEXT: case CMD_BOOK_CLEAR: return st->shown[0] && st->book_count > 0;
        case CMD_LOOP_A: case CMD_LOOP_B: return st->shown[0] && !st->play.seeking;
        case CMD_LOOP: return st->shown[0] && st->play.loop_b > st->play.loop_a;
        default: return 1;
        }
    }
    if (command == CMD_FIELD + SDR_F_MARK) return out;
    if (command == CMD_FIELD + SDR_F_GAIN || command == CMD_GAIN_AUTO)
        return st->state == SDR_RUNNING && st->gain_known == 1 &&
               !st->gain_queued && !st->gain_inflight;
    if (command >= CMD_FIELD && command < CMD_FIELD + SDR_F_AVG)
        return !st->play.file;          /* a recording playing sets the view */
    switch (command) {
    case CMD_ZOOM_IN: case CMD_ZOOM_OUT: case CMD_VIEW_BACK:
        return !st->play.file && !st->shown[0] &&
            (command != CMD_VIEW_BACK || st->previous_count > 0);
    case CMD_HISTORY_OLDER: case CMD_HISTORY_NEWER: case CMD_HISTORY_LATEST:
        return m->wf_count > 0;
    case CMD_PAUSE: return !st->play.seeking &&
        (st->state == SDR_RUNNING || st->play.file);
    case CMD_GAIN_READ:
        return st->state == SDR_RUNNING && !st->gain_queued && !st->gain_inflight;
    case CMD_REWIND: case CMD_FORWARD: case CMD_STEP: case CMD_SPEED:
    case CMD_GAPS:
        return st->play.file != NULL ||
            ((command == CMD_REWIND || command == CMD_FORWARD) &&
             st->state == SDR_IDLE && st->shown[0]);
    case CMD_CONNECT:                   /* idle: Connect; else Disconnect */
        return st->state == SDR_IDLE ? st->names[0] != '\0' :
                                       st->state < SDR_STOPPING;
    case CMD_LIVE: return !st->play.file;
    case CMD_RECORD: return st->state == SDR_RUNNING || st->rec.file;
    case CMD_REPLAY: return st->state == SDR_IDLE;
    case CMD_EXPORT:
        return (m->fresh || st->shown[0]) && !st->csv.in;
    case CMD_PICTURE: return m->fresh;
    case CMD_PEAK_SEARCH: case CMD_NEXT_PEAK: case CMD_PEAK_LEFT:
    case CMD_PEAK_RIGHT: return peaks;
    case CMD_MARK_CENTER: return peaks && !st->play.file;
    case CMD_MARK_REMOVE: return out;
    case CMD_MARK_CLEAR: return sdr_marks_out(m);
    default: return 1;
    }
}

/** @brief Fill the RBW list unless it is open: Auto, with the RBW it gives
 *         this view, then each RBW the view can have, coarsest first, and
 *         the one in use though the list would leave it out.
 *         @return the row of the RBW in use. */
static int rbw_items(sdr_t *st)
{
    tiku_choice_t *ch = &st->choice[SDR_C_RBW];
    const sdr_model_t *m = &st->model;
    double now = sdr_model_bin_khz(m);
    int manual = m->rbw_bin_khz > 0 || m->rate_override || m->fft_override;
    int sel = 0, at = 0, found = 0;
    char a[24], t[48];

    if (ch->open) return ch->selected;
    st->rbw_count = sdr_model_rbw_options(m, st->rbw_list, SDR_RBW_MAX);
    for (int i = 0; i < st->rbw_count; i++)
        found |= fabs(st->rbw_list[i] - now) < 1e-6;
    if (manual && !found && st->rbw_count < SDR_RBW_MAX) {
        while (at < st->rbw_count && st->rbw_list[at] > now) at++;
        memmove(&st->rbw_list[at + 1], &st->rbw_list[at],
                (size_t)(st->rbw_count - at) * sizeof st->rbw_list[0]);
        st->rbw_list[at] = now;
        st->rbw_count++;
    }
    tiku_choice_init(ch);
    sdr_mhz_text(SDR_HANN_RBW * sdr_model_auto_bin_khz(m), a, sizeof a);
    snprintf(t, sizeof t, "Auto %s kHz", a);
    tiku_choice_add(ch, t, 1);
    for (int i = 0; i < st->rbw_count; i++) {
        sdr_mhz_text(SDR_HANN_RBW * st->rbw_list[i], a, sizeof a);
        snprintf(t, sizeof t, "%s kHz", a);
        tiku_choice_add(ch, t, 1);
        if (manual && fabs(st->rbw_list[i] - now) < 1e-6) sel = i + 1;
    }
    return sel;
}

/** @brief Set each list that is not open to show what it sets, and enable
 *         the lists as sdr_enabled() says. */
static void sync_choices(sdr_t *st)
{
    const sdr_model_t *m = &st->model;
    int rbw = rbw_items(st);
    int value[SDR_CHOICES] = {st->trace_sel, m->type[st->trace_sel],
                              m->mark_sel, m->mark[m->mark_sel].trace, rbw};

    for (int c = 0; c < SDR_CHOICES; c++) {
        tiku_choice_t *ch = &st->choice[c];
        if (!ch->open) ch->selected = ch->hot = value[c];
        ch->enabled = sdr_enabled(st, CMD_CHOICE + c);
    }
}

void sdr_controls(sdr_t *st)
{
    int previous_focus = st->dialog.focus;
    /* The bar's buttons come first: a focus the dialog places itself goes
     * to the first enabled control, Pause, so Space pauses and keys are
     * not typed into a field.  Then each section from the top, in the
     * order Tab takes them. */
    static const struct { int id; tiku_dialog_kind_t kind; } ctl[] = {
        {CMD_PAUSE, TIKU_DIALOG_BUTTON}, {CMD_RECORD, TIKU_DIALOG_BUTTON},
        {CMD_CONNECT, TIKU_DIALOG_BUTTON},
        {CMD_FIELD + SDR_F_CENTER, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_SPAN, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_START, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_STOP, TIKU_DIALOG_INPUT},
        {CMD_LIVE, TIKU_DIALOG_BUTTON},
        {CMD_CHOICE + SDR_C_RBW, TIKU_DIALOG_INPUT},
        {CMD_CHOICE + SDR_C_TRACE, TIKU_DIALOG_INPUT},
        {CMD_CHOICE + SDR_C_TYPE, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_AVG, TIKU_DIALOG_INPUT},
        {CMD_CLEAR, TIKU_DIALOG_BUTTON}, {CMD_EXPORT, TIKU_DIALOG_BUTTON},
        {CMD_CHOICE + SDR_C_MARKER, TIKU_DIALOG_INPUT},
        {CMD_MARK_ACTIVE, TIKU_DIALOG_TOGGLE},
        {CMD_CHOICE + SDR_C_ON, TIKU_DIALOG_INPUT},
        {CMD_TRACK, TIKU_DIALOG_TOGGLE},
        {CMD_FIELD + SDR_F_MARK, TIKU_DIALOG_INPUT},
        {CMD_PEAK_SEARCH, TIKU_DIALOG_BUTTON},
        {CMD_NEXT_PEAK, TIKU_DIALOG_BUTTON},
        {CMD_PEAK_LEFT, TIKU_DIALOG_BUTTON},
        {CMD_PEAK_RIGHT, TIKU_DIALOG_BUTTON},
        {CMD_MARK_CENTER, TIKU_DIALOG_BUTTON},
        {CMD_MARK_CLEAR, TIKU_DIALOG_BUTTON},
        {CMD_WATERFALL, TIKU_DIALOG_TOGGLE},
        {CMD_SCALE_AUTO, TIKU_DIALOG_TOGGLE}, {CMD_COLOR_AUTO, TIKU_DIALOG_TOGGLE},
        {CMD_FIELD + SDR_F_REF, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_DIV, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_FLOOR, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_CEILING, TIKU_DIALOG_INPUT},
        {CMD_GAIN_AUTO, TIKU_DIALOG_TOGGLE}, {CMD_GAIN_READ, TIKU_DIALOG_BUTTON},
        {CMD_FIELD + SDR_F_GAIN, TIKU_DIALOG_INPUT},
        {CMD_CAPS, TIKU_DIALOG_BUTTON}, {CMD_FFT, TIKU_DIALOG_BUTTON},
        {CMD_RATE, TIKU_DIALOG_BUTTON}, {CMD_OCCUPANCY, TIKU_DIALOG_BUTTON},
        {CMD_OCC_PLAN, TIKU_DIALOG_BUTTON}, {CMD_TRIGGER, TIKU_DIALOG_BUTTON},
        {CMD_OCC_PAGE, TIKU_DIALOG_BUTTON},
        {CMD_FIELD + SDR_F_THRESHOLD, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_PRE, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_POST, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_BOOK, TIKU_DIALOG_INPUT},
        {CMD_BOOK_ADD, TIKU_DIALOG_BUTTON}, {CMD_BOOK_NEXT, TIKU_DIALOG_BUTTON},
        {CMD_FIELD + SDR_F_TX_FREQ, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_TX_OFFSET, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_TX_AMP, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_TX_MS, TIKU_DIALOG_INPUT},
        {CMD_TX_CAPS, TIKU_DIALOG_BUTTON}, {CMD_TX_LOAD, TIKU_DIALOG_BUTTON},
        {CMD_TX_TONE, TIKU_DIALOG_BUTTON}, {CMD_TX_SEND, TIKU_DIALOG_BUTTON}
    };

    for (int i = SECTIONS - 1; i >= 0 &&
         section_y(st, SECTIONS) > st->h - BAR_H - 4; i--)
        st->collapsed |= 1u << i;
    sync_choices(st);
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, CMD_QUIT);
    for (size_t i = 0; i < sizeof ctl / sizeof ctl[0]; i++) {
        tiku_rect_t r = sdr_control_rect(st, ctl[i].id);
        if (r.w > 0 && r.h > 0)
            tiku_dialog_add(&st->dialog, ctl[i].id, r,
                           ctl[i].kind, sdr_enabled(st, ctl[i].id));
    }
    for (int i = 0; i < SECTIONS; i++)
        tiku_dialog_add(&st->dialog, CMD_SECTION + i,
                       sdr_control_rect(st, CMD_SECTION + i),
                       TIKU_DIALOG_BUTTON, !st->alert.open);
    tiku_dialog_end(&st->dialog);
    /* An acquisition ending or a section closing must not start editing a
     * frequency. Only an explicit click or Tab should enter a text field. */
    if (st->dialog.focus >= CMD_FIELD &&
        st->dialog.focus < CMD_FIELD + SDR_FIELDS &&
        st->dialog.focus != previous_focus)
        st->dialog.focus = CMD_SECTION + SDR_S_FREQ;
}

/** @brief The x of @p mhz across @p r. */
static int x_of(tiku_rect_t r, const sdr_model_t *m, double mhz)
{
    double lo = m->lo_mhz - m->bin_mhz / 2, hi = lo + m->n * m->bin_mhz;

    return r.x + (int)lround((mhz - lo) / (hi - lo) * (r.w - 1));
}

/** @brief The y of @p db in @p r, clamped inside it. */
static int y_of(tiku_rect_t r, const sdr_model_t *m, float db)
{
    float t = (db - m->scale_lo) / (m->scale_hi - m->scale_lo);
    int y = r.y + r.h - 1 - (int)lroundf(t * (r.h - 1));

    return y < r.y ? r.y : y > r.y + r.h - 1 ? r.y + r.h - 1 : y;
}

/** @brief A trace's value at column @p x, between its bins. */
static float at_column(tiku_rect_t r, const sdr_model_t *m, const float *v,
                       int x)
{
    float fb = (float)(x - r.x) * (float)m->n / (float)r.w - 0.5f;
    int b = (int)floorf(fb);
    float f = fb - (float)b;

    if (b < 0) return v[0];
    if (b >= m->n - 1) return v[m->n - 1];
    return v[b] + (v[b + 1] - v[b]) * f;
}

/** @brief A trace as a line of vertical runs, one a column. */
static void trace(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m,
                  const float *v, tiku_rgb_t ink)
{
    int prev = -1;

    for (int x = r.x; x < r.x + r.w; x++) {
        int y = y_of(r, m, at_column(r, m, v, x));
        int top = prev < 0 ? y : (prev < y ? prev : y);
        int bot = prev < 0 ? y : (prev > y ? prev : y);

        tiku_vline(s, x, top, bot - top + 1, ink);
        prev = y;
    }
}

/** @brief Draw @p t, centred in the NOTE_H row at @p y: ending at @p x
 *         with @p align -1, centred on it with 0, starting at it with 1. */
static void note(tiku_surface_t *s, int x, int y, int align, const char *t,
                 tiku_rgb_t ink)
{
    const tiku_font_t *f = tiku_font_plain();
    int w = tiku_text_width(f, t);

    x = align < 0 ? x - w : align == 0 ? x - w / 2 : x;
    tiku_text(s, f, x, y + (NOTE_H - f->height) / 2 + f->ascent, t, ink);
}

/** @brief The graticule: ten divisions each way, dotted, in a solid
 *         frame, the level of each line at its left. */
static void graticule(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m)
{
    char t[16];

    for (int i = 0; i <= SDR_DIVS; i++) {
        int y = r.y + i * (r.h - 1) / SDR_DIVS;
        int x = r.x + i * (r.w - 1) / SDR_DIVS;
        for (int k = 0; k < r.w; k += 3) tiku_pixel(s, r.x + k, y, GRID_INK);
        for (int k = 0; k < r.h; k += 3) tiku_pixel(s, x, r.y + k, GRID_INK);
        snprintf(t, sizeof t, "%.0f",
                 (double)(m->scale_hi - (float)i * m->div));
        note(s, r.x - 6, y - NOTE_H / 2, -1, t, NOTE_INK);
    }
    tiku_frame(s, (tiku_rect_t){r.x - 1, r.y - 1, r.w + 2, r.h + 2},
               FRAME_INK);
}

/** @brief A dotted line down the graticule at each marker in the view. */
static void marker_lines(tiku_surface_t *s, tiku_rect_t r,
                         const sdr_model_t *m)
{
    for (int k = 0; k < SDR_MARKERS; k++) {
        int b = sdr_mark_bin(m, k), x;

        if (b < 0) continue;
        x = x_of(r, m, sdr_model_freq(m, b));
        for (int y = r.y; y < r.y + r.h; y += 4)
            tiku_vline(s, x, y, 2, k == m->mark_sel ? MARK_DIM
                                                    : GRID_INK);
    }
}

/** @brief Each marker as a numbered pointer down onto its trace, kept
 *         inside the graticule; the selected one in MARK_INK, the others
 *         in MARK_DIM. */
static void marker_pointers(tiku_surface_t *s, tiku_rect_t r,
                            const sdr_model_t *m)
{
    const tiku_font_t *f = tiku_font_bold();

    for (int k = 0; k < SDR_MARKERS; k++) {
        int b = sdr_mark_bin(m, k), x, base;
        tiku_rgb_t ink = k == m->mark_sel ? MARK_INK : MARK_DIM;
        char t[4];

        if (b < 0) continue;
        x = x_of(r, m, sdr_model_freq(m, b));
        base = y_of(r, m, sdr_mark_trace(m, k)[b]) - 7;
        if (base < r.y + 14) base = r.y + 14;
        for (int i = 0; i < 5; i++)
            tiku_hline(s, x - 4 + i, base + i, 9 - 2 * i, ink);
        snprintf(t, sizeof t, "%d", k + 1);
        tiku_text(s, f, x - tiku_text_width(f, t) / 2, base - 3, t, ink);
    }
}

/** @brief The markers' readings in the graticule's top left corner, on
 *         white, the selected marker's in its ink. */
static void readout(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m)
{
    const tiku_font_t *f = tiku_font_plain();
    char line[SDR_MARKERS][96];
    int which[SDR_MARKERS], n = 0, w = 0, row = f->height + 2;

    for (int k = 0; k < SDR_MARKERS; k++) {
        if (!m->mark[k].on) continue;
        sdr_mark_text(m, k, line[n], sizeof line[n]);
        if (tiku_text_width(f, line[n]) > w) w = tiku_text_width(f, line[n]);
        which[n++] = k;
    }
    if (!n) return;
    tiku_rect_t box = {r.x + 4, r.y + 4, w + 12, n * row + 6};
    tiku_fill(s, box, PLOT_BG);
    tiku_frame(s, box, GRID_INK);
    for (int i = 0; i < n; i++)
        tiku_text(s, f, box.x + 6, box.y + 3 + i * row + f->ascent, line[i],
                  which[i] == m->mark_sel ? MARK_INK : NOTE_INK);
}

/** @brief Write the frame's notes.  Above the graticule: the reference
 *         level and dB a division, the resolution bandwidth and the rate,
 *         the gain and which trace is which.  Below it: the start, centre,
 *         stop and span, the points, and the frames a second while the
 *         board runs or a recording plays. */
static void annotations(sdr_t *st, tiku_rect_t f, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    tiku_surface_t *s = st->surface;
    int top = f.y + 2, foot = r.y + r.h + 3, cx = r.x + r.w / 2;
    int right = f.x + f.w - 8;
    char t[96], a[24], b[24];

    snprintf(t, sizeof t, "Ref %.0f dB", (double)m->scale_hi);
    note(s, f.x + 8, top, 1, t, NOTE_INK);
    snprintf(t, sizeof t, "Div %.0f dB", (double)m->div);
    note(s, f.x + 8, top + NOTE_H, 1, t, NOTE_INK);
    sdr_mhz_text(SDR_HANN_RBW * sdr_model_bin_khz(m), a, sizeof a);
    sdr_mhz_text(sdr_model_bin_khz(m), b, sizeof b);
    snprintf(t, sizeof t, "RBW %s kHz · bin %s kHz", a, b);
    note(s, cx, top, 0, t, NOTE_INK);
    if (m->sweep)
        snprintf(t, sizeof t, "%d slices at %g MS/s", sdr_model_slices(m),
                 sdr_rate_mhz(m->rate));
    else
        snprintf(t, sizeof t, "%g MS/s", sdr_rate_mhz(m->rate));
    note(s, cx, top + NOTE_H, 0, t, DIM_INK);
    if (m->fresh) {
        snprintf(t, sizeof t, m->held ? "Gain %d" : "Gain %d auto", m->gain);
        note(s, right, top, -1, t, NOTE_INK);
    }
    /* Each trace that is on, in its ink: 1 Avg 4, 2 Max. */
    for (int k = SDR_TRACES - 1, x = right; k >= 0; k--) {
        if (m->type[k] == SDR_T_OFF) continue;
        if (m->type[k] == SDR_T_AVG)
            snprintf(t, sizeof t, "%d %s %d", k + 1, type_short[m->type[k]],
                     m->avg_count);
        else
            snprintf(t, sizeof t, "%d %s", k + 1, type_short[m->type[k]]);
        note(s, x, top + NOTE_H, -1, t, trace_ink[k]);
        x -= tiku_text_width(tiku_font_plain(), t) + 12;
    }
    sdr_mhz_text(m->start, a, sizeof a);
    snprintf(t, sizeof t, "Start %s MHz", a);
    note(s, r.x, foot, 1, t, NOTE_INK);
    sdr_mhz_text((m->start + m->stop) / 2, a, sizeof a);
    snprintf(t, sizeof t, "Center %s MHz", a);
    note(s, cx, foot, 0, t, NOTE_INK);
    sdr_mhz_text(m->stop, a, sizeof a);
    snprintf(t, sizeof t, "Stop %s MHz", a);
    note(s, r.x + r.w, foot, -1, t, NOTE_INK);
    sdr_mhz_text(m->stop - m->start, a, sizeof a);
    snprintf(t, sizeof t, "Span %s MHz", a);
    note(s, cx, foot + NOTE_H, 0, t, NOTE_INK);
    snprintf(t, sizeof t, "%d pts", m->n);
    note(s, r.x + r.w, foot + NOTE_H, -1, t, DIM_INK);
    if (st->state == SDR_RUNNING || st->play.file) {
        snprintf(t, sizeof t, "%.1f %s/s", (double)st->rate_hz,
                 m->sweep ? "sweeps" : "spectra");
        note(s, r.x, foot + NOTE_H, 1, t, DIM_INK);
    }
}

/** @brief The spectrum's frame: white, with the graticule and its notes;
 *         once a frame is in, a 40 MS/s spectrum shaded outside its flat
 *         middle, then the traces and the markers. */
static void spectrum(sdr_t *st, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    tiku_rect_t f = frame_rect(st);

    tiku_fill(st->surface, f, PLOT_BG);
    tiku_frame(st->surface, f, FRAME_INK);
    graticule(st->surface, r, m);
    annotations(st, f, r);
    if (!m->fresh) return;
    if (!m->sweep && m->rate == 1) {
        /* 40 MS/s is flat over its middle 24 MHz alone: the rest shaded. */
        int a = x_of(r, m, m->tune - 12.0), b = x_of(r, m, m->tune + 12.0);
        if (a > r.x)
            tiku_veil(st->surface, (tiku_rect_t){r.x, r.y, a - r.x, r.h},
                      TIKU_RGB(0, 0, 0), 0.05f);
        if (b < r.x + r.w)
            tiku_veil(st->surface, (tiku_rect_t){b, r.y, r.x + r.w - b, r.h},
                      TIKU_RGB(0, 0, 0), 0.05f);
    }
    marker_lines(st->surface, r, m);
    tiku_clip_set(st->surface, r);
    for (int k = SDR_TRACES - 1; k >= 0; k--) {
        const float *v = sdr_trace(m, m->type[k]);
        if (v) trace(st->surface, r, m, v, trace_ink[k]);
    }
    int row = sdr_history_row(m, m->wf_selected);
    if (sdr_reference_matches(st)) trace(st->surface, r, m, st->reference.db, BLE_INK);
    if (row >= 0) {
        float history[SDR_BINS];
        for (int b = 0; b < m->n; b++) history[b] = m->wf[row][b] / 2.0f;
        trace(st->surface, r, m, history, MARK_INK);
    }
    marker_pointers(st->surface, r, m);
    tiku_clip_reset(st->surface);
    readout(st->surface, r, m);
    if (st->reference.valid && st->reference.visible) {
        char text[160];
        snprintf(text, sizeof text, "Ref: %.60s · %s", st->reference.name,
                 sdr_reference_matches(st) ? "relative dB; gain/device may differ" : "hidden: acquisition grid differs");
        tiku_text_line_draw(st->surface,
                            (tiku_rect_t){r.x + 4, r.y + r.h - (row >= 0 ? 40 : 20), r.w - 8, 18},
                            TIKU_MEASURE_PLAIN, text, BLE_INK);
    }
    if (row >= 0) {
        char text[96];
        snprintf(text, sizeof text, "History overlay: %.3f s ago",
                 m->wf_at[m->wf_head] - m->wf_at[row]);
        tiku_text_line_draw(st->surface,
            (tiku_rect_t){r.x + 6, r.y + r.h - 20, r.w - 12, 18},
            TIKU_MEASURE_PLAIN, text, MARK_INK);
    }
}

/** @brief Paint retained captures from the history offset, with age labels,
 *         gaps, the selected row, and the independent colour scale. */
static void spectrogram(sdr_t *st, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    size_t need = (size_t)r.w * (size_t)r.h;
    float lo = m->floor_db * 2.0f, span = (m->ceiling_db - m->floor_db) * 2.0f;
    tiku_rgb_t lut[256];

    if (r.w <= 0 || r.h <= 0) return;
    for (int y = 0; y < r.h; y++)       /* the scale: highest at the top */
        tiku_hline(st->surface, r.x - 22, r.y + y, 12,
                   sdr_palette(1.0f - (float)y / (float)(r.h - 1)));
    tiku_frame(st->surface, (tiku_rect_t){r.x - 23, r.y - 1, 14, r.h + 2},
               FRAME_INK);
    for (int v = 0; v < 256; v++) lut[v] = sdr_palette(((float)v - lo) / span);
    if (need > st->pic_n) {
        uint32_t *p = realloc(st->pic, need * sizeof *p);
        if (!p) return;
        st->pic = p;
        st->pic_n = need;
    }
    for (int y = 0; y < r.h; y++) {
        int at = sdr_history_row(m, m->wf_offset + y);
        for (int x = 0; x < r.w; x++) {
            tiku_rgb_t c = at >= 0 && m->fresh ?
                           lut[m->wf[at][x * m->n / r.w]] : sdr_palette(0.0f);
            st->pic[(size_t)y * (size_t)r.w + (size_t)x] = 0xFF000000u | c;
        }
    }
    tiku_picture(st->surface, r.x, r.y, r.w, r.h, st->pic);
    tiku_frame(st->surface, (tiku_rect_t){r.x - 1, r.y - 1, r.w + 2, r.h + 2},
               FRAME_INK);
    int rows = m->wf_count - m->wf_offset;
    if (rows > r.h) rows = r.h;
    for (int y = 0; y < rows; y++) {
        int at = sdr_history_row(m, m->wf_offset + y);
        int newer = sdr_history_row(m, m->wf_offset + y - 1);
        if (newer >= 0 && m->wf_at[newer] - m->wf_at[at] > 2.0)
            for (int x = 0; x < r.w; x += 8)
                tiku_hline(st->surface, r.x + x, r.y + y, 3, GRID_INK);
        if (m->wf_offset + y == m->wf_selected)
            tiku_hline(st->surface, r.x, r.y + y, r.w, TIKU_C_SELECT);
        if (y == 0 || (rows > 48 && y == rows / 2) ||
            (rows > 40 && y == rows - 18)) {
            char text[48];
            if (m->wf_offset + y == 0) snprintf(text, sizeof text, "Latest");
            else snprintf(text, sizeof text, "-%.2fs",
                          m->wf_at[m->wf_head] - m->wf_at[at]);
            tiku_rect_t label = {r.x + 3, r.y + y, 84, 16};
            tiku_fill(st->surface, label, TIKU_RGB(0, 0, 50));
            tiku_text_line_draw(st->surface, label, TIKU_MEASURE_PLAIN,
                               text, TIKU_RGB(255, 255, 255));
        }
    }
}

/** @brief A section's bar across the panel, its name in bold. */
static void section_bar(sdr_t *st, int sec)
{
    tiku_surface_t *s = st->surface;
    const tiku_font_t *f = tiku_font_bold();
    int y = section_y(st, sec);

    tiku_fill(s, (tiku_rect_t){PANEL_X, y, PANEL_W, HEAD_H},
              tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_hline(s, PANEL_X, y + HEAD_H - 1, PANEL_W,
               tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    for (int i = 0; i < 4; i++) {
        if (st->collapsed & (1u << sec))
            tiku_vline(s, PANEL_X + 6 + i, y + 6 + i, 7 - 2 * i, DIM_INK);
        else
            tiku_hline(s, PANEL_X + 5 + i, y + 8 + i, 7 - 2 * i, DIM_INK);
    }
    if (tiku_dialog_focused(&st->dialog, CMD_SECTION + sec))
        tiku_frame(s, sdr_control_rect(st, CMD_SECTION + sec), TIKU_C_SELECT);
    tiku_text(s, f, PANEL_X + 17, y + (HEAD_H - f->height) / 2 + f->ascent,
              section[sec].name, TIKU_C_TEXT);
}

/** @brief The panel's bars, its rows' names and the units after them. */
static void panel_paint(sdr_t *st)
{
    for (int sec = 0; sec < SECTIONS; sec++) {
        section_bar(st, sec);
        if (st->collapsed & (1u << sec)) continue;
        for (int i = 0; i < section[sec].rows && i < 8; i++)
            if (row_name[sec][i][0])
                tiku_text_line_draw(st->surface, row_at(st, sec, i, PANEL_X + 2,
                                    LABEL_W - 4), TIKU_MEASURE_PLAIN,
                                    row_name[sec][i], TIKU_C_TEXT);
    }
    for (int id = CMD_FIELD + SDR_F_CENTER; id <= CMD_FIELD + SDR_F_MARK;
         id++) {
        tiku_rect_t r = sdr_control_rect(st, id);
        if (id == CMD_FIELD + SDR_F_AVG || r.w <= 0) continue;
        tiku_text_line_draw(st->surface, (tiku_rect_t){r.x + r.w + 4, r.y,
                            30, r.h}, TIKU_MEASURE_PLAIN, "MHz", TIKU_C_TEXT);
    }
}

void sdr_field_text(const sdr_t *st, int i, char *out, size_t size)
{
    if (i == SDR_F_TX_FREQ) snprintf(out, size, "%d", st->tx.mhz);
    else if (i == SDR_F_TX_OFFSET) snprintf(out, size, "%.3f", st->tx.cycles * 78.125);
    else if (i == SDR_F_TX_AMP) snprintf(out, size, "%d", st->tx.amplitude);
    else if (i == SDR_F_TX_MS) snprintf(out, size, "%d", st->tx.ms);
    else if (i == SDR_F_THRESHOLD) snprintf(out, size, "%.1f", (double)st->occupancy.threshold);
    else if (i == SDR_F_PRE) snprintf(out, size, "%d", st->trigger.pre_seconds);
    else if (i == SDR_F_POST) snprintf(out, size, "%d", st->trigger.post_seconds);
    else if (i == SDR_F_BOOK) snprintf(out, size, "%s", st->book_label);
    else if (i == SDR_F_GAIN) {
        if (st->gain_known != 1 || st->gain_value < 0) out[0] = '\0';
        else snprintf(out, size, "%d", st->gain_value);
    } else sdr_model_field(&st->model, i, out, size);
}

/** @brief Draw field @p i: while it has the focus, the text being typed,
 *         with its caret; otherwise its current value. */
static void field(sdr_t *st, int i, tiku_rect_t r, unsigned flags)
{
    char t[32];

    if (st->dialog.focus == CMD_FIELD + i && !(flags & TIKU_S_DISABLED)) {
        const tiku_field_t *e = &st->field[i];
        tiku_ui_textfield_scroll(st->surface, r, e->text, e->caret, e->sel_a,
                                 e->sel_b, flags | TIKU_S_FOCUS,
                                 st->input[i].scroll);
    } else {
        sdr_field_text(st, i, t, sizeof t);
        tiku_ui_textfield(st->surface, r, t, -1, flags & ~TIKU_S_FOCUS);
    }
}

/** @brief The bar's two glyphs: a red dot to record, a square while it
 *         records; two bars to pause, a triangle to resume. */
static void glyph(sdr_t *st, int id, tiku_rect_t r, unsigned flags)
{
    int cx = r.x + 12, cy = r.y + r.h / 2;
    int off = (flags & TIKU_S_PRESSED) ? 1 : 0;
    tiku_rgb_t ink = (flags & TIKU_S_DISABLED) ?
                     tiku_tint(TIKU_C_PANEL, 1.30f) : TIKU_C_TEXT;

    if (id == CMD_RECORD && st->rec.file)
        tiku_fill(st->surface, (tiku_rect_t){cx - 4 + off, cy - 4 + off, 9, 9},
                  REC_INK);
    else if (id == CMD_RECORD)
        tiku_fill_round(st->surface, (tiku_rect_t){cx - 5 + off, cy - 5 + off,
                        11, 11}, 5, TIKU_CORNER_ALL, (flags & TIKU_S_DISABLED)
                        ? ink : REC_INK);
    else if (st->paused || (st->state != SDR_RUNNING && !st->play.file))
        for (int i = 0; i < 6; i++)
            tiku_vline(st->surface, cx - 3 + i + off, cy - 6 + i + off,
                       13 - 2 * i, ink);
    else {
        tiku_fill(st->surface, (tiku_rect_t){cx - 4 + off, cy - 5 + off, 3,
                  11}, ink);
        tiku_fill(st->surface, (tiku_rect_t){cx + 1 + off, cy - 5 + off, 3,
                  11}, ink);
    }
}

/** @brief Name the acquisition mode without implying a connection state. */
static void live_button(sdr_t *st, tiku_rect_t r, unsigned flags)
{
    tiku_ui_button(st->surface, r, sdr_label(st, CMD_LIVE), flags);
}

/** @brief The panel's controls and the bar's buttons, each in its state:
 *         a checkbox or switch is on while its setting is. */
static void controls(sdr_t *st)
{
    const sdr_model_t *m = &st->model;

    for (int i = 0; i < st->dialog.count; i++) {
        int id = st->dialog.control[i].id;
        if (id >= CMD_SECTION && id < CMD_SECTION + SECTIONS) continue;
        tiku_rect_t r = st->dialog.control[i].rect;
        unsigned flags =
            tiku_dialog_pressed(&st->dialog, id) ? TIKU_S_PRESSED : 0;
        if (tiku_dialog_focused(&st->dialog, id)) flags |= TIKU_S_FOCUS;
        if (!st->dialog.control[i].enabled) flags |= TIKU_S_DISABLED;
        if (id >= CMD_FIELD && id < CMD_FIELD + SDR_FIELDS) {
            field(st, id - CMD_FIELD, r, flags);
        } else if (id == CMD_CHOICE + SDR_C_RBW) {
            char a[24], label[32];          /* the list's rows say more */
            sdr_mhz_text(SDR_HANN_RBW * sdr_model_bin_khz(m), a, sizeof a);
            if (st->choice[SDR_C_RBW].selected > 0)
                snprintf(label, sizeof label, "RBW %sk", a);
            else
                snprintf(label, sizeof label, "RBW Auto");
            tiku_choice_draw(&st->choice[SDR_C_RBW], st->surface, r, label,
                             flags);
        } else if (id >= CMD_CHOICE && id < CMD_CHOICE + SDR_CHOICES) {
            tiku_choice_draw(&st->choice[id - CMD_CHOICE], st->surface, r,
                             NULL, flags);
        } else if (id == CMD_MARK_ACTIVE || id == CMD_TRACK) {
            if (id == CMD_TRACK ? m->track : m->mark[m->mark_sel].on)
                flags |= TIKU_S_ON;
            tiku_ui_checkbox(st->surface, r, sdr_label(st, id), flags);
        } else if (id == CMD_WATERFALL ||
                   id == CMD_SCALE_AUTO || id == CMD_COLOR_AUTO ||
                   id == CMD_GAIN_AUTO) {
            if (id == CMD_WATERFALL ? st->fall_on :
                id == CMD_SCALE_AUTO ? !m->manual_scale :
                id == CMD_COLOR_AUTO ? !m->manual_color :
                st->gain_known == 1 && st->gain_value < 0)
                flags |= TIKU_S_ON;
            tiku_ui_switch(st->surface, r, sdr_label(st, id), flags);
        } else if (id == CMD_LIVE) {
            live_button(st, r, flags);
        } else if (id == CMD_RECORD || id == CMD_PAUSE) {
            tiku_ui_button(st->surface, r, "", flags);
            glyph(st, id, r, flags);
            tiku_text_line_draw(st->surface,
                (tiku_rect_t){r.x + 23, r.y, r.w - 25, r.h},
                TIKU_MEASURE_PLAIN, id == CMD_RECORD ?
                (st->rec.file ? "Stop" : "Rec") : sdr_label(st, id),
                flags & TIKU_S_DISABLED ? DIM_INK : TIKU_C_TEXT);
        } else if (id == CMD_FFT || id == CMD_RATE || id == CMD_OCC_PLAN) {
            char label[80];
            if (id == CMD_FFT) snprintf(label, sizeof label, "FFT: %s%d", m->fft_override ? "" : "Auto / ", m->nfft);
            else if (id == CMD_RATE) snprintf(label, sizeof label, "Rate: %s%d MS/s", m->rate_override ? "" : "Auto / ",
                                             (int[]){80,40,20,10,8,4}[m->rate]);
            else snprintf(label, sizeof label, "%s channels", (const char *[]){"Wi-Fi", "BLE", "802.15.4"}[st->occupancy.plan]);
            tiku_ui_button(st->surface, r, label, flags);
        } else {
            tiku_ui_button(st->surface, r, sdr_label(st, id), flags);
        }
    }
}

/** @brief How far a recording has come: being made, or being played. */
static void tape_progress(const sdr_t *st, char *out, size_t max)
{
    char at[16], end[16];

    out[0] = '\0';
    if (st->rec.file) {
        sdr_tape_clock((double)(st->now - st->rec.began) / 1e6, at, sizeof at);
        sdr_tape_size(st->rec.bytes, end, sizeof end);
        snprintf(out, max, st->trigger.armed ? " · trigger ARMED, %s" :
                 " · recording %s, %s", at, end);
    } else if (st->play.file) {
        sdr_tape_clock((double)st->play.clock / 1e6, at, sizeof at);
        sdr_tape_clock(st->play.total, end, sizeof end);
        snprintf(out, max, " · %s of %s", at, end);
    }
}

/** @brief Keep connection and recording status separate from pointer readings. */
static void status(sdr_t *st)
{
    char t[400], progress[64];
    int recording_view = st->play.file || (st->shown[0] && st->state == SDR_IDLE);
    const char *state = recording_view ? "Recording" :
        st->state == SDR_RUNNING ? "Connected" :
        st->state == SDR_IDLE ? "Disconnected" :
        st->state >= SDR_STOPPING ? "Returning device" : "Connecting";

    tape_progress(st, progress, sizeof progress);
    snprintf(t, sizeof t, "%s%s%s · %s%s · Relative dB (uncalibrated)",
             state, !recording_view && st->board[0] ? ": " : "",
             recording_view ? "" : st->board,
             recording_view && !st->play.file ? "Finished" :
             st->paused ? "Paused" : st->state == SDR_RUNNING || st->play.file ?
             "Running" : "Idle", progress);
    tiku_hline(st->surface, 0, st->h - BAR_H, st->w,
               tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    tiku_text_line_draw(st->surface,
                        (tiku_rect_t){PANEL_X, st->h - 48,
                                      st->w - 16, CTL_H},
                        TIKU_MEASURE_PLAIN, t, TIKU_C_TEXT);
    int note_w = st->hover[0] && st->shown[0] ? st->w / 3 : st->w - 16;
    tiku_text_line_draw(st->surface,
        (tiku_rect_t){PANEL_X, st->h - 24, note_w, CTL_H},
        TIKU_MEASURE_PLAIN, st->note, TIKU_C_TEXT);
    if (note_w != st->w - 16)
        tiku_text_line_draw(st->surface,
            (tiku_rect_t){PANEL_X + note_w + 8, st->h - 24,
                         st->w - note_w - 32, CTL_H},
            TIKU_MEASURE_PLAIN, st->hover, TIKU_C_TEXT);
    tiku_rect_t r = sdr_timeline_rect(st);
    if (st->play.file || (st->shown[0] && st->state == SDR_IDLE)) {
        double ratio = st->play.total > 0 ?
            (st->play.seeking ? st->play.seek_to : st->play.clock / 1e6) /
            st->play.total : 0;
        if (ratio < 0) ratio = 0;
        if (ratio > 1) ratio = 1;
        tiku_fill(st->surface, r, PLOT_BG);
        tiku_fill(st->surface, (tiku_rect_t){r.x, r.y,
            (int)(r.w * ratio), r.h}, tiku_tint(TIKU_C_SELECT, 0.5f));
        tiku_frame(st->surface, r, FRAME_INK);
        if (st->play.total > 0) {
            for (int i = 0; i < st->book_count; i++)
                tiku_vline(st->surface, r.x + (int)((r.w - 1) * st->books[i].at / st->play.total), r.y, 5, REC_INK);
            if (st->play.loop_b > st->play.loop_a) {
                int left = r.x + (int)((r.w - 1) * st->play.loop_a / st->play.total);
                int right = r.x + (int)((r.w - 1) * st->play.loop_b / st->play.total);
                tiku_hline(st->surface, left, r.y + r.h - 3, right - left + 1, MARK_INK);
            }
        }
        snprintf(t, sizeof t, "%s · %.2gx · %s", st->play.seeking ?
                 "Seeking" : st->play.loop ? "Looping" : "Click to seek", st->play.speed,
                 st->play.skip_gaps ? "skip gaps" : "original timing");
        tiku_text_line_draw(st->surface, r, TIKU_MEASURE_PLAIN, t, TIKU_C_TEXT);
    } else {
        char health[200];
        sdr_health_text(st, health, sizeof health);
        tiku_text_line_draw(st->surface, r, TIKU_MEASURE_PLAIN,
                           st->hover[0] ? st->hover :
                           st->health_visible ? health :
                           "Wheel: zoom · Shift-drag: range · Middle-drag: pan",
                           TIKU_C_TEXT);
    }
}

/** @brief Activity bars replace the waterfall, with unobserved channels explicit. */
static void occupancy_paint(sdr_t *st, tiku_rect_t r)
{
    const sdr_occupancy_t *o = &st->occupancy;
    int first = o->plan == 0 ? 0 : o->plan == 1 ? 13 : 53;
    int count = o->plan == 0 ? 13 : o->plan == 1 ? 40 : 16;
    int rows = (r.h - 42) / 18;
    if (r.w <= 0 || rows < 1) return;
    int cols = r.w / 125;
    if (cols < 1) cols = 1;
    if (cols > (count + rows - 1) / rows) cols = (count + rows - 1) / rows;
    int width = r.w / cols, per_page = cols * rows;
    int pages = (count + per_page - 1) / per_page;
    st->occupancy.page %= pages;
    int offset = st->occupancy.page * per_page;
    char text[200];
    double span = 0;
    uint64_t observations = 0;
    for (int i = first; i < first + count; i++) {
        if (o->seen[i] > observations) observations = o->seen[i];
        if (o->last[i] - o->first[i] > span) span = o->last[i] - o->first[i];
    }
    tiku_fill(st->surface, r, PLOT_BG);
    tiku_frame(st->surface, r, FRAME_INK);
    tiku_clip_set(st->surface, r);
    snprintf(text, sizeof text, "Observed activity >= %.1f relative dB · %.1f s · up to %llu observations",
             (double)o->threshold, span, (unsigned long long)observations);
    tiku_text_line_draw(st->surface, (tiku_rect_t){r.x + 5, r.y + 2, r.w - 10, 18},
                        TIKU_MEASURE_PLAIN, text, NOTE_INK);
    for (int j = 0; j < per_page && offset + j < count; j++) {
        int i = first + offset + j, x = r.x + (j / rows) * width + 5, y = r.y + 23 + (j % rows) * 18;
        double center, bandwidth, percent = o->seen[i] ? 100.0 * o->busy[i] / o->seen[i] : 0;
        char name[24];
        sdr_channel(i, name, sizeof name, &center, &bandwidth);
        if (o->seen[i]) snprintf(text, sizeof text, "%s  %.0f%%", name, percent);
        else snprintf(text, sizeof text, "%s  n/a", name);
        tiku_fill(st->surface, (tiku_rect_t){x, y + 1, (int)((width - 10) * percent / 100), 15},
                  TIKU_RGB(204, 224, 245));
        tiku_text_line_draw(st->surface, (tiku_rect_t){x, y, width - 10, 17},
                            TIKU_MEASURE_PLAIN, text, o->seen[i] ? NOTE_INK : DIM_INK);
    }
    snprintf(text, sizeof text, "Page %d/%d · n/a = not fully observed · Observations, not airtime or decoded traffic",
             st->occupancy.page + 1, pages);
    tiku_text_line_draw(st->surface, (tiku_rect_t){r.x + 5, r.y + r.h - 18, r.w - 10, 17},
                        TIKU_MEASURE_PLAIN, text, DIM_INK);
    tiku_clip_reset(st->surface);
}

void sdr_paint(sdr_t *st)
{
    tiku_rect_t plot, fall, screen = {0, 0, st->w, st->h};

    if (!st->id) return;                /* the window is not open */
    sdr_layout(st, &plot, &fall);
    sdr_controls(st);
    sdr_model_scale(&st->model);
    tiku_fill(st->surface, screen, TIKU_C_PANEL);
    panel_paint(st);
    controls(st);
    if (st->occupancy.visible) occupancy_paint(st, fall);
    else spectrogram(st, fall);
    if (fall.h > 0)
        for (int dx = -8; dx <= 8; dx += 4)
            tiku_hline(st->surface, fall.x + fall.w / 2 + dx,
                       fall.y + fall.h + 3, 2, FRAME_INK);
    spectrum(st, plot);
    if (st->nav_drag) {
        tiku_rect_t selection = plot;
        if (st->nav_drag == 1) {
            selection.x = st->nav_x < st->nav_anchor ? st->nav_x : st->nav_anchor;
            selection.w = abs(st->nav_x - st->nav_anchor) + 1;
        }
        tiku_clip_set(st->surface, plot);
        tiku_veil(st->surface, selection, TIKU_C_SELECT, 0.15f);
        tiku_frame(st->surface, selection, TIKU_C_SELECT);
        tiku_clip_reset(st->surface);
        if (fall.h) {
            selection.y = fall.y;
            selection.h = fall.h;
            tiku_clip_set(st->surface, fall);
            tiku_veil(st->surface, selection, TIKU_C_SELECT, 0.15f);
            tiku_frame(st->surface, selection, TIKU_C_SELECT);
            tiku_clip_reset(st->surface);
        }
    }
    status(st);
    for (int c = 0; c < SDR_CHOICES; c++)
        if (st->choice[c].open)
            tiku_choice_draw_popup(&st->choice[c], st->surface,
                                   sdr_control_rect(st, CMD_CHOICE + c),
                                   screen);
    if (st->alert.open)
        tiku_alert_draw(&st->alert, st->surface, sdr_alert_rect(st));
    st->services->frame(st->services->ctx, st->id, st->surface->px, st->w,
                        st->h);
    st->dirty = 0;
}

int sdr_picture(sdr_t *st, const char *path)
{
    tiku_rect_t plot, fall, area;
    tiku_surface_t *pic;
    int sc, rc;

    st->hover[0] = '\0';                /* no hover text in the picture */
    sdr_paint(st);
    sdr_layout(st, &plot, &fall);
    area = (tiku_rect_t){AREA_X, 2, st->w - AREA_X - 4, st->h - BAR_H - 4};
    sc = st->surface->scale > 0 ? st->surface->scale : 1;
    pic = tiku_surface_new(area.w * sc, area.h * sc, PLOT_BG);
    if (!pic) {
        snprintf(st->note, sizeof st->note, "No memory for a picture.");
        return -1;
    }
    for (int y = 0; y < pic->h; y++)
        memcpy(pic->px + (size_t)y * (size_t)pic->w,
               st->surface->px + ((size_t)(area.y * sc + y) *
                                  (size_t)(st->w * sc) + (size_t)(area.x * sc)),
               sizeof *pic->px * (size_t)pic->w);
    rc = tiku_surface_png(pic, path);
    tiku_surface_free(pic);
    if (rc == 0)
        snprintf(st->note, sizeof st->note, "Saved the picture as %s.",
                 sdr_tape_leaf(path));
    else
        snprintf(st->note, sizeof st->note, "Cannot write %s.",
                 sdr_tape_leaf(path));
    st->dirty = 1;
    return rc == 0 ? 0 : -1;
}
