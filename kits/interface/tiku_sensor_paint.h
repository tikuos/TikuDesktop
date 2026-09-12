/* Sensor widgets composed from portable kit drawing operations.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_SENSOR_PAINT_H_
#define TIKU_SENSOR_PAINT_H_
#include "tiku_sensor.h"
#include "tiku_gfx.h"

typedef struct {
    const tiku_history_t *history;
    const char *label;            /* borrowed, lifetime of the draw call */
    tiku_rgb_t ink;
    int hidden, dashed;          /* distinguish traces without colour */
} tiku_sensor_trace_t;

typedef struct {
    tiku_sensor_trace_t trace[TIKU_SENSOR_TRACES];
    int count, autoscale;
    int64_t from_us, to_us, gap_us;
    float min, max;              /* fixed scale; auto considers visible traces */
    const char *unit;            /* all traces share a unit and axis */
    int threshold_on;
    float threshold;
    int cursor_on;
    int64_t cursor_us;           /* application chooses time; no hidden state */
} tiku_sensor_plot_t;

/** Plot interior, shared by painting and application cursor hit testing. */
tiku_rect_t tiku_sensor_plot_area(tiku_rect_t bounds);
/** Effective axis range; returns false for invalid fixed ranges. */
int tiku_sensor_plot_scale(const tiku_sensor_plot_t *plot, double *min, double *max);
/** Labelled time-series plot. X labels are seconds relative to to_us.
 * Tiny bounds draw only the frame. Hidden traces do not affect autoscaling. */
void tiku_sensor_plot_draw(const tiku_sensor_plot_t *plot,
                          tiku_surface_t *s, tiku_rect_t bounds);
/** Compact history trace; no axes. Uses the same peak/gap rules as plots. */
void tiku_sensor_spark_draw(const tiku_history_t *h, tiku_surface_t *s,
    tiku_rect_t bounds, int64_t from, int64_t to, int64_t gap, tiku_rgb_t ink);
/** Value, unit and status. Non-live readings show a dash, not an apparent zero. */
void tiku_sensor_readout_draw(tiku_surface_t *s, tiku_rect_t bounds,
    const char *label, const char *unit, float value, int decimals,
    tiku_sensor_status_t status);
/** Labelled horizontal/vertical meter; signed ranges allowed, value clamped.
 * threshold_on draws a marker and changes fill above threshold. */
void tiku_sensor_meter_draw(tiku_surface_t *s, tiku_rect_t bounds,
    float value, float min, float max, const char *unit, int vertical,
    int threshold_on, float threshold, tiku_sensor_status_t status);

#endif
