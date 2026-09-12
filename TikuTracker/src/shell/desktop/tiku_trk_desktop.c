/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop.c - the desktop shell.
 *
 * One host window holds the whole thing: a backdrop, folder windows with the
 * R5 tab chrome, and the Deskbar in the corner with a live tray.  Opening a
 * folder opens ITS window, and opening it again brings that window forward
 * rather than making a second one -- the spatial promise.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"
#include "tiku_report.h"
#include "tiku_watch.h"

/**
 * @brief Apply the settings file to the running shell: thumbnails, the
 *        global menu bar, the theme, and the per-window switches every
 *        folder view carries.  A change that alters a window's shape or
 *        palette forgets its pixels and marks the frame dirty.
 */
void
sync_folder_preferences(desktop_t *app)
{
    tiku_trk_prefs_t *prefs = prefs_model(app);
    int hide = tiku_trk_prefs_get(prefs, "HideDotFiles") != 0;
    int folders_first = tiku_trk_prefs_get(prefs, "SortFolderNamesFirst") != 0;
    int filter = tiku_trk_prefs_get(prefs, "TypeAheadFiltering") != 0;
    int full_path = tiku_trk_prefs_get(prefs, "ShowFullPathInTitleBar") != 0;
    int i;

    /* The thumbnail queue's own switch IS the preference (TS-053). */
    app->thumbs.enabled = tiku_trk_prefs_get(prefs,
                              "GenerateImageThumbnails") != 0;
    {
        /* One bar at the top of the screen, or one per window: flipping
         * re-teaches every window its shape, so the pixels are forgotten
         * and the desk poses move out from under the strip. */
        int gon = tiku_trk_prefs_get(prefs, "GlobalMenuBar") != 0;

        if (gon != app->gbar_on) {
            app->gbar_on = gon;
            app->folder_render.global_bar = gon;
            tiku_trk_desktop_view_set_top_inset(&app->desktop_view,
                gon ? app->menus.bar.height : 0);
            tiku_trk_deskbar_set_top_inset(app->deskbar,
                gon ? app->menus.bar.height : 0);
            for (i = 0; i < app->folder_count; i++) {
                tiku_trk_folder_window_forget(&app->folders[i]);
            }
            app->gbar_open = -1;
            app->dirty = 1;
        }
    }
    {
        /* The theme is a TABLE SWAP, applied the moment the setting says
         * so (settings semantics: immediate, no OK button).  A change
         * forgets every window's pixels -- they were painted out of the
         * other table -- and repaints the world once. */
        int mode = tiku_trk_prefs_get(prefs, "DarkMode");

        if (mode < 0 || mode > 2) {
            mode = 0;               /* a file from elsewhere says anything */
        }
        /* The Deskbar's switch shows the same state, wherever it was
         * moved from. */
        tiku_trk_deskbar_set_theme(app->deskbar, mode);
        if (mode != app->theme_mode) {
            tiku_trk_folder_window_t *desk =
                tiku_trk_desktop_view_folder(&app->desktop_view);

            app->theme_mode = mode;
            tiku_theme_set(mode == 1 ? tiku_theme_dark()
                         : mode == 2 ? tiku_theme_mono() : NULL);
            for (i = 0; i < app->folder_count; i++) {
                tiku_trk_folder_window_forget(&app->folders[i]);
            }
            if (desk != NULL) {
                tiku_trk_folder_window_forget(desk);
            }
            app->dirty = 1;
        }
    }
    /* The file panel's top: with the setting on, Up from the root lands
     * on the Desktop and the Desktop is the top (AW-061).  Read live, so
     * a change re-answers the very next Up. */
    app->file_panel.model.desktop_is_root =
        tiku_trk_prefs_get(prefs, "DesktopFilePanelRoot") != 0;

    {   /* With BOTH storage settings off there is no doorway to the root
         * left on the Desktop, so a Disks window still open is a view of
         * something the user just asked not to see: it closes with the
         * icon (PVN-058). */
        int disks = tiku_trk_prefs_get(prefs, "ShowDisksIcon") != 0 ||
                    tiku_trk_prefs_get(prefs,
                                       "MountVolumesOntoDesktop") != 0;

        if (!disks) {
            for (i = app->folder_count - 1; i >= 0; i--) {
                folder_t *folder = &app->folders[i];

                if (folder->win != NULL && folder->win->open &&
                    strcmp(folder->path, "/") == 0) {
                    close_window(app, folder->win);
                    app->dirty = 1;
                }
            }
        }
    }

    if (full_path != app->applied_full_path) {
        /* kWindowsShowFullPathChanged: every OPEN window re-titles the
         * moment the preference moves (TS-047). */
        app->applied_full_path = full_path;
        for (i = 0; i < app->folder_count; i++) {
            folder_t *folder = &app->folders[i];

            if (folder->view != NULL && folder->win != NULL) {
                tiku_trk_folder_title_for(prefs, folder->path,
                                          folder->win->title,
                                          sizeof folder->win->title);
            }
        }
        app->dirty = 1;
    }

    /* Every turn, not only on change: a window opened or switched to list
     * mode since the last pass must pick the preference up too, and the
     * per-view guard keeps the steady state free (TS-052). */
    for (i = 0; i < app->folder_count; i++) {
        folder_t *folder = &app->folders[i];

        if (folder->view == NULL ||
            tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
            continue;
        }
        if (filter != tiku_trk_poseview_is_filtering(folder->view)) {
            tiku_trk_poseview_set_filtering(folder->view, filter);
            app->dirty = 1;
        }
    }
    if (hide == app->applied_hide_dotfiles &&
        folders_first == app->applied_folders_first) {
        return;
    }
    app->applied_hide_dotfiles = hide;
    app->applied_folders_first = folders_first;
    for (i = 0; i < app->folder_count; i++) {
        folder_t *folder = &app->folders[i];

        if (folder->view == NULL) {
            continue;
        }
        tiku_trk_poseview_set_hide_dotfiles(folder->view, hide);
        tiku_trk_poseview_set_folders_first(folder->view, folders_first);
    }
    app->dirty = 1;
}

/**
 * @brief Fill the Open-with submenu for what @p fl has selected (AW-040).
 *
 * Rebuilt per menu open rather than cached: the answer depends on the
 * selection, and a list that outlived the selection would offer an
 * application for something else (PVL-095, PVN-035).
 */

/**
 * @brief Open one entry that is not a folder: run it, or open it in
 *        whatever handles its type.
 *
 * The order is the whole point.  A file that can be EXECUTED is asked to
 * run itself first, because it needs nothing else to open it; only then
 * does the registry get asked which application handles the type.  The
 * other way round, double-clicking tiku-term asked what opens an
 * octet-stream, was told nothing does, and said so -- while the file
 * sitting there was a program the shell could have started.
 */
/**
 * @brief Close a window, persisting what it was showing first.
 *
 * State is written HERE rather than only at exit, so a session that is killed
 * does not lose every arrangement made since it started.
 *
 * Tracker quits when its last window closes, and that rule does NOT carry
 * over: there, the Desktop is a window of its own that outlives the folders.
 * Here the backdrop and the Deskbar ARE the application, so closing the last
 * folder must leave them standing.
 */
/**
 * @brief Close every window showing @p path or anything under it (FS-042).
 *
 * A window onto a folder that has just gone to the Trash is a window onto
 * nothing; leaving it up would let the user keep working in a folder whose
 * every path has changed underneath them.
 */
/**
 * @brief Let queued file work finish before the process goes.
 *
 * Bounded, because a job that cannot finish must not hold the application
 * open forever: the original gives up after five seconds and so does this.
 * What is already done stays done either way -- the engine has no
 * transaction above one item.
 */
/**
 * @brief Pixels a window built up frame by frame has wrong (PVL-045).
 *
 * A window that paints what changed is held against the picture it is
 * meant to equal: keep what is on the screen, throw the windows' kept
 * pixels away so the next draw builds from nothing, and count where the
 * two disagree.  Nothing is drawn before the copy is taken, since a frame
 * drawn to settle the window would be the very repaint the check exists
 * to do without; the from-scratch draw stands, being the right picture.
 */
int
tracker_run(int argc, char **argv)
{
    desktop_t app;
    desktop_options_t opt;
    desktop_loop_t lp;
    int code;

        {
        /* What a Restart starts over with: the same words, less the
         * -after that told THIS desk why it was started, because a
         * restart nobody crashed from has nothing to explain. */
        static char *again[64];
        int i, n = 0;

        for (i = 0; i < argc && n < 63; i++) {
            if (strcmp(argv[i], "-after") == 0 && i + 1 < argc) {
                i++;
                continue;
            }
            again[n++] = argv[i];
        }
        again[n] = NULL;
        restart_argv = again;
    }
    /* The words the interface is made of, before anything is drawn with
     * them: the language comes from the environment, the English is baked
     * in as the floor. */
    (void)tiku_strings_init(NULL, NULL);
    memset(&opt, 0, sizeof opt);
    opt.baud = 115200;
    opt.argv0 = argv[0];
    memset(&lp, 0, sizeof lp);
    lp.started_at = tiku_trk_now();
    memset(&app, 0, sizeof app);
    app.theme_mode = -1;      /* unapplied: the first sync decides */
    app.gbar_on = -1;
    app.gbar_open = -1;
    tiku_menubar_init(&app.gbar);
    (void)setlocale(LC_ALL, "");
    app.bar_open = -1;
    code = desktop_parse_args(argc, argv, &opt);
    if (code >= 0) {
        return code;
    }
    code = desktop_boot(&app, &opt, &lp);
    if (code >= 0) {
        return code;
    }

        /* Watched from here: a turn that does not end is the shape a shell
     * with one thread fails in, and nothing inside it can notice. */
        tiku_watch_install(0);
    tiku_report_ask_install();
    while (app.running) {
        tiku_watch_beat();
        desktop_turn_begin(&app, &opt, &lp);
        for (;;) {
            tiku_event_t ev;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (lp.sc != NULL) {
                tiku_trk_script_adapter_result_t script_result =
                    tiku_trk_script_adapter_next(&app.script_adapter, lp.sc,
                                                 &ev);

                if (script_result == TIKU_TRK_SCRIPT_ADAPTER_STOP) {
                    app.running = 0;
                    break;
                }
                if (script_result == TIKU_TRK_SCRIPT_ADAPTER_SHOT) {
                    draw_all(&app);
                    (void)tiku_surface_png(
                        app.surf, app.script_adapter.shot_path);
                }
                have = script_result == TIKU_TRK_SCRIPT_ADAPTER_EVENT;
            } else if (app.cq_head != app.cq_tail) {
                /* An injected event, taken in the order it arrived and
                 * handled by everything below exactly as the display's
                 * own would be. */
                ev = app.cq[app.cq_head];
                app.cq_head = (app.cq_head + 1) %
                              (int)(sizeof app.cq / sizeof app.cq[0]);
                have = 1;
            } else if (app.host != NULL && tiku_host_poll(app.host,
                                                               &ev)) {
                have = 1;
            }
            if (!have) {
                break;
            }
            if (app.closing) {
                /* Still taken OFF the queue -- a display whose events go
                 * unread stops sending them -- and then dropped: a click
                 * during the second the screen is going under would open
                 * something nobody is going to see. */
                continue;
            }
            if (ev.type != TIKU_EVENT_TEXT_INPUT)
                (void)tiku_text_input_event(&app.legacy_commit, &ev);
            switch (ev.type) {
            case TIKU_EVENT_TEXT_INPUT:
                desktop_text_input(&app, &ev);
                break;
            case TIKU_EVENT_CLIPBOARD:
                clip_mirror(&app);
                if (ev.key != TIKU_CLIPBOARD_OK) {
                    snprintf(app.status, sizeof app.status, "%s",
                             tiku_clipboard_status_text((int)ev.key));
                    app.dirty = 1;
                }
                break;
            case TIKU_EVENT_DEACTIVATED: {
                tiku_window_t *fw = tiku_workspace_focused(app.desk);
                if (app.prefs_track && app.prefs_track != fw)
                    (void)tiku_window_send(app.prefs_track, &ev);
                app.prefs_track = NULL;
                if (fw == desktop_deviceprefs_handle(&app) ||
                    desktop_utility_owns(&app, fw) ||
                    fw == desktop_startup_handle(&app))
                    (void)tiku_window_send(fw, &ev);
                break;
            }
            case TIKU_EVENT_RESIZE:
                (void)resize_desktop(&app, ev.width, ev.height);
                app.dirty = 1;
                break;
            case TIKU_EVENT_EXPOSE:
                app.dirty = 1;
                break;
            case TIKU_EVENT_POINTER_DOWN:
                desktop_pointer_down(&app, &ev, &lp);
                break;
            case TIKU_EVENT_POINTER_MOVE:
                desktop_pointer_move(&app, &ev, &lp);
                break;
            case TIKU_EVENT_POINTER_UP:
                desktop_pointer_up(&app, &ev, &lp);
                break;
            case TIKU_EVENT_KEY_UP:
                if (ev.type == TIKU_EVENT_KEY_DOWN ||
                    ev.type == TIKU_EVENT_POINTER_DOWN ||
                    ev.type == TIKU_EVENT_POINTER_MOVE) {
                    /* The user did something: idle-only work starts its
                     * wait again (AW-085). */
                    tiku_trk_loop_note_activity(app.loop);
                }
                if (tiku_trk_switcher_window_shown(&app.switcher_window) &&
                    ev.key == TIKU_KEY_CMD) {
                    /* Released inside the hair trigger the user meant the
                     * previous application; held longer they meant the
                     * list, and letting go now is the choice (DB-054). */
                    (void)tiku_trk_switcher_window_release(
                        &app.switcher_window, tiku_trk_now());
                    tiku_trk_switcher_window_close(&app.switcher_window, 1);
                    break;
                }
                /* fall through */
            case TIKU_EVENT_KEY_DOWN:
                desktop_key_down(&app, &ev, &lp);
                break;
            case TIKU_EVENT_CLOSE:
                app.running = 0;
                break;
            default:
                break;
            }
            if (lp.sc != NULL) {
                break;              /* one instruction per turn */
            }
        }
        desktop_turn_end(&app, &opt, &lp);
    }

    return desktop_teardown(&app, &lp);
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker",
    .name = "TikuTracker",
    .run = tracker_run
};
