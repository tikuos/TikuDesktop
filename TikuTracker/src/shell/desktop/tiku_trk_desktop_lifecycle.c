/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_lifecycle.c - the way up and the way down: the boot
 * screen's stages, the closing screen, and the answers to the questions the
 * status overlay asks
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief Hold the line now up for whatever is still owed it.
 *
 * The dwell is paid at the START of the next stage rather than at the end
 * of this one, so the work a stage names counts toward its own dwell: a
 * piece that takes 300ms is shown for 300ms and topped up by nothing,
 * and one that takes 2ms is topped up to the minimum.  What is being
 * paced is the reading, not the loading.  The last line of all has no
 * next stage to pay it, so the caller settles that one itself.
 */
static void
splash_settle(desktop_t *app)
{
    int64_t owed;

    if (!app->splash.live) {
        return;
    }
    owed = tiku_trk_splash_owed(&app->splash, tiku_trk_now());
    if (owed > 0) {
        struct timespec ts;

        ts.tv_sec = (time_t)(owed / 1000000);
        ts.tv_nsec = (long)(owed % 1000000) * 1000L;
        (void)nanosleep(&ts, NULL);
    }
}

/** @brief Name the piece now being built, or now being put away. */
static void
splash_stage(desktop_t *app, const char *what)
{
    if (!app->splash.live) {
        return;
    }
    splash_settle(app);
    tiku_trk_splash_say(&app->splash, what, tiku_trk_now());
    tiku_trk_splash_draw(&app->splash, app->surf,
                         (tiku_rect_t){ 0, 0, DESK_W, DESK_H },
                         tiku_trk_now());
    if (app->host != NULL) {
        (void)tiku_host_present(app->host, app->surf);
    }
}

/**
 * @brief Take the desktop down behind the screen it came up behind.
 *
 * The shutdown a PERSON asked for, which is the one worth showing: the
 * black comes back over the desktop, and the pieces name themselves as
 * they are put away.  A run with no splash -- scripted, or driven over
 * the conductor channel -- stops where it always stopped.
 */
void
begin_shutdown(desktop_t *app)
{
    tiku_sound_play(TIKU_SOUND_SHUTDOWN);
    tiku_trk_splash_closing(&app->splash, tiku_trk_now());
    if (tiku_trk_splash_closed(&app->splash, tiku_trk_now())) {
        app->running = 0;
        return;
    }
    app->closing = 1;
    app->dirty = 1;
}

/** @brief Hand a modal filesystem answer back to the operation engine. */
static void
status_answer(void *context, const tiku_alert_t *alert)
{
    desktop_t *app = context;

    if (alert->tag == TIKU_TRK_ALERT_TAG_CONFLICT) {
        if (alert->chosen == alert->cancel) {
            tiku_fs_cancel(app->fs);
        } else {
            tiku_fs_resolve(app->fs,
                                tiku_trk_alert_conflict_answer(alert));
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_ERROR) {
        tiku_fs_resolve_error(app->fs,
                                  alert->chosen != alert->cancel);
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SHUTDOWN) {
        if (alert->chosen != alert->cancel) {
            begin_shutdown(app);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SAVE_REPLACE) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_panel_window_confirmed(&app->file_panel);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_OPEN_ANYWAY) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_open_with_window_confirmed(&app->open_with_window);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_BROKEN_OPEN) {
        if (alert->chosen != alert->cancel &&
            app->broken_open_path[0] != '\0') {
            {
                int islot = tiku_trk_info_windows_open(&app->info_windows,
                                                app->broken_open_path);

                if (islot >= 0) {
                    info_opens_with_fill(app,
                        &app->info_windows.model[islot]);
                }
            }
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_EMPTY_TRASH) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_folder_file_actions_answer_empty(&app->file_actions);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_DELETE) {
        /* Cancel, Move to Trash, Delete, in the order the buttons are
         * offered (FS-046). */
        folder_t *fl = folder_of(app, tiku_workspace_focused(app->desk));

        if (fl == NULL) {
            fl = desktop_folder(app);
        }
        {
            /* Cancel, Move to Trash, Delete, in the order the buttons
             * are offered (FS-046); the alert's own default already
             * steps left of Delete, so Return lands on Move to Trash. */
            static const tiku_trk_delete_answer_t by_button[] = {
                TIKU_TRK_DELETE_ANSWER_CANCEL,
                TIKU_TRK_DELETE_ANSWER_TRASH,
                TIKU_TRK_DELETE_ANSWER_DELETE,
            };
            int chosen = (alert->chosen >= 0 && alert->chosen < 3)
                             ? alert->chosen : 0;

            tiku_trk_folder_file_actions_answer_delete(&app->file_actions,
                fl, by_button[chosen]);
        }
    }
}

/** @brief Stop asks the operation engine to cancel at its safe boundary. */
static void
status_stop(void *context)
{
    desktop_t *app = context;

    tiku_fs_cancel(app->fs);
}

/**
 * @brief What the command line asked for, into @p o.
 *
 * @return -1 to carry on; otherwise the exit code the line called for,
 *         as -version and -reset-settings do their work here and stop.
 */
int
desktop_parse_args(int argc, char **argv, desktop_options_t *o)
{
    int i;

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
        if (strcmp(argv[i], "-d") == 0)                      { o->use_device = 1; }
        /*
         * Every board that answers, each under its own name in the
         * namespace -- as against -d, which puts ONE board at the root
         * and is the arrangement that made two devices mean two
         * desktops.  Both are kept: -d is what a device tool wants.
         */
        else if (strcmp(argv[i], "-devices") == 0) { o->want_devices = 1; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { o->port = argv[++i]; }
        /*
         * Where the one board -p names goes in the namespace, instead of
         * under its own name: a script that expects /devices/board1 can
         * be run against whatever is on the cable.
         */
        else if (strcmp(argv[i], "-mount") == 0 && i + 1 < argc) { o->mount_at = argv[++i]; o->want_devices = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { o->baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { o->shot = argv[++i]; }
        else if (strcmp(argv[i], "-shotmenu") == 0) { o->shot_menu = 1; }
        else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc) { o->script_path = argv[++i]; }
        else if (strcmp(argv[i], "-mute") == 0) { o->mute = 1; }
        else if (strcmp(argv[i], "-conduct") == 0 && i + 1 < argc) { o->conduct_path = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-tty") == 0 && i + 1 < argc) { o->conduct_tty = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-token") == 0 && i + 1 < argc) { o->conduct_token = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-query-token") == 0 && i + 1 < argc) { o->conduct_query_token = argv[++i]; }
        else if (strcmp(argv[i], "-shotedit") == 0 && i + 1 < argc) {
            o->shot_edit = argv[++i];
        }
        else if (strcmp(argv[i], "-shotfind") == 0 && i + 1 < argc) {
            o->shot_find = argv[++i];
        }
        else if (strcmp(argv[i], "-shotpanel") == 0) { o->shot_panel = 1; }
        else if (strcmp(argv[i], "-shotlive") == 0) { o->shot_live = 1; }
        else if (strcmp(argv[i], "-safe") == 0) { o->safe_mode = 1; }
        else if (strcmp(argv[i], "-shotshift") == 0) { o->shot_shift = 1; }
        else if (strcmp(argv[i], "-shotzoom") == 0) { o->shot_zoom = 1; }
        else if (strcmp(argv[i], "-shotcascade") == 0) { o->shot_cascade = 1; }
        else if (strcmp(argv[i], "-shotnotify") == 0) { o->shot_notify = 1; }
        else if (strcmp(argv[i], "-shotselect") == 0) { o->shot_select = 1; }
        else if (argv[i][0] != '-' && o->nopen < (int)(sizeof o->openme /
                                                    sizeof o->openme[0])) {
            /* AW-100: EVERY path argument opens its own window.  Keeping
             * only the last is how a shell glob silently opens one folder
             * out of the several the user asked for. */
            o->openme[o->nopen++] = argv[i];
        }
        else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) {
            o->deadline = tiku_trk_now() + (int64_t)atoi(argv[++i]) * 1000000;
        }
    }
    /* A port named without -devices is the board AT the root, the
     * device-tool arrangement; with -devices it is one entry under
     * /devices and the root stays local, so the line is opened once. */
    if (o->port != NULL && !o->want_devices) {
        o->use_device = 1;
    }
    return -1;
}

/**
 * @brief The namespace the shell holds and the window it draws in: one backend
 * at the root, the devices beside it, the surface, and the host.
 */
static int
boot_namespace(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)lp;
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
        tiku_backend_t *root = o->use_device
                                   ? tiku_backend_tiku_open(o->port, o->baud)
                                   : tiku_backend_posix_open();

        if (root == NULL) {
            fprintf(stderr, "tracker: no backend\n");
            return 1;
        }
        app->backend = tiku_mount_open();
        if (app->backend == NULL ||
            tiku_mount_add(app->backend, "/", root, 1) != 0) {
            fprintf(stderr, "tracker: no namespace\n");
            return 1;
        }
    }
    if (o->want_devices) {
        (void)mount_devices(app, o->port, o->baud, o->mount_at);
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
                tiku_mount_add(app->backend, mirror, second, 1) != 0) {
                tiku_backend_close(second);
            }
        }
    }
    app->fs = tiku_fs_new(app->backend);
    (void)tiku_fs_prepare_trash(app->fs);
    app->backdrop_color = TIKU_C_BACKDROP;
    app->surf = tiku_surface_new(DESK_W, DESK_H, app->backdrop_color);
    /*
     * The window goes up BEFORE the work does: a window opened after
     * everything is built suits a shell that appears all at once and not
     * one that says what it is doing, since a boot screen that arrives
     * after the boot has nothing left to report.  The displayless runs
     * -- scripted, and driven over the
     * conductor channel -- still take no window at all.
     */
    if (o->script_path == NULL && o->conduct_path == NULL && o->conduct_tty == NULL) {
        app->host = tiku_host_open(DESK_W, DESK_H, "TikuOS Desktop");
        if (app->host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
        if (tiku_host_scale(app->host) > 1) {
            (void)tiku_surface_rescale(app->surf,
                tiku_host_scale(app->host), app->backdrop_color);
        }
    }
    /*
     * A scripted run gets no splash unless it asks: the dwell is a
     * second and a half, and 164 scripts would each pay it to watch a
     * screen they are not looking at.  The one script that IS looking
     * sets TIKU_SPLASH.
     */
    return -1;
}

/**
 * @brief The boot screen goes up, and the workspace and the Deskbar are made.
 */
static int
boot_deskbar(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)lp;
    /* The moments sound only for a person: a scripted or conducted run
     * counts them and keeps quiet. */
    tiku_sound_init((o->script_path != NULL || o->conduct_path != NULL ||
                     o->conduct_tty != NULL || o->mute)
                        ? NULL : tiku_sound_pulse_backend());
    (void)tiku_sound_load_scores();
    tiku_trk_splash_begin(&app->splash,
        app->host != NULL || getenv("TIKU_SPLASH") != NULL, tiku_trk_now());
    tiku_sound_play(TIKU_SOUND_STARTUP);
    splash_stage(app, "storage");
    app->row_h = tiku_font_plain()->height + 5;
    app->desk = tiku_workspace_new(app->surf);
    app->deskbar = tiku_trk_deskbar_new(app->backend);
    app->team = tiku_trk_deskbar_add_team(app->deskbar, "TikuTracker");
    splash_stage(app, "the Deskbar");
    /* The machine's own processes are not this desktop's business: what
     * belongs in the bar is what runs ON the device, which is this shell
     * and the applications it hosts. */
    tiku_trk_cwmenu_init(&app->menus);
    tiku_trk_status_overlay_init(&app->status_overlay, app->surf,
        DESK_W, DESK_H, &app->dirty, status_answer, status_stop, app);
    tiku_trk_find_window_init(&app->find_window, app->desk, DESK_W, DESK_H,
        &app->dirty, app->status, sizeof app->status, panel_search, app);
    prepare_places(app);
    offer_installed(app, o->argv0);
    /*
     * The stream's icon player, injected below the kit boundary: the
     * Interface kit carries icon commands without knowing what HVIF is,
     * and this is the one line that teaches this end to draw them.
     */
    tiku_dl_set_icon_painter(tiku_icon_stream_play);
    if (tiku_remote_listen(&app->remote) != 0) {
        /* No socket, no sessions: the desktop still stands alone. */
        app->remote.fd = -1;
    }
    if (getenv("TIKU_EMBED_DEMO") != NULL) {
        /* The both-ways proof: the SAME descriptor the client runner
         * takes out of process, hosted here linked-in. */
        (void)embed_start(app, &tiku_demo_app);
    }
    app->loop = tiku_trk_loop_new();
    /* Once a second, so the displayed minute can never go stale while
     * nothing else dirties the surface (DB-044). */
    (void)tiku_trk_run_periodic(app->loop, tick_clock, app, NULL, 1000000,
                                0);
    /* Fast enough that a fade of sixteen steps reads as continuous, and
     * cheap because a tick that changes nothing frames nothing.  75 ms:
     * the About's fade is 1.2 seconds, and every hosted application's
     * tick rides the same beat rather than each arriving with a timer
     * of its own. */
    (void)tiku_trk_run_periodic(app->loop, tick_embeds, app, NULL,
                                75000, 0);
    if (o->script_path == NULL && o->conduct_path == NULL &&
        o->conduct_tty == NULL) {
        /* Nothing to rescan: the bar lists this shell and what it
         * hosts, and neither arrives by looking at /proc. */
    }
    tiku_trk_open_with_window_init(&app->open_with_window, app->desk,
        DESK_W, DESK_H, &app->dirty, open_with_window_launch, app,
        alert_model(app), app->store);
    tiku_trk_select_window_init(&app->select_window, app->desk,
        DESK_W, DESK_H, &app->dirty, apply_select_model, app);
    tiku_trk_clip_init(&app->clip);
    app->store = tiku_trk_session_open_store(app->backend);
    return -1;
}

/** @brief Settings and the windows that edit them. */
static int
boot_settings(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "settings");
    tiku_trk_deskbar_set_store(app->deskbar, app->store);
    (void)tiku_trk_deskbar_load_replicants(app->deskbar);
    tiku_trk_session_controller_init(&app->session, app->store, app->deskbar,
        &app->desktop_view, &app->folder_lifecycle);
    tiku_trk_find_window_set_store(&app->find_window, app->store);
    (void)tiku_trk_poseview_install_query_defaults(app->backend, app->store);
    tiku_trk_session_restore_deskbar(&app->session);
    {
        /* Where the menu's own folder is, and the lists it shows above it.
         * Fed rather than read by the bar: the recent lists are the same
         * ones the Open panel keeps, and a second copy would disagree
         * (DB-002, DB-003). */
        const char *home = getenv("HOME");
        char user[TIKU_PATH_MAX];

        snprintf(user, sizeof user, "%s/.config/deskbar",
                 (home != NULL && home[0] != '\0') ? home : "");
        tiku_trk_deskbar_set_menu_dir(app->deskbar, user,
                                      "/usr/share/tiku-tracker/deskbar");
    }
    tiku_trk_deskbar_prefs_window_init(&app->deskbar_prefs_window, app->desk,
        app->deskbar, app->store, DESK_W, DESK_H,
        tiku_trk_deskbar_user_menu_dir(app->deskbar), &app->dirty,
        deskbar_edit_path, app);
    /* The BACKEND's store, which is where a node's own properties live
     * and where the Desktop reads the picture behind it from; the
     * session store is a different book. */
    tiku_trk_desktop_prefs_window_init(&app->desktop_prefs_window, app->desk,
        &app->desktop_view,
        (app->backend != NULL && app->backend->ops->state_store != NULL)
            ? app->backend->ops->state_store(app->backend) : NULL,
        DESK_W, DESK_H, &app->dirty);
    tiku_trk_fonts_window_init(&app->fonts_window, app->desk, app->deskbar,
                               app->store, DESK_W, DESK_H, &app->dirty);
    {
        /* After the Deskbar's own settings have been applied, because a
         * face dropped in the fonts folder has to win over the baked
         * family they name -- and from startup, not from whenever
         * somebody next opens the window that chose it. */
        tiku_trk_fontprefs_t chosen;

        (void)tiku_trk_fontprefs_load(&chosen, app->store);
        tiku_trk_fontprefs_apply(&chosen);
    }
    return -1;
}

/** @brief The faces, and the window that chooses them. */
static int
boot_fonts(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "fonts");
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
            (void)tiku_trk_types_install(&app->types, tdb);
            tiku_trk_find_window_set_types(&app->find_window, &app->types);
            /* The art a type wears is the database's to declare, and the
             * icon cache is what asks (IV-011). */
            tiku_trk_nodeicon_set_types(&app->nicons, &app->types);
        }
        tiku_trk_launch_clear(&app->apps);
        {
            /* This Tracker is in the list, so choosing it is answerable:
             * it opens folders here rather than starting a second desktop
             * (AW-035). */
            int self = tiku_trk_launch_add(&app->apps, "TikuTracker", "",
                                           "inode/directory");
            if (self >= 0) {
                app->apps.app[self].is_tracker = 1;
            }
        }
        snprintf(reg, sizeof reg, "%s/.config/tracker/apps",
                 (home != NULL && home[0] != '\0') ? home : "");
        if (tiku_trk_launch_load(&app->apps, reg) < 0) {
            (void)tiku_trk_launch_load(&app->apps,
                                       "/usr/share/tiku-tracker/apps");
        }
    }
    return -1;
}

/** @brief The applications the desktop hosts and offers. */
static int
boot_applications(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "applications");
    tiku_trk_launch_controller_init(&app->launcher, &app->apps, app->store,
        app->status, sizeof app->status, launch_open_path, app);
    tiku_trk_folder_menu_init(&app->folder_menu, &app->menus, &app->clip,
        app->backend, app->fs, &app->types, &app->launcher, &app->bar_fl,
        &app->bar_open);
    tiku_menu_set_icon_painter(paint_menu_icon);
    tiku_trk_deskbar_set_menu_icon_painter(app->deskbar, deskbar_menu_icon,
                                           app);
    tiku_trk_addons_init(&app->addons);
    (void)tiku_trk_addons_register(&app->addons, "Open in Tracker", "",
                                   "inode/directory", 'o');
    (void)tiku_trk_addons_register(&app->addons, "Open with Editor", "",
                                   "text/plain", 'e');
    (void)tiku_trk_addons_register(&app->addons, "Inspect", "", "*", 'i');
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(app->addons_dir, sizeof app->addons_dir,
                     "%s/.config/tracker/add-ons", home);
            snprintf(app->addons_keys, sizeof app->addons_keys,
                     "%s/.config/tracker/addon-shortcuts", home);
        }
        sync_addons(app, 1);
    }
    tiku_thumb_init(&app->thumbs);
    tiku_trk_nodeicon_init(&app->nicons);
    return -1;
}

/**
 * @brief The views, panels, controllers and adapters, and the art they draw
 * with.
 */
static int
boot_icons(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "icons");
    tiku_volwatch_init(&app->volwatch);
    app->info_relink = -1;
    tiku_trk_info_windows_init(&app->info_windows, app->desk, app->backend,
        app->store, app->fs, alert_model(app), &app->dirty);
    tiku_trk_panel_window_init(&app->file_panel, app->desk, app->backend,
        app->store, &app->nicons, app->row_h, DESK_W, DESK_H, &app->dirty,
        fpanel_commit, app);
    tiku_trk_panel_window_set_alert(&app->file_panel, alert_model(app));
    app->find_window.save_as = find_save_as;
    app->find_window.save_as_context = app;
    app->find_window.select_folder = find_select_folder;
    {
        const char *home = getenv("HOME");
        char path[TIKU_PATH_MAX] = "";

        if (home != NULL && home[0] != '\0') {
            snprintf(path, sizeof path, "%s/.config/TrackerSettings", home);
        }
        tiku_trk_prefs_window_init(&app->prefs_window, app->desk,
            DESK_W, DESK_H, path, &app->dirty,
            &alert_model(app)->open);
    }
    tiku_trk_folder_file_actions_init(&app->file_actions, app->fs,
        app->backend, &app->clip,
        &app->drop_controller.pending, app->drop_controller.destination,
        alert_model(app), open_preferences, app, app->status,
        sizeof app->status, file_actions_close_under, app);
    tiku_trk_folder_edit_init(&app->folder_edit, app->backend, app->fs,
        app->status, sizeof app->status, &app->dirty);
    tiku_trk_folder_menu_set_editor(&app->folder_menu, &app->folder_edit);
    tiku_trk_band_controller_init(&app->band_controller, app->row_h,
                                  &app->dirty);
    tiku_trk_switcher_window_init(&app->switcher_window, app->desk,
        app->deskbar, DESK_W, DESK_H, &app->dirty, app->status,
        sizeof app->status);
    app->folder_render = (tiku_trk_folder_render_t){
        .workspace = app->desk,
        .menus = &app->menus,
        .prefs = prefs_model(app),
        .clip = &app->clip,
        .backend = app->backend,
        .icons = &app->nicons,
        .store = app->store,
        .thumbs = &app->thumbs,
        .row_h = app->row_h,
        .screen_w = DESK_W,
        .screen_h = DESK_H,
        .status = app->status,
        .count_text = app->count_text,
        .count_text_size = sizeof app->count_text,
        .finding = &app->finding,
        .find_text = app->find_buf,
        .editing = &app->folder_edit.editing,
        .edit_path = app->folder_edit.edit_path,
        .value_edit = &app->folder_edit.value_edit,
        .renaming = &app->folder_edit.renaming,
        .rename_folder = &app->folder_edit.rename_folder,
        .rename_row = &app->folder_edit.rename_row,
        .rename_edit = &app->folder_edit.rename_edit,
        .band_folder = &app->band_controller.folder,
        .band_x0 = &app->band_controller.x0,
        .band_y0 = &app->band_controller.y0,
        .band_x1 = &app->band_controller.x1,
        .band_y1 = &app->band_controller.y1,
        .drop_folder = &app->drag_target.folder,
        .drop_row = &app->drag_target.row,
        .prepare_menu = tiku_trk_folder_menu_prepare,
        .strip = &app->tstrip,
        .strip_owner = NULL,
        .prepare_context = &app->folder_menu
    };
    tiku_trk_folder_lifecycle_init(&app->folder_lifecycle, app->folders,
        TIKU_WINDOW_MAX, &app->folder_count, &app->cascade_x,
        &app->cascade_y, app->desk, app->backend, app->store,
        prefs_model(app), &app->folder_render, app->status,
        sizeof app->status, &app->dirty, DESK_W, DESK_H);

    app->desk_row = -1;
    (void)tiku_trk_desktop_view_init(&app->desktop_view, app->backend, app->fs,
        prefs_model(app), app->deskbar, &app->folder_render, DESK_W, DESK_H);
    tiku_trk_drag_target_init(&app->drag_target, app->desk, app->deskbar,
        &app->desktop_view, &app->drag, app->folders, &app->folder_count,
        app->row_h, &app->dirty, spring_open_nav, app);
    tiku_trk_script_adapter_init(&app->script_adapter, app->desk, app->surf,
        &app->folder_lifecycle, &app->desktop_view, app->deskbar,
        alert_model(app), &app->find_window, &app->file_panel,
        &app->folder_edit, &app->popup, &app->popup_sub, &app->popup_frame,
        &app->popup_sub_frame, app->status, app->count_text, app->row_h);
    /* After the init, which writes the whole adapter. */
    app->script_adapter.redraw_diff = script_redraw_diff;
    app->script_adapter.redraw_ctx = app;
    app->script_adapter.say = conduct_text;
    app->script_adapter.say_ctx = app;
    tiku_trk_drop_controller_init(&app->drop_controller, app->deskbar,
        app->backend, app->fs, &app->desktop_view, &app->menus,
        alert_model(app), &app->drag, &app->drag_target.folder,
        &app->drag_target.row,
        &app->popup, &app->popup_sub, &app->popup_frame, app->prog_dest,
        sizeof app->prog_dest, app->status, sizeof app->status, &app->dirty,
        DESK_W, DESK_H, set_backdrop_color, app);
    tiku_trk_drop_controller_set_launcher(&app->drop_controller,
        (const struct tiku_trk_apps *)&app->apps, drop_launch, app);
    return -1;
}

/**
 * @brief A device mounted beside the files when asked, and the last session's
 * folders back where they were.
 */
static int
boot_session(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)lp;
    if (o->use_device) {
        /* The tray is the device: three values that say at a glance whether
         * the board is alive and what it is doing. */
        (void)tiku_trk_deskbar_add_replicant(app->deskbar, "uptime",
                                             "/sys/uptime");
        (void)tiku_trk_deskbar_add_replicant(app->deskbar, "boots",
                                             "/sys/boot_count");
        (void)tiku_trk_deskbar_add_replicant(app->deskbar, "free",
                                             "/sys/mem/nvmfree");
        /* The panel offers the namespace's own top level as its scopes,
         * read from the device rather than assumed. */
        {
            tiku_model_t roots[TIKU_TRK_FIND_PLACES_MAX];
            int nr = 0, k;

            nr = list_children(app->backend, "/", roots,
                               TIKU_TRK_FIND_PLACES_MAX);
            for (k = 0; k < nr; k++) {
                if (tiku_model_is_container(&roots[k])) {
                    (void)tiku_trk_find_add_place_identity(
                        find_model(app), roots[k].name, roots[k].facts.dev,
                        app->backend->devid[0] != '\0' ? app->backend->devid
                                                     : roots[k].name);
                }
            }
        }
    }
    tiku_trk_session_restore_folders(&app->session, o->openme, o->nopen,
                                     o->safe_mode, o->use_device);

    (void)tiku_trk_find_open_default(find_model(app), (int64_t)time(NULL));
    return -1;
}

/**
 * @brief The conductor channel and the script, for a shell driven from outside.
 */
static int
boot_drivers(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    if (o->conduct_tty != NULL) {
        int line = tiku_conduct_open_tty(o->conduct_tty, 0);

        if (line < 0 ||
            tiku_conduct_adopt(&app->conduct, line, app,
                                    conduct_inject, conduct_pixel,
                                    conduct_text) != 0) {
            fprintf(stderr, "tracker: cannot speak the conductor channel "
                            "over %s\n", o->conduct_tty);
            return 3;
        }
        app->conduct_on = 1;
    }
    if (o->conduct_path != NULL) {
        if (tiku_conduct_listen(&app->conduct, o->conduct_path, app,
                                     conduct_inject, conduct_pixel,
                                     conduct_text) != 0) {
            fprintf(stderr, "tracker: cannot offer the conductor channel "
                            "on %s\n", o->conduct_path);
            return 3;
        }
        app->conduct_on = 1;
    }
    if (app->conduct_on &&
        (o->conduct_token != NULL || o->conduct_query_token != NULL)) {
        tiku_conduct_require(&app->conduct, o->conduct_token,
                                  o->conduct_query_token);
    }
    if (app->conduct_on) {
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
            tiku_trk_deskbar_set_clock(app->deskbar, mktime(&fixed));
        }
    }
    if (o->script_path != NULL) {
        /* Displayless.  The shell draws into the same surface either way,
         * so a scripted run exercises the real draw path -- it just has
         * nobody to show it to (see tiku_trk_script.h). */
        lp->sc = tiku_trk_script_open(o->script_path);
        /* A scripted wait is a number of turns: count the autoscroll hold
         * in pulses so the same script passes under any load (PVS-050). */
        tiku_trk_band_controller_set_script_delay(&app->band_controller, 30);
        tiku_trk_drag_target_set_script_delay(&app->drag_target, 30);
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
            tiku_trk_deskbar_set_clock(app->deskbar, mktime(&fixed));
        }
        if (lp->sc == NULL) {
            return 2;
        }
    } else if (app->conduct_on) {
        /* Driven and displayless: the surface IS the output, and the
         * driver reads it through the channel. */
        app->host = NULL;
    }
    /* Otherwise the window is already up: it was opened before any of
     * this was built, so the splash had somewhere to be. */
    return -1;
}

/**
 * @brief The -shot flags: what a headless run puts on screen before it writes
 * its one frame.
 */
static int
boot_shots(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    if (o->shot != NULL) {
        lp->shot_at = tiku_trk_now() + 2000000;
    }
    if (o->shot_select) {
        /* Scripted: the Select window over the front folder, with an
         * expression already in it, so a capture shows the whole control
         * set the way it comes up. */
        tiku_window_t *fw = tiku_workspace_focused(app->desk);
        folder_t *ffl = folder_of(app, fw);

        tiku_trk_select_window_open(&app->select_window, ffl, 520, 300);
        {
            const char *e = "gpio*";

            while (*e != '\0') {
                (void)tiku_field_key(&select_model(app)->expr,
                                        TIKU_FIELD_CHAR, 0, *e++);
            }
        }
    }
    if (o->shot_live) {
        /* Search for what is currently on under /dev, change one of them,
         * and let the window's OWN refresh path apply the difference. */
        char err[128];

        open_panel(app);
        tiku_trk_find_set_mode(find_model(app), TIKU_QMODE_ATTRIBUTE);
        find_model(app)->row[0].field = TIKU_QF_VALUE;
        find_model(app)->row[0].op = TIKU_OP_IS;
        snprintf(find_model(app)->row[0].value, sizeof find_model(app)->row[0].value, "1");
        find_model(app)->everywhere = 0;
        {
            int k;

            for (k = 0; k < find_model(app)->place_count; k++) {
                if (strcmp(find_model(app)->place[k], "dev") == 0) {
                    find_model(app)->place_mask = 1u << k;
                }
            }
        }
        (void)panel_search(app, find_model(app));
        (void)app->backend->ops->write(app->backend, "/dev/led2", "1", 1, err,
                                      sizeof err);
        {
            int k;

            for (k = 0; k < app->folder_count; k++) {
                (void)refresh_results(app, &app->folders[k]);
            }
        }
        tiku_trk_find_window_close(&app->find_window);
    }
    if (o->shot_panel) {
        /* Drive the panel the way a user would: open it, choose attribute
         * mode, fill two rows joined by or, scope it, and search. */
        open_panel(app);
        tiku_trk_find_set_mode(find_model(app), TIKU_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(find_model(app));
        find_model(app)->row[0].field = TIKU_QF_UNIT;
        find_model(app)->row[0].op = TIKU_OP_IS;
        snprintf(find_model(app)->row[0].value, sizeof find_model(app)->row[0].value,
                 "bytes");
        find_model(app)->row[0].join = TIKU_JOIN_OR;
        find_model(app)->row[1].field = TIKU_QF_UNIT;
        find_model(app)->row[1].op = TIKU_OP_IS;
        snprintf(find_model(app)->row[1].value, sizeof find_model(app)->row[1].value, "s");
        find_model(app)->everywhere = 0;
        find_model(app)->place_mask = 1u;
        (void)panel_search(app, find_model(app));
        open_panel(app);
    }
    if (o->shot_find != NULL) {
        snprintf(app->find_buf, sizeof app->find_buf, "%.60s", o->shot_find);
        app->find_len = (int)strlen(app->find_buf);
        run_find(app, &app->folders[app->folder_count - 1]);
    }
    if (o->shot_edit != NULL) {
        /* Select the node, start an edit, type the value and commit -- the
         * same path a user drives, so a screenshot is evidence. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        int i2, n2 = tiku_trk_poseview_count(fl->view);
        const char *eq = strchr(o->shot_edit, '=');
        char want[32];

        snprintf(want, sizeof want, "%.*s",
                 (int)(eq ? (size_t)(eq - o->shot_edit) : strlen(o->shot_edit)),
                 o->shot_edit);
        for (i2 = 0; i2 < n2; i2++) {
            tiku_trk_pose_t *p2 = tiku_trk_poseview_at(fl->view, i2);

            if (p2 != NULL && strcmp(p2->model.name, want) == 0) {
                tiku_trk_poseview_click(fl->view, i2, 0u);
                begin_edit(app, fl);
                if (eq != NULL && app->folder_edit.editing) {
                    tiku_field_start(&app->folder_edit.value_edit, eq + 1);
                    commit_edit(app, fl);
                }
                break;
            }
        }
    }
    if (o->shot_notify) {
        /* Change a value under the view and let the POLL derive the notice,
         * so what the screen shows came through the handler. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        char err[128];
        unsigned nf;

        (void)app->backend->ops->write(app->backend, "/dev/led0", "1", 1, err,
                                      sizeof err);
        {
            tiku_model_change_t ch[32];
            int nch = -1, complete = 1;

            if (app->backend->ops->changes != NULL) {
                nch = app->backend->ops->changes(app->backend, ch, 32,
                                                &complete);
            }
            nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(), ch, nch,
                                        complete);
        }
        (void)tiku_trk_poseview_refresh_values(fl->view, 0, 64);
        snprintf(app->status, sizeof app->status,
                 "poll: flags=%u, %d parked, %d rows", nf,
                 tiku_trk_poseview_parked(fl->view),
                 tiku_trk_poseview_count(fl->view));
    }
    if (o->shot_cascade) {
        static const char *dirs[] = { "/sys", "/sys/mem", "/sys/cpu",
                                      "/sys/timer", "/sys/clock",
                                      "/dev/gpio", "/dev/uart", "/dev/i2c",
                                      "/sys/watchdog", "/sys/power" };
        size_t k;

        for (k = 0; k < sizeof dirs / sizeof *dirs; k++) {
            (void)open_folder(app, dirs[k]);
        }
    }
    if (o->shot_zoom) {
        folder_t *fl = &app->folders[app->folder_count - 1];
        const tiku_trk_column_t *cols;
        int nc = tiku_trk_poseview_columns(fl->view, &cols);
        int wide = 0, k;

        for (k = 0; k < nc; k++) { wide += cols[k].width; }
        (void)tiku_workspace_zoom(app->desk, fl->win, wide + 8,
            tiku_trk_poseview_count(fl->view) * app->row_h + COUNT_H + 6);
    }
    if (o->shot_shift) {
        /* Open a pose context menu, then apply the modifier the way the key
         * handler does, so the shot shows the shifted faces. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 7, 0u);
        p = tiku_trk_poseview_at(fl->view, 7);
        c = menu_ctx(app, fl, 0u);
        build_open_with(app, fl);
        build_per_type(app, fl);
        app->popup = tiku_trk_cwmenu_context(&app->menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app->popup_frame = tiku_menu_place(app->popup, 250, 150,
                              (tiku_rect_t){ 0, 0, app->surf->w,
                                                  app->surf->h });
        c = menu_ctx(app, fl, TIKU_TRK_MOD_SHIFT);
        tiku_trk_sc_update(app->popup, &c);
    }
    if (o->shot_menu) {
        /* Drive the real path -- select a pose, ask which context menu
         * belongs to it, place it -- so a screenshot shows what a click
         * would, not a mock-up. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 1, 0u);
        p = tiku_trk_poseview_at(fl->view, 1);
        c = menu_ctx(app, fl, 0u);
        build_open_with(app, fl);
        build_per_type(app, fl);
        app->popup = tiku_trk_cwmenu_context(&app->menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app->popup_frame = tiku_menu_place(app->popup, 250, 160,
                              (tiku_rect_t){ 0, 0, app->surf->w,
                                                  app->surf->h });
    }
    return -1;
}


/**
 * @brief Everything between the command line and the first frame.
 *
 * The namespace, the host window, the boot screen and its stages: the
 * workspace, the Deskbar, settings, fonts, applications, icons, and
 * the session, in that order.  @return -1 once the desktop is up;
 * otherwise the exit code of the piece that refused.
 */
int
desktop_boot(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    int code;

    code = boot_namespace(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_deskbar(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_settings(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_fonts(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_applications(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_icons(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_session(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_drivers(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_shots(app, o, lp);
    if (code >= 0) {
        return code;
    }

    app->running = 1;
    app->applied_hide_dotfiles = -1;
    app->applied_full_path = -1;
    app->applied_folders_first = -1;
    app->dirty = 1;
    /*
     * Everything is built.  The last line names the thing the rest of it
     * was for, and then the screen is told to let go: from here the
     * desktop is drawn underneath every frame and the black comes off
     * it, so nothing below is waiting on anything.
     */
    splash_stage(app, "the desktop");
    tiku_trk_splash_done(&app->splash, tiku_trk_now());
    return -1;
}

/**
 * @brief The way out, named as it happens.
 *
 * Each line goes up before the work it names, the way the boot's do,
 * and the order is the boot read backwards.  @return what the script,
 * if there was one, reports.
 */
int
desktop_teardown(desktop_t *app, desktop_loop_t *lp)
{
    int rc = 0;
    int i;

    tiku_sound_free();
    /*
     * The way out, named as it happens.  Each line goes up before the
     * work it names, the way the boot's do, so the dwell a stage is held
     * for is the time that piece actually took plus whatever it takes to
     * reach the minimum -- and the order is the order the pieces really
     * come apart, which is the boot read backwards.
     */
    splash_stage(app, "the session");
    tiku_trk_session_controller_save(&app->session);
    splash_stage(app, "the desktop");
    for (i = 0; i < app->folder_count; i++) {
        tiku_trk_poseview_free(app->folders[i].view);
    }
    tiku_trk_desktop_view_destroy(&app->desktop_view);
    if (tiku_trk_switcher_window_shown(&app->switcher_window)) {
        tiku_trk_switcher_window_close(&app->switcher_window, 0);
    }
    tiku_trk_panel_window_destroy(&app->file_panel);
    tiku_trk_deskbar_prefs_window_destroy(&app->deskbar_prefs_window);
    tiku_trk_desktop_prefs_window_destroy(&app->desktop_prefs_window);
    tiku_trk_fonts_window_destroy(&app->fonts_window);
    tiku_trk_prefs_window_destroy(&app->prefs_window);
    splash_stage(app, "the Deskbar");
    tiku_trk_deskbar_free(app->deskbar);
    splash_stage(app, "applications");
    tiku_remote_shutdown(&app->remote);
    tiku_workspace_free(app->desk);
    splash_stage(app, "storage");
    /* Quitting waits for work already under way, up to five seconds
     * (AW-009): the alternative is a copy abandoned mid-file with
     * nothing to say so.  A stage that waits on a real copy is the one
     * place here where the line is reporting the wait rather than
     * covering it, which is what it is for. */
    tiku_trk_session_drain_filesystem(app->fs);
    if (lp->sc != NULL) {
        rc = tiku_trk_script_report(lp->sc, "desktop");
        tiku_trk_script_free(lp->sc);
    }
    tiku_fs_free(app->fs);
    tiku_backend_close(app->backend);
    if (app->conduct_on) {
        tiku_conduct_shutdown(&app->conduct);
    }
    /* The last line has nothing after it to pay its dwell, so it is paid
     * here: without this the screen's final word is on the glass for
     * however long the last free took, which is nothing. */
    splash_settle(app);
    tiku_host_close(app->host);
    tiku_surface_free(app->surf);
    return rc;
}
