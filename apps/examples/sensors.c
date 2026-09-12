/* Sensor Monitor: a simulated source exercising the shared sensor widgets.
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_dialog.h"
#include "tiku_sensor_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

#define SENSOR_W 780
#define SENSOR_H 550
#define SAMPLES 1024
enum { PAUSE = 1, RANGE, SCALE, CONNECTION, GAPS, TRACE, QUIT };

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface;
    uint32_t id;
    tiku_dialog_t dialog;
    tiku_history_t history[2], frozen[2];
    tiku_sample_t storage[2][SAMPLES], snapshot[2][SAMPLES];
    tiku_sensor_plot_t plot;
    int paused, connected, gaps, frozen_connected;
    int64_t origin, last_tick, now, frozen_time, window_us;
    unsigned sequence;
    char inspect[160];
} sensor_demo_t;

static const tiku_rect_t CHART = {16, 190, 748, 282};

/** The same controls are reachable from keyboard, pointer and menu. */
static const char *command_label(const sensor_demo_t *st, int command)
{
    switch (command) {
    case PAUSE: return st->paused ? "Resume live" : "Pause view";
    case RANGE: return st->window_us == 30000000 ? "Range: 30s" : "Range: 60s";
    case SCALE: return st->plot.autoscale ? "Scale: Auto" : "Scale: Fixed";
    case CONNECTION: return st->connected ? "Disconnect" : "Reconnect";
    case GAPS: return st->gaps ? "Gaps: On" : "Gaps: Off";
    case TRACE: return st->plot.trace[1].hidden ? "Show sensor 2" : "Hide sensor 2";
    default: return "Quit";
    }
}

/** Stable rectangles registered before both input and paint. */
static void controls(sensor_demo_t *st)
{
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, QUIT);
    for (int i = PAUSE; i <= TRACE; i++) {
        tiku_rect_t r = {16 + (i - 1) * 126, 146, 118, 30};
        tiku_dialog_add(&st->dialog, i, r, TIKU_DIALOG_BUTTON, 1);
    }
    tiku_dialog_end(&st->dialog);
}

/** Menu state follows the same commands as the toolbar. */
static void publish(sensor_demo_t *st)
{
    tiku_menuset_t menus = {0};
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Sensors");
    menus.menu[0].nitem = QUIT;
    for (int i = PAUSE; i <= QUIT; i++) {
        snprintf(menus.menu[0].item[i - 1].label,
                 sizeof menus.menu[0].item[i - 1].label, "%s", command_label(st, i));
        menus.menu[0].item[i - 1].command = i;
        menus.menu[0].item[i - 1].enabled = 1;
    }
    st->services->menus(st->services->ctx, st->id, &menus);
}

/** Rebind the view, not acquisition, when the person pauses. */
static void view(sensor_demo_t *st)
{
    st->plot.to_us = st->paused ? st->frozen_time : st->now;
    st->plot.from_us = st->plot.to_us - st->window_us;
    if (st->plot.cursor_on && st->plot.cursor_us < st->plot.from_us) {
        st->plot.cursor_on = 0;
        st->inspect[0] = '\0';
    }
    for (int i = 0; i < 2; i++)
        st->plot.trace[i].history = st->paused ? &st->frozen[i] : &st->history[i];
}

/** Current readouts and the two compact visualizations. */
static void summaries(sensor_demo_t *st)
{
    for (int i = 0; i < 2; i++) {
        const tiku_history_t *h = st->plot.trace[i].history;
        const tiku_sample_t *last = tiku_history_at(h, h->count - 1);
        tiku_sensor_status_t status = tiku_history_status(h,
            st->paused ? st->frozen_connected : st->connected,
            st->plot.to_us, 500000);
        tiku_sensor_readout_draw(st->surface,
            (tiku_rect_t){16 + 248 * i, 42, 232, 92},
            st->plot.trace[i].label, "°C", last ? last->value : 0, 2, status);
        if (!i) tiku_sensor_meter_draw(st->surface,
            (tiku_rect_t){512, 80, 252, 54}, last ? last->value : 0,
            -10, 40, "°C", 0, 1, 27, status);
    }
    tiku_sensor_spark_draw(st->plot.trace[0].history, st->surface,
        (tiku_rect_t){512, 42, 252, 32}, st->plot.from_us, st->plot.to_us,
        st->plot.gap_us, st->plot.trace[0].ink);
}

/** No extra windows: all widgets and status fit one classic panel. */
static void paint(sensor_demo_t *st)
{
    view(st);
    controls(st);
    tiku_fill(st->surface, (tiku_rect_t){0, 0, SENSOR_W, SENSOR_H}, TIKU_C_PANEL);
    tiku_text_line_draw(st->surface, (tiku_rect_t){16, 8, 748, 26},
        TIKU_MEASURE_BOLD, "Sensor Monitor - simulated data, no device connection",
        TIKU_C_TEXT);
    summaries(st);
    for (int i = 0; i < st->dialog.count; i++) {
        int id = st->dialog.control[i].id;
        unsigned flags = tiku_dialog_pressed(&st->dialog, id) ? TIKU_S_PRESSED : 0;
        if (tiku_dialog_focused(&st->dialog, id)) flags |= TIKU_S_FOCUS;
        tiku_ui_button(st->surface, st->dialog.control[i].rect,
                       command_label(st, id), flags);
    }
    tiku_sensor_plot_draw(&st->plot, st->surface, CHART);
    const char *status = st->paused ?
        "Paused snapshot - acquisition continues. Resume live to catch up." :
        st->connected ? "Live | 10 samples/s | warning line: 27 °C" :
                        "Simulated device disconnected - readings unavailable";
    tiku_text_line_draw(st->surface, (tiku_rect_t){16, 482, 748, 24},
                        TIKU_MEASURE_PLAIN, status, TIKU_C_TEXT);
    tiku_text_line_draw(st->surface, (tiku_rect_t){16, 510, 748, 24},
        TIKU_MEASURE_PLAIN, st->inspect[0] ? st->inspect :
        "Click the plot to inspect samples. Tab selects controls; Space activates.",
        TIKU_C_TEXT);
    st->services->frame(st->services->ctx, st->id, st->surface->px,
                         SENSOR_W, SENSOR_H);
}

/** A pause takes a true snapshot, so ring wrap cannot change a frozen view. */
static int sensor_pick(void *state, uint32_t window, int command)
{
    sensor_demo_t *st = state;
    (void)window;
    if (command == QUIT) return 1;
    if (command == PAUSE) {
        if (!st->paused) {
            for (int i = 0; i < 2; i++)
                tiku_history_copy(&st->frozen[i], &st->history[i]);
            st->frozen_time = st->now;
            st->frozen_connected = st->connected;
        }
        st->paused = !st->paused;
    } else if (command == RANGE) {
        st->window_us = st->window_us == 30000000 ? 60000000 : 30000000;
    } else if (command == SCALE) st->plot.autoscale = !st->plot.autoscale;
    else if (command == CONNECTION) st->connected = !st->connected;
    else if (command == GAPS) st->gaps = !st->gaps;
    else if (command == TRACE) st->plot.trace[1].hidden = !st->plot.trace[1].hidden;
    st->inspect[0] = '\0';
    st->plot.cursor_on = 0;
    publish(st);
    paint(st);
    return 0;
}

/** Inspect a real timestamp, never an interpolated value across missing data. */
static void inspect(sensor_demo_t *st, int x)
{
    tiku_rect_t r = tiku_sensor_plot_area(CHART);
    st->plot.cursor_us = st->plot.from_us +
        (int64_t)((double)(x - r.x) / (r.w - 1) * st->window_us);
    st->plot.cursor_on = 1;
    char values[2][64];
    for (int i = 0; i < 2; i++) {
        const tiku_sample_t *s = tiku_history_nearest(st->plot.trace[i].history,
                                                     st->plot.cursor_us);
        if (!s || !s->valid ||
            llabs(s->time_us - st->plot.cursor_us) > st->plot.gap_us / 2)
            snprintf(values[i], sizeof values[i], "%d: No data", i + 1);
        else snprintf(values[i], sizeof values[i], "%d: %.2f °C @ %.3fs",
                       i + 1, s->value, (double)(s->time_us - 60000000) / 1e6);
    }
    snprintf(st->inspect, sizeof st->inspect, "%s   |   %s", values[0], values[1]);
}

static int sensor_event(void *state, const tiku_event_t *event)
{
    sensor_demo_t *st = state;
    if (event->type == TIKU_EVENT_CLOSE) return 1;
    if (event->type == TIKU_EVENT_KEY_DOWN && event->key == TIKU_KEY_ESCAPE) return 1;
    controls(st);
    tiku_dialog_result_t result = tiku_dialog_event(&st->dialog, event);
    if (result.activated != TIKU_DIALOG_NONE)
        return sensor_pick(st, st->id, result.activated);
    tiku_rect_t r = tiku_sensor_plot_area(CHART);
    if (event->type == TIKU_EVENT_POINTER_DOWN && event->button == 1 &&
        event->x >= r.x && event->x < r.x + r.w &&
        event->y >= r.y && event->y < r.y + r.h) inspect(st, event->x);
    paint(st);
    return 0;
}

/** At most ten paints/s, independent of the desktop's tick frequency. */
static void sensor_tick(void *state, int64_t now_us)
{
    sensor_demo_t *st = state;
    if (st->origin < 0) st->origin = now_us;
    if (st->last_tick >= 0 && now_us - st->last_tick < 100000) return;
    st->last_tick = now_us;
    st->now = 60000000 + now_us - st->origin;
    double seconds = (double)(st->now - 60000000) / 1e6;
    if (st->connected) {
        int valid = !st->gaps || st->sequence % 120 < 112;
        for (int i = 0; i < 2; i++) {
            float value = (float)(23 + i * 2 + 4 * sin(seconds * .65 + i) +
                                   .2 * sin(seconds * 7));
            tiku_history_push(&st->history[i], st->now, value, valid);
        }
    }
    st->sequence++;
    paint(st);
}

static int sensor_start(void **state, const tiku_app_services_t *services)
{
    sensor_demo_t *st = calloc(1, sizeof *st);
    if (!st) return -1;
    st->services = services;
    st->surface = tiku_surface_new(SENSOR_W, SENSOR_H, TIKU_C_PANEL);
    if (!st->surface) { free(st); return -1; }
    st->id = services->open(services->ctx, "Sensor Monitor", SENSOR_W, SENSOR_H);
    if (!st->id) { tiku_surface_free(st->surface); free(st); return -1; }
    st->connected = st->gaps = 1;
    st->now = 60000000;
    st->origin = st->last_tick = -1;
    st->window_us = 30000000;
    st->plot.count = 2;
    st->plot.autoscale = 1;
    st->plot.min = 15; st->plot.max = 35;
    st->plot.unit = "Temperature (°C)";
    st->plot.gap_us = 500000;
    st->plot.threshold_on = 1; st->plot.threshold = 27;
    for (int i = 0; i < 2; i++) {
        tiku_history_init(&st->history[i], st->storage[i], SAMPLES);
        tiku_history_init(&st->frozen[i], st->snapshot[i], SAMPLES);
        st->plot.trace[i].label = i ? "Ambient" : "Probe";
        st->plot.trace[i].ink = i ? TIKU_RGB(154, 81, 26) : TIKU_RGB(36, 100, 153);
        st->plot.trace[i].dashed = i;
    }
    tiku_dialog_init(&st->dialog);
    publish(st);
    paint(st);
    *state = st;
    return 0;
}

static void sensor_stop(void *state)
{
    sensor_demo_t *st = state;
    if (!st) return;
    tiku_surface_free(st->surface);
    free(st);
}

const tiku_app_descriptor_t tiku_example_sensors = {
    .id = "org.tikuos.example.sensors", .name = "Sensor Monitor",
    .start = sensor_start, .stop = sensor_stop, .event = sensor_event,
    .pick = sensor_pick, .tick = sensor_tick
};
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_example_sensors
};
#endif
#ifndef TIKU_EXAMPLE_EMBED
int main(void) { return tiku_client_run(&tiku_example_sensors); }
#endif
