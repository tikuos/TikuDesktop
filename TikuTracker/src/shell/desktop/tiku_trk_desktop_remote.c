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
#include "tiku_vfs_rpc.h"

/** @brief A remote window's pixels are whatever its session last sent. */
static void
remote_window_draw(tiku_window_t *window, tiku_surface_t *surface,
                   tiku_rect_t content, void *context)
{
    tiku_remote_window_t *rw = context;
    const uint32_t *px;
    int y, w, h;

    (void)window;
    if (rw && surface->record && rw->description[0]) {
        (void)tiku_dl_text(surface->record, TIKU_DL_PLAIN, content.x,
                           content.y, rw->description, TIKU_C_TEXT);
        return;
    }
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
                desktop_damage_all(app);
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
                    /* Never under the global bar: a tab nobody can
                     * reach is a window nobody can move or zoom. */
                    if (app->gbar_on == 1 &&
                        frame.y < app->menus.bar.height) {
                        frame.y = app->menus.bar.height;
                    }
                }
                rw->window = tiku_workspace_open(app->desk, rw->title,
                    frame, remote_window_draw, rw, rw);
                if (rw->window != NULL) {
                    tiku_window_set_size_controls(rw->window, 0, 0);
                }
                desktop_damage_all(app);
            }
            if (rw->window != NULL && rw->menus_fresh) {
                tiku_window_publish_menus(rw->window, &rw->menus,
                                               remote_menu_pick, app);
                rw->menus_fresh = 0;
                desktop_damage_all(app);
            }
            if (rw->window != NULL && rw->wants_size) {
                tiku_window_set_size_controls(rw->window, 1, 1);
                rw->wants_size = 0;
            }
            if (rw->window != NULL && rw->configured) {
                tiku_window_t *w = rw->window;
                snprintf(w->title, sizeof w->title, "%s", rw->title);
                tiku_window_set_size_controls(w, rw->resizable, rw->zoomable);
                if (rw->config_w && rw->config_h) {
                    tiku_rect_t content = tiku_window_content(w);
                    w->frame.w += rw->config_w - content.w;
                    w->frame.h += rw->config_h - content.h;
                }
                rw->configured = 0;
                desktop_damage_all(app);
            }
            if (rw->window != NULL && rw->wants_front) {
                tiku_workspace_activate(app->desk, rw->window);
                rw->wants_front = 0;
                desktop_damage_all(app);
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
                desktop_damage_all(app);
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
                desktop_damage_all(app);
            }
        }
    }
}

/** @brief Acknowledge local-image dispatch before the client exits. */
static void remote_image_open(desktop_t *app, int session, tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int ok = path && desktop_document_local(app, path);
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_IMAGEOPENED);
    if (reply) {
        (void)tiku_msg_add_int32(reply, "accepted", ok);
        (void)tiku_remote_tell(&app->remote, session, reply);
        tiku_msg_free(reply);
    }
}

/** @brief Dispatch requested local documents through associations. */
static void remote_document_open(desktop_t *app, int session, tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    const char *ext = path ? strrchr(path, '.') : NULL;
    int supported = ext && (!strcasecmp(ext, ".bas") ||
        !strcasecmp(ext, ".tiku") || !strcasecmp(ext, ".txt") ||
        !strcasecmp(ext, ".md"));
    int ok = supported && desktop_document_local(app, path);
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_DOCUMENTOPENED);
    if (reply) {
        (void)tiku_msg_add_int32(reply, "accepted", ok);
        (void)tiku_remote_tell(&app->remote, session, reply);
        tiku_msg_free(reply);
    }
}

/** @brief Native app workspaces cannot infer the desktop's keyboard owner. */
void
remote_focus(desktop_t *app, tiku_window_t *focused)
{
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &app->remote.session[i];
        if (!s->used) continue;
        for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            tiku_remote_window_t *rw = &s->win[j];
            if (!rw->window || !rw->opened) continue;
            int active = rw->window == focused;
            if (active == rw->focused) continue;
            rw->focused = active;
            tiku_event_t event = {.type = active ? TIKU_EVENT_ACTIVATED :
                                                   TIKU_EVENT_DEACTIVATED};
            tiku_remote_event(&app->remote, rw->window, &event);
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
            if (tiku_msg_what(m) == TIKU_MSG_IMAGEOPEN)
                remote_image_open(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_DOCUMENTOPEN)
                remote_document_open(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_PICK)
                desktop_remote_pick(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_VFS_REQUEST)
                desktop_vfs_request(app, i, m);
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
                desktop_board_look_begin(app, i, NULL);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDFREE) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                int ok = desktop_board_release(app, board);

                /* Success is acknowledged after the close worker finishes,
                 * not while another owner still has the serial port open. */
                if (!ok) desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED,
                                           (board != NULL) ? board : "");
                desktop_board_names_tell(app);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDBACK) {
                const char *board =
                    tiku_msg_find_string(m, "board", 0);

                /* Taken: the answer comes when the ports have spoken.
                 * Not taken: there is nothing to wait for. */
                if (!desktop_board_reclaim(app, board, i, NULL)) {
                    desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED,
                                       (board != NULL) ? board : "");
                }
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
            if (tiku_msg_what(m) == TIKU_MSG_HELPPAGE) {
                const char *page = tiku_msg_find_string(m, "page", 0);

                snprintf(app->remote.session[i].help_page,
                         sizeof app->remote.session[i].help_page, "%s",
                         (page != NULL) ? page : "");
            }
            if (tiku_msg_what(m) == TIKU_MSG_HELPOPEN) {
                const char *page = tiku_msg_find_string(m, "page", 0);
                if (page && strlen(page) < 256)
                    (void)desktop_help_open(app, page);
            }
            if (tiku_msg_what(m) == TIKU_MSG_HELPHTML) {
                const char *title = tiku_msg_find_string(m, "title", 0);
                const char *html = tiku_msg_find_string(m, "html", 0);
                if (html && strlen(html) <= 15000 &&
                    (!title || strlen(title) <= 127))
                    (void)desktop_help_html(app,
                                                    title, html);
            }
            if (tiku_msg_what(m) == TIKU_MSG_OPENPATH) {
                const char *path = tiku_msg_find_string(m, "path", 0);
                if (path && !desktop_app_open_path(app, path)) {
                    snprintf(app->status, sizeof app->status,
                             "Could not open %.180s", path);
                    desktop_damage_all(app);
                }
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
        strcmp(names, app->board_names_told) == 0 &&
        strcmp(app->current_device, app->board_current_told) == 0) {
        return;
    }
    app->board_names_sessions = sessions;
    (void)snprintf(app->board_names_told, sizeof app->board_names_told,
                   "%s", names);
    (void)snprintf(app->board_current_told,
                   sizeof app->board_current_told, "%s",
                   app->current_device);
    m = tiku_msg_new(TIKU_MSG_BOARDS);
    if (m == NULL) {
        return;
    }
    (void)tiku_msg_add_string(m, "names", names);
    /* The current device rides with them: a session out of process has
     * no other way to know what its work defaults to. */
    (void)tiku_msg_add_string(m, "current", app->current_device);
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

    for (i = 0; ; ) {
        w = tiku_workspace_at(app->desk, i);
        if (w == NULL) {
            break;
        }
        if (w->open && w->draw == remote_window_draw &&
            tiku_remote_owner(&app->remote, w) == NULL) {
            /* Closing compacts the workspace's order, so the next
             * window is at this index: advancing here would step over
             * it, and a killed application would leave a ghost. */
            tiku_workspace_close(app->desk, w);
            desktop_damage_all(app);
            continue;
        }
        i++;
    }
}
