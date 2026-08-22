/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_conduct.c - the conductor channel over a raw serial line.
 *
 * The socket the harness drives through is a convenience of the host; a
 * real board has a console line and nothing else.  This proves the channel
 * over a PTY PAIR configured raw -- which is what a cable is, as far as
 * this code can tell -- with the real listener on one end and the real
 * driver on the other, in TWO PROCESSES, because that is the only
 * arrangement in which a blocking question and the answering of it can
 * both happen: on a device they are two machines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "tiku_conduct.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/* The shell's world: what it would have shown, and what it was told. */
static int               injected;
static tiku_event_t last_event;

static void
fake_inject(void *ctx, const tiku_event_t *event)
{
    (void)ctx;
    injected++;
    last_event = *event;
}

static unsigned
fake_pixel(void *ctx, int x, int y)
{
    (void)ctx;
    /* A pattern, not a constant: an answer about the WRONG pixel must not
     * be indistinguishable from a right one. */
    return (unsigned)((x << 8) ^ y) & 0xffffffu;
}

static int
fake_text(void *ctx, const char *name, char *out, size_t max)
{
    (void)ctx;
    if (strcmp(name, "title") == 0) {
        snprintf(out, max, "Over the cable");
        return 1;
    }
    return 0;                   /* every other name is unknown here */
}

static void
raw(int fd)
{
    struct termios tio;

    /* A line discipline that helpfully rewrites a carriage return would
     * corrupt a frame; these bytes are not text. */
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        /* VMIN stays 1: with 0, a read of a quiet line returns zero, and
         * zero from a socket means the peer left.  See the note in
         * tiku_conduct_open_tty -- this is the bug this test found. */
        (void)tcsetattr(fd, TCSANOW, &tio);
    }
}

/** @brief The driver half: everything a script's ops are built from. */
static int
drive(int fd)
{
    tiku_conduct_client_t driver;
    tiku_event_t ev;
    char text[TIKU_CONDUCT_TEXT];
    int num = 0;
    int bad = 0;

    if (tiku_conduct_connect_fd(&driver, fd) != 0) {
        printf("  [FAIL] the driver cannot speak over the line\n");
        return 1;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.x = 314;
    ev.y = 159;
    ev.button = 1u;
    (void)tiku_conduct_inject(&driver, &ev);

    if (!tiku_conduct_query(&driver, TIKU_CQ_PIXEL, 7, 3, "",
                                 &num, NULL, 0u) ||
        num != (int)(((7 << 8) ^ 3) & 0xffffff)) {
        printf("  [FAIL] a pixel asked for over the line comes back, and "
               "is the pixel that was asked for\n");
        bad++;
    } else {
        printf("  [PASS] a pixel asked for over the line comes back, and "
               "is the pixel that was asked for\n");
    }

    text[0] = '\0';
    if (!tiku_conduct_query(&driver, TIKU_CQ_TEXT, 0, 0, "title",
                                 NULL, text, sizeof text) ||
        strcmp(text, "Over the cable") != 0) {
        printf("  [FAIL] so does a named string\n");
        bad++;
    } else {
        printf("  [PASS] so does a named string\n");
    }

    text[0] = '\0';
    if (tiku_conduct_query(&driver, TIKU_CQ_TEXT, 0, 0,
                                "no-such-name", NULL, text, sizeof text) ||
        text[0] != '\0') {
        printf("  [FAIL] a name the shell cannot say is REFUSED rather "
               "than answered with something\n");
        bad++;
    } else {
        printf("  [PASS] a name the shell cannot say is REFUSED rather "
               "than answered with something\n");
    }

    tiku_conduct_disconnect(&driver);
    return bad;
}

int
main(void)
{
    tiku_conduct_t shell;
    char slave_name[128];
    int master, slave;
    pid_t kid;
    int status = 0;
    int turns;

    printf("the conductor channel over a raw line (DEV-004)\n");

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
        ptsname_r(master, slave_name, sizeof slave_name) != 0) {
        printf("  [FAIL] cannot make a pty pair\n");
        return 1;
    }
    slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        printf("  [FAIL] cannot open the pty slave\n");
        return 1;
    }
    raw(master);
    raw(slave);

    (void)fflush(stdout);       /* or the child inherits our lines */
    kid = fork();
    if (kid < 0) {
        printf("  [FAIL] cannot fork the driver\n");
        return 1;
    }
    if (kid == 0) {
        int bad;

        (void)close(master);
        bad = drive(slave);
        (void)close(slave);
        (void)fflush(stdout);   /* _exit does not, and its verdicts matter */
        _exit(bad);
    }
    (void)close(slave);

    check(tiku_conduct_adopt(&shell, master, NULL, fake_inject,
                                  fake_pixel, fake_text) == 0,
          "the shell speaks the channel over a line it was handed -- no "
          "socket, no listening, just the cable");
    /* The shell's turn, over and over, exactly as its main loop takes it. */
    for (turns = 0; turns < 600; turns++) {
        struct timespec ts;

        (void)tiku_conduct_poll(&shell);
        if (waitpid(kid, &status, WNOHANG) == kid) {
            kid = -1;
            break;
        }
        ts.tv_sec = 0;
        ts.tv_nsec = 5 * 1000 * 1000;
        (void)nanosleep(&ts, NULL);
    }
    if (kid > 0) {
        printf("  [FAIL] the driver never finished\n");
        fails++;
        (void)kill(kid, SIGKILL);
        (void)waitpid(kid, &status, 0);
    }

    check(injected == 1 && last_event.x == 314 && last_event.y == 159 &&
          last_event.type == TIKU_EVENT_POINTER_DOWN,
          "an injected event arrives intact -- the same event, not a "
          "reasonable-looking one");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "and every question the driver asked was answered over the wire");

    tiku_conduct_shutdown(&shell);

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe channel does not care what carries it\n");
    return 0;
}
