/* Sensor history, display-list fidelity and the demo, without a device/X11.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <float.h>
#include <limits.h>
#include "tiku_dl.h"
#define TIKU_EXAMPLE_EMBED
#include "../../examples/apps/sensors.c"

static void history_test(void)
{
    tiku_history_t h, copy;
    tiku_sample_t samples[4], kept[4];
    assert(!tiku_history_init(&h, NULL, 4));
    assert(!tiku_history_init(&h, samples, 0));
    assert(!tiku_history_init(&h, samples, TIKU_SENSOR_CAP_MAX + 1));
    assert(tiku_history_init(&h, samples, 4));
    assert(tiku_history_status(&h, 1, 0, 10) == TIKU_SENSOR_EMPTY);
    assert(!tiku_history_push(&h, -1, 3, 1));
    assert(!tiku_history_push(&h, 0, NAN, 1));
    assert(!tiku_history_push(&h, 0, INFINITY, 1));
    for (int i = 0; i < 8; i++) assert(tiku_history_push(&h, i * 10, i - 5, 1));
    assert(h.count == 4 && tiku_history_at(&h, 0)->time_us == 40);
    assert(!tiku_history_at(&h, 4));
    assert(!tiku_history_push(&h, 60, 7, 1));
    assert(tiku_history_push(&h, 70, 8, 1) && h.count == 4);
    assert(tiku_history_at(&h, 3)->value == 8);
    float min, max;
    assert(tiku_history_range(&h, 40, 70, &min, &max) && min == -1 && max == 8);
    assert(!tiku_history_range(&h, 71, 99, &min, &max));
    assert(tiku_history_nearest(&h, 54)->time_us == 50);
    assert(tiku_history_nearest(&h, 56)->time_us == 60);
    assert(tiku_history_status(&h, 1, 75, 10) == TIKU_SENSOR_LIVE);
    assert(tiku_history_status(&h, 1, 81, 10) == TIKU_SENSOR_STALE);
    assert(tiku_history_status(&h, 0, 75, 10) == TIKU_SENSOR_DISCONNECTED);
    assert(tiku_history_push(&h, 80, NAN, 0));
    assert(tiku_history_status(&h, 1, 80, 10) == TIKU_SENSOR_STALE);
    assert(!tiku_history_nearest(&h, 80)->valid);
    assert(tiku_history_init(&copy, kept, 4));
    assert(tiku_history_copy(&copy, &h));
    assert(tiku_history_push(&h, INT64_MAX, FLT_MAX, 1));
    assert(tiku_history_at(&copy, 3)->time_us == 80);
    assert(tiku_history_nearest(&h, INT64_MAX)->value == FLT_MAX);
    assert(!tiku_history_copy(&h, &h));
    tiku_history_t alias;
    assert(tiku_history_init(&alias, samples + 1, 3));
    assert(!tiku_history_copy(&alias, &h));
    puts("PASS: bounded history, wrap, snapshots, invalid values, time and status");
}

static tiku_sensor_span_t spans[32];
static int nspan;
static void collect(void *ctx, const tiku_sensor_span_t *span)
{
    (void)ctx;
    assert(nspan < 32);
    spans[nspan++] = *span;
}

static void reduction_test(void)
{
    tiku_history_t h;
    tiku_sample_t data[100];
    assert(tiku_history_init(&h, data, 100));
    for (int i = 0; i < 100; i++)
        assert(tiku_history_push(&h, i, i == 42 ? 100 : i == 43 ? -50 : 1, 1));
    nspan = 0;
    tiku_history_reduce(&h, 0, 99, 4, 10, collect, NULL);
    assert(nspan == 4 && !spans[0].connect && spans[1].connect);
    assert(spans[1].max == 100 && spans[1].min == -50);
    nspan = 0;
    tiku_history_reduce(&h, 0, 99, 1, 10, collect, NULL);
    assert(nspan == 1 && spans[0].max == 100 && spans[0].min == -50);
    assert(tiku_history_init(&h, data, 100));
    tiku_history_push(&h, 0, 1, 1);
    tiku_history_push(&h, 1, 0, 0);
    tiku_history_push(&h, 2, 2, 1);
    tiku_history_push(&h, 90, 3, 1);
    tiku_history_push(&h, 91, 4, 1);
    nspan = 0;
    tiku_history_reduce(&h, 0, 99, 100, 10, collect, NULL);
    assert(nspan == 4 && !spans[1].connect && !spans[2].connect && spans[3].connect);
    nspan = 0;
    tiku_history_reduce(&h, 0, 99, 1, 10, collect, NULL);
    assert(nspan == 3 && !spans[1].connect && !spans[2].connect);
    puts("PASS: peak-preserving reduction, explicit gaps and timestamp gaps");
}

static void scale_test(void)
{
    tiku_sample_t data[2];
    tiku_history_t h;
    tiku_history_init(&h, data, 2);
    tiku_history_push(&h, 0, 5, 1);
    tiku_sensor_plot_t plot = { .count = 1, .autoscale = 1, .to_us = 1 };
    plot.trace[0].history = &h;
    double min, max;
    assert(tiku_sensor_plot_scale(&plot, &min, &max) && min < 5 && max > 5);
    plot.trace[0].hidden = 1;
    assert(tiku_sensor_plot_scale(&plot, &min, &max) && max < 5);
    plot.trace[0].hidden = 0;
    tiku_history_push(&h, 1, FLT_MAX, 1);
    assert(tiku_sensor_plot_scale(&plot, &min, &max) && isfinite(max));
    plot.autoscale = 0;
    assert(!tiku_sensor_plot_scale(&plot, &min, &max));
    plot.min = -FLT_MAX; plot.max = FLT_MAX;
    assert(tiku_sensor_plot_scale(&plot, &min, &max));
    plot.max = NAN;
    assert(!tiku_sensor_plot_scale(&plot, &min, &max));
    puts("PASS: fixed/auto scales, constant traces, hidden series and extremes");
}

static void widgets(tiku_surface_t *s)
{
    tiku_sample_t data[10];
    tiku_history_t h;
    tiku_history_init(&h, data, 10);
    for (int i = 0; i < 10; i++) tiku_history_push(&h, i * 100000, i - 5, i != 4);
    tiku_sensor_plot_t plot = { .count = 1, .autoscale = 1, .to_us = 1000000,
        .unit = "Voltage (V)", .threshold_on = 1, .threshold = 0 };
    plot.trace[0] = (tiku_sensor_trace_t){&h, "Probe", TIKU_C_SELECT, 0, 0};
    tiku_sensor_plot_draw(&plot, s, (tiku_rect_t){10, 10, 480, 240});
    tiku_sensor_spark_draw(&h, s, (tiku_rect_t){10, 265, 130, 70},
                            0, 1000000, 500000, TIKU_C_SELECT);
    tiku_sensor_readout_draw(s, (tiku_rect_t){155, 265, 180, 100},
                              "Probe", "V", 3.14, 2, TIKU_SENSOR_LIVE);
    tiku_sensor_meter_draw(s, (tiku_rect_t){350, 265, 140, 100},
                            3, -5, 5, "V", 1, 1, 2, TIKU_SENSOR_LIVE);
    tiku_sensor_readout_draw(s, (tiku_rect_t){10, 380, 230, 100},
                              "Probe", "V", 0, 2, TIKU_SENSOR_DISCONNECTED);
    tiku_sensor_meter_draw(s, (tiku_rect_t){255, 380, 235, 100},
                            NAN, 5, 5, "V", 0, 0, 0, TIKU_SENSOR_EMPTY);
}

static void meter_gap_test(void)
{
    tiku_surface_t *s = tiku_surface_new(100, 80, TIKU_C_PANEL);
    assert(s);
    tiku_rect_t r = {0, 0, 100, 80};
    tiku_sensor_meter_draw(s, r, 0, -10, 10, "V", 0, 0, 0, TIKU_SENSOR_LIVE);
    assert(s->px[10 * 100 + 49] == TIKU_C_SELECT);
    assert(s->px[10 * 100 + 50] == TIKU_C_DOC);
    tiku_sensor_meter_draw(s, r, 0, -10, 10, "V", 1, 0, 0, TIKU_SENSOR_LIVE);
    assert(s->px[29 * 100 + 10] == TIKU_C_DOC);
    assert(s->px[30 * 100 + 10] == TIKU_C_SELECT);
    tiku_sensor_meter_draw(s, r, FLT_MAX, -10, 10, "V", 0, 0, 0, TIKU_SENSOR_LIVE);
    assert(s->px[10 * 100 + 93] == TIKU_C_SELECT);
    tiku_sensor_meter_draw(s, r, -FLT_MAX, -10, 10, "V", 0, 0, 0, TIKU_SENSOR_LIVE);
    assert(s->px[10 * 100 + 6] == TIKU_C_DOC);
    tiku_sensor_meter_draw(s, r, 10, -10, 10, "V", 0, 0, 0, TIKU_SENSOR_DISCONNECTED);
    assert(s->px[10 * 100 + 50] == TIKU_C_DOC);
    tiku_sample_t data[5];
    tiku_history_t h;
    tiku_history_init(&h, data, 5);
    for (int i = 0; i < 5; i++) tiku_history_push(&h, i, 5, i != 2);
    tiku_sensor_spark_draw(&h, s, (tiku_rect_t){0, 0, 100, 20}, 0, 4, 0, TIKU_C_SELECT);
    assert(s->px[10 * 100 + 10] == TIKU_C_SELECT);
    assert(s->px[10 * 100 + 60] == TIKU_C_DOC);
    assert(s->px[10 * 100 + 90] == TIKU_C_SELECT);
    tiku_surface_free(s);
    puts("PASS: signed meters, clamping, missing values and visible plot gaps");
}

static void paint_test(void)
{
    for (int theme = 0; theme < 3; theme++) {
        tiku_theme_set(theme == 1 ? tiku_theme_dark() :
                        theme == 2 ? tiku_theme_mono() : NULL);
        tiku_surface_t *a = tiku_surface_new(500, 490, TIKU_C_PANEL);
        tiku_surface_t *b = tiku_surface_new(500, 490, TIKU_C_PANEL);
        tiku_dl_t *dl = tiku_dl_new();
        assert(a && b && dl);
        a->record = dl;
        widgets(a);
        a->record = NULL;
        assert(!tiku_dl_misses(dl));
        size_t length = tiku_dl_flat_size(dl), written = 0;
        void *wire = malloc(length);
        assert(wire && tiku_dl_flatten(dl, wire, length, &written));
        tiku_dl_t *back = tiku_dl_unflatten(wire, written, NULL);
        assert(back);
        tiku_dl_play(back, b);
        assert(!memcmp(a->px, b->px, 500 * 490 * sizeof *a->px));
        char words[64000];
        tiku_dl_say(dl, words, sizeof words);
        assert(strstr(words, "Disconnected") && strstr(words, "Invalid range"));
        /* A reading that is not live shows a dash, never a number that
         * reads as current: the whole point of the status. */
        assert(strstr(words, "-- V"));
        assert(!strstr(words, "0.00 V"));
        tiku_rect_t clip = {20, 20, 20, 20};
        tiku_clip_set(a, clip);
        widgets(a);
        assert(!memcmp(&a->clip, &clip, sizeof clip));
        for (int y = 0; y < 490; y++) for (int x = 0; x < 500; x++)
            if (x < 20 || x >= 40 || y < 20 || y >= 40)
                assert(a->px[y * 500 + x] == b->px[y * 500 + x]);
        free(wire); tiku_dl_free(back); tiku_dl_free(dl);
        tiku_surface_free(a); tiku_surface_free(b);
    }
    tiku_theme_set(NULL);
    puts("PASS: widget clipping, status text, themes and exact display-list replay");
}

static int frames, opens;
static uint32_t open_window(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)title; (void)w; (void)h; return ++opens; }
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 0; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{ (void)ctx; (void)id; (void)set; return 0; }

static void demo_test(void)
{
    tiku_app_services_t services = {.open = open_window, .frame = frame, .menus = menus};
    void *state = NULL;
    assert(!sensor_start(&state, &services));
    sensor_demo_t *st = state;
    assert(!st->history[0].count);
    for (int i = 0; i < 300; i++) sensor_tick(st, i * 100000);
    int before = frames;
    sensor_tick(st, 29900001);
    assert(frames == before);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-sensor-monitor.png"));
    tiku_font_set_size(16);
    paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-sensor-monitor-large.png"));
    tiku_font_set_size(12);
    tiku_event_t event = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = 30, .y = 158};
    sensor_event(st, &event);
    assert(!st->paused);
    event.type = TIKU_EVENT_POINTER_UP;
    sensor_event(st, &event);
    assert(st->paused && st->frozen[0].count == 300);
    int64_t saved = tiku_history_at(&st->frozen[0], 0)->time_us;
    for (int i = 300; i < 1500; i++) {
        tiku_history_push(&st->history[0], 60000000 + i * 100000, i, 1);
    }
    assert(tiku_history_at(&st->frozen[0], 0)->time_us == saved);
    assert(tiku_history_at(&st->history[0], 0)->time_us > saved);
    sensor_pick(st, st->id, PAUSE);
    assert(!st->paused);
    sensor_pick(st, st->id, CONNECTION);
    assert(!st->connected);
    sensor_tick(st, 150000000);
    assert(tiku_history_status(&st->history[0], st->connected,
                               st->now, 500000) == TIKU_SENSOR_DISCONNECTED);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-sensor-disconnected.png"));
    event.type = TIKU_EVENT_POINTER_DOWN; event.x = 200; event.y = 300;
    sensor_event(st, &event);
    assert(st->plot.cursor_on && strstr(st->inspect, "No data"));
    sensor_pick(st, st->id, RANGE);
    assert(st->window_us == 60000000);
    sensor_pick(st, st->id, SCALE);
    assert(!st->plot.autoscale);
    sensor_pick(st, st->id, TRACE);
    assert(st->plot.trace[1].hidden && opens == 1);
    sensor_stop(st);
    puts("PASS: demo lifecycle, throttling, buttons, pause while wrapping, disconnect and cursor");
}

int main(void)
{
    history_test(); reduction_test(); scale_test(); meter_gap_test();
    for (int size = 12; size <= 16; size += 4) {
        tiku_font_set_size(size);
        paint_test();
    }
    tiku_font_set_size(12);
    demo_test();
    return 0;
}
