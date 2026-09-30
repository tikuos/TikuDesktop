/* editor_fixtures.inc -- the editor baseline's documents and edits: the
 * same generated fixtures and edit scripts for the document engine,
 * Editor and the IDE, a plain byte model to check each one against, and
 * the report the three tests share.  Included by
 * tests/kits/test_editdoc_fixtures.c, tests/apps/test_editor_baseline.c
 * and tests/apps/test_ide_baseline.c.
 *
 * Nothing large is committed: every fixture is generated from a fixed
 * seed.  TIKU_EDITOR_FIXTURES=<dir> keeps the files there for opening by
 * hand; TIKU_EDITOR_STRESS=1 adds the 100 MiB fixture.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

/* The fixtures, in report order. */
enum {
    FX_BASIC, FX_C, FX_NOTES, FX_CRLF, FX_MIXED, FX_CONTROLS, FX_SOURCE,
    FX_LOG, FX_SHORT, FX_LONGLINE, FX_STRESS, FX_COUNT
};

/* Operations per script. */
enum { FX_TYPE_OPS = 100, FX_NEWLINE_OPS = 50 };

/** @brief What a fixture is, for the report. */
typedef struct {
    const char *name;           /* its file name                        */
    const char *what;           /* what it holds                        */
    int stress;                 /* made only with TIKU_EDITOR_STRESS=1  */
} fx_info_t;

static const fx_info_t fx_info[FX_COUNT] = {
    { "small.bas", "a numbered BASIC program", 0 },
    { "small.c", "a C source file", 0 },
    { "notes.txt", "UTF-8 notes with tabs", 0 },
    { "notes-crlf.txt", "short text with CRLF endings", 0 },
    { "mixed.txt", "LF and CRLF endings in one file", 0 },
    { "controls.txt", "a form feed and escape bytes", 0 },
    { "source-10m.c", "10 MiB of C-like source", 0 },
    { "log-10m.log", "10 MiB of log lines", 0 },
    { "short-100k.txt", "100,000 short lines", 0 },
    { "line-1m.txt", "one 1 MiB line of tabs and multibyte text", 0 },
    { "stress-100m.c", "100 MiB of C-like source", 1 }
};

/** @brief A growing byte string, always NUL-terminated. */
typedef struct {
    char *p;                    /* the bytes                            */
    size_t n;                   /* how many are used                    */
    size_t cap;                 /* how many are allocated               */
} fx_buf_t;

/** @brief Make room for @p more bytes and a NUL; exits when memory is
 *         exhausted, as a fixture that cannot be built is no result. */
static void
fx_room(fx_buf_t *b, size_t more)
{
    size_t cap = b->cap ? b->cap : 4096;
    char *p;

    if (b->n + more + 1 <= b->cap)
        return;
    while (b->n + more + 1 > cap)
        cap *= 2;
    p = realloc(b->p, cap);
    if (!p) {
        fprintf(stderr, "editor fixtures: out of memory\n");
        exit(2);
    }
    b->p = p;
    b->cap = cap;
}

/** @brief Append @p n bytes of @p s. */
static void
fx_put(fx_buf_t *b, const char *s, size_t n)
{
    fx_room(b, n);
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

/** @brief Append the string @p s. */
static void
fx_puts(fx_buf_t *b, const char *s)
{
    fx_put(b, s, strlen(s));
}

/** @brief Release @p b's bytes. */
static void
fx_free(fx_buf_t *b)
{
    free(b->p);
    memset(b, 0, sizeof *b);
}

static uint64_t fx_seed;

/** @brief The next of a fixed stream of numbers, below @p n. */
static unsigned
fx_next(unsigned n)
{
    fx_seed ^= fx_seed << 13;
    fx_seed ^= fx_seed >> 7;
    fx_seed ^= fx_seed << 17;
    return (unsigned)(fx_seed % n);
}

/** @brief A BASIC program of 90 numbered lines. */
static void
fx_basic(fx_buf_t *b)
{
    static const char *const body[] = {
        "REM read the sensor and print it", "LET T = 0",
        "FOR I = 1 TO 10", "T = T + ADC(1)", "NEXT I",
        "PRINT \"average: \"; T / 10", "IF T > 400 THEN GOSUB 900",
        "SLEEP 1000", "GOTO 20"
    };
    char line[96];
    int i;

    for (i = 0; i < 90; i++) {
        snprintf(line, sizeof line, "%d %s\n", (i + 1) * 10, body[i % 9]);
        fx_puts(b, line);
    }
}

/** @brief A C file of twelve small functions. */
static void
fx_c(fx_buf_t *b)
{
    char line[96];
    int k;

    fx_puts(b, "#include <stdint.h>\n\n");
    for (k = 0; k < 12; k++) {
        fx_puts(b, "/* sample a channel and keep a running mean */\n");
        fx_puts(b, "static int\n");
        snprintf(line, sizeof line, "mean_of_%d(const int *v, int n)\n", k);
        fx_puts(b, line);
        fx_puts(b, "{\n    int i, sum = 0;\n\n");
        fx_puts(b, "    for (i = 0; i < n; i++)\n        sum += v[i];\n");
        fx_puts(b, "    return n ? sum / n : 0;\n}\n\n");
    }
}

/** @brief Notes with tabs, accents, CJK and an emoji. */
static void
fx_notes(fx_buf_t *b)
{
    static const char *const body[] = {
        "Sensor notes\n",
        "\tTemperature:\t24.8 \xc2\xb0" "C\n",
        "Caf\xc3\xa9 by the lab, r\xc3\xa9sum\xc3\xa9 filed\n",
        "\xe6\xb8\xa9\xe5\xba\xa6 (temperature) and "
        "\xe6\xb9\xbf\xe5\xba\xa6 (humidity)\n",
        "Status \xf0\x9f\x98\x80 all good\n",
        "\n"
    };
    int i;

    for (i = 0; i < 48; i++)
        fx_puts(b, body[i % 6]);
}

/** @brief C-like source lines until @p size bytes. */
static void
fx_source(fx_buf_t *b, size_t size)
{
    static const char *const w[] = {
        "sensor", "value", "count", "index", "buffer", "state", "event",
        "timer", "channel", "sample"
    };
    char line[160];

    fx_room(b, size + 160);
    while (b->n < size) {
        const char *a = w[fx_next(10)], *c = w[fx_next(10)];
        unsigned v = fx_next(100000);

        switch (fx_next(6)) {
        case 0:
            snprintf(line, sizeof line, "    %s_%s = %s_%s + %u;\n", a, c,
                     c, a, v);
            break;
        case 1:
            snprintf(line, sizeof line, "    if (%s_%s > %u)\n", a, c, v);
            break;
        case 2:
            snprintf(line, sizeof line, "        return %s_%s(%u);\n", a,
                     c, v);
            break;
        case 3:
            snprintf(line, sizeof line, "/* %s %s at %u */\n", a, c, v);
            break;
        case 4:
            snprintf(line, sizeof line, "static int %s_%s_%u;\n", a, c, v);
            break;
        default:
            snprintf(line, sizeof line, "\n");
        }
        fx_puts(b, line);
    }
}

/** @brief Log lines until @p size bytes. */
static void
fx_log(fx_buf_t *b, size_t size)
{
    static const char *const level[] = { "INFO", "WARN", "DEBUG" };
    static const char *const from[] = { "sensor", "radio", "storage" };
    static const char *const key[] = { "temperature", "humidity", "rssi" };
    char line[160];
    unsigned t = 0;

    fx_room(b, size + 160);
    while (b->n < size) {
        t += 1 + fx_next(900);
        snprintf(line, sizeof line,
                 "2026-09-30T%02u:%02u:%02u.%03uZ %s %s[%u] %s=%u.%u\n",
                 t / 3600000 % 24, t / 60000 % 60, t / 1000 % 60, t % 1000,
                 level[fx_next(3)], from[fx_next(3)], fx_next(16),
                 key[fx_next(3)], fx_next(100), fx_next(10));
        fx_puts(b, line);
    }
}

/** @brief 100,000 short numbered lines. */
static void
fx_short(fx_buf_t *b)
{
    char line[32];
    unsigned i;

    fx_room(b, 100000u * 12u);
    for (i = 1; i <= 100000u; i++) {
        snprintf(line, sizeof line, "line %06u\n", i);
        fx_puts(b, line);
    }
}

/** @brief One line of at least 1 MiB: words, tabs and characters of two,
 *         three and four bytes, with no line break. */
static void
fx_longline(fx_buf_t *b)
{
    static const char *const piece[] = {
        "word ", "\t", "\xc3\xa9", "\xe6\xb8\xa9", "\xf0\x9f\x98\x80",
        "data "
    };

    fx_room(b, (1u << 20) + 8);
    while (b->n < (1u << 20))
        fx_puts(b, piece[fx_next(6)]);
}

/** @brief Build fixture @p fx into @p b, the same bytes every time. */
static void
fx_make(int fx, fx_buf_t *b)
{
    memset(b, 0, sizeof *b);
    fx_seed = 0x9e3779b97f4a7c15ull + (uint64_t)fx;
    switch (fx) {
    case FX_BASIC: fx_basic(b); break;
    case FX_C: fx_c(b); break;
    case FX_NOTES: fx_notes(b); break;
    case FX_CRLF: fx_puts(b, "one\r\ntwo\r\nthree\r\nfour\r\n"); break;
    case FX_MIXED: fx_puts(b, "one\ntwo\r\nthree\n"); break;
    case FX_CONTROLS:
        fx_puts(b, "page one\fpage two\n\x1b[1mbold\x1b[0m\n");
        break;
    case FX_SOURCE: fx_source(b, 10u << 20); break;
    case FX_LOG: fx_log(b, 10u << 20); break;
    case FX_SHORT: fx_short(b); break;
    case FX_LONGLINE: fx_longline(b); break;
    default: fx_source(b, 100u << 20); break;
    }
}

/** @brief Whether fixture @p fx runs in this invocation. */
static int
fx_enabled(int fx)
{
    const char *s = getenv("TIKU_EDITOR_STRESS");

    return !fx_info[fx].stress || (s && *s == '1');
}

/** @brief Write @p b to @p dir / fixture @p fx's name into @p path. */
static int
fx_write(const char *dir, int fx, const fx_buf_t *b, char *path, size_t cap)
{
    FILE *f;
    int ok;

    if ((size_t)snprintf(path, cap, "%s/%s", dir, fx_info[fx].name) >= cap)
        return 0;
    f = fopen(path, "wb");
    if (!f)
        return 0;
    ok = fwrite(b->p, 1, b->n, f) == b->n;
    return fclose(f) == 0 && ok;
}

/** @brief The directory fixtures are written to: TIKU_EDITOR_FIXTURES,
 *         kept afterwards, or a new temporary one.  @return 1 if kept. */
static int
fx_dir(char *dir, size_t cap)
{
    const char *keep = getenv("TIKU_EDITOR_FIXTURES");

    if (keep && *keep) {
        snprintf(dir, cap, "%s", keep);
        mkdir(dir, 0755);
        return 1;
    }
    snprintf(dir, cap, "/tmp/tiku-editor-fixtures-XXXXXX");
    if (!mkdtemp(dir)) {
        fprintf(stderr, "editor fixtures: no temporary folder\n");
        exit(2);
    }
    return 0;
}

/** @brief Remove the fixtures written to a temporary @p dir. */
static void
fx_dir_done(const char *dir, int kept)
{
    char path[512];
    int fx;

    if (kept)
        return;
    for (fx = 0; fx < FX_COUNT; fx++) {
        if ((size_t)snprintf(path, sizeof path, "%s/%s", dir,
                             fx_info[fx].name) < sizeof path)
            unlink(path);
    }
    rmdir(dir);
}

/* The model: the expected bytes, lines split at LF, a CR before an LF
 * kept with its line's text but never counted in a column. */

/** @brief The byte offset of line @p y, column @p x of @p b. */
static size_t
fx_offset(const fx_buf_t *b, long y, size_t x)
{
    const char *p = b->p, *end = b->p + b->n;

    while (y-- > 0) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));

        if (!nl)
            break;
        p = nl + 1;
    }
    return (size_t)(p - b->p) + x;
}

/** @brief Lines split at LF: one more than the number of LFs. */
static long
fx_lines(const fx_buf_t *b)
{
    const char *p = b->p, *end = b->p + b->n;
    long n = 1;

    while ((p = memchr(p, '\n', (size_t)(end - p))) != NULL) {
        n++;
        p++;
    }
    return n;
}

/** @brief The last line with text: the one before a final empty line. */
static long
fx_last(const fx_buf_t *b)
{
    long n = fx_lines(b);

    return n > 1 && b->p[b->n - 1] == '\n' ? n - 2 : n - 1;
}

/** @brief The length of line @p y's text, without its LF or a CR. */
static size_t
fx_line_len(const fx_buf_t *b, long y)
{
    size_t s = fx_offset(b, y, 0), e = s;

    while (e < b->n && b->p[e] != '\n')
        e++;
    if (e > s && e < b->n && b->p[e - 1] == '\r')
        e--;
    return e - s;
}

/** @brief The column at or before @p x that starts a character. */
static size_t
fx_boundary(const fx_buf_t *b, long y, size_t x)
{
    size_t s = fx_offset(b, y, 0);

    while (x > 0 && ((unsigned char)b->p[s + x] & 0xc0) == 0x80)
        x--;
    return x;
}

/** @brief Replace [@p s, @p e) of @p b by @p n bytes of @p text. */
static void
fx_splice(fx_buf_t *b, size_t s, size_t e, const char *text, size_t n)
{
    fx_room(b, n);
    memmove(b->p + s + n, b->p + e, b->n - e + 1);
    memcpy(b->p + s, text, n);
    b->n = b->n - (e - s) + n;
}

/* The script: typing alternately at the start and at the last line,
 * line breaks typed in the middle line, then one large replacement that
 * is undone and redone. */
enum { FX_TYPE, FX_NEWLINE, FX_REPLACE };

/** @brief One edit: line y0 column x0 up to line y1 column x1 replaced. */
typedef struct {
    int kind;                   /* FX_TYPE, FX_NEWLINE or FX_REPLACE    */
    long y0, y1;                /* the lines                            */
    size_t x0, x1;              /* the byte columns                     */
    const char *text;           /* the replacement                      */
    size_t len;                 /* its length                           */
} fx_op_t;

static fx_buf_t fx_block;

/** @brief The replacement text: short numbered lines, a quarter of the
 *         document's size, between 256 bytes and 64 KiB. */
static void
fx_make_block(size_t doc)
{
    size_t want = doc / 4;
    char line[32];
    unsigned i = 0;

    fx_free(&fx_block);
    if (want < 256)
        want = 256;
    if (want > 65536)
        want = 65536;
    while (fx_block.n < want) {
        snprintf(line, sizeof line, "replaced line %05u\n", i++);
        fx_puts(&fx_block, line);
    }
}

/** @brief The @p i-th edit of fixture @p fx's script, worked out from the
 *         model @p m as it stands and applied to it.  @return 0 after the
 *         last. */
static int
fx_step(int fx, fx_buf_t *m, int i, fx_op_t *op)
{
    long last = fx_last(m);

    memset(op, 0, sizeof *op);
    if (i < FX_TYPE_OPS) {
        op->kind = FX_TYPE;
        op->y0 = op->y1 = i % 2 ? last : 0;
        op->text = i % 2 ? "y" : "x";
    } else if (i < FX_TYPE_OPS + FX_NEWLINE_OPS) {
        op->kind = FX_NEWLINE;
        op->y0 = op->y1 = last / 2;
        op->x0 = op->x1 = fx_boundary(m, last / 2,
                                      fx_line_len(m, last / 2) / 2);
        op->text = "\n";
    } else if (i == FX_TYPE_OPS + FX_NEWLINE_OPS) {
        op->kind = FX_REPLACE;
        op->text = fx_block.p;
        if (fx == FX_CRLF) {
            /* From inside line 1 to inside line 3, across two CRLFs;
             * the lines are short by now, so the columns are clamped
             * as a caret would be. */
            op->y0 = 1;
            op->y1 = 3;
            op->x0 = fx_line_len(m, 1) < 1 ? fx_line_len(m, 1) : 1;
            op->x1 = fx_line_len(m, 3) < 2 ? fx_line_len(m, 3) : 2;
            op->text = "-";
        } else if (last >= 4) {
            op->y0 = last / 4;
            op->y1 = last - last / 4;
        } else {
            size_t len = fx_line_len(m, 0);

            op->x0 = fx_boundary(m, 0, len / 4);
            op->x1 = fx_boundary(m, 0, len - len / 4);
        }
    } else {
        return 0;
    }
    op->len = strlen(op->text);
    fx_splice(m, fx_offset(m, op->y0, op->x0), fx_offset(m, op->y1, op->x1),
              op->text, op->len);
    return 1;
}

/** @brief Now, in nanoseconds. */
static uint64_t
fx_now(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/** @brief Heap bytes in use, blocks the C library mapped on their own
 *         included, or 0 where it cannot say. */
static size_t
fx_heap(void)
{
#ifdef __GLIBC__
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)
    struct mallinfo2 mi = mallinfo2();

    return mi.uordblks + mi.hblkhd;
#else
    return 0;
#endif
#else
    return 0;
#endif
}

/** @brief Durations of one kind of operation. */
typedef struct {
    uint64_t v[FX_TYPE_OPS];    /* nanoseconds                          */
    int n;                      /* how many                             */
} fx_times_t;

/** @brief Record one duration. */
static void
fx_time(fx_times_t *t, uint64_t ns)
{
    if (t->n < FX_TYPE_OPS)
        t->v[t->n++] = ns;
}

/** @brief qsort order for durations. */
static int
fx_cmp(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

    return x < y ? -1 : x > y;
}

/** @brief The @p q-th percentile of @p t by nearest rank, in µs. */
static double
fx_pct(fx_times_t *t, int q)
{
    int k;

    if (!t->n)
        return 0;
    qsort(t->v, (size_t)t->n, sizeof *t->v, fx_cmp);
    k = (q * t->n + 99) / 100 - 1;
    return t->v[k < 0 ? 0 : k] / 1000.0;
}

/** @brief One implementation's results on one fixture. */
typedef struct {
    int loaded;                 /* 1 when the fixture opened            */
    char refusal[200];          /* what it said when it did not         */
    uint64_t load;              /* read and load, ns                    */
    uint64_t copy;              /* a copy of the whole text, ns         */
    uint64_t replace;           /* the large replacement, ns            */
    uint64_t undo;              /* undoing it, ns                       */
    uint64_t redo;              /* redoing it, ns                       */
    fx_times_t type;            /* each typed character                 */
    fx_times_t newline;         /* each typed line break                */
    size_t heap;                /* heap growth from the load, bytes     */
} fx_result_t;

/** @brief The machine, compiler and options the numbers come from. */
static void
fx_report_machine(const char *impl)
{
    struct utsname u;
    char line[256], model[160] = "unknown CPU";
    FILE *f = fopen("/proc/cpuinfo", "r");
#ifdef __OPTIMIZE__
    const char *opt = ", optimised";
#else
    const char *opt = ", not optimised";
#endif

    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "model name", 10) && strchr(line, ':')) {
            snprintf(model, sizeof model, "%s", strchr(line, ':') + 2);
            model[strcspn(model, "\n")] = 0;
            break;
        }
    if (f)
        fclose(f);
    if (uname(&u))
        memset(&u, 0, sizeof u);
    printf("%s baseline: %s, %ld CPUs; %s %s %s; cc %s%s\n", impl, model,
           sysconf(_SC_NPROCESSORS_ONLN), u.sysname, u.release, u.machine,
           __VERSION__, opt);
    printf("| Fixture | Bytes | Lines | Load ms | Heap MiB | Copy ms | "
           "Type p50/p95 us | Line break p50/p95 us | Replace ms | "
           "Undo ms | Redo ms |\n");
    printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
           "---: | ---: |\n");
}

/** @brief One row of the report. */
static void
fx_report_row(int fx, const fx_buf_t *b, fx_result_t *r)
{
    if (!r->loaded) {
        printf("| %s | %zu | %ld | refused: %s | | | | | | | |\n",
               fx_info[fx].name, b->n, fx_lines(b), r->refusal);
        return;
    }
    printf("| %s | %zu | %ld | %.1f | %.1f | %.2f | %.1f / %.1f | "
           "%.1f / %.1f | %.2f | %.2f | %.2f |\n", fx_info[fx].name, b->n,
           fx_lines(b), r->load / 1e6, r->heap / 1048576.0, r->copy / 1e6,
           fx_pct(&r->type, 50), fx_pct(&r->type, 95),
           fx_pct(&r->newline, 50), fx_pct(&r->newline, 95),
           r->replace / 1e6, r->undo / 1e6, r->redo / 1e6);
}
