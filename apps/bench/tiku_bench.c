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
#define ROWS_MAX    200
#define BOARDS_MAX  8

#define CMD_RUN      1          /* + i: on the i'th mounted board */
#define CMD_STOP     32
#define CMD_REFRESH  33

typedef struct {
    char key[48];
    char group[32];
    char name[96];
} suite_t;

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
    char line[512];
    FILE *f;

    st->nsuite = 0;
    snprintf(cmd, sizeof cmd, "cd '%s' && python3 gui/tb_meta.py 2>/dev/null",
             bench_dir());
    f = popen(cmd, "r");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot ask the bench");
        return;
    }
    while (fgets(line, sizeof line, f) != NULL && st->nsuite < SUITES_MAX) {
        char *field[6];
        char *p = line;
        int n = 0;

        if (line[0] != 'S' || line[1] != '\t') {
            continue;
        }
        while (n < 6) {
            char *tab = strchr(p, '\t');

            field[n++] = p;
            if (tab == NULL) {
                break;
            }
            *tab = '\0';
            p = tab + 1;
        }
        if (n < 4) {
            continue;
        }
        {
            suite_t *s = &st->suite[st->nsuite++];
            char *nl;

            snprintf(s->key, sizeof s->key, "%s", field[1]);
            snprintf(s->group, sizeof s->group, "%s", field[2]);
            snprintf(s->name, sizeof s->name, "%s", field[3]);
            nl = strchr(s->name, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }
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

static const char *
suite_name(void *ctx, int row)
{
    bench_t *st = ctx;
    static char text[160];

    if (row < 0 || row >= st->nsuite) {
        return NULL;
    }
    snprintf(text, sizeof text, "%s  %s", st->suite[row].group,
             st->suite[row].name);
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
    char port[160], mcu[64], key[48];
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
        char *argv[12];
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
    snprintf(st->note, sizeof st->note, "%s on %s…", key, st->board);
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
    if (st->nsuite > 0) {
        snprintf(st->note, sizeof st->note, "%d suites", st->nsuite);
    }
    (void)tiku_list_set_count(&st->suites_list, st->nsuite);
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
    if (command == CMD_REFRESH) {
        suites_load(st);
        (void)tiku_list_set_count(&st->suites_list, st->nsuite);
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

        if (st->running || chosen < 0 || chosen >= st->nsuite) {
            return 0;
        }
        if (!board_at(st, command - CMD_RUN, st->board, sizeof st->board)) {
            return 0;
        }
        snprintf(st->suite_key, sizeof st->suite_key, "%s",
                 st->suite[chosen].key);
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
        tiku_list_click(&st->suites_list,
                        tiku_list_at(&st->suites_list, suites_body(),
                                     event->x, event->y), event->modifiers);
        bench_paint(st);
        bench_publish(st);
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
