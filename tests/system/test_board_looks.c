/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_board_looks.c - looks for boards, and a board's windows.
 *
 * The port scan is replaced, through the linker's --wrap, by a list of
 * ports that do not exist, and the workers by a queue the test lands by
 * hand: a probe that lands with nothing found is a port with no board.  A
 * connect from the Devices menu during a look, or one counted at startup
 * that lands after the startup wait, must not end the look; every
 * hosted application that asks during a look must be told when the next
 * one ends; a board reclaimed with a /gui, and a windows-only board (a
 * socket pair, through a --wrap of the port open) connected from the
 * Devices menu, must have its channel taken into the remote listener.
 * Off Linux, where the link has no --wrap, the checks are skipped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "../../system/desktop/tiku_trk_desktop_private.h"
#include "tiku_channel.h"
#include "tiku_link_tcp.h"
#include "tiku_mount.h"
#include "tiku_tx.h"
#include "tiku_work.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

/*---------------------------------------------------------------------------*/
/* THE PORTS AND THE WORKERS, BY HAND                                        */
/*---------------------------------------------------------------------------*/

static int nports;              /* how many ports the scan finds */

/* The windows-only board's port; declared on every host, since its test
 * closes it wherever it is compiled. */
static int win_fd[2] = { -1, -1 };

#ifdef __linux__
/** @brief The scan: ports /dev/ttyLOOK0 up to nports, none of them real. */
int
__wrap_tiku_tx_scan_serial(char out[][256], int max)
{
    int i;

    for (i = 0; i < nports && i < max; i++) {
        snprintf(out[i], 256, "/dev/ttyLOOK%d", i);
    }
    return i;
}

/** @brief A channel on the port of a board with windows and no shell: a
 *         link over one end of a socket pair. */
tiku_channel_t *
__wrap_tiku_channel_open_port(const char *port, int baud)
{
    (void)port;
    (void)baud;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, win_fd) != 0) {
        return NULL;
    }
    return tiku_channel_adopt(tiku_link_tcp_open(win_fd[0]));
}
#endif

#define JOBS 32

static tiku_work_job_t jobs[JOBS];
static int landed[JOBS];
static int njobs;

int
tiku_work_post(const tiku_work_job_t *job)
{
    if (njobs >= JOBS || job->run == NULL) {
        return -1;
    }
    jobs[njobs] = *job;
    landed[njobs] = 0;
    njobs++;
    return 0;
}

/** @brief A slow job joins the same queue. */
int
tiku_work_post_slow(const tiku_work_job_t *job)
{
    return tiku_work_post(job);
}

int tiku_work_cancel(const char *key) { (void)key; return 0; }
int tiku_work_collect(void) { return 0; }
int tiku_work_cancelled(const void *ctx) { (void)ctx; return 0; }
int tiku_work_busy(void) { return njobs; }

/** @brief Job @p i lands on the loop without running: its port had no
 *         board on it. */
static void
land(int i)
{
    if (i < njobs && !landed[i]) {
        landed[i] = 1;
        jobs[i].done(jobs[i].ctx);
        jobs[i].free(jobs[i].ctx);
    }
}

/** @brief Free every job not landed, and empty the queue. */
static void
drop_jobs(void)
{
    int i;

    for (i = 0; i < njobs; i++) {
        if (!landed[i] && jobs[i].free != NULL) {
            jobs[i].free(jobs[i].ctx);
        }
    }
    njobs = 0;
}

/*---------------------------------------------------------------------------*/
/* THE HOSTED APPLICATIONS                                                   */
/*---------------------------------------------------------------------------*/

typedef struct {
    int looked;                 /* how many LOOKED it was told */
    int reclaimed;              /* how many RECLAIMED it was told */
} told_t;

static told_t told[3];

static void
said(void *state, int what, const char *text)
{
    told_t *t = state;

    (void)text;
    t->looked += (what == TIKU_APP_BOARD_LOOKED);
    t->reclaimed += (what == TIKU_APP_BOARD_RECLAIMED);
}

static tiku_app_descriptor_t asker_app;

/** @brief Three hosted applications that can be told about boards. */
static void
hosted(desktop_t *app)
{
    int e;

    asker_app.board_said = said;
    for (e = 0; e < 3; e++) {
        memset(&told[e], 0, sizeof told[e]);
        app->embeds[e].app = app;
        app->embeds[e].running = 1;
        app->embeds[e].descriptor = &asker_app;
        app->embeds[e].state = &told[e];
    }
}

/*---------------------------------------------------------------------------*/
/* THE CHECKS                                                                */
/*---------------------------------------------------------------------------*/

/** @brief A connect chosen from the Devices menu while a look runs. */
static void
the_connect_during_a_look(desktop_t *app)
{
    printf("a Devices menu connect during a look (D-050)\n");
    hosted(app);
    nports = 2;
    desktop_board_look_begin(app, -1, &app->embeds[0]);
    check(app->look.going && njobs == 2, "a look asks two ports");
    check(device_connect(app, "/dev/ttyMENU") == 0 && njobs == 3,
          "a connect is chosen from the Devices menu");
    land(2);
    land(0);
    printf("  (the menu's connect and one port answered: pending %d, "
           "LOOKED told %d)\n", app->look.pending, told[0].looked);
    check(app->look.going && told[0].looked == 0,
          "the look runs on while one of its ports has not answered");
    land(1);
    check(!app->look.going && told[0].looked == 1,
          "it ends, and its asker is told once, when its last port answers");
    drop_jobs();
}

/** @brief A startup connect, counted while startup waited for it, lands
 *         after the wait gave up and a look began. */
static void
the_late_startup_connect(desktop_t *app)
{
    printf("a startup connect that lands during the first look\n");
    hosted(app);
    nports = 2;
    app->look.going = 1;
    app->look.gen++;
    app->look.pending = 0;
    if (desktop_wire_probe(app, DESKTOP_ASK_CONNECT, "/dev/ttyBOOT", 115200,
                           0, NULL, 1) == 0) {
        app->look.pending++;
    }
    app->look.going = 0;
    desktop_board_look_begin(app, -1, &app->embeds[0]);
    check(njobs == 3 && app->look.pending == 2,
          "the startup wait gave up, and a look asks two ports");
    land(0);
    land(1);
    check(app->look.going && told[0].looked == 0,
          "the late connect does not count toward the look");
    land(2);
    check(!app->look.going && told[0].looked == 1,
          "the look ends when its own last port answers");
    drop_jobs();
}

/** @brief Two applications and the Scan row ask while a look runs. */
static void
the_askers_during_a_look(desktop_t *app)
{
    printf("askers waiting for the look after this one (D-051)\n");
    hosted(app);
    nports = 2;
    desktop_board_look_begin(app, -1, &app->embeds[0]);
    desktop_board_look_begin(app, -1, &app->embeds[1]);
    desktop_board_look_begin(app, -1, NULL);
    desktop_board_look_begin(app, -1, &app->embeds[2]);
    check(njobs == 2, "one look runs; the three asking after wait");
    land(0);
    land(1);
    check(told[0].looked == 1 && told[1].looked == 0 && told[2].looked == 0,
          "the first look tells its asker alone");
    check(app->look.going && njobs == 4,
          "a second look starts when the first ends");
    land(2);
    land(3);
    printf("  (LOOKED told: %d, %d, %d)\n", told[0].looked, told[1].looked,
           told[2].looked);
    check(!app->look.going && told[0].looked == 1 && told[1].looked == 1 &&
          told[2].looked == 1,
          "the second look tells both applications that asked during the "
          "first, once each");
    drop_jobs();
}

/*---------------------------------------------------------------------------*/
/* THE FAKE BOARD                                                            */
/*---------------------------------------------------------------------------*/

#define FAKE_UID "UID-FAKE-1"

static char   line[1024];
static size_t line_len;
static int    board_fd = -1;

static void
say(const char *text)
{
    size_t n = strlen(text);

    if (write(board_fd, text, n) != (ssize_t)n) {
        printf("  [FAIL] the board end could not write\n");
        fails++;
    }
}

/** @brief One typed line: the shell's answer. */
static void
board_line(const char *cmd)
{
    say(cmd);
    say("\r\n");
    if (strncmp(cmd, "read /sys/vfs/manifest 0 ", 25) == 0) {
        say("/gui\td\tr-\t-\t-\t1\r\n"
            "/sys\td\t--\t-\t-\t2\r\n"
            "/sys/device\td\t--\t-\t-\t3\r\n"
            "/sys/device/uid\tf\tr-\t-\t-\t4\r\n");
    } else if (strcmp(cmd, "cat /sys/device/uid") == 0) {
        say(FAKE_UID "\r\n");
    }
    say("tikuOS:/> ");
}

/** @brief The transport's write: what the desktop sent the board. */
static int
board_hears(const void *buf, size_t len, void *ctx)
{
    const char *p = buf;
    size_t i;

    (void)ctx;
    for (i = 0; i < len; i++) {
        if (p[i] == '\r' || p[i] == '\x03') {
            continue;
        }
        if (p[i] == '\n') {
            line[line_len] = '\0';
            board_line(line);
            line_len = 0;
        } else if (line_len + 1 < sizeof line) {
            line[line_len++] = p[i];
        }
    }
    return (int)len;
}

/** @brief A released board with a /gui is reclaimed. */
static void
the_reclaim(desktop_t *app)
{
    int sv[2], k, at = -1, adopted = 0;
    tiku_backend_t *b;
    tiku_tx_t *tx;

    printf("a board reclaimed with its windows (D-052)\n");
    hosted(app);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        check(0, "a socket pair for the board");
        return;
    }
    board_fd = sv[1];
    line_len = 0;
    tx = tiku_tx_open_custom(sv[0], board_hears, NULL, NULL, "fake board");
    b = (tx != NULL) ? tiku_backend_tiku_open_tx(tx) : NULL;
    check(b != NULL && tiku_backend_tiku_identity(b) != NULL &&
          strcmp(tiku_backend_tiku_identity(b)->uid, FAKE_UID) == 0,
          "the fake board answers with its uid");
    if (b == NULL) {
        return;
    }
    snprintf(app->released[0].name, sizeof app->released[0].name, "fake");
    snprintf(app->released[0].uid, sizeof app->released[0].uid, FAKE_UID);
    app->released[0].used = 1;
    nports = 1;
    check(desktop_board_reclaim(app, "fake", -1, &app->embeds[0]) == 1 &&
          njobs == 1, "a reclaim asks the one port");
    /* The probe's answer, as probe_done() passes it on. */
    desktop_board_reclaim_landed(app, b);
    drop_jobs();
    check(told[0].reclaimed == 1, "the board is reclaimed");
    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        if (app->channels[k] != NULL &&
            strcmp(app->channel_board[k], "/devices/fake") == 0) {
            at = k;
        }
    }
    check(at >= 0, "a channel is kept for its /gui");
    for (k = 0; at >= 0 && k < TIKU_REMOTE_SESSIONS; k++) {
        adopted |= app->remote.session[k].used &&
                   app->remote.session[k].fd ==
                       tiku_channel_fd(app->channels[at]);
    }
    check(adopted,
          "and the channel is taken into the remote listener, so its "
          "windows come back");
    if (at >= 0) {
        tiku_channel_close(app->channels[at]);
        app->channels[at] = NULL;
    }
    (void)desktop_board_unmount(app, "/devices/fake");
    for (k = 0; k < njobs; k++) {
        jobs[k].run(jobs[k].ctx);
        jobs[k].free(jobs[k].ctx);
    }
    njobs = 0;
    close(sv[0]);
    close(sv[1]);
}

/** @brief A board with windows and no shell, connected from the Devices
 *         menu. */
static void
the_windows_only_connect(desktop_t *app)
{
    int k, at = -1, adopted = 0;

    printf("a windows-only board connected from the Devices menu (D-216)\n");
    /* No session is left from the reclaim, whose descriptor may be
     * reused. */
    for (k = 0; k < TIKU_REMOTE_SESSIONS; k++) {
        app->remote.session[k].used = 0;
    }
    check(device_connect(app, "/dev/ttyWIN0") == 0 && njobs == 1,
          "a connect is chosen from the Devices menu");
    if (njobs != 1) {
        drop_jobs();
        return;
    }
    jobs[0].run(jobs[0].ctx);
    land(0);
    drop_jobs();
    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        if (app->channels[k] != NULL) {
            at = k;
        }
    }
    check(at >= 0, "a channel is kept for the port");
    for (k = 0; at >= 0 && k < TIKU_REMOTE_SESSIONS; k++) {
        adopted |= app->remote.session[k].used &&
                   app->remote.session[k].fd ==
                       tiku_channel_fd(app->channels[at]);
    }
    check(adopted,
          "and the channel is taken into the remote listener, so its "
          "windows reach the desktop");
    if (at >= 0) {
        tiku_channel_close(app->channels[at]);
        app->channels[at] = NULL;
    }
    if (win_fd[1] >= 0) {
        close(win_fd[1]);
    }
}

int
main(void)
{
    desktop_t *app = calloc(1, sizeof *app);

    if (app == NULL) {
        return 1;
    }
#ifndef __linux__
    printf("skipped: the scan is replaced through GNU ld's --wrap\n");
    free(app);
    return 0;
#endif
    app->backend = tiku_mount_open();
    app->board_run_session = -1;
    app->board_listen = -1;
    app->remote.fd = -1;
    the_connect_during_a_look(app);
    the_late_startup_connect(app);
    the_askers_during_a_look(app);
    the_reclaim(app);
    the_windows_only_connect(app);
    tiku_backend_close(app->backend);
    free(app);
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
