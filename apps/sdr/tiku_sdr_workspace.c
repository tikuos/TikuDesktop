/* TikuSDR saved setups, reference spectra and recording annotations.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tiku_atomic_file.h"
#include "tiku_state.h"
#include "tiku_sdr.h"

/** @brief Small documents must be regular files, never a blocking pipe. */
static FILE *workspace_open(const char *path)
{
    struct stat info;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return NULL;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size > 65536) {
        close(fd);
        return NULL;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) close(fd);
    return f;
}

int sdr_document_save(const char *path, int (*write_doc)(FILE *, void *), void *ctx)
{
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) return -1;
    locale_t previous = uselocale(numeric);
    int result = tiku_atomic_file(path, write_doc, ctx);
    uselocale(previous);
    freelocale(numeric);
    return result;
}

/** @brief Finish a small file operation with a visible, bounded message. */
static int workspace_result(sdr_t *st, const char *path, int result)
{
    snprintf(st->note, sizeof st->note, "%s: %.120s", result ?
             "Could not read or save file" : "Saved", sdr_tape_leaf(path));
    st->dirty = 1;
    return result;
}

/** @brief Write display settings only; restoring never changes hardware gain. */
static int setup_write(FILE *f, void *ctx)
{
    const sdr_t *st = ctx;
    const sdr_model_t *m = &st->model;
    fprintf(f, "TikuSDR-setup 1\n%.9g %.9g\n%d %d %u %d\n"
            "%d %d %d %d %d\n%d %.9g %.9g %d %.9g %.9g\n"
            "%.9g %d %d %d\nEND\n", m->start, m->stop,
            st->fall_on, st->fall_percent, st->collapsed, st->plans_on,
            m->type[0], m->type[1], m->type[2], m->avg_count, m->track,
            m->manual_scale, (double)m->scale_hi, (double)m->div,
            m->manual_color, (double)m->floor_db, (double)m->ceiling_db,
            (double)st->occupancy.threshold, st->trigger.pre_seconds,
            st->trigger.post_seconds, st->occupancy.plan);
    return ferror(f) ? -1 : 0;
}

int sdr_setup_save(sdr_t *st, const char *path)
{
    return workspace_result(st, path, sdr_document_save(path, setup_write, st));
}

int sdr_setup_load(sdr_t *st, const char *path)
{
    FILE *f = workspace_open(path);
    double a, b, hi, div, low, top, threshold;
    int version, fall, percent, plans, type[3], avg, track, manual, color;
    int pre, post, plan, ok = 0;
    unsigned collapsed;
    char end[8];
    if (!f) return workspace_result(st, path, -1);
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) { fclose(f); return -1; }
    locale_t previous = uselocale(numeric);
    if (fscanf(f, "TikuSDR-setup %d %lf %lf %d %d %u %d %d %d %d %d %d"
               " %d %lf %lf %d %lf %lf %lf %d %d %d %7s",
               &version, &a, &b, &fall, &percent, &collapsed, &plans,
               &type[0], &type[1], &type[2], &avg, &track, &manual, &hi,
               &div, &color, &low, &top, &threshold, &pre, &post, &plan,
               end) == 23 && version == 1 && !strcmp(end, "END") &&
        isfinite(a) && isfinite(b) && a >= SDR_TUNE_LO && b <= SDR_TUNE_HI &&
        b - a >= SDR_SPAN_MIN && percent >= 15 && percent <= 60 &&
        (fall == 0 || fall == 1) && (plans == 0 || plans == 1) &&
        collapsed < (1u << SDR_SECTIONS) &&
        type[0] >= 0 && type[0] < SDR_T_TYPES &&
        type[1] >= 0 && type[1] < SDR_T_TYPES &&
        type[2] >= 0 && type[2] < SDR_T_TYPES && avg >= 1 && avg <= 100 &&
        (track == 0 || track == 1) && (manual == 0 || manual == 1) &&
        (color == 0 || color == 1) && isfinite(hi) && hi >= -200 && hi <= 300 &&
        isfinite(div) && div >= 1 && div <= 50 && isfinite(low) &&
        isfinite(top) && low >= -200 && top <= 300 && low < top &&
        isfinite(threshold) && threshold >= -200 && threshold <= 300 &&
        pre >= 0 && pre <= 30 && post >= 1 && post <= 300 &&
        plan >= 0 && plan <= 2) ok = 1;
    fclose(f);
    uselocale(previous); freelocale(numeric);
    if (!ok) return workspace_result(st, path, -1);
    sdr_trigger_cancel(st);
    sdr_play_stop(st);
    st->shown[0] = 0;
    st->previous_count = st->nav_drag = 0;
    st->model.fft_override = st->model.rate_override = 0;
    st->model.rbw_bin_khz = 0;
    sdr_model_range(&st->model, a, b);
    double *range = st->model.sweep ? st->wide_view : st->live_view;
    range[0] = a; range[1] = b;
    st->fall_on = fall; st->fall_percent = percent;
    st->collapsed = collapsed; st->plans_on = plans;
    memcpy(st->model.type, type, sizeof type);
    st->model.avg_count = avg; st->model.track = track;
    st->model.manual_scale = manual; st->model.scale_hi = (float)hi;
    st->model.div = (float)div; st->model.scale_lo = (float)(hi - 10 * div);
    st->model.manual_color = color;
    st->model.floor_db = (float)low; st->model.ceiling_db = (float)top;
    st->occupancy.threshold = (float)threshold; st->occupancy.plan = plan;
    st->trigger.pre_seconds = pre; st->trigger.post_seconds = post;
    sdr_occupancy_reset(st);
    sdr_rec_view(st);
    st->dialog.focus = TIKU_DIALOG_NONE;
    snprintf(st->note, sizeof st->note, "Loaded setup: %.115s", sdr_tape_leaf(path));
    st->dirty = 1;
    return 0;
}

void sdr_preferences(sdr_t *st, int save)
{
    const char *home = getenv("HOME");
    char path[SDR_PATH], note_before[sizeof st->note];
    if (!home || !*home || snprintf(path, sizeof path,
        "%s/.config/TikuDesktop/TikuSDR.prefs", home) >= (int)sizeof path) return;
    snprintf(note_before, sizeof note_before, "%s", st->note);
    if (save) {
        /* A recording's range is not a new receiver preference. */
        double start = st->model.start, stop = st->model.stop;
        if (st->shown[0] && st->play.initial_stop > st->play.initial_start) {
            st->model.start = st->play.initial_start;
            st->model.stop = st->play.initial_stop;
        }
        if (tiku_state_mkparents(path) == 0) (void)sdr_setup_save(st, path);
        st->model.start = start; st->model.stop = stop;
    } else (void)sdr_setup_load(st, path);
    snprintf(st->note, sizeof st->note, "%s", note_before);
}

/** @brief Write one trace and its acquisition grid without quantizing levels. */
static int reference_write(FILE *f, void *ctx)
{
    const sdr_reference_t *r = ctx;
    fprintf(f, "TikuSDR-reference 1\n%d %.12g %.12g %d %d %d %d\n%s\n",
            r->n, r->lo, r->bin, r->rate, r->nfft, r->gain, r->held, r->board);
    for (int i = 0; i < r->n; i++) fprintf(f, "%.9g\n", (double)r->db[i]);
    return ferror(f) ? -1 : 0;
}

int sdr_reference_save(sdr_t *st, const char *path)
{
    const sdr_model_t *m = &st->model;
    const float *v = sdr_trace(m, m->type[st->trace_sel]);
    sdr_reference_t r = {0};
    if (!m->fresh || !v) return -1;
    r.n = m->n; r.lo = m->lo_mhz; r.bin = m->bin_mhz;
    r.rate = m->rate; r.nfft = m->nfft; r.gain = m->gain; r.held = m->held;
    memcpy(r.db, v, (size_t)m->n * sizeof *v);
    snprintf(r.board, sizeof r.board, "%s", st->board);
    return workspace_result(st, path, sdr_document_save(path, reference_write, &r));
}

int sdr_reference_load(sdr_t *st, const char *path)
{
    sdr_reference_t r = {0};
    FILE *f = workspace_open(path);
    int version, ok = 0;
    if (!f) return workspace_result(st, path, -1);
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) { fclose(f); return -1; }
    locale_t previous = uselocale(numeric);
    if (fscanf(f, "TikuSDR-reference %d\n%d %lf %lf %d %d %d %d",
               &version, &r.n, &r.lo, &r.bin, &r.rate, &r.nfft, &r.gain,
               &r.held) == 8 && version == 1 && r.n > 0 && r.n <= SDR_BINS &&
        isfinite(r.lo) && isfinite(r.bin) && r.lo >= SDR_TUNE_LO - 1 &&
        r.bin > 0 && r.lo + (r.n - 1) * r.bin <= SDR_TUNE_HI + 1 &&
        r.rate >= 0 && r.rate < 6 &&
        (r.nfft == 64 || r.nfft == 128 || r.nfft == 256) &&
        r.gain >= 0 && r.gain <= 255 && (r.held == 0 || r.held == 1) &&
        fgetc(f) == '\n' && fgets(r.board, sizeof r.board, f) &&
        strchr(r.board, '\n')) {
        r.board[strcspn(r.board, "\r\n")] = 0;
        ok = 1;
        for (int i = 0; i < r.n; i++)
            if (fscanf(f, "%f", &r.db[i]) != 1 || !isfinite(r.db[i]) ||
                r.db[i] < -300 || r.db[i] > 400) { ok = 0; break; }
    }
    fclose(f);
    uselocale(previous); freelocale(numeric);
    if (!ok) return workspace_result(st, path, -1);
    r.valid = r.visible = 1;
    snprintf(r.name, sizeof r.name, "%.95s", sdr_tape_leaf(path));
    st->reference = r;
    snprintf(st->note, sizeof st->note, "Reference loaded; shown only on a matching frequency grid. Relative dB, not calibrated.");
    st->dirty = 1;
    return 0;
}

int sdr_reference_matches(const sdr_t *st)
{
    const sdr_reference_t *r = &st->reference;
    const sdr_model_t *m = &st->model;
    return r->valid && r->visible && r->n == m->n && r->rate == m->rate &&
        r->nfft == m->nfft && fabs(r->lo - m->lo_mhz) < 1e-6 &&
        fabs(r->bin - m->bin_mhz) < 1e-6;
}

/** @brief Annotation files belong beside recordings, never inside them. */
static int book_path(const sdr_t *st, char *out, size_t size)
{
    return st->shown[0] && snprintf(out, size, "%s.sdrmarks", st->shown) < (int)size;
}

/** @brief Save labels and interval endpoints, but never auto-enable looping. */
static int books_write(FILE *f, void *ctx)
{
    const sdr_t *st = ctx;
    fprintf(f, "TikuSDR-bookmarks 1\n%.9g %.9g\n", st->play.loop_a, st->play.loop_b);
    for (int i = 0; i < st->book_count; i++)
        fprintf(f, "%.9g\t%s\n", st->books[i].at, st->books[i].label);
    return ferror(f) ? -1 : 0;
}

int sdr_books_save(sdr_t *st)
{
    char path[SDR_PATH + 16];
    if (!book_path(st, path, sizeof path)) return -1;
    return workspace_result(st, path, sdr_document_save(path, books_write, st));
}

void sdr_books_load(sdr_t *st)
{
    char path[SDR_PATH + 16], line[160];
    int version;
    double a, b;
    FILE *f;
    st->book_count = 0;
    if (!book_path(st, path, sizeof path) || !(f = workspace_open(path))) return;
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) { fclose(f); return; }
    locale_t previous = uselocale(numeric);
    if (fscanf(f, "TikuSDR-bookmarks %d\n%lf %lf\n", &version, &a, &b) == 3 &&
        version == 1 && isfinite(a) && isfinite(b) && a >= 0 && b >= a &&
        b <= st->play.total) {
        st->play.loop_a = a; st->play.loop_b = b;
        while (st->book_count < SDR_BOOKS && fgets(line, sizeof line, f)) {
            char *end;
            double at = strtod(line, &end);
            if (!isfinite(at) || at < 0 || at > st->play.total || *end != '\t') continue;
            sdr_bookmark_t *bk = &st->books[st->book_count++];
            bk->at = at;
            end++; end[strcspn(end, "\r\n")] = 0;
            snprintf(bk->label, sizeof bk->label, "%.63s", end);
        }
    }
    fclose(f);
    uselocale(previous); freelocale(numeric);
}

void sdr_book_add(sdr_t *st)
{
    if (!st->shown[0] || st->book_count >= SDR_BOOKS || st->play.seeking) return;
    sdr_bookmark_t *b = &st->books[st->book_count++];
    b->at = fmin(st->play.clock / 1e6, st->play.total);
    snprintf(b->label, sizeof b->label, "%s", st->book_label[0] ? st->book_label : "Event");
    for (char *p = b->label; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
    (void)sdr_books_save(st);
}

void sdr_book_next(sdr_t *st)
{
    int best = -1, first = -1;
    for (int i = 0; i < st->book_count; i++) {
        if (first < 0 || st->books[i].at < st->books[first].at) first = i;
        if (st->books[i].at > st->play.clock / 1e6 + .001 &&
            (best < 0 || st->books[i].at < st->books[best].at)) best = i;
    }
    if (best < 0) best = first;
    if (best < 0) return;
    sdr_play_seek(st, st->books[best].at);
    snprintf(st->note, sizeof st->note, "Bookmark: %.63s (%.3f s)",
             st->books[best].label, st->books[best].at);
}
