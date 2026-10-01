/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_speed.c - how long a code view takes, on generated
 * documents: a small program, 10 MiB of source, 100,000 short lines and
 * a 1 MiB line, and 100 MiB when TIKU_CODEVIEW_SPEED_BIG is set.  Typing,
 * moving, scrolling, searching and Replace All are timed in process, and
 * typing and moving through the desktop transport.  Results are checked;
 * times are printed, not judged.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE 1
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tiku_client.h"
#include "tiku_codeview.h"
#include "tiku_dl.h"
#include "tiku_remote.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

static int64_t
now_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static int
order(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;

    return x < y ? -1 : x > y;
}

/** @brief Median and p95 of @p n times, which are sorted. */
static void
report(const char *fixture, const char *what, int64_t *t, int n)
{
    qsort(t, (size_t)n, sizeof *t, order);
    printf("  %-14s %-24s median %6lld us  p95 %6lld us  (%d)\n", fixture,
           what, (long long)t[n / 2], (long long)t[(n * 95) / 100], n);
}

/** @brief One timing, printed. */
static void
once(const char *fixture, const char *what, int64_t us)
{
    printf("  %-14s %-24s %8lld us\n", fixture, what, (long long)us);
}

/*---------------------------------------------------------------------------*/
/* Fixtures                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Lines of C-shaped source to about @p bytes. */
static char *
source(size_t bytes, size_t *n)
{
    static const char *const lines[] = {
        "static int count_%zu(const char *p) /* tally */\n",
        "{\n",
        "    int n = 0x%zx; /* a remark\n",
        "       that runs on */ while (*p++) n += 'a';\n",
        "    return n; // done %zu\n",
        "}\n",
        "\tchar *s = \"caf\xc3\xa9 \xce\xb1\xce\xb2 %zu\";\n",
    };
    char *s = malloc(bytes + 256);
    size_t len = 0, i = 0;

    if (s == NULL)
        exit(1);
    while (len < bytes) {
        len += (size_t)snprintf(s + len, 256, lines[i % 7], i, i, i);
        i++;
    }
    *n = len;
    return s;
}

/** @brief @p count short lines. */
static char *
short_lines(size_t count, size_t *n)
{
    char *s = malloc(count * 16 + 1);
    size_t len = 0, i;

    if (s == NULL)
        exit(1);
    for (i = 0; i < count; i++)
        len += (size_t)sprintf(s + len, "x%zu = %zu;\n", i % 1000, i % 97);
    *n = len;
    return s;
}

/** @brief One line of about @p bytes, with tabs and multibyte letters. */
static char *
long_line(size_t bytes, size_t *n)
{
    static const char unit[] = "word\t\xc3\xa9t\xc3\xa9 \xce\xbb x=1; ";
    char *s = malloc(bytes + sizeof unit);
    size_t len = 0;

    if (s == NULL)
        exit(1);
    while (len < bytes) {
        memcpy(s + len, unit, sizeof unit - 1);
        len += sizeof unit - 1;
    }
    *n = len;
    return s;
}

/** @brief A small BASIC program. */
static char *
program(size_t *n)
{
    char *s = malloc(120 * 48);
    size_t len = 0;
    int i;

    if (s == NULL)
        exit(1);
    for (i = 1; i <= 120; i++)
        len += (size_t)sprintf(s + len, "%d PRINT \"line \"; %d * A%d\n",
                               i * 10, i, i % 7);
    *n = len;
    return s;
}

/*---------------------------------------------------------------------------*/
/* In process                                                                 */
/*---------------------------------------------------------------------------*/

#define RUNS 200

/** @brief A key event. */
static tiku_event_t
key(unsigned k, unsigned mods, const char *text)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = k;
    e.modifiers = mods;
    if (text != NULL)
        memcpy(e.text, text, strnlen(text, sizeof e.text - 1));
    return e;
}

/** @brief Time @p runs of an event and the repaint after it. */
static void
time_event(tiku_codeview_t *v, tiku_surface_t *s, const char *fixture,
           const char *what, tiku_event_t e)
{
    int64_t t[RUNS];
    int i;

    for (i = 0; i < RUNS; i++) {
        int64_t start = now_us();

        (void)tiku_codeview_event(v, &e);
        tiku_codeview_paint(v, s);
        t[i] = now_us() - start;
    }
    report(fixture, what, t, RUNS);
}

/** @brief Every measure on one fixture of @p n bytes of @p text. */
static void
measure(const char *fixture, const char *text, size_t n,
        tiku_syntax_lang_t lang, const char *query)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;
    tiku_surface_t *s = tiku_surface_new(1280, 800, TIKU_C_PANEL);
    tiku_rect_t box = { 0, 0, 1280, 800 };
    tiku_edit_query_t q;
    tiku_codeview_found_t f;
    int64_t start, lines, length;
    tiku_edit_status_t st;
    tiku_event_t e;

    if (s == NULL || tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK)
        exit(1);
    start = now_us();
    st = tiku_editdoc_load(doc, text, n);
    once(fixture, "load", now_us() - start);
    check(st == TIKU_EDIT_OK, "PERF-001", "the fixture loads");
    if (tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS |
                                 TIKU_CODEVIEW_VSCROLL |
                                 TIKU_CODEVIEW_HSCROLL);
    tiku_codeview_set_language(v, lang);
    lines = tiku_editdoc_lines(doc);
    start = now_us();
    tiku_codeview_paint(v, s);
    once(fixture, "first paint", now_us() - start);

    tiku_codeview_select(v, tiku_editdoc_line_start(doc, lines / 2),
                         tiku_editdoc_line_start(doc, lines / 2));
    tiku_codeview_reveal(v);
    length = tiku_editdoc_length(doc);
    time_event(v, s, fixture, "type, middle", key('x', 0, "x"));
    check(tiku_editdoc_length(doc) == length + RUNS, "PERF-001",
          "every key typed went in");
    time_event(v, s, fixture, "down", key(TIKU_KEY_DOWN, 0, NULL));
    time_event(v, s, fixture, "page down", key(TIKU_KEY_PAGE_DOWN, 0, NULL));
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_WHEEL;
    e.dy = -3;
    time_event(v, s, fixture, "wheel up", e);
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_DOC_END, 0);
    time_event(v, s, fixture, "type, end", key('y', 0, "y"));
    time_event(v, s, fixture, "backspace, end",
               key(TIKU_KEY_BACKSPACE, 0, NULL));

    q.text = "zzzz absent";
    q.length = strlen(q.text);
    q.flags = 0;
    start = now_us();
    st = tiku_codeview_find(v, &q, 0, &f);
    once(fixture, "find, no match", now_us() - start);
    check(st == TIKU_EDIT_NOTHING, "PERF-001", "a search for nothing finds "
          "nothing");
    q.text = query;
    q.length = strlen(query);
    length = tiku_editdoc_length(doc);
    start = now_us();
    st = tiku_codeview_replace_all(v, &q, "QQ", 2, &f);
    once(fixture, "replace all", now_us() - start);
    check(st == TIKU_EDIT_OK && f.replaced > 0, "PERF-001",
          "Replace All replaces");
    start = now_us();
    st = tiku_editdoc_undo(doc);
    once(fixture, "undo replace all", now_us() - start);
    check(st == TIKU_EDIT_OK && tiku_editdoc_length(doc) == length,
          "PERF-001", "and its undo restores the length");
    start = now_us();
    tiku_codeview_paint(v, s);
    once(fixture, "paint after undo", now_us() - start);
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
    tiku_surface_free(s);
}

/*---------------------------------------------------------------------------*/
/* Over the desktop transport                                                 */
/*---------------------------------------------------------------------------*/

/* What the child's application opens, and how it colours it. */
static const char *served;
static size_t served_n;
static tiku_syntax_lang_t served_lang;

typedef struct {
    const tiku_app_services_t *svc;
    uint32_t id;
    tiku_surface_t *surface;
    tiku_dl_t *dl;
    tiku_editdoc_t *doc;
    tiku_codeview_t *view;
} host_t;

static void
host_paint(host_t *h)
{
    tiku_dl_clear(h->dl);
    h->surface->record = h->dl;
    tiku_codeview_paint(h->view, h->surface);
    h->surface->record = NULL;
    (void)h->svc->present(h->svc->ctx, h->id, h->dl, h->surface->px,
                          h->surface->w, h->surface->h);
}

static int
host_start(void **state, const tiku_app_services_t *services)
{
    host_t *h = calloc(1, sizeof *h);
    tiku_rect_t box = { 0, 0, 1280, 800 };

    if (h == NULL || tiku_editdoc_create(NULL, &h->doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(h->doc, served, served_n) != TIKU_EDIT_OK ||
        tiku_codeview_create(h->doc, &h->view) != TIKU_EDIT_OK)
        return 1;
    h->svc = services;
    tiku_codeview_set_bounds(h->view, box);
    tiku_codeview_set_options(h->view, TIKU_CODEVIEW_LINE_NUMBERS |
                                       TIKU_CODEVIEW_VSCROLL |
                                       TIKU_CODEVIEW_HSCROLL);
    tiku_codeview_set_language(h->view, served_lang);
    {
        tiku_edit_pos_t mid = tiku_editdoc_line_start(
            h->doc, tiku_editdoc_lines(h->doc) / 2);

        tiku_codeview_select(h->view, mid, mid);
        tiku_codeview_reveal(h->view);
    }
    h->surface = tiku_surface_new(1280, 800, TIKU_C_PANEL);
    h->dl = tiku_dl_new();
    h->id = services->open(services->ctx, "Speed", 1280, 800);
    if (h->surface == NULL || h->dl == NULL || h->id == 0)
        return 1;
    *state = h;
    host_paint(h);
    return 0;
}

static void
host_stop(void *state)
{
    host_t *h = state;

    tiku_codeview_destroy(h->view);
    tiku_editdoc_release(h->doc);
    tiku_surface_free(h->surface);
    tiku_dl_free(h->dl);
    free(h);
}

static int
host_event(void *state, const tiku_event_t *e)
{
    host_t *h = state;

    if (e->type == TIKU_EVENT_KEY_DOWN && (e->modifiers & TIKU_MOD_CMD) &&
        e->key == 'w')
        return 1;
    if (tiku_codeview_event(h->view, e))
        host_paint(h);
    return 0;
}

static const tiku_app_descriptor_t host_app = {
    .id = "test.codeview.speed", .name = "Speed",
    .start = host_start, .stop = host_stop, .event = host_event
};

/** @brief Read until a frame arrives; 0 when the session is gone or none
 *         came in two seconds. */
static int
next_frame(tiku_remote_listener_t *desk)
{
    int64_t until = now_us() + 2000000;

    while (now_us() < until) {
        if (tiku_remote_poll(desk) && desk->session[0].used &&
            tiku_remote_pixels(&desk->session[0].win[0]) != NULL)
            return 1;
        if (!desk->session[0].used)
            return 0;
        (void)tiku_remote_wait(desk, 1000);
    }
    return 0;
}

/** @brief Input-to-frame times through the transport on @p text,
 *         coloured as @p lang. */
static void
transport(const char *fixture, const char *text, size_t n,
          tiku_syntax_lang_t lang)
{
    tiku_remote_listener_t desk;
    tiku_window_t fake;
    int pair[2], status = 0, i, k, got = 1;
    pid_t child;
    int64_t t[RUNS];
    static const unsigned keys[] = { 'x', TIKU_KEY_DOWN, TIKU_KEY_PAGE_DOWN };
    static const char *const names[] = { "type, transport",
                                         "down, transport",
                                         "page down, transport" };

    served = text;
    served_n = n;
    served_lang = lang;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
        exit(1);
    child = fork();
    if (child == 0) {
        close(pair[0]);
        _exit(tiku_client_run_fd(&host_app, pair[1]));
    }
    close(pair[1]);
    memset(&desk, 0, sizeof desk);
    memset(&fake, 0, sizeof fake);
    desk.fd = -1;
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++)
        desk.session[i].fd = -1;
    if (tiku_remote_adopt(&desk, pair[0]) != 0)
        exit(1);
    for (i = 0; i < 5000 && (!desk.session[0].used ||
                             tiku_remote_pixels(&desk.session[0].win[0]) ==
                             NULL); i++) {
        tiku_remote_poll(&desk);
        (void)tiku_remote_wait(&desk, 2000);
    }
    desk.session[0].win[0].window = &fake;
    for (k = 0; k < 3 && got; k++) {
        tiku_event_t e = key(keys[k], 0, k == 0 ? "x" : NULL);

        for (i = 0; i < RUNS && got; i++) {
            int64_t start = now_us();

            tiku_remote_event(&desk, &fake, &e);
            got = next_frame(&desk);
            t[i] = now_us() - start;
        }
        if (got)
            report(fixture, names[k], t, RUNS);
    }
    check(got, "PERF-002", "every input through the transport brought a "
          "frame back");
    {
        tiku_event_t e = key('w', TIKU_MOD_CMD, NULL);

        tiku_remote_event(&desk, &fake, &e);
    }
    for (i = 0; i < 2500 && child != 0; i++) {
        tiku_remote_poll(&desk);
        if (waitpid(child, &status, WNOHANG) == child)
            child = 0;
        else
            usleep(2000);
    }
    if (child != 0) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    tiku_remote_shutdown(&desk);
}

int
main(void)
{
    size_t n;
    char *text;
    struct rusage use;

    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    printf("code view timings, 1280x800, monospaced 12 px, line numbers "
           "and scrollbars\n");
    text = program(&n);
    measure("small", text, n, TIKU_SYNTAX_BASIC, "PRINT");
    transport("small", text, n, TIKU_SYNTAX_BASIC);
    free(text);
    text = source(10u << 20, &n);
    measure("10 MiB", text, n, TIKU_SYNTAX_C, "count");
    transport("10 MiB", text, n, TIKU_SYNTAX_C);
    free(text);
    text = short_lines(100000, &n);
    measure("100k lines", text, n, TIKU_SYNTAX_C, "x1");
    transport("100k lines", text, n, TIKU_SYNTAX_C);
    free(text);
    text = long_line(1u << 20, &n);
    measure("1 MiB line", text, n, TIKU_SYNTAX_C, "word");
    free(text);
    if (getenv("TIKU_CODEVIEW_SPEED_BIG") != NULL) {
        text = source(100u << 20, &n);
        measure("100 MiB", text, n, TIKU_SYNTAX_C, "count");
        free(text);
    } else {
        printf("  100 MiB: set TIKU_CODEVIEW_SPEED_BIG to measure it\n");
    }
    getrusage(RUSAGE_SELF, &use);
    printf("  peak resident %ld KiB\n", use.ru_maxrss);
    printf("%s\n", fails ? "FAIL: code view timings" :
           "PASS: code view timings -- results right on every fixture; "
           "times printed above");
    return fails != 0;
}
