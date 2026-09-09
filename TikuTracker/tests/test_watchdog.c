/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog.c - a turn that does not end is written down.
 *
 * A freeze leaves no signal and no exit, so nothing a crash handler
 * does would catch one.  What is asked here: that a loop which stops
 * turning is noticed, that the report names the thread's OWN place --
 * not the watcher's -- and that a loop still turning is left alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_report.h"
#include "tiku_trail.h"
#include "tiku_watch.h"

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

/** @brief The whole of @p path, or an empty string. */
static const char *
slurp(const char *path)
{
    static char text[64 * 1024];
    FILE *f = fopen(path, "r");
    size_t n = 0u;

    text[0] = '\0';
    if (f == NULL) {
        return text;
    }
    n = fread(text, 1u, sizeof text - 1u, f);
    text[n] = '\0';
    (void)fclose(f);
    return text;
}

/** @brief Turn the loop @p times, a little apart, as a shell would. */
static void
turning_for(int times)
{
    int i;

    for (i = 0; i < times; i++) {
        tiku_watch_beat();
        usleep(50000);
    }
}

/*
 * Not static, so that -rdynamic puts the name where a backtrace can find
 * it, and it does not end early: being asked to speak interrupts a
 * sleep, and a turn that gave up at the first ask would not be stuck.
 */
void __attribute__((noinline))
the_turn_that_never_ended(void)
{
    struct timespec began, now;

    (void)clock_gettime(CLOCK_MONOTONIC, &began);
    for (;;) {
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - began.tv_sec >= 3) {
            return;
        }
        usleep(100000);
    }
}

int
main(void)
{
    char dir[] = "/tmp/tiku_watchXXXXXX";
    char latest[512];
    const char *said;

    printf("a turn that does not end\n");
    if (mkdtemp(dir) == NULL) {
        printf("  [SKIP] no temporary folder\n");
        return 0;
    }
    snprintf(latest, sizeof latest, "%s/latest-stall.txt", dir);
    tiku_report_install("WatchedProgram", dir);
    tiku_watch_install(700);    /* a turn quiet for 0.7 s is a stall */

    tiku_trail("turning normally");
    turning_for(20);            /* a second of ordinary turns */
    check(tiku_watch_stalls() == 0,
          "a loop that keeps turning is left alone");
    check(slurp(latest)[0] == '\0', "and nothing is written about it");

    tiku_trail("the last thing before the stall");
    the_turn_that_never_ended();
    tiku_watch_beat();          /* it came back, eventually */

    check(tiku_watch_stalls() >= 1, "a turn that stops is noticed");
    said = slurp(latest);
    check(said[0] != '\0', "and written down");
    check(strstr(said, "a turn that has not ended") != NULL,
          "the report says what kind of trouble it is");
    check(strstr(said, "WatchedProgram") != NULL,
          "and which program it was");
    check(strstr(said, "the_turn_that_never_ended") != NULL,
          "the backtrace is the STUCK thread's, not the watcher's");
    check(strstr(said, "the last thing before the stall") != NULL,
          "and what it had been doing is there");
    check(strstr(said, " ms)") != NULL,
          "with how long it had been quiet");

    /* Turning again settles it: the next stall is a new one, not a
     * report every quarter second for ever. */
    {
        int was = tiku_watch_stalls();

        turning_for(20);
        check(tiku_watch_stalls() == was,
              "and a loop that starts turning again is quiet again");
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
