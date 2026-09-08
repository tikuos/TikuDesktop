/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_boardbasic.c - a program runs on a board and is watched here.
 *
 * The runner hands a program to the board's interpreter and gives back
 * what it printed, as lines.  This asks the four things a person doing
 * it would notice: the output arrives, an error is reported the way the
 * same interpreter reports it here, a program can be stopped, and the
 * board is left usable afterwards -- which is the one a leaked lease
 * would fail.  Needs a board: -p names its port.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_board_basic.h"
#include "tiku_model.h"

static int passes, fails;

static char said[4096];

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        fails++;
        /* What the board actually said: a run that failed for a reason
         * nobody can see is a run nobody can fix. */
        printf("         the board said: %.400s\n", said);
    }
}

static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void
took_line(const char *line, void *ctx)
{
    (void)ctx;
    if (strlen(said) + strlen(line) + 2u < sizeof said) {
        strcat(said, line);
        strcat(said, "\n");
    }
}

/** @brief Run @p program to its end (or @p ms), collecting its output. */
static tiku_board_basic_state_t
run_it(tiku_backend_t *b, const char *program, int ms, int stop_after_ms)
{
    tiku_board_basic_t *r = tiku_board_basic_start(b, program);
    tiku_board_basic_state_t st = TIKU_BOARD_BASIC_FAILED;
    long deadline = now_ms() + ms;
    long stop_at = (stop_after_ms > 0) ? now_ms() + stop_after_ms : 0;

    said[0] = '\0';
    if (r == NULL) {
        return TIKU_BOARD_BASIC_FAILED;
    }
    while (now_ms() < deadline) {
        st = tiku_board_basic_pump(r, 50, took_line, NULL);
        if (st == TIKU_BOARD_BASIC_ENDED || st == TIKU_BOARD_BASIC_FAILED) {
            break;
        }
        if (stop_at != 0 && now_ms() >= stop_at) {
            tiku_board_basic_stop(r);
            stop_at = 0;
            deadline = now_ms() + 3000;
        }
    }
    tiku_board_basic_free(r);
    return st;
}

int
main(int argc, char **argv)
{
    const char *port = NULL;
    int baud = 115200, i;
    tiku_backend_t *b;
    tiku_board_basic_state_t st;
    char buf[128];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        }
    }
    printf("a program on a board\n");
    if (port == NULL) {
        printf("  [SKIP] no -p port given\n");
        return 0;
    }
    b = tiku_backend_tiku_open(port, baud);
    if (b == NULL) {
        printf("  [SKIP] no device answered\n");
        return 0;
    }

    st = run_it(b, "10 PRINT \"HELLO\"\n20 PRINT 6 * 7\n", 30000, 0);
    check(st == TIKU_BOARD_BASIC_ENDED && strstr(said, "HELLO") != NULL
          && strstr(said, "42") != NULL,
          "the program's output comes back, and the run ends by itself");

    /* The same two lines the interpreter prints on this machine -- `?
     * reason` then `at line N` -- which is what lets the editor fold them
     * onto one row and put the caret on the line named. */
    st = run_it(b, "10 PRINT \"A\"\n20 PRINT UNDEFINED$(\n", 30000, 0);
    check(st == TIKU_BOARD_BASIC_ENDED && strchr(said, '?') != NULL
          && strstr(said, "at line 20") != NULL,
          "an error comes back in the two lines the editor already folds: "
          "'? reason' and 'at line N'");

    /* Long enough that it is plainly still going when it is stopped: the
     * board runs about a thousand loop steps a second. */
    st = run_it(b, "10 PRINT \"GO\"\n20 FOR I = 1 TO 20000\n30 NEXT I\n"
                   "40 PRINT \"NOTREACHED\"\n", 30000, 1500);
    check(st == TIKU_BOARD_BASIC_STOPPED
          && strstr(said, "NOTREACHED") == NULL,
          "a running program is stopped, and the rest of it never runs");

    /* The one a leaked lease fails: the namespace has to work again. */
    check(b->ops->read(b, "/sys/uptime", buf, sizeof buf) > 0,
          "and the board answers its namespace again afterwards");

    tiku_backend_close(b);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
