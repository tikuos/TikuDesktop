/* tiku_sdr_model.c -- TikuSDR's model: a view's range and how the board
 * is asked for it, the board's SPEC lines taken in, slices stitched into
 * one band, what each trace type keeps of them, the graticule's scale, the
 * spectrogram, and the channel plans of the 2.4 GHz band.  No window and
 * no device.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_sdr.h"

/* A sweep's slice keeps 8.2 MHz either side of its centre, so neighbours
 * overlap rather than gap, in 312.5 kHz bins (40 MS/s in 128); one heard
 * at the AGC's gain has its noise floor moved to 40 dB. */
#define SLICE_HALF_MHZ 8.2
#define SLICE_BIN_MHZ  0.3125
#define BAND_FLOOR     40.0f

/* A line heard at a held gain is raised by the steps it was held below the
 * board's own hold, a step being about a decibel: whatever gain heard
 * them, lines read alike, and a sweep's slices need no levelling. */
#define GAIN_REF       60

/* The rate codes the board takes, by what one spectrum spans, in MHz.
 * Below 40 MS/s the whole of it is flat; at 40 the middle 24 MHz. */
static const double rate_mhz[] = {80.0, 40.0, 20.0, 10.0, 8.0, 4.0};
#define RATE_NARROWEST 5

void sdr_model_init(sdr_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->type[0] = SDR_T_AVG;
    m->avg_count = 4;
    m->track = 1;
    m->mark_held = -1;
    m->one_max = SDR_LIVE_SPAN;
    m->div = 10.0f;
    m->scale_hi = 110.0f;
    m->scale_lo = m->scale_hi - SDR_DIVS * m->div;
    m->floor_db = 30.0f;
    sdr_model_range(m, 2437.0 - SDR_LIVE_SPAN / 2, 2437.0 + SDR_LIVE_SPAN / 2);
}

void sdr_range_fit(double *start, double *stop)
{
    double a = *start < *stop ? *start : *stop;
    double b = *start < *stop ? *stop : *start;

    if (b - a > SDR_TUNE_HI - SDR_TUNE_LO) {
        a = SDR_TUNE_LO;
        b = SDR_TUNE_HI;
    }
    if (b - a < SDR_SPAN_MIN) {
        a = (a + b - SDR_SPAN_MIN) / 2;
        b = a + SDR_SPAN_MIN;
    }
    if (a < SDR_TUNE_LO) {
        b += SDR_TUNE_LO - a;
        a = SDR_TUNE_LO;
    }
    if (b > SDR_TUNE_HI) {
        a -= b - SDR_TUNE_HI;
        b = SDR_TUNE_HI;
    }
    *start = round(a * 1000.0) / 1000.0;
    *stop = round(b * 1000.0) / 1000.0;
}

/** @brief One spectrum: tuned to the whole MHz nearest the centre, at the
 *         narrowest rate that holds the view, 40 MS/s holding any up to
 *         40 MHz; the view is the bins of it inside start to stop. */
static void one_spectrum(sdr_model_t *m)
{
    double centre = (m->start + m->stop) / 2, span = m->stop - m->start;
    double off, bin;
    int r = RATE_NARROWEST, k1;

    m->tune = (int)lround(centre);
    off = fabs(centre - m->tune);
    while (r > 1 && rate_mhz[r] < span + 2 * off - 1e-9) r--;
    m->rate = r;
    m->nfft = 256;
    bin = rate_mhz[r] / m->nfft;
    m->k0 = (int)ceil((m->start - m->tune) / bin + m->nfft / 2 - 1e-6);
    k1 = (int)floor((m->stop - m->tune) / bin + m->nfft / 2 + 1e-6);
    if (m->k0 < 0) m->k0 = 0;
    if (k1 > m->nfft - 1) k1 = m->nfft - 1;
    m->n = k1 - m->k0 + 1;
    m->bin_mhz = bin;
    m->lo_mhz = m->tune + (m->k0 - m->nfft / 2) * bin;
}

/** @brief A sweep: 40 MS/s slices 16 MHz apart from the first whose kept
 *         middle reaches the start; one running past the board's top moves
 *         down whole.  The trace's bins are the slices' own, or wider, a
 *         trace holding SDR_BINS at most. */
static void slices(sdr_model_t *m)
{
    double span = m->stop - m->start;
    int count;

    m->rate = 1;
    m->nfft = 128;
    m->first = (int)floor(m->start + SLICE_HALF_MHZ);
    count = (int)ceil((m->stop - m->first - SLICE_HALF_MHZ) / SDR_SLICE_STEP);
    count = count < 0 ? 1 : count + 1;
    m->last = m->first + (count - 1) * SDR_SLICE_STEP;
    if (m->last > (int)SDR_TUNE_HI) {
        m->last = (int)SDR_TUNE_HI;
        m->first = m->last - (count - 1) * SDR_SLICE_STEP;
    }
    m->bin_mhz = span / SDR_BINS > SLICE_BIN_MHZ ? span / SDR_BINS
                                                 : SLICE_BIN_MHZ;
    m->n = (int)(span / m->bin_mhz + 1e-6);
    if (m->n > SDR_BINS) m->n = SDR_BINS;
    m->lo_mhz = m->start;
}

void sdr_model_range(sdr_model_t *m, double start, double stop)
{
    sdr_range_fit(&start, &stop);
    m->start = start;
    m->stop = stop;
    m->sweep = stop - start > m->one_max + 1e-6;
    if (m->sweep) slices(m); else one_spectrum(m);
    memset(m->db, 0, sizeof m->db);
    memset(m->seen, 0, sizeof m->seen);
    m->got = 0;
    m->wf_head = m->wf_count = 0;
    m->fresh = 0;
    sdr_model_clear(m);
}

int sdr_model_is_live(const sdr_model_t *m)
{
    return !m->sweep;
}

void sdr_mhz_text(double mhz, char *out, size_t max)
{
    long long khz = llround(mhz * 1000.0);
    char frac[8];
    int n;

    if (khz % 1000 == 0) {
        snprintf(out, max, "%lld", khz / 1000);
        return;
    }
    n = snprintf(frac, sizeof frac, "%03lld", khz % 1000);
    while (n > 0 && frac[n - 1] == '0') frac[--n] = '\0';
    snprintf(out, max, "%lld.%s", khz / 1000, frac);
}

void sdr_model_field(const sdr_model_t *m, int field, char *out, size_t max)
{
    double v = field == SDR_F_CENTER ? (m->start + m->stop) / 2 :
               field == SDR_F_SPAN ? m->stop - m->start :
               field == SDR_F_START ? m->start :
               field == SDR_F_STOP ? m->stop : m->mark[m->mark_sel].mhz;

    if (field == SDR_F_AVG)
        snprintf(out, max, "%d", m->avg_count);
    else if (field == SDR_F_MARK && !m->mark[m->mark_sel].on)
        out[0] = '\0';
    else
        sdr_mhz_text(v, out, max);
}

int sdr_mhz_parse(const char *text, double *mhz)
{
    double v = 0.0, place = 1.0;
    int digits = 0;
    const char *p = text;

    while (*p == ' ') p++;
    for (; *p >= '0' && *p <= '9'; p++, digits++) v = v * 10 + (*p - '0');
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++, digits++)
            v += (*p - '0') * (place /= 10);
    if (!digits || v > 1e6) return 0;
    while (*p == ' ') p++;
    if (*p == 'k' || *p == 'K') v /= 1000.0;
    else if (*p == 'g' || *p == 'G') v *= 1000.0;
    if (*p == 'k' || *p == 'K' || *p == 'm' || *p == 'M' || *p == 'g' ||
        *p == 'G') p++;
    if ((p[0] == 'h' || p[0] == 'H') && (p[1] == 'z' || p[1] == 'Z')) p += 2;
    while (*p == ' ') p++;
    if (*p) return 0;
    *mhz = v;
    return 1;
}

void sdr_model_clear(sdr_model_t *m)
{
    memcpy(m->avg, m->db, sizeof m->avg);
    memcpy(m->peak, m->db, sizeof m->peak);
    memcpy(m->low, m->db, sizeof m->low);
}

const float *sdr_trace(const sdr_model_t *m, int type)
{
    return type == SDR_T_WRITE ? m->db : type == SDR_T_AVG ? m->avg :
           type == SDR_T_MAX ? m->peak : type == SDR_T_MIN ? m->low : NULL;
}

double sdr_model_freq(const sdr_model_t *m, int bin)
{
    return m->lo_mhz + bin * m->bin_mhz;
}

int sdr_model_bin(const sdr_model_t *m, double mhz)
{
    long j = lround((mhz - m->lo_mhz) / m->bin_mhz);

    return (j < 0 || j >= m->n) ? -1 : (int)j;
}

/** @brief Two hex digits at @p s, or -1. */
static int hex2(const char *s)
{
    int v = 0;

    for (int i = 0; i < 2; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else return -1;
    }
    return v;
}

/** @brief A finished frame: the average, the highest and the lowest kept
 *         from it, a spectrogram row, and markers kept on the peaks they
 *         follow. */
static void frame_done(sdr_model_t *m)
{
    float a = 1.0f / (float)(m->avg_count > 1 ? m->avg_count : 1);
    unsigned char *row;

    for (int j = 0; j < m->n; j++) {
        m->avg[j] = m->fresh ? m->avg[j] + (m->db[j] - m->avg[j]) * a
                             : m->db[j];
        if (!m->fresh || m->db[j] > m->peak[j]) m->peak[j] = m->db[j];
        if (!m->fresh || m->db[j] < m->low[j]) m->low[j] = m->db[j];
    }
    m->wf_head = (m->wf_head + 1) % SDR_ROWS;
    if (m->wf_count < SDR_ROWS) m->wf_count++;
    m->wf_at[m->wf_head] = m->clock;
    row = m->wf[m->wf_head];
    for (int j = 0; j < m->n; j++) {
        float v = m->db[j] * 2.0f;
        row[j] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    m->fresh = 1;
    m->spectra++;
    sdr_mark_follow(m);
}

/** @brief A comparison for qsort over bytes. */
static int by_byte(const void *a, const void *b)
{
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

/** @brief What the latest line's levels are raised by: a held gain's
 *         steps below GAIN_REF, or nothing for the AGC's. */
static float lift(const sdr_model_t *m)
{
    return m->held ? (float)(GAIN_REF - m->gain) : 0.0f;
}

/** @brief One slice of a band sweep into its place, keeping the middle,
 *         where the receiver's filter is flat.  A slice heard at a held gain
 *         is lifted as any line is; one heard at the AGC's own has its noise
 *         floor (a fifth of the way up its values) moved to a common one. */
static void band_slice(sdr_model_t *m, int centre, double bin,
                       const unsigned char *v, int n)
{
    unsigned char sorted[SDR_BINS];
    int kept = 0;
    float shift = lift(m);

    for (int k = 0; k < n; k++)
        if (fabs((k - n / 2) * bin) < SLICE_HALF_MHZ) sorted[kept++] = v[k];
    if (!kept) return;
    if (!m->held) {
        qsort(sorted, (size_t)kept, 1, by_byte);
        shift = BAND_FLOOR - sorted[kept / 5] / 2.0f;
    }
    for (int k = 0; k < n; k++) {
        double f = centre + (k - n / 2) * bin;
        int j = sdr_model_bin(m, f);
        float val = v[k] / 2.0f + shift;

        if (fabs(f - centre) >= SLICE_HALF_MHZ || j < 0) continue;
        if (!m->seen[j] || val > m->db[j]) m->db[j] = val;
        m->seen[j] = 1;
    }
}

/** @brief The receiver's own leakage at its centre is no signal: the three
 *         bins on it take their neighbours' mean. */
static void dc_patch(unsigned char *v, int n)
{
    int c = n / 2, mean;

    if (n < 16) return;
    mean = (v[c - 3] + v[c + 3]) / 2;
    v[c - 1] = v[c] = v[c + 1] = (unsigned char)mean;
}

int sdr_model_line(sdr_model_t *m, const char *line)
{
    unsigned char v[SDR_BINS];
    int centre, gain, n, at = 0;
    long hz;

    if (!strncmp(line, "SWEEP ", 6)) {
        int count = (m->last - m->first) / SDR_SLICE_STEP + 1;
        uint64_t all = count >= 64 ? ~0ULL : (1ULL << count) - 1;
        int whole = m->got == all;

        if (!m->sweep) return 0;
        memset(m->seen, 0, sizeof m->seen);
        m->got = 0;
        /* A sweep begun before this view, or one short of a slice, leaves
         * part of the range unheard: no frame of it. */
        if (!whole) return 0;
        frame_done(m);
        return 1;
    }
    if (sscanf(line, "SPEC %d %ld %d %d %n", &centre, &hz, &gain, &n,
               &at) != 4 || at == 0 || n <= 0 || n > SDR_BINS || hz <= 0 ||
        strlen(line + at) < (size_t)n * 2u)
        return -1;
    for (int k = 0; k < n; k++) {
        int b = hex2(line + at + 2 * k);
        if (b < 0) return -1;
        v[k] = (unsigned char)b;
    }
    /* A line asked for by an older view is dropped, not drawn crooked. */
    if (hz != lround(rate_mhz[m->rate] * 1e6) || n != m->nfft) return 0;
    dc_patch(v, n);
    m->gain = gain;
    m->held = !strncmp(line + at + 2 * n, " held", 5);
    if (m->sweep) {
        if (centre >= m->first && centre <= m->last &&
            (centre - m->first) % SDR_SLICE_STEP == 0)
            m->got |= 1ULL << ((centre - m->first) / SDR_SLICE_STEP);
        band_slice(m, centre, hz / 1e6 / n, v, n);
        return 0;
    }
    if (centre != m->tune) return 0;
    for (int j = 0; j < m->n; j++) m->db[j] = v[m->k0 + j] / 2.0f + lift(m);
    frame_done(m);
    return 1;
}

/** @brief A comparison for qsort over floats. */
static int by_value(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;

    return (x > y) - (x < y);
}

void sdr_model_scale(sdr_model_t *m)
{
    static const float divs[] = {2.0f, 5.0f, 10.0f, 20.0f};
    float sorted[SDR_BINS], lo = 1e9f, hi = -1e9f, top, need, div = 20.0f;
    int shown = 0;

    if (!m->fresh || m->n <= 0) return;
    for (int t = 0; t < SDR_TRACES; t++) {
        const float *v = sdr_trace(m, m->type[t]);
        if (!v) continue;
        memcpy(sorted, v, sizeof(float) * (size_t)m->n);
        qsort(sorted, (size_t)m->n, sizeof(float), by_value);
        if (sorted[m->n / 10] < lo) lo = sorted[m->n / 10];
        if (sorted[m->n - 1] > hi) hi = sorted[m->n - 1];
        shown = 1;
    }
    if (!shown) return;                 /* nothing drawn: the scale stays */
    top = ceilf((hi + 3.0f) / 10.0f) * 10.0f;
    need = top - lo + 6.0f;
    for (int i = 0; i < 4; i++)
        if (divs[i] * SDR_DIVS >= need) { div = divs[i]; break; }
    /* Wider at once; narrower, or a lower top, only with room to spare. */
    if (m->spectra <= 1 || div > m->div ||
        (div < m->div && need <= div * SDR_DIVS * 0.8f))
        m->div = div;
    if (m->spectra <= 1 || top > m->scale_hi ||
        top <= m->scale_hi - 2 * m->div)
        m->scale_hi = top;
    m->scale_lo = m->scale_hi - SDR_DIVS * m->div;
    m->floor_db = floorf((lo - 6.0f) / 5.0f) * 5.0f;
    if (m->floor_db > m->scale_hi - 20.0f) m->floor_db = m->scale_hi - 20.0f;
}

tiku_rgb_t sdr_palette(float t)
{
    /* An analyser's spectrogram: navy, blue, cyan, green, yellow, red. */
    static const unsigned char stop[6][3] = {
        {0, 0, 96}, {0, 40, 255}, {0, 210, 255}, {40, 220, 40},
        {255, 230, 0}, {230, 20, 20}
    };
    float x = (t < 0 ? 0 : t > 1 ? 1 : t) * 5.0f;
    int i = (int)x >= 5 ? 4 : (int)x;
    float f = x - i;

    return TIKU_RGB((int)(stop[i][0] + (stop[i + 1][0] - stop[i][0]) * f),
                    (int)(stop[i][1] + (stop[i + 1][1] - stop[i][1]) * f),
                    (int)(stop[i][2] + (stop[i + 1][2] - stop[i][2]) * f));
}

/** @brief The BLE channel index at @p mhz (2 MHz raster), or -1. */
static int ble_channel(double mhz)
{
    long k = lround((mhz - 2402.0) / 2.0);

    if (k < 0 || k > 39 || fabs(mhz - (2402.0 + 2.0 * k)) > 1.0) return -1;
    if (k == 0) return 37;
    if (k == 12) return 38;
    if (k == 39) return 39;
    return k < 12 ? (int)k - 1 : (int)k - 2;
}

int sdr_channels(double mhz, char *out, size_t max)
{
    int n = 0, lo = 0, hi = 0, ble = ble_channel(mhz);
    long z = lround((mhz - 2405.0) / 5.0) + 11;

    for (int c = 1; c <= 13; c++) {
        if (fabs(mhz - (2407.0 + 5.0 * c)) <= 10.0) {
            if (!lo) lo = c;
            hi = c;
        }
    }
    out[0] = '\0';
    if (lo) n += snprintf(out + n, max - (size_t)n, lo == hi ?
                          "Wi-Fi %d" : "Wi-Fi %d-%d", lo, hi);
    if (ble >= 0 && (size_t)n < max)
        n += snprintf(out + n, max - (size_t)n, "%sBLE %d%s", n ? " · " : "",
                      ble, ble >= 37 ? " (advertising)" : "");
    if (z >= 11 && z <= 26 && fabs(mhz - (2405.0 + 5.0 * (z - 11))) <= 1.0 &&
        (size_t)n < max)
        n += snprintf(out + n, max - (size_t)n, "%s802.15.4 %ld",
                      n ? " · " : "", z);
    return n;
}

const char *sdr_command(const sdr_model_t *m, char *out, size_t max)
{
    if (m->sweep)
        snprintf(out, max, "sdr sweep %d %d %d %d %d", m->first, m->last,
                 SDR_SLICE_STEP, m->rate, m->nfft);
    else
        snprintf(out, max, "sdr spec %d %d %d", m->tune, m->rate, m->nfft);
    return out;
}
