/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_board_jobs.c - a board's jobs and channels end with the board.
 *
 * A board's close runs as a job under the board's key, after every job
 * still asking it.  A fake board answers the shell and the interpreter
 * over a socket pair, mounted under a name longer than thirty characters;
 * a save and a run on it end, and the job that gives its shell back must
 * carry the key the unmount cancels and closes under.  Then a board dials
 * in while every channel slot is held, and no remote session may be left
 * on the descriptor its refused channel closed.  Last, a board on a serial
 * port is disconnected and its port is offered to connect again.  The
 * workers are replaced by a queue the test runs by hand.
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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
/* THE WORKERS, BY HAND                                                      */
/*---------------------------------------------------------------------------*/

#define JOBS 16

static tiku_work_job_t jobs[JOBS];
static int job_cancelled[JOBS];
static int njobs;
static char cancel_key[TIKU_WORK_KEY];

int
tiku_work_post(const tiku_work_job_t *job)
{
    if (njobs >= JOBS || job->run == NULL) {
        return -1;
    }
    jobs[njobs] = *job;
    jobs[njobs].key[TIKU_WORK_KEY - 1] = '\0';
    job_cancelled[njobs] = 0;
    njobs++;
    return 0;
}

int
tiku_work_cancel(const char *key)
{
    int i, n = 0;

    snprintf(cancel_key, sizeof cancel_key, "%s", key);
    for (i = 0; i < njobs; i++) {
        if (strcmp(jobs[i].key, key) == 0 && !job_cancelled[i]) {
            job_cancelled[i] = 1;
            n++;
        }
    }
    return n;
}

/** @brief A slow job joins the same queue. */
int
tiku_work_post_slow(const tiku_work_job_t *job)
{
    return tiku_work_post(job);
}

int tiku_work_collect(void) { return 0; }
int tiku_work_cancelled(const void *ctx) { (void)ctx; return 0; }
int tiku_work_busy(void) { return njobs; }

/** @brief Run every job queued, in order, as one worker would; a cancelled
 *         one is only freed. */
static void
run_jobs(void)
{
    int i;

    for (i = 0; i < njobs; i++) {
        if (!job_cancelled[i]) {
            jobs[i].run(jobs[i].ctx);
        }
        if (jobs[i].free != NULL) {
            jobs[i].free(jobs[i].ctx);
        }
    }
    njobs = 0;
}

/*---------------------------------------------------------------------------*/
/* THE FAKE BOARD                                                            */
/*---------------------------------------------------------------------------*/

static char   line[1024];
static size_t line_len;
static int    in_basic;
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

/** @brief One typed line: the shell's answer, or the interpreter's. */
static void
board_line(const char *cmd)
{
    say(cmd);
    say("\r\n");
    if (in_basic) {
        if (strcmp(cmd, "BYE") == 0) {
            in_basic = 0;
            say("tikuOS:/> ");
            return;
        }
        if (strcmp(cmd, "RUN") == 0) {
            say("1\r\n");
        }
        say("ok> ");
        return;
    }
    if (strcmp(cmd, "basic") == 0) {
        in_basic = 1;
        say("Tiku BASIC\r\nok> ");
        return;
    }
    if (strncmp(cmd, "read /sys/vfs/manifest 0 ", 25) == 0) {
        say("/sys\td\t--\t-\t-\t1\r\n/sys/uptime\tf\tr-\t-\t-\t2\r\n");
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

/*---------------------------------------------------------------------------*/
/* THE CHECKS                                                                */
/*---------------------------------------------------------------------------*/

/* Longer than the thirty characters a key once kept of a name. */
#define LONG_NAME "a-board-whose-name-runs-past-thirty-chars"

/** @brief Pump @p app's run until the job that gives the shell back is
 *         posted.  @return its index, or -1. */
static int
run_to_over(desktop_t *app)
{
    int turns;

    for (turns = 0; turns < 400 && app->board_run != NULL; turns++) {
        desktop_board_pump(app);
    }
    return (app->board_run == NULL && njobs == 1) ? 0 : -1;
}

/** @brief A run or save ends on the board mounted at @p prefix, and the
 *         board is unmounted: the two jobs must share the board's key. */
static void
one_ending(desktop_t *app, const char *prefix, int saving)
{
    char over_key[TIKU_WORK_KEY];
    int sv[2], at;
    tiku_backend_t *b;
    tiku_tx_t *tx;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        check(0, "a socket pair for the board");
        return;
    }
    board_fd = sv[1];
    in_basic = 0;
    line_len = 0;
    tx = tiku_tx_open_custom(sv[0], board_hears, NULL, NULL, "fake board");
    b = (tx != NULL) ? tiku_backend_tiku_open_tx(tx) : NULL;
    check(b != NULL && tiku_mount_add(app->backend, prefix, b, 1) == 0,
          "the fake board is mounted under a long name");
    if (saving) {
        check(desktop_board_save(app, NULL, "", "prog", "10 PRINT 1\n"),
              "a save starts on the board, named by no board");
    } else {
        check(desktop_board_run(app, NULL, "", "10 PRINT 1\n"),
              "a run starts on the board, named by no board");
    }
    at = run_to_over(app);
    check(at == 0, "it ends, and its shell is handed back on a worker");
    if (at != 0) {
        return;
    }
    memcpy(over_key, jobs[at].key, sizeof over_key);
    printf("  (the job that gives the shell back is keyed \"%s\")\n",
           over_key);
    check(desktop_board_unmount(app, prefix) == 0 && njobs == 2,
          "the board is unmounted, and its close is queued");
    printf("  (the unmount cancels \"%s\" and closes under \"%s\")\n",
           cancel_key, jobs[1].key);
    check(strcmp(over_key, jobs[1].key) == 0,
          "the shell's hand-back and the close share the board's key, so "
          "the close runs after it");
    check(job_cancelled[at],
          "and the unmount cancels the hand-back before it starts");
    run_jobs();
    close(sv[0]);
    close(sv[1]);
}

static void
the_board_jobs(desktop_t *app)
{
    printf("a board's jobs end with the board\n");
    one_ending(app, "/devices/" LONG_NAME, 1);
    one_ending(app, "/devices/" LONG_NAME, 0);
}

/** @brief A board on a serial port is disconnected from the Devices menu:
 *         its port is offered to connect again, under the board's name. */
static void
the_disconnect(desktop_t *app)
{
    int sv[2], i, offered = 0, rc;
    char port[256];
    tiku_backend_t *b;
    tiku_tx_t *tx;

    printf("a board disconnected from the Devices menu (D-049)\n");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        check(0, "a socket pair for the board");
        return;
    }
    board_fd = sv[1];
    in_basic = 0;
    line_len = 0;
    app->ncandidates = 0;
    tx = tiku_tx_open_custom(sv[0], board_hears, NULL, NULL,
                             "/dev/ttyFAKE7 @115200");
    b = (tx != NULL) ? tiku_backend_tiku_open_tx(tx) : NULL;
    check(b != NULL &&
          tiku_mount_add(app->backend, "/devices/fake", b, 1) == 0 &&
          desktop_board_port(app, "fake", port, sizeof port) > 0,
          "the fake board is mounted, on a serial port");
    rc = device_disconnect(app, "fake");
    printf("  (device_disconnect gave %d; %d port(s) on offer)\n", rc,
           app->ncandidates);
    check(rc == 0 && desktop_board_port(app, "fake", port, sizeof port) == 0,
          "it is unmounted, and the disconnect says it succeeded");
    for (i = 0; i < app->ncandidates; i++) {
        offered |= strcmp(app->candidates[i].port, "/dev/ttyFAKE7") == 0 &&
                   strcmp(app->candidates[i].name, "fake") == 0;
    }
    check(offered, "its port is offered to connect again, under its name");
    run_jobs();
    check(device_disconnect(app, "fake") == -1,
          "and disconnecting a board not mounted fails");
    close(sv[0]);
    close(sv[1]);
}

/** @brief Whether @p fd is an open descriptor. */
static int
is_open(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

static void
the_full_slots(desktop_t *app)
{
    struct sockaddr_in a;
    socklen_t alen = sizeof a;
    int pairs[TIKU_MOUNT_MAX][2];
    int dial, k, stale = 0, sessions = 0;
    struct timespec nap = { 0, 20 * 1000 * 1000 };

    printf("a board that dials in when every slot is held\n");
    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, pairs[k]) != 0) {
            check(0, "a socket pair for a held channel");
            return;
        }
        app->channels[k] = tiku_channel_adopt(tiku_link_tcp_open(pairs[k][0]));
    }
    app->board_listen = tiku_link_tcp_listen(0);
    check(app->board_listen > 0 &&
          getsockname(app->board_listen, (struct sockaddr *)&a, &alen) == 0,
          "the desktop listens for boards");
    dial = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(dial >= 0 && connect(dial, (struct sockaddr *)&a, sizeof a) == 0,
          "a board dials in");
    nanosleep(&nap, NULL);
    boards_accept(app);
    for (k = 0; k < TIKU_REMOTE_SESSIONS; k++) {
        if (app->remote.session[k].used) {
            sessions++;
            stale += !is_open(app->remote.session[k].fd);
        }
    }
    printf("  (%d remote sessions, %d on a closed descriptor)\n", sessions,
           stale);
    check(stale == 0,
          "no remote session is left on the descriptor of the channel that "
          "found no slot");
    {
        char c;

        (void)fcntl(dial, F_SETFL, O_NONBLOCK);
        check(read(dial, &c, 1) == 0,
              "and the board's connection was taken and closed");
    }
    close(dial);
    close(app->board_listen);
    app->board_listen = -1;
    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        tiku_channel_close(app->channels[k]);
        app->channels[k] = NULL;
        close(pairs[k][1]);
    }
}

int
main(void)
{
    desktop_t *app = calloc(1, sizeof *app);

    if (app == NULL) {
        return 1;
    }
    app->backend = tiku_mount_open();
    app->board_run_session = -1;
    app->board_listen = -1;
    the_board_jobs(app);
    the_full_slots(app);
    the_disconnect(app);
    tiku_backend_close(app->backend);
    free(app);
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
