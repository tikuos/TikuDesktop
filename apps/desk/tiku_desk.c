/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk.c - the program that is still standing when the desk is not.
 *
 * It starts TikuDesktop with its stderr kept, waits, and when the desk
 * ends badly writes down how -- the signal, the status, what the desk
 * last printed, and where its own account of itself is -- then starts it
 * once more, told where that account is, so the first thing on the
 * screen says what happened.  A desk that ends badly twice in a minute
 * is left down, with the reason on this program's stderr.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tiku_report.h"

#define LOG_LIMIT       (1024L * 1024L)     /* desk.log rolls over at */
#define TAIL_LINES      200
#define TAIL_ROOM       (64 * 1024)
#define SOON_S          60                  /* a second end this soon
                                             * after a start is a loop,
                                             * not a mishap */

static pid_t child;
static char  folder[512];
static volatile sig_atomic_t asked_to_end;   /* somebody ended US */

/*---------------------------------------------------------------------------*/
/* WHERE THINGS ARE                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief The desk to run: $TIKU_DESK when set (the harness names the
 *        development flavour so), else the one beside this program,
 *        else "TikuDesktop" on the PATH.
 */
static void
find_desk(const char *argv0, char *out, size_t max)
{
    const char *named = getenv("TIKU_DESK");
    char dir[512];
    char *slash;

    if (named != NULL && named[0] != '\0') {
        snprintf(out, max, "%s", named);
        return;
    }
    snprintf(dir, sizeof dir, "%s", argv0);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
        snprintf(out, max, "%s/TikuDesktop", dir);
        if (access(out, X_OK) == 0) {
            return;
        }
    }
    snprintf(out, max, "TikuDesktop");
}

/** @brief mkdir -p, for the reports folder on a fresh home. */
static void
make_path(const char *path)
{
    char part[512];
    size_t i;

    snprintf(part, sizeof part, "%s", path);
    for (i = 1; part[i] != '\0'; i++) {
        if (part[i] == '/') {
            part[i] = '\0';
            (void)mkdir(part, 0700);
            part[i] = '/';
        }
    }
    (void)mkdir(part, 0700);
}

/*---------------------------------------------------------------------------*/
/* THE LOG                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Open desk.log for the desk's stderr, rolling the old one over
 *        to desk.log.1 when it has grown past its limit.
 */
static int
open_log(void)
{
    char path[600], old[600];
    struct stat st;

    snprintf(path, sizeof path, "%s/desk.log", folder);
    snprintf(old, sizeof old, "%s/desk.log.1", folder);
    if (stat(path, &st) == 0 && st.st_size > LOG_LIMIT) {
        (void)rename(path, old);
    }
    return open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
}

/** @brief The last lines of desk.log, for the report. */
static size_t
log_tail(char *out, size_t max)
{
    char path[600];
    FILE *f;
    long size, from;
    size_t n, lines = 0u, i;

    snprintf(path, sizeof path, "%s/desk.log", folder);
    f = fopen(path, "r");
    if (f == NULL) {
        out[0] = '\0';
        return 0u;
    }
    (void)fseek(f, 0L, SEEK_END);
    size = ftell(f);
    from = (size > (long)max - 1L) ? size - ((long)max - 1L) : 0L;
    (void)fseek(f, from, SEEK_SET);
    n = fread(out, 1u, max - 1u, f);
    out[n] = '\0';
    (void)fclose(f);
    /* Count back TAIL_LINES line ends and start there. */
    for (i = n; i > 0u; i--) {
        if (out[i - 1u] == '\n' && ++lines > TAIL_LINES) {
            memmove(out, out + i, n - i + 1u);
            n -= i;
            break;
        }
    }
    return n;
}

/*---------------------------------------------------------------------------*/
/* THE REPORT                                                                */
/*---------------------------------------------------------------------------*/

/** @brief Write exit-<time>.txt for a desk that ended badly. */
static void
write_exit(int status, char *path, size_t max)
{
    static char tail[TAIL_ROOM];
    char latest[600];
    struct tm when;
    time_t now = time(NULL);
    FILE *f;

    (void)localtime_r(&now, &when);
    snprintf(path, max, "%s/exit-%04d%02d%02d-%02d%02d%02d.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec);
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "TikuDesktop\nended: %04d-%02d-%02d %02d:%02d:%02d\n",
            when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
            when.tm_hour, when.tm_min, when.tm_sec);
    if (WIFSIGNALED(status)) {
        fprintf(f, "stopped by: signal %d (%s)\n", WTERMSIG(status),
                strsignal(WTERMSIG(status)));
    } else {
        fprintf(f, "stopped by: exit %d\n", WEXITSTATUS(status));
    }
    /* The desk's own account, if it managed one, is the better one. */
    snprintf(latest, sizeof latest, "%s/latest-crash.txt", folder);
    if (access(latest, R_OK) == 0) {
        fprintf(f, "its own account: %s\n", latest);
    }
    snprintf(latest, sizeof latest, "%s/latest-stall.txt", folder);
    if (access(latest, R_OK) == 0) {
        fprintf(f, "a stall before it: %s\n", latest);
    }
    fprintf(f, "\nthe last it printed:\n");
    if (log_tail(tail, sizeof tail) > 0u) {
        fputs(tail, f);
    }
    (void)fclose(f);
    snprintf(latest, sizeof latest, "%s/latest-exit.txt", folder);
    (void)unlink(latest);
    (void)symlink(path, latest);
}

/*---------------------------------------------------------------------------*/
/* RUNNING THE DESK                                                          */
/*---------------------------------------------------------------------------*/

/** @brief What is sent to this program is passed on to the desk. */
static void
pass_on(int sig)
{
    asked_to_end = 1;
    if (child > 0) {
        (void)kill(child, sig);
    }
}

/**
 * @brief Start the desk and wait for it.
 *
 * @param after the exit report to tell it about, or NULL.
 * @return the wait status, or -1 when it could not be started.
 */
static int
run_desk(const char *desk, int argc, char **argv, const char *after)
{
    /* argv[0], the caller's arguments, -after, its path, the end. */
    const char **args = calloc((size_t)argc + 4u, sizeof *args);
    int status = 0, i, n = 0, log;

    if (args == NULL) {
        return -1;
    }
    args[n++] = desk;
    for (i = 1; i < argc; i++) {
        args[n++] = argv[i];
    }
    if (after != NULL) {
        args[n++] = "-after";
        args[n++] = after;
    }
    args[n] = NULL;

    log = open_log();
    child = fork();
    if (child == 0) {
        /* Only what is needed to become the desk: no allocation, no
         * stdio, between here and exec. */
        if (log >= 0) {
            (void)dup2(log, STDERR_FILENO);
            (void)close(log);
        }
        execvp(desk, (char *const *)(void *)args);
        _exit(127);
    }
    if (log >= 0) {
        (void)close(log);
    }
    free(args);
    if (child < 0) {
        return -1;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    child = 0;
    return status;
}

/**
 * @brief Ended the way a desk does when somebody quit it -- or when
 *        somebody ended this program, whose end the desk shared.
 */
static int
ended_well(int status)
{
    return asked_to_end ||
           (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int
main(int argc, char **argv)
{
    char desk[512], report[600];
    time_t began;
    int status;
    struct sigaction sa;

    tiku_report_folder(NULL, folder, sizeof folder);
    make_path(folder);
    find_desk(argv[0], desk, sizeof desk);

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = pass_on;
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);

    began = time(NULL);
    status = run_desk(desk, argc, argv, NULL);
    if (status < 0) {
        fprintf(stderr, "tiku-desk: could not start %s\n", desk);
        return 127;
    }
    if (ended_well(status)) {
        return 0;
    }
    write_exit(status, report, sizeof report);
    fprintf(stderr, "tiku-desk: the desk ended badly; see %s\n", report);

    /* Once more, told why.  A desk that ends badly again soon after is
     * not restarted into a loop; it is left down with the reason here. */
    began = time(NULL);
    status = run_desk(desk, argc, argv, report);
    if (status >= 0 && ended_well(status)) {
        return 0;
    }
    if (status >= 0) {
        write_exit(status, report, sizeof report);
    }
    fprintf(stderr, "tiku-desk: ended badly again%s; not started a "
                    "third time.  See %s\n",
            (time(NULL) - began < SOON_S) ? " within a minute" : "",
            report);
    return 1;
}
