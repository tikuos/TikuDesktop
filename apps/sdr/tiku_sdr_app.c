/* tiku_sdr_app.c -- TikuSDR: the descriptor, its menus, the file panels
 * for recordings and exports, and what keys, the pointer and the tick do.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_sdr.h"

#define REPAINT_US 50000            /* at most twenty paints a second */
#define NAMES_US 1000000

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

/** @brief A command's words in a menu, where they may say more than on
 *         its button. */
static const char *menu_label(const sdr_t *st, int command)
{
    if (command == CMD_AVERAGE) return "Average";
    if (command == CMD_PEAK) return "Peak hold";
    if (command == CMD_RECORD) return st->rec.file ? "Stop Recording" :
                                                     "Record…";
    if (command == CMD_REPLAY) return st->play.file ? "Close Recording" :
                                                      "Open Recording…";
    if (command == CMD_EXPORT) return "Export CSV…";
    if (command == CMD_PICTURE) return "Save Picture…";
    return sdr_label(st, command);
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
        snprintf(e->label, sizeof e->label, "%s", menu_label(st, c));
        e->command = c;
        e->enabled = sdr_enabled(st, c);
        e->marked = (c == CMD_LIVE && sdr_model_is_live(&st->model)) ||
                    (c == CMD_BAND && sdr_model_is_band(&st->model)) ||
                    (c == CMD_PEAK && st->model.peak_on) ||
                    (c == CMD_AVERAGE && st->model.avg_on);
        if (c == CMD_QUIT) { e->sc = 'q'; e->mods = TIKU_MOD_CMD; }
    }
    return count;
}

/** @brief What the menus show, folded into one number: what can be used
 *         and what is on.  The tick publishes them again when it changes,
 *         since the board and the files move on between commands. */
static unsigned menu_state(const sdr_t *st)
{
    return (unsigned)st->state | (unsigned)(st->rec.file != NULL) << 4 |
           (unsigned)(st->play.file != NULL) << 5 |
           (unsigned)(st->csv.in != NULL) << 6 |
           (unsigned)(st->model.fresh != 0) << 7 |
           (unsigned)(st->shown[0] != '\0') << 8 |
           (unsigned)sdr_model_is_live(&st->model) << 9 |
           (unsigned)(st->model.peak_on != 0) << 10 |
           (unsigned)(st->model.avg_on != 0) << 11 |
           (unsigned)(st->alert.open != 0) << 12 |
           (unsigned)sdr_model_is_band(&st->model) << 13;
}

/** @brief Menus follow the same commands as the toolbar: the receiver's,
 *         then files -- recordings, CSV and pictures -- then tuning and the
 *         boards. */
static void publish(sdr_t *st)
{
    static const int sdr[] = {CMD_CONNECT, 0, CMD_LIVE, CMD_BAND, 0, CMD_PEAK,
                              CMD_AVERAGE, CMD_CLEAR, CMD_PAUSE, 0, CMD_QUIT};
    static const int file[] = {CMD_RECORD, CMD_REPLAY, 0, CMD_EXPORT,
                               CMD_PICTURE};
    tiku_menuset_t set;

    memset(&set, 0, sizeof set);
    set.nmenu = 4;
    set.menu[0].nitem = menu_rows(st, &set.menu[0], "SDR", sdr,
                                  (int)(sizeof sdr / sizeof sdr[0]));
    set.menu[1].nitem = menu_rows(st, &set.menu[1], "File", file,
                                  (int)(sizeof file / sizeof file[0]));
    set.menu[2].nitem = tune_rows(st, &set.menu[2]);
    set.menu[3].nitem = device_rows(st, &set.menu[3]);
    if (!set.menu[3].nitem) set.nmenu = 3;
    st->menu_state = menu_state(st);
    st->services->menus(st->services->ctx, st->id, &set);
}

/** @brief A new view, @p start to @p stop fitted, the trace and waterfall
 *         started afresh and written into a recording; the same view again
 *         is left alone, and a recording being played keeps its own. */
static void show(sdr_t *st, double start, double stop)
{
    if (st->play.file) return;
    sdr_range_fit(&start, &stop);
    if (start == st->model.start && stop == st->model.stop) return;
    st->shown[0] = '\0';                /* a recording no longer shown */
    sdr_model_range(&st->model, start, stop);
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

/** @brief Field @p i opened over the view's value, all of it selected, so
 *         the first key typed replaces it. */
static void field_open(sdr_t *st, int i)
{
    char t[32];

    sdr_model_field(&st->model, i, t, sizeof t);
    tiku_field_start(&st->field[i], t);
    tiku_field_input_init(&st->input[i]);
}

/** @brief What was typed in field @p i, applied: Center or Span keep the
 *         other of the two, Start or Stop the other end -- or the span,
 *         when the end typed passes it.  What is no frequency is said, and
 *         the field shows the view again. */
static void field_apply(sdr_t *st, int i)
{
    const sdr_model_t *m = &st->model;
    double v, a = m->start, b = m->stop, span = b - a, c = (a + b) / 2;
    char now[32];

    sdr_model_field(m, i, now, sizeof now);
    if (!strcmp(st->field[i].text, now)) return;
    if (!sdr_mhz_parse(st->field[i].text, &v)) {
        snprintf(st->note, sizeof st->note, "%.40s is not a frequency.",
                 st->field[i].text);
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

/** @brief One command, from a button, a menu or a key. */
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
    } else if (command == CMD_LIVE)
        centre_at(st, (m->start + m->stop) / 2, SDR_LIVE_SPAN);
    else if (command == CMD_BAND) show(st, SDR_BAND_START, SDR_BAND_STOP);
    else if (command == CMD_DOWN) shift(st, -5);
    else if (command == CMD_UP) shift(st, 5);
    else if (command == CMD_PEAK) {
        m->peak_on = !m->peak_on;
        sdr_model_clear(m);
    }
    else if (command == CMD_AVERAGE) m->avg_on = !m->avg_on;
    else if (command == CMD_CLEAR) sdr_model_clear(m);
    else if (command == CMD_PAUSE) st->paused = !st->paused;
    else if (command == CMD_RECORD || command == CMD_REPLAY ||
             command == CMD_EXPORT || command == CMD_PICTURE)
        tape(st, command);
    else if (command >= CMD_TUNE && command < CMD_FIELD)
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

/** @brief What lies under the pointer, or nothing when it is elsewhere. */
static void hover(sdr_t *st, int x, int y)
{
    tiku_rect_t plot, fall;
    const sdr_model_t *m = &st->model;
    char ch[96];

    sdr_layout(st, &plot, &fall);
    st->hover[0] = '\0';
    if (!m->fresh || x < plot.x || x >= plot.x + plot.w ||
        ((y < plot.y || y >= plot.y + plot.h) &&
         (y < fall.y || y >= fall.y + fall.h)))
        return;
    int b = (x - plot.x) * m->n / plot.w;
    double mhz = sdr_model_freq(m, b);
    sdr_channels(mhz, ch, sizeof ch);
    snprintf(st->hover, sizeof st->hover,
             "%.2f MHz   %.1f dB (uncalibrated)%s%s%s", mhz,
             (double)(m->avg_on ? m->avg[b] : m->db[b]), *ch ? "   " : "",
             ch, m->sweep ? "   · click to tune here" : "");
}

/** @brief Keys tune and toggle; the toolbar takes its own. */
static int keys(sdr_t *st, const tiku_event_t *ev)
{
    int step = (ev->modifiers & TIKU_MOD_SHIFT) ? 5 : 1;

    int command = ev->key == ' ' ? CMD_PAUSE : ev->key == 'b' ? CMD_BAND :
                  ev->key == 'l' ? CMD_LIVE : ev->key == 'p' ? CMD_PEAK :
                  ev->key == 'r' ? CMD_RECORD : ev->key == 'c' ? CMD_CLEAR :
                  0;

    if (ev->key == TIKU_KEY_LEFT) shift(st, -step);
    else if (ev->key == TIKU_KEY_RIGHT) shift(st, step);
    else if (command) sdr_pick(st, st->id, command);
    else return 0;
    return 1;
}

/** @brief The pointer: click a sweep to see 40 MHz about there, wheel to
 *         step one spectrum's view. */
static void pointer(sdr_t *st, const tiku_event_t *ev)
{
    tiku_rect_t plot, fall;

    sdr_layout(st, &plot, &fall);
    if (ev->type == TIKU_EVENT_POINTER_MOVE) hover(st, ev->x, ev->y);
    else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
             st->model.sweep && st->model.fresh &&
             ev->x >= plot.x && ev->x < plot.x + plot.w &&
             ev->y >= plot.y && ev->y < fall.y + fall.h)
        centre_at(st, round(sdr_model_freq(&st->model,
                  (ev->x - plot.x) * st->model.n / plot.w)), SDR_LIVE_SPAN);
    else if (ev->type == TIKU_EVENT_WHEEL && !st->model.sweep && ev->dy)
        shift(st, ev->dy > 0 ? -1 : 1);
    st->dirty = 1;
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

/** @brief Input: an alert first, then the toolbar, keys and the pointer. */
static int sdr_event(void *state, const tiku_event_t *ev)
{
    sdr_t *st = state;
    tiku_dialog_result_t result;
    int was;

    if (ev->type == TIKU_EVENT_CLOSE) return 1;
    if (ev->type == TIKU_EVENT_RESIZE && ev->width > 400 && ev->height > 300) {
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
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE) {
        if (was < 0) return 1;
        st->dialog.focus = TIKU_DIALOG_NONE;    /* the typing given up */
        sdr_paint(st);
        return 0;
    }
    result = tiku_dialog_event(&st->dialog, ev);
    if (was >= 0 && ev->type == TIKU_EVENT_POINTER_DOWN &&
        editing(st) == was && !inside(sdr_field_rect(was), ev->x, ev->y))
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
                             sdr_field_rect(i), 1) == TIKU_FIELD_COMMITTED)
            field_apply(st, i);
    } else if (editing(st) >= 0) {
        int i = editing(st);
        (void)tiku_field_event(&st->field[i], &st->input[i], ev,
                               sdr_field_rect(i), 1);
        if (!result.taken) pointer(st, ev);
    } else if (!result.taken) {
        if (ev->type == TIKU_EVENT_KEY_DOWN) keys(st, ev);
        else pointer(st, ev);
    }
    sdr_paint(st);
    return 0;
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

/** @brief A window, the model, and the boards: nothing touched yet.  A
 *         recording it was opened with, from Tracker, plays at once and
 *         needs no board; else the tick borrows an ESP32-C61 as soon as
 *         the desktop has said which boards it has, or says there is none. */
static int sdr_start(void **state, const tiku_app_services_t *services)
{
    sdr_t *st = calloc(1, sizeof *st);

    if (!st) return -1;
    st->services = services;
    st->w = SDR_W;
    st->h = SDR_H;
    st->fd = -1;
    st->surface = tiku_surface_new(SDR_W, SDR_H, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->id = services->open(services->ctx, "TikuSDR", SDR_W, SDR_H);
    if (!st->id) { tiku_surface_free(st->surface); free(st); return -1; }
    if (services->sizeable) services->sizeable(services->ctx, st->id);
    sdr_model_init(&st->model);
    tiku_dialog_init(&st->dialog);
    tiku_alert_reset(&st->alert);
    if (services->startup_document && *services->startup_document)
        (void)sdr_play_start(st, services->startup_document);
    else
        st->looking = 1;
    (void)sdr_targets(st);
    publish(st);
    sdr_paint(st);
    *state = st;
    return 0;
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
    free(st->pic);
    free(st);
}

const tiku_app_descriptor_t tiku_sdr_app = {
    .id = "sdr", .name = "TikuSDR",
    .start = sdr_start, .stop = sdr_stop, .event = sdr_event,
    .tick = sdr_tick, .pick = sdr_pick, .picked = sdr_picked,
    .board_said = sdr_board_news
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_sdr_app
};
#endif
