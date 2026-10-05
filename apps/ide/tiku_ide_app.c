/* tiku_ide_app.c -- shared BASIC/C descriptor, lifecycle and periodic work.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static void ide_closed(void *state, uint32_t window);

/**
 * @brief Periodic work: recovery drafts, the splash, and the run.
 *
 * Every 2 s, brings each open page's recovery draft up to date; repaints
 * the windows when the target strip's text changes; steps the splash's
 * status line and closes the splash after SPLASH_US; reads a local run's
 * output into the messages and stops the run after IDE_RUN_LIMIT_US; and
 * releases a board run IDE_BOARD_STOP_US after an unanswered Stop.
 */
void
tiku_ide_tick(void *state, int64_t now_us)
{
    ide_t *st = state;
    char buf[256];
    ssize_t n = -1;
    int heard = 0;

    if (st == NULL) {
        return;
    }
    /* Every 2 s, bring each open page's recovery draft up to date and
     * repaint the page; when the target strip's text changes, repaint
     * the Project window and the pages. */
    {
        char status[160];
        int i, checkpoint = now_us - st->recovery_at >= 2000000;

        tiku_ide_target_text(st, status, sizeof status);
        if (strcmp(status, st->target_status)) { tiku_ide_project_paint(st); }
        for (i = 0; i < IDE_EDITS; i++) {
            edit_t *e = &st->edit[i];
            if (!e->surface) { continue; }
            if (checkpoint && !tiku_ide_recovery_write(e)) {
                snprintf(e->feedback, sizeof e->feedback,
                         "Recovery unavailable — save your work manually");
            }
            if (checkpoint || strcmp(status, st->target_status)) {
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
            }
        }
        snprintf(st->target_status, sizeof st->target_status, "%s", status);
        if (checkpoint) { st->recovery_at = now_us; }
    }
    if (st->splash_surface != NULL) {
        /* The splash closes after SPLASH_US; until then its status line
         * shows three fixed labels in turn, a third of SPLASH_US each. */
        static const char *const STAGE[] = {
#ifdef TIKU_IDE_C
            "finding the compilers…",
#else
            "waking the interpreter…",
#endif
            "reading the kits…",
            "opening the project window…"
        };
        int stage;

        if (st->splash_from == 0) {
            st->splash_from = now_us;
        } else if (now_us - st->splash_from > SPLASH_US) {
            tiku_ide_splash_drop(st);
        } else {
            stage = (int)((now_us - st->splash_from) /
                          (SPLASH_US / 3 + 1));
            if (stage > 2) {
                stage = 2;
            }
            if (stage != st->splash_stage) {
                st->splash_stage = stage;
                tiku_ide_announce_paint_art(st->splash_surface, STAGE[stage],
                                   st->splash_art);
                (void)st->services->frame(st->services->ctx,
                                          st->splash_id,
                                          st->splash_surface->px,
                                          SPLASH_W, SPLASH_H);
            }
        }
    }
    if (!st->running) {
        return;
    }
    if (st->local_debug) { tiku_ide_debug_tick(st); return; }
    if (st->run_from == 0) {
        st->run_from = now_us;
    }
    /* A board run has no child and no pipe: the shell owns it and reports
     * through tiku_ide_board_said(), so nothing is read here.  A read
     * would get EBADF from the closed descriptor, which the loop below
     * takes for a finished run.  A Stop the board has not answered is
     * timed from here, and the run is released IDE_BOARD_STOP_US after
     * it. */
    if (st->board[0] != '\0') {
        if (st->stop_asked == -1) {
            st->stop_asked = now_us;
        } else if (st->stop_asked > 0 &&
                   now_us - st->stop_asked > IDE_BOARD_STOP_US) {
            tiku_ide_board_run_released(st);
        }
        return;
    }
    /* A local run gets no input from the IDE; it is stopped after
     * IDE_RUN_LIMIT_US. */
    if (now_us - st->run_from > IDE_RUN_LIMIT_US) {
        tiku_ide_run_stop(st, "? the run was stopped: it did not finish");
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            n = 0;              /* a broken pipe is a finished run */
            break;
        }
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        {
            char *p = buf;

            while (*p != '\0') {
                size_t used = strlen(st->partial);

                if (*p == '\n') {
                    tiku_ide_message_take(st, st->partial);
                    st->partial[0] = '\0';
                } else if (used + 1u < sizeof st->partial) {
                    st->partial[used] = *p;
                    st->partial[used + 1u] = '\0';
                }
                p++;
            }
        }
        heard = 1;
    }
    if (n == 0) {
        int status = 0;

        if (st->partial[0] != '\0') {
            tiku_ide_message_take(st, st->partial);
            st->partial[0] = '\0';
        }
        (void)waitpid(st->child, &status, 0);
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        heard = 1;
        /* When the run ends, the Messages window is raised if it holds
         * a message. */
        if (st->nmsg > 0 && st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->msg_id);
        }
    }
    if (heard) {
        tiku_ide_messages_show(st);
    }
}

/**
 * @brief Start the application: open the splash, or the Project window
 *        when there is no splash, then the startup document if any.
 *
 * A startup document ending in .tiku opens as the project; any other
 * opens in an editor page.
 *
 * @return 0, or -1 when the state or the Project surface cannot be made.
 */
int
tiku_ide_start(void **state, const tiku_app_services_t *services)
{
    ide_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    if (services->help_page != NULL) {
#ifdef TIKU_IDE_C
        (void)services->help_page(services->ctx, "apps/tikuc.html");
#else
        (void)services->help_page(services->ctx, "apps/tikubasic.html");
#endif
    }
    tiku_list_init(&st->tv_list, 1);
    st->click_row = -1;
    st->proj_w = IDE_W;
    st->proj_h = IDE_H;
    st->proj_surface = tiku_surface_new(IDE_W, IDE_H, TIKU_C_PANEL);
    if (st->proj_surface == NULL) {
        free(st);
        return -1;
    }
    /* Only the splash opens here; tiku_ide_splash_drop() opens the
     * Project window when the splash closes. */
    st->splash_art = tiku_ide_splash_art_load();
    st->splash_surface = tiku_surface_new(SPLASH_W, SPLASH_H,
                                               TIKU_C_PANEL);
    if (st->splash_surface != NULL) {
        st->splash_id = services->open(services->ctx, APP_NAME,
                                       SPLASH_W, SPLASH_H);
        tiku_ide_announce_paint_art(st->splash_surface,
#ifdef TIKU_IDE_C
                           "finding the compilers…",
#else
                           "waking the interpreter…",
#endif
                           st->splash_art);
        (void)services->frame(services->ctx, st->splash_id,
                              st->splash_surface->px, SPLASH_W,
                              SPLASH_H);
        if (services->place != NULL) {
            (void)services->place(services->ctx, st->splash_id,
                                  TIKU_APP_PLACE_ANNOUNCE);
        }
    }
    if (st->splash_surface == NULL) {
        /* No splash: the Project window opens now. */
        st->proj_id = services->open(services->ctx, "Project", IDE_W,
                                     IDE_H);
        tiku_ide_project_sizeable(st);
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);
    }
    if (services->startup_document && services->startup_document[0]) {
        const char *path = services->startup_document;
        const char *ext = strrchr(path, '.');
        tiku_ide_splash_drop(st);
        if (strlen(path) >= sizeof st->proj_file) {
            snprintf(st->note, sizeof st->note, "Document path is too long");
            tiku_ide_project_paint(st);
        } else if (ext && !strcasecmp(ext, ".tiku")) {
            tiku_ide_project_take(st, path);
        } else {
            prow_t want = {0};
            const char *leaf = strrchr(path, '/');
            if (strlen(path) >= sizeof want.path) {
                snprintf(st->note, sizeof st->note, "Document path is too long");
                tiku_ide_project_paint(st);
            } else {
                snprintf(want.path, sizeof want.path, "%s", path);
                snprintf(want.name, sizeof want.name, "%s", leaf ? leaf + 1 : path);
                tiku_ide_edit_open(st, &want);
            }
        }
    }
    *state = st;
    return 0;
}

/** @brief Stop the application: end a run on the board or this machine,
 *         write each modified page's recovery draft, and free everything. */
void
tiku_ide_stop(void *state)
{
    ide_t *st = state;
    int i;

    if (st == NULL) {
        return;
    }
    if (st->local_debug) { tiku_ide_debug_cleanup(st); st->running = 0; }
    if (st->running && st->board[0] != '\0') {
        /* A board program left running can wait at INPUT with nobody to
         * answer, and the board's console then stays silent until the
         * board is power-cycled; the run is stopped first. */
        if (st->services->board_stop != NULL) {
            (void)st->services->board_stop(st->services->ctx);
        }
        st->running = 0;
    } else if (st->running) {
        (void)kill(st->child, SIGTERM);
        (void)waitpid(st->child, NULL, 0);
        (void)close(st->child_fd);
        st->running = 0;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        tiku_ide_recovery_release(&st->edit[i], 0);
        tiku_ide_text_free(&st->edit[i]);
        tiku_surface_free(st->edit[i].surface);
    }
    tiku_surface_free(st->msg_surface);
    tiku_surface_free(st->splash_surface);
    tiku_surface_free(st->about_surface);
    tiku_surface_free(st->splash_art);
    tiku_surface_free(st->proj_surface);
    free(st);
}

/** @brief The desktop closed @p window: free what it showed.  Closing the
 *         Project window closes every other window too. */
static void
ide_closed(void *state, uint32_t window)
{
    ide_t *st = state;
    edit_t *e = tiku_ide_edit_of_id(st, window);

    if (window == st->proj_id && st->proj_surface != NULL) {
        /* Closing the Project window ends the IDE: each page closes as on
         * a desktop close, a modified page keeping its draft; the
         * Messages, console and About windows close, and any open
         * question is dropped. */
        uint32_t *ids[] = { &st->msg_id, &st->con_id, &st->about_id };
        tiku_surface_t **surfaces[] = { &st->msg_surface, &st->con_surface,
                                        &st->about_surface };
        int i;

        tiku_surface_free(st->proj_surface);
        st->proj_surface = NULL;
        st->proj_id = 0u;
        tiku_alert_reset(&st->ask);
        st->closing_editor = 0u;
        for (i = 0; i < IDE_EDITS; i++) {
            edit_t *page = &st->edit[i];

            if (page->surface == NULL) {
                continue;
            }
            tiku_ide_recovery_release(page, 0);
            tiku_ide_text_free(page);
            st->services->close(st->services->ctx, page->id);
            tiku_surface_free(page->surface);
            page->surface = NULL;
        }
        for (i = 0; i < 3; i++) {
            if (*surfaces[i] != NULL) {
                st->services->close(st->services->ctx, *ids[i]);
                tiku_surface_free(*surfaces[i]);
                *surfaces[i] = NULL;
                *ids[i] = 0u;
            }
        }
        return;
    }
    if (e != NULL) {
        tiku_ide_recovery_release(e, 0);
        tiku_ide_text_free(e);
        tiku_surface_free(e->surface);
        e->surface = NULL;
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);
        return;
    }
        if (window == st->msg_id && st->msg_surface != NULL) {
        tiku_surface_free(st->msg_surface);
        st->msg_surface = NULL;
        st->msg_id = 0u;
        return;
    }
    if (window == st->con_id && st->con_surface != NULL) {
        tiku_surface_free(st->con_surface);
        st->con_surface = NULL;
        st->con_id = 0u;
        return;
    }
    if (window == st->splash_id && st->splash_surface != NULL) {
        tiku_surface_free(st->splash_surface);
        st->splash_surface = NULL;
        return;
    }
    if (window == st->about_id && st->about_surface != NULL) {
        tiku_surface_free(st->about_surface);
        st->about_surface = NULL;
    }
}

const tiku_app_descriptor_t tiku_ide_app = {
    .id = APP_ID,
    .name = APP_NAME,
    .start = tiku_ide_start,
    .stop = tiku_ide_stop,
    .pick = tiku_ide_pick,
    .picked = tiku_ide_picked,
    .tick = tiku_ide_tick,
    .event_in = tiku_ide_event,
    .closed = ide_closed,
    .board_said = tiku_ide_board_said
};

#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_ide_app
};
#endif

#ifndef TIKU_APP_EMBED
/** @brief Run the IDE as a program of its own, a client of the desktop. */
int
main(void)
{
    return tiku_client_run(&tiku_ide_app);
}
#endif
