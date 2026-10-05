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
#define ZB_INK     TIKU_RGB(60, 140, 60)
#define WIFI_INK   TIKU_RGB(60, 110, 180)
#define REC_INK    TIKU_RGB(214, 48, 49)
#define LIVE_ON    TIKU_RGB(40, 186, 72)
#define LIVE_OFF   TIKU_RGB(150, 150, 150)

#define PANEL_X    8
#define PANEL_W    200
#define LABEL_W    66               /* a row's name, before its control */
#define HEAD_H     20               /* a section's bar                  */
#define ROW_H      24
#define CTL_H      22               /* a field, list or button in a row */
#define AREA_X     (PANEL_X + PANEL_W + 10)  /* the display area's left edge */
#define AXIS_W     52               /* the level scale, left of the grid */
#define BAR_H      30               /* the bar at the window's foot     */
#define NOTE_H     16               /* an annotation row                */
#define RULER      12               /* a channel plan's row             */

/* The traces' colours, by trace: black, red and blue, on white. */
static const tiku_rgb_t trace_ink[SDR_TRACES] = {
    TIKU_RGB(16, 16, 16), TIKU_RGB(210, 50, 40), TIKU_RGB(30, 100, 210)
};
static const char *const type_short[SDR_T_TYPES] = {
    "Off", "Clr/Wr", "Avg", "Max", "Min"
};

/* The panel's sections: a bar, then rows. */
enum { S_FREQ, S_TRACE, S_MARK, S_SHOW, SECTIONS };
static const struct { const char *name; int rows; } section[SECTIONS] = {
    {"Frequency", 5}, {"Traces", 4}, {"Markers", 7}, {"Display", 2}
};
/* Each row's name, by section, "" for none. */
static const char *const row_name[SECTIONS][7] = {
    {"Center", "Span", "Start", "Stop", "", "", ""},
    {"Trace", "Type", "Avg count", "", "", "", ""},
    {"Marker", "On trace", "", "Freq", "", "", ""},
    {"", "", "", "", "", "", ""}
};

/** @brief Section @p s's bar's top. */
static int section_y(int s)
{
    int y = 6;

    for (int i = 0; i < s; i++) y += HEAD_H + 4 + section[i].rows * ROW_H + 6;
    return y;
}

/** @brief Row @p row of section @p s, from @p x, @p w wide. */
static tiku_rect_t row_at(int s, int row, int x, int w)
{
    return (tiku_rect_t){x, section_y(s) + HEAD_H + 4 + row * ROW_H + 1, w,
                         CTL_H};
}

/** @brief Button @p col of @p cols sharing row @p row of section @p s. */
static tiku_rect_t button_at(int s, int row, int col, int cols)
{
    int w = (PANEL_W - 4 * (cols - 1)) / cols;

    return row_at(s, row, PANEL_X + col * (w + 4), w);
}

tiku_rect_t sdr_control_rect(const sdr_t *st, int id)
{
    int x = PANEL_X + LABEL_W, w = PANEL_W - LABEL_W, fw = w - 30;
    int bar = st->h - BAR_H + 4;

    switch (id) {
    case CMD_FIELD + SDR_F_CENTER: return row_at(S_FREQ, 0, x, fw);
    case CMD_FIELD + SDR_F_SPAN:   return row_at(S_FREQ, 1, x, fw);
    case CMD_FIELD + SDR_F_START:  return row_at(S_FREQ, 2, x, fw);
    case CMD_FIELD + SDR_F_STOP:   return row_at(S_FREQ, 3, x, fw);
    case CMD_LIVE: return button_at(S_FREQ, 4, 0, 1);
    case CMD_CHOICE + SDR_C_TRACE: return row_at(S_TRACE, 0, x, w);
    case CMD_CHOICE + SDR_C_TYPE:  return row_at(S_TRACE, 1, x, w);
    case CMD_FIELD + SDR_F_AVG:    return row_at(S_TRACE, 2, x, 48);
    case CMD_CLEAR:  return button_at(S_TRACE, 3, 0, 2);
    case CMD_EXPORT: return button_at(S_TRACE, 3, 1, 2);
    case CMD_CHOICE + SDR_C_MARKER: return row_at(S_MARK, 0, x, w);
    case CMD_CHOICE + SDR_C_ON: return row_at(S_MARK, 1, x, w);
    case CMD_MARK_ACTIVE: return row_at(S_MARK, 2, x, 64);
    case CMD_TRACK: return row_at(S_MARK, 2, x + 70, w - 70);
    case CMD_FIELD + SDR_F_MARK: return row_at(S_MARK, 3, x, fw);
    case CMD_PEAK_SEARCH: return button_at(S_MARK, 4, 0, 2);
    case CMD_NEXT_PEAK:   return button_at(S_MARK, 4, 1, 2);
    case CMD_PEAK_LEFT:   return button_at(S_MARK, 5, 0, 2);
    case CMD_PEAK_RIGHT:  return button_at(S_MARK, 5, 1, 2);
    case CMD_MARK_CENTER: return button_at(S_MARK, 6, 0, 2);
    case CMD_MARK_CLEAR:  return button_at(S_MARK, 6, 1, 2);
    case CMD_WATERFALL: return row_at(S_SHOW, 0, PANEL_X + 4, PANEL_W - 8);
    case CMD_PLANS:     return row_at(S_SHOW, 1, PANEL_X + 4, PANEL_W - 8);
    case CMD_RECORD: return (tiku_rect_t){PANEL_X, bar, 30, CTL_H};
    case CMD_PAUSE:  return (tiku_rect_t){PANEL_X + 34, bar, 30, CTL_H};
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
    int fh = st->fall_on ? (bottom - top) * 36 / 100 : 0;
    int ft = st->fall_on ? top + fh + 6 : top;  /* the frame's top */
    int py = ft + 2 * NOTE_H + 6;
    int plans = st->plans_on ? 3 * RULER + 6 : 0;

    *fall = (tiku_rect_t){x, top, w, fh};
    *plot = (tiku_rect_t){x, py, w, bottom - 2 * NOTE_H - 6 - plans - py};
}

const char *sdr_label(const sdr_t *st, int command)
{
    switch (command) {
    case CMD_CONNECT:
        return st->state == SDR_IDLE ? "Connect" :
               st->state >= SDR_STOPPING ? "Returning" : "Disconnect";
    case CMD_LIVE: return "Live";
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
    case CMD_PLANS: return "Channel plans";
    case CMD_PAUSE: return st->paused ? "Resume" : "Pause";
    case CMD_RECORD: return st->rec.file ? "Recording" : "Record";
    case CMD_REPLAY: return "Open Recording";
    case CMD_PICTURE: return "Save Picture";
    case CMD_PEAK: return "Max Hold";
    case CMD_OVERLAY: return "Clear & Write";
    case CMD_MARK_REMOVE: return "Marker Off";
    case CMD_ABOUT: return "About TikuSDR";
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
    if (command == CMD_FIELD + SDR_F_MARK) return out;
    if (command >= CMD_FIELD && command < CMD_FIELD + SDR_F_AVG)
        return !st->play.file;          /* a recording playing sets the view */
    switch (command) {
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

/** @brief Set each list that is not open to show what it sets, and enable
 *         the lists as sdr_enabled() says. */
static void sync_choices(sdr_t *st)
{
    const sdr_model_t *m = &st->model;
    int value[SDR_CHOICES] = {st->trace_sel, m->type[st->trace_sel],
                              m->mark_sel, m->mark[m->mark_sel].trace};

    for (int c = 0; c < SDR_CHOICES; c++) {
        tiku_choice_t *ch = &st->choice[c];
        if (!ch->open) ch->selected = ch->hot = value[c];
        ch->enabled = sdr_enabled(st, CMD_CHOICE + c);
    }
}

void sdr_controls(sdr_t *st)
{
    /* The bar's buttons come first: a focus the dialog places itself goes
     * to the first enabled control, Pause, so Space pauses and keys are
     * not typed into a field.  Then each section from the top, in the
     * order Tab takes them. */
    static const struct { int id; tiku_dialog_kind_t kind; } ctl[] = {
        {CMD_PAUSE, TIKU_DIALOG_BUTTON}, {CMD_RECORD, TIKU_DIALOG_BUTTON},
        {CMD_FIELD + SDR_F_CENTER, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_SPAN, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_START, TIKU_DIALOG_INPUT},
        {CMD_FIELD + SDR_F_STOP, TIKU_DIALOG_INPUT},
        {CMD_LIVE, TIKU_DIALOG_BUTTON},
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
        {CMD_WATERFALL, TIKU_DIALOG_TOGGLE}, {CMD_PLANS, TIKU_DIALOG_TOGGLE}
    };

    sync_choices(st);
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, CMD_QUIT);
    for (size_t i = 0; i < sizeof ctl / sizeof ctl[0]; i++)
        tiku_dialog_add(&st->dialog, ctl[i].id, sdr_control_rect(st,
                        ctl[i].id), ctl[i].kind, sdr_enabled(st, ctl[i].id));
    tiku_dialog_end(&st->dialog);
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

/** @brief One channel plan's row beneath the graticule: its name at the
 *         left, a tick and a number at each channel. */
static void ruler(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m,
                  int y, const char *name, const int *num, const double *mhz,
                  int count, tiku_rgb_t ink)
{
    const tiku_font_t *f = tiku_font_plain();
    char t[8];

    tiku_text(s, f, AREA_X + 6, y + 10, name, DIM_INK);
    for (int i = 0; i < count; i++) {
        int x = x_of(r, m, mhz[i]);
        if (x < r.x || x >= r.x + r.w) continue;
        snprintf(t, sizeof t, "%d", num[i]);
        tiku_vline(s, x, y, 3, ink);
        tiku_text(s, f, x - tiku_text_width(f, t) / 2, y + 12, t, ink);
    }
}

/** @brief The band's channel plans beneath the graticule. */
static void rulers(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m)
{
    int num[16];
    double mhz[16];
    int y = r.y + r.h + 3;

    for (int c = 1; c <= 13; c++) {
        num[c - 1] = c;
        mhz[c - 1] = 2407.0 + 5.0 * c;
    }
    ruler(s, r, m, y, "Wi-Fi", num, mhz, 13, WIFI_INK);
    num[0] = 37; mhz[0] = 2402.0;
    num[1] = 38; mhz[1] = 2426.0;
    num[2] = 39; mhz[2] = 2480.0;
    ruler(s, r, m, y + RULER, "BLE", num, mhz, 3, BLE_INK);
    for (int z = 11; z <= 26; z++) {
        num[z - 11] = z;
        mhz[z - 11] = 2405.0 + 5.0 * (z - 11);
    }
    ruler(s, r, m, y + 2 * RULER, "15.4", num, mhz, 16, ZB_INK);
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
    int top = f.y + 2, foot = r.y + r.h + (st->plans_on ? 3 * RULER + 6 : 0)
                              + 3, cx = r.x + r.w / 2, right = f.x + f.w - 8;
    char t[96], a[24];

    snprintf(t, sizeof t, "Ref %.0f dB", (double)m->scale_hi);
    note(s, f.x + 8, top, 1, t, NOTE_INK);
    snprintf(t, sizeof t, "Div %.0f dB", (double)m->div);
    note(s, f.x + 8, top + NOTE_H, 1, t, NOTE_INK);
    sdr_mhz_text(m->bin_mhz * 1000.0, a, sizeof a);
    snprintf(t, sizeof t, "RBW %s kHz", a);
    note(s, cx, top, 0, t, NOTE_INK);
    if (m->sweep)
        snprintf(t, sizeof t, "%d slices at 40 MS/s",
                 (m->last - m->first) / SDR_SLICE_STEP + 1);
    else
        snprintf(t, sizeof t, "%g MS/s", m->bin_mhz * m->nfft);
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
 *         middle, BLE's advertising channels and the channel plans when
 *         shown, then the traces and the markers. */
static void spectrum(sdr_t *st, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    static const double adv[3] = {2402.0, 2426.0, 2480.0};
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
    if (st->plans_on) {
        for (int i = 0; i < 3; i++) {
            int x = x_of(r, m, adv[i]);
            if (x >= r.x && x < r.x + r.w)
                for (int y = r.y; y < r.y + r.h; y += 6)
                    tiku_vline(st->surface, x, y, 3, BLE_INK);
        }
        rulers(st->surface, r, m);
    }
    marker_lines(st->surface, r, m);
    tiku_clip_set(st->surface, r);
    for (int k = SDR_TRACES - 1; k >= 0; k--) {
        const float *v = sdr_trace(m, m->type[k]);
        if (v) trace(st->surface, r, m, v, trace_ink[k]);
    }
    marker_pointers(st->surface, r, m);
    tiku_clip_reset(st->surface);
    readout(st->surface, r, m);
}

/** @brief The spectrogram: newest at the top, one row a spectrum, coloured
 *         from just under the noise to the graticule's top, its colour
 *         scale at its left. */
static void spectrogram(sdr_t *st, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    size_t need = (size_t)r.w * (size_t)r.h;
    float lo = m->floor_db * 2.0f, span = (m->scale_hi - m->floor_db) * 2.0f;
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
        const unsigned char *row =
            m->wf[(m->wf_head - y + SDR_ROWS) % SDR_ROWS];
        for (int x = 0; x < r.w; x++) {
            tiku_rgb_t c = y < m->wf_count && m->fresh ?
                           lut[row[x * m->n / r.w]] : sdr_palette(0.0f);
            st->pic[(size_t)y * (size_t)r.w + (size_t)x] = 0xFF000000u | c;
        }
    }
    tiku_picture(st->surface, r.x, r.y, r.w, r.h, st->pic);
    tiku_frame(st->surface, (tiku_rect_t){r.x - 1, r.y - 1, r.w + 2, r.h + 2},
               FRAME_INK);
}

/** @brief A section's bar across the panel, its name in bold. */
static void section_bar(tiku_surface_t *s, int sec)
{
    const tiku_font_t *f = tiku_font_bold();
    int y = section_y(sec);

    tiku_fill(s, (tiku_rect_t){PANEL_X, y, PANEL_W, HEAD_H},
              tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_hline(s, PANEL_X, y + HEAD_H - 1, PANEL_W,
               tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    for (int i = 0; i < 4; i++)         /* a small triangle, point down */
        tiku_hline(s, PANEL_X + 5 + i, y + 8 + i, 7 - 2 * i, DIM_INK);
    tiku_text(s, f, PANEL_X + 17, y + (HEAD_H - f->height) / 2 + f->ascent,
              section[sec].name, TIKU_C_TEXT);
}

/** @brief The panel's bars, its rows' names and the units after them. */
static void panel_paint(sdr_t *st)
{
    for (int sec = 0; sec < SECTIONS; sec++) {
        section_bar(st->surface, sec);
        for (int i = 0; i < section[sec].rows && i < 7; i++)
            if (row_name[sec][i][0])
                tiku_text_line_draw(st->surface, row_at(sec, i, PANEL_X + 2,
                                    LABEL_W - 4), TIKU_MEASURE_PLAIN,
                                    row_name[sec][i], TIKU_C_TEXT);
    }
    for (int id = CMD_FIELD + SDR_F_CENTER; id <= CMD_FIELD + SDR_F_MARK;
         id++) {
        tiku_rect_t r = sdr_control_rect(st, id);
        if (id == CMD_FIELD + SDR_F_AVG) continue;
        tiku_text_line_draw(st->surface, (tiku_rect_t){r.x + r.w + 4, r.y,
                            30, r.h}, TIKU_MEASURE_PLAIN, "MHz", TIKU_C_TEXT);
    }
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
        sdr_model_field(&st->model, i, t, sizeof t);
        tiku_ui_textfield(st->surface, r, t, -1, flags & ~TIKU_S_FOCUS);
    }
}

/** @brief The bar's two glyphs: a red dot to record, a square while it
 *         records; two bars to pause, a triangle to resume. */
static void glyph(sdr_t *st, int id, tiku_rect_t r, unsigned flags)
{
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
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
    else if (st->paused)
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

/** @brief Draw the Live button: a round light, green while the view is
 *         one spectrum and grey while it is swept, before the word, the
 *         two centred together. */
static void live_button(sdr_t *st, tiku_rect_t r, unsigned flags)
{
    const tiku_font_t *f = tiku_font_plain();
    const char *word = sdr_label(st, CMD_LIVE);
    int off = (flags & TIKU_S_PRESSED) ? 1 : 0;
    int x = r.x + (r.w - 16 - tiku_text_width(f, word)) / 2 + off;
    int y = r.y + r.h / 2 + off;
    int dim = (flags & TIKU_S_DISABLED) != 0;

    tiku_ui_button(st->surface, r, "", flags);
    tiku_fill_round(st->surface, (tiku_rect_t){x, y - 5, 10, 10}, 5,
                    TIKU_CORNER_ALL, dim ? tiku_tint(TIKU_C_PANEL, 1.20f) :
                    sdr_model_is_live(&st->model) ? LIVE_ON : LIVE_OFF);
    tiku_text(st->surface, f, x + 16, y - f->height / 2 + f->ascent, word,
              dim ? tiku_tint(TIKU_C_PANEL, 1.30f) : TIKU_C_TEXT);
}

/** @brief The panel's controls and the bar's buttons, each in its state:
 *         a checkbox or switch is on while its setting is. */
static void controls(sdr_t *st)
{
    const sdr_model_t *m = &st->model;

    for (int i = 0; i < st->dialog.count; i++) {
        int id = st->dialog.control[i].id;
        tiku_rect_t r = st->dialog.control[i].rect;
        unsigned flags =
            tiku_dialog_pressed(&st->dialog, id) ? TIKU_S_PRESSED : 0;
        if (tiku_dialog_focused(&st->dialog, id)) flags |= TIKU_S_FOCUS;
        if (!st->dialog.control[i].enabled) flags |= TIKU_S_DISABLED;
        if (id >= CMD_FIELD && id < CMD_FIELD + SDR_FIELDS) {
            field(st, id - CMD_FIELD, r, flags);
        } else if (id >= CMD_CHOICE && id < CMD_CHOICE + SDR_CHOICES) {
            tiku_choice_draw(&st->choice[id - CMD_CHOICE], st->surface, r,
                             NULL, flags);
        } else if (id == CMD_MARK_ACTIVE || id == CMD_TRACK) {
            if (id == CMD_TRACK ? m->track : m->mark[m->mark_sel].on)
                flags |= TIKU_S_ON;
            tiku_ui_checkbox(st->surface, r, sdr_label(st, id), flags);
        } else if (id == CMD_WATERFALL || id == CMD_PLANS) {
            if (id == CMD_WATERFALL ? st->fall_on : st->plans_on)
                flags |= TIKU_S_ON;
            tiku_ui_switch(st->surface, r, sdr_label(st, id), flags);
        } else if (id == CMD_LIVE) {
            live_button(st, r, flags);
        } else if (id == CMD_RECORD || id == CMD_PAUSE) {
            tiku_ui_button(st->surface, r, "", flags);
            glyph(st, id, r, flags);
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
        snprintf(out, max, " · recording %s, %s", at, end);
    } else if (st->play.file) {
        sdr_tape_clock((double)st->play.clock / 1e6, at, sizeof at);
        sdr_tape_clock(st->play.total, end, sizeof end);
        snprintf(out, max, " · %s of %s", at, end);
    }
}

/** @brief The bar's line: what is under the pointer, or else the note,
 *         with how a recording is going, or beside it what the last
 *         recording became. */
static void status(sdr_t *st)
{
    char t[400], progress[64];

    tape_progress(st, progress, sizeof progress);
    if (st->hover[0])
        snprintf(t, sizeof t, "%s", st->hover);
    else if (st->play.file || st->rec.file)
        snprintf(t, sizeof t, "%s%s%s", st->note, progress,
                 st->paused ? " · paused" : "");
    else if (st->saved[0] && strcmp(st->note, st->saved))
        snprintf(t, sizeof t, "%s   |   %s", st->note, st->saved);
    else
        snprintf(t, sizeof t, "%s%s", st->note,
                 st->paused ? " · paused" : "");
    tiku_hline(st->surface, 0, st->h - BAR_H, st->w,
               tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    tiku_text_line_draw(st->surface,
                        (tiku_rect_t){PANEL_X + 72, st->h - BAR_H + 4,
                                      st->w - PANEL_X - 88, CTL_H},
                        TIKU_MEASURE_PLAIN, t, TIKU_C_TEXT);
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
    spectrogram(st, fall);
    spectrum(st, plot);
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
