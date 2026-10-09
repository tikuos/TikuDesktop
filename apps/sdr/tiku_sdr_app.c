/* tiku_sdr_app.c -- TikuSDR: the descriptor, its menus, the file panels
 * for recordings and exports, and what the panel, keys, the pointer and
 * the tick do.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_sdr.h"

#define REPAINT_US 50000            /* at most twenty paints a second */
#define NAMES_US 1000000

static const char *const type_name[SDR_T_TYPES] = {
    "Off", "Clear & Write", "Average", "Max Hold", "Min Hold"
};

/** @brief Fill the Tune menu: Wi-Fi channels 1 to 13, then BLE's three
 *         advertising channels, each disabled while Live is (a recording
 *         playing, or the alert up). */
static int tune_rows(const sdr_t *st, tiku_menu_list_t *menu)
{
    static const int ble[3][2] = {{37, 2402}, {38, 2426}, {39, 2480}};
    int n = 0, on = sdr_enabled(st, CMD_LIVE);

    snprintf(menu->title, sizeof menu->title, "Tune");
    for (int c = 1; c <= 13; c++, n++) {
        snprintf(menu->item[n].label, sizeof menu->item[n].label,
                 "Wi-Fi %d · %d MHz", c, 2407 + 5 * c);
        menu->item[n].command = CMD_TUNE + 7 + 5 * c;
        menu->item[n].enabled = on;
    }
    menu->item[n++].separator = 1;
    for (int i = 0; i < 3; i++, n++) {
        snprintf(menu->item[n].label, sizeof menu->item[n].label,
                 "BLE %d · %d MHz", ble[i][0], ble[i][1]);
        menu->item[n].command = CMD_TUNE + ble[i][1] - 2400;
        menu->item[n].enabled = on;
    }
    if (sdr_tuning_valid(st, 5180, 5825)) {
        static const int channels[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
        menu->item[n++].separator = 1;
        for (unsigned i = 0; i < sizeof channels / sizeof channels[0] &&
             n < TIKU_MENUSET_ITEMS; i++, n++) {
            int mhz = 5000 + 5 * channels[i];
            snprintf(menu->item[n].label, sizeof menu->item[n].label,
                     "Wi-Fi %d · %d MHz", channels[i], mhz);
            menu->item[n].command = CMD_TUNE5 + mhz - 5000;
            menu->item[n].enabled = on;
        }
    }
    return n;
}

/** @brief Fill the Device menu with the mounted ESP32-C61s, the chosen one
 *         marked, each enabled only while the link is idle and no alert is
 *         up. */
static int device_rows(sdr_t *st, tiku_menu_list_t *menu)
{
    char names[sizeof st->names], *save = NULL;
    int n = 0;

    snprintf(menu->title, sizeof menu->title, "Device");
    snprintf(names, sizeof names, "%s", st->names);
    for (char *p = strtok_r(names, "\n", &save); p && n < TIKU_MENUSET_ITEMS;
         p = strtok_r(NULL, "\n", &save), n++) {
        char *tab = strchr(p, '\t');
        if (tab) *tab = '\0';
        snprintf(menu->item[n].label, sizeof menu->item[n].label, "%s", p);
        menu->item[n].command = CMD_BOARD + n;
        menu->item[n].enabled = st->state == SDR_IDLE && !st->alert.open;
        menu->item[n].marked = !strcmp(p, st->board);
    }
    return n;
}

/** @brief Write @p command's words for a menu: the traces, the trace types
 *         and the markers by name; the menus' own words for the file
 *         commands, Clear and the marker commands; and sdr_label()'s for
 *         the rest. */
static void menu_label(const sdr_t *st, int command, char *out, size_t max)
{
    const char *t = NULL;

    if (command == CMD_RBW) {
        char a[24];
        sdr_mhz_text(SDR_HANN_RBW * sdr_model_bin_khz(&st->model), a, sizeof a);
        snprintf(out, max, "RBW: %s%s kHz (Cycle)",
                 st->model.rbw_bin_khz > 0 ? "" : "Auto / ", a);
    } else if (command == CMD_FFT)
        snprintf(out, max, "FFT: %s%d (Cycle)", st->model.fft_override ? "" : "Auto / ", st->model.nfft);
    else if (command == CMD_RATE)
        snprintf(out, max, "Rate: %s%d MS/s (Cycle)", st->model.rate_override ? "" : "Auto / ",
                 (int[]){80,40,20,10,8,4}[st->model.rate]);
    else if (command == CMD_OCC_PLAN)
        snprintf(out, max, "Channels: %s (Cycle)", (const char *[]){"Wi-Fi", "BLE", "802.15.4"}[st->occupancy.plan]);
    else if (command >= CMD_TYPE && command < CMD_TYPE + SDR_T_TYPES)
        t = type_name[command - CMD_TYPE];
    else if (command >= CMD_TRACE_SEL && command < CMD_TRACE_SEL + SDR_TRACES)
        snprintf(out, max, "Trace %d", command - CMD_TRACE_SEL + 1);
    else if (command >= CMD_MARK_SEL && command < CMD_MARK_SEL + SDR_MARKERS)
        snprintf(out, max, "Marker %d", command - CMD_MARK_SEL + 1);
    else if (command == CMD_RECORD)
        t = st->rec.file ? "Stop Recording" : "Record…";
    else if (command == CMD_REPLAY)
        t = st->play.file ? "Close Recording" : "Open Recording…";
    else if (command == CMD_EXPORT) t = "Export CSV…";
    else if (command == CMD_PICTURE) t = "Save Picture…";
    else if (command == CMD_CLEAR) t = "Clear Traces";
    else if (command == CMD_MARK_ACTIVE) t = "Marker On";
    else if (command == CMD_TRACK) t = "Track Peaks";
    else if (command == CMD_PEAK_LEFT) t = "Next Peak Left";
    else if (command == CMD_PEAK_RIGHT) t = "Next Peak Right";
    else if (command == CMD_MARK_CENTER) t = "Marker to Center";
    else if (command == CMD_MARK_CLEAR) t = "All Markers Off";
    else if (command == CMD_LIVE) t = "Live";
    else if (command == CMD_SPEED)
        snprintf(out, max, "Speed: %.2gx (Cycle)", st->play.speed > 0 ?
                 st->play.speed : 1.0);
    else t = sdr_label(st, command);
    if (t) snprintf(out, max, "%s", t);
}

/** @brief Whether command @p c's menu row shows a mark: the type of the
 *         trace being set, the trace and the marker being set, Live while
 *         the view is one spectrum, and a setting that is on. */
static int menu_marked(const sdr_t *st, int c)
{
    const sdr_model_t *m = &st->model;

    if (c >= CMD_TYPE && c < CMD_TYPE + SDR_T_TYPES)
        return m->type[st->trace_sel] == c - CMD_TYPE;
    if (c >= CMD_TRACE_SEL && c < CMD_TRACE_SEL + SDR_TRACES)
        return st->trace_sel == c - CMD_TRACE_SEL;
    if (c >= CMD_MARK_SEL && c < CMD_MARK_SEL + SDR_MARKERS)
        return m->mark_sel == c - CMD_MARK_SEL;
    return (c == CMD_LIVE && sdr_model_is_live(m)) ||
           (c == CMD_MARK_ACTIVE && m->mark[m->mark_sel].on) ||
           (c == CMD_TRACK && m->track) ||
           (c == CMD_WATERFALL && st->fall_on) ||
           (c == CMD_GAPS && st->play.skip_gaps) ||
           (c == CMD_REF_SHOW && st->reference.visible) ||
           (c == CMD_OCCUPANCY && st->occupancy.visible) ||
           (c == CMD_TRIGGER && (st->trigger.armed || st->trigger.fired)) ||
           (c == CMD_LOOP && st->play.loop) || (c == CMD_HEALTH && st->health_visible);
}

/** @brief Fill @p menu, titled @p title, from @p commands, 0 being a
 *         separator: each row's words, whether it can be used now, its
 *         mark, and Cmd-Q for Quit.  @return the number of rows. */
static int menu_rows(const sdr_t *st, tiku_menu_list_t *menu,
                     const char *title, const int *commands, int count)
{
    snprintf(menu->title, sizeof menu->title, "%s", title);
    for (int i = 0; i < count; i++) {
        tiku_menu_entry_t *e = &menu->item[i];
        int c = commands[i];
        if (!c) { e->separator = 1; continue; }
        menu_label(st, c, e->label, sizeof e->label);
        e->command = c;
        e->enabled = sdr_enabled(st, c);
        e->marked = menu_marked(st, c);
        if (c == CMD_QUIT) { e->sc = 'q'; e->mods = TIKU_MOD_CMD; }
    }
    return count;
}

/** @brief What the menus show, folded into one number: what can be used
 *         and what is on.  sdr_tick() publishes the menus again when it
 *         changes, which covers changes the board and the files make
 *         between commands. */
static unsigned menu_state(const sdr_t *st)
{
    const sdr_model_t *m = &st->model;
    unsigned v = (unsigned)st->state | (unsigned)(st->rec.file != NULL) << 4 |
                 (unsigned)(st->play.file != NULL) << 5 |
                 (unsigned)(st->csv.in != NULL) << 6 |
                 (unsigned)(m->fresh != 0) << 7 |
                 (unsigned)(st->shown[0] != '\0') << 8 |
                 (unsigned)sdr_model_is_live(m) << 9 |
                 (unsigned)(st->alert.open != 0) << 11 |
                 (unsigned)(st->fall_on != 0) << 12 |
                 (unsigned)(m->track != 0) << 14;

    v = v * 31u + (unsigned)st->trace_sel;
    v = v * 31u + (unsigned)(st->gain_known + 1);
    v = v * 31u + (unsigned)(st->gain_queued || st->gain_inflight);
    v = v * 31u + (unsigned)(st->play.speed * 4);
    v = v * 31u + (unsigned)st->play.skip_gaps;
    v = v * 31u + (unsigned)st->play.seeking;
    v = v * 31u + (unsigned)st->previous_count;
    v = v * 31u + (unsigned)(st->caps_known + 1);
    v = v * 31u + (unsigned)lround(sdr_model_bin_khz(m) * 64);
    v = v * 31u + (unsigned)(m->rbw_bin_khz > 0);
    v = v * 31u + (unsigned)st->book_count;
    v = v * 31u + (unsigned)st->trigger.armed;
    v = v * 31u + (unsigned)(m->wf_count > 0);
    v = v * 31u + (unsigned)m->mark_sel;
    for (int t = 0; t < SDR_TRACES; t++) v = v * 31u + (unsigned)m->type[t];
    for (int k = 0; k < SDR_MARKERS; k++) v = v * 31u + (unsigned)m->mark[k].on;
    return v;
}

/** @brief Publish the menus: SDR, File (recordings, CSV and pictures),
 *         Trace, Marker, Tune, and Device while an ESP32-C61 is mounted;
 *         nothing while the window is not open. */
static void publish(sdr_t *st)
{
    static const int sdr[] = {CMD_ABOUT, 0, CMD_CONNECT, 0, CMD_LIVE, 0,
                              CMD_PAUSE, 0, CMD_WATERFALL, 0,
                              CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_VIEW_BACK, 0,
                              CMD_HISTORY_OLDER, CMD_HISTORY_NEWER,
                              CMD_HISTORY_LATEST, 0,
                              CMD_HEALTH, CMD_CAPS, CMD_RBW, CMD_FFT, CMD_RATE,
                              CMD_QUIT};
    static const int file[] = {CMD_RECORD, CMD_REPLAY, 0, CMD_EXPORT,
                               CMD_PICTURE, 0, CMD_REWIND, CMD_FORWARD,
                               CMD_STEP, CMD_SPEED, CMD_GAPS, 0,
                               CMD_PRESET_SAVE, CMD_PRESET_OPEN, CMD_REF_SAVE,
                               CMD_REF_OPEN, 0, CMD_TRIGGER, CMD_BOOK_ADD,
                               CMD_BOOK_NEXT, CMD_BOOK_CLEAR, CMD_LOOP_A,
                               CMD_LOOP_B, CMD_LOOP};
    static const int trace[] = {CMD_TRACE_SEL, CMD_TRACE_SEL + 1,
                                CMD_TRACE_SEL + 2, 0, CMD_TYPE + SDR_T_OFF,
                                CMD_TYPE + SDR_T_WRITE, CMD_TYPE + SDR_T_AVG,
                                CMD_TYPE + SDR_T_MAX, CMD_TYPE + SDR_T_MIN, 0,
                                CMD_CLEAR, CMD_REF_SHOW, 0, CMD_OCCUPANCY,
                                CMD_OCC_PLAN, CMD_OCC_PAGE, CMD_OCC_RESET, CMD_OCC_EXPORT};
    static const int marker[] = {CMD_MARK_SEL, CMD_MARK_SEL + 1,
                                 CMD_MARK_SEL + 2, CMD_MARK_SEL + 3, 0,
                                 CMD_MARK_ACTIVE, CMD_TRACK, 0,
                                 CMD_PEAK_SEARCH, CMD_NEXT_PEAK, CMD_PEAK_LEFT,
                                 CMD_PEAK_RIGHT, 0, CMD_MARK_CENTER,
                                 CMD_MARK_CLEAR};
    tiku_menuset_t set;

    if (!st->id) return;                /* the window is not open */
    memset(&set, 0, sizeof set);
    set.nmenu = 6;
    set.menu[0].nitem = menu_rows(st, &set.menu[0], "SDR", sdr,
                                  (int)(sizeof sdr / sizeof sdr[0]));
    set.menu[1].nitem = menu_rows(st, &set.menu[1], "File", file,
                                  (int)(sizeof file / sizeof file[0]));
    set.menu[2].nitem = menu_rows(st, &set.menu[2], "Trace", trace,
                                  (int)(sizeof trace / sizeof trace[0]));
    set.menu[3].nitem = menu_rows(st, &set.menu[3], "Marker", marker,
                                  (int)(sizeof marker / sizeof marker[0]));
    set.menu[4].nitem = tune_rows(st, &set.menu[4]);
    set.menu[5].nitem = device_rows(st, &set.menu[5]);
    if (!set.menu[5].nitem) set.nmenu = 5;
    st->menu_state = menu_state(st);
    st->services->menus(st->services->ctx, st->id, &set);
}

/** @brief Show @p start to @p stop, fitted: the traces and the spectrogram
 *         start afresh, a recording being made notes the view, and the view
 *         is kept as the last of its kind, live or wide.  Nothing while a
 *         recording plays, or when the fitted view is the one shown. */
static void show(sdr_t *st, double start, double stop)
{
    sdr_navigate(st, start, stop, 1);
}

/** @brief Show @p span MHz about @p mhz. */
static void centre_at(sdr_t *st, double mhz, double span)
{
    show(st, mhz - span / 2, mhz + span / 2);
}

/** @brief Move the view by @p mhz, its span kept. */
static void shift(sdr_t *st, double mhz)
{
    show(st, st->model.start + mhz, st->model.stop + mhz);
}

/** @brief Whether (@p x, @p y) is in @p r. */
static int inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief The field focus is in, as SDR_F_*, or -1. */
static int editing(const sdr_t *st)
{
    int i = st->dialog.focus - CMD_FIELD;

    return i >= 0 && i < SDR_FIELDS ? i : -1;
}

/** @brief Start editing field @p i on its value, all of it selected, so
 *         the first key typed replaces it. */
static void field_open(sdr_t *st, int i)
{
    char t[32];

    sdr_field_text(st, i, t, sizeof t);
    tiku_field_start(&st->field[i], t);
    tiku_field_input_init(&st->input[i]);
}

/** @brief Make trace @p t show @p type; switching to Max Hold or Min Hold
 *         starts that hold again from the latest spectrum. */
static void set_type(sdr_t *st, int t, int type)
{
    sdr_model_t *m = &st->model;

    if (t < 0 || t >= SDR_TRACES || type < 0 || type >= SDR_T_TYPES) return;
    if (type == SDR_T_MAX && m->type[t] != SDR_T_MAX)
        memcpy(m->peak, m->db, sizeof m->peak);
    if (type == SDR_T_MIN && m->type[t] != SDR_T_MIN)
        memcpy(m->low, m->db, sizeof m->low);
    m->type[t] = type;
}

/** @brief Apply what was typed in field @p i.  Center keeps the span and
 *         Span the centre; Start keeps the stop and Stop the start, unless
 *         the value passes it, when the span is kept.  The average's count
 *         takes 1 to 100, and the marker moves to the frequency typed.  A
 *         value refused is reported in the note.  The field then shows its
 *         value again, all selected. */
static void field_apply(sdr_t *st, int i)
{
    sdr_model_t *m = &st->model;
    double v, a = m->start, b = m->stop, span = b - a, c = (a + b) / 2;
    char now[32], *end;
    long count;

    sdr_field_text(st, i, now, sizeof now);
    if (!strcmp(st->field[i].text, now)) return;
    if (i == SDR_F_BOOK) {
        snprintf(st->book_label, sizeof st->book_label, "%.63s", st->field[i].text);
        st->dirty = 1;
        return;
    }
    if (i >= SDR_F_THRESHOLD) {
        v = strtod(st->field[i].text, &end);
        int valid = end != st->field[i].text && !*end && isfinite(v);
        if (i == SDR_F_THRESHOLD) valid = valid && v >= -200 && v <= 300;
        else valid = valid && floor(v) == v && v >= (i == SDR_F_PRE ? 0 : 1) &&
                     v <= (i == SDR_F_PRE ? 30 : 300);
        if (valid) {
            sdr_trigger_cancel(st);
            if (i == SDR_F_THRESHOLD) { st->occupancy.threshold = (float)v; sdr_occupancy_reset(st); }
            else if (i == SDR_F_PRE) st->trigger.pre_seconds = (int)v;
            else st->trigger.post_seconds = (int)v;
        } else snprintf(st->note, sizeof st->note,
                        "Threshold: -200..300 relative dB; pre: 0..30 s; post: 1..300 s.");
        field_open(st, i); st->dirty = 1;
        return;
    }
    if (i >= SDR_F_REF) {
        v = strtod(st->field[i].text, &end);
        int valid = end != st->field[i].text && !*end && isfinite(v);
        if (i == SDR_F_GAIN) {
            valid = valid && v >= 0 && v <= 255 && floor(v) == v;
            if (valid) valid = sdr_gain_request(st, (int)v);
        } else if (i == SDR_F_DIV) {
            valid = valid && (v == 2 || v == 5 || v == 10 || v == 20);
            if (valid) m->div = (float)v, m->manual_scale = 1;
        } else {
            valid = valid && v >= -200 && v <= 300;
            if (i == SDR_F_FLOOR) valid = valid && v < m->ceiling_db;
            if (i == SDR_F_CEILING) valid = valid && v > m->floor_db;
            if (valid && i == SDR_F_REF)
                m->scale_hi = (float)v, m->manual_scale = 1;
            if (valid && i == SDR_F_FLOOR)
                m->floor_db = (float)v, m->manual_color = 1;
            if (valid && i == SDR_F_CEILING)
                m->ceiling_db = (float)v, m->manual_color = 1;
        }
        m->scale_lo = m->scale_hi - SDR_DIVS * m->div;
        if (!valid) snprintf(st->note, sizeof st->note, "%s",
            i == SDR_F_GAIN ? "Read receiver gain first; use an index from 0 to 255." :
            i == SDR_F_DIV ? "Use 2, 5, 10 or 20 dB per division." :
            "Use -200 to 300 dB; waterfall floor must be below its ceiling.");
        field_open(st, i);
        st->dirty = 1;
        return;
    }
    if (i == SDR_F_AVG) {
        count = strtol(st->field[i].text, &end, 10);
        if (end == st->field[i].text || *end || count < 1 || count > 100)
            snprintf(st->note, sizeof st->note,
                     "An average spans 1 to 100 spectra.");
        else
            m->avg_count = (int)count;
        field_open(st, i);
        st->dirty = 1;
        return;
    }
    if (!sdr_mhz_parse(st->field[i].text, &v)) {
        snprintf(st->note, sizeof st->note, "%.40s is not a frequency.",
                 st->field[i].text);
        field_open(st, i);
        st->dirty = 1;
        return;
    }
    if (i == SDR_F_MARK) {
        sdr_mark_at(m, m->mark_sel, v, 0);
        field_open(st, i);
        st->dirty = 1;
        return;
    }
    if (i == SDR_F_CENTER) a = v - span / 2, b = v + span / 2;
    else if (i == SDR_F_SPAN) a = c - v / 2, b = c + v / 2;
    else if (i == SDR_F_START) a = v, b = b > v ? b : v + span;
    else b = v, a = a < v ? a : v - span;
    show(st, a, b);
    field_open(st, i);                  /* the value as fitted, selected */
}

/** @brief Open the desktop's file panel for @p command: where a recording,
 *         a CSV or a picture goes, or which recording to play.  It opens in
 *         the last file's folder, else $HOME; the answer comes to
 *         sdr_picked(). */
static void ask(sdr_t *st, int command)
{
    static const char *const ext[] = {".sdrlog", ".csv", ".png"};
    const tiku_app_services_t *sv = st->services;
    const char *home = getenv("HOME"), *start = *st->tape_dir ? st->tape_dir
                                                               : home;
    char name[SDR_PATH];
    int put;

    if (!sv->pick) {
        snprintf(st->note, sizeof st->note,
                 "This desktop cannot show a file panel.");
        return;
    }
    st->asking = command;
    if (command == CMD_REPLAY || command == CMD_PRESET_OPEN || command == CMD_REF_OPEN) {
        put = sv->pick(sv->ctx, st->id, TIKU_APP_PICK_LOCAL_OPEN, start, NULL);
    } else {
        sdr_tape_name(st, command == CMD_PRESET_SAVE ? ".sdrpreset" :
                      command == CMD_REF_SAVE ? ".sdrref" :
                      command == CMD_OCC_EXPORT ? ".occupancy.csv" :
                      ext[command == CMD_EXPORT ? 1 :
                              command == CMD_PICTURE ? 2 : 0],
                      name, sizeof name);
        put = sv->pick(sv->ctx, st->id, TIKU_APP_PICK_SAVE, start, name);
    }
    if (put <= 0) {
        st->asking = 0;
        snprintf(st->note, sizeof st->note, "The file panel did not open.");
    }
}

/** @brief Record: ask where while the board runs, or stop.  Open
 *         Recording: ask which while the link is idle, or close the one
 *         playing.  Export CSV and Save Picture: ask where, when they can
 *         be used. */
static void tape(sdr_t *st, int command)
{
    if (command == CMD_RECORD && st->rec.file) {
        sdr_rec_stop(st);
    } else if (command == CMD_RECORD && st->state == SDR_RUNNING) {
        ask(st, CMD_RECORD);
    } else if (command == CMD_REPLAY && st->play.file) {
        sdr_play_stop(st);
        snprintf(st->note, sizeof st->note, "Closed %.140s.",
                 sdr_tape_leaf(st->play.path));
    } else if (command == CMD_REPLAY && st->state == SDR_IDLE) {
        ask(st, CMD_REPLAY);
    } else if ((command == CMD_EXPORT || command == CMD_PICTURE) &&
               sdr_enabled(st, command)) {
        ask(st, command);
    }
}

/** @brief Open the alert, over the window, that no radio is connected and
 *         that TikuSDR takes an ESP32-C61 through the Devices menu.  Its
 *         one button, OK, clicked or taken by Return or Escape, closes
 *         TikuSDR. */
static void missing(sdr_t *st)
{
    tiku_alert_open(&st->alert, TIKU_ALERT_STOP, 0,
                    "No software-defined radio detected. TikuSDR currently "
                    "supports ESP32-C5 and ESP32-C61 receivers. "
                    "Please connect one "
                    "through the Devices menu.", "OK");
    st->dirty = 1;
}

/** @brief Act on a marker command: turn the selected marker on or off,
 *         switch Track, move the marker to a peak or centre the view on it,
 *         or turn every marker off.  A peak move that finds no peak says so
 *         in the note. */
static void mark(sdr_t *st, int command)
{
    static const char *const way[] = {"", "lower than", "left of",
                                      "right of"};
    sdr_model_t *m = &st->model;
    int k = m->mark_sel, how;

    if (command == CMD_MARK_ACTIVE) {
        if (m->mark[k].on) sdr_mark_off(m, k); else sdr_mark_on(m, k);
    } else if (command == CMD_TRACK) {
        m->track = !m->track;
    } else if (command == CMD_MARK_REMOVE) {
        sdr_mark_off(m, k);
    } else if (command == CMD_MARK_CLEAR) {
        for (int i = 0; i < SDR_MARKERS; i++) sdr_mark_off(m, i);
    } else if (!m->mark[k].on || !m->fresh) {
        return;                         /* no marker on, or no frame */
    } else if (command == CMD_MARK_CENTER) {
        centre_at(st, m->mark[k].mhz, m->stop - m->start);
    } else {
        how = command == CMD_PEAK_SEARCH ? SDR_TO_HIGHEST :
              command == CMD_NEXT_PEAK ? SDR_TO_NEXT :
              command == CMD_PEAK_LEFT ? SDR_TO_LEFT : SDR_TO_RIGHT;
        if (!sdr_mark_peak(m, k, how))
            snprintf(st->note, sizeof st->note, "No peak %s M%d.", way[how],
                     k + 1);
    }
}

/** @brief Carry out @p command, from the panel, a menu or a key, then
 *         publish the menus and paint; nothing but Quit while the alert is
 *         up.  @return 1 for Quit, else 0. */
static int sdr_pick(void *state, uint32_t window, int command)
{
    sdr_t *st = state;
    sdr_model_t *m = &st->model;

    (void)window;
    if (command == CMD_QUIT) return 1;
    if (st->alert.open) return 0;       /* the alert is answered first */
    if (command != CMD_CONNECT && !sdr_enabled(st, command)) return 0;
    if (command >= CMD_PRESET_SAVE && command <= CMD_RBW) {
        if (command == CMD_PRESET_SAVE || command == CMD_PRESET_OPEN ||
            command == CMD_REF_SAVE || command == CMD_REF_OPEN || command == CMD_OCC_EXPORT)
            ask(st, command);
        else if (command == CMD_REF_SHOW) st->reference.visible = !st->reference.visible;
        else if (command == CMD_OCCUPANCY) {
            st->occupancy.visible = !st->occupancy.visible;
            if (st->occupancy.visible) st->fall_on = 1;
        } else if (command == CMD_OCC_PLAN) {
            st->occupancy.plan = (st->occupancy.plan + 1) % 3;
            st->occupancy.page = 0;
        } else if (command == CMD_OCC_PAGE) st->occupancy.page = (st->occupancy.page + 1) % 40;
        else if (command == CMD_OCC_RESET) sdr_occupancy_reset(st);
        else if (command == CMD_TRIGGER) {
            if (st->trigger.armed || st->trigger.fired) sdr_trigger_cancel(st);
            else ask(st, command);
        } else if (command == CMD_CAPS) st->caps_queued = 1;
        else if (command == CMD_FFT || command == CMD_RATE) sdr_acquisition_cycle(st, command == CMD_FFT);
        else if (command == CMD_RBW) sdr_rbw_cycle(st);
        else if (command == CMD_BOOK_ADD) sdr_book_add(st);
        else if (command == CMD_BOOK_NEXT) sdr_book_next(st);
        else if (command == CMD_BOOK_CLEAR) { st->book_count = 0; (void)sdr_books_save(st); }
        else if (command == CMD_LOOP_A || command == CMD_LOOP_B) {
            double at = fmin(st->play.clock / 1e6, st->play.total);
            if (command == CMD_LOOP_A) {
                st->play.loop_a = at;
                if (st->play.loop_b <= at) st->play.loop_b = st->play.total;
            } else if (at > st->play.loop_a) st->play.loop_b = at;
            st->play.loop = 0;
            (void)sdr_books_save(st);
        } else if (command == CMD_LOOP) {
            st->play.loop = !st->play.loop;
            if (st->play.loop) {
                st->play.skip_gaps = 0;
                sdr_play_seek(st, st->play.loop_a);
                st->play.loop = 1;
                st->play.skip_gaps = 0;
                st->play.resume_seek = 1;
            }
        } else if (command == CMD_HEALTH) st->health_visible = !st->health_visible;
    } else if (command == CMD_CONNECT) {
        if (st->state != SDR_IDLE) sdr_disconnect(st);
        else if (sdr_connect(st) < 0)   /* gone since the menu was drawn */
            snprintf(st->note, sizeof st->note,
                     "No software-defined radio is connected.");
    } else if (command == CMD_ABOUT) {
        sdr_about_open(st);
        return 0;
    } else if (command >= CMD_SECTION && command < CMD_SECTION + SDR_SECTIONS) {
        sdr_section_toggle(st, command - CMD_SECTION);
        if (command == CMD_SECTION + SDR_S_RECEIVER &&
            !(st->collapsed & (1u << SDR_S_RECEIVER)))
            (void)sdr_gain_request(st, -2);
    } else if (command == CMD_GAIN_READ) (void)sdr_gain_request(st, -2);
    else if (command == CMD_GAIN_AUTO)
        (void)sdr_gain_request(st, st->gain_value < 0 ? 60 : -1);
    else if (command == CMD_SCALE_AUTO) m->manual_scale = !m->manual_scale;
    else if (command == CMD_COLOR_AUTO) m->manual_color = !m->manual_color;
    else if (command == CMD_REWIND || command == CMD_FORWARD)
        sdr_play_seek(st, st->play.clock / 1e6 + (command == CMD_REWIND ? -10 : 10));
    else if (command == CMD_STEP) sdr_play_step(st);
    else if (command == CMD_SPEED)
        st->play.speed = st->play.speed >= 4 ? 0.25 : st->play.speed * 2;
    else if (command == CMD_GAPS) st->play.skip_gaps = !st->play.skip_gaps;
    else if (command == CMD_ZOOM_IN) sdr_zoom(st, 0.5, 0.5);
    else if (command == CMD_ZOOM_OUT) sdr_zoom(st, 0.5, 2);
    else if (command == CMD_VIEW_BACK) sdr_view_back(st);
    else if (command == CMD_HISTORY_OLDER) sdr_history_scroll(m, 40);
    else if (command == CMD_HISTORY_NEWER) sdr_history_scroll(m, -40);
    else if (command == CMD_HISTORY_LATEST) {
        m->wf_offset = 0;
        m->wf_selected = -1;
    }
    else if (command == CMD_LIVE) {   /* the last view of the other kind */
        const double *v = sdr_model_is_live(m) ? st->wide_view
                                               : st->live_view;
        show(st, v[0], v[1]);
    } else if (command == CMD_CLEAR) sdr_model_clear(m);
    else if (command == CMD_PEAK)       /* trace 2 a max hold, or off */
        set_type(st, 1, m->type[1] == SDR_T_MAX ? SDR_T_OFF : SDR_T_MAX);
    else if (command == CMD_OVERLAY)    /* trace 3 the latest, or off */
        set_type(st, 2, m->type[2] == SDR_T_WRITE ? SDR_T_OFF : SDR_T_WRITE);
    else if (command >= CMD_TYPE && command < CMD_TYPE + SDR_T_TYPES)
        set_type(st, st->trace_sel, command - CMD_TYPE);
    else if (command >= CMD_TRACE_SEL &&
             command < CMD_TRACE_SEL + SDR_TRACES)
        st->trace_sel = command - CMD_TRACE_SEL;
    else if (command >= CMD_MARK_SEL && command < CMD_MARK_SEL + SDR_MARKERS)
        m->mark_sel = command - CMD_MARK_SEL;
    else if (command >= CMD_MARK_ACTIVE && command <= CMD_MARK_CLEAR)
        mark(st, command);
    else if (command == CMD_MARK_REMOVE) mark(st, command);
    else if (command == CMD_WATERFALL) st->fall_on = !st->fall_on;
    else if (command == CMD_PAUSE) st->paused = !st->paused;
    else if (command == CMD_RECORD || command == CMD_REPLAY ||
             command == CMD_EXPORT || command == CMD_PICTURE)
        tape(st, command);
    else if (command >= CMD_TUNE5 && command <= CMD_TUNE5 + 900 &&
             sdr_tuning_valid(st, 5000 + command - CMD_TUNE5,
                             5000 + command - CMD_TUNE5))
        centre_at(st, 5000 + command - CMD_TUNE5,
                  m->sweep ? SDR_LIVE_SPAN : m->stop - m->start);
    else if (command >= CMD_TUNE && command < CMD_CHOICE)
        /* A channel: the view's span about it, or SDR_LIVE_SPAN in place
         * of a sweep. */
        centre_at(st, 2400 + command - CMD_TUNE,
                  m->sweep ? SDR_LIVE_SPAN : m->stop - m->start);
    else if (command >= CMD_BOARD && command < CMD_BOARD + TIKU_MENUSET_ITEMS && st->state == SDR_IDLE) {
        char names[sizeof st->names], *save = NULL, *p;
        int i = 0;
        snprintf(names, sizeof names, "%s", st->names);
        for (p = strtok_r(names, "\n", &save); p && i < command - CMD_BOARD;
             p = strtok_r(NULL, "\n", &save)) i++;
        if (p && strchr(p, '\t')) *strchr(p, '\t') = '\0';
        if (p) snprintf(st->board, sizeof st->board, "%.63s", p);
    }
    publish(st);
    sdr_paint(st);
    return 0;
}

/** @brief Apply list @p c's new selection: the trace or marker the panel
 *         sets, the type of the trace being set, the trace the selected
 *         marker reads, or the resolution bandwidth. */
static void chose(sdr_t *st, int c)
{
    int v = st->choice[c].selected;

    if (c == SDR_C_TRACE) st->trace_sel = v;
    else if (c == SDR_C_TYPE) set_type(st, st->trace_sel, v);
    else if (c == SDR_C_MARKER) st->model.mark_sel = v;
    else if (c == SDR_C_RBW)
        sdr_rbw_choose(st, v > 0 && v <= st->rbw_count ? st->rbw_list[v - 1]
                                                       : 0);
    else st->model.mark[st->model.mark_sel].trace = v;
}

/** @brief The bin under graticule column @p x, kept inside the trace. */
static int bin_at(const sdr_model_t *m, tiku_rect_t plot, int x)
{
    int b = (x - plot.x) * m->n / plot.w;

    return b < 0 ? 0 : b >= m->n ? m->n - 1 : b;
}

/** @brief Whether a press at (@p x, @p y) is one markers take: on the
 *         graticule, with a marker on and a frame shown. */
static int marking(const sdr_t *st, tiku_rect_t plot, int x, int y)
{
    return sdr_marks_out(&st->model) && st->model.fresh &&
           inside(plot, x, y);
}

/** @brief Set the hover text for the pointer at (@p x, @p y) over the
 *         graticule or the spectrogram: the frequency, each trace's level
 *         there and the channels it is in, and in a sweep that a click
 *         tunes there.  Empty elsewhere, or before the first frame. */
static void hover(sdr_t *st, int x, int y)
{
    static const char *const kind[SDR_T_TYPES] = {"", "", "avg ", "max ",
                                                  "min "};
    tiku_rect_t plot, fall;
    const sdr_model_t *m = &st->model;
    char ch[96], level[128];
    int n = 0;

    sdr_layout(st, &plot, &fall);
    st->hover[0] = '\0';
    st->hover_x = x; st->hover_y = y;
    st->hover_valid = inside(plot, x, y) || inside(fall, x, y);
    if (st->occupancy.visible && inside(fall, x, y)) { st->hover_valid = 0; return; }
    if (!m->fresh || !st->hover_valid) return;
    int b = bin_at(m, plot, x);
    double mhz = sdr_model_freq(m, b);
    if (inside(fall, x, y)) {
        int row = sdr_history_row(m, m->wf_offset + y - fall.y);
        if (row < 0) return;
        snprintf(st->hover, sizeof st->hover,
                 "%.3f MHz · stored %.1f dB · %.3f s ago%s", mhz,
                 m->wf[row][b] / 2.0, m->wf_at[m->wf_head] - m->wf_at[row],
                 m->sweep && !st->play.file ? " · click to tune here" :
                 " · Option-click to inspect");
        return;
    }
    level[0] = '\0';
    for (int t = 0; t < SDR_TRACES && n < (int)sizeof level - 32; t++) {
        const float *v = sdr_trace(m, m->type[t]);
        if (v)
            n += snprintf(level + n, sizeof level - (size_t)n,
                          "%s%d: %s%.1f dB", n ? " · " : "", t + 1,
                          kind[m->type[t]], (double)v[b]);
    }
    sdr_channels(mhz, ch, sizeof ch);
    snprintf(st->hover, sizeof st->hover, "%.3f MHz   %s%s%s%s", mhz, level,
             *ch ? "   " : "", ch,
             m->sweep && !marking(st, plot, x, y) ? "   · click to tune here"
                                                  : "");
    int row = sdr_history_row(m, m->wf_selected);
    if (sdr_reference_matches(st)) {
        size_t used = strlen(st->hover);
        snprintf(st->hover + used, sizeof st->hover - used,
                 " · Ref %.1f, delta %+.1f dB", st->reference.db[b],
                 m->db[b] - st->reference.db[b]);
    }
    if (row >= 0) {
        size_t used = strlen(st->hover);
        snprintf(st->hover + used, sizeof st->hover - used,
                 " · History %.1f dB", m->wf[row][b] / 2.0);
    }
}

/** @brief The window's keys: Left and Right move the view 1 MHz, 5 with
 *         Shift; 1 to 4 select a marker; Space, l, p, a, r, c, m, n, k, [,
 *         ], w, Delete and Backspace run their commands through sdr_pick().
 *         @return 1 when the key was taken. */
static int keys(sdr_t *st, const tiku_event_t *ev)
{
    int step = (ev->modifiers & TIKU_MOD_SHIFT) ? 5 : 1;
    int command = ev->key == ' ' ? CMD_PAUSE : ev->key == 'l' ? CMD_LIVE :
                  ev->key == 'p' ? CMD_PEAK : ev->key == 'a' ? CMD_OVERLAY :
                  ev->key == 'r' ? CMD_RECORD : ev->key == 'c' ? CMD_CLEAR :
                  ev->key == 'm' ? CMD_MARK_ACTIVE :
                  ev->key == 'n' ? CMD_NEXT_PEAK :
                  ev->key == 'k' ? CMD_PEAK_SEARCH :
                  ev->key == '[' ? CMD_PEAK_LEFT :
                  ev->key == ']' ? CMD_PEAK_RIGHT :
                  ev->key == 'w' ? CMD_WATERFALL :
                  ev->key == '+' || ev->key == '=' ? CMD_ZOOM_IN :
                  ev->key == '-' ? CMD_ZOOM_OUT :
                  ev->key == 'b' ? CMD_VIEW_BACK :
                  ev->key == TIKU_KEY_DELETE || ev->key == TIKU_KEY_BACKSPACE ?
                  CMD_MARK_REMOVE : 0;

    if (ev->key >= '1' && ev->key < '1' + SDR_MARKERS)
        command = CMD_MARK_SEL + (int)(ev->key - '1');
    if (ev->key == TIKU_KEY_LEFT) shift(st, -step);
    else if (ev->key == TIKU_KEY_RIGHT) shift(st, step);
    else if (command) sdr_pick(st, st->id, command);
    else return 0;
    return 1;
}

/** @brief Take a press on the graticule at column @p x: the marker within
 *         6 pixels of it, or else the selected one, is selected, put there
 *         and held by the pointer. */
static void grab(sdr_t *st, tiku_rect_t plot, int x)
{
    sdr_model_t *m = &st->model;
    int at = bin_at(m, plot, x), k = -1, best = 7;

    for (int i = 0; i < SDR_MARKERS; i++) {
        int b = sdr_mark_bin(m, i), px;
        if (b < 0) continue;
        px = abs(b - at) * plot.w / m->n;
        if (px < best) {
            best = px;
            k = i;
        }
    }
    if (k < 0) k = m->mark_sel;
    m->mark_sel = m->mark_held = k;
    sdr_mark_at(m, k, sdr_model_freq(m, at), 0);
}

/** @brief Route navigation gestures, divider and marker drags, timeline
 *         seeks, and pointer readings. An ordinary click in a sweep tunes
 *         a live range unless a marker takes the click. */
static void pointer(sdr_t *st, const tiku_event_t *ev)
{
    tiku_rect_t plot, fall;
    sdr_model_t *m = &st->model;

    sdr_layout(st, &plot, &fall);
    if (!st->divider_drag && sdr_navigation_event(st, ev)) {
        if (!st->nav_drag) hover(st, ev->x, ev->y);
        return;
    }
    if (ev->type == TIKU_EVENT_DEACTIVATED) st->divider_drag = 0;
    if (st->divider_drag && (ev->type == TIKU_EVENT_POINTER_MOVE ||
                             ev->type == TIKU_EVENT_POINTER_UP)) {
        int percent = (ev->y - fall.y) * 100 / (st->h - 86);
        st->fall_percent = percent < 15 ? 15 : percent > 60 ? 60 : percent;
        if (ev->type == TIKU_EVENT_POINTER_UP) st->divider_drag = 0;
        st->dirty = 1;
        return;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        if ((st->play.file || (st->shown[0] && st->state == SDR_IDLE)) &&
            inside(sdr_timeline_rect(st), ev->x, ev->y)) {
            tiku_rect_t r = sdr_timeline_rect(st);
            sdr_play_seek(st, st->play.total * (ev->x - r.x) / r.w);
            return;
        }
        if (fall.h && ev->x >= fall.x && ev->x < fall.x + fall.w &&
            ev->y >= fall.y + fall.h && ev->y < fall.y + fall.h + 6) {
            st->divider_drag = 1;
            return;
        }
    }
    if (m->mark_held >= 0 && (ev->type == TIKU_EVENT_POINTER_MOVE ||
                              ev->type == TIKU_EVENT_POINTER_UP)) {
        int up = ev->type == TIKU_EVENT_POINTER_UP;
        sdr_mark_at(m, m->mark_held,
                    sdr_model_freq(m, bin_at(m, plot, ev->x)), up);
        if (up) m->mark_held = -1;
        hover(st, ev->x, ev->y);
    } else if (ev->type == TIKU_EVENT_DEACTIVATED) {
        m->mark_held = -1;
    } else if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        hover(st, ev->x, ev->y);
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
               marking(st, plot, ev->x, ev->y)) {
        grab(st, plot, ev->x);
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
               m->sweep && m->fresh &&
               (inside(plot, ev->x, ev->y) || inside(fall, ev->x, ev->y))) {
        centre_at(st, round(sdr_model_freq(m, bin_at(m, plot, ev->x))),
                  SDR_LIVE_SPAN);
    }
    st->dirty = 1;
}

/** @brief Offer @p ev to the panel's lists: an open list takes every
 *         event, a closed one a press on it or keys while it has the focus,
 *         and a new selection is applied.  @return 1 when a list took it. */
static int lists(sdr_t *st, const tiku_event_t *ev)
{
    tiku_rect_t screen = {0, 0, st->w, st->h};
    int open = -1;

    for (int c = 0; c < SDR_CHOICES; c++)
        if (st->choice[c].open) open = c;
    for (int c = 0; c < SDR_CHOICES; c++) {
        tiku_choice_result_t r;
        if (open >= 0 && c != open) continue;
        r = tiku_choice_event(&st->choice[c], ev,
                              sdr_control_rect(st, CMD_CHOICE + c), screen,
                              st->dialog.focus == CMD_CHOICE + c);
        if (r == TIKU_CHOICE_IGNORED) continue;
        if (r == TIKU_CHOICE_CHANGED) {
            chose(st, c);
            publish(st);
        }
        return 1;
    }
    return 0;
}

/** @brief Pass @p ev to the alert, a key or the pointer; its OK ends
 *         TikuSDR.  @return 1 when OK was chosen. */
static int answer(sdr_t *st, const tiku_event_t *ev)
{
    tiku_rect_t f = sdr_alert_rect(st);
    int x = ev->x - f.x, y = ev->y - f.y, at = -1, redraw = 0;

    if (ev->type == TIKU_EVENT_KEY_DOWN)
        at = tiku_alert_key(&st->alert, ev->key);
    else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1)
        redraw = tiku_alert_press(&st->alert, f.w, f.h, x, y);
    else if (ev->type == TIKU_EVENT_POINTER_MOVE)
        redraw = tiku_alert_track(&st->alert, f.w, f.h, x, y);
    else if (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1) {
        at = tiku_alert_release(&st->alert, f.w, f.h, x, y);
        redraw = 1;
    }
    if (at >= 0) return 1;              /* OK: TikuSDR ends */
    if (redraw) sdr_paint(st);
    return 0;
}

/** @brief Input in the main window: close, and a resize above 600 by 560;
 *         while the alert is up it takes the rest.  Otherwise the lists,
 *         then Escape (which leaves a field, or else closes TikuSDR), the
 *         other controls and the fields, the keys and the pointer.
 *         @return 1 when TikuSDR is done. */
static int sdr_event(void *state, const tiku_event_t *ev)
{
    sdr_t *st = state;
    tiku_dialog_result_t result;
    int was;

    if (ev->type == TIKU_EVENT_CLOSE) return 1;
    if (ev->type == TIKU_EVENT_RESIZE ||
        (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE &&
         st->nav_drag)) {
        int cancelled = sdr_navigation_event(st, ev);
        if (ev->type != TIKU_EVENT_RESIZE && cancelled) {
            sdr_paint(st);
            return 0;
        }
    }
    if (ev->type == TIKU_EVENT_RESIZE && ev->width > 600 &&
        ev->height > 560) {
        if (tiku_surface_resize(st->surface, ev->width, ev->height,
                                TIKU_C_PANEL) == 0) {
            st->w = ev->width;
            st->h = ev->height;
        }
        sdr_paint(st);
        return 0;
    }
    if (st->alert.open) return answer(st, ev);
    sdr_controls(st);
    was = editing(st);
    if (lists(st, ev)) {
        if (was >= 0) {                 /* what was typed is applied */
            st->dialog.focus = TIKU_DIALOG_NONE;
            field_apply(st, was);
        }
        sdr_paint(st);
        return 0;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE) {
        if (was < 0) return 1;
        st->dialog.focus = TIKU_DIALOG_NONE;    /* what was typed is dropped */
        sdr_paint(st);
        return 0;
    }
    result = tiku_dialog_event(&st->dialog, ev);
    if (was >= 0 && ev->type == TIKU_EVENT_POINTER_DOWN &&
        editing(st) == was &&
        !inside(sdr_control_rect(st, CMD_FIELD + was), ev->x, ev->y))
        st->dialog.focus = TIKU_DIALOG_NONE;    /* a press elsewhere */
    if (was >= 0 && editing(st) != was) field_apply(st, was);
    if (editing(st) >= 0 && editing(st) != was) {
        field_open(st, editing(st));
        if (ev->type == TIKU_EVENT_POINTER_DOWN) {  /* all left selected */
            sdr_paint(st);
            return 0;
        }
    }
    if (result.activated != TIKU_DIALOG_NONE)
        return sdr_pick(st, st->id, result.activated);
    if (editing(st) >= 0 && ev->type == TIKU_EVENT_KEY_DOWN) {
        int i = editing(st);
        if ((!result.taken || ev->key == TIKU_KEY_RETURN) &&
            tiku_field_event(&st->field[i], &st->input[i], ev,
                             sdr_control_rect(st, CMD_FIELD + i), 1) ==
                TIKU_FIELD_COMMITTED)
            field_apply(st, i);
    } else if (editing(st) >= 0) {
        int i = editing(st);
        (void)tiku_field_event(&st->field[i], &st->input[i], ev,
                               sdr_control_rect(st, CMD_FIELD + i), 1);
        if (!result.taken) pointer(st, ev);
    } else if (!result.taken) {
        if (ev->type == TIKU_EVENT_KEY_DOWN) keys(st, ev);
        else pointer(st, ev);
    }
    sdr_paint(st);
    return 0;
}

/** @brief Input in one of TikuSDR's windows: any key or press closes the
 *         splash; a press, Escape, Return or a close event closes the About
 *         box; the main window's input goes to sdr_event().
 *         @return 1 when TikuSDR is done. */
static int sdr_event_in(void *state, uint32_t window, const tiku_event_t *ev)
{
    sdr_t *st = state;
    int key = ev->type == TIKU_EVENT_KEY_DOWN;

    if (st->splash && window == st->splash_id) {
        if (key || ev->type == TIKU_EVENT_POINTER_DOWN) sdr_splash_drop(st);
        return 0;
    }
    if (st->about && window == st->about_id) {
        if (ev->type == TIKU_EVENT_POINTER_DOWN ||
            ev->type == TIKU_EVENT_CLOSE ||
            (key && (ev->key == TIKU_KEY_ESCAPE || ev->key == TIKU_KEY_RETURN)))
            sdr_about_drop(st);
        return 0;
    }
    return window == st->id ? sdr_event(st, ev) : 0;
}

/** @brief Forget window @p window, which the host closed: the splash, the
 *         About box, or the main window, which closes the About box too.
 *         The host ends TikuSDR when its last window has closed. */
static void sdr_closed(void *state, uint32_t window)
{
    sdr_t *st = state;

    if (st->splash && window == st->splash_id) {
        tiku_surface_free(st->splash);
        st->splash = NULL;
    } else if (st->about && window == st->about_id) {
        tiku_surface_free(st->about);
        st->about = NULL;
        st->about_id = 0;
    } else if (window == st->id) {
        st->id = 0;
        sdr_about_drop(st);
    }
}

/** @brief Read the boards again.  The first time the desktop has listed
 *         them, an ESP32-C61 among them is borrowed, or the alert says
 *         there is none; one that appears while the alert is up is
 *         borrowed and the alert closed. */
static void look(sdr_t *st)
{
    if (!sdr_targets(st)) return;
    if (st->looking && !*st->names) {
        missing(st);
    } else if ((st->looking || st->alert.open) && *st->names) {
        tiku_alert_reset(&st->alert);
        (void)sdr_connect(st);          /* one try; the note says why not */
    }
    st->looking = 0;
}

/** @brief Each tick: the splash, and with the window open, the boards
 *         read every NAMES_US (every tick before the first look and while
 *         the alert is up), the link, playing and exporting, the menus
 *         published again when what they show changed, and a repaint at
 *         most every REPAINT_US. */
static void sdr_tick(void *state, int64_t now_us)
{
    sdr_t *st = state;
    int second = now_us / 1000000 != st->now / 1000000;
    unsigned spectra_before = st->model.spectra;

    st->now = now_us;
    sdr_splash_tick(st, now_us);
    if (!st->id) return;                /* no window: nothing else runs */
    if (st->looking || st->alert.open || now_us - st->names_at >= NAMES_US) {
        char before[sizeof st->names];
        snprintf(before, sizeof before, "%s", st->names);
        st->names_at = now_us;
        look(st);
        if (strcmp(before, st->names)) publish(st);
    }
    sdr_link_tick(st);
    sdr_trigger_tick(st);
    sdr_play_tick(st);
    sdr_csv_tick(st);
    if (st->model.acq_dropped) {        /* a view the RBW cannot sweep */
        st->model.acq_dropped = 0;
        snprintf(st->note, sizeof st->note, "RBW back to automatic: this "
                 "span needs more than %d slices at that resolution.",
                 SDR_SLICES_MAX);
        st->dirty = 1;
    }
    if (st->hover_valid && spectra_before != st->model.spectra)
        hover(st, st->hover_x, st->hover_y);
    if (second && (st->rec.file || st->state == SDR_RUNNING)) st->dirty = 1;
    if (menu_state(st) != st->menu_state) publish(st);
    if (st->dirty && now_us - st->painted_at >= REPAINT_US) {
        st->painted_at = now_us;
        sdr_paint(st);
    }
}

/** @brief The file panel's answer: where to record, export or save the
 *         picture, or what to play; its folder is kept for the next panel.
 *         It can come long after the asking: the board's state is checked
 *         again here. */
static void sdr_picked(void *state, uint32_t window, const char *path)
{
    sdr_t *st = state;
    int asked = st->asking;
    char *slash;

    (void)window;
    st->asking = 0;
    if (!path || !*path) return;
    snprintf(st->tape_dir, sizeof st->tape_dir, "%s", path);
    slash = strrchr(st->tape_dir, '/');
    if (slash) slash[slash == st->tape_dir] = '\0';
    if (asked == CMD_PRESET_SAVE) (void)sdr_setup_save(st, path);
    else if (asked == CMD_PRESET_OPEN) (void)sdr_setup_load(st, path);
    else if (asked == CMD_REF_SAVE) (void)sdr_reference_save(st, path);
    else if (asked == CMD_REF_OPEN) (void)sdr_reference_load(st, path);
    else if (asked == CMD_OCC_EXPORT) (void)sdr_occupancy_export(st, path);
    else if (asked == CMD_TRIGGER) (void)sdr_trigger_arm(st, path);
    else if (asked == CMD_RECORD && st->state != SDR_RUNNING)
        snprintf(st->note, sizeof st->note,
                 "Nothing was recorded: the board is no longer connected.");
    else if (asked == CMD_RECORD)
        (void)sdr_rec_start(st, path);
    else if (asked == CMD_REPLAY && st->state != SDR_IDLE)
        snprintf(st->note, sizeof st->note,
                 "Disconnect the board to play a recording.");
    else if (asked == CMD_REPLAY)
        (void)sdr_play_start(st, path);
    else if (asked == CMD_EXPORT)
        (void)sdr_csv_start(st, path);
    else if (asked == CMD_PICTURE)
        (void)sdr_picture(st, path);
    publish(st);
    sdr_paint(st);
}

/** @brief Act on what the desktop says about boards, through
 *         sdr_board_said(), then publish the menus and paint. */
static void sdr_board_news(void *state, int what, const char *text)
{
    sdr_t *st = state;

    sdr_board_said(st, what, text);
    publish(st);
    sdr_paint(st);
}

/** @brief Fill the panel's lists: the traces, for the trace being set and
 *         for the marker's trace, the trace types, and the markers. */
static void lists_init(sdr_t *st)
{
    char t[16];

    for (int c = 0; c < SDR_CHOICES; c++) tiku_choice_init(&st->choice[c]);
    for (int i = 0; i < SDR_TRACES; i++) {
        snprintf(t, sizeof t, "Trace %d", i + 1);
        tiku_choice_add(&st->choice[SDR_C_TRACE], t, 1);
        tiku_choice_add(&st->choice[SDR_C_ON], t, 1);
    }
    for (int i = 0; i < SDR_T_TYPES; i++)
        tiku_choice_add(&st->choice[SDR_C_TYPE], type_name[i], 1);
    for (int i = 0; i < SDR_MARKERS; i++) {
        snprintf(t, sizeof t, "Marker %d", i + 1);
        tiku_choice_add(&st->choice[SDR_C_MARKER], t, 1);
    }
}

int sdr_window_open(sdr_t *st)
{
    const tiku_app_services_t *sv = st->services;

    st->id = sv->open(sv->ctx, "TikuSDR", st->w, st->h);
    if (!st->id) return -1;
    if (sv->sizeable) sv->sizeable(sv->ctx, st->id);
    publish(st);
    sdr_paint(st);
    return 0;
}

/** @brief Make TikuSDR's state and open the splash, after which the window
 *         opens, or the window at once when the splash cannot be opened; no
 *         board is borrowed here.  Started with a recording, it skips the
 *         splash and plays the recording.  Otherwise the tick borrows an
 *         ESP32-C61 once the window is open and the desktop has listed its
 *         boards, or opens the alert that there is none.
 *         @return 0, or -1 when memory or a window could not be had. */
static int sdr_start(void **state, const tiku_app_services_t *services)
{
    sdr_t *st = calloc(1, sizeof *st);
    const char *doc = services->startup_document;

    if (!st) return -1;
    st->services = services;
    st->w = SDR_W;
    st->h = SDR_H;
    st->fd = -1;
    st->surface = tiku_surface_new(SDR_W, SDR_H, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->fall_on = 1;
    st->fall_percent = 36;
    st->collapsed = (1u << SDR_S_MARK) | (1u << SDR_S_DISPLAY) |
                    (1u << SDR_S_RECEIVER) | (1u << SDR_S_MEASURE);
    sdr_model_init(&st->model);
    sdr_tools_init(st);
    sdr_preferences(st, 0);
    st->live_view[0] = st->model.start;    /* it opens live */
    st->live_view[1] = st->model.stop;
    st->wide_view[0] = SDR_BAND_START;     /* Live's first wide view */
    st->wide_view[1] = SDR_BAND_STOP;
    tiku_dialog_init(&st->dialog);
    lists_init(st);
    tiku_alert_reset(&st->alert);
    st->looking = !(doc && *doc);
    if ((st->looking && sdr_splash_open(st) == 0) ||
        sdr_window_open(st) == 0) {
        if (!st->looking) (void)sdr_play_start(st, doc);
        (void)sdr_targets(st);
        publish(st);
        sdr_paint(st);
        *state = st;
        return 0;
    }
    tiku_surface_free(st->surface);
    free(st);
    return -1;
}

/** @brief Stop recording, playing and exporting, give the board back if
 *         it was taken, and free everything. */
static void sdr_stop(void *state)
{
    sdr_t *st = state;

    if (!st) return;
    sdr_preferences(st, 1);
    sdr_rec_stop(st);
    sdr_play_stop(st);
    sdr_csv_stop(st);
    sdr_disconnect_now(st);
    tiku_surface_free(st->surface);
    tiku_surface_free(st->splash);
    tiku_surface_free(st->about);
    free(st->pic);
    free(st);
}

/** @brief TikuSDR's descriptor; a loadable build exports it as
 *         tiku_app_v1. */
const tiku_app_descriptor_t tiku_sdr_app = {
    .id = "sdr", .name = "TikuSDR",
    .start = sdr_start, .stop = sdr_stop, .event = sdr_event,
    .tick = sdr_tick, .pick = sdr_pick, .picked = sdr_picked,
    .event_in = sdr_event_in, .closed = sdr_closed,
    .board_said = sdr_board_news
};
#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_sdr_app
};
#endif
