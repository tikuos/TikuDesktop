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
    int want_devices = 0;
    const char *port = NULL, *shot = NULL, *script_path = NULL;
    /* The conductor channel is OFF unless asked for by name: a shell that
     * anything reaching its socket could puppet is a shell with a hole in
     * it.  The path is given rather than derived so a harness can run more
     * than one driven shell at once. */
    const char *conduct_path = NULL;
    /* The same channel over a LINE instead of a socket: this is how a
     * shell on a real board is driven, from whatever is on the other end
     * of its console cable. */
    const char *conduct_tty = NULL;
    /* Stage 3: names on the channel.  Either may stand alone; requiring
     * one turns every unnamed peer away at HELLO. */
    const char *conduct_token = NULL;
    const char *conduct_query_token = NULL;

    restart_argv = argv;
    /* The words the interface is made of, before anything is drawn with
     * them: the language comes from the environment, the English is baked
     * in as the floor. */
    (void)tiku_strings_init(NULL, NULL);
    tiku_trk_script_t *sc = NULL;
    int rc = 0;
    int baud = 115200, use_device = 0, i;
    int64_t shot_at = 0, deadline = 0, last_pump = 0, last_requery = 0;
    int64_t last_sweep = 0, started_at = tiku_trk_now();
    int shot_menu = 0;
    const char *shot_edit = NULL, *shot_find = NULL;
    int shot_panel = 0, shot_live = 0, shot_shift = 0, shot_zoom = 0;
    int shot_cascade = 0, shot_notify = 0, shot_select = 0;
    desktop_loop_t lp = { 0 };
    const char *openme[TIKU_WINDOW_MAX];
    int nopen = 0;
    /* Safe mode starts deliberately plain: it restores no session.  The
     * row's other clauses -- replicants and add-ons -- have nothing here to
     * suppress (AW-007). */
    int safe_mode = 0;

    memset(&app, 0, sizeof app);
    app.theme_mode = -1;      /* unapplied: the first sync decides */
    app.gbar_on = -1;
    app.gbar_open = -1;
    tiku_menubar_init(&app.gbar);
    (void)setlocale(LC_ALL, "");
    app.bar_open = -1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-version") == 0 ||
            strcmp(argv[i], "--version") == 0) {
            /* WHICH build this is, said before anything else: four
             * copies of this program live in a development tree, and
             * "the code is still old" has to be answerable in one
             * line rather than by archaeology. */
            printf("TikuDesktop, built %s %s\n", __DATE__, __TIME__);
            return 0;
        }
        if (strcmp(argv[i], "-reset-settings") == 0 ||
            strcmp(argv[i], "--reset-settings") == 0) {
            /* The rescue path: settings back to factory WITHOUT drawing a
             * pixel, for the day a setting makes the screen unreadable.
             * The files are moved aside rather than destroyed. */
            const char *home = getenv("HOME");
            char from[TIKU_PATH_MAX], to[TIKU_PATH_MAX];

            if (home == NULL || home[0] == '\0') {
                fprintf(stderr, "reset-settings: no HOME\n");
                return 1;
            }
            snprintf(from, sizeof from, "%s/.config/TrackerSettings", home);
            snprintf(to, sizeof to, "%s.factory-reset", from);
            if (rename(from, to) == 0) {
                printf("moved %s -> %s\n", from, to);
            }
            {
                char dir[TIKU_PATH_MAX];
                DIR *d;
                struct dirent *e;

                snprintf(dir, sizeof dir,
                         "%s/.local/share/tracker/devices", home);
                d = opendir(dir);
                while (d != NULL && (e = readdir(d)) != NULL) {
                    size_t len = strlen(e->d_name);

                    if (len > 6u &&
                        strcmp(e->d_name + len - 6u, ".state") == 0) {
                        snprintf(from, sizeof from, "%s/%s", dir,
                                 e->d_name);
                        snprintf(to, sizeof to, "%s.factory-reset", from);
                        if (rename(from, to) == 0) {
                            printf("moved %s -> %s\n", from, to);
                        }
                    }
                }
                if (d != NULL) {
                    (void)closedir(d);
                }
            }
            printf("settings are factory defaults on the next start\n");
            return 0;
        }
        if (strcmp(argv[i], "-d") == 0)                      { use_device = 1; }
        /*
         * Every board that answers, each under its own name in the
         * namespace -- as against -d, which puts ONE board at the root
         * and is the arrangement that made two devices mean two
         * desktops.  Both are kept: -d is what a device tool wants.
         */
        else if (strcmp(argv[i], "-devices") == 0) { want_devices = 1; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { port = argv[++i]; use_device = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { shot = argv[++i]; }
        else if (strcmp(argv[i], "-shotmenu") == 0) { shot_menu = 1; }
        else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc) { script_path = argv[++i]; }
        else if (strcmp(argv[i], "-conduct") == 0 && i + 1 < argc) { conduct_path = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-tty") == 0 && i + 1 < argc) { conduct_tty = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-token") == 0 && i + 1 < argc) { conduct_token = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-query-token") == 0 && i + 1 < argc) { conduct_query_token = argv[++i]; }
        else if (strcmp(argv[i], "-shotedit") == 0 && i + 1 < argc) {
            shot_edit = argv[++i];
        }
        else if (strcmp(argv[i], "-shotfind") == 0 && i + 1 < argc) {
            shot_find = argv[++i];
        }
        else if (strcmp(argv[i], "-shotpanel") == 0) { shot_panel = 1; }
        else if (strcmp(argv[i], "-shotlive") == 0) { shot_live = 1; }
        else if (strcmp(argv[i], "-safe") == 0) { safe_mode = 1; }
        else if (strcmp(argv[i], "-shotshift") == 0) { shot_shift = 1; }
        else if (strcmp(argv[i], "-shotzoom") == 0) { shot_zoom = 1; }
        else if (strcmp(argv[i], "-shotcascade") == 0) { shot_cascade = 1; }
        else if (strcmp(argv[i], "-shotnotify") == 0) { shot_notify = 1; }
        else if (strcmp(argv[i], "-shotselect") == 0) { shot_select = 1; }
        else if (argv[i][0] != '-' && nopen < (int)(sizeof openme /
                                                    sizeof openme[0])) {
            /* AW-100: EVERY path argument opens its own window.  Keeping
             * only the last is how a shell glob silently opens one folder
             * out of the several the user asked for. */
            openme[nopen++] = argv[i];
        }
        else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) {
            deadline = tiku_trk_now() + (int64_t)atoi(argv[++i]) * 1000000;
        }
    }
    {
        /*
         * The shell holds a NAMESPACE, never a backend directly -- even
         * with one thing in it.  A single backend at the root routes to
         * itself and behaves exactly as it did; what it buys is that a
         * device arriving later is an entry in a table rather than a
         * second desktop, and that every view above already asks its
         * questions of something that can answer for more than one
         * store.
         */
        tiku_backend_t *root = use_device
                                   ? tiku_backend_tiku_open(port, baud)
                                   : tiku_backend_posix_open();

        if (root == NULL) {
            fprintf(stderr, "tracker: no backend\n");
            return 1;
        }
        app.backend = tiku_mount_open();
        if (app.backend == NULL ||
            tiku_mount_add(app.backend, "/", root, 1) != 0) {
            fprintf(stderr, "tracker: no namespace\n");
            return 1;
        }
    }
    if (want_devices) {
        (void)mount_devices(&app, port, baud);
    }
    {
        /*
         * The local files again, under a second name.
         *
         * A diagnostic, and said plainly: everything above the backend
         * seam sees a second mount exactly as it would see a second
         * machine, and on a host with one filesystem and no board
         * attached there is otherwise no way to put the routing under a
         * real window.  It offers what is already at the root, so the
         * two names are the same files reached two ways -- which is the
         * shape that catches a view holding a path from one mount and
         * asking the other about it.
         */
        const char *mirror = getenv("TIKU_MOUNT");

        if (mirror != NULL && mirror[0] == '/' && mirror[1] != '\0') {
            /*
             * Its OWN backend, not the one already at the root.  Sharing
             * the object would make the two names one store as far as
             * anything that asks "are these the same store" is concerned
             * -- which is the question a copy between mounts turns on,
             * and the whole thing this exists to put under a test.
             */
            tiku_backend_t *second = tiku_backend_posix_open();

            if (second == NULL ||
                tiku_mount_add(app.backend, mirror, second, 1) != 0) {
                tiku_backend_close(second);
            }
        }
    }
    app.fs = tiku_fs_new(app.backend);
    (void)tiku_fs_prepare_trash(app.fs);
    app.backdrop_color = TIKU_C_BACKDROP;
    app.surf = tiku_surface_new(DESK_W, DESK_H, app.backdrop_color);
    /*
     * The window goes up BEFORE the work does.  It used to be opened
     * after everything was built, which is fine for a shell that appears
     * all at once and useless for one that wants to say what it is
     * doing: a boot screen that arrives after the boot has nothing left
     * to report.  The displayless runs -- scripted, and driven over the
     * conductor channel -- still take no window at all.
     */
    if (script_path == NULL && conduct_path == NULL && conduct_tty == NULL) {
        app.host = tiku_host_open(DESK_W, DESK_H, "TikuOS Desktop");
        if (app.host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
        if (tiku_host_scale(app.host) > 1) {
            (void)tiku_surface_rescale(app.surf,
                tiku_host_scale(app.host), app.backdrop_color);
        }
    }
    /*
     * A scripted run gets no splash unless it asks: the dwell is a
     * second and a half, and 164 scripts would each pay it to watch a
     * screen they are not looking at.  The one script that IS looking
     * sets TIKU_SPLASH.
     */
    tiku_trk_splash_begin(&app.splash,
        app.host != NULL || getenv("TIKU_SPLASH") != NULL, tiku_trk_now());
    splash_stage(&app, "storage");
    app.row_h = tiku_font_plain()->height + 5;
    app.desk = tiku_workspace_new(app.surf);
    app.deskbar = tiku_trk_deskbar_new(app.backend);
    app.team = tiku_trk_deskbar_add_team(app.deskbar, "TikuTracker");
    splash_stage(&app, "the Deskbar");
    /* The machine's own processes are not this desktop's business: what
     * belongs in the bar is what runs ON the device, which is this shell
     * and the applications it hosts. */
    tiku_trk_cwmenu_init(&app.menus);
    tiku_trk_status_overlay_init(&app.status_overlay, app.surf,
        DESK_W, DESK_H, &app.dirty, status_answer, status_stop, &app);
    tiku_trk_find_window_init(&app.find_window, app.desk, DESK_W, DESK_H,
        &app.dirty, app.status, sizeof app.status, panel_search, &app);
    prepare_places(&app);
    offer_installed(&app, argv[0]);
    /*
     * The stream's icon player, injected below the kit boundary: the
     * Interface kit carries icon commands without knowing what HVIF is,
     * and this is the one line that teaches this end to draw them.
     */
    tiku_dl_set_icon_painter(tiku_icon_stream_play);
    if (tiku_remote_listen(&app.remote) != 0) {
        /* No socket, no sessions: the desktop still stands alone. */
        app.remote.fd = -1;
    }
    if (getenv("TIKU_EMBED_DEMO") != NULL) {
        /* The both-ways proof: the SAME descriptor the client runner
         * takes out of process, hosted here linked-in. */
        (void)embed_start(&app, &tiku_demo_app);
    }
    app.loop = tiku_trk_loop_new();
    /* Once a second, so the displayed minute can never go stale while
     * nothing else dirties the surface (DB-044). */
    (void)tiku_trk_run_periodic(app.loop, tick_clock, &app, NULL, 1000000,
                                0);
    /* Fast enough that a fade of sixteen steps reads as continuous, and
     * cheap because a tick that changes nothing frames nothing.  75 ms:
     * the About's fade is 1.2 seconds, and every hosted application's
     * tick rides the same beat rather than each arriving with a timer
     * of its own. */
    (void)tiku_trk_run_periodic(app.loop, tick_embeds, &app, NULL,
                                75000, 0);
    if (script_path == NULL && conduct_path == NULL &&
        conduct_tty == NULL) {
        /* Nothing to rescan: the bar lists this shell and what it
         * hosts, and neither arrives by looking at /proc. */
    }
    tiku_trk_open_with_window_init(&app.open_with_window, app.desk,
        DESK_W, DESK_H, &app.dirty, open_with_window_launch, &app,
        alert_model(&app), app.store);
    tiku_trk_select_window_init(&app.select_window, app.desk,
        DESK_W, DESK_H, &app.dirty, apply_select_model, &app);
    tiku_trk_clip_init(&app.clip);
    app.store = tiku_trk_session_open_store(app.backend);
    splash_stage(&app, "settings");
    tiku_trk_deskbar_set_store(app.deskbar, app.store);
    (void)tiku_trk_deskbar_load_replicants(app.deskbar);
    tiku_trk_session_controller_init(&app.session, app.store, app.deskbar,
        &app.desktop_view, &app.folder_lifecycle);
    tiku_trk_find_window_set_store(&app.find_window, app.store);
    (void)tiku_trk_poseview_install_query_defaults(app.backend, app.store);
    tiku_trk_session_restore_deskbar(&app.session);
    {
        /* Where the menu's own folder is, and the lists it shows above it.
         * Fed rather than read by the bar: the recent lists are the same
         * ones the Open panel keeps, and a second copy would disagree
         * (DB-002, DB-003). */
        const char *home = getenv("HOME");
        char user[TIKU_PATH_MAX];

        snprintf(user, sizeof user, "%s/.config/deskbar",
                 (home != NULL && home[0] != '\0') ? home : "");
        tiku_trk_deskbar_set_menu_dir(app.deskbar, user,
                                      "/usr/share/tiku-tracker/deskbar");
    }
    tiku_trk_deskbar_prefs_window_init(&app.deskbar_prefs_window, app.desk,
        app.deskbar, app.store, DESK_W, DESK_H,
        tiku_trk_deskbar_user_menu_dir(app.deskbar), &app.dirty,
        deskbar_edit_path, &app);
    /* The BACKEND's store, which is where a node's own properties live
     * and where the Desktop reads the picture behind it from; the
     * session store is a different book. */
    tiku_trk_desktop_prefs_window_init(&app.desktop_prefs_window, app.desk,
        &app.desktop_view,
        (app.backend != NULL && app.backend->ops->state_store != NULL)
            ? app.backend->ops->state_store(app.backend) : NULL,
        DESK_W, DESK_H, &app.dirty);
    tiku_trk_fonts_window_init(&app.fonts_window, app.desk, app.deskbar,
                               app.store, DESK_W, DESK_H, &app.dirty);
    {
        /* After the Deskbar's own settings have been applied, because a
         * face dropped in the fonts folder has to win over the baked
         * family they name -- and from startup, not from whenever
         * somebody next opens the window that chose it. */
        tiku_trk_fontprefs_t chosen;

        (void)tiku_trk_fontprefs_load(&chosen, app.store);
        tiku_trk_fontprefs_apply(&chosen);
    }
    splash_stage(&app, "fonts");
    {
        /* Which applications there are.  A file rather than a database:
         * what the rows are about is the ANSWERS -- which application
         * opens what, and how well -- not where the list is kept. */
        const char *home = getenv("HOME");
        char reg[TIKU_PATH_MAX];

        {
            char tdb[TIKU_PATH_MAX];

            snprintf(tdb, sizeof tdb, "%s/.config/tracker/types",
                     (home != NULL && home[0] != '\0') ? home : "");
            (void)tiku_trk_types_install(&app.types, tdb);
            tiku_trk_find_window_set_types(&app.find_window, &app.types);
            /* The art a type wears is the database's to declare, and the
             * icon cache is what asks (IV-011). */
            tiku_trk_nodeicon_set_types(&app.nicons, &app.types);
        }
        tiku_trk_launch_clear(&app.apps);
        {
            /* This Tracker is in the list, so choosing it is answerable:
             * it opens folders here rather than starting a second desktop
             * (AW-035). */
            int self = tiku_trk_launch_add(&app.apps, "TikuTracker", "",
                                           "inode/directory");
            if (self >= 0) {
                app.apps.app[self].is_tracker = 1;
            }
        }
        snprintf(reg, sizeof reg, "%s/.config/tracker/apps",
                 (home != NULL && home[0] != '\0') ? home : "");
        if (tiku_trk_launch_load(&app.apps, reg) < 0) {
            (void)tiku_trk_launch_load(&app.apps,
                                       "/usr/share/tiku-tracker/apps");
        }
    }
    splash_stage(&app, "applications");
    tiku_trk_launch_controller_init(&app.launcher, &app.apps, app.store,
        app.status, sizeof app.status, launch_open_path, &app);
    tiku_trk_folder_menu_init(&app.folder_menu, &app.menus, &app.clip,
        app.backend, app.fs, &app.types, &app.launcher, &app.bar_fl,
        &app.bar_open);
    tiku_menu_set_icon_painter(paint_menu_icon);
    tiku_trk_deskbar_set_menu_icon_painter(app.deskbar, deskbar_menu_icon,
                                           &app);
    tiku_trk_addons_init(&app.addons);
    (void)tiku_trk_addons_register(&app.addons, "Open in Tracker", "",
                                   "inode/directory", 'o');
    (void)tiku_trk_addons_register(&app.addons, "Open with Editor", "",
                                   "text/plain", 'e');
    (void)tiku_trk_addons_register(&app.addons, "Inspect", "", "*", 'i');
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(app.addons_dir, sizeof app.addons_dir,
                     "%s/.config/tracker/add-ons", home);
            snprintf(app.addons_keys, sizeof app.addons_keys,
                     "%s/.config/tracker/addon-shortcuts", home);
        }
        sync_addons(&app, 1);
    }
    tiku_thumb_init(&app.thumbs);
    tiku_trk_nodeicon_init(&app.nicons);
    splash_stage(&app, "icons");
    tiku_volwatch_init(&app.volwatch);
    app.info_relink = -1;
    tiku_trk_info_windows_init(&app.info_windows, app.desk, app.backend,
        app.store, app.fs, alert_model(&app), &app.dirty);
    tiku_trk_panel_window_init(&app.file_panel, app.desk, app.backend,
        app.store, &app.nicons, app.row_h, DESK_W, DESK_H, &app.dirty,
        fpanel_commit, &app);
    tiku_trk_panel_window_set_alert(&app.file_panel, alert_model(&app));
    app.find_window.save_as = find_save_as;
    app.find_window.save_as_context = &app;
    app.find_window.select_folder = find_select_folder;
    {
        const char *home = getenv("HOME");
        char path[TIKU_PATH_MAX] = "";

        if (home != NULL && home[0] != '\0') {
            snprintf(path, sizeof path, "%s/.config/TrackerSettings", home);
        }
        tiku_trk_prefs_window_init(&app.prefs_window, app.desk,
            DESK_W, DESK_H, path, &app.dirty,
            &alert_model(&app)->open);
    }
    tiku_trk_folder_file_actions_init(&app.file_actions, app.fs,
        app.backend, &app.clip,
        &app.drop_controller.pending, app.drop_controller.destination,
        alert_model(&app), open_preferences, &app, app.status,
        sizeof app.status, file_actions_close_under, &app);
    tiku_trk_folder_edit_init(&app.folder_edit, app.backend, app.fs,
        app.status, sizeof app.status, &app.dirty);
    tiku_trk_folder_menu_set_editor(&app.folder_menu, &app.folder_edit);
    tiku_trk_band_controller_init(&app.band_controller, app.row_h,
                                  &app.dirty);
    tiku_trk_switcher_window_init(&app.switcher_window, app.desk,
        app.deskbar, DESK_W, DESK_H, &app.dirty, app.status,
        sizeof app.status);
    app.folder_render = (tiku_trk_folder_render_t){
        .workspace = app.desk,
        .menus = &app.menus,
        .prefs = prefs_model(&app),
        .clip = &app.clip,
        .backend = app.backend,
        .icons = &app.nicons,
        .store = app.store,
        .thumbs = &app.thumbs,
        .row_h = app.row_h,
        .screen_w = DESK_W,
        .screen_h = DESK_H,
        .status = app.status,
        .count_text = app.count_text,
        .count_text_size = sizeof app.count_text,
        .finding = &app.finding,
        .find_text = app.find_buf,
        .editing = &app.folder_edit.editing,
        .edit_path = app.folder_edit.edit_path,
        .value_edit = &app.folder_edit.value_edit,
        .renaming = &app.folder_edit.renaming,
        .rename_folder = &app.folder_edit.rename_folder,
        .rename_row = &app.folder_edit.rename_row,
        .rename_edit = &app.folder_edit.rename_edit,
        .band_folder = &app.band_controller.folder,
        .band_x0 = &app.band_controller.x0,
        .band_y0 = &app.band_controller.y0,
        .band_x1 = &app.band_controller.x1,
        .band_y1 = &app.band_controller.y1,
        .drop_folder = &app.drag_target.folder,
        .drop_row = &app.drag_target.row,
        .prepare_menu = tiku_trk_folder_menu_prepare,
        .strip = &app.tstrip,
        .strip_owner = NULL,
        .prepare_context = &app.folder_menu
    };
    tiku_trk_folder_lifecycle_init(&app.folder_lifecycle, app.folders,
        TIKU_WINDOW_MAX, &app.folder_count, &app.cascade_x,
        &app.cascade_y, app.desk, app.backend, app.store,
        prefs_model(&app), &app.folder_render, app.status,
        sizeof app.status, &app.dirty, DESK_W, DESK_H);

    app.desk_row = -1;
    (void)tiku_trk_desktop_view_init(&app.desktop_view, app.backend, app.fs,
        prefs_model(&app), app.deskbar, &app.folder_render, DESK_W, DESK_H);
    tiku_trk_drag_target_init(&app.drag_target, app.desk, app.deskbar,
        &app.desktop_view, &app.drag, app.folders, &app.folder_count,
        app.row_h, &app.dirty, spring_open_nav, &app);
    tiku_trk_script_adapter_init(&app.script_adapter, app.desk, app.surf,
        &app.folder_lifecycle, &app.desktop_view, app.deskbar,
        alert_model(&app), &app.find_window, &app.file_panel,
        &app.folder_edit, &app.popup, &app.popup_sub, &app.popup_frame,
        &app.popup_sub_frame, app.status, app.count_text, app.row_h);
    /* After the init, which writes the whole adapter. */
    app.script_adapter.redraw_diff = script_redraw_diff;
    app.script_adapter.redraw_ctx = &app;
    app.script_adapter.say = conduct_text;
    app.script_adapter.say_ctx = &app;
    tiku_trk_drop_controller_init(&app.drop_controller, app.deskbar,
        app.backend, app.fs, &app.desktop_view, &app.menus,
        alert_model(&app), &app.drag, &app.drag_target.folder,
        &app.drag_target.row,
        &app.popup, &app.popup_sub, &app.popup_frame, app.prog_dest,
        sizeof app.prog_dest, app.status, sizeof app.status, &app.dirty,
        DESK_W, DESK_H, set_backdrop_color, &app);
    tiku_trk_drop_controller_set_launcher(&app.drop_controller,
        (const struct tiku_trk_apps *)&app.apps, drop_launch, &app);

    if (use_device) {
        /* The tray is the device: three values that say at a glance whether
         * the board is alive and what it is doing. */
        (void)tiku_trk_deskbar_add_replicant(app.deskbar, "uptime",
                                             "/sys/uptime");
        (void)tiku_trk_deskbar_add_replicant(app.deskbar, "boots",
                                             "/sys/boot_count");
        (void)tiku_trk_deskbar_add_replicant(app.deskbar, "free",
                                             "/sys/mem/nvmfree");
        /* The panel offers the namespace's own top level as its scopes,
         * read from the device rather than assumed. */
        {
            tiku_model_t roots[TIKU_TRK_FIND_PLACES_MAX];
            int nr = 0, k;

            nr = list_children(app.backend, "/", roots,
                               TIKU_TRK_FIND_PLACES_MAX);
            for (k = 0; k < nr; k++) {
                if (tiku_model_is_container(&roots[k])) {
                    (void)tiku_trk_find_add_place_identity(
                        find_model(&app), roots[k].name, roots[k].facts.dev,
                        app.backend->devid[0] != '\0' ? app.backend->devid
                                                     : roots[k].name);
                }
            }
        }
    }
    tiku_trk_session_restore_folders(&app.session, openme, nopen,
                                     safe_mode, use_device);

    (void)tiku_trk_find_open_default(find_model(&app), (int64_t)time(NULL));

    if (conduct_tty != NULL) {
        int line = tiku_conduct_open_tty(conduct_tty, 0);

        if (line < 0 ||
            tiku_conduct_adopt(&app.conduct, line, &app,
                                    conduct_inject, conduct_pixel,
                                    conduct_text) != 0) {
            fprintf(stderr, "tracker: cannot speak the conductor channel "
                            "over %s\n", conduct_tty);
            return 3;
        }
        app.conduct_on = 1;
    }
    if (conduct_path != NULL) {
        if (tiku_conduct_listen(&app.conduct, conduct_path, &app,
                                     conduct_inject, conduct_pixel,
                                     conduct_text) != 0) {
            fprintf(stderr, "tracker: cannot offer the conductor channel "
                            "on %s\n", conduct_path);
            return 3;
        }
        app.conduct_on = 1;
    }
    if (app.conduct_on &&
        (conduct_token != NULL || conduct_query_token != NULL)) {
        tiku_conduct_require(&app.conduct, conduct_token,
                                  conduct_query_token);
    }
    if (app.conduct_on) {
        /* The same fixture clock a scripted run froze: a driver comparing
         * pixels across a minute boundary would otherwise be told the
         * clock changed, which is true and useless. */
        {
            struct tm fixed;

            memset(&fixed, 0, sizeof fixed);
            fixed.tm_year = 2026 - 1900;
            fixed.tm_mon = 7;
            fixed.tm_mday = 7;
            fixed.tm_hour = 16;
            fixed.tm_min = 26;
            fixed.tm_isdst = -1;
            tiku_trk_deskbar_set_clock(app.deskbar, mktime(&fixed));
        }
    }
    if (script_path != NULL) {
        /* Displayless.  The shell draws into the same surface either way,
         * so a scripted run exercises the real draw path -- it just has
         * nobody to show it to (see tiku_trk_script.h). */
        sc = tiku_trk_script_open(script_path);
        /* A scripted wait is a number of turns: count the autoscroll hold
         * in pulses so the same script passes under any load (PVS-050). */
        tiku_trk_band_controller_set_script_delay(&app.band_controller, 30);
        tiku_trk_drag_target_set_script_delay(&app.drag_target, 30);
        {
            /* The fixture's clock: 7 Aug 16:26, the sample the clock row
             * itself quotes.  Live, a minute rolling over mid-comparison
             * cost the redraw checks a fistful of clock pixels. */
            struct tm fixed;

            memset(&fixed, 0, sizeof fixed);
            fixed.tm_year = 2026 - 1900;
            fixed.tm_mon = 7;
            fixed.tm_mday = 7;
            fixed.tm_hour = 16;
            fixed.tm_min = 26;
            fixed.tm_isdst = -1;
            tiku_trk_deskbar_set_clock(app.deskbar, mktime(&fixed));
        }
        if (sc == NULL) {
            return 2;
        }
    } else if (app.conduct_on) {
        /* Driven and displayless: the surface IS the output, and the
         * driver reads it through the channel. */
        app.host = NULL;
    }
    /* Otherwise the window is already up: it was opened before any of
     * this was built, so the splash had somewhere to be. */
    if (shot != NULL) {
        shot_at = tiku_trk_now() + 2000000;
    }
    if (shot_select) {
        /* Scripted: the Select window over the front folder, with an
         * expression already in it, so a capture shows the whole control
         * set the way it comes up. */
        tiku_window_t *fw = tiku_workspace_focused(app.desk);
        folder_t *ffl = folder_of(&app, fw);

        tiku_trk_select_window_open(&app.select_window, ffl, 520, 300);
        {
            const char *e = "gpio*";

            while (*e != '\0') {
                (void)tiku_field_key(&select_model(&app)->expr,
                                        TIKU_FIELD_CHAR, 0, *e++);
            }
        }
    }
    if (shot_live) {
        /* Search for what is currently on under /dev, change one of them,
         * and let the window's OWN refresh path apply the difference. */
        char err[128];

        open_panel(&app);
        tiku_trk_find_set_mode(find_model(&app), TIKU_QMODE_ATTRIBUTE);
        find_model(&app)->row[0].field = TIKU_QF_VALUE;
        find_model(&app)->row[0].op = TIKU_OP_IS;
        snprintf(find_model(&app)->row[0].value, sizeof find_model(&app)->row[0].value, "1");
        find_model(&app)->everywhere = 0;
        {
            int k;

            for (k = 0; k < find_model(&app)->place_count; k++) {
                if (strcmp(find_model(&app)->place[k], "dev") == 0) {
                    find_model(&app)->place_mask = 1u << k;
                }
            }
        }
        (void)panel_search(&app, find_model(&app));
        (void)app.backend->ops->write(app.backend, "/dev/led2", "1", 1, err,
                                      sizeof err);
        {
            int k;

            for (k = 0; k < app.folder_count; k++) {
                (void)refresh_results(&app, &app.folders[k]);
            }
        }
        tiku_trk_find_window_close(&app.find_window);
    }
    if (shot_panel) {
        /* Drive the panel the way a user would: open it, choose attribute
         * mode, fill two rows joined by or, scope it, and search. */
        open_panel(&app);
        tiku_trk_find_set_mode(find_model(&app), TIKU_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(find_model(&app));
        find_model(&app)->row[0].field = TIKU_QF_UNIT;
        find_model(&app)->row[0].op = TIKU_OP_IS;
        snprintf(find_model(&app)->row[0].value, sizeof find_model(&app)->row[0].value,
                 "bytes");
        find_model(&app)->row[0].join = TIKU_JOIN_OR;
        find_model(&app)->row[1].field = TIKU_QF_UNIT;
        find_model(&app)->row[1].op = TIKU_OP_IS;
        snprintf(find_model(&app)->row[1].value, sizeof find_model(&app)->row[1].value, "s");
        find_model(&app)->everywhere = 0;
        find_model(&app)->place_mask = 1u;
        (void)panel_search(&app, find_model(&app));
        open_panel(&app);
    }
    if (shot_find != NULL) {
        snprintf(app.find_buf, sizeof app.find_buf, "%.60s", shot_find);
        app.find_len = (int)strlen(app.find_buf);
        run_find(&app, &app.folders[app.folder_count - 1]);
    }
    if (shot_edit != NULL) {
        /* Select the node, start an edit, type the value and commit -- the
         * same path a user drives, so a screenshot is evidence. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        int i2, n2 = tiku_trk_poseview_count(fl->view);
        const char *eq = strchr(shot_edit, '=');
        char want[32];

        snprintf(want, sizeof want, "%.*s",
                 (int)(eq ? (size_t)(eq - shot_edit) : strlen(shot_edit)),
                 shot_edit);
        for (i2 = 0; i2 < n2; i2++) {
            tiku_trk_pose_t *p2 = tiku_trk_poseview_at(fl->view, i2);

            if (p2 != NULL && strcmp(p2->model.name, want) == 0) {
                tiku_trk_poseview_click(fl->view, i2, 0u);
                begin_edit(&app, fl);
                if (eq != NULL && app.folder_edit.editing) {
                    tiku_field_start(&app.folder_edit.value_edit, eq + 1);
                    commit_edit(&app, fl);
                }
                break;
            }
        }
    }
    if (shot_notify) {
        /* Change a value under the view and let the POLL derive the notice,
         * so what the screen shows came through the handler. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        char err[128];
        unsigned nf;

        (void)app.backend->ops->write(app.backend, "/dev/led0", "1", 1, err,
                                      sizeof err);
        {
            tiku_model_change_t ch[32];
            int nch = -1, complete = 1;

            if (app.backend->ops->changes != NULL) {
                nch = app.backend->ops->changes(app.backend, ch, 32,
                                                &complete);
            }
            nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(), ch, nch,
                                        complete);
        }
        (void)tiku_trk_poseview_refresh_values(fl->view, 0, 64);
        snprintf(app.status, sizeof app.status,
                 "poll: flags=%u, %d parked, %d rows", nf,
                 tiku_trk_poseview_parked(fl->view),
                 tiku_trk_poseview_count(fl->view));
    }
    if (shot_cascade) {
        static const char *dirs[] = { "/sys", "/sys/mem", "/sys/cpu",
                                      "/sys/timer", "/sys/clock",
                                      "/dev/gpio", "/dev/uart", "/dev/i2c",
                                      "/sys/watchdog", "/sys/power" };
        size_t k;

        for (k = 0; k < sizeof dirs / sizeof *dirs; k++) {
            (void)open_folder(&app, dirs[k]);
        }
    }
    if (shot_zoom) {
        folder_t *fl = &app.folders[app.folder_count - 1];
        const tiku_trk_column_t *cols;
        int nc = tiku_trk_poseview_columns(fl->view, &cols);
        int wide = 0, k;

        for (k = 0; k < nc; k++) { wide += cols[k].width; }
        (void)tiku_workspace_zoom(app.desk, fl->win, wide + 8,
            tiku_trk_poseview_count(fl->view) * app.row_h + COUNT_H + 6);
    }
    if (shot_shift) {
        /* Open a pose context menu, then apply the modifier the way the key
         * handler does, so the shot shows the shifted faces. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 7, 0u);
        p = tiku_trk_poseview_at(fl->view, 7);
        c = menu_ctx(&app, fl, 0u);
        build_open_with(&app, fl);
        build_per_type(&app, fl);
        app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app.popup_frame = tiku_menu_place(app.popup, 250, 150,
                              (tiku_rect_t){ 0, 0, app.surf->w,
                                                  app.surf->h });
        c = menu_ctx(&app, fl, TIKU_TRK_MOD_SHIFT);
        tiku_trk_sc_update(app.popup, &c);
    }
    if (shot_menu) {
        /* Drive the real path -- select a pose, ask which context menu
         * belongs to it, place it -- so a screenshot shows what a click
         * would, not a mock-up. */
        folder_t *fl = &app.folders[app.folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 1, 0u);
        p = tiku_trk_poseview_at(fl->view, 1);
        c = menu_ctx(&app, fl, 0u);
        build_open_with(&app, fl);
        build_per_type(&app, fl);
        app.popup = tiku_trk_cwmenu_context(&app.menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app.popup_frame = tiku_menu_place(app.popup, 250, 160,
                              (tiku_rect_t){ 0, 0, app.surf->w,
                                                  app.surf->h });
    }
    app.running = 1;
    app.applied_hide_dotfiles = -1;
    app.applied_full_path = -1;
    app.applied_folders_first = -1;
    app.dirty = 1;
    /*
     * Everything is built.  The last line names the thing the rest of it
     * was for, and then the screen is told to let go: from here the
     * desktop is drawn underneath every frame and the black comes off
     * it, so nothing below is waiting on anything.
     */
    splash_stage(&app, "the desktop");
    tiku_trk_splash_done(&app.splash, tiku_trk_now());

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
            if (sc != NULL) {
                tiku_trk_script_adapter_result_t script_result =
                    tiku_trk_script_adapter_next(&app.script_adapter, sc,
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
            if (sc != NULL) {
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
        if (tiku_trk_now() - last_sweep > 60000000) {
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
                                           (tiku_trk_now() - started_at) /
                                               1000000, open, nopen);
            }
            last_sweep = tiku_trk_now();
        }
        if (tiku_trk_now() - last_pump > 400000) {
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
            if (tiku_trk_now() - last_requery > 3000000) {
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
                last_requery = tiku_trk_now();
            }
            last_pump = tiku_trk_now();
            if (changed > 0) {
                app.dirty = 1;
            }
        }
        if (shot != NULL && shot_at > 0 && tiku_trk_now() >= shot_at) {
            app.dirty = 1;
        }
        if (app.dirty) {
            draw_all(&app);
            blit(&app);
            if (shot != NULL && shot_at > 0 && tiku_trk_now() >= shot_at) {
                (void)tiku_surface_png(app.surf, shot);
                printf("wrote %s\n", shot);
                fflush(stdout);
                shot_at = 0;
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
        if (deadline > 0 && tiku_trk_now() > deadline) {
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
            int64_t nap = (sc != NULL) ? 20000 : 10000;
            int64_t due = (app.loop != NULL)
                              ? tiku_trk_loop_next_due(app.loop) : -1;

            if (due >= 0 && due < nap) {
                nap = (due < 1000) ? 1000 : due;
            }
            usleep((useconds_t)nap);
        }
    }

    /*
     * The way out, named as it happens.  Each line goes up before the
     * work it names, the way the boot's do, so the dwell a stage is held
     * for is the time that piece actually took plus whatever it takes to
     * reach the minimum -- and the order is the order the pieces really
     * come apart, which is the boot read backwards.
     */
    splash_stage(&app, "the session");
    tiku_trk_session_controller_save(&app.session);
    splash_stage(&app, "the desktop");
    for (i = 0; i < app.folder_count; i++) {
        tiku_trk_poseview_free(app.folders[i].view);
    }
    tiku_trk_desktop_view_destroy(&app.desktop_view);
    if (tiku_trk_switcher_window_shown(&app.switcher_window)) {
        tiku_trk_switcher_window_close(&app.switcher_window, 0);
    }
    tiku_trk_panel_window_destroy(&app.file_panel);
    tiku_trk_deskbar_prefs_window_destroy(&app.deskbar_prefs_window);
    tiku_trk_desktop_prefs_window_destroy(&app.desktop_prefs_window);
    tiku_trk_fonts_window_destroy(&app.fonts_window);
    tiku_trk_prefs_window_destroy(&app.prefs_window);
    splash_stage(&app, "the Deskbar");
    tiku_trk_deskbar_free(app.deskbar);
    splash_stage(&app, "applications");
    tiku_remote_shutdown(&app.remote);
    tiku_workspace_free(app.desk);
    splash_stage(&app, "storage");
    /* Quitting waits for work already under way, up to five seconds
     * (AW-009): the alternative is a copy abandoned mid-file with
     * nothing to say so.  A stage that waits on a real copy is the one
     * place here where the line is reporting the wait rather than
     * covering it, which is what it is for. */
    tiku_trk_session_drain_filesystem(app.fs);
    if (sc != NULL) {
        rc = tiku_trk_script_report(sc, "desktop");
        tiku_trk_script_free(sc);
    }
    tiku_fs_free(app.fs);
    tiku_backend_close(app.backend);
    if (app.conduct_on) {
        tiku_conduct_shutdown(&app.conduct);
    }
    /* The last line has nothing after it to pay its dwell, so it is paid
     * here: without this the screen's final word is on the glass for
     * however long the last free took, which is nothing. */
    splash_settle(&app);
    tiku_host_close(app.host);
    tiku_surface_free(app.surf);
    return rc;
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker",
    .name = "TikuTracker",
    .run = tracker_run
};
