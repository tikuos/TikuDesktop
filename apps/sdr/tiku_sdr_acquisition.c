/* TikuSDR firmware capability discovery and validated acquisition choices.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_sdr.h"

static const int acquisition_rates[] = {80, 40, 20, 10, 8, 4};

int sdr_caps_parse(sdr_t *st, const char *line)
{
    char text[512], *save = NULL, *p;
    unsigned rates = 0, ffts = 0;
    int mode = 0, lo = 0, hi = 0;
    if (strncmp(line, "SDR bank ", 9) || strlen(line) >= sizeof text) return 0;
    snprintf(text, sizeof text, "%s", line);
    for (p = strtok_r(text, " ", &save); p; p = strtok_r(NULL, " ", &save)) {
        if (!strcmp(p, "rates")) { mode = 1; continue; }
        if (!strcmp(p, "nfft")) { mode = 2; continue; }
        if (!strcmp(p, "tune")) { mode = 3; continue; }
        if (!mode) continue;
        char *end;
        long n = strtol(p, &end, 10);
        if (*end || end == p) return 0;
        if (mode == 1) {
            for (int i = 0; i < 6; i++) if (n == acquisition_rates[i]) rates |= 1u << i;
        } else if (mode == 2) {
            if (n == 64) ffts |= 1; else if (n == 128) ffts |= 2;
            else if (n == 256) ffts |= 4;
        } else if (mode == 3) { if (n < 2000 || n > 3000) return 0; lo = (int)n; mode = 4; }
        else if (mode == 4) { if (n <= lo || n > 3000) return 0; hi = (int)n; mode = 5; }
        else return 0;
    }
    if (!rates || !ffts || mode != 5) return 0;
    st->caps_rates = rates; st->caps_ffts = ffts;
    st->caps_lo = lo; st->caps_hi = hi; st->caps_known = 1;
    st->dirty = 1;
    return 1;
}

int sdr_acquisition_valid(const sdr_t *st)
{
    const sdr_model_t *m = &st->model;
    unsigned fft = m->nfft == 64 ? 1 : m->nfft == 128 ? 2 : 4;
    if (st->caps_known != 1) return !m->rate_override && !m->fft_override;
    if (!(st->caps_rates & (1u << m->rate)) || !(st->caps_ffts & fft)) return 0;
    return m->sweep ? m->first >= st->caps_lo && m->last <= st->caps_hi &&
                      m->rate == 1 : m->tune >= st->caps_lo && m->tune <= st->caps_hi &&
        m->stop - m->start + 2 * fabs((m->start + m->stop) / 2 - m->tune) <=
        acquisition_rates[m->rate] * (m->rate <= 1 ? .6 : 1.0) + 1e-6;
}

void sdr_acquisition_cycle(sdr_t *st, int fft)
{
    sdr_model_t *m = &st->model;
    if (st->caps_known != 1 || st->state != SDR_RUNNING) return;
    int old = fft ? m->fft_override : m->rate_override;
    int start = fft ? old == 64 ? 1 : old == 128 ? 2 : old == 256 ? 3 : 0 : old;
    for (int step = 1; step <= (fft ? 4 : 7); step++) {
        int next = (start + step) % (fft ? 4 : 7);
        if (fft) m->fft_override = next ? 32 << next : 0;
        else m->rate_override = next;
        sdr_model_range(m, m->start, m->stop);
        if (sdr_acquisition_valid(st)) {
            sdr_trigger_cancel(st);
            sdr_occupancy_reset(st);
            sdr_rec_view(st);
            snprintf(st->note, sizeof st->note, "Acquisition: %d-point FFT, %d MS/s. Bin spacing is not calibrated resolution.",
                     m->nfft, acquisition_rates[m->rate]);
            st->dirty = 1;
            return;
        }
    }
    if (fft) m->fft_override = old; else m->rate_override = old;
    sdr_model_range(m, m->start, m->stop);
}
