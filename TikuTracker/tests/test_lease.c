/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_lease.c - one client holds the board's shell.
 *
 * A program running on a board owns its console: every byte sent to it
 * is the program's input, and its output carries no prompt to end it
 * until it ends.  So a caller that runs one TAKES the shell -- the text
 * comes to it as it arrives, the namespace is shut until it is handed
 * back, and the board's window frames go on regardless.  A pty stands in
 * for the board here; with -p, a real one answers as well.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_model.h"
#include "tiku_session.h"
#include "tiku_tx.h"

static int passes, fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        fails++;
    }
}

static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/*---------------------------------------------------------------------------*/
/* WHAT THE CLIENT HEARD                                                     */
/*---------------------------------------------------------------------------*/

static char   heard[1024];
static size_t heard_len;

static void
take_text(const unsigned char *bytes, size_t n, void *ctx)
{
    (void)ctx;
    if (heard_len + n < sizeof heard) {
        memcpy(heard + heard_len, bytes, n);
        heard_len += n;
        heard[heard_len] = '\0';
    }
}

static int  frames;
static char frame_seen[64];

static void
take_frame(const unsigned char *bytes, size_t len, void *ctx)
{
    (void)ctx;
    if (len < sizeof frame_seen) {
        memcpy(frame_seen, bytes, len);
        frame_seen[len] = '\0';
    }
    frames++;
}

static void
put(int fd, const void *buf, size_t n)
{
    if (write(fd, buf, n) != (ssize_t)n) {
        printf("  [FAIL] the board end could not write\n");
        fails++;
    }
}

/** @brief The board while a client holds the line: text with a frame in
 *         the middle of it, as a board drawing a window would send. */
static void *
board_talks(void *arg)
{
    int master = *(int *)arg;
    static const unsigned char frame[6] = { 0xC0, 0xF1, 'o', 'k', '!', 0xC0 };

    usleep(30000);
    put(master, "one\r\n", 5);
    usleep(10000);
    put(master, frame, sizeof frame);
    usleep(10000);
    put(master, "two\r\n", 5);
    return NULL;
}

/** @brief The board answering a command, once the line is its own again. */
static void *
board_answers(void *arg)
{
    int master = *(int *)arg;
    char line[64];
    size_t n = 0;

    while (n < sizeof line - 1) {
        char c;

        if (read(master, &c, 1) != 1) {
            return NULL;
        }
        line[n++] = c;
        if (c == '\n') {
            break;
        }
    }
    put(master, "ping\r\nPONG\r\ntikuOS:/> ", 22);
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* THE LINE ITSELF                                                           */
/*---------------------------------------------------------------------------*/

static void
the_line(void)
{
    int master, slave;
    char path[64], out[64], rx[16];
    tiku_session_t *s;
    pthread_t th;
    long deadline;
    ssize_t n;
    int rc;

    printf("holding the line\n");
    if (openpty(&master, &slave, path, NULL, NULL) != 0) {
        printf("  [FAIL] no pty\n");
        fails++;
        return;
    }
    close(slave);
    s = tiku_session_new(tiku_tx_open_serial(path, 115200));
    if (s == NULL) {
        printf("  [FAIL] no session on the pty\n");
        fails++;
        return;
    }
    tiku_session_on_frame(s, take_frame, NULL);
    tiku_session_on_text(s, take_text, NULL);

    pthread_create(&th, NULL, board_talks, &master);
    deadline = now_ms() + 3000;
    while (strstr(heard, "two") == NULL && now_ms() < deadline) {
        (void)tiku_session_poll(s, 20);
    }
    pthread_join(th, NULL);
    check(strstr(heard, "one") != NULL && strstr(heard, "two") != NULL,
          "the board's text reaches the client holding the line, as it comes");
    check(frames == 1 && strcmp(frame_seen, "ok!") == 0,
          "and a frame in the middle of it still reaches the window");

    /* The discriminator against a command: a command appends a newline
     * and waits for a prompt.  These bytes are the caller's own. */
    check(tiku_session_send_raw(s, "RUN", 3) == 0, "raw bytes are taken");
    usleep(50000);
    n = read(master, rx, sizeof rx);
    check(n == 3 && memcmp(rx, "RUN", 3) == 0,
          "they arrive exactly as given -- no newline added, no prompt awaited");

    tiku_session_on_text(s, NULL, NULL);
    pthread_create(&th, NULL, board_answers, &master);
    rc = tiku_session_cmd(s, "ping", out, sizeof out, 3000);
    pthread_join(th, NULL);
    check(rc >= 0 && strcmp(out, "PONG") == 0,
          "handing the line back makes commands work again");

    tiku_session_free(s);
    close(master);
}

/*---------------------------------------------------------------------------*/
/* A REAL BOARD                                                              */
/*---------------------------------------------------------------------------*/

static void
the_board(const char *port, int baud)
{
    tiku_backend_t *b;
    char buf[128], err[128];
    long deadline, took;
    int before, during, after;

    printf("a board's shell (%s)\n", port ? port : "auto");
    b = tiku_backend_tiku_open(port, baud);
    if (b == NULL) {
        printf("  [SKIP] no device answered\n");
        return;
    }
    before = b->ops->read(b, "/sys/uptime", buf, sizeof buf);
    check(before > 0, "the namespace answers before anybody holds the shell");

    heard_len = 0;
    heard[0] = '\0';
    check(tiku_backend_tiku_lease(b, take_text, NULL) == 0,
          "a client takes the shell");
    check(tiku_backend_tiku_leased(b) == 1, "and the board says it is held");
    check(tiku_backend_tiku_lease(b, take_text, NULL) == -1,
          "a second client is refused -- one line, one holder");

    /* AT ONCE, and that is the point.  Without the guard the read still
     * fails -- the holder's callback is taking the text, so the command
     * never finds its prompt -- but only after its whole timeout.  A
     * refusal a caller waits seconds for is a hang with an excuse. */
    took = now_ms();
    during = b->ops->read(b, "/sys/uptime", buf, sizeof buf);
    took = now_ms() - took;
    check(during < 0 && took < 250,
          "a read is refused AT ONCE while the shell is held, not left to "
          "time out");
    err[0] = '\0';
    took = now_ms();
    during = b->ops->write(b, "/dev/led0", "1", 1u, err, sizeof err);
    took = now_ms() - took;
    check(during < 0 && took < 250 && strstr(err, "running") != NULL,
          "a write is refused at once and NAMES the running program, rather "
          "than becoming its input");

    check(tiku_backend_tiku_lease_send(b, "cat /sys/uptime\n", 16u) == 0,
          "the holder writes to the shell directly");
    deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        (void)b->ops->pump(b, 50);
        if (strchr(heard, '\n') != NULL && strpbrk(heard, "0123456789")) {
            break;
        }
    }
    check(strpbrk(heard, "0123456789") != NULL,
          "and the board's answer arrives on the holder's own callback");

    tiku_backend_tiku_unlease(b);
    check(tiku_backend_tiku_leased(b) == 0, "the shell is given back");
    after = b->ops->read(b, "/sys/uptime", buf, sizeof buf);
    check(after > 0, "and the namespace answers again");

    tiku_backend_close(b);
}

int
main(int argc, char **argv)
{
    const char *port = NULL;
    int baud = 115200, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        }
    }
    the_line();
    if (port != NULL) {
        the_board(port, baud);
    } else {
        printf("a board's shell\n  [SKIP] no -p port given\n");
    }
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
