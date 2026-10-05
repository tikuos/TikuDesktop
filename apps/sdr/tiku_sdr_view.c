/* tiku_sdr_view.c -- TikuSDR's window: a toolbar, the spectrum on an
 * instrument-dark plot with the band's channel plans beneath it, the
 * waterfall, and one status line.  Painting only; no device.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_font.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_sdr.h"

#define PLOT_BG    TIKU_RGB(14, 18, 23)
#define GRID       TIKU_RGB(38, 46, 56)
#define TRACE      TIKU_RGB(87, 211, 176)
#define TRACE_FILL TIKU_RGB(21, 60, 54)
#define PEAK       TIKU_RGB(242, 179, 61)
#define BLE_INK    TIKU_RGB(205, 104, 205)
#define ZB_INK     TIKU_RGB(104, 176, 104)
#define WIFI_INK   TIKU_RGB(96, 150, 210)
#define REC_INK    TIKU_RGB(214, 48, 49)
#define LEFT       56
#define TOP        48
#define RULER      14
#define STATUS_H   26

void sdr_layout(const sdr_t *st, tiku_rect_t *plot, tiku_rect_t *fall)
{
    int w = st->w - LEFT - 16;
    int room = st->h - TOP - 18 - 3 * RULER - 10 - STATUS_H - 8;

    *plot = (tiku_rect_t){LEFT, TOP, w, room * 45 / 100};
    *fall = (tiku_rect_t){LEFT, plot->y + plot->h + 18 + 3 * RULER + 8, w,
                          st->h - STATUS_H - 8 - (plot->y + plot->h + 18 +
                                                  3 * RULER + 8)};
}

const char *sdr_label(const sdr_t *st, int command)
{
    switch (command) {
    case CMD_CONNECT:
        return st->state == SDR_IDLE ? "Connect" :
               st->state >= SDR_STOPPING ? "Returning" : "Disconnect";
    case CMD_LIVE: return "Live";
    case CMD_BAND: return "Band";
    case CMD_DOWN: return "-5 MHz";
    case CMD_UP: return "+5 MHz";
    case CMD_PEAK: return "Peak";
    case CMD_CLEAR: return "Clear";
    case CMD_PAUSE: return st->paused ? "Resume" : "Pause";
    case CMD_RECORD: return st->rec.file ? "Recording" : "Record";
    case CMD_REPLAY: return "Open Recording";
    case CMD_EXPORT: return "Export CSV";
    case CMD_PICTURE: return "Save Picture";
    case CMD_AVERAGE: return "Average";
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
    if (st->alert.open) return command == CMD_QUIT;
    switch (command) {
    case CMD_CONNECT:                   /* Disconnect, or a radio there */
        return st->state == SDR_IDLE ? st->names[0] != '\0' :
                                       st->state < SDR_STOPPING;
    case CMD_LIVE: case CMD_BAND: case CMD_DOWN: case CMD_UP:
        return !st->play.file;
    case CMD_RECORD: return st->state == SDR_RUNNING || st->rec.file;
    case CMD_REPLAY: return st->state == SDR_IDLE;
    case CMD_EXPORT:
        return (st->model.fresh || st->shown[0]) && !st->csv.in;
    case CMD_PICTURE: return st->model.fresh;
    default: return 1;
    }
}

void sdr_controls(sdr_t *st)
{
    /* Narrow enough to end clear of the Deskbar, in the desk's top right
     * corner, where the window opens.  No Connect: the radio is borrowed
     * as the window opens, and the SDR menu keeps Connect. */
    static const int width[SDR_BUTTONS] = {60, 60, 72, 72, 64, 76, 100};
    int x = 16;

    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, CMD_QUIT);
    for (int i = 0; i < SDR_BUTTONS; i++) {
        int id = CMD_LIVE + i;

        if (id == CMD_UP) x += 112;             /* room for the tuning */
        tiku_dialog_add(&st->dialog, id, (tiku_rect_t){x, 10, width[i], 28},
                        TIKU_DIALOG_BUTTON, sdr_enabled(st, id));
        x += width[i] + 8;
    }
    tiku_dialog_end(&st->dialog);
}

/** @brief The plot's x for @p mhz. */
static int x_of(tiku_rect_t r, const sdr_model_t *m, double mhz)
{
    double lo = m->lo_mhz - m->bin_mhz / 2, hi = lo + m->n * m->bin_mhz;

    return r.x + (int)lround((mhz - lo) / (hi - lo) * (r.w - 1));
}

/** @brief The plot's y for @p db, clamped inside it. */
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

/** @brief A trace as a line of vertical runs, filled beneath on request. */
static void trace(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m,
                  const float *v, tiku_rgb_t ink, int fill)
{
    int prev = -1;

    for (int x = r.x; x < r.x + r.w; x++) {
        int y = y_of(r, m, at_column(r, m, v, x));
        int top = prev < 0 ? y : (prev < y ? prev : y);
        int bot = prev < 0 ? y : (prev > y ? prev : y);

        if (fill) tiku_vline(s, x, y, r.y + r.h - y, TRACE_FILL);
        tiku_vline(s, x, top, bot - top + 1, ink);
        prev = y;
    }
}

/** @brief Grid, decibel labels at the left and megahertz below. */
static void grid(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m)
{
    const tiku_font_t *f = tiku_font_plain();
    double lo = sdr_model_freq(m, 0), hi = sdr_model_freq(m, m->n - 1);
    int step = m->mode == SDR_BAND ? 10 : 5;
    char t[16];

    for (int d = (int)ceilf(m->scale_lo / 10) * 10; d <= m->scale_hi; d += 10) {
        int y = y_of(r, m, (float)d);
        tiku_hline(s, r.x, y, r.w, GRID);
        snprintf(t, sizeof t, "%d", d);
        tiku_text(s, f, r.x - 8 - tiku_text_width(f, t), y + 4, t, TIKU_C_TEXT);
    }
    for (int mhz = (int)ceil(lo / step) * step; mhz <= hi; mhz += step) {
        int x = x_of(r, m, mhz);
        tiku_vline(s, x, r.y, r.h, GRID);
        snprintf(t, sizeof t, "%d", mhz);
        tiku_text(s, f, x - tiku_text_width(f, t) / 2, r.y + r.h + 14, t,
                  TIKU_C_TEXT);
    }
    tiku_text(s, f, 8, r.y + 12, "dB", TIKU_C_TEXT);
}

/** @brief One ruler row: a label at the left and marks at @p mhz[]. */
static void ruler(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m,
                  int y, const char *name, const int *num, const double *mhz,
                  int count, tiku_rgb_t ink)
{
    const tiku_font_t *f = tiku_font_plain();
    char t[8];

    tiku_text(s, f, 8, y + 11, name, TIKU_C_TEXT);
    for (int i = 0; i < count; i++) {
        int x = x_of(r, m, mhz[i]);
        if (x < r.x || x >= r.x + r.w) continue;
        snprintf(t, sizeof t, "%d", num[i]);
        tiku_vline(s, x, y + 1, 3, ink);
        tiku_text(s, f, x - tiku_text_width(f, t) / 2, y + 13, t, ink);
    }
}

/** @brief The band's channel plans beneath the plot. */
static void rulers(tiku_surface_t *s, tiku_rect_t r, const sdr_model_t *m)
{
    int num[16];
    double mhz[16];
    int y = r.y + r.h + 18;

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

/** @brief The spectrum: plot ground, the receiver's flat zone, BLE's
 *         advertising channels, then the average or latest trace and the
 *         peaks over it. */
static void spectrum(sdr_t *st, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    static const double adv[3] = {2402.0, 2426.0, 2480.0};

    tiku_fill(st->surface, r, PLOT_BG);
    if (!m->fresh) return;
    grid(st->surface, r, m);
    if (m->mode == SDR_LIVE) {
        int a = x_of(r, m, m->centre - 12.0), b = x_of(r, m, m->centre + 12.0);
        tiku_veil(st->surface, (tiku_rect_t){r.x, r.y, a - r.x, r.h},
                  TIKU_RGB(0, 0, 0), 0.45f);
        tiku_veil(st->surface, (tiku_rect_t){b, r.y, r.x + r.w - b, r.h},
                  TIKU_RGB(0, 0, 0), 0.45f);
    }
    for (int i = 0; i < 3; i++) {
        int x = x_of(r, m, adv[i]);
        if (x >= r.x && x < r.x + r.w)
            for (int y = r.y; y < r.y + r.h; y += 4)
                tiku_vline(st->surface, x, y, 2, BLE_INK);
    }
    trace(st->surface, r, m, m->avg_on ? m->avg : m->db, TRACE, 1);
    if (m->peak_on) trace(st->surface, r, m, m->peak, PEAK, 0);
    rulers(st->surface, r, m);
}

/** @brief The waterfall: newest at the top, one row a spectrum, coloured
 *         over the plot's own range. */
static void waterfall(sdr_t *st, tiku_rect_t r)
{
    const sdr_model_t *m = &st->model;
    size_t need = (size_t)r.w * (size_t)r.h;
    float lo = m->scale_lo * 2.0f, span = (m->scale_hi - m->scale_lo) * 2.0f;
    tiku_rgb_t lut[256];

    if (r.w <= 0 || r.h <= 0) return;
    for (int v = 0; v < 256; v++) lut[v] = sdr_palette(((float)v - lo) / span);
    tiku_fill(st->surface, r, PLOT_BG);
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
            tiku_rgb_t c = y < m->wf_count ? lut[row[x * m->n / r.w]] : PLOT_BG;
            st->pic[(size_t)y * (size_t)r.w + (size_t)x] = 0xFF000000u | c;
        }
    }
    tiku_picture(st->surface, r.x, r.y, r.w, r.h, st->pic);
}

/** @brief Record's dot, red before its word, the button held down while
 *         a recording is being made. */
static void record_dot(sdr_t *st, tiku_rect_t r, unsigned flags)
{
    const char *word = sdr_label(st, CMD_RECORD);
    int x = r.x + (r.w - tiku_text_width(tiku_font_plain(), word)) / 2 - 14;
    int y = r.y + r.h / 2 - 4 + ((flags & TIKU_S_PRESSED) ? 1 : 0);

    tiku_fill_round(st->surface, (tiku_rect_t){x, y, 9, 9}, 4,
                    TIKU_CORNER_ALL, (flags & TIKU_S_DISABLED) ?
                    tiku_tint(TIKU_C_PANEL, 1.30f) : REC_INK);
}

/** @brief The toolbar: buttons, and the tuning between them.  A button
 *         that is on (the view shown, peaks held, a recording made) stays
 *         held down. */
static void toolbar(sdr_t *st)
{
    char t[48];
    tiku_rect_t up = st->dialog.control[CMD_UP - CMD_LIVE].rect;

    for (int i = 0; i < st->dialog.count; i++) {
        int id = st->dialog.control[i].id;
        unsigned flags =
            tiku_dialog_pressed(&st->dialog, id) ? TIKU_S_PRESSED : 0;
        if (tiku_dialog_focused(&st->dialog, id)) flags |= TIKU_S_FOCUS;
        if (!st->dialog.control[i].enabled) flags |= TIKU_S_DISABLED;
        if ((id == CMD_LIVE && st->model.mode == SDR_LIVE) ||
            (id == CMD_BAND && st->model.mode == SDR_BAND) ||
            (id == CMD_PEAK && st->model.peak_on) ||
            (id == CMD_RECORD && st->rec.file)) flags |= TIKU_S_PRESSED;
        tiku_ui_button(st->surface, st->dialog.control[i].rect,
                       sdr_label(st, id), flags);
        if (id == CMD_RECORD)
            record_dot(st, st->dialog.control[i].rect, flags);
    }
    if (st->model.mode == SDR_BAND) snprintf(t, sizeof t, "2.4 GHz band");
    else snprintf(t, sizeof t, "%d MHz", st->model.centre);
    tiku_text_line_draw(st->surface, (tiku_rect_t){up.x - 108, 12, 100, 24},
                        TIKU_MEASURE_BOLD, t, TIKU_C_TEXT);
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

/** @brief One line: what happened last, or what is under the pointer. */
static void status(sdr_t *st)
{
    char t[400], progress[64];
    const sdr_model_t *m = &st->model;

    tape_progress(st, progress, sizeof progress);
    if (st->hover[0])
        snprintf(t, sizeof t, "%s", st->hover);
    else if ((st->state == SDR_RUNNING || st->play.file) && m->fresh)
        snprintf(t, sizeof t, "%s   |   %s · gain %d · %.1f %s/s%s%s",
                 st->note, m->mode == SDR_BAND ?
                 "six slices, floors levelled" : "40 MS/s, 256 bins", m->gain,
                 (double)st->rate_hz, m->mode == SDR_BAND ? "sweeps" :
                 "spectra", progress, st->paused ? " · paused" : "");
    else if (st->play.file || st->rec.file)
        snprintf(t, sizeof t, "%s%s", st->note, progress);
    else if (st->saved[0] && strcmp(st->note, st->saved))
        snprintf(t, sizeof t, "%s   |   %s", st->note, st->saved);
    else
        snprintf(t, sizeof t, "%s", st->note);
    tiku_text_line_draw(st->surface,
                        (tiku_rect_t){16, st->h - STATUS_H, st->w - 32,
                                      STATUS_H - 4},
                        TIKU_MEASURE_PLAIN, t, TIKU_C_TEXT);
}

void sdr_paint(sdr_t *st)
{
    tiku_rect_t plot, fall;

    sdr_layout(st, &plot, &fall);
    sdr_controls(st);
    sdr_model_scale(&st->model);
    tiku_fill(st->surface, (tiku_rect_t){0, 0, st->w, st->h}, TIKU_C_PANEL);
    toolbar(st);
    spectrum(st, plot);
    waterfall(st, fall);
    status(st);
    if (st->alert.open)
        tiku_alert_draw(&st->alert, st->surface, sdr_alert_rect(st));
    st->services->frame(st->services->ctx, st->id, st->surface->px, st->w,
                        st->h);
    st->dirty = 0;
}

int sdr_picture(sdr_t *st, const char *path)
{
    tiku_rect_t plot, fall;
    tiku_surface_t *pic;
    int sc, top, rc;

    st->hover[0] = '\0';                /* the pointer is no part of it */
    sdr_paint(st);
    sdr_layout(st, &plot, &fall);
    sc = st->surface->scale > 0 ? st->surface->scale : 1;
    top = plot.y - 14;                  /* the dB label, not the toolbar */
    pic = tiku_surface_new(st->w * sc, (st->h - top) * sc, PLOT_BG);
    if (!pic) {
        snprintf(st->note, sizeof st->note, "No memory for a picture.");
        return -1;
    }
    memcpy(pic->px, st->surface->px + (size_t)top * sc * st->w * sc,
           sizeof *pic->px * (size_t)pic->w * (size_t)pic->h);
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
