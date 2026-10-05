/* tiku_sdr_marker.c -- TikuSDR's markers: the peaks of a trace found, up
 * to four markers put on them, moved from peak to peak or by hand, kept on
 * the top of a peak that drifts, and read out.  No window and no device.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_sdr.h"

/* The least rise, in dB, that moves a marker following a peak. */
#define FOLLOW_DB 0.5f

const float *sdr_mark_trace(const sdr_model_t *m, int k)
{
    const float *v = NULL;

    if (k >= 0 && k < SDR_MARKERS) {
        int t = m->mark[k].trace;
        v = sdr_trace(m, t >= 0 && t < SDR_TRACES ? m->type[t] : SDR_T_OFF);
    }
    return v ? v : m->db;
}

int sdr_peaks(const sdr_model_t *m, const float *v, int *bins, int max)
{
    int count = 0;

    if (!m->fresh) return 0;
    for (int j = 1; j < m->n - 1 && count < max; j++) {
        float left = v[j], right = v[j];
        int e = j, i;

        if (v[j - 1] >= v[j]) continue;             /* no rise into it */
        while (e + 1 < m->n && v[e + 1] == v[j]) e++;       /* its top */
        if (e + 1 >= m->n || v[e + 1] > v[j]) {     /* still rising    */
            j = e;
            continue;
        }
        for (i = j - 1; i >= 0 && v[i] <= v[j]; i--)
            if (v[i] < left) left = v[i];
        for (i = e + 1; i < m->n && v[i] <= v[j]; i++)
            if (v[i] < right) right = v[i];
        if (v[j] - (left > right ? left : right) >= SDR_EXCURSION)
            bins[count++] = (j + e) / 2;
        j = e;
    }
    return count;
}

int sdr_mark_bin(const sdr_model_t *m, int k)
{
    if (k < 0 || k >= SDR_MARKERS || !m->mark[k].on) return -1;
    return sdr_model_bin(m, m->mark[k].mhz);
}

int sdr_marks_out(const sdr_model_t *m)
{
    for (int k = 0; k < SDR_MARKERS; k++)
        if (m->mark[k].on) return 1;
    return 0;
}

/** @brief The highest bin within @p reach bins of @p b, moved to the
 *         middle of its flat top; @p b itself when no bin there is
 *         higher. */
static int climb(const float *v, int n, int b, int reach)
{
    int lo = b - reach < 0 ? 0 : b - reach;
    int hi = b + reach > n - 1 ? n - 1 : b + reach;
    int best = b, s, e;

    for (int j = lo; j <= hi; j++)
        if (v[j] > v[best]) best = j;
    if (best == b) return b;
    for (s = best; s > 0 && v[s - 1] == v[best]; s--) {}
    for (e = best; e + 1 < n && v[e + 1] == v[best]; e++) {}
    return (s + e) / 2;
}

/** @brief Whether a marker other than @p k stands within a bin of @p b. */
static int taken(const sdr_model_t *m, int k, int b)
{
    for (int i = 0; i < SDR_MARKERS; i++) {
        int at = i == k ? -1 : sdr_mark_bin(m, i);
        if (at >= 0 && abs(at - b) <= 1) return 1;
    }
    return 0;
}

/** @brief The bin nearest the view's middle with no marker but @p k
 *         within a bin of it, or -1. */
static int middle_free(const sdr_model_t *m, int k)
{
    int c = m->n / 2;

    for (int d = 0; d <= c; d++) {
        if (c + d < m->n && !taken(m, k, c + d)) return c + d;
        if (c - d >= 0 && !taken(m, k, c - d)) return c - d;
    }
    return -1;
}

int sdr_mark_peak(sdr_model_t *m, int k, int how)
{
    const float *v = sdr_mark_trace(m, k);
    int bins[SDR_BINS / 2], count, at, pick = -1;

    if (!m->fresh || k < 0 || k >= SDR_MARKERS || !m->mark[k].on) return 0;
    count = sdr_peaks(m, v, bins, SDR_BINS / 2);
    at = sdr_mark_bin(m, k);
    if (at < 0) how = SDR_TO_HIGHEST;           /* nowhere to move from */
    for (int i = 0; i < count; i++) {
        int b = bins[i];
        if (how == SDR_TO_HIGHEST) {
            if (pick < 0 || v[b] > v[pick]) pick = b;
        } else if (how == SDR_TO_NEXT) {
            /* The highest peak lower than the marker, or as high and to
             * its right; of equal ones, the leftmost. */
            if ((v[b] < v[at] || (v[b] == v[at] && b > at)) &&
                (pick < 0 || v[b] > v[pick])) pick = b;
        } else if (how == SDR_TO_LEFT) {
            if (b < at) pick = b;
        } else if (b > at && pick < 0) {
            pick = b;
        }
    }
    if (pick < 0 && how == SDR_TO_HIGHEST) {    /* no peak: the top bin */
        pick = 0;
        for (int j = 1; j < m->n; j++)
            if (v[j] > v[pick]) pick = j;
    }
    if (pick < 0) return 0;
    m->mark[k].mhz = sdr_model_freq(m, pick);
    return 1;
}

void sdr_mark_on(sdr_model_t *m, int k)
{
    const float *v = sdr_mark_trace(m, k);
    int bins[SDR_BINS / 2], count, pick = -1;

    if (k < 0 || k >= SDR_MARKERS || m->n <= 0) return;
    count = sdr_peaks(m, v, bins, SDR_BINS / 2);
    for (int i = 0; i < count; i++)
        if (!taken(m, k, bins[i]) && (pick < 0 || v[bins[i]] > v[pick]))
            pick = bins[i];
    if (pick < 0) pick = middle_free(m, k);     /* no free peak */
    if (pick < 0) pick = m->n / 2;
    m->mark[k].on = 1;
    m->mark[k].mhz = sdr_model_freq(m, pick);
}

void sdr_mark_off(sdr_model_t *m, int k)
{
    if (k < 0 || k >= SDR_MARKERS) return;
    m->mark[k].on = 0;
    if (m->mark_held == k) m->mark_held = -1;
}

void sdr_mark_at(sdr_model_t *m, int k, double mhz, int snap)
{
    int b;

    if (k < 0 || k >= SDR_MARKERS || m->n <= 0) return;
    b = sdr_model_bin(m, mhz);
    if (b < 0) b = mhz < m->lo_mhz ? 0 : m->n - 1;
    if (snap && m->track && m->fresh)
        b = climb(sdr_mark_trace(m, k), m->n, b, 2 + m->n / 48);
    m->mark[k].on = 1;
    m->mark[k].mhz = sdr_model_freq(m, b);
}

void sdr_mark_follow(sdr_model_t *m)
{
    if (!m->track || !m->fresh) return;
    for (int k = 0; k < SDR_MARKERS; k++) {
        const float *v = sdr_mark_trace(m, k);
        int b = sdr_mark_bin(m, k), top;

        if (b < 0 || k == m->mark_held) continue;
        /* Each frame it looks 3 + n/64 bins either way, and moves only to
         * a level at least FOLLOW_DB over the one it is on. */
        top = climb(v, m->n, b, 3 + m->n / 64);
        if (v[top] - v[b] >= FOLLOW_DB)
            m->mark[k].mhz = sdr_model_freq(m, top);
    }
}

void sdr_mark_text(const sdr_model_t *m, int k, char *out, size_t max)
{
    int b = sdr_mark_bin(m, k), first = sdr_mark_bin(m, 0);

    if (k < 0 || k >= SDR_MARKERS || !m->mark[k].on) {
        out[0] = '\0';
    } else if (!m->fresh) {
        snprintf(out, max, "M%d  %.3f MHz", k + 1, m->mark[k].mhz);
    } else if (b < 0) {
        snprintf(out, max, "M%d  %.3f MHz  outside the view", k + 1,
                 m->mark[k].mhz);
    } else if (k > 0 && first >= 0) {
        float d = sdr_mark_trace(m, k)[b] - sdr_mark_trace(m, 0)[first];
        snprintf(out, max, "M%d  %.3f MHz  %5.1f dB   Δ %+.3f MHz  %+.1f dB",
                 k + 1, m->mark[k].mhz, (double)sdr_mark_trace(m, k)[b],
                 m->mark[k].mhz - m->mark[0].mhz, (double)d);
    } else {
        snprintf(out, max, "M%d  %.3f MHz  %5.1f dB", k + 1, m->mark[k].mhz,
                 (double)sdr_mark_trace(m, k)[b]);
    }
}
