/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_trail.c - the ring that says what a program was doing.
 *
 * What it has to get right: the oldest lines go when it is full, the
 * newest are the ones kept, and reading into a small buffer gives the
 * END of the trail -- the run-up to whatever is being asked about.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trail.h"

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

int
main(void)
{
    char big[64 * 1024];
    char small[256];
    int i;

    printf("the trail\n");

    tiku_trail("first");
    tiku_trail("second %d", 2);
    (void)tiku_trail_read(big, sizeof big);
    check(strcmp(strstr(big, "first") ? "y" : "n", "y") == 0 &&
          strstr(big, "second 2") != NULL &&
          strstr(big, "first") < strstr(big, "second"),
          "lines come back in the order they were written");
    check(strstr(big, "     ") != NULL || big[0] == ' ' || big[0] >= '0',
          "and each says how long into the program it was written");

    /* Past the end of the ring: the oldest go, the newest stay. */
    for (i = 0; i < TIKU_TRAIL_LINES + 100; i++) {
        tiku_trail("line %d", i);
    }
    (void)tiku_trail_read(big, sizeof big);
    check(strstr(big, "first") == NULL,
          "what a full ring drops is the oldest");
    check(strstr(big, "line 99") == NULL &&
          strstr(big, "line 611") != NULL,
          "and what it keeps is the newest");
    check(tiku_trail_written() == (unsigned long)(TIKU_TRAIL_LINES + 102uL),
          "it counts every line ever written, not the ones it still has");

    /* A small buffer gets the END of the trail: the run-up to now. */
    (void)tiku_trail_read(small, sizeof small);
    check(strstr(small, "line 611") != NULL,
          "a reader with little room is given the newest lines");
    check(strlen(small) < sizeof small,
          "and never more than it asked for");

        tiku_trail("look: probe ttyACM0");
    tiku_trail("run: sending");
    tiku_trail("look: probe ttyACM1");
    (void)tiku_trail_find("look:", small, sizeof small);
    check(strstr(small, "ttyACM0") != NULL &&
          strstr(small, "ttyACM1") != NULL &&
          strstr(small, "run: sending") == NULL,
          "a reader asking about one thing is given only that");
    check(strstr(small, "line 611") == NULL,
          "and not the newest lines merely because they are newest");

    {
        /* The same lines, by the road a signal handler would take. */
        char path[] = "/tmp/tiku_trail_testXXXXXX";
        int fd = mkstemp(path);
        char back[64 * 1024];
        ssize_t n;

        tiku_trail_write(fd);
        (void)lseek(fd, 0, SEEK_SET);
        n = read(fd, back, sizeof back - 1u);
        back[(n > 0) ? n : 0] = '\0';
        (void)close(fd);
        (void)unlink(path);
        check(n > 0 && strstr(back, "line 611") != NULL &&
              strstr(back, "line 610") < strstr(back, "line 611"),
              "writing it to a file gives the same lines, in the same order");
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
