/* tiku_sdr_tape.c -- TikuSDR's recordings: the board's SPEC and SWEEP
 * lines and the window's views, written with their times, played back at
 * the pace they were made, and spectra exported as CSV.  A recording is
 * plain text, its format described in comment lines at its head.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_sdr.h"

#define TAPE_MAGIC   "# TikuSDR recording"
#define TAPE_TAIL    8192           /* a recording's last bytes, read
                                       for its length                   */
#define TAPE_LONGEST 100000000L     /* seconds; a line timed later is
                                       skipped                          */
#define GAP_MAX      2.0            /* seconds: a longer silence is cut  */
#define GAP_LEFT     0.5            /* ... down to this                  */
#define PLAY_BURST   64             /* lines played in one tick, at most */
#define CSV_LINES    2000           /* a recording's lines read a tick   */
#define CSV_ROWS     16384          /* ... or rows written, whichever first */
#define CSV_HEAD     "time_s,freq_mhz,db\n"

/* What a recording's lines mean, said at its head. */
static const char *const format[] = {
    "# Each line below: seconds since the start, then what the board sent.",
    "#   SPEC <centre MHz> <sample rate Hz> <gain> <bins> <hex>[ held]:",
    "#     one spectrum, a byte a bin from the lowest frequency, in half-",
    "#     decibels (uncalibrated: compare levels within one recording);",
    "#     held: at a gain index the board held, a step about a decibel",
    "#   SWEEP <slices>: the band's slices since the last SWEEP are one",
    "#   VIEW range <start MHz> <stop MHz>: what the window showed from",
    "#     here on: one spectrum up to 24 MHz wide, else a sweep (in",
    "#     version 1, up to 40 MHz; VIEW live <MHz> and VIEW band too)",
};

const char *sdr_tape_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

void sdr_tape_clock(double seconds, char *out, size_t max)
{
    long t = seconds < 0 ? 0 : (long)seconds;

    if (t >= 3600)
        snprintf(out, max, "%ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60);
    else
        snprintf(out, max, "%ld:%02ld", t / 60, t % 60);
}

/** @brief "spectrum" or "spectra", as @p n needs. */
static const char *spectra(unsigned n)
{
    return n == 1 ? "spectrum" : "spectra";
}

void sdr_tape_size(long bytes, char *out, size_t max)
{
    if (bytes >= 1000000L) snprintf(out, max, "%.1f MB", bytes / 1e6);
    else snprintf(out, max, "%ld KB", (bytes + 999) / 1000);
}

void sdr_tape_name(const sdr_t *st, const char *ext, char *out, size_t max)
{
    time_t now = time(NULL);
    struct tm tm;
    char when[24] = "";
    const char *leaf = sdr_tape_leaf(st->shown);
    size_t n = strlen(leaf);

    if (*st->shown && strcmp(ext, ".sdrlog")) {
        if (n > 7 && !strcmp(leaf + n - 7, ".sdrlog")) n -= 7;
        snprintf(out, max, "%.*s%s", (int)n, leaf, ext);
        return;
    }
    if (localtime_r(&now, &tm))
        strftime(when, sizeof when, "%Y-%m-%d %H%M", &tm);
    snprintf(out, max, "TikuSDR %s %s%s", *st->board ? st->board : "board",
             when, ext);
    for (char *c = out; *c; c++)
        if (*c == '/' || *c == ':') *c = '-';
}

/** @brief Open @p path for writing, made or emptied.
 *  @return the file, or NULL with errno set. */
static FILE *create(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;

    if (fd >= 0 && !f) close(fd);
    return f;
}

/** @brief Write the recording's head: what it is, whose, when, and what
 *         its lines mean. */
static void rec_head(sdr_t *st)
{
    sdr_rec_t *r = &st->rec;
    time_t now = time(NULL);
    struct tm tm;
    char began[40] = "";
    int n;

    if (localtime_r(&now, &tm))
        strftime(began, sizeof began, "%Y-%m-%d %H:%M:%S %z", &tm);
    n = fprintf(r->file, TAPE_MAGIC " 3\n# board: %s%s%s%s\n# began: %s\n",
                st->board, *st->mcu ? " (" : "", st->mcu, *st->mcu ? ")" : "",
                began);
    r->bytes = n > 0 ? n : 0;
    for (size_t i = 0; i < sizeof format / sizeof format[0]; i++)
        if ((n = fprintf(r->file, "%s\n", format[i])) > 0) r->bytes += n;
    n = fprintf(r->file, "# Version 3 VIEW range adds FFT override and rate-code+1; 0 means auto.\n"
                        "# TRIGGER threshold marks the detection time.\n");
    if (n > 0) r->bytes += n;
}

int sdr_rec_start(sdr_t *st, const char *path)
{
    sdr_rec_t *r = &st->rec;

    if (r->file || st->state != SDR_RUNNING || !path || strlen(path) >= sizeof r->path)
        return -1;
    snprintf(r->path, sizeof r->path, "%s", path);
    if (!(r->file = create(r->path))) {
        snprintf(st->note, sizeof st->note, "Cannot record to %s: %s.",
                 sdr_tape_leaf(r->path), strerror(errno));
        st->dirty = 1;
        return -1;
    }
    r->began = st->now;
    r->frames = 0;
    r->failed = 0;
    rec_head(st);
    sdr_rec_view(st);
    if (!r->file || r->failed) return -1;
    st->saved[0] = '\0';
    snprintf(st->note, sizeof st->note, "Recording to %s.",
             sdr_tape_leaf(r->path));
    st->dirty = 1;
    return 0;
}

void sdr_rec_line(sdr_t *st, const char *line, int frame)
{
    sdr_rec_t *r = &st->rec;
    long long ms = (long long)(st->now - r->began) / 1000;
    int n;

    if (!r->file) return;
    /* Seconds formatted from integers: the point is '.' in every
     * locale. */
    n = fprintf(r->file, "%lld.%03lld %s\n", ms / 1000, ms % 1000, line);
    if (n < 0 || fflush(r->file) != 0) {
        r->failed = 1;
        sdr_rec_stop(st);
        return;
    }
    r->bytes += n;
    if (frame) r->frames++;
}

void sdr_rec_view(sdr_t *st)
{
    char line[96], a[24], b[24];

    sdr_trigger_cancel(st);
    sdr_occupancy_reset(st);
    sdr_mhz_text(st->model.start, a, sizeof a);
    sdr_mhz_text(st->model.stop, b, sizeof b);
    if (st->model.fft_override || st->model.rate_override)
        snprintf(line, sizeof line, "VIEW range %s %s %d %d", a, b,
                 st->model.fft_override, st->model.rate_override);
    else snprintf(line, sizeof line, "VIEW range %s %s", a, b);
    sdr_rec_line(st, line, 0);
}

void sdr_rec_stop(sdr_t *st)
{
    sdr_rec_t *r = &st->rec;
    char took[16], size[24];

    st->trigger.armed = st->trigger.fired = 0;

    if (!r->file) return;
    if (fclose(r->file) != 0) r->failed = 1;
    r->file = NULL;
    sdr_tape_clock((double)(st->now - r->began) / 1e6, took, sizeof took);
    sdr_tape_size(r->bytes, size, sizeof size);
    if (r->failed)
        snprintf(st->saved, sizeof st->saved, "%s stopped early: it could "
                 "not be written (%s kept).", sdr_tape_leaf(r->path), size);
    else
        snprintf(st->saved, sizeof st->saved, "Saved %s: %s, %u %s, %s.",
                 sdr_tape_leaf(r->path), took, r->frames, spectra(r->frames),
                 size);
    snprintf(st->note, sizeof st->note, "%s", st->saved);
    st->dirty = 1;
}

/** @brief Read the time "12.345" at @p s, in seconds, with '.' in every
 *         locale, at most TAPE_LONGEST seconds (a little over three years).
 *  @return what follows it, or NULL when there is none. */
static const char *read_time(const char *s, double *at)
{
    char *end;
    long whole = strtol(s, &end, 10);
    double unit = 0.1;

    if (end == s || whole < 0 || whole > TAPE_LONGEST) return NULL;
    *at = (double)whole;
    if (*end == '.')
        for (end++; *end >= '0' && *end <= '9'; end++, unit /= 10)
            *at += (*end - '0') * unit;
    return end;
}

/** @brief Whether @p line is one this window plays: a time, then SPEC,
 *         SWEEP or VIEW.  @return the words after the time, or NULL. */
static const char *tape_words(const char *line, double *at)
{
    const char *rest = read_time(line, at);

    if (!rest || *rest++ != ' ') return NULL;
    if (strncmp(rest, "SPEC ", 5) && strncmp(rest, "SWEEP ", 6) &&
        strncmp(rest, "VIEW ", 5))
        return NULL;
    return rest;
}

/** @brief The last time a recording holds, read from its last TAPE_TAIL
 *         bytes; the file's position is kept. */
static double last_time(FILE *f)
{
    char line[SDR_LINE];
    long here = ftell(f), size;
    double last = 0.0, at;

    if (here < 0 || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0)
        return 0.0;
    if (fseek(f, size > TAPE_TAIL ? size - TAPE_TAIL : 0, SEEK_SET) == 0) {
        if (size > TAPE_TAIL) (void)fgets(line, sizeof line, f);  /* partial */
        while (fgets(line, sizeof line, f))
            if (tape_words(line, &at) && at > last) last = at;
    }
    (void)fseek(f, here, SEEK_SET);
    return last;
}

/** @brief Add to p->from, which the note shows while the recording plays,
 *         the board's name or the minute the recording began, when header
 *         line @p line gives one. */
static void head_line(sdr_play_t *p, const char *line)
{
    size_t n = strlen(p->from), word;

    if (!strncmp(line, "# board: ", 9)) {
        word = strcspn(line + 9, " ");
        snprintf(p->from + n, sizeof p->from - n, "%s%.*s", n ? ", " : "",
                 (int)(word < 40 ? word : 40), line + 9);
    } else if (!strncmp(line, "# began: ", 9))
        snprintf(p->from + n, sizeof p->from - n, "%s%.16s", n ? ", " : "",
                 line + 9);
}

/** @brief Read the next line of @p f this window plays: its words moved to
 *         the front of @p line and its time in @p at; header lines on the
 *         way go to head_line() when @p p is not NULL.
 *         @return 1 with a line, 0 at the end. */
static int tape_read(FILE *f, char *line, size_t max, double *at,
                     sdr_play_t *p)
{
    const char *words;
    size_t len;
    int c;

    while (fgets(line, (int)max, f)) {
        len = strlen(line);
        if (len + 1 == max && line[len - 1] != '\n') {
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;                   /* longer than any the board sends */
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' && p) head_line(p, line);
        if (!(words = tape_words(line, at))) continue;
        memmove(line, words, strlen(words) + 1);
        return 1;
    }
    return 0;
}

/** @brief Apply one recorded line to @p m: a VIEW line sets the view
 *         (VIEW band and VIEW live come from older recordings); SPEC and
 *         SWEEP go as the board's own lines do.
 *  @return 1 when it finished a frame. */
static int tape_apply(sdr_model_t *m, const char *line)
{
    char a[24], b[24];
    double start, stop;
    int mhz, fft = 0, rate = 0;

    if (sscanf(line, "VIEW range %23s %23s %d %d", a, b, &fft, &rate) >= 2) {
        if (sdr_mhz_parse(a, &start) && sdr_mhz_parse(b, &stop) &&
            (fft == 0 || fft == 64 || fft == 128 || fft == 256) &&
            rate >= 0 && rate <= 6 && (stop - start <= m->one_max || rate <= 2)) {
            m->fft_override = fft; m->rate_override = rate;
            sdr_model_range(m, start, stop);
        }
        return 0;
    }
    if (!strcmp(line, "VIEW band")) {
        sdr_model_range(m, SDR_BAND_START, SDR_BAND_STOP);
        return 0;
    }
    if (sscanf(line, "VIEW live %d", &mhz) == 1) {     /* version 1's */
        if (mhz >= SDR_TUNE_LO && mhz <= SDR_TUNE_HI)
            sdr_model_range(m, mhz - SDR_ONE_V1 / 2, mhz + SDR_ONE_V1 / 2);
        return 0;
    }
    return sdr_model_line(m, line) == 1;
}

/** @brief Open a recording for reading, past its first line, which must
 *         begin with TAPE_MAGIC; set @p one_max to the widest view one
 *         spectrum showed when it was made: SDR_ONE_V1 for version 1 or no
 *         version, else SDR_LIVE_SPAN.  @return the file, or NULL with the
 *         reason in the note. */
static FILE *tape_open(sdr_t *st, const char *path, double *one_max)
{
    char head[64];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    FILE *f = fd >= 0 ? fdopen(fd, "r") : NULL;

    if (fd >= 0 && !f) close(fd);
    if (!f) {
        snprintf(st->note, sizeof st->note, "Cannot open %s: %s.",
                 sdr_tape_leaf(path), strerror(errno));
        return NULL;
    }
    if (!fgets(head, sizeof head, f) ||
        strncmp(head, TAPE_MAGIC, strlen(TAPE_MAGIC))) {
        fclose(f);
        snprintf(st->note, sizeof st->note,
                 "%s is not a TikuSDR recording.", sdr_tape_leaf(path));
        return NULL;
    }
    *one_max = strtol(head + strlen(TAPE_MAGIC), NULL, 10) <= 1 ?
               SDR_ONE_V1 : SDR_LIVE_SPAN;
    return f;
}

/** @brief Read the next playable line into p->line, ready when there is
 *         one, to play when its time comes: a line timed before the last
 *         is skipped, and a silence over GAP_MAX seconds is cut to
 *         GAP_LEFT. */
static void next_line(sdr_play_t *p)
{
    double at;

    p->ready = 0;
    while (tape_read(p->file, p->line, sizeof p->line, &at, p)) {
        if (at < p->at) continue;
        if (p->skip_gaps && !p->seeking && !p->stepping && at - p->at > GAP_MAX)
            p->clock += (int64_t)((at - p->at - GAP_LEFT) * 1e6);
        p->at = at;
        p->ready = 1;
        return;
    }
}

int sdr_play_start(sdr_t *st, const char *path)
{
    sdr_play_t *p = &st->play;
    double one_max;
    FILE *f;

    sdr_play_stop(st);
    if (!(f = tape_open(st, path, &one_max))) return -1;
    st->nav_drag = st->previous_count = 0;
    memset(p, 0, sizeof *p);
    p->file = f;
    p->speed = 1;
    p->skip_gaps = 1;
    p->initial_start = st->model.start;
    p->initial_stop = st->model.stop;
    snprintf(p->path, sizeof p->path, "%s", path);
    p->total = last_time(f);
    p->last = st->now;
    st->paused = 0;
    st->saved[0] = '\0';
    st->model.one_max = one_max;        /* its views as they were made */
    st->model.fft_override = st->model.rate_override = 0;
    st->model.rejected = st->model.incomplete = 0;
    sdr_occupancy_reset(st);
    sdr_model_range(&st->model, st->model.start, st->model.stop);
    st->model.spectra = 0;
    next_line(p);
    if (!p->ready) {
        sdr_play_stop(st);
        snprintf(st->note, sizeof st->note, "%s holds no spectra.",
                 sdr_tape_leaf(path));
        return -1;
    }
    snprintf(st->shown, sizeof st->shown, "%s", path);
    sdr_books_load(st);
    snprintf(st->note, sizeof st->note, "Playing %s%s%s%s.",
             sdr_tape_leaf(path), *p->from ? " (" : "", p->from,
             *p->from ? ")" : "");
    st->dirty = 1;
    return 0;
}

/** @brief Apply one line of a recording to the model at the line's own
 *         time, counting a finished frame toward the rate shown. */
static void play_line(sdr_t *st, const char *line)
{
    st->model.clock = st->play.at;
    if (tape_apply(&st->model, line)) {
        st->rate_count++;
        sdr_observe(st);
    }
}

void sdr_play_tick(sdr_t *st)
{
    sdr_play_t *p = &st->play;
    int64_t shown = p->clock / 1000000;
    int burst = 0;
    char took[16];

    if (!p->file) return;
    if (p->seeking) {
        while (p->ready && burst < PLAY_BURST && p->at <= p->seek_to) {
            play_line(st, p->line);
            next_line(p);
            burst++;
        }
        if (!p->ready || p->at > p->seek_to) {
            p->seeking = 0;
            p->clock = (int64_t)(p->seek_to * 1e6);
            if (p->resume_seek) { st->paused = 0; p->resume_seek = 0; }
            snprintf(st->note, sizeof st->note, "Seek complete. Paused; Run resumes playback.");
            for (int i = 0; i < st->book_count; i++)
                if (fabs(st->books[i].at - p->seek_to) < .001)
                    snprintf(st->note, sizeof st->note, "Bookmark: %.63s (%.3f s). %s",
                             st->books[i].label, p->seek_to,
                             st->paused ? "Paused." : "Playing.");
        }
        p->last = st->now;
        st->dirty = 1;
        return;
    }
    if (p->stepping) {
        unsigned before = st->model.spectra;
        while (p->ready && burst++ < PLAY_BURST) {
            p->clock = (int64_t)(p->at * 1e6);
            play_line(st, p->line);
            next_line(p);
            if (st->model.spectra != before) break;
        }
        if (!p->ready || st->model.spectra != before) p->stepping = 0;
        p->last = st->now;
        st->dirty = 1;
        return;
    }
    if (!st->paused) p->clock += (int64_t)((st->now - p->last) * p->speed);
    p->last = st->now;
    if (st->paused) {
        while (p->ready && burst++ < PLAY_BURST &&
               p->at * 1e6 <= (double)p->clock && !strncmp(p->line, "VIEW ", 5)) {
            play_line(st, p->line);
            next_line(p);
            st->dirty = 1;
        }
        return;
    }
    if (p->loop && p->loop_b > p->loop_a && p->clock / 1e6 >= p->loop_b) {
        sdr_play_seek(st, p->loop_a);
        p->resume_seek = 1;
        return;
    }
    while (p->ready && burst < PLAY_BURST && p->at * 1e6 <= (double)p->clock &&
           (!p->loop || p->at < p->loop_b)) {
        play_line(st, p->line);
        next_line(p);
        burst++;
    }
    if (burst || p->clock / 1000000 != shown) st->dirty = 1;
    if (p->ready) return;
    sdr_tape_clock(p->at, took, sizeof took);
    snprintf(st->note, sizeof st->note, "End of %s: %s, %u %s.",
             sdr_tape_leaf(p->path), took, st->model.spectra,
             spectra(st->model.spectra));
    sdr_play_stop(st);
}

void sdr_play_seek(sdr_t *st, double seconds)
{
    sdr_play_t *p = &st->play;
    if (!isfinite(seconds)) return;
    if (!p->file) {
        char path[SDR_PATH];
        if (!st->shown[0] || st->state != SDR_IDLE) return;
        snprintf(path, sizeof path, "%s", st->shown);
        if (sdr_play_start(st, path) != 0) return;
    }
    if (fseek(p->file, 0, SEEK_SET) != 0) {
        snprintf(st->note, sizeof st->note, "Cannot seek in this recording.");
        return;
    }
    p->seek_to = seconds < 0 ? 0 : seconds > p->total ? p->total : seconds;
    p->seeking = 1;
    p->stepping = 0;
    p->resume_seek = 0;
    p->at = 0;
    p->clock = 0;
    p->last = st->now;
    st->paused = 1;
    st->model.fft_override = st->model.rate_override = 0;
    sdr_occupancy_reset(st);
    sdr_model_range(&st->model, p->initial_start, p->initial_stop);
    st->model.spectra = 0;
    st->rate_count = 0;
    next_line(p);
    snprintf(st->note, sizeof st->note, "Seeking to %.1f seconds...", p->seek_to);
    st->dirty = 1;
}

void sdr_play_step(sdr_t *st)
{
    if (!st->play.file || st->play.seeking) return;
    st->paused = 1;
    st->play.stepping = 1;
    st->play.last = st->now;
    st->dirty = 1;
}

void sdr_play_stop(sdr_t *st)
{
    if (!st->play.file) return;
    fclose(st->play.file);
    st->play.file = NULL;
    st->play.ready = 0;
    st->model.one_max = SDR_LIVE_SPAN;  /* the board's limit again */
    st->dirty = 1;
}

/** @brief @p v with @p places decimals and a '.' point in every locale:
 *         printf's %f follows the locale, which the desktop takes from the
 *         environment. */
static void fixed(char *out, size_t max, double v, int places)
{
    long long scale = 1, q;

    for (int i = 0; i < places; i++) scale *= 10;
    q = llround(fabs(v) * (double)scale);
    snprintf(out, max, "%s%lld.%0*lld", v < 0 && q ? "-" : "", q / scale,
             places, q % scale);
}

/** @brief One spectrum as rows of time, frequency and level.
 *         @return the rows written, or -1 when one could not be. */
static long csv_frame(FILE *out, const sdr_model_t *m, double t,
                      const float *db)
{
    char at[24], mhz[24], level[16];

    fixed(at, sizeof at, t, 3);
    for (int j = 0; j < m->n; j++) {
        fixed(mhz, sizeof mhz, sdr_model_freq(m, j), 5);
        fixed(level, sizeof level, db[j], 1);
        if (fprintf(out, "%s,%s,%s\n", at, mhz, level) < 0) return -1;
    }
    return m->n;
}

/** @brief Write the spectra the spectrogram holds, oldest first, timed
 *         from the first, at its half-decibel steps from 0 to 127.5 dB.
 *         @return the rows written, or -1. */
static long csv_held(const sdr_model_t *m, FILE *out)
{
    int first = (m->wf_head - m->wf_count + 1 + SDR_ROWS) % SDR_ROWS;
    float db[SDR_BINS];
    long rows = 0, got;

    for (int k = 0; k < m->wf_count; k++) {
        int r = (first + k) % SDR_ROWS;
        for (int j = 0; j < m->n; j++) db[j] = m->wf[r][j] / 2.0f;
        got = csv_frame(out, m, m->wf_at[r] - m->wf_at[first], db);
        if (got < 0) return -1;
        rows += got;
    }
    return rows;
}

/** @brief Close the export and say in the note what it became, or that it
 *         could not be written whole when @p failed. */
static void csv_done(sdr_t *st, int failed)
{
    sdr_csv_t *x = &st->csv;
    long bytes = x->out ? ftell(x->out) : 0;
    char size[24];

    if (x->out && fclose(x->out) != 0) failed = 1;
    if (x->in) fclose(x->in);
    free(x->model);
    x->in = x->out = NULL;
    x->model = NULL;
    sdr_tape_size(bytes, size, sizeof size);
    if (failed)
        snprintf(st->note, sizeof st->note, "%s could not be written whole.",
                 sdr_tape_leaf(x->path));
    else
        snprintf(st->note, sizeof st->note, "Exported %s: %u %s, %ld rows, "
                 "%s.", sdr_tape_leaf(x->path), x->frames, spectra(x->frames),
                 x->rows, size);
    st->dirty = 1;
}

int sdr_csv_start(sdr_t *st, const char *path)
{
    sdr_csv_t *x = &st->csv;
    FILE *in = NULL;
    double one_max = SDR_LIVE_SPAN;
    long rows;

    if (x->out || (*st->shown && !(in = tape_open(st, st->shown, &one_max))))
        return -1;
    snprintf(x->path, sizeof x->path, "%s", path);
    x->rows = 0;
    x->frames = 0;
    if (!(x->out = create(path)) || fputs(CSV_HEAD, x->out) < 0 ||
        (in && !(x->model = calloc(1, sizeof *x->model)))) {
        int why = errno;

        x->in = in;
        csv_done(st, 1);
        snprintf(st->note, sizeof st->note, "Cannot write %s: %s.",
                 sdr_tape_leaf(path), strerror(why));
        return -1;
    }
    if (!in) {                          /* what the waterfall holds */
        rows = csv_held(&st->model, x->out);
        x->rows = rows < 0 ? 0 : rows;
        x->frames = (unsigned)st->model.wf_count;
        csv_done(st, rows < 0);
        return rows < 0 ? -1 : 0;
    }
    x->in = in;
    sdr_model_init(x->model);
    x->model->one_max = one_max;
    if (fseek(in, 0, SEEK_END) == 0) x->size = ftell(in);
    (void)fseek(in, 0, SEEK_SET);
    snprintf(st->note, sizeof st->note, "Exporting %s...",
             sdr_tape_leaf(path));
    st->dirty = 1;
    return 0;
}

void sdr_csv_tick(sdr_t *st)
{
    sdr_csv_t *x = &st->csv;
    long got, done = 0;
    double at;

    if (!x->in) return;
    for (int i = 0; i < CSV_LINES && done < CSV_ROWS; i++) {
        if (!tape_read(x->in, x->line, sizeof x->line, &at, NULL)) {
            csv_done(st, ferror(x->in));
            return;
        }
        x->model->clock = at;
        if (!tape_apply(x->model, x->line)) continue;
        if ((got = csv_frame(x->out, x->model, at, x->model->db)) < 0) {
            csv_done(st, 1);
            return;
        }
        x->rows += got;
        done += got;
        x->frames++;
    }
    snprintf(st->note, sizeof st->note, "Exporting %s... %ld%%",
             sdr_tape_leaf(x->path),
             x->size > 0 ? ftell(x->in) * 100 / x->size : 0L);
    st->dirty = 1;
}

void sdr_csv_stop(sdr_t *st)
{
    if (st->csv.in) csv_done(st, 1);
}
