/* TikuSDR observed channel activity, bounded triggers and receiver health.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_atomic_file.h"
#include "tiku_sdr.h"

void sdr_tools_init(sdr_t *st)
{
    st->occupancy.threshold = 60;
    st->trigger.pre_seconds = 3;
    st->trigger.post_seconds = 10;
    snprintf(st->book_label, sizeof st->book_label, "Event");
    st->health_visible = 1;
}

void sdr_channel(int index, char *name, size_t size, double *center, double *width)
{
    if (index < 13) {
        *center = 2412 + 5 * index; *width = 20;
        snprintf(name, size, "Wi-Fi %d", index + 1);
    } else if (index < 53) {
        int ch = index - 13;
        *width = 2;
        *center = ch == 37 ? 2402 : ch == 38 ? 2426 : ch == 39 ? 2480 :
                  ch < 11 ? 2404 + 2 * ch : 2406 + 2 * ch;
        snprintf(name, size, "BLE %d", ch);
    } else {
        *center = 2405 + 5 * (index - 53); *width = 2;
        snprintf(name, size, "15.4 %d", index - 53 + 11);
    }
}

void sdr_occupancy_reset(sdr_t *st)
{
    sdr_occupancy_t *o = &st->occupancy;
    memset(o->seen, 0, sizeof o->seen);
    memset(o->busy, 0, sizeof o->busy);
    memset(o->first, 0, sizeof o->first);
    memset(o->last, 0, sizeof o->last);
    o->initialized = 0;
    st->dirty = 1;
}

void sdr_observe(sdr_t *st)
{
    const sdr_model_t *m = &st->model;
    sdr_occupancy_t *o = &st->occupancy;
    if (!m->fresh) return;
    if (!o->initialized || o->start != m->start || o->stop != m->stop ||
        o->rate != m->rate || o->nfft != m->nfft || o->held != m->held ||
        (m->held && o->gain != m->gain)) {
        sdr_occupancy_reset(st);
        o->initialized = 1; o->start = m->start; o->stop = m->stop;
        o->rate = m->rate; o->nfft = m->nfft; o->gain = m->gain; o->held = m->held;
    }
    for (int c = 0; c < SDR_CHANNELS; c++) {
        char name[24];
        double center, width;
        int busy = 0, bins = 0;
        sdr_channel(c, name, sizeof name, &center, &width);
        /* Partial channels are unknown, not quiet. Include half-bin support. */
        if (center - width / 2 < m->lo_mhz - m->bin_mhz / 2 ||
            center + width / 2 > m->lo_mhz + (m->n - .5) * m->bin_mhz) continue;
        for (int i = 0; i < m->n; i++)
            if (fabs(sdr_model_freq(m, i) - center) <= width / 2) {
                bins++;
                if (m->db[i] >= o->threshold) busy = 1;
            }
        if (!bins) continue;
        if (!o->seen[c]) o->first[c] = m->clock;
        o->seen[c]++; o->busy[c] += (unsigned)busy; o->last[c] = m->clock;
    }
}

/** @brief Export counts and observation spans; missing channels stay unknown. */
static int occupancy_write(FILE *f, void *ctx)
{
    const sdr_t *st = ctx;
    const sdr_occupancy_t *o = &st->occupancy;
    fprintf(f, "channel,center_mhz,width_mhz,threshold_relative_db,observations,busy_observations,activity_percent,observed_span_s\n");
    for (int c = 0; c < SDR_CHANNELS; c++) {
        char name[24];
        double center, width;
        sdr_channel(c, name, sizeof name, &center, &width);
        fprintf(f, "%s,%.3f,%.3f,%.3f,%llu,%llu,", name, center, width,
                (double)o->threshold, (unsigned long long)o->seen[c],
                (unsigned long long)o->busy[c]);
        if (o->seen[c]) fprintf(f, "%.3f,%.3f\n", 100.0 * o->busy[c] / o->seen[c],
                                o->last[c] - o->first[c]);
        else fprintf(f, "NA,NA\n");
    }
    return ferror(f) ? -1 : 0;
}

int sdr_occupancy_export(sdr_t *st, const char *path)
{
    int rc = sdr_document_save(path, occupancy_write, st);
    snprintf(st->note, sizeof st->note, "%s occupancy CSV: %.105s",
             rc ? "Could not save" : "Saved", sdr_tape_leaf(path));
    return rc;
}

int sdr_trigger_arm(sdr_t *st, const char *path)
{
    if (st->state != SDR_RUNNING || st->rec.file || st->paused) return -1;
    if (sdr_rec_start(st, path)) return -1;
    st->trigger.count = st->trigger.head = st->trigger.fired = 0;
    st->trigger.armed = 1;
    snprintf(st->note, sizeof st->note, "Trigger armed: >= %.1f relative dB; up to %d s / %d lines before, %d s after.",
             (double)st->occupancy.threshold, st->trigger.pre_seconds,
             SDR_PRE_LINES, st->trigger.post_seconds);
    return 0;
}

void sdr_trigger_cancel(sdr_t *st)
{
    if (!st->trigger.armed && !st->trigger.fired) return;
    st->trigger.armed = st->trigger.fired = 0;
    st->trigger.count = 0;
    sdr_rec_stop(st);
}

void sdr_trigger_line(sdr_t *st, const char *line, int frame)
{
    sdr_trigger_t *t = &st->trigger;
    if (!t->armed) { sdr_rec_line(st, line, frame); return; }
    int i = t->head;
    t->lines[i].at = st->now; t->lines[i].frame = frame;
    snprintf(t->lines[i].text, sizeof t->lines[i].text, "%s", line);
    t->head = (i + 1) % SDR_PRE_LINES;
    if (t->count < SDR_PRE_LINES) t->count++;
    if (!frame) return;
    int hit = 0;
    for (int b = 0; b < st->model.n; b++)
        if (st->model.db[b] >= st->occupancy.threshold) hit = 1;
    if (!hit) return;
    int begin = (t->head + SDR_PRE_LINES - t->count) % SDR_PRE_LINES;
    int skip = 0;
    int64_t cutoff = st->now - (int64_t)t->pre_seconds * 1000000;
    /* Trim at complete-frame boundaries, so a sweep never starts halfway. */
    for (int j = 0; j < t->count - 1; j++) {
        int at = (begin + j) % SDR_PRE_LINES;
        if (t->lines[at].frame && (t->lines[at].at < cutoff ||
            (t->count == SDR_PRE_LINES && !skip))) skip = j + 1;
    }
    begin = (begin + skip) % SDR_PRE_LINES;
    int64_t now = st->now;
    st->rec.began = t->lines[begin].at;
    t->armed = 0; t->fired = 1; t->fired_at = now;
    for (int j = skip; j < t->count && st->rec.file; j++) {
        int at = (begin + j - skip) % SDR_PRE_LINES;
        st->now = t->lines[at].at;
        sdr_rec_line(st, t->lines[at].text, t->lines[at].frame);
    }
    st->now = now;
    if (st->rec.file) {
        sdr_rec_line(st, "TRIGGER threshold", 0);
        snprintf(st->note, sizeof st->note, "Triggered; recording %d seconds after detection.", t->post_seconds);
    }
    st->dirty = 1;
}

void sdr_trigger_tick(sdr_t *st)
{
    if (st->trigger.fired && st->now - st->trigger.fired_at >=
        (int64_t)st->trigger.post_seconds * 1000000) sdr_trigger_cancel(st);
}

void sdr_health_text(const sdr_t *st, char *out, size_t size)
{
    const char *state = st->shown[0] ? "Playback" : st->state != SDR_RUNNING ?
        "Offline" : st->paused ? "Paused" : !st->last_frame ? "Waiting" :
        st->now - st->last_frame > 3000000 ? "STALE" : "Receiving";
    snprintf(out, size, "%s | rejected %u | incomplete %u | overflow %u | clipping: not reported",
             state, st->model.rejected, st->model.incomplete, st->rx_overflows);
}
