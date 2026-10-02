/* tiku_ide_run.c -- local interpreter/compiler processes and stopping runs.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static const char *
compiler_for(const char *target);

/**
 * @brief The compiler a target is built with.
 *
 * The names the tree's own Makefile already detects, so a project says
 * what it is FOR and the toolchain follows from that rather than from
 * a path somebody typed.  A target this build has never heard of gets
 * no compiler and is told so; a compiler that is not installed says so
 * itself, through the same road every other failure takes.
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
 * @brief End a run by hand: the same road the time bound takes.
 *
 * A program waiting on INPUT, or printing for ever, is somebody's to
 * stop NOW rather than when the bound gives up on it; the message says
 * which way it ended.
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
        /* Somebody else's program: asked to stop, not killed.  A kill
         * here would signal this process's own group -- there is no
         * child, so st->child is nobody. */
        if (st->services->board_stop != NULL) {
            (void)st->services->board_stop(st->services->ctx);
        }
        /* The shell says when it has stopped -- unless the board is
         * gone, and then nobody ever will: the tick lets the run go
         * here after the grace below, so Stop always ends in a page
         * that can be edited again. */
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
    /* ABSOLUTE, because a runner starts an application in the person's
     * home rather than wherever the desktop was built. */
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
        /* A tool that is not installed says so through the same pipe
         * everything else does, and becomes a message like any other. */
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
    /* No window yet: a run that says nothing has nothing to show, and
     * a message window standing empty is furniture. */
    (void)tiku_list_set_count(&st->msg_list, 0);
    tiku_ide_messages_paint(st);
    tiku_ide_problems_show(st);
}

/**
 * @brief Ask the compiler the project's target names whether its C is
 *        good, and say whatever it says.
 *
 * What BUILD means, today: the check, not the image.  Making an image
 * for a board needs a toolchain for that board, and a project asking
 * for one this machine has not got is told so rather than shown a
 * silence it could mistake for success.
 */
void
tiku_ide_build_project(ide_t *st)
{
    const char *cc;

#ifdef TIKU_IDE_C
    /* No target line means this machine: the check is the point. */
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
