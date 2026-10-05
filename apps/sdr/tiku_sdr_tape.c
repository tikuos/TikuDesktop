/* tiku_sdr_tape.c -- TikuSDR's recordings: every line the board sends,
 * written with its time, played back at the pace it was made, and spectra
 * exported as CSV.  A recording is plain text with its format at its head,
 * so a script reads one as easily as this window does.
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
#define TAPE_TAIL    8192           /* read for the last time it holds   */
#define TAPE_LONGEST 100000000L     /* seconds: a later time is no time  */
#define GAP_MAX      2.0            /* seconds: a longer silence is cut  */
#define GAP_LEFT     0.5            /* ... down to this                  */
#define PLAY_BURST   64             /* lines played in one tick, at most */
#define CSV_LINES    2000           /* a recording's lines read a tick   */
#define CSV_ROWS     16384          /* ... or rows written, the fewer    */
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

/** @brief A new file at @p path, or a file emptied, for writing.
 *         @return NULL with errno set when it cannot be. */
static FILE *create(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;

    if (fd >= 0 && !f) close(fd);
    return f;
}

/** @brief The head: what this is, whose, when, and what its lines mean. */
static void rec_head(sdr_t *st)
{
    sdr_rec_t *r = &st->rec;
    time_t now = time(NULL);
    struct tm tm;
    char began[40] = "";
    int n;

    if (localtime_r(&now, &tm))
        strftime(began, sizeof began, "%Y-%m-%d %H:%M:%S %z", &tm);
    n = fprintf(r->file, TAPE_MAGIC " 2\n# board: %s%s%s%s\n# began: %s\n",
                st->board, *st->mcu ? " (" : "", st->mcu, *st->mcu ? ")" : "",
                began);
    r->bytes = n > 0 ? n : 0;
    for (size_t i = 0; i < sizeof format / sizeof format[0]; i++)
        if ((n = fprintf(r->file, "%s\n", format[i])) > 0) r->bytes += n;
}

int sdr_rec_start(sdr_t *st, const char *path)
{
    sdr_rec_t *r = &st->rec;

    if (r->file || st->state != SDR_RUNNING) return -1;
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
    /* Seconds written by hand, so no locale turns the point into a comma. */
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
    char line[64], a[24], b[24];

    sdr_mhz_text(st->model.start, a, sizeof a);
    sdr_mhz_text(st->model.stop, b, sizeof b);
    snprintf(line, sizeof line, "VIEW range %s %s", a, b);
    sdr_rec_line(st, line, 0);
}

void sdr_rec_stop(sdr_t *st)
{
    sdr_rec_t *r = &st->rec;
    char took[16], size[24];

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

/** @brief The time "12.345" at @p s, in seconds, whatever the locale, and
 *         under three years, so microseconds of it fit any clock.
 *         @return what follows it, or NULL when there is none. */
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

/** @brief The last time a recording holds, read from its tail. */
static double last_time(FILE *f)
{
    char line[SDR_LINE];
    long here = ftell(f), size;
    double last = 0.0, at;

    if (here < 0 || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0)
        return 0.0;
    if (fseek(f, size > TAPE_TAIL ? size - TAPE_TAIL : 0, SEEK_SET) == 0) {
        if (size > TAPE_TAIL) (void)fgets(line, sizeof line, f);  /* a part */
        while (fgets(line, sizeof line, f))
            if (tape_words(line, &at) && at > last) last = at;
    }
    (void)fseek(f, here, SEEK_SET);
    return last;
}

/** @brief A header line kept for the status: the board's name, and the
 *         minute the recording began. */
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

/** @brief The next line of @p f this window plays: its words moved to the
 *         front of @p line, its time in @p at, and the header lines on the
 *         way handed to @p p when there is one.  @return 0 at the end. */
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

/** @brief One recorded line into @p m, as the board's own line would go.
 *         @return 1 when it finished a spectrum. */
static int tape_apply(sdr_model_t *m, const char *line)
{
    char a[24], b[24];
    double start, stop;
    int mhz;

    if (sscanf(line, "VIEW range %23s %23s", a, b) == 2) {
        if (sdr_mhz_parse(a, &start) && sdr_mhz_parse(b, &stop))
            sdr_model_range(m, start, stop);
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

/** @brief A recording opened for reading, past its first line, which must
 *         say what it is; @p one_max the widest view one spectrum showed
 *         when it was made: 40 MHz in version 1, else 24.  @return NULL
 *         with the reason in the note. */
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

/** @brief The next line worth playing, held until its time comes; a long
 *         silence is cut short, so playing never seems to stall. */
static void next_line(sdr_play_t *p)
{
    double at;

    p->ready = 0;
    while (tape_read(p->file, p->line, sizeof p->line, &at, p)) {
        if (at < p->at) continue;
        if (at - p->at > GAP_MAX)
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
    memset(p, 0, sizeof *p);
    p->file = f;
    snprintf(p->path, sizeof p->path, "%s", path);
    p->total = last_time(f);
    p->last = st->now;
    st->paused = 0;
    st->saved[0] = '\0';
    st->model.one_max = one_max;        /* its views as they were made */
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
    snprintf(st->note, sizeof st->note, "Playing %s%s%s%s.",
             sdr_tape_leaf(path), *p->from ? " (" : "", p->from,
             *p->from ? ")" : "");
    st->dirty = 1;
    return 0;
}

/** @brief One line of a recording, as if the board had just sent it. */
static void play_line(sdr_t *st, const char *line)
{
    st->model.clock = st->play.at;
    if (tape_apply(&st->model, line)) st->rate_count++;
}

void sdr_play_tick(sdr_t *st)
{
    sdr_play_t *p = &st->play;
    int64_t shown = p->clock / 1000000;
    int burst = 0;
    char took[16];

    if (!p->file) return;
    if (!st->paused) p->clock += st->now - p->last;
    p->last = st->now;
    while (p->ready && burst < PLAY_BURST && p->at * 1e6 <= (double)p->clock) {
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

void sdr_play_stop(sdr_t *st)
{
    if (!st->play.file) return;
    fclose(st->play.file);
    st->play.file = NULL;
    st->play.ready = 0;
    st->model.one_max = SDR_LIVE_SPAN;  /* the next view the board's way */
    st->dirty = 1;
}

/** @brief @p v with @p places decimals and a point, whatever the locale
 *         (the desktop takes its locale from the environment). */
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

/** @brief The waterfall's spectra, oldest first, timed from the first.
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

/** @brief The export closed, and what it became said. */
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
