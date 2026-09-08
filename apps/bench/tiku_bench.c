/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bench.c - running the bench's suites against a mounted board.
 *
 * The suites and what they are called come from TikuBench itself, and
 * the run is TikuBench's own command: nothing about building, flashing
 * or talking to a board is repeated here.  What the shell adds is the
 * board -- already mounted, named, and known by its part -- and the
 * hand-over of its port, since a run that reflashes a board cannot
 * share it with the shell that has it open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_list.h"
#include "tiku_list_paint.h"
#include "tiku_ui.h"

#define APP_ID      "tiku.bench"
#define APP_NAME    "Bench"

#define WIN_W       460
#define WIN_H       320
#define RES_W       460
#define RES_H       260
#define MARGIN      8
#define STRIP_H     22

#define SUITES_MAX  160
#define LEAVES_MAX  600
#define HIST_MAX    200
#define ENTRY_MAX   (SUITES_MAX + 64)
#define ROWS_MAX    200
#define BOARDS_MAX  8

#define CMD_RUN      1          /* + i: on the i'th mounted board */
#define CMD_STOP     32
#define CMD_REFRESH  33
#define CMD_OPEN     34

typedef struct {
    char key[48];
    char group[32];
    char name[96];
    char boards[320];           /* the parts it can answer on, or "*" */
    int  first_leaf;            /* into leaf[], -1 when it has none   */
    int  nleaf;
} suite_t;

/**
 * @brief What a suite last did on a board, as the bench's own ledger
 *        remembers it.
 *
 * Only the FULL runs: a part of a suite passing says nothing about the
 * suite, and a result that says less than it seems to is worse than no
 * result at all.
 */
typedef struct {
    char board[40];
    char suite[48];
    char verdict[12];
    long age;                   /* seconds since it was recorded */
} past_t;

/** @brief One of a suite's own sub-tests, which can be run alone. */
typedef struct {
    char id[64];
    char label[120];
} leaf_t;

/**
 * @brief A row of the list: a suite, or one leaf of the open suite.
 *
 * Flat, because a list IS flat: what a row means is carried here rather
 * than worked out again from where it happens to be.
 */
typedef struct {
    int  suite;                 /* into suite[] */
    int  leaf;                  /* into leaf[], -1 for the suite itself */
} entry_t;

typedef struct {
    char text[160];
    int  bad;                   /* a failure, or a run that said nothing */
    int  warn;
} row_t;

typedef struct {
    const tiku_app_services_t *services;
    void                      *unused;

    uint32_t        win_id;
    tiku_surface_t *win;
        tiku_list_t     suites_list;
    suite_t         suite[SUITES_MAX];
    int             nsuite;
    leaf_t          leaf[LEAVES_MAX];
    int             nleaf;
        past_t          past[HIST_MAX];
    int             npast;
    entry_t         entry[ENTRY_MAX];
    int             nentry;
    int             open_suite;     /* whose leaves are shown, or -1 */
    int             hidden;         /* suites no mounted board answers */

    uint32_t        res_id;
    tiku_surface_t *res;
    tiku_list_t     res_list;
    row_t           row[ROWS_MAX];
    int             nrow;

    /* The run: the bench's own process, on a pipe. */
    pid_t           child;
    int             child_fd;
    char            partial[512];
    int             running;
    char            board[64];  /* whose port it is using */
        char            suite_key[48];
    char            only[64];   /* one sub-test, or empty for all of it */
    int             released;   /* the shell let the board go for us */
    int             waiting;    /* released, and the run not started yet */
        char            note[160];
    char            boards_seen[512];   /* to notice one arriving */
} bench_t;

/*---------------------------------------------------------------------------*/
/* WHERE TIKUBENCH IS                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief The TikuBench checkout, from TIKU_BENCH or beside this tree.
 *
 * Asked for rather than guessed at: a bench somebody keeps elsewhere is
 * theirs to name, and a wrong guess would run nothing and say why badly.
 */
static const char *
bench_dir(void)
{
    static char dir[512];
    const char *env = getenv("TIKU_BENCH");
    const char *home;

    if (dir[0] != '\0') {
        return dir;
    }
    if (env != NULL && env[0] != '\0') {
        snprintf(dir, sizeof dir, "%s", env);
        return dir;
    }
    home = getenv("HOME");
    snprintf(dir, sizeof dir, "%s/tikuOS/TikuBench",
             (home != NULL) ? home : ".");
    return dir;
}

/*---------------------------------------------------------------------------*/
/* THE SUITES, FROM THE BENCH ITSELF                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the bench's ledger again, and nothing else.
 *
 * After a run, because the run has just added to it: a window that
 * still says a suite has never run here, moments after running it, is
 * telling somebody something that was true and is not.
 */
static void
past_load(bench_t *st)
{
    char cmd[768];
    char line[1024];
    FILE *f;

    snprintf(cmd, sizeof cmd, "cd '%s' && python3 gui/tb_meta.py 2>/dev/null",
             bench_dir());
    f = popen(cmd, "r");
    if (f == NULL) {
        return;
    }
    st->npast = 0;
    while (fgets(line, sizeof line, f) != NULL && st->npast < HIST_MAX) {
        char *field[7];
        char *p = line;
        int n = 0, i;

        if (line[0] != 'H' || line[1] != '\t') {
            continue;
        }
        while (n < 7) {
            char *tab = strchr(p, '\t');

            field[n++] = p;
            if (tab == NULL) {
                break;
            }
            *tab = '\0';
            p = tab + 1;
        }
        for (i = 0; i < n; i++) {
            char *nl = strchr(field[i], '\n');

            if (nl != NULL) {
                *nl = '\0';
            }
        }
        if (n >= 6 && strcmp(field[3], "full") == 0) {
            past_t *h = &st->past[st->npast++];

            snprintf(h->board, sizeof h->board, "%s", field[1]);
            snprintf(h->suite, sizeof h->suite, "%s", field[2]);
            snprintf(h->verdict, sizeof h->verdict, "%s", field[4]);
            h->age = atol(field[5]);
        }
    }
    (void)pclose(f);
}

/**
 * @brief Read the suite list the bench emits for its own front ends.
 *
 * One record a line, tab separated; the S records are the suites.  The
 * bench owns this list, so a suite added there appears here without
 * anything being written twice.
 */
static void
suites_load(bench_t *st)
{
        char cmd[768];
    char line[1024];
    FILE *f;
    int i;

        st->nsuite = 0;
    st->nleaf = 0;
    st->npast = 0;
    st->open_suite = -1;
    snprintf(cmd, sizeof cmd, "cd '%s' && python3 gui/tb_meta.py 2>/dev/null",
             bench_dir());
    f = popen(cmd, "r");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot ask the bench");
        return;
    }
        while (fgets(line, sizeof line, f) != NULL) {
        char *field[9];
        char *p = line;
        int n = 0;

                if ((line[0] != 'S' && line[0] != 'C' && line[0] != 'H') ||
            line[1] != '\t') {
            continue;
        }
        while (n < 9) {
            char *tab = strchr(p, '\t');

            field[n++] = p;
            if (tab == NULL) {
                break;
            }
            *tab = '\0';
            p = tab + 1;
        }
        for (i = 0; i < n; i++) {
            char *nl = strchr(field[i], '\n');

            if (nl != NULL) {
                *nl = '\0';
            }
        }
                if (line[0] == 'H' && n >= 6 && st->npast < HIST_MAX) {
            /* Only the authoritative kind is kept; the rest would make
             * a partial run look like the suite's verdict. */
            if (strcmp(field[3], "full") == 0) {
                past_t *h = &st->past[st->npast++];

                snprintf(h->board, sizeof h->board, "%s", field[1]);
                snprintf(h->suite, sizeof h->suite, "%s", field[2]);
                snprintf(h->verdict, sizeof h->verdict, "%s", field[4]);
                h->age = atol(field[5]);
            }
            continue;
        }
        if (line[0] == 'S' && n >= 5 && st->nsuite < SUITES_MAX) {
            suite_t *su = &st->suite[st->nsuite++];

            snprintf(su->key, sizeof su->key, "%s", field[1]);
            snprintf(su->group, sizeof su->group, "%s", field[2]);
            snprintf(su->name, sizeof su->name, "%s", field[3]);
            snprintf(su->boards, sizeof su->boards, "%s", field[4]);
            su->first_leaf = -1;
            su->nleaf = 0;
            continue;
        }
        /* A suite's sub-tests follow it, so they belong to the last one
         * read -- which is the order the bench emits them in. */
        if (line[0] == 'C' && n >= 4 && st->nsuite > 0 &&
            st->nleaf < LEAVES_MAX) {
            suite_t *su = &st->suite[st->nsuite - 1];
            leaf_t *lf = &st->leaf[st->nleaf];

            if (strcmp(su->key, field[1]) != 0) {
                continue;
            }
            snprintf(lf->id, sizeof lf->id, "%s", field[2]);
            snprintf(lf->label, sizeof lf->label, "%s", field[3]);
            if (su->first_leaf < 0) {
                su->first_leaf = st->nleaf;
            }
            su->nleaf++;
            st->nleaf++;
        }
    }
    (void)pclose(f);
    if (st->nsuite == 0) {
        snprintf(st->note, sizeof st->note, "no suites: is TIKU_BENCH set?");
    }
}

/*---------------------------------------------------------------------------*/
/* DRAWING                                                                   */
/*---------------------------------------------------------------------------*/

static int board_at(bench_t *st, int which, char *out, size_t max);

/** @brief Whether @p csv, a bench board list, names @p part. */
static int
board_listed(const char *csv, const char *part)
{
    const char *at;
    size_t n;

    if (csv[0] == '*' || part[0] == '\0') {
        return 1;               /* every board, or nothing to go on */
    }
    n = strlen(part);
    for (at = csv; (at = strstr(at, part)) != NULL; at += n) {
        char before = (at == csv) ? ',' : at[-1];
        char after = at[n];

        if ((before == ',' || before == '\0') &&
            (after == ',' || after == '\0')) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Whether any board this shell has mounted can answer @p su.
 *
 * A bench with ninety suites and one board on the desk is a list of
 * things that cannot be done; what is left out is counted and said,
 * rather than quietly dropped.
 */
static int
any_board_answers(bench_t *st, const suite_t *su)
{
    char name[64];
    int i;

    for (i = 0; i < BOARDS_MAX && board_at(st, i, name, sizeof name); i++) {
        char part[64];
        char *p;

        part[0] = '\0';
        if (st->services->board_mcu != NULL) {
            (void)st->services->board_mcu(st->services->ctx, name, part,
                                          sizeof part);
        }
        for (p = part; *p != '\0'; p++) {
            if (*p >= 'A' && *p <= 'Z') {
                *p = (char)(*p - 'A' + 'a');
            }
        }
        if (board_listed(su->boards, part)) {
            return 1;
        }
    }
    return (i == 0);            /* no board yet: nothing to rule out */
}

/** @brief Build the rows: the suites on offer, and the open one's parts. */
static void
entries_build(bench_t *st)
{
    int i, j;

    st->nentry = 0;
    st->hidden = 0;
    for (i = 0; i < st->nsuite && st->nentry < ENTRY_MAX; i++) {
        if (!any_board_answers(st, &st->suite[i])) {
            st->hidden++;
            continue;
        }
        st->entry[st->nentry].suite = i;
        st->entry[st->nentry].leaf = -1;
        st->nentry++;
        if (i != st->open_suite) {
            continue;
        }
        for (j = 0; j < st->suite[i].nleaf && st->nentry < ENTRY_MAX; j++) {
            st->entry[st->nentry].suite = i;
            st->entry[st->nentry].leaf = st->suite[i].first_leaf + j;
            st->nentry++;
        }
    }
    (void)tiku_list_set_count(&st->suites_list, st->nentry);
}

static const char *
suite_name(void *ctx, int row)
{
    bench_t *st = ctx;
    static char text[200];
    const entry_t *e;

    if (row < 0 || row >= st->nentry) {
        return NULL;
    }
    e = &st->entry[row];
    if (e->leaf >= 0) {
        snprintf(text, sizeof text, "      %s  %s", st->leaf[e->leaf].id,
                 st->leaf[e->leaf].label);
        return text;
    }
    snprintf(text, sizeof text, "%s %s  %s",
             (st->suite[e->suite].nleaf > 0)
                 ? ((e->suite == st->open_suite) ? "\xe2\x96\xbe"
                                                 : "\xe2\x96\xb8")
                 : " ",
             st->suite[e->suite].group, st->suite[e->suite].name);
    return text;
}

static const char *
row_name(void *ctx, int row)
{
    bench_t *st = ctx;

    return (row >= 0 && row < st->nrow) ? st->row[row].text : NULL;
}

static tiku_rect_t
suites_body(void)
{
    tiku_rect_t r = { MARGIN, MARGIN, WIN_W - 2 * MARGIN,
                      WIN_H - STRIP_H - 2 * MARGIN };
    return r;
}

static void
bench_paint(bench_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, WIN_W, WIN_H };
    tiku_rect_t strip = { 0, WIN_H - STRIP_H, WIN_W, STRIP_H };

    if (st->win == NULL) {
        return;
    }
    tiku_fill(st->win, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->win, suites_body(), TIKU_C_DOC);
    tiku_list_draw(&st->suites_list, st->win, suites_body(), suite_name, st);
    tiku_fill(st->win, strip, TIKU_C_PANEL);
    tiku_text(st->win, f, MARGIN,
              strip.y + (STRIP_H - f->height) / 2 + f->ascent,
              st->note, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->win_id, st->win->px,
                              WIN_W, WIN_H);
}

static tiku_rect_t
res_body(void)
{
    tiku_rect_t r = { MARGIN, MARGIN, RES_W - 2 * MARGIN,
                      RES_H - 2 * MARGIN };
    return r;
}

static void
res_paint(bench_t *st)
{
    if (st->res == NULL) {
        return;
    }
    tiku_fill(st->res, (tiku_rect_t){ 0, 0, RES_W, RES_H }, TIKU_C_PANEL);
    tiku_ui_sunken(st->res, res_body(), TIKU_C_DOC);
    tiku_list_draw(&st->res_list, st->res, res_body(), row_name, st);
    (void)st->services->frame(st->services->ctx, st->res_id, st->res->px,
                              RES_W, RES_H);
}

/** @brief The results window, made when there is first something in it. */
static void
res_open(bench_t *st)
{
    if (st->res != NULL) {
        return;
    }
    st->res = tiku_surface_new(RES_W, RES_H, TIKU_C_PANEL);
    if (st->res == NULL) {
        return;
    }
    st->res_id = st->services->open(st->services->ctx, "Results", RES_W,
                                    RES_H);
    if (st->res_id == 0u) {
        tiku_surface_free(st->res);
        st->res = NULL;
        return;
    }
    tiku_list_init(&st->res_list, 1);
}

static void
row_take(bench_t *st, const char *text, int bad, int warn)
{
    row_t *r;

    if (st->nrow >= ROWS_MAX) {
        return;
    }
    r = &st->row[st->nrow++];
    snprintf(r->text, sizeof r->text, "%s", text);
    r->bad = bad;
    r->warn = warn;
    res_open(st);
    (void)tiku_list_set_count(&st->res_list, st->nrow);
    res_paint(st);
}

/*---------------------------------------------------------------------------*/
/* MENUS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief The @p which'th mounted board, into @p out. */
static int
board_at(bench_t *st, int which, char *out, size_t max)
{
    char boards[512];
    const char *p;
    int i = 0;

    out[0] = '\0';
    if (st->services->board_names == NULL ||
        st->services->board_names(st->services->ctx, boards,
                                  sizeof boards) <= 0) {
        return 0;
    }
    for (p = boards; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (n > 0u) {
            if (i == which) {
                if (n >= max) {
                    n = max - 1u;
                }
                memcpy(out, p, n);
                out[n] = '\0';
                return 1;
            }
            i++;
        }
        p = (nl != NULL) ? nl + 1 : p + strlen(p);
    }
    return 0;
}

/** @brief The part a board is, in the bench's spelling, into @p out. */
static void
board_part(bench_t *st, const char *name, char *out, size_t max)
{
    char *p;

    out[0] = '\0';
    if (st->services->board_mcu != NULL) {
        (void)st->services->board_mcu(st->services->ctx, name, out, max);
    }
    for (p = out; *p != '\0'; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
}

/** @brief How long ago @p secs was, in the largest unit that fits. */
static void
how_long(long secs, char *out, size_t max)
{
    if (secs < 90L) {
        snprintf(out, max, "just now");
    } else if (secs < 5400L) {
        snprintf(out, max, "%ld minutes ago", secs / 60L);
    } else if (secs < 172800L) {
        snprintf(out, max, "%ld hours ago", secs / 3600L);
    } else {
        snprintf(out, max, "%ld days ago", secs / 86400L);
    }
}

/**
 * @brief What the chosen suite last did on a board that is here.
 *
 * The bench keeps this; the shell only says it, at the moment somebody
 * is deciding whether to run the thing again.  A suite that has never
 * run here says so, which is a different answer from a pass.
 */
static void
past_line(bench_t *st, int chosen, char *out, size_t max)
{
    char name[64], part[64], ago[40];
    int i, j;

    out[0] = '\0';
    if (chosen < 0 || chosen >= st->nentry || st->entry[chosen].leaf >= 0) {
        return;
    }
    for (i = 0; i < BOARDS_MAX && board_at(st, i, name, sizeof name); i++) {
        board_part(st, name, part, sizeof part);
        for (j = 0; j < st->npast; j++) {
            if (strcmp(st->past[j].board, part) != 0 ||
                strcmp(st->past[j].suite,
                       st->suite[st->entry[chosen].suite].key) != 0) {
                continue;
            }
            how_long(st->past[j].age, ago, sizeof ago);
            snprintf(out, max, "%s on %s: %s, %s",
                     st->suite[st->entry[chosen].suite].key, name,
                     (strcmp(st->past[j].verdict, "pass") == 0) ? "passed"
                                                                : "FAILED",
                     ago);
            return;
        }
    }
    if (i > 0) {
        snprintf(out, max, "%s has not run here",
                 st->suite[st->entry[chosen].suite].key);
    }
}

static void
bench_publish(bench_t *st)
{
    tiku_menuset_t menus;
    char name[64];
    int at = 0, i;
    int chosen = tiku_list_chosen(&st->suites_list);
    int can = (chosen >= 0 && chosen < st->nsuite && !st->running);

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Bench");
    /* One row per mounted board: which board a suite is run against is
     * the whole question, and the shell already knows them by name. */
    for (i = 0; i < BOARDS_MAX && board_at(st, i, name, sizeof name); i++) {
        snprintf(menus.menu[0].item[at].label,
                 sizeof menus.menu[0].item[at].label, "Run on %s", name);
        menus.menu[0].item[at].command = CMD_RUN + i;
        menus.menu[0].item[at].enabled = (unsigned char)can;
        at++;
    }
    if (i == 0) {
        snprintf(menus.menu[0].item[at].label,
                 sizeof menus.menu[0].item[at].label, "no board is mounted");
        menus.menu[0].item[at].command = CMD_REFRESH;
        menus.menu[0].item[at].enabled = 0;
        at++;
    }
            {
        /* What it did last time, where somebody is deciding whether to
         * do it again.  Said, not acted on: it is the bench's record. */
        char was[160];

        past_line(st, chosen, was, sizeof was);
        if (was[0] != '\0') {
            snprintf(menus.menu[0].item[at].label,
                     sizeof menus.menu[0].item[at].label, "%s", was);
            menus.menu[0].item[at].command = CMD_REFRESH;
            menus.menu[0].item[at].enabled = 0;
            at++;
        }
    }
    if (chosen >= 0 && chosen < st->nentry &&
        st->entry[chosen].leaf < 0 &&
        st->suite[st->entry[chosen].suite].nleaf > 0) {
        /* A suite is a set of checks, and sometimes one of them is the
         * question: opening it offers them one at a time. */
        snprintf(menus.menu[0].item[at].label,
                 sizeof menus.menu[0].item[at].label, "%s the parts of %s",
                 (st->entry[chosen].suite == st->open_suite) ? "Close"
                                                             : "Show",
                 st->suite[st->entry[chosen].suite].key);
        menus.menu[0].item[at].command = CMD_OPEN;
        menus.menu[0].item[at].enabled = 1;
        at++;
    }
    snprintf(menus.menu[0].item[at].label,
             sizeof menus.menu[0].item[at].label, "Stop");
    menus.menu[0].item[at].command = CMD_STOP;
    menus.menu[0].item[at].enabled = (unsigned char)st->running;
    at++;
    snprintf(menus.menu[0].item[at].label,
             sizeof menus.menu[0].item[at].label, "Read the suites again");
    menus.menu[0].item[at].command = CMD_REFRESH;
    menus.menu[0].item[at].enabled = (unsigned char)!st->running;
    at++;
    menus.menu[0].nitem = at;
    (void)st->services->menus(st->services->ctx, st->win_id, &menus);
}

/*---------------------------------------------------------------------------*/
/* THE RUN                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Spawn the bench's own command for the chosen suite and board.
 *
 * The port is passed because the shell has just let go of it; the part
 * is what the board says it is.  Nothing here builds or flashes: that
 * is the bench's, and this is only where it was asked from.
 */
static void
run_spawn(bench_t *st)
{
        char port[160], mcu[64], key[48], only[64];
    char *lower;
    int fd[2];
    int i;

    port[0] = '\0';
    mcu[0] = '\0';
    if (st->services->board_port != NULL) {
        (void)st->services->board_port(st->services->ctx, st->board, port,
                                       sizeof port);
    }
    if (st->services->board_mcu != NULL) {
        (void)st->services->board_mcu(st->services->ctx, st->board, mcu,
                                      sizeof mcu);
    }
    /* The bench names parts in lower case ("nrf54lm20b"); a board says
     * what it is in its own ("nRF54LM20B"). */
    for (lower = mcu; *lower != '\0'; lower++) {
        if (*lower >= 'A' && *lower <= 'Z') {
            *lower = (char)(*lower - 'A' + 'a');
        }
    }
        snprintf(key, sizeof key, "%s", st->suite_key);
    snprintf(only, sizeof only, "%s", st->only);
    if (pipe(fd) != 0) {
        snprintf(st->note, sizeof st->note, "no pipe for the run");
        bench_paint(st);
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fd[0]);
        (void)close(fd[1]);
        snprintf(st->note, sizeof st->note, "the run would not start");
        bench_paint(st);
        return;
    }
    if (st->child == 0) {
        char *argv[16];   /* every word below, and the NUL end */
        int n = 0;

        (void)dup2(fd[1], 1);
        (void)dup2(fd[1], 2);
        (void)close(fd[0]);
        (void)close(fd[1]);
        if (chdir(bench_dir()) != 0) {
            _exit(127);
        }
        argv[n++] = (char *)"python3";
        argv[n++] = (char *)"-u";
        argv[n++] = (char *)"-m";
        argv[n++] = (char *)"tikubench";
        argv[n++] = (char *)"run";
        argv[n++] = key;
                argv[n++] = (char *)"--board";
        argv[n++] = mcu;
        if (port[0] != '\0') {
            argv[n++] = (char *)"--port";
            argv[n++] = port;
        }
        if (only[0] != '\0') {
            argv[n++] = (char *)"--only";
            argv[n++] = only;
        }
        argv[n] = NULL;
        (void)execvp("python3", argv);
        _exit(127);
    }
    (void)close(fd[1]);
    st->child_fd = fd[0];
    (void)fcntl(st->child_fd, F_SETFL, O_NONBLOCK);
    st->partial[0] = '\0';
    st->running = 1;
    st->waiting = 0;
    for (i = 0; i < 1; i++) {
        (void)i;
    }
        if (only[0] != '\0') {
        snprintf(st->note, sizeof st->note, "%s %s on %s…", key, only,
                 st->board);
    } else {
        snprintf(st->note, sizeof st->note, "%s on %s…", key, st->board);
    }
    bench_paint(st);
    bench_publish(st);
}

/** @brief Give the board back and put the run away. */
static void
run_finish(bench_t *st, const char *why)
{
    if (st->child_fd >= 0) {
        (void)close(st->child_fd);
        st->child_fd = -1;
    }
    if (st->child > 0) {
        (void)waitpid(st->child, NULL, WNOHANG);
        st->child = 0;
    }
    st->running = 0;
    st->waiting = 0;
    if (st->released && st->services->board_reclaim != NULL) {
        /* The board goes back to the shell whatever happened: a bench
         * run that ends badly must not cost the desk its board. */
        (void)st->services->board_reclaim(st->services->ctx, st->board);
    }
        /* The ledger has just been added to, so what the window says about
     * this suite is read again rather than left as it was. */
    past_load(st);
    snprintf(st->note, sizeof st->note, "%s", why);
    bench_paint(st);
    bench_publish(st);
}

/** @brief Take one line the bench said. */
static void
run_line(bench_t *st, const char *line)
{
    char text[160];
    unsigned pass = 0u, fail = 0u;
    char id[96];

    if (sscanf(line, "[TC:END] %95s pass=%u fail=%u", id, &pass, &fail) == 3) {
        const char *mark = (fail > 0u) ? "\xe2\x9c\x97"
                         : (pass == 0u) ? "?" : "\xe2\x9c\x93";

        snprintf(text, sizeof text, "%s %s  %u/%u", mark, id, pass,
                 pass + fail);
        row_take(st, text, fail > 0u, pass == 0u && fail == 0u);
        return;
    }
    if (strncmp(line, "[TC:RUN] ", 9) == 0) {
        snprintf(st->note, sizeof st->note, "%s", line + 9);
        bench_paint(st);
        return;
    }
    if (strncmp(line, "[BUILD]", 7) == 0 || strncmp(line, "[FLASH]", 7) == 0 ||
        strncmp(line, "[MONITOR]", 9) == 0) {
        snprintf(st->note, sizeof st->note, "%s", line);
        bench_paint(st);
        return;
    }
    if (strncmp(line, "[WARN]", 6) == 0) {
        row_take(st, line, 0, 1);
        return;
    }
    if (strncmp(line, "[FAIL]", 6) == 0) {
        row_take(st, line, 1, 0);
    }
}

/** @brief Everything the bench has said since the last look. */
static void
run_read(bench_t *st)
{
    char buf[1024];
    ssize_t n;

    if (st->child_fd < 0) {
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                      errno == EINTR)) {
            return;             /* nothing now; the run goes on */
        }
        if (n <= 0) {
            break;              /* a closed pipe is a finished run */
        }
        buf[n] = '\0';
        {
            char *p = buf;

            while (*p != '\0') {
                char *nl = strchr(p, '\n');

                if (nl == NULL) {
                    size_t have = strlen(st->partial);

                    snprintf(st->partial + have, sizeof st->partial - have,
                             "%s", p);
                    break;
                }
                *nl = '\0';
                if (st->partial[0] != '\0') {
                    char whole[672];

                    snprintf(whole, sizeof whole, "%s%s", st->partial, p);
                    st->partial[0] = '\0';
                    run_line(st, whole);
                } else {
                    run_line(st, p);
                }
                p = nl + 1;
            }
        }
    }
    {
        int bad = 0, i;

        for (i = 0; i < st->nrow; i++) {
            if (st->row[i].bad) {
                bad++;
            }
        }
        run_finish(st, (st->nrow == 0) ? "the run said nothing"
                                       : (bad > 0 ? "finished, with failures"
                                                  : "finished"));
    }
}

/*---------------------------------------------------------------------------*/
/* THE APPLICATION                                                           */
/*---------------------------------------------------------------------------*/

static int
bench_start(void **state, const tiku_app_services_t *services)
{
    bench_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    *state = st;
    st->services = services;
    st->child_fd = -1;
    st->win = tiku_surface_new(WIN_W, WIN_H, TIKU_C_PANEL);
    if (st->win == NULL) {
        free(st);
        *state = NULL;
        return -1;
    }
    st->win_id = services->open(services->ctx, APP_NAME, WIN_W, WIN_H);
    if (st->win_id == 0u) {
        tiku_surface_free(st->win);
        free(st);
        *state = NULL;
        return -1;
    }
    tiku_list_init(&st->suites_list, 1);
    snprintf(st->note, sizeof st->note, "reading the bench's suites…");
    suites_load(st);
        entries_build(st);
    if (st->nsuite > 0) {
        snprintf(st->note, sizeof st->note,
                 "%d suites; %d this desk's boards cannot answer",
                 st->nentry, st->hidden);
    }
    bench_paint(st);
    bench_publish(st);
    return 0;
}

static void
bench_stop(void *state)
{
    bench_t *st = state;

    if (st == NULL) {
        return;
    }
    if (st->running) {
        if (st->child > 0) {
            (void)kill(st->child, SIGINT);
        }
        run_finish(st, "stopped");
    }
    if (st->res != NULL) {
        tiku_surface_free(st->res);
    }
    tiku_surface_free(st->win);
    free(st);
}

static void
bench_tick(void *state, int64_t now_us)
{
    bench_t *st = state;
    char boards[512];

    (void)now_us;
    if (st == NULL) {
        return;
    }
    /* A board is mounted when its cable is found, which may be after
     * this window was made: the offer to run on it follows the shell's
     * list rather than whatever it held at startup. */
    boards[0] = '\0';
    if (st->services->board_names != NULL) {
        (void)st->services->board_names(st->services->ctx, boards,
                                        sizeof boards);
    }
            if (strcmp(boards, st->boards_seen) != 0) {
        snprintf(st->boards_seen, sizeof st->boards_seen, "%s", boards);
        /* What is worth offering changes with the boards: a suite no
         * board here can answer is not on the list.  Not DURING a run,
         * though: the board is away because this application asked for
         * it, and a list that reshuffles under the row somebody chose
         * is the one thing worse than a stale one. */
        if (!st->running) {
            entries_build(st);
        }
        bench_paint(st);
        bench_publish(st);
    }
    if (st->running) {
        run_read(st);
    }
}

/**
 * @brief What the shell says about the board a run asked for.
 *
 * The run starts only once the board is REALLY let go: asking for a
 * port the shell still holds is how two owners end up on one cable.
 */
static void
bench_board_said(void *state, int what, const char *text)
{
    bench_t *st = state;

    (void)text;
    if (st == NULL) {
        return;
    }
    if (what == TIKU_APP_BOARD_RELEASED && st->waiting) {
        st->released = 1;
        run_spawn(st);
        return;
    }
    if (what == TIKU_APP_BOARD_RECLAIMED) {
        st->released = 0;
        bench_publish(st);
        return;
    }
    if (what == TIKU_APP_BOARD_FAILED && st->waiting) {
        st->waiting = 0;
        run_finish(st, "the shell would not let the board go");
    }
}

static int
bench_pick(void *state, uint32_t window, int command)
{
    bench_t *st = state;

    (void)window;
    if (st == NULL) {
        return 0;
    }
            if (command == CMD_OPEN) {
        int row = tiku_list_chosen(&st->suites_list);

        if (row >= 0 && row < st->nentry && st->entry[row].leaf < 0) {
            int which = st->entry[row].suite;

            st->open_suite = (st->open_suite == which) ? -1 : which;
            entries_build(st);
            tiku_list_click(&st->suites_list, row, 0u);
            bench_paint(st);
            bench_publish(st);
        }
        return 0;
    }
    if (command == CMD_REFRESH) {
        suites_load(st);
        entries_build(st);
        bench_paint(st);
        return 0;
    }
    if (command == CMD_STOP) {
        if (st->running && st->child > 0) {
            (void)kill(st->child, SIGINT);
        }
        return 0;
    }
        if (command >= CMD_RUN && command < CMD_RUN + BOARDS_MAX) {
        int chosen = tiku_list_chosen(&st->suites_list);
        const entry_t *e;

        if (st->running || chosen < 0 || chosen >= st->nentry) {
            return 0;
        }
        if (!board_at(st, command - CMD_RUN, st->board, sizeof st->board)) {
            return 0;
        }
        e = &st->entry[chosen];
        snprintf(st->suite_key, sizeof st->suite_key, "%s",
                 st->suite[e->suite].key);
        /* A part of a suite is run alone when a part is what was
         * chosen; the whole of it when the suite's own row was. */
        snprintf(st->only, sizeof st->only, "%s",
                 (e->leaf >= 0) ? st->leaf[e->leaf].id : "");
        st->nrow = 0;
        (void)tiku_list_set_count(&st->res_list, 0);
        /* The shell has the board open; a run that reflashes it needs
         * the port to itself, so it is asked for and waited for. */
        if (st->services->board_release == NULL ||
            !st->services->board_release(st->services->ctx, st->board)) {
            run_finish(st, "cannot ask for the board");
            return 0;
        }
        st->waiting = 1;
        st->running = 1;
        snprintf(st->note, sizeof st->note, "waiting for %s…", st->board);
        bench_paint(st);
        bench_publish(st);
    }
    return 0;
}

static int
bench_event(void *state, uint32_t window, const tiku_event_t *event)
{
    bench_t *st = state;

    if (st == NULL || event == NULL) {
        return 0;
    }
    if (window == st->res_id && st->res != NULL) {
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_list_click(&st->res_list,
                            tiku_list_at(&st->res_list, res_body(), event->x,
                                         event->y), event->modifiers);
            res_paint(st);
        } else if (tiku_list_key(&st->res_list, event->key, event->modifiers,
                                 res_body())) {
            res_paint(st);
        }
        return 0;
    }
    if (window != st->win_id) {
        return 0;
    }
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
        int was = tiku_list_chosen(&st->suites_list);
        int row = tiku_list_at(&st->suites_list, suites_body(), event->x,
                               event->y);

        tiku_list_click(&st->suites_list, row, event->modifiers);
        /* The second click on a suite opens it, exactly as it opens a
         * row anywhere else on this desk. */
        if (row >= 0 && row == was && row < st->nentry &&
            st->entry[row].leaf < 0 && st->suite[st->entry[row].suite].nleaf
            > 0) {
            int which = st->entry[row].suite;

            st->open_suite = (st->open_suite == which) ? -1 : which;
            entries_build(st);
            tiku_list_click(&st->suites_list, row, 0u);
        }
        bench_paint(st);
        bench_publish(st);
        return 0;
    }
    if (event->key == TIKU_KEY_RETURN) {
        int row = tiku_list_chosen(&st->suites_list);

        if (row >= 0 && row < st->nentry && st->entry[row].leaf < 0) {
            int which = st->entry[row].suite;

            st->open_suite = (st->open_suite == which) ? -1 : which;
            entries_build(st);
            tiku_list_click(&st->suites_list, row, 0u);
            bench_paint(st);
            bench_publish(st);
        }
        return 0;
    }
    if (tiku_list_key(&st->suites_list, event->key, event->modifiers,
                      suites_body())) {
        bench_paint(st);
        bench_publish(st);
    }
    return 0;
}

static void
bench_closed(void *state, uint32_t window)
{
    bench_t *st = state;

    if (st != NULL && window == st->res_id) {
        st->res_id = 0u;
        if (st->res != NULL) {
            tiku_surface_free(st->res);
            st->res = NULL;
        }
    }
}

const tiku_app_descriptor_t tiku_bench_app = {
    .id = APP_ID,
    .name = APP_NAME,
    .start = bench_start,
    .stop = bench_stop,
    .tick = bench_tick,
    .pick = bench_pick,
    .event_in = bench_event,
    .closed = bench_closed,
    .board_said = bench_board_said
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_bench_app
};
#endif
