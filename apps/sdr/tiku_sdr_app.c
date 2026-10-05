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

/** @brief The Tune menu's rows: Wi-Fi's channels, then BLE's advertising;
 *         none while a recording says what is shown. */
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
    return n;
}

/** @brief The Device menu: the mounted boards, the chosen one marked. */
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

/** @brief A command's words in a menu, where they say more than on the
 *         panel: the traces, their types and the markers by name. */
static void menu_label(const sdr_t *st, int command, char *out, size_t max)
{
    const char *t = NULL;

    if (command >= CMD_TYPE && command < CMD_TYPE + SDR_T_TYPES)
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
    else if (command == CMD_PLANS) t = "Channel Plans";
    else t = sdr_label(st, command);
    if (t) snprintf(out, max, "%s", t);
}

/** @brief Whether @p command's menu row shows a mark: the view shown, the
 *         trace and marker the panel sets, a type, a setting that is on. */
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
           (c == CMD_PLANS && st->plans_on);
}

/** @brief One menu of @p commands, 0 a separator, each with its words,
 *         whether it can be used now, and whether it is on. */
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
 *         and what is on.  The tick publishes them again when it changes,
 *         since the board and the files move on between commands. */
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
                 (unsigned)(st->plans_on != 0) << 13 |
                 (unsigned)(m->track != 0) << 14;

    v = v * 31u + (unsigned)st->trace_sel;
    v = v * 31u + (unsigned)m->mark_sel;
    for (int t = 0; t < SDR_TRACES; t++) v = v * 31u + (unsigned)m->type[t];
    for (int k = 0; k < SDR_MARKERS; k++) v = v * 31u + (unsigned)m->mark[k].on;
    return v;
}

/** @brief Menus follow the panel: the receiver's, files -- recordings, CSV
 *         and pictures -- the traces, the markers, tuning and the boards. */
static void publish(sdr_t *st)
{
    static const int sdr[] = {CMD_ABOUT, 0, CMD_CONNECT, 0, CMD_LIVE, 0,
                              CMD_PAUSE, 0, CMD_WATERFALL, CMD_PLANS, 0,
                              CMD_QUIT};
    static const int file[] = {CMD_RECORD, CMD_REPLAY, 0, CMD_EXPORT,
                               CMD_PICTURE};
    static const int trace[] = {CMD_TRACE_SEL, CMD_TRACE_SEL + 1,
                                CMD_TRACE_SEL + 2, 0, CMD_TYPE + SDR_T_OFF,
                                CMD_TYPE + SDR_T_WRITE, CMD_TYPE + SDR_T_AVG,
                                CMD_TYPE + SDR_T_MAX, CMD_TYPE + SDR_T_MIN, 0,
                                CMD_CLEAR};
    static const int marker[] = {CMD_MARK_SEL, CMD_MARK_SEL + 1,
                                 CMD_MARK_SEL + 2, CMD_MARK_SEL + 3, 0,
                                 CMD_MARK_ACTIVE, CMD_TRACK, 0,
                                 CMD_PEAK_SEARCH, CMD_NEXT_PEAK, CMD_PEAK_LEFT,
                                 CMD_PEAK_RIGHT, 0, CMD_MARK_CENTER,
                                 CMD_MARK_CLEAR};
    tiku_menuset_t set;

    if (!st->id) return;                /* the splash still stands */
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

/** @brief A new view, @p start to @p stop fitted, the traces and the
 *         spectrogram started afresh and written into a recording, and
 *         kept as the last of its kind, live or wide; the same view again
 *         is left alone, and a recording being played keeps its own. */
static void show(sdr_t *st, double start, double stop)
{
    if (st->play.file) return;
    sdr_range_fit(&start, &stop);
    if (start == st->model.start && stop == st->model.stop) return;
    st->shown[0] = '\0';                /* a recording no longer shown */
    sdr_model_range(&st->model, start, stop);
    double *kept = sdr_model_is_live(&st->model) ? st->live_view
                                                 : st->wide_view;
    kept[0] = start;
    kept[1] = stop;
    sdr_rec_view(st);
    st->dirty = 1;
}

/** @brief @p span MHz about @p mhz. */
static void centre_at(sdr_t *st, double mhz, double span)
{
    show(st, mhz - span / 2, mhz + span / 2);
}

/** @brief The view moved by @p mhz, its span kept. */
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

/** @brief Field @p i opened over what it says, all of it selected, so the
 *         first key typed replaces it. */
static void field_open(sdr_t *st, int i)
{
    char t[32];

    sdr_model_field(&st->model, i, t, sizeof t);
    tiku_field_start(&st->field[i], t);
    tiku_field_input_init(&st->input[i]);
}

/** @brief Trace @p t made type @p type; a hold begun afresh, from now. */
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

/** @brief What was typed in field @p i, applied.  Center or Span keep the
 *         other of the two, Start or Stop the other end -- or the span,
 *         when the end typed passes it; the count is 1 to 100 spectra; the
 *         marker stands where its frequency is typed.  What is not one is
 *         said, and the field shows what is so again. */
static void field_apply(sdr_t *st, int i)
{
    sdr_model_t *m = &st->model;
    double v, a = m->start, b = m->stop, span = b - a, c = (a + b) / 2;
    char now[32], *end;
    long count;

    sdr_model_field(m, i, now, sizeof now);
    if (!strcmp(st->field[i].text, now)) return;
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

/** @brief The desktop's file panel: where a recording, a CSV or a
 *         picture goes, or which recording to play.  It starts where the
 *         last file was; the answer comes to sdr_picked(). */
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
    if (command == CMD_REPLAY) {
        put = sv->pick(sv->ctx, st->id, TIKU_APP_PICK_LOCAL_OPEN, start, NULL);
    } else {
        sdr_tape_name(st, ext[command == CMD_EXPORT ? 1 :
                              command == CMD_PICTURE ? 2 : 0],
                      name, sizeof name);
        put = sv->pick(sv->ctx, st->id, TIKU_APP_PICK_SAVE, start, name);
    }
    if (put <= 0) {
        st->asking = 0;
        snprintf(st->note, sizeof st->note, "The file panel did not open.");
    }
}

/** @brief Record: ask where, or stop.  Open Recording: ask which, or close
 *         the one playing. */
static void tape(sdr_t *st, int command)
{
    if (command == CMD_RECORD && st->rec.file) {
        sdr_rec_stop(st);
    } else if (command == CMD_RECORD && st->state == SDR_RUNNING) {
        ask(st, CMD_RECORD);
    } else if (command == CMD_REPLAY && st->play.file) {
        sdr_play_stop(st);
        snprintf(st->note, sizeof st->note, "Closed %s.",
                 sdr_tape_leaf(st->play.path));
    } else if (command == CMD_REPLAY && st->state == SDR_IDLE) {
        ask(st, CMD_REPLAY);
    } else if ((command == CMD_EXPORT || command == CMD_PICTURE) &&
               sdr_enabled(st, command)) {
        ask(st, command);
    }
}

/** @brief Say, over the window, that no radio is connected: the receiver
 *         is an ESP32-C61's, lent by the desktop.  Its one answer, OK, by
 *         hand, Return or Escape, closes TikuSDR with it: opened without a
 *         radio, it has nothing to show. */
static void missing(sdr_t *st)
{
    tiku_alert_open(&st->alert, TIKU_ALERT_STOP, 0,
                    "No software-defined radio detected. TikuSDR currently "
                    "supports the ESP32-C61 as its SDR. Please connect one "
                    "through the Devices menu.", "OK");
    st->dirty = 1;
}

/** @brief The selected marker out or in, following peaks or not, moved,
 *         made the view's centre; all of them in.  What could not be done
 *         is said. */
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
        return;                         /* nothing out to move */
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

/** @brief One command, from the panel, a menu or a key. */
static int sdr_pick(void *state, uint32_t window, int command)
{
    sdr_t *st = state;
    sdr_model_t *m = &st->model;

    (void)window;
    if (command == CMD_QUIT) return 1;
    if (st->alert.open) return 0;       /* the alert is answered first */
    if (command == CMD_CONNECT) {
        if (st->state != SDR_IDLE) sdr_disconnect(st);
        else if (sdr_connect(st) < 0)   /* gone since the menu was drawn */
            snprintf(st->note, sizeof st->note,
                     "No software-defined radio is connected.");
    } else if (command == CMD_ABOUT) {
        sdr_about_open(st);
        return 0;
    } else if (command == CMD_LIVE) {   /* the last view of the other kind */
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
    else if (command == CMD_PLANS) st->plans_on = !st->plans_on;
    else if (command == CMD_PAUSE) st->paused = !st->paused;
    else if (command == CMD_RECORD || command == CMD_REPLAY ||
             command == CMD_EXPORT || command == CMD_PICTURE)
        tape(st, command);
    else if (command >= CMD_TUNE && command < CMD_CHOICE)
        /* A channel: a span narrower than a sweep is kept about it. */
        centre_at(st, 2400 + command - CMD_TUNE,
                  m->sweep ? SDR_LIVE_SPAN : m->stop - m->start);
    else if (command >= CMD_BOARD && st->state == SDR_IDLE) {
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

/** @brief A drop-down list's answer: the trace or marker the panel sets,
 *         a trace's type, or the trace the selected marker reads. */
static void chose(sdr_t *st, int c)
{
    int v = st->choice[c].selected;

    if (c == SDR_C_TRACE) st->trace_sel = v;
    else if (c == SDR_C_TYPE) set_type(st, st->trace_sel, v);
    else if (c == SDR_C_MARKER) st->model.mark_sel = v;
    else st->model.mark[st->model.mark_sel].trace = v;
}

/** @brief The bin under graticule column @p x, kept inside the trace. */
static int bin_at(const sdr_model_t *m, tiku_rect_t plot, int x)
{
    int b = (x - plot.x) * m->n / plot.w;

    return b < 0 ? 0 : b >= m->n ? m->n - 1 : b;
}

/** @brief Whether a press at (@p x, @p y) is one markers take: on the
 *         graticule, with a marker out over a spectrum. */
static int marking(const sdr_t *st, tiku_rect_t plot, int x, int y)
{
    return sdr_marks_out(&st->model) && st->model.fresh &&
           inside(plot, x, y);
}

/** @brief What lies under the pointer, or nothing when it is elsewhere:
 *         the frequency, each trace's level there, its channels. */
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
    if (!m->fresh || x < plot.x || x >= plot.x + plot.w ||
        ((y < plot.y || y >= plot.y + plot.h) &&
         (y < fall.y || y >= fall.y + fall.h)))
        return;
    int b = bin_at(m, plot, x);
    double mhz = sdr_model_freq(m, b);
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
}

/** @brief Keys tune and toggle; the panel takes its own. */
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

/** @brief A press markers take: the marker within a few pixels of it, or
 *         else the selected one, out there now and held by the pointer. */
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

/** @brief The pointer: with a marker out, a press on the graticule grabs
 *         one and drags it, and letting go puts it on the peak there; in
 *         a sweep, a press on the spectrogram -- or the graticule, with no
 *         marker out -- sees 40 MHz about there.  The wheel steps one
 *         spectrum's view. */
static void pointer(sdr_t *st, const tiku_event_t *ev)
{
    tiku_rect_t plot, fall;
    sdr_model_t *m = &st->model;

    sdr_layout(st, &plot, &fall);
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
    } else if (ev->type == TIKU_EVENT_WHEEL && !m->sweep && ev->dy &&
               (inside(plot, ev->x, ev->y) || inside(fall, ev->x, ev->y))) {
        shift(st, ev->dy > 0 ? -1 : 1);
    }
    st->dirty = 1;
}

/** @brief The panel's lists take the event first: an open one all of it,
 *         a shut one a press on it or keys while it has the focus.
 *         @return 1 when one took it. */
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

/** @brief The alert answered, by key or by hand: its OK ends TikuSDR.
 *         @return 1 when TikuSDR is done. */
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
    if (at >= 0) return 1;              /* OK: the window goes with it */
    if (redraw) sdr_paint(st);
    return 0;
}

/** @brief Input: an alert first, then the panel's lists, its other
 *         controls and fields, keys and the pointer. */
static int sdr_event(void *state, const tiku_event_t *ev)
{
    sdr_t *st = state;
    tiku_dialog_result_t result;
    int was;

    if (ev->type == TIKU_EVENT_CLOSE) return 1;
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
        if (was >= 0) {                 /* what was typed stands */
            st->dialog.focus = TIKU_DIALOG_NONE;
            field_apply(st, was);
        }
        sdr_paint(st);
        return 0;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE) {
        if (was < 0) return 1;
        st->dialog.focus = TIKU_DIALOG_NONE;    /* the typing given up */
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
        if (ev->type == TIKU_EVENT_POINTER_DOWN) {  /* it selects it all */
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

/** @brief Input in one of TikuSDR's windows: a key or a click sends the
 *         splash away, as one closes the About box (Escape and Return too);
 *         the window's own goes to sdr_event(). */
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

/** @brief A window the runtime took.  The window takes the About box with
 *         it, so the last of them going ends TikuSDR. */
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

/** @brief The boards again.  The first time the desktop has said which it
 *         has, an ESP32-C61 among them is borrowed, once, and none is said
 *         over the window; one connected while that is said is borrowed,
 *         the alert gone. */
static void look(sdr_t *st)
{
    if (!sdr_targets(st)) return;
    if (st->looking && !*st->names) {
        missing(st);
    } else if ((st->looking || st->alert.open) && *st->names) {
        tiku_alert_reset(&st->alert);
        (void)sdr_connect(st);          /* one try: a refusal is said */
    }
    st->looking = 0;
}

/** @brief The link's work and a throttled repaint.  The boards are looked
 *         at every second, and every tick while one is awaited. */
static void sdr_tick(void *state, int64_t now_us)
{
    sdr_t *st = state;

    st->now = now_us;
    sdr_splash_tick(st, now_us);
    if (!st->id) return;                /* nothing is begun under the splash */
    if (st->looking || st->alert.open || now_us - st->names_at >= NAMES_US) {
        char before[sizeof st->names];
        snprintf(before, sizeof before, "%s", st->names);
        st->names_at = now_us;
        look(st);
        if (strcmp(before, st->names)) publish(st);
    }
    sdr_link_tick(st);
    sdr_play_tick(st);
    sdr_csv_tick(st);
    if (menu_state(st) != st->menu_state) publish(st);
    if (st->dirty && now_us - st->painted_at >= REPAINT_US) {
        st->painted_at = now_us;
        sdr_paint(st);
    }
}

/** @brief The file panel's answer: where to record or export, or what to
 *         play.  It may come long after the asking, so the board's state is
 *         checked again here. */
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
    if (asked == CMD_RECORD && st->state != SDR_RUNNING)
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

/** @brief Board news: the link's handoff and the desktop's current device. */
static void sdr_board_news(void *state, int what, const char *text)
{
    sdr_t *st = state;

    sdr_board_said(st, what, text);
    publish(st);
    sdr_paint(st);
}

/** @brief The panel's lists and what they hold: three traces, each type,
 *         four markers. */
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

/** @brief The model and the boards, nothing touched yet, and the splash:
 *         the window opens as it leaves.  A recording it was opened with,
 *         from Tracker, opens the window at once and plays; else the tick
 *         borrows an ESP32-C61 once the window is up and the desktop has
 *         said which boards it has, or says there is none. */
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
    sdr_model_init(&st->model);
    st->live_view[0] = st->model.start;    /* it opens live */
    st->live_view[1] = st->model.stop;
    st->wide_view[0] = SDR_BAND_START;     /* and goes wide to the band */
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

/** @brief The board given back, if it was taken, and everything freed. */
static void sdr_stop(void *state)
{
    sdr_t *st = state;

    if (!st) return;
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

const tiku_app_descriptor_t tiku_sdr_app = {
    .id = "sdr", .name = "TikuSDR",
    .start = sdr_start, .stop = sdr_stop, .event = sdr_event,
    .tick = sdr_tick, .pick = sdr_pick, .picked = sdr_picked,
    .event_in = sdr_event_in, .closed = sdr_closed,
    .board_said = sdr_board_news
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_sdr_app
};
#endif
