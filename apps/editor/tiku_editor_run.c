/* tiku_editor_run.c -- interpreter child process and captured output.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static void out_line(edit_state_t *st, const char *line);

/** @brief Append one finished line of the child's output to the pane. */
static void
out_line(edit_state_t *st, const char *line)
{
    if (st->out_count >= (int)(sizeof st->out / sizeof st->out[0])) {
        /* Full: the oldest line is dropped. */
        memmove(st->out[0], st->out[1],
                sizeof st->out - sizeof st->out[0]);
        st->out_count--;
    }
    snprintf(st->out[st->out_count], sizeof st->out[0], "%s", line);
    st->out_count++;
}

/**
 * @brief Run the document with tiku-basic, the interpreter a board runs;
 *        tiku_editor_tick() reads its output from a pipe.
 *
 * tiku-basic reads a file, so the document is saved first, or written to
 * a scratch file in /tmp when it has no name.  The program's VFS sandbox
 * is the file's folder, so a program that writes /led writes beside its
 * source.
 */
void
tiku_editor_run_program(edit_state_t *st)
{
    char file[560];
    char box[560];
    const char *tool;
    int fds[2];
    const char *slash;

    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        return;
    }
    if (st->path[0] != '\0') {
        if (!tiku_editor_save(st)) return;
        snprintf(file, sizeof file, "%s", st->path);
    } else {
        FILE *f;
        if (st->scratch[0]) unlink(st->scratch);
        snprintf(st->scratch, sizeof st->scratch, "/tmp/tiku-edit-run-XXXXXX");
        int fd = mkstemp(st->scratch);
        snprintf(file, sizeof file, "%s", st->scratch);
        f = fd < 0 ? NULL : fdopen(fd, "wb");
        if (f == NULL) {
            if (fd >= 0) close(fd);
            snprintf(st->note, sizeof st->note, "cannot write the scratch");
            return;
        }
        int ok = tiku_editor_write_document(st, f);
        if (fclose(f)) ok = 0;
        if (!ok) {
            snprintf(st->note, sizeof st->note, "Could not write the run snapshot.");
            return;
        }
    }
    snprintf(box, sizeof box, "%s", file);
    slash = strrchr(box, '/');
    if (slash != NULL && slash != box) {
        box[slash - box] = '\0';
    } else {
        snprintf(box, sizeof box, "/tmp");
    }
    tool = getenv("TIKU_BASIC");
    if (tool == NULL || tool[0] == '\0') {
        tool = "tiku-basic";
    }
    if (pipe(fds) != 0) {
        snprintf(st->note, sizeof st->note, "cannot make the pipe");
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        snprintf(st->note, sizeof st->note, "cannot start the run");
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execlp(tool, tool, file, box, (char *)NULL);
        /* Reached only when the interpreter cannot be run. */
        fprintf(stderr, "cannot run %s\n", tool);
        _exit(127);
    }
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->out_count = 0;
    st->out_partial[0] = '\0';
    snprintf(st->note, sizeof st->note, "running");
}

/** @brief Periodic work: count the rows of lines still to be wrapped,
 *         repaint for what the panes changed since the last frame,
 *         publish the menus when the document or its commands changed,
 *         clear a footer note after 8 s, and read the run's output. */
void
tiku_editor_tick(void *state, int64_t now_us)
{
    const unsigned menu_changes = TIKU_CODEVIEW_CHANGE_DOCUMENT |
                                  TIKU_CODEVIEW_CHANGE_COMMANDS;
    edit_state_t *st = state;
    tiku_event_t count = {.type = TIKU_EVENT_TICK};
    char buf[256];
    ssize_t n;
    int heard = 0, counted = 0;
    unsigned changes = 0;

    for (int i = 0; st && i < 2 && st->panes[i]; i++)
        if (tiku_codeview_wrapping(st->panes[i]))
            counted |= tiku_codeview_event(st->panes[i], &count);
    /* A paint takes in the panes' changes, so these came after the last
     * frame. */
    for (int i = 0; st && i < 2 && st->panes[i]; i++)
        changes |= tiku_codeview_changes(st->panes[i]);
    if (st && ((changes | st->menus_due) & menu_changes))
        tiku_editor_publish(st);
    if (counted || changes)
        tiku_editor_paint(st);
    if (st && strcmp(st->note, st->last_note)) {
        snprintf(st->last_note, sizeof st->last_note, "%s", st->note);
        st->note_until = now_us + 8000000;
    }
    if (st && st->note[0] && st->note_until && now_us >= st->note_until) {
        st->note[0] = st->last_note[0] = 0;
        tiku_editor_paint(st);
    }
    if (st == NULL || !st->running) {
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n <= 0) {
            break;
        }
        /* A NUL byte is dropped: a line cannot hold one. */
        for (ssize_t i = 0; i < n; i++) {
            size_t used = strlen(st->out_partial);

            if (buf[i] == '\n') {
                out_line(st, st->out_partial);
                st->out_partial[0] = '\0';
            } else if (buf[i] != '\0' && used + 1u < sizeof st->out_partial) {
                st->out_partial[used] = buf[i];
                st->out_partial[used + 1u] = '\0';
            }
        }
        heard = 1;
    }
    if (n == 0) {
        /* The pipe closed: keep the last partial line; once the child is
         * reaped, the note says how the run ended. */
        int status = 0;

        if (st->out_partial[0] != '\0') {
            out_line(st, st->out_partial);
            st->out_partial[0] = '\0';
        }
        pid_t ended = waitpid(st->child, &status, WNOHANG);
        if (!ended) return;  /* the child may still be running */
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        snprintf(st->note, sizeof st->note, "%s",
                 (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                     ? "finished" : "stopped");
        heard = 1;
    }
    if (heard) {
        tiku_editor_publish(st);
        tiku_editor_paint(st);
    }
}
