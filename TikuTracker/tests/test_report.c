/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_report.c - a program killed by a fault says what it was doing.
 *
 * The report is written by a signal handler, so it is asked for the
 * things a handler can get wrong: that it is written at all, that it
 * names the signal, that it holds the trail up to the fault, and that
 * the program still dies of what killed it -- a handler that swallowed
 * the signal would cost the core somebody wants.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_report.h"
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

/*
 * Zero, but not a constant the compiler can fold: a plain store through
 * a null pointer is undefined, and at -O2 the whole function goes.
 */
static volatile intptr_t nowhere;

/** @brief Recurse until the stack is gone; -O0 so it is not turned into
 *         a loop that never overflows. */
static int __attribute__((noinline, optimize("O0")))
eat_the_stack(volatile int x)
{
    volatile char pad[4096];

    pad[0] = (char)x;
    return eat_the_stack(x + 1) + pad[0];
}

/*
 * Not static, so that -rdynamic puts the name where a backtrace can
 * find it.  A static one shows as the module and an offset, which is
 * what addr2line is for.
 */
void __attribute__((noinline))
the_place_it_happened(void)
{
    *(volatile int *)nowhere = 1;   /* the fault this test is about */
}

int
main(int argc, char **argv)
{
    char dir[] = "/tmp/tiku_reportXXXXXX";
    char latest[512];
    const char *said;
    pid_t child;
    int status = 0;

        if (argc > 1 && strcmp(argv[1], "--overflow") == 0) {
        /* The crash a handler is most likely to be unable to catch: the
         * stack running out.  It must run on a stack of its own. */
        tiku_report_install("OverflowProgram", argv[2]);
        tiku_trail("about to run out of stack");
        return eat_the_stack(0);
    }
    if (argc > 1 && strcmp(argv[1], "--crash") == 0) {
        /* The child: arm, leave a trail, and fault. */
        tiku_report_install("TestProgram", argv[2]);
                tiku_trail("an early step");
        tiku_trail("the step it was on");
        the_place_it_happened();
        _exit(0);               /* not reached */
    }

    printf("a report of a fault\n");
    if (mkdtemp(dir) == NULL) {
        printf("  [SKIP] no temporary folder\n");
        return 0;
    }
    snprintf(latest, sizeof latest, "%s/latest-crash.txt", dir);

    child = fork();
    if (child == 0) {
        (void)execl(argv[0], argv[0], "--crash", dir, (char *)NULL);
        _exit(127);
    }
    (void)waitpid(child, &status, 0);

    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
          "the program still dies of what killed it, so a core can drop");

    said = slurp(latest);
    check(said[0] != '\0', "and leaves a report behind");
    check(strstr(said, "SIGSEGV") != NULL,
          "which names the signal that stopped it");
    check(strstr(said, "TestProgram") != NULL,
          "and the program it was");
        check(strstr(said, "the_place_it_happened") != NULL,
          "the backtrace reaches the place it happened");
        check(strstr(said, "where it was:") < strstr(said, "what it was doing:"),
          "the report says where it was before what it was doing");
    check(strstr(said, "the last thing it did:") != NULL &&
          strstr(said, "the last thing it did:") < strstr(said, "where it was:"),
          "and what it did last comes before either, where it is seen");
        {
        /* Within the whole trail, which comes after the summary at the
         * top -- where the last step is quoted on its own. */
        const char *all = strstr(said, "what it was doing:");

        check(all != NULL && strstr(all, "an early step") != NULL &&
              strstr(all, "the step it was on") != NULL,
              "and what it was doing is there, in the order it did it");
        check(all != NULL && strstr(all, "an early step") <
              strstr(all, "the step it was on"),
              "oldest first, so the last line is the last thing that "
              "happened");
    }

        {
        /* The audit's finding: a stack-overflow crash still leaves a
         * report, because the handler runs on a stack of its own. */
        pid_t so = fork();
        int st2 = 0;

        if (so == 0) {
            (void)execl(argv[0], argv[0], "--overflow", dir, (char *)NULL);
            _exit(127);
        }
        (void)waitpid(so, &st2, 0);
        said = slurp(latest);
        check(WIFSIGNALED(st2) && WTERMSIG(st2) == SIGSEGV,
              "a stack-overflow crash still dies of its signal");
        check(strstr(said, "OverflowProgram") != NULL &&
              strstr(said, "about to run out of stack") != NULL,
              "and still leaves a report, though the stack was gone");
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
