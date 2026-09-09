/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_report.c - what a program was doing when it was killed.
 *
 * Everything a handler would have to compose is composed BEFORE the
 * fault: the path, the header, the program's name.  What is left for
 * the signal itself is opening a file and writing bytes, which is all
 * a handler may safely do.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_report.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* WHAT IS READY BEFORE THE FAULT                                            */
/*---------------------------------------------------------------------------*/

#define PATH_ROOM  512
#define FRAMES     64

static char  report_path[PATH_ROOM];
static char  latest_path[PATH_ROOM];
static char  header[512];           /* program, pid, when it started */
static int   armed;
static void *frames[FRAMES];        /* filled in the handler; never freed */
/* The handler runs HERE, not on the stack that faulted: a crash that IS
 * the stack running out would otherwise fault the handler too, and
 * leave no report -- which is the most a report is for. */
static char  alt_stack[64 * 1024];

/** @brief The signals worth a report: the ones nothing recovers from. */
static const int FATAL[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };

/** @brief Digits of @p v into @p out, and where the text now ends. */
static size_t
put_num(char *out, size_t at, long v)
{
    char digits[24];
    size_t n = 0u;

    if (v < 0) {
        out[at++] = '-';
        v = -v;
    }
    do {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0 && n < sizeof digits);
    while (n > 0u) {
        out[at++] = digits[--n];
    }
    return at;
}

/** @brief @p s into @p out, and where the text now ends. */
static size_t
put_str(char *out, size_t at, const char *s)
{
    while (s != NULL && *s != '\0') {
        out[at++] = *s++;
    }
    return at;
}

/** @brief The name of @p sig, since a number is a thing to look up. */
static const char *
named(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    case SIGABRT: return "SIGABRT";
    default:      return "signal";
    }
}

/*---------------------------------------------------------------------------*/
/* THE REPORT                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write the whole report to @p path.
 *
 * Only open, write and close: no allocation, no formatting, nothing
 * that takes a lock.  @p why is a phrase the caller already has.
 */
static void
write_report(const char *path, const char *why, int depth)
{
    char line[256];
    size_t at;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd < 0) {
        return;
    }
    (void)!write(fd, header, strlen(header));
        at = put_str(line, 0u, "stopped by: ");
    at = put_str(line, at, why);
    at = put_str(line, at, "\nlines it had written: ");
    at = put_num(line, at, (long)tiku_trail_written());
    line[at++] = '\n';
    (void)!write(fd, line, at);

        /* The last thing it did, at the top: a reader opening this wants
     * that before forty frames of where it was standing. */
    at = put_str(line, 0u, "the last thing it did:\n");
    (void)!write(fd, line, at);
    tiku_trail_write_last(fd);

    at = put_str(line, 0u, "\nwhere it was:\n");
    (void)!write(fd, line, at);
    if (depth > 0) {
        backtrace_symbols_fd(frames, depth, fd);
    }
    at = put_str(line, 0u, "\nwhat it was doing:\n");
    (void)!write(fd, line, at);
    tiku_trail_write(fd);
    (void)close(fd);
}

/** @brief The fault itself: write it down, then die as it would have. */
static void
took_signal(int sig)
{
    int depth = backtrace(frames, FRAMES);

    write_report(report_path, named(sig), depth);
    write_report(latest_path, named(sig), depth);
    /* And on, as though nothing had caught it: a core for whoever wants
     * one, and a parent that is told what killed this. */
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

/*---------------------------------------------------------------------------*/
/* ARMING                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Make @p dir and everything above it, as far as it can. */
static void
make_path(const char *dir)
{
    char work[PATH_ROOM];
    size_t i;

    snprintf(work, sizeof work, "%s", dir);
    for (i = 1u; work[i] != '\0'; i++) {
        if (work[i] == '/') {
            work[i] = '\0';
            (void)mkdir(work, 0755);
            work[i] = '/';
        }
    }
    (void)mkdir(work, 0755);
}

void
tiku_report_install(const char *program, const char *dir)
{
        char folder[PATH_ROOM / 2];
    const char *home = getenv("HOME");
    const char *env = getenv("TIKU_TRK_REPORTS");
    struct tm when;
    time_t now = time(NULL);
    size_t i;

    if (armed) {
        return;
    }
    if (dir == NULL) {
        dir = env;
    }
    if (dir != NULL && dir[0] != '\0') {
        snprintf(folder, sizeof folder, "%s", dir);
    } else {
        snprintf(folder, sizeof folder, "%s/.config/tracker/reports",
                 (home != NULL) ? home : "/tmp");
    }
    make_path(folder);

    /* The name is settled now, while formatting is still allowed. */
    (void)localtime_r(&now, &when);
    snprintf(report_path, sizeof report_path,
             "%s/crash-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec,
             (program != NULL) ? program : "program", (long)getpid());
    snprintf(latest_path, sizeof latest_path, "%s/latest-crash.txt", folder);
    snprintf(header, sizeof header,
             "%s\nstarted: %04d-%02d-%02d %02d:%02d:%02d\npid: %ld\n",
             (program != NULL) ? program : "program",
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec, (long)getpid());

        /* The unwinder loads what it needs on its first use; asked once
     * here, so the fault's own backtrace allocates nothing. */
    (void)backtrace(frames, FRAMES);

    {
        stack_t ss;

        ss.ss_sp = alt_stack;
        ss.ss_size = sizeof alt_stack;
        ss.ss_flags = 0;
        (void)sigaltstack(&ss, NULL);
    }
    for (i = 0u; i < sizeof FATAL / sizeof FATAL[0]; i++) {
        struct sigaction sa;

        memset(&sa, 0, sizeof sa);
        sa.sa_handler = took_signal;
        /* ONSTACK so a stack-overflow crash is caught; RESETHAND so a
         * fault INSIDE the handler dies rather than looping; NODEFER so
         * the re-raise below reaches the default action at once. */
        sa.sa_flags = SA_RESETHAND | SA_NODEFER | SA_ONSTACK;
        (void)sigemptyset(&sa.sa_mask);
        (void)sigaction(FATAL[i], &sa, NULL);
    }
    armed = 1;
    tiku_trail("reports go to %s", report_path);
}

const char *
tiku_report_path(void)
{
    return report_path;
}

const char *
tiku_report_latest(void)
{
    return latest_path;
}

void
tiku_report_now(const char *why)
{
    int depth;

    if (!armed) {
        return;
    }
    depth = backtrace(frames, FRAMES);
    write_report(report_path, (why != NULL) ? why : "asked for", depth);
    write_report(latest_path, (why != NULL) ? why : "asked for", depth);
}
