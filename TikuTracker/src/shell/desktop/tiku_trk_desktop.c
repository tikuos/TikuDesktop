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

/** @brief Apply folder-list settings to every already-open spatial view. */
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
 * A window that paints what changed can only be held against the picture
 * it is meant to be equal to: keep what is on the screen, throw the
 * windows' kept pixels away so the next draw builds them from nothing, and
 * count where the two disagree.  Nothing is drawn before the copy is
 * taken -- a frame drawn to settle the window would be the very repaint
 * the check exists to do without.  The from-scratch draw stands: it is the
 * picture the shell should be showing anyway.
 */
int
tracker_run(int argc, char **argv)
{
    desktop_t app;
    desktop_options_t opt;
    desktop_loop_t lp;
    int code, i;

    restart_argv = argv;
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

    while (app.running) {
        {   /* The settings FILE is a doorway too: a change written by
             * anything -- another shell, a script, an editor -- lands in
             * the live model, the reload the browser shell already does
             * (TS-052, TS-047). */
            static time_t settings_mtime;
            static off_t settings_size;
            struct stat sst;

            if (app.prefs_window.path[0] != '\0' &&
                stat(app.prefs_window.path, &sst) == 0 &&
                (sst.st_mtime != settings_mtime ||
                 sst.st_size != settings_size)) {
                settings_mtime = sst.st_mtime;
                settings_size = sst.st_size;
                (void)tiku_trk_prefs_load(prefs_model(&app),
                                          app.prefs_window.path);
                app.dirty = 1;
            }
        }
        {   /* The scripting suite's transport (PVK-052): a command file
             * whose change delivers verb/property/specifier lines into
             * this running shell. */
            static int64_t command_stamp;
            struct stat cst;
            char cpath[TIKU_PATH_MAX];
            const char *chome = getenv("HOME");

            snprintf(cpath, sizeof cpath, "%s/.config/tracker/command",
                     (chome != NULL && chome[0] != '\0') ? chome : "");
            if (stat(cpath, &cst) == 0) {
                /* Nanoseconds, not seconds: two commands written within
                 * one second are two commands. */
                int64_t stamp = (int64_t)cst.st_mtim.tv_sec * 1000000000 +
                                cst.st_mtim.tv_nsec;

                if (stamp != command_stamp) {
                    command_stamp = stamp;
                    script_command_file(&app, cpath);
                }
            }
        }
        if (tiku_remote_poll(&app.remote)) {
            app.dirty = 1;
        }
        if (app.conduct_on) {
            (void)tiku_conduct_poll(&app.conduct);
        }
        remote_reconcile(&app);
        remote_hear(&app);
        clip_mirror(&app);
        pick_sweep(&app);
        remote_sweep(&app);
        sync_folder_preferences(&app);
        sync_addons(&app, 0);
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
            switch (ev.type) {
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

        if ((app.nav_on && app.popup != NULL) || app.spring_nav_on) {
            /* A chunk per turn: the menu answers the mouse while a large
             * folder is still arriving (CW-055). */
            if (tiku_trk_nav_fill(&app.nav, app.nav.depth - 1)) {
                if (app.spring_nav_on) {
                    /* What the drag can land on is per entry, so fresh
                     * entries get their states as they arrive (PVS-052). */
                    spring_nav_states(&app);
                }
                app.dirty = 1;
            }
        } else if (app.nav_on && app.popup == NULL) {
            app.nav_on = 0;         /* the menu went; so does the walk */
        }
        {   /* Ask the entries that have not been asked whether they have
             * a picture of their own -- a bounded batch per turn, for the
             * same reason the thumbnails are made in one: a folder of a
             * thousand images must not open a thousand files inside one
             * tick (IV-005, IV-037). */
            int budget = HINTS_PER_TURN;

            for (i = 0; i < app.folder_count && budget > 0; i++) {
                folder_t *fl = &app.folders[i];
                int k, n;

                if (fl->win == NULL || !fl->win->open) {
                    continue;
                }
                n = tiku_trk_poseview_count(fl->view);
                for (k = 0; k < n && budget > 0; k++) {
                    tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, k);

                    if (p == NULL || p->model.facts.icon_hint !=
                                         TIKU_ICONHINT_UNASKED) {
                        continue;
                    }
                    (void)tiku_trk_nodeicon_hint(app.store, &app.thumbs,
                                                 &p->model);
                    budget--;
                    /* One pose learned what picture it has, so one row is
                     * what needs painting: a bare dirty here repainted the
                     * whole listing and undid the damage the notice had
                     * just recorded (PVL-075). */
                    tiku_trk_poseview_damage_row(fl->view, k);
                    app.dirty = 1;
                }
            }
        }
        /* One directory of one folder-size walk per turn, so a Get Info
         * window on a deep tree fills in while the desktop keeps answering. */
        (void)tiku_trk_info_windows_calc_step(&app.info_windows);
        if (tiku_thumb_step(&app.thumbs, app.store)) {
            /* One picture per turn, on the idle tick: the window keeps
             * answering while a folder of images fills in (IV-031). */
            app.dirty = 1;
        }
        (void)tiku_trk_band_controller_pulse(&app.band_controller,
                                             tiku_trk_now());
        (void)tiku_trk_drag_target_pulse(&app.drag_target,
                                         tiku_trk_now());

        /* PVS-017: a button held still for the click interval opens the
         * context menu without waiting for the release. */
        if (app.deskbar_press &&
            tiku_trk_now() - app.press_at > 2 * TIKU_TRK_CLICK_SPEED_US) {
            tiku_trk_db_pick_t dbpick;

            /* A still primary press is the Deskbar's long-press equivalent
             * of a secondary click.  It replaces the calendar with clock
             * options without waiting for release. */
            tiku_trk_deskbar_close_calendar(app.deskbar);
            (void)tiku_trk_deskbar_click(app.deskbar, app.press_x,
                                         app.press_y, 0u, 3, 1, &dbpick);
            app.deskbar_press = 0;
            app.dirty = 1;
        }
        if (app.press_down && !app.press_menu && app.popup == NULL &&
            app.press_fl != NULL &&
            tiku_trk_now() - app.press_at > TIKU_TRK_CLICK_SPEED_US) {
            folder_t *pfl = app.press_fl;
            tiku_trk_pose_t *p = (app.press_row >= 0)
                ? tiku_trk_poseview_at(pfl->view, app.press_row) : NULL;
            tiku_trk_sc_ctx_t c;

            tiku_trk_poseview_cancel_edit_wait(pfl->view);
            commit_rename(&app, 1);            /* CW-029: mouse-opened */
            (void)tiku_trk_poseview_context_click(pfl->view, app.press_row);
            c = menu_ctx(&app, pfl, 0u);
            build_open_with(&app, pfl);
            build_per_type(&app, pfl);
            app.popup = tiku_trk_cwmenu_context(&app.menus,
                            (p != NULL) ? &p->model : NULL, &c);
            app.popup_sub = NULL;
            app.popup_frame = tiku_menu_place(app.popup, app.press_x,
                app.press_y, (tiku_rect_t){ 0, 0, app.surf->w,
                                                 app.surf->h });
            app.press_menu = 1;
            app.press_down = 0;
            app.dirty = 1;
        }
        /* Tracker's Pulse: a rename armed by a slow second click starts
         * here rather than on the click itself (MA-043). */
        for (i = 0; i < app.folder_count; i++) {
            folder_t *fl = &app.folders[i];
            int due;

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            due = tiku_trk_poseview_edit_due(fl->view, tiku_trk_now());
            if (due >= 0 && !app.folder_edit.editing) {
                begin_rename(&app, fl, due);
                app.dirty = 1;
            }
        }
        /* PVS-023: whichever window has focus paints its selection; the
         * rest own theirs without showing it.  Done once per cycle so every
         * path that changes focus is covered rather than each one
         * remembering to. */
        {
            tiku_window_t *fw = tiku_workspace_focused(app.desk);

            for (i = 0; i < app.folder_count; i++) {
                folder_t *fl = &app.folders[i];

                if (fl->win == NULL || !fl->win->open) {
                    continue;
                }
                tiku_trk_poseview_window_activated(fl->view,
                                                   fl->win == fw);
            }
        }

        /* One pump serves every window and the tray, so a device is read
         * once per cycle however many windows are showing it. */
        for (i = 0; i < app.folder_count; i++) {
            /* One batch per window per turn: a folder still arriving keeps
             * the desktop drawing rather than holding the loop. */
            if (app.folders[i].win != NULL && app.folders[i].win->open &&
                tiku_trk_poseview_loading(app.folders[i].view)) {
                (void)tiku_trk_poseview_load_step(app.folders[i].view,
                                                  tiku_trk_now());
                app.dirty = 1;
            }
        }
        if (tiku_trk_now() - lp.last_sweep > 60000000) {
            /* The reaping of searches nobody named.  On a slow tick of its
             * own and skipped for the first half hour, because it is the
             * least urgent work there is (Q-037, AW-090). */
            const char *open[TIKU_WINDOW_MAX];
            int nopen = 0;

            for (i = 0; i < app.folder_count; i++) {
                if (app.folders[i].qname[0] != '\0' &&
                    app.folders[i].win != NULL && app.folders[i].win->open) {
                    open[nopen++] = app.folders[i].qname;
                }
            }
            if (!tiku_trk_task_system_busy()) {
                /* Deleting user data is idle-only work twice over: the
                 * half-hour hold-off AND the machine's own load gate
                 * (AW-090). */
                (void)tiku_query_sweep(app.store, (int64_t)time(NULL),
                                           (tiku_trk_now() - lp.started_at) /
                                               1000000, open, nopen);
            }
            lp.last_sweep = tiku_trk_now();
        }
        if (tiku_trk_now() - lp.last_pump > 400000) {
            int changed = 0;
            {
                /* What the leaf menu offers above the Deskbar folder.
                 * Refreshed on the tick rather than read by the bar: they
                 * are the same lists the Open panel keeps (DB-002). */
                static const tiku_trk_recent_t kWhich[] = {
                    TIKU_TRK_RECENT_DOCS, TIKU_TRK_RECENT_FOLDERS,
                    TIKU_TRK_RECENT_APPS
                };
                char paths[TIKU_TRK_DB_RECENT_MAX][TIKU_PATH_MAX];
                int k;

                for (k = 0; k < 3; k++) {
                    int nr = tiku_trk_recent_list(app.store, kWhich[k],
                                 TIKU_TRK_DB_RECENT_MAX, paths,
                                 TIKU_TRK_DB_RECENT_MAX);

                    tiku_trk_deskbar_set_recent(app.deskbar,
                        (tiku_trk_db_recent_t)k, paths, nr);
                }
            }
            /*
             * Drained ONCE for the whole desktop: reading the device's ring
             * takes the records out of it, so a per-window drain would let
             * the first window consume everyone else's and leave them told
             * that nothing happened.
             */
            tiku_model_change_t ch[32];
            int nch = -1, complete = 1;

            if (app.backend->ops->changes != NULL) {
                nch = app.backend->ops->changes(app.backend, ch, 32,
                                                &complete);
            }
            for (i = 0; i < app.folder_count; i++) {
                folder_t *fl = &app.folders[i];
                unsigned nf;

                if (fl->win == NULL || !fl->win->open) {
                    continue;
                }
                changed += tiku_trk_poseview_pump(fl->view, 5);
                /* Membership changes go through the notice handler rather
                 * than a reload: a re-read would rebuild every row and take
                 * the selection, the scroll position and the arrangement
                 * with it. */
                nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(),
                                            ch, nch, complete);
                if ((nf & TIKU_TRK_NOTIFY_GONE) != 0u) {
                    /* The folder is gone.  Its window goes with it, and its
                     * position is deliberately NOT saved -- there is nothing
                     * left for it to be the position of, and the teardown
                     * sweep must not write one either. */
                    fl->gone = 1;
                    tiku_workspace_close(app.desk, fl->win);
                    changed++;
                } else if (nf != TIKU_TRK_NOTIFY_NOTHING) {
                    if ((nf & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
                        tiku_trk_folder_title_for(prefs_model(&app),
                            tiku_trk_poseview_path(fl->view),
                            fl->win->title, sizeof fl->win->title);
                        snprintf(fl->path, sizeof fl->path, "%s",
                                 tiku_trk_poseview_path(fl->view));
                    }
                    changed++;
                }
            }
            changed += tiku_trk_desktop_view_poll(&app.desktop_view,
                tiku_trk_now(), ch, nch, complete);
            changed += tiku_trk_panel_window_poll(&app.file_panel,
                tiku_trk_now(), ch, nch, complete);
            {   /* What has been mounted or unmounted since the last look.
                 * Polled with everything else rather than on a timer of its
                 * own: a volume arriving is a change like any other. */
                tiku_volchange_t vc[TIKU_VOLWATCH_MAX];
                int nv = tiku_volwatch_poll(&app.volwatch, vc,
                                                TIKU_VOLWATCH_MAX);
                int k;

                for (k = 0; k < nv; k++) {
                    if (vc[k].renamed) {
                        /* Only the label moved: the union rebuild re-reads
                         * every volume's name, and the relist refreshes
                         * open listings (MA-021). */
                        tiku_trk_desktop_view_rebuild(&app.desktop_view);
                        changed++;
                        continue;
                    }
                    if (vc[k].mounted) {
                        /* A folder window showing the parent finds it by
                         * re-listing, which the poll above already did.
                         * The Desktop union gains the volume's Desktop
                         * folder as a source, live (PVN-040, PVN-051). */
                        tiku_trk_desktop_view_rebuild(&app.desktop_view);
                        /* And half a second on, broken links get another
                         * look: by then a link to the new volume resolves
                         * as a volume (PVN-042). */
                        (void)tiku_trk_run_later(app.loop,
                            refresh_after_mount, &app, NULL, 500000);
                        changed++;
                        continue;
                    }
                    /* And loses it: a source that has gone must not be
                     * polled for. */
                    tiku_trk_desktop_view_rebuild(&app.desktop_view);
                    /* Unmounted.  A window ON the volume closes -- and its
                     * arrangement is deliberately NOT saved, because the
                     * disk it belongs to has gone; a window merely SHOWING
                     * rows from it drops those rows (PVN-043). */
                    for (i = 0; i < app.folder_count; i++) {
                        folder_t *fl = &app.folders[i];

                        if (fl->win == NULL || !fl->win->open) {
                            continue;
                        }
                        if (tiku_trk_poseview_is_query(fl->view) &&
                            tiku_query_targets_device(
                                fl->run, (uint64_t)vc[k].v.dev)) {
                            /* The run is the query's subscription.  Stop it
                             * with the window so no watch survives a target
                             * volume disappearing (Q-046). */
                            tiku_query_stop(fl->run);
                            fl->run = NULL;
                            fl->live = 0;
                            fl->gone = 1;
                            tiku_workspace_close(app.desk, fl->win);
                            changed++;
                        } else if (strncmp(fl->path, vc[k].v.mount,
                                    strlen(vc[k].v.mount)) == 0) {
                            fl->gone = 1;
                            tiku_workspace_close(app.desk, fl->win);
                            changed++;
                        } else if (tiku_trk_poseview_device_gone(fl->view,
                                       (uint64_t)vc[k].v.dev) > 0) {
                            changed++;
                        }
                    }
                    changed += tiku_trk_panel_window_device_gone(
                        &app.file_panel, vc[k].v.mount);
                    changed += tiku_trk_info_windows_device_gone(
                        &app.info_windows, (uint64_t)vc[k].v.dev);
                }
            }
            changed += tiku_trk_info_windows_refresh(&app.info_windows,
                                                      tiku_trk_now());
            changed += tiku_trk_deskbar_refresh(app.deskbar);
            /* Live queries re-evaluate on a slower beat than values are
             * read: a re-walk costs a directory listing per subtree, and a
             * result set that moves every 400 ms is unreadable anyway. */
            if (tiku_trk_now() - lp.last_requery > 3000000) {
                for (i = 0; i < app.folder_count; i++) {
                    if (app.folders[i].win != NULL &&
                        app.folders[i].win->open) {
                        changed += refresh_results(&app, &app.folders[i]);
                        if (app.folders[i].trash_union) {
                            changed += trash_union_refresh(&app,
                                                           &app.folders[i]);
                        }
                    }
                }
                changed += tiku_trk_desktop_view_trash_check(
                    &app.desktop_view);
                lp.last_requery = tiku_trk_now();
            }
            lp.last_pump = tiku_trk_now();
            if (changed > 0) {
                app.dirty = 1;
            }
        }
        if (opt.shot != NULL && lp.shot_at > 0 && tiku_trk_now() >= lp.shot_at) {
            app.dirty = 1;
        }
        if (app.dirty) {
            draw_all(&app);
            blit(&app);
            if (opt.shot != NULL && lp.shot_at > 0 && tiku_trk_now() >= lp.shot_at) {
                (void)tiku_surface_png(app.surf, opt.shot);
                printf("wrote %s\n", opt.shot);
                fflush(stdout);
                lp.shot_at = 0;
            }
        }
        if (app.fs != NULL && tiku_fs_busy(app.fs) &&
            !progress_model(&app)->row[0].paused) {
            const tiku_progress_t *pr;

            /* Stepped here, not inside the command: start() only builds the
             * queue, so a shell that never steps reports work it has not
             * done.  Membership changes reach the windows through the
             * notice poll above, which is why nothing is reloaded here. */
            (void)tiku_fs_step(app.fs, 4);
            if (!tiku_fs_busy(app.fs)) {
                /* The operation just finished: what it MADE gets selected
                 * where it lives, once it shows up (AW-033). */
                char made[8][TIKU_PATH_MAX];
                int nmade = tiku_fs_last_created(app.fs, made, 8);
                int mi;

                for (mi = 0; mi < nmade; mi++) {
                    select_created_t *scx = malloc(sizeof *scx);

                    if (scx == NULL) {
                        break;
                    }
                    scx->app = &app;
                    snprintf(scx->path, sizeof scx->path, "%s", made[mi]);
                    (void)tiku_trk_run_periodic(app.loop, select_created,
                                                scx, free, 200000,
                                                5000000);
                }
            }
            /* A mixed paste left its second half waiting for the engine to
             * go idle; this is where it gets to start (FS-055). */
            (void)tiku_trk_folder_file_actions_pump(&app.file_actions);
            pr = tiku_fs_progress(app.fs);
            if (pr->conflict) {
                /* Asked, not assumed.  The engine worked out which form
                 * the question takes; this puts it up and waits. */
                if (!alert_model(&app)->open) {
                    tiku_trk_alert_for_conflict(alert_model(&app), pr);
                }
            } else if (pr->error_ask) {
                if (!alert_model(&app)->open) {
                    tiku_trk_alert_for_error(alert_model(&app), pr);
                }
            } else {
                snprintf(app.status, sizeof app.status, "%d of %d%s%s",
                         pr->done + pr->skipped, pr->total,
                         pr->skipped ? " (some skipped)" : "",
                         pr->failed ? " -- some failed" : "");
            }
            if (tiku_trk_progress_wanted(pr->op, pr->total)) {
                /* Where it is going, which is the other half of "what is
                 * happening": a file name alone does not say whether this
                 * is the copy the user meant (FS-069). */
                tiku_trk_progress_update(progress_model(&app), 0, pr, 1,
                                         pr->to[0] != '\0' ? pr->to
                                         : app.prog_dest[0] != '\0'
                                             ? app.prog_dest : NULL);
                tiku_trk_progress_sample(progress_model(&app), 0, pr->bytes,
                                         tiku_trk_now());
            }
            app.dirty = 1;
            if (tiku_trk_status_overlay_progress_opened(
                    &app.status_overlay) == 0) {
                tiku_trk_status_overlay_progress_start(&app.status_overlay,
                                                       tiku_trk_now());
                /* Whoever was in front when the job began gets the focus
                 * back when it ends: an operation is not a reason to be
                 * left looking at something else (FS-073). */
                app.prog_was_active = tiku_workspace_focused(app.desk);
            }
        } else if (progress_model(&app)->open) {
            tiku_trk_progress_update(progress_model(&app), 0, NULL, 0, NULL);
            tiku_trk_status_overlay_progress_finish(&app.status_overlay);
            if (app.prog_was_active != NULL &&
                app.prog_was_active->open) {
                tiku_workspace_activate(app.desk, app.prog_was_active);
            }
            app.prog_was_active = NULL;
            app.dirty = 1;
        }
        if (opt.deadline > 0 && tiku_trk_now() > opt.deadline) {
            app.running = 0;
        }
        if (app.closing &&
            tiku_trk_splash_closed(&app.splash, tiku_trk_now())) {
            /* The black has arrived and held.  What is left is the
             * putting away, and it is named on the screen below. */
            app.running = 0;
        }
        /* A script still lets the clock run, just faster: the notice
         * poll, the armed rename and the barber pole are all timed, and a
         * loop that spun without time passing would test none of them. */
        if (app.loop != NULL) {
            /* A task that changed something on screen dirties the surface
             * itself; running is not, by itself, a repaint. */
            (void)tiku_trk_loop_pulse(app.loop);
            if (app.sbar_fl != NULL && app.sbar_fl->view != NULL) {
                /* A held arrow keeps stepping (NeXT UIG ch3). */
                int rep = tiku_scroll_arrow_repeat(&app.sbar_fl->sbar);

                if (rep != 0) {
                    tiku_trk_poseview_scroll_to(app.sbar_fl->view,
                        tiku_trk_poseview_scroll(app.sbar_fl->view) +
                            rep * tiku_scroll_line(&app.sbar_fl->sbar));
                    app.dirty = 1;
                }
            }
        }
        {
            /* Sleep until the next task is due, or one heartbeat,
             * whichever is sooner (AW-086). */
            int64_t nap = (lp.sc != NULL) ? 20000 : 10000;
            int64_t due = (app.loop != NULL)
                              ? tiku_trk_loop_next_due(app.loop) : -1;

            if (due >= 0 && due < nap) {
                nap = (due < 1000) ? 1000 : due;
            }
            usleep((useconds_t)nap);
        }
    }

    return desktop_teardown(&app, &lp);
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker",
    .name = "TikuTracker",
    .run = tracker_run
};
