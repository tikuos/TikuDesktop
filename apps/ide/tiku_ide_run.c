/* tiku_ide_run.c -- local interpreter/compiler processes and stopping runs.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static const char * compiler_for(const char *target);

/**
 * @brief The compiler that builds for @p target, or NULL when the table
 *        does not name it.
 *
 * The names are TIKU_PLATFORM values of the TikuOS Makefile; esp32c61 has
 * no entry.  A compiler that is not installed shows up as a "cannot run"
 * message from the build.
 */
static const char *
compiler_for(const char *target)
{
    static const struct { const char *target, *cc; } WHICH[] = {
        { "host",      "cc"                 },
        { "rp2350",    "arm-none-eabi-gcc"  },
        { "ambiq",     "arm-none-eabi-gcc"  },
        { "nordic",    "arm-none-eabi-gcc"  },
        { "stm32n6",   "arm-none-eabi-gcc"  },
        { "ra8p1",     "arm-none-eabi-gcc"  },
        { "msp430",    "msp430-elf-gcc"     }
    };
    size_t i;

    for (i = 0; i < sizeof WHICH / sizeof WHICH[0]; i++) {
        if (strcmp(target, WHICH[i].target) == 0) {
            return WHICH[i].cc;
        }
    }
    return NULL;
}

/**
 * @brief Stop the run: a local one is killed and @p why is added to the
 *        messages; a board is asked to stop; a local debug run ends.
 *
 * Stop calls this, and so does tiku_ide_tick() when a local run passes
 * IDE_RUN_LIMIT_US.
 */
void
tiku_ide_run_stop(ide_t *st, const char *why)
{
    if (!st->running) {
        return;
    }
    if (st->local_debug) {
        tiku_ide_debug_cleanup(st);
        st->running = st->asking = 0;
        return;
    }
    if (st->board[0] != '\0') {
        /* A board run is asked to stop, not killed: there is no child,
         * and kill() with st->child of 0 would signal this process's
         * own group. */
        if (st->services->board_stop != NULL) {
            (void)st->services->board_stop(st->services->ctx);
        }
        /* The shell reports when the run has stopped, but a board that
         * is gone never answers: tiku_ide_tick() releases the run
         * IDE_BOARD_STOP_US after the stop is dated, so the page can be
         * edited again. */
        if (st->stop_asked == 0) {
            st->stop_asked = -1;        /* the next tick dates it */
        }
        return;
    }
    (void)kill(st->child, SIGTERM);
    (void)waitpid(st->child, NULL, 0);
    st->running = 0;
    (void)close(st->child_fd);
    st->child_fd = -1;
    tiku_ide_message_take(st, why);
    tiku_ide_messages_show(st);
    if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, st->msg_id);
    }
}

/**
 * @brief Run the BASIC file at @p path on this machine with tiku-basic;
 *        tiku_ide_tick() reads its output from a pipe.
 *
 * The file's folder, or /tmp, is the interpreter's VFS sandbox.  Refused
 * while a run is going or when the file does not fit BASIC's lines.
 */
void
tiku_ide_run_file(ide_t *st, const char *path)
{
    char box[560];
    const char *tool;
    char *slash;
    int fds[2];

    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        tiku_ide_project_paint(st);
        return;
    }
    if (!tiku_ide_basic_file_fits(st, path)) {
        edit_t *e = tiku_ide_edit_of_path(st, path);

        if (e) { snprintf(e->feedback, sizeof e->feedback, "%s", st->note); }
        tiku_ide_project_paint(st);
        return;
    }
    snprintf(st->child_file, sizeof st->child_file, "%s", path);
    snprintf(box, sizeof box, "%s", path);
    slash = strrchr(box, '/');
    if (slash != NULL && slash != box) {
        *slash = '\0';
    } else {
        snprintf(box, sizeof box, "/tmp");
    }
    /* TIKU_BASIC, when set, must be an absolute path, since a runner
     * starts the application in the person's home directory; without
     * it, tiku-basic is looked up on PATH. */
    tool = getenv("TIKU_BASIC");
    if (tool == NULL || tool[0] == '\0') {
        tool = "tiku-basic";
    }
    if (pipe(fds) != 0) {
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execlp(tool, tool, path, box, (char *)NULL);
        /* A tool that cannot be run is reported through the pipe, as a
         * message. */
        fprintf(stderr, "cannot run %s\n", tool);
        _exit(127);
    }
    st->child_numbered = 1;     /* BASIC names its own lines */
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->run_from = 0;           /* the first tick says when */
    st->stop_asked = 0;
    st->nmsg = 0;
    st->msg_problems = 1;
    st->partial[0] = '\0';
    /* tiku_ide_messages_show() opens the Messages window once there are
     * messages. */
    (void)tiku_list_set_count(&st->msg_list, 0);
    tiku_ide_messages_paint(st);
    tiku_ide_problems_show(st);
}

/**
 * @brief Check the project's .c files with its target's compiler
 *        (-fsyntax-only) and show what the compiler says.
 *
 * Build makes no image.  A project with no target, a target with no
 * compiler here, or no .c files gets a message saying so instead.
 */
void
tiku_ide_build_project(ide_t *st)
{
    const char *cc;

#ifdef TIKU_IDE_C
    /* TikuC builds for host when the project names no target. */
    if (st->proj.target[0] == '\0') {
        snprintf(st->proj.target, sizeof st->proj.target, "host");
    }
#endif
    cc = compiler_for(st->proj.target);
    char inc[560];
    const char *argv[IDE_ROWS + 8];
    int argc = 0;
    int fds[2];
    int i, any = 0;

    if (st->running) {
        return;
    }
    st->nmsg = 0;
    st->msg_problems = 1;
    st->partial[0] = '\0';
    st->child_numbered = 0;     /* a compiler counts the file's lines */
    (void)tiku_list_set_count(&st->msg_list, 0);
    tiku_ide_problems_show(st);
    if (st->proj.target[0] == '\0') {
        tiku_ide_message_take(st, "? this project does not say what it is for");
        tiku_ide_messages_show(st);
        return;
    }
    if (cc == NULL) {
        char says[160];

        snprintf(says, sizeof says,
                 "? nothing here knows how to build for %s",
                 st->proj.target);
        tiku_ide_message_take(st, says);
        tiku_ide_messages_show(st);
        return;
    }
    snprintf(inc, sizeof inc, "-I%s", st->proj.dir);
    argv[argc++] = cc;
    argv[argc++] = "-fsyntax-only";
    argv[argc++] = inc;
    for (i = 0; i < st->proj.nrow && argc < IDE_ROWS + 6; i++) {
        const prow_t *row = &st->proj.row[i];
        size_t n = strlen(row->name);

        if (row->heading || n < 3u ||
            strcmp(row->name + n - 2, ".c") != 0) {
            continue;
        }
        argv[argc++] = row->path;
        any = 1;
    }
    argv[argc] = NULL;
    if (!any) {
        tiku_ide_message_take(st, "? this project has no C in it to build");
        tiku_ide_messages_show(st);
        return;
    }
    if (pipe(fds) != 0) {
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execvp(cc, (char *const *)argv);
        fprintf(stderr, "cannot run %s\n", cc);
        _exit(127);
    }
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->run_from = 0;
    st->stop_asked = 0;
    st->child_file[0] = '\0';
    tiku_ide_messages_paint(st);
}
