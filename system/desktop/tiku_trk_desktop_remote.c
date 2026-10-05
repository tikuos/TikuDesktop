/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_remote.c - remote sessions: the windows of applications
 * running in processes of their own, over the desk socket; their messages
 * (file picks, documents, boards, help, the clipboard, device requests);
 * and closing what a session leaves behind when it ends.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_vfs_rpc.h"

/**
 * @brief Draw a remote window: when recording, its description, or its
 *        display list when a fresh one came; otherwise the pixels its
 *        session last sent, on the panel colour.
 */
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
        /* While recording, the window's display list is copied into the
         * record as it came, words and controls included. */
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
    /* The pixels come from the application's shared surface, or from
     * the copied frame. */
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

/** @brief Send a pick in a remote window's published menu back to its
 *         session. */
static void
remote_menu_pick(tiku_window_t *window, int command, void *context)
{
    desktop_t *app = context;

    tiku_remote_pick(&app->remote, window, command);
}

/**
 * @brief Apply each session's window requests: close a reaped window, open
 *        a window for an OPEN, publish its menus, turn on its size
 *        controls, apply a configuration, a raise or a placement, and
 *        close a window given back.  Menus are published with
 *        tiku_window_publish_menus(), as an embedded window's are.
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
                /* A window whose row was reused: closed before the row's
                 * new state is read. */
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
                /* A session's windows are laid side by side, each tagged
                 * by its own window record. */
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
                    /* Never under the global bar, so the tab stays
                     * reachable. */
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
                    /* An announcement has no tab: its frame shrinks to
                     * the content and a hairline. */
                    rw->window->frame.w -= 2 * TIKU_WINDOW_BORDER - 2;
                    rw->window->frame.h -=
                        tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER - 2;
                }
                /* Both roles are centred in the upper third, at a fixed
                 * size.  An announcement is plain, with no tab, and its
                 * program takes it down; a panel keeps its tab and its
                 * close box. */
                rw->window->frame.x =
                    (app->surf->w - rw->window->frame.w) / 2;
                rw->window->frame.y =
                    (app->surf->h - rw->window->frame.h) / 3;
                tiku_window_set_size_controls(rw->window, 0, 0);
                tiku_window_set_plain(rw->window, plain);
                rw->wants_place = 0;
                desktop_damage_all(app);
            }
            /* A row the application gave back: its window closes, and
             * the session stays. */
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

/**
 * @brief Open the local image the session names through the associations,
 *        and reply TIKU_MSG_IMAGEOPENED with whether it was accepted; the
 *        client waits for the reply before it exits.
 */
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

int desktop_remote_document(const char *path)
{
    /* The extensions of the documents TikuTracker (system/tracker)
     * forwards from browser_open_image(), as tiku_backend_posix.c types
     * them. */
    static const char *const known[] = {
        ".bas", ".tiku", ".txt", ".md", ".markdown", ".html", ".htm"
    };
    const char *ext = (path != NULL) ? strrchr(path, '.') : NULL;

    if (ext == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
        if (!strcasecmp(ext, known[i])) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Open the local document the session names, when its name is one
 *        desktop_remote_document() accepts, through the associations; reply
 *        TIKU_MSG_DOCUMENTOPENED with whether it was accepted.
 */
static void remote_document_open(desktop_t *app, int session, tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int ok = desktop_remote_document(path) &&
        desktop_document_local(app, path);
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_DOCUMENTOPENED);
    if (reply) {
        (void)tiku_msg_add_int32(reply, "accepted", ok);
        (void)tiku_remote_tell(&app->remote, session, reply);
        tiku_msg_free(reply);
    }
}

/**
 * @brief Tell each remote window whether it has the focus, when that
 *        changes: an application's own workspace cannot see the desktop's
 *        keyboard focus.
 */
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
 * @brief Drain each session's messages and act on them: opening an image, a
 *        document or a path, a file pick, a device request, a screenshot, a
 *        tray icon, a cursor, the clipboard, help, and a board run, look,
 *        release, reclaim, save, input or stop.
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
            if (tiku_msg_what(m) == TIKU_MSG_GRAB)
                desktop_grab(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_TRAY)
                desktop_tray(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_CURSOR)
                desktop_cursor(app, i, m);
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
                /* The asker is told when the last port has answered. */
                desktop_board_look_begin(app, i, NULL);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDFREE) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                int ok = desktop_board_release(app, board);

                /* Success is told (RELEASED) once the close worker has
                 * closed the port; only a refusal is told here. */
                if (!ok) desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED,
                                           (board != NULL) ? board : "");
                desktop_board_names_tell(app);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDBACK) {
                const char *board =
                    tiku_msg_find_string(m, "board", 0);

                /* A reclaim taken is answered when every port has; a
                 * reclaim refused is answered FAILED now. */
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
                /* Into the desk's clipboard; clip_mirror() sends it on
                 * to every session, this one included. */
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

/** @brief Tell session @p session something about its board run, as
 *         TIKU_MSG_BOARDOUT. */
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
 * @brief Tell every session the mounted boards and the current device, when
 *        either changes or the number of sessions does.
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
    /* Also when the number of sessions changes, so a session that
     * connects later hears them. */
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
    /* The current device goes with them: no other message carries it to
     * the remote sessions. */
    (void)tiku_msg_add_string(m, "current", app->current_device);
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        (void)tiku_remote_tell(&app->remote, i, m);
    }
    tiku_msg_free(m);
}

/**
 * @brief Send the desk's clipboard to each session that has not had its
 *        current version: after a change of the clipboard's stamp, or when
 *        a session joins while text is held.
 *
 * The clipboard does not record who changed it, so its stamp is checked
 * once a turn.  A session whose output is full is told on a later turn.
 */
void
clip_mirror(desktop_t *app)
{
    static uint64_t heard[TIKU_REMOTE_SESSIONS];    /* who was told ...   */
    static unsigned told[TIKU_REMOTE_SESSIONS];     /* ... which version  */
    unsigned now = tiku_clipboard_stamp();
    tiku_msg_t *m = NULL;
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        const tiku_remote_session_t *s = &app->remote.session[i];
        int joined = heard[i] != s->generation;

        if (!s->used || (!joined && told[i] == now)) {
            continue;
        }
        if (joined && tiku_clipboard_length() == 0u) {
            heard[i] = s->generation;   /* nothing held: nothing to say */
            told[i] = now;
            continue;
        }
        if (m == NULL && (m = tiku_msg_new(TIKU_MSG_CLIP)) != NULL) {
            (void)tiku_msg_add_string(m, "text", tiku_clipboard_text());
        }
        if (m != NULL && tiku_remote_tell(&app->remote, i, m)) {
            heard[i] = s->generation;
            told[i] = now;
        }
    }
    tiku_msg_free(m);
}

/**
 * @brief Forget an application's file-panel question once the panel no
 *        longer asks it (closed, or taken by another question).  The
 *        panel sends nothing on cancel, and while pending_pick.kind is set
 *        every question but a newer one from the same window is refused.
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

/** @brief Close the windows of sessions that ended since the last poll. */
void
remote_sweep(desktop_t *app)
{
    /* A window drawn by remote_window_draw() that no session owns any
     * more belongs to a session that has ended; the workspace still
     * holds it. */
    tiku_window_t *w;
    int i;

    for (i = 0; ; ) {
        w = tiku_workspace_at(app->desk, i);
        if (w == NULL) {
            break;
        }
        if (w->open && w->draw == remote_window_draw &&
            tiku_remote_owner(&app->remote, w) == NULL) {
            /* Closing compacts the workspace's order, so the next window
             * is at this index: the index does not advance. */
            tiku_workspace_close(app->desk, w);
            desktop_damage_all(app);
            continue;
        }
        i++;
    }
}
