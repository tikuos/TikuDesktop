/* Sensor history and plot geometry, independent of drawing and transport.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_SENSOR_H_
#define TIKU_SENSOR_H_
#include <stddef.h>
#include <stdint.h>

#define TIKU_SENSOR_CAP_MAX 4096
#define TIKU_SENSOR_TRACES 4

typedef struct {
    int64_t time_us;             /* nonnegative, monotonic within a history */
    float value;
    int valid;                   /* zero marks a gap, never a zero reading */
} tiku_sample_t;

typedef struct {
    tiku_sample_t *samples;       /* caller-owned, no allocation in the kit */
    size_t capacity, count, head; /* head is the next insertion slot */
} tiku_history_t;

typedef enum {
    TIKU_SENSOR_EMPTY, TIKU_SENSOR_LIVE,
    TIKU_SENSOR_STALE, TIKU_SENSOR_DISCONNECTED
} tiku_sensor_status_t;

typedef struct {
    int column, connect;         /* may connect to the preceding span */
    float first, last, min, max; /* preserve peaks within each pixel column */
} tiku_sensor_span_t;

typedef void (*tiku_sensor_span_fn)(void *ctx, const tiku_sensor_span_t *span);

/** Initialize with 1..CAP_MAX slots. Invalid arguments leave an empty history. */
int tiku_history_init(tiku_history_t *h, tiku_sample_t *storage, size_t capacity);
/** Append or replace an equal timestamp; reject older times and invalid numbers.
 * Missing samples (valid == 0) are accepted and break the plotted line. */
int tiku_history_push(tiku_history_t *h, int64_t time_us, float value, int valid);
/** Logical index zero is the oldest retained sample; NULL if out of range. */
const tiku_sample_t *tiku_history_at(const tiku_history_t *h, size_t index);
/** Copy the newest samples into an independently initialized history. */
int tiku_history_copy(tiku_history_t *dest, const tiku_history_t *source);
/** Latest sample state. stale_us <= 0 disables the age limit. */
tiku_sensor_status_t tiku_history_status(const tiku_history_t *h,
    int connected, int64_t now_us, int64_t stale_us);
/** Min/max of valid samples in the inclusive window; false if none. */
int tiku_history_range(const tiku_history_t *h, int64_t from, int64_t to,
                       float *min, float *max);
/** Nearest sample, including invalid samples. Never interpolate over a gap. */
const tiku_sample_t *tiku_history_nearest(const tiku_history_t *h, int64_t time);
/** Stream screen-column summaries, O(samples), constant scratch space.
 * Explicit gaps and intervals > gap_us break connections; <= 0 disables the
 * interval limit. Width must be 1..8192. The callback must not mutate history. */
void tiku_history_reduce(const tiku_history_t *h, int64_t from, int64_t to,
    int width, int64_t gap_us, tiku_sensor_span_fn emit, void *ctx);
/** Human-readable status, shared by readouts and application status rows. */
const char *tiku_sensor_status_text(tiku_sensor_status_t status);

#endif
