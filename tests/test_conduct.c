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

/* A fact that will CHANGE while the driver watches: the shell flips it
 * mid-loop, and the driver must hear about it without asking again. */
static const char *song = "quiet";

static int
fake_text(void *ctx, const char *name, char *out, size_t max)
{
    (void)ctx;
    if (strcmp(name, "title") == 0) {
        snprintf(out, max, "Over the cable");
        return 1;
    }
    if (strcmp(name, "song") == 0) {
        snprintf(out, max, "%s", song);
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

    text[0] = '\0';
    if (!tiku_conduct_query(&driver, TIKU_CQ_TEXT, 0, 0, "~song",
                                 NULL, text, sizeof text) ||
        strcmp(text, "quiet") != 0) {
        printf("  [FAIL] a watched name answers its current value\n");
        bad++;
    } else {
        printf("  [PASS] a watched name answers its current value\n");
    }

    if (tiku_conduct_query(&driver, TIKU_CQ_TEXT, 0, 0, "~no-such-name",
                                 NULL, text, sizeof text)) {
        printf("  [FAIL] a watch on a name the shell cannot say is "
               "refused, the way the name itself is\n");
        bad++;
    } else {
        printf("  [PASS] a watch on a name the shell cannot say is "
               "refused, the way the name itself is\n");
    }

    {
        char who[TIKU_CONDUCT_ARG];
        char said[TIKU_CONDUCT_TEXT];

        /* The driver now goes silent.  The next thing on the wire must
         * be the shell speaking first -- the whole point of stage 2. */
        if (!tiku_conduct_told(&driver, who, sizeof who, said,
                                    sizeof said, 4000) ||
            strcmp(who, "song") != 0 || strcmp(said, "loud") != 0) {
            printf("  [FAIL] the changed fact is PUSHED to the driver, "
                   "named and current, unasked\n");
            bad++;
        } else {
            printf("  [PASS] the changed fact is PUSHED to the driver, "
                   "named and current, unasked\n");
        }
        /* And a quiet watch stays quiet: no change, no frame.  Bounded
         * short -- this waits out the full window to prove silence. */
        if (tiku_conduct_told(&driver, who, sizeof who, said,
                                   sizeof said, 400)) {
            printf("  [FAIL] an unchanged fact is not spoken of again\n");
            bad++;
        } else {
            printf("  [PASS] an unchanged fact is not spoken of again\n");
        }
    }

    tiku_conduct_disconnect(&driver);
    return bad;
}

/* The grants section's world: how many events got through the door. */
static int g_injected;

static void
grant_inject(void *ctx, const tiku_event_t *event)
{
    (void)ctx;
    (void)event;
    g_injected++;
}

/** @brief One session under required names: says what its grant let it do. */
static int
grant_drive(const char *sock)
{
    tiku_conduct_client_t d;
    tiku_event_t ev;
    char text[TIKU_CONDUCT_TEXT];
    int bad = 0;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.x = 1;
    ev.y = 2;
    ev.button = 1u;

    /* An unnamed (wrongly named) peer gets exactly what a version
     * mismatch gets: nothing.  The closed line reads as a failed
     * question, quickly, not as a wait. */
    if (tiku_conduct_connect_as(&d, sock, 2000, "wrong-name") != 0) {
        printf("  [FAIL] the socket accepts a connection at all\n");
        return 1;
    }
    if (tiku_conduct_query(&d, TIKU_CQ_TEXT, 0, 0, "title",
                                NULL, text, sizeof text)) {
        printf("  [FAIL] a wrongly named peer is answered nothing\n");
        bad++;
    } else {
        printf("  [PASS] a wrongly named peer is answered nothing\n");
    }
    tiku_conduct_disconnect(&d);

    /* The reader's credential: asking works, injecting provably not. */
    if (tiku_conduct_connect_as(&d, sock, 2000, "look-only") != 0) {
        printf("  [FAIL] the reader can connect\n");
        return bad + 1;
    }
    text[0] = '\0';
    if (!tiku_conduct_query(&d, TIKU_CQ_TEXT, 0, 0, "title",
                                 NULL, text, sizeof text) ||
        strcmp(text, "Over the cable") != 0) {
        printf("  [FAIL] a QUERY-only credential is answered\n");
        bad++;
    } else {
        printf("  [PASS] a QUERY-only credential is answered\n");
    }
    (void)tiku_conduct_inject(&d, &ev);      /* it TRIES -- see parent */
    /* One more answered question AFTER the try, so the inject cannot
     * still be in flight when the parent counts what arrived. */
    (void)tiku_conduct_query(&d, TIKU_CQ_TEXT, 0, 0, "title",
                                  NULL, text, sizeof text);
    tiku_conduct_disconnect(&d);

    /* The driver's credential: the same event goes through. */
    if (tiku_conduct_connect_as(&d, sock, 2000, "drive-me") != 0) {
        printf("  [FAIL] the full driver can connect\n");
        return bad + 1;
    }
    (void)tiku_conduct_inject(&d, &ev);
    (void)tiku_conduct_query(&d, TIKU_CQ_TEXT, 0, 0, "title",
                                  NULL, text, sizeof text);
    tiku_conduct_disconnect(&d);
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
    for (turns = 0; turns < 1600; turns++) {
        struct timespec ts;

        (void)tiku_conduct_poll(&shell);
        if (shell.subs > 0 && strcmp(song, "quiet") == 0) {
            /* Flipped on the FACT of the watch, not on a clock: the
             * quiet answer is already on the wire when subs shows, so
             * this cannot race the subscription on a slow machine. */
            song = "loud";
        }
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

    printf("\nidentity before reach: names decide grants (stage 3)\n");
    {
        tiku_conduct_t gate;
        const char *gsock = "/tmp/trk_conduct_grants.sock";
        pid_t gkid;
        int gstatus = 0;

        (void)unlink(gsock);
        check(tiku_conduct_listen(&gate, gsock, NULL, grant_inject,
                                       fake_pixel, fake_text) == 0,
              "a shell can require names on its channel");
        tiku_conduct_require(&gate, "drive-me", "look-only");
        (void)fflush(stdout);
        gkid = fork();
        if (gkid == 0) {
            int bad = grant_drive(gsock);

            (void)fflush(stdout);
            _exit(bad);
        }
        for (turns = 0; turns < 3000; turns++) {
            struct timespec ts;

            (void)tiku_conduct_poll(&gate);
            if (waitpid(gkid, &gstatus, WNOHANG) == gkid) {
                gkid = -1;
                break;
            }
            ts.tv_sec = 0;
            ts.tv_nsec = 5 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
        }
        if (gkid > 0) {
            printf("  [FAIL] the grants driver never finished\n");
            fails++;
            (void)kill(gkid, SIGKILL);
            (void)waitpid(gkid, &gstatus, 0);
        }
        check(WIFEXITED(gstatus) && WEXITSTATUS(gstatus) == 0,
              "every grant answered exactly what its name allows");
        check(g_injected == 1,
              "one event arrived: the driver's -- the reader's try was "
              "dropped at the door");
        tiku_conduct_shutdown(&gate);
        (void)unlink(gsock);
    }

    printf("\na session's spend is bounded (stage 3)\n");
    {
        tiku_conduct_t meter;
        const char *qsock = "/tmp/trk_conduct_quota.sock";
        pid_t qkid;
        int qstatus = 0;

        (void)unlink(qsock);
        check(tiku_conduct_listen(&meter, qsock, NULL, fake_inject,
                                       fake_pixel, fake_text) == 0,
              "a shell can meter its channel");
        tiku_conduct_quota(&meter, 3);
        (void)fflush(stdout);
        qkid = fork();
        if (qkid == 0) {
            tiku_conduct_client_t d;
            char text[TIKU_CONDUCT_TEXT];
            int bad = 0, k;

            if (tiku_conduct_connect(&d, qsock, 2000) != 0) {
                _exit(9);
            }
            for (k = 0; k < 3; k++) {
                if (!tiku_conduct_query(&d, TIKU_CQ_TEXT, 0, 0,
                                             "title", NULL, text,
                                             sizeof text)) {
                    printf("  [FAIL] the %dth question within the quota "
                           "is answered\n", k + 1);
                    bad++;
                }
            }
            if (bad == 0) {
                printf("  [PASS] every question within the quota is "
                       "answered\n");
            }
            if (tiku_conduct_query(&d, TIKU_CQ_TEXT, 0, 0, "title",
                                        NULL, text, sizeof text)) {
                printf("  [FAIL] the question past the quota is cut "
                       "off, not rationed\n");
                bad++;
            } else {
                printf("  [PASS] the question past the quota is cut "
                       "off, not rationed\n");
            }
            tiku_conduct_disconnect(&d);
            (void)fflush(stdout);
            _exit(bad);
        }
        for (turns = 0; turns < 3000; turns++) {
            struct timespec ts;

            (void)tiku_conduct_poll(&meter);
            if (waitpid(qkid, &qstatus, WNOHANG) == qkid) {
                qkid = -1;
                break;
            }
            ts.tv_sec = 0;
            ts.tv_nsec = 5 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
        }
        if (qkid > 0) {
            printf("  [FAIL] the quota driver never finished\n");
            fails++;
            (void)kill(qkid, SIGKILL);
            (void)waitpid(qkid, &qstatus, 0);
        }
        check(WIFEXITED(qstatus) && WEXITSTATUS(qstatus) == 0,
              "the cap holds at exactly the number it names");
        tiku_conduct_shutdown(&meter);
        (void)unlink(qsock);
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe channel does not care what carries it\n");
    return 0;
}
