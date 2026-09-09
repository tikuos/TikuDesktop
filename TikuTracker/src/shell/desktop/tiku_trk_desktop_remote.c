/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_remote.c - remote sessions: out-of-process windows over
 * the desk socket, and the clipboard and picks that travel with them
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"

/** @brief A remote window's pixels are whatever its session last sent. */
static void
remote_window_draw(tiku_window_t *window, tiku_surface_t *surface,
                   tiku_rect_t content, void *context)
{
    tiku_remote_window_t *rw = context;
    const uint32_t *px;
    int y, w, h;

    (void)window;
    if (rw != NULL && rw->list_fresh && rw->list != NULL &&
        surface->record != NULL) {
        /* A reader, not a screen: the window is what its list says, so
         * the list is what is recorded, words and controls intact. */
        int rec = tiku_gfx_rec_enter(surface);

        if (rec) {
            (void)tiku_dl_append(surface->record, rw->list);
        }
        tiku_gfx_rec_leave(surface, rec);
        return;
    }
    tiku_fill(surface, content, TIKU_C_PANEL);
    if (rw == NULL) {
        return;
    }
    /* A shared surface is drawn out of the application's own pixels; a
     * copied frame out of the copy.  Nothing else differs. */
    px = tiku_remote_pixels(rw);
    if (px == NULL) {
        return;
    }
    w = (rw->fw < content.w) ? rw->fw : content.w;
    h = (rw->fh < content.h) ? rw->fh : content.h;
    for (y = 0; y < h; y++) {
        int x;

        for (x = 0; x < w; x++) {
            tiku_pixel(surface, content.x + x, content.y + y,
                            px[(size_t)y * (size_t)rw->fw + x]);
        }
    }
}

/** @brief A pick in a remote window's published menu rides the wire back. */
static void
remote_menu_pick(tiku_window_t *window, int command, void *context)
{
    desktop_t *app = context;

    tiku_remote_pick(&app->remote, window, command);
}

/**
 * @brief Give every session what it is owed: a window for an OPEN, the
 *        published menus for a MENUS -- the same publish path a linked-in
 *        window uses, which is the whole point.
 */
void
remote_reconcile(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &app->remote.session[i];
        int j;

        if (!s->used) {
            continue;
        }
        for (j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            tiku_remote_window_t *rw = &s->win[j];

            if (rw->reap != NULL) {
                /* A window whose row has already moved on: closed here,
                 * where the workspace is, before anything else about
                 * the row is believed. */
                if (app->remote_press == rw->reap) {
                    app->remote_press = NULL;
                }
                tiku_workspace_close(app->desk, rw->reap);
                rw->reap = NULL;
                app->dirty = 1;
            }
            if (rw->opened && rw->window == NULL) {
                tiku_rect_t frame;

                frame.w = rw->open_w + 2 * TIKU_WINDOW_BORDER;
                frame.h = rw->open_h + tiku_window_tab_h() +
                          2 * TIKU_WINDOW_BORDER;
                frame.x = (app->surf->w - frame.w) / 2;
                frame.y = (app->surf->h - frame.h) / 3;
                /* Beside each other, and tagged by the WINDOW: a tag
                 * naming the session handed the second window the
                 * first one back, wearing a name it never took. */
                {
                    int across = frame.w + 8;
                    int cols = (across > 0) ? (app->surf->w / across) : 1;

                    if (cols < 1) {
                        cols = 1;
                    }
                    frame.x += (j % cols) * across;
                    frame.y += (j / cols) * (frame.h + 8);
                    if (frame.x + frame.w > app->surf->w) {
                        frame.x = app->surf->w - frame.w;
                    }
                    if (frame.y + frame.h > app->surf->h) {
                        frame.y = app->surf->h - frame.h;
                    }
                    if (frame.x < 0) {
                        frame.x = 0;
                    }
                    if (frame.y < 0) {
                        frame.y = 0;
                    }
                }
                rw->window = tiku_workspace_open(app->desk, rw->title,
                    frame, remote_window_draw, rw, rw);
                if (rw->window != NULL) {
                    tiku_window_set_size_controls(rw->window, 0, 0);
                }
                app->dirty = 1;
            }
            if (rw->window != NULL && rw->menus_fresh) {
                tiku_window_publish_menus(rw->window, &rw->menus,
                                               remote_menu_pick, app);
                rw->menus_fresh = 0;
                app->dirty = 1;
            }
            if (rw->window != NULL && rw->wants_size) {
                tiku_window_set_size_controls(rw->window, 1, 0);
                rw->wants_size = 0;
            }
            if (rw->window != NULL && rw->wants_front) {
                tiku_workspace_activate(app->desk, rw->window);
                rw->wants_front = 0;
                app->dirty = 1;
            }
            if (rw->window != NULL &&
                (rw->wants_place == TIKU_APP_PLACE_ANNOUNCE ||
                 rw->wants_place == TIKU_APP_PLACE_PANEL)) {
                int plain =
                    (rw->wants_place == TIKU_APP_PLACE_ANNOUNCE);

                if (plain) {
                    /* The frame was cut for a tab it will not wear:
                     * taken in, so the hairline hugs the content. */
                    rw->window->frame.w -= 2 * TIKU_WINDOW_BORDER - 2;
                    rw->window->frame.h -=
                        tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER - 2;
                }
                /* Both roles across the wire: centred in the upper
                 * part and fixed.  A splash is PLAIN -- a card the
                 * program holds up, taken down by the program.  A
                 * panel keeps its tab, because a window a person
                 * opened is a window a person must be able to close. */
                rw->window->frame.x =
                    (app->surf->w - rw->window->frame.w) / 2;
                rw->window->frame.y =
                    (app->surf->h - rw->window->frame.h) / 3;
                tiku_window_set_size_controls(rw->window, 0, 0);
                tiku_window_set_plain(rw->window, plain);
                rw->wants_place = 0;
                app->dirty = 1;
            }
            /* A row the application gave back: the window goes with it.
             * The session stays -- it has others, or it is about to
             * say it is leaving. */
            if (rw->window != NULL && !rw->opened) {
                if (app->remote_press == rw->window) {
                    app->remote_press = NULL;
                }
                tiku_workspace_close(app->desk, rw->window);
                rw->window = NULL;
                app->dirty = 1;
            }
        }
    }
}

/**
 * @brief Take what each session has said, and act on what it asked for.
 *
 * The upward message road existed and nothing drained it: a session
 * could say things into an eight-deep ring for ever and the shell would
 * never look.  The first thing worth saying is "let the person pick a
 * file", so this is where it is heard.
 */
void
remote_hear(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_msg_t *m;

        while ((m = tiku_remote_said(&app->remote, i)) != NULL) {
            if (tiku_msg_what(m) == TIKU_MSG_PICK &&
                app->pending_pick.kind == 0) {
                int32_t window = 0, mode = TIKU_APP_PICK_OPEN;
                const char *start = tiku_msg_find_string(m, "start", 0);
                const char *name = tiku_msg_find_string(m, "name", 0);

                (void)tiku_msg_find_int32(m, "window", 0, &window);
                (void)tiku_msg_find_int32(m, "mode", 0, &mode);
                app->pending_pick.kind = 2;
                app->pending_pick.session = i;
                app->pending_pick.slot = NULL;
                app->pending_pick.window = (uint32_t)window;
                if (mode == TIKU_APP_PICK_SAVE) {
                    tiku_trk_panel_window_open_save(&app->file_panel,
                        &app->pending_pick, TIKU_TRK_CMD_APP_PICK,
                        start, name);
                } else {
                    tiku_trk_panel_window_open_file(&app->file_panel,
                        &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start,
                        name);
                }
                app->dirty = 1;
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDRUN) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                const char *prog = tiku_msg_find_string(m, "program", 0);

                /* Which session asked is remembered, so what the program
                 * prints goes back to it and not to every session. */
                app->board_run_session = i;
                if (!desktop_board_run(app, NULL, board,
                                       (prog != NULL) ? prog : "")) {
                    app->board_run_session = -1;
                    desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED, "");
                }
            }
                                    {
                uint32_t w = tiku_msg_what(m);

                tiku_trail("wire %c%c%c%c from session %d",
                           (char)(w >> 24), (char)(w >> 16),
                           (char)(w >> 8), (char)w, i);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDLOOK) {
                /* Begun here, finished over the beats: the asker is told
                 * when the last port has been asked. */
                desktop_board_look_begin(app, i);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDFREE) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                int ok = desktop_board_release(app, board);

                desktop_board_tell(app, i,
                                   ok ? TIKU_APP_BOARD_RELEASED
                                      : TIKU_APP_BOARD_FAILED,
                                   (board != NULL) ? board : "");
                desktop_board_names_tell(app);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDBACK) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                int ok = desktop_board_reclaim(app, board);

                desktop_board_tell(app, i,
                                   ok ? TIKU_APP_BOARD_RECLAIMED
                                      : TIKU_APP_BOARD_FAILED,
                                   (board != NULL) ? board : "");
                desktop_board_names_tell(app);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDSAVE) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                const char *name = tiku_msg_find_string(m, "name", 0);
                const char *prog = tiku_msg_find_string(m, "program", 0);

                app->board_run_session = i;
                if (!desktop_board_save(app, NULL, board, name,
                                        (prog != NULL) ? prog : "")) {
                    app->board_run_session = -1;
                    desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED, "");
                }
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDIN) {
                const char *text = tiku_msg_find_string(m, "text", 0);

                (void)desktop_board_input(app, (text != NULL) ? text : "");
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDSTOP) {
                (void)desktop_board_stop(app);
            }
            if (tiku_msg_what(m) == TIKU_MSG_CLIP) {
                /* A session put text on the desk's clipboard.  Taken
                 * into the one store; the stamp watch below mirrors it
                 * back out to every session, this one included. */
                const char *text = tiku_msg_find_string(m, "text", 0);

                tiku_clipboard_set((text != NULL) ? text : "");
            }
            tiku_msg_free(m);
        }
    }
}

/**
 * @brief Tell one session something its board program said.
 */
void
desktop_board_tell(desktop_t *app, int session, int what, const char *text)
{
    tiku_msg_t *m;

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS) {
        return;
    }
    m = tiku_msg_new(TIKU_MSG_BOARDOUT);
    if (m == NULL) {
        return;
    }
    (void)tiku_msg_add_int32(m, "what", (int32_t)what);
    (void)tiku_msg_add_string(m, "text", (text != NULL) ? text : "");
    (void)tiku_remote_tell(&app->remote, session, m);
    tiku_msg_free(m);
}

/**
 * @brief Tell every session which boards are mounted, when that changes.
 *
 * Warm before a menu asks, for the same reason the clipboard is.
 */
void
desktop_board_names_tell(desktop_t *app)
{
    char names[512];
    tiku_msg_t *m;
    int i, sessions = 0;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (app->remote.session[i].used) {
            sessions++;
        }
    }
        (void)desktop_board_list(app, names, sizeof names);
    /* Told when the names change -- AND when the room does.  The boards
     * were named once at startup, before any application had connected,
     * and a session arriving after that would never have heard them. */
    if (sessions == app->board_names_sessions &&
        strcmp(names, app->board_names_told) == 0) {
        return;
    }
    app->board_names_sessions = sessions;
    (void)snprintf(app->board_names_told, sizeof app->board_names_told,
                   "%s", names);
    m = tiku_msg_new(TIKU_MSG_BOARDS);
    if (m == NULL) {
        return;
    }
    (void)tiku_msg_add_string(m, "names", names);
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        (void)tiku_remote_tell(&app->remote, i, m);
    }
    tiku_msg_free(m);
}

/**
 * @brief Mirror the desk's clipboard to every session when it moves.
 *
 * The store cannot say who changed it -- a field's Cmd-C deep in the
 * kit, an embedded application's service, a session over the wire --
 * so the shell WATCHES its stamp once a turn and tells the sessions on
 * any change.  Every session's copy is therefore always warm, and a
 * paste out of process never waits on the wire.
 */
void
clip_mirror(desktop_t *app)
{
    static unsigned told;
    unsigned now = tiku_clipboard_stamp();
    tiku_msg_t *m;
    int i;

    if (now == told) {
        return;
    }
    told = now;
    m = tiku_msg_new(TIKU_MSG_CLIP);
    if (m == NULL) {
        return;
    }
    (void)tiku_msg_add_string(m, "text", tiku_clipboard_text());
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        (void)tiku_remote_tell(&app->remote, i, m);
    }
    tiku_msg_free(m);
}

/**
 * @brief Forget an ask whose panel is no longer asking it.
 *
 * The panel says nothing on cancel and the application is owed no
 * answer, but a note of the ask that outlives it refuses every later
 * one.  The panel's target says whose question it holds; the moment
 * that is not the remote's, closed or taken by one of the shell's own
 * askers, the ask is over.
 */
void
pick_sweep(desktop_t *app)
{
    if (app->pending_pick.kind != 0 &&
        app->file_panel.target != &app->pending_pick) {
        app->pending_pick.kind = 0;
        app->pending_pick.slot = NULL;
    }
}

/** @brief Close the windows of sessions that died since the last poll. */
void
remote_sweep(desktop_t *app)
{
    /* A window whose session vanished is swept by tag: the workspace
     * still holds it, but no session claims it. */
    tiku_window_t *w;
    int i;

    for (i = 0; ; i++) {
        w = tiku_workspace_at(app->desk, i);
        if (w == NULL) {
            break;
        }
        if (w->open && w->draw == remote_window_draw &&
            tiku_remote_owner(&app->remote, w) == NULL) {
            tiku_workspace_close(app->desk, w);
            app->dirty = 1;
        }
    }
}
