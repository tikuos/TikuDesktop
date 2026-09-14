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
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "tiku_sys.h"
#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* WHAT IS READY BEFORE THE FAULT                                            */
/*---------------------------------------------------------------------------*/

#define PATH_ROOM  512
#define FRAMES     64

static char  report_path[PATH_ROOM];
static char  latest_path[PATH_ROOM];
static char  stall_path[PATH_ROOM];
static char  latest_stall[PATH_ROOM];
static char  header[512];           /* program, pid, when it started */
static char  folder_path[PATH_ROOM / 2];
static char  program_name[64];
static int   armed;
static volatile sig_atomic_t asked;
static void *frames[FRAMES];        /* filled in the handler; never freed */
/*
 * The instruction that faulted, taken from the signal's own context.
 * An array of one because it is printed through backtrace_symbols_fd(),
 * which names it the same way it names the frames below it.
 */
static void *fault_pc[1];
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
write_report(const char *path, const char *why, long num, const char *unit,
             int depth)
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
    if (unit != NULL) {
        at = put_str(line, at, " (");
        at = put_num(line, at, num);
        at = put_str(line, at, unit);
        line[at++] = ')';
    }
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
    /* The faulting instruction first, and from the context rather than
     * from the walk: a function that only stores keeps no frame pointer
     * on arm64, so the walk steps from the trampoline straight to the
     * caller and the frame a reader most wants is the one missing. */
    if (fault_pc[0] != NULL) {
        backtrace_symbols_fd(fault_pc, 1, fd);
        /* Two lists, and each is numbered from nothing by the same call:
         * said out loud, so the second 0 does not read as the first one
         * again. */
        at = put_str(line, 0u, "-- and the frames above it --\n");
        (void)!write(fd, line, at);
    }
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
took_signal(int sig, siginfo_t *info, void *context)
{
    int depth;

    (void)info;
    fault_pc[0] = tiku_sys_fault_pc(context);
    depth = backtrace(frames, FRAMES);

    write_report(report_path, named(sig), 0L, NULL, depth);
    write_report(latest_path, named(sig), 0L, NULL, depth);
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
tiku_report_folder(const char *dir, char *out, size_t max)
{
    const char *home = getenv("HOME");

    if (dir == NULL) {
        dir = getenv("TIKU_TRK_REPORTS");
    }
    if (dir != NULL && dir[0] != '\0') {
        snprintf(out, max, "%s", dir);
    } else {
        snprintf(out, max, "%s/.config/tracker/reports",
                 (home != NULL) ? home : "/tmp");
    }
}

void
tiku_report_install(const char *program, const char *dir)
{
    char folder[PATH_ROOM / 2];
    struct tm when;
    time_t now = time(NULL);
    size_t i;

    if (armed) {
        return;
    }
        tiku_report_folder(dir, folder, sizeof folder);
    make_path(folder);
    snprintf(folder_path, sizeof folder_path, "%s", folder);
    snprintf(program_name, sizeof program_name, "%s",
             (program != NULL) ? program : "program");

    /* The name is settled now, while formatting is still allowed. */
    (void)localtime_r(&now, &when);
    snprintf(report_path, sizeof report_path,
             "%s/crash-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec,
             (program != NULL) ? program : "program", (long)getpid());
    snprintf(latest_path, sizeof latest_path, "%s/latest-crash.txt", folder);
    snprintf(stall_path, sizeof stall_path,
             "%s/stall-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec,
             (program != NULL) ? program : "program", (long)getpid());
    snprintf(latest_stall, sizeof latest_stall, "%s/latest-stall.txt",
             folder);
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
        sa.sa_sigaction = took_signal;
        /* ONSTACK so a stack-overflow crash is caught; RESETHAND so a
         * fault INSIDE the handler dies rather than looping; NODEFER so
         * the re-raise below reaches the default action at once; SIGINFO
         * for the context, which is where the faulting address is. */
        sa.sa_flags = SA_RESETHAND | SA_NODEFER | SA_ONSTACK | SA_SIGINFO;
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

const char *
tiku_report_stall_path(void)
{
    return stall_path;
}

void
tiku_report_stall(long ms, int with_stack)
{
    int depth = 0;

    if (!armed) {
        return;
    }
    /* Only the stuck thread's own stack says anything: a watchdog's
     * would be a picture of the watchdog waiting. */
    if (with_stack) {
        depth = backtrace(frames, FRAMES);
    }
    write_report(stall_path, "a turn that has not ended", ms, " ms", depth);
    write_report(latest_stall, "a turn that has not ended", ms, " ms",
                 depth);
}

void
tiku_report_now(const char *why)
{
    int depth;

    if (!armed) {
        return;
    }
    depth = backtrace(frames, FRAMES);
    write_report(report_path, (why != NULL) ? why : "asked for", 0L, NULL,
                 depth);
    write_report(latest_path, (why != NULL) ? why : "asked for", 0L, NULL,
                 depth);
}

/*---------------------------------------------------------------------------*/
/* ON DEMAND                                                                 */
/*---------------------------------------------------------------------------*/

static void
was_asked(int sig)
{
    (void)sig;
    asked = 1;
}

void
tiku_report_ask_install(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = was_asked;
    sa.sa_flags = SA_RESTART;
    (void)sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGUSR2, &sa, NULL);
}

int
tiku_report_asked(void)
{
    int was = asked;

    asked = 0;
    return was;
}

int
tiku_report_write(const char *why, void (*snapshot)(FILE *, void *),
                  void *ctx, char *out, size_t max)
{
    char latest[PATH_ROOM];
    struct tm when;
    time_t now = time(NULL);
    FILE *f;

    out[0] = '\0';
    if (!armed) {
        return -1;
    }
    (void)localtime_r(&now, &when);
    snprintf(out, max, "%s/report-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt",
             folder_path, when.tm_year + 1900, when.tm_mon + 1,
             when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec,
             program_name, (long)getpid());
    f = fopen(out, "w");
    if (f == NULL) {
        out[0] = '\0';
        return -1;
    }
    fputs(header, f);
    fprintf(f, "asked for: %s\n", (why != NULL) ? why : "a report");
    fprintf(f, "lines it had written: %lu\n\n", tiku_trail_written());
    if (snapshot != NULL) {
        snapshot(f, ctx);
    }
    fprintf(f, "\nwhat it was doing:\n");
    (void)fflush(f);
    tiku_trail_write(fileno(f));
    (void)fclose(f);
    snprintf(latest, sizeof latest, "%s/latest-report.txt", folder_path);
    (void)unlink(latest);
    if (symlink(out, latest) != 0) {
        latest[0] = '\0';           /* the file stands on its own */
    }
    return 0;
}


/** @brief The moment in a report's name, as a string that sorts. */
static void
report_stamp(const char *leaf, char *out, size_t max)
{
    const char *dash = strchr(leaf, '-');

    snprintf(out, max, "%.15s", (dash != NULL) ? dash + 1 : leaf);
}

int
tiku_report_list(char out[][512], int max)
{
    char folder[PATH_ROOM / 2];
    DIR *d;
    struct dirent *e;
    char when[64][16];
    int n = 0, i, j;

    if (max > 64) {
        max = 64;
    }
    tiku_report_folder(NULL, folder, sizeof folder);
    d = opendir(folder);
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        char stamp[16];

        if (strncmp(e->d_name, "latest-", 7) == 0 ||
            strstr(e->d_name, ".txt") == NULL) {
            continue;
        }
        /* By the moment in the name, newest first: a copied file keeps
         * its account but not its time. */
        report_stamp(e->d_name, stamp, sizeof stamp);
        for (i = 0; i < n && strcmp(when[i], stamp) >= 0; i++) {
        }
        if (i >= max) {
            continue;
        }
        if (n < max) {
            n++;
        }
        for (j = n - 1; j > i; j--) {
            memcpy(when[j], when[j - 1], sizeof when[0]);
            snprintf(out[j], 512, "%s", out[j - 1]);
        }
        memcpy(when[i], stamp, sizeof when[0]);
        snprintf(out[i], 512, "%s/%s", folder, e->d_name);
    }
    (void)closedir(d);
    return n;
}

int
tiku_report_newest(char *out, size_t max)
{
    char one[1][512];

    out[0] = '\0';
    if (tiku_report_list(one, 1) < 1) {
        return -1;
    }
    snprintf(out, max, "%s", one[0]);
    return 0;
}
