/*
 * TikuSDR
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_sdr_transmit.c - validated IQ files and acknowledged bounded bursts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sdr.h"
#include <stdlib.h>
#include <string.h>

enum { TX_CAPS = 1, TX_STOP, TX_LOAD, TX_PUT, TX_SEND, TX_CLEAR, TX_RX };
#define TX_FILE_MAX 16128u

void sdr_tx_init(sdr_t *st)
{
    memset(&st->tx, 0, sizeof st->tx);
    st->tx.mhz = 2437;
    st->tx.cycles = 16;
    st->tx.amplitude = 64;
    st->tx.ms = 100;
}

int sdr_tx_busy(const sdr_t *st) { return st->tx.phase || st->tx.queued; }

void sdr_tx_dispose(sdr_t *st)
{
    free(st->tx.samples);
    st->tx.samples = NULL;
    st->tx.words = 0;
    st->tx.path[0] = '\0';
    st->tx.phase = st->tx.queued = 0;
}

int sdr_tx_file(sdr_t *st, const char *path)
{
    FILE *file;
    long bytes;
    uint32_t *samples;
    unsigned words;
    if (sdr_tx_busy(st)) { return -1; }
    if (!path) {
        free(st->tx.samples); st->tx.samples = NULL; st->tx.words = 0;
        st->tx.path[0] = '\0';
        snprintf(st->note, sizeof st->note, "Using a generated IQ tone; amplitude is a sample code, not dBm.");
        st->dirty = 1;
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) { goto invalid; }
    if (fseek(file, 0, SEEK_END) || (bytes = ftell(file)) <= 0 ||
        bytes > (long)(TX_FILE_MAX * 4) || bytes % 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file); goto invalid;
    }
    words = (unsigned)bytes / 4;
    if (st->tx.known == 1 && words > st->tx.maximum) { fclose(file); goto invalid; }
    samples = malloc((size_t)bytes);
    if (!samples) { fclose(file); goto invalid; }
    if (fread(samples, 1, (size_t)bytes, file) != (size_t)bytes || ferror(file)) {
        fclose(file); free(samples); goto invalid;
    }
    fclose(file);
    for (unsigned i = 0; i < words; i++) {
        unsigned char *p = (unsigned char *)(samples + i);
        uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        if (v >> 20) { free(samples); goto invalid; }
        samples[i] = v;
    }
    free(st->tx.samples);
    st->tx.samples = samples; st->tx.words = words;
    snprintf(st->tx.path, sizeof st->tx.path, "%s", path);
    snprintf(st->note, sizeof st->note, "Loaded %u IQ10 samples locally at 80 MS/s. Transmit burst sends them.", words);
    st->dirty = 1;
    return 0;
invalid:
    snprintf(st->note, sizeof st->note,
             "Use 256..16128 packed IQ10 samples, multiples of 256, little-endian 32-bit words with upper bits zero.");
    st->dirty = 1;
    return -1;
}

int sdr_tx_request(sdr_t *st)
{
    const sdr_transmit_t *t = &st->tx;
    if (st->state != SDR_RUNNING || t->known != 1 || sdr_tx_busy(st) ||
        st->caps_queued || st->caps_inflight || st->gain_queued || st->gain_inflight ||
        st->rec.file || st->play.file || st->trigger.armed || st->trigger.fired ||
        st->asking || t->mhz < (int)t->low || t->mhz > (int)t->high || !t->step ||
        ((unsigned)t->mhz - t->low) % t->step || t->ms < 1 || t->ms > (int)t->max_ms ||
        t->cycles < -128 || t->cycles > 128 || t->amplitude < 1 || t->amplitude > 128 ||
        (t->samples && (!t->words || t->words > t->maximum || t->words % t->alignment))) {
        return 0;
    }
    st->tx.queued = 2;
    st->tx.failed = st->tx.cancel = 0;
    st->tx.offset = st->tx.sent = 0;
    snprintf(st->note, sizeof st->note, "Preparing one %d ms burst on %d MHz...", t->ms, t->mhz);
    st->dirty = 1;
    return 1;
}

void sdr_tx_cancel(sdr_t *st)
{
    if (sdr_tx_busy(st)) { st->tx.cancel = 1; }
}

/** @brief Send one TX operation and require its matching reply before advancing. */
static void tx_issue(sdr_t *st, int phase, const char *line)
{
    st->tx.phase = phase;
    st->tx.answered = 0;
    sdr_link_command(st, line);
}

/** @brief Send at most eight waveform words in one bounded console line. */
static void tx_chunk(sdr_t *st)
{
    sdr_transmit_t *t = &st->tx;
    char line[96];
    unsigned count = t->words - t->offset;
    size_t used;
    if (count > t->chunk) { count = t->chunk; }
    used = (size_t)snprintf(line, sizeof line, "sdr tx put %u", t->offset);
    for (unsigned i = 0; i < count; i++)
        used += (size_t)snprintf(line + used, sizeof line - used, " %08lx",
                                (unsigned long)t->samples[t->offset + i]);
    t->sent = t->offset + count;
    tx_issue(st, TX_PUT, line);
    snprintf(st->note, sizeof st->note, "Uploading IQ: %u / %u samples.", t->offset, t->words);
    st->dirty = 1;
}

/** @brief Start the single requested burst after the complete waveform is acknowledged. */
static void tx_send(sdr_t *st)
{
    char line[64];
    snprintf(line, sizeof line, "sdr tx send %d %d", st->tx.mhz, st->tx.ms);
    tx_issue(st, TX_SEND, line);
}

void sdr_tx_line(sdr_t *st, const char *line)
{
    sdr_transmit_t *t = &st->tx;
    unsigned version, hz, maximum, alignment, ms, low, high, step, chunk;
    unsigned written, total, elapsed, words;
    int used = 0, rc;
    if (t->phase == TX_CAPS) {
        if (sscanf(line, "TXCAP %u %u %u %u %u %u %u %u %u%n", &version, &hz,
                   &maximum, &alignment, &ms, &low, &high, &step, &chunk, &used) == 9 &&
            !line[used] && version == 1 && hz == 80000000 && maximum >= 1024 &&
            maximum <= TX_FILE_MAX && alignment == 256 && maximum % alignment == 0 &&
            ms >= 1 && ms <= 1000 && low >= 2400 && high <= 2500 && low < high &&
            step == 5 && chunk >= 1 && chunk <= 8) {
            t->known = t->answered = 1; t->hz = hz; t->maximum = maximum;
            t->alignment = alignment; t->max_ms = ms;
            t->low = low; t->high = high; t->step = step; t->chunk = chunk;
        }
    } else if (t->phase == TX_STOP) {
        if (!strcmp(line, "SDR stopped")) { t->answered = 1; }
    } else if (t->phase == TX_LOAD || t->phase == TX_PUT) {
        if (sscanf(line, "TX load %u %u%n", &written, &total, &used) == 2 && !line[used] &&
            total == (t->samples ? t->words : 1024u) &&
            written == (t->phase == TX_PUT ? t->sent : t->samples ? 0u : 1024u)) {
            t->offset = written; t->answered = 1;
        }
    } else if (t->phase == TX_SEND) {
        if (!strncmp(line, "TX active ", 10)) {
            snprintf(st->note, sizeof st->note, "Transmitting one %d ms burst on %d MHz.", t->ms, t->mhz);
            st->dirty = 1;
        }
        if (sscanf(line, "TX done %d %u %u %u%n", &rc, &elapsed, &hz, &words, &used) == 4 &&
            !line[used]) {
            t->answered = 1;
            if (rc || hz != t->hz || words != (t->samples ? t->words : 1024u) ||
                elapsed < (unsigned)t->ms * 1000u) { t->failed = 1; }
        }
    } else if (t->phase == TX_CLEAR) {
        if (!strcmp(line, "TX cleared")) { t->answered = 1; }
    } else if (t->phase == TX_RX) {
        if (!strcmp(line, "SDR ready")) { t->answered = 1; }
    }
    if (!strncmp(line, "ERR ", 4)) { t->failed = 1; }
}

void sdr_tx_prompt(sdr_t *st)
{
    sdr_transmit_t *t = &st->tx;
    char line[64];
    int phase = t->phase;
    if (phase == TX_CAPS) {
        if (!t->answered) { t->known = -1; }
        t->phase = 0;
        snprintf(st->note, sizeof st->note, t->known == 1 ?
                 "TX available: 80 MS/s, preloaded IQ10, bursts up to 1000 ms. Power is uncalibrated." :
                 "This firmware does not offer supported IQ transmission.");
    } else if (phase == TX_CLEAR) {
        t->phase = 0;
        if (!t->answered) {
            st->paused = 1;
            t->known = -2;
            snprintf(st->note, sizeof st->note, "TX cleanup was not confirmed. Reset the board before another radio session.");
        } else if (st->closing) { sdr_disconnect(st); }
        else { tx_issue(st, TX_RX, "sdr start"); }
    } else if (phase == TX_RX) {
        t->phase = 0;
        if (!t->answered) { st->paused = 1; }
        snprintf(st->note, sizeof st->note, !t->answered ? "Receiver did not restart; reconnect the board." :
                 t->cancel ? "Transmission canceled. Receiving again." :
                 t->failed ? "Transmission failed. Receiving again." : "Burst complete. Receiving again.");
    } else if (!t->answered || t->failed || t->cancel || phase == TX_SEND) {
        if (!t->answered) { t->failed = 1; }
        tx_issue(st, TX_CLEAR, "sdr tx clear");
    } else if (phase == TX_STOP) {
        if (t->samples) { snprintf(line, sizeof line, "sdr tx begin %u", t->words); }
        else { snprintf(line, sizeof line, "sdr tx tone %d %d", t->cycles, t->amplitude); }
        tx_issue(st, TX_LOAD, line);
    } else if (phase == TX_LOAD || phase == TX_PUT) {
        if (t->samples && t->offset < t->words) { tx_chunk(st); }
        else { tx_send(st); }
    }
    if (!t->phase && st->closing && st->state == SDR_RUNNING) { sdr_disconnect(st); }
    st->dirty = 1;
}

int sdr_tx_tick(sdr_t *st)
{
    if (st->tx.phase) { return 1; }
    if (st->tx.queued && st->state == SDR_RUNNING && !st->pending && st->fd >= 0) {
        int request = st->tx.queued;
        st->tx.queued = 0;
        if (st->tx.cancel) {
            st->tx.cancel = 0;
            if (st->closing) { sdr_disconnect(st); }
        } else if (request == 1) {
            st->tx.known = 0;
            tx_issue(st, TX_CAPS, "sdr tx info");
        } else { tx_issue(st, TX_STOP, "sdr stop"); }
    }
    return sdr_tx_busy(st);
}
