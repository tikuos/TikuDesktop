/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit.c - a text editor, as one application descriptor.
 *
 * Lines are separate allocations so an edit touches one of them; the
 * caret is a line and a column into that text, never a pixel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_textview.h"
#include "tiku_text_input.h"
#include "tiku_alert.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_dl.h"
#include "tiku_gfx.h"
#include "tiku_syntax.h"
#include "tiku_ui.h"
#include "tiku_field_event.h"
#include "tiku_scroll.h"
#include "tiku_clipboard.h"
#include "tiku_iconpaint.h"
#include "tiku_icons_tiku.h"

#include "tiku_edit_buffer.inl"

#define EDIT_W      560
#define EDIT_H      400
#define STRIP_H     20
#define MARGIN      6

#define CMD_SAVE   1
#define CMD_REVERT 2
#define CMD_QUIT   3
#define CMD_SYNTAX 4
#define CMD_OPEN   5
#define CMD_SAVEAS 6
#define CMD_RUN    7
enum { CMD_NEW = 8, CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE,
       CMD_ALL, CMD_FIND, CMD_NEXT, CMD_PREV, CMD_REPLACE, CMD_GOTO,
       CMD_NUMBERS, CMD_REPLACE_ALL, CMD_INVISIBLES };

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    /*
     * What the painting below wrote down as it went.  The editor does not
     * paint twice for it: the surface records while it is drawn into, so
     * there is one painting path and a stream cannot come to disagree
     * with the picture.  NULL when the services this is running against
     * have no use for one -- linked into a desktop, they do not.
     */
    tiku_dl_t                      *dl;
    uint32_t                        id;
    /* Desktop-sized dynamic document; the legacy shared textview remains
     * unchanged for other applications with different compatibility needs. */
    edit_buffer_t                 tv;
    tiku_text_input_t               commit;
    /* The question the close gesture asks while there are unsaved
     * changes.  The kit's own alert, held over this window's content:
     * an application no longer needs a shell to be able to ask. */
    tiku_alert_t                    ask;
    tiku_rect_t                     ask_frame;
    int                             saved_shown;
    char                            path[512];
    char                            note[128];
    /*
     * Which language the document is written in, or none.  Taken from
     * the file's name when it is opened and offered on the menu after
     * that, because the name is the only thing that can honestly say --
     * and because an editor started with nothing (which is how the
     * desktop starts this one) has no name to go on and the person does.
     */
    tiku_syntax_lang_t              lang;
    /* Which question the shell's panel is answering, when it answers:
     * the ask is not remembered by the panel and the answer arrives
     * later, so what to DO with a path is remembered here. */
    int                             pick_save;
    /*
     * What the last run said, and the run itself while it lives.  The
     * program runs in the interpreter a BOARD runs -- tiku-basic, the
     * kernel's own engine compiled for the host -- as a child on the
     * far end of a pipe, the way the terminal next door holds its
     * shell.  The pane exists while there are lines to show.
     */
    char                            out[48][96];
    int                             out_count;
    int                             running;
    int                             child_fd;
    pid_t                           child;
    char                            out_partial[96];
    int w, h, left, dragging, finding, find_focus, pending;
    int crlf, bom, load_failed, disk_known, numbers, allow_open;
    struct stat disk;
    tiku_field_t field[3];
    tiku_field_input_t field_input[3];
    tiku_scroll_t vertical, horizontal;
    int width_dirty, max_width, measured_cell, measured_height;
    tiku_menuset_t last_menus;
    char scratch[64];
    uint32_t search_id;
    tiku_surface_t *search_surface;
    int search_active, hover_tool, pressed_tool, show_invisibles;
    int match_case, whole_word;
    char last_note[128];
    int64_t note_until;
} edit_state_t;

/* The file named on the command line, before any state exists. */
static char edit_path[512];

static int
out_pane_h(const edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_plain();

    if (st == NULL || (st->out_count == 0 && !st->running)) {
        return 0;               /* no run yet: the page is all editor */
    }
    return 7 * (f->height + 2) + 8;
}

#include "tiku_edit_file.inl"
#include "tiku_edit_view.inl"


/** @brief Append one finished line of the child's output to the pane. */
static void
out_line(edit_state_t *st, const char *line)
{
    if (st->out_count >= (int)(sizeof st->out / sizeof st->out[0])) {
        /* Full: the oldest line goes, because the newest is the one a
         * person watching a run is waiting for. */
        memmove(st->out[0], st->out[1],
                sizeof st->out - sizeof st->out[0]);
        st->out_count--;
    }
    snprintf(st->out[st->out_count], sizeof st->out[0], "%s", line);
    st->out_count++;
}

/**
 * @brief Run the document in the interpreter a board runs.
 *
 * The text is written to its file first (or a scratch one when it has
 * no name yet), because the interpreter reads a FILE -- what runs is
 * what would run tomorrow, not a copy that only ever existed in this
 * window.  The sandbox the program's VFS lands in is the file's own
 * folder: a program that writes /led writes beside its source, where
 * its author can look at it.
 */
static void
run_program(edit_state_t *st)
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
        if (!save(st)) return;
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
        int ok = write_document(st, f);
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
        /* Reached only when the interpreter is not there to run. */
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

/** @brief Hear what the run has said since the last frame. */
static void
edit_tick(void *state, int64_t now_us)
{
    edit_state_t *st = state;
    char buf[256];
    ssize_t n;
    int heard = 0;

    if (st && strcmp(st->note, st->last_note)) {
        snprintf(st->last_note, sizeof st->last_note, "%s", st->note);
        st->note_until = now_us + 8000000;
    }
    if (st && st->note[0] && st->note_until && now_us >= st->note_until) {
        st->note[0] = st->last_note[0] = 0;
        paint(st);
    }
    if (st == NULL || !st->running) {
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        {
            char *p = buf;

            while (*p != '\0') {
                size_t used = strlen(st->out_partial);

                if (*p == '\n') {
                    out_line(st, st->out_partial);
                    st->out_partial[0] = '\0';
                } else if (used + 1u < sizeof st->out_partial) {
                    st->out_partial[used] = *p;
                    st->out_partial[used + 1u] = '\0';
                }
                p++;
            }
        }
        heard = 1;
    }
    if (n == 0) {
        /* The pipe closed: the run is over, and its verdict is worth a
         * line of its own. */
        int status = 0;

        if (st->out_partial[0] != '\0') {
            out_line(st, st->out_partial);
            st->out_partial[0] = '\0';
        }
        pid_t ended = waitpid(st->child, &status, WNOHANG);
        if (!ended) return;  /* Closed stdout is not necessarily process exit. */
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        snprintf(st->note, sizeof st->note, "%s",
                 (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                     ? "finished" : "stopped");
        heard = 1;
    }
    if (heard) {
        publish(st);
        paint(st);
    }
}

#include "tiku_edit_commands.inl"

static int
edit_start(void **state, const tiku_app_services_t *services)
{
    edit_state_t *st = calloc(1, sizeof *st);
    const char *slash;
    char title[64];

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->w = EDIT_W; st->h = EDIT_H; st->numbers = 1;
    st->find_focus = -1; st->child_fd = -1;
    st->hover_tool = st->pressed_tool = -1; st->match_case = 1;
    tiku_icons_tiku_register();
    tiku_scroll_init(&st->vertical, 1);
    tiku_scroll_init(&st->horizontal, tiku_font_mono_cell(0));
    for (int i = 0; i < 3; i++) {
        tiku_field_start(&st->field[i], "");
        tiku_field_input_init(&st->field_input[i]);
    }
    if (!edit_buffer_init(&st->tv)) { free(st); return -1; }
    st->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_DOC);
    /* Only worth keeping when something can use it; a NULL one simply
     * means every frame goes over as pixels, which is what happens
     * linked into a desktop anyway. */
    st->dl = (services->present != NULL) ? tiku_dl_new() : NULL;
    if (st->surface == NULL) {
        edit_buffer_free(&st->tv);
        tiku_dl_free(st->dl);
        free(st);
        return -1;
    }
    const char *document = services->startup_document;
    if (!document) document = edit_path;
    if (strlen(document) >= sizeof st->path) {
        edit_buffer_free(&st->tv);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        tiku_surface_free(st->search_surface);
        free(st);
        return -1;
    }
    snprintf(st->path, sizeof st->path, "%s", document);
    /* The name decides, once, before anything is drawn; after that the
     * menu does.  A file called .bas opens coloured, and everything else
     * opens as what it is -- prose. */
    st->lang = tiku_syntax_of_path(st->path);
    load(st);
    slash = strrchr(st->path, '/');
    snprintf(title, sizeof title, "%.63s",
             (slash != NULL) ? slash + 1
             : (st->path[0] != '\0') ? st->path : "Edit");
    st->id = services->open(services->ctx, title, EDIT_W, EDIT_H);
    if (!st->id) {
        edit_buffer_free(&st->tv);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        tiku_surface_free(st->search_surface);
        free(st);
        return -1;
    }
    if (services->sizeable) services->sizeable(services->ctx, st->id);
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/editor.html");
    }
    paint(st);
    publish(st);
    *state = st;
    return 0;
}

static void
edit_stop(void *state)
{
    edit_state_t *st = state;

    if (st != NULL) {
        if (st->running && st->child > 0) {
            kill(st->child, SIGKILL);
            while (waitpid(st->child, NULL, 0) < 0 && errno == EINTR) {}
        }
        if (st->child_fd >= 0) close(st->child_fd);
        if (st->scratch[0]) unlink(st->scratch);
        if (edit_buffer_modified(&st->tv)) {
            char recovery[] = "/tmp/TikuEdit-recovery-XXXXXX";
            int fd = mkstemp(recovery);
            if (fd >= 0) {
                FILE *f = fdopen(fd, "wb");
                if (f) {
                    int ok = write_document(st, f) && !fflush(f) && !fsync(fd);
                    if (fclose(f)) ok = 0;
                    fprintf(stderr, "Editor recovery %s: %s\n", ok ? "saved" : "incomplete", recovery);
                } else close(fd);
            }
        }
        edit_buffer_free(&st->tv);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        tiku_surface_free(st->search_surface);
        free(st);
    }
}

#include "tiku_edit_input.inl"

const tiku_app_descriptor_t tiku_edit_app = {
    .id = "org.tikuos.edit",
    .name = "Edit",
    .start = edit_start,
    .stop = edit_stop,
    .event = edit_event,
    .event_in = edit_event_in,
    .pick = edit_pick,
    .tick = edit_tick,
    .picked = edit_picked,
    .closed = edit_closed
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_edit_app
};
#endif

#ifndef TIKU_APP_EMBED
int
main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(edit_path, sizeof edit_path, "%s", argv[1]);
    }
    return tiku_client_run(&tiku_edit_app);
}
#endif
