/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_deskbar.c - the Deskbar's picks: what each row of its
 * menus does, from opening a window to restarting the desktop
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_sys.h"
#include "tiku_trk_desktop_private.h"

/* Used before they are defined. */
int deskbar_pick_clock(desktop_t *app, tiku_trk_db_pick_t *pick);
int deskbar_pick_team(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y);
int deskbar_pick_system(desktop_t *app, tiku_trk_db_pick_t *pick);
/* End of the forward declarations. */

/* The argv the desktop was started with, for Restart system (DB-006). */
char **restart_argv;

/** @brief Preferences, as this shell shows them: a window of its own. */
void
open_preferences(void *context)
{
    desktop_t *app = context;

    tiku_trk_prefs_window_open(&app->prefs_window);
}

/** @brief Real art for the Deskbar's own menu rows (DB-002). */
int
deskbar_menu_icon(void *context, tiku_surface_t *s,
                  const tiku_trk_db_item_t *item, int x, int y, int size)
{
    const char *art = NULL;
    struct stat st;

    (void)context;
    switch (item->kind) {
    case TIKU_TRK_DB_ITEM_DIR:
        art = "folder";
        break;
    case TIKU_TRK_DB_ITEM_FIND:
        art = "query";
        break;
    case TIKU_TRK_DB_ITEM_RECENT:
        art = (item->which == 1) ? "folder"
            : (item->which == 2) ? "application" : "file";
        break;
    case TIKU_TRK_DB_ITEM_PATH:
        art = (stat(item->path, &st) == 0 && S_ISDIR(st.st_mode))
                  ? "folder" : "file";
        break;
    case TIKU_TRK_DB_ITEM_APPS:
        art = "folder_apps";
        break;
    case TIKU_TRK_DB_ITEM_SETTINGS:
        art = "folder_system";
        break;
    case TIKU_TRK_DB_ITEM_PREFS:
        art = "folder_config";
        break;
    case TIKU_TRK_DB_ITEM_TRACKER_PREFS:
        art = "folder_prefs";
        break;
    case TIKU_TRK_DB_ITEM_DESKTOP_PREFS:
        art = "desktop";
        break;
    case TIKU_TRK_DB_ITEM_FONT_PREFS:
        art = "folder_fonts";
        break;
    case TIKU_TRK_DB_ITEM_DEVICE_PREFS:
        art = "microcontroller";
        break;
    case TIKU_TRK_DB_ITEM_ASSOC_PREFS:
        art = "folder_prefs";
        break;
    case TIKU_TRK_DB_ITEM_FORCE_QUIT:
        art = "force_quit";
        break;
    case TIKU_TRK_DB_ITEM_SHOW_REPLICANTS:
        art = "package";
        break;
    case TIKU_TRK_DB_ITEM_ABOUT:
        /* The row that opens the box about the system wears the system's
         * own mark -- drawn from its geometry, so it is the same mark at
         * fourteen pixels here as at eighty in the box it opens. */
        tiku_logo_paint(s, (tiku_rect_t){ x, y, size, size }, 0u);
        return 1;
    case TIKU_TRK_DB_ITEM_APP:
        /* Whatever the shell named when it offered the row; a program
         * that said nothing is drawn as what it is. */
        art = (item->icon[0] != '\0') ? item->icon : "application";
        break;
    default:
        return 0;               /* a command row carries no art */
    }
    return tiku_icon_paint(s, art, x, y, size);
}

/** @brief Real art for menu items, dimmed for a disabled one (IV-054). */
int
paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    if (icon != NULL && strcmp(icon, MARK_ICON) == 0) {
        /* Not a name in the icon set: the mark is drawn.  Dimming is
         * moot -- the row it marks is never offered disabled. */
        tiku_logo_paint(s, (tiku_rect_t){ x, y, size, size }, 0u);
        return 1;
    }
    return dimmed
        ? tiku_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_C_PANEL)
        : tiku_icon_paint(s, icon, x, y, size);
}

/**
 * @brief Carry out what a Deskbar click asked for.
 *
 * Shared by the press path and the armed-dock release path: a CMD press
 * on the bar might be the start of a re-dock drag, so its CLICK meaning
 * is delivered only if the press never moved (DB-049 vs DB-019/021).
 */
void
deskbar_pick_dispatch(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y)
{
    if (deskbar_pick_system(app, pick)) {
        return;
    }
    if (deskbar_pick_team(app, pick, x, y)) {
        return;
    }
    (void)deskbar_pick_clock(app, pick);
}

/**
 * @brief A pick on the clock: shown, hidden, seconds, day, zone.  @return 1 for
 * a pick of this group
 */
int
deskbar_pick_clock(desktop_t *app, tiku_trk_db_pick_t *pick)
{
    switch (pick->what) {

    case TIKU_TRK_DB_CLOCK_HIDE:
    case TIKU_TRK_DB_CLOCK_SHOW:
        app->deskbar_prefs_window.model.value.show_clock =
            (pick->what == TIKU_TRK_DB_CLOCK_SHOW);
        tiku_trk_deskbar_settings_normalize(
            &app->deskbar_prefs_window.model.value);
        tiku_trk_deskbar_apply_settings(app->deskbar,
            &app->deskbar_prefs_window.model.value);
        (void)tiku_trk_deskbar_prefs_save(
            &app->deskbar_prefs_window.model, app->store);
        break;
    case TIKU_TRK_DB_CLOCK_SECONDS:
    case TIKU_TRK_DB_CLOCK_DAY:
    case TIKU_TRK_DB_CLOCK_ZONE:
        if (pick->what == TIKU_TRK_DB_CLOCK_SECONDS) {
            app->deskbar_prefs_window.model.value.show_seconds =
                !app->deskbar_prefs_window.model.value.show_seconds;
        } else if (pick->what == TIKU_TRK_DB_CLOCK_DAY) {
            app->deskbar_prefs_window.model.value.show_day_of_week =
                !app->deskbar_prefs_window.model.value.show_day_of_week;
        } else {
            app->deskbar_prefs_window.model.value.show_timezone =
                !app->deskbar_prefs_window.model.value.show_timezone;
        }
        tiku_trk_deskbar_settings_normalize(
            &app->deskbar_prefs_window.model.value);
        tiku_trk_deskbar_apply_settings(app->deskbar,
            &app->deskbar_prefs_window.model.value);
        (void)tiku_trk_deskbar_prefs_save(
            &app->deskbar_prefs_window.model, app->store);
        break;
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief A pick on a team or one of its windows: activate, minimise, close,
 * launch, quit, kill, hide and show all.  @return 1 for a pick of this group
 */
int
deskbar_pick_team(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y)
{
    tiku_window_t *t;
    
    switch (pick->what) {

    case TIKU_TRK_DB_ACTIVATE_WINDOW:
        /* Invoke is a no-op while dragging: the drop
         * owns the release, and a window that raised
         * itself under it would steal the target
         * (DB-030). */
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            /* The zoom animates only when the state
             * really changes (DB-030). */
            if (t->minimized) {
                app->zoom_t0 = tiku_trk_now();
                app->zoom_from = (tiku_rect_t){
                    x - 8, y - 8, 16, 16 };
                app->zoom_to = t->frame;
            }
            t->minimized = 0;
            tiku_workspace_activate(app->desk, t);
        }
        break;
    case TIKU_TRK_DB_MINIMIZE_WINDOW:
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            if (!t->minimized) {
                app->zoom_t0 = tiku_trk_now();
                app->zoom_from = t->frame;
                app->zoom_to = (tiku_rect_t){
                    x - 8, y - 8, 16, 16 };
            }
            t->minimized = 1;
            tiku_sound_play(TIKU_SOUND_WINDOW_MINIMIZE);
        }
        break;
    case TIKU_TRK_DB_CLOSE_WINDOW:
        t = tiku_workspace_find(app->desk, pick->tag);
        close_window(app, t);
        break;
    case TIKU_TRK_DB_ACTIVATE_TEAM: {
        /* Activating the application: its windows to
         * the front, the top one focused (DB-019). */
        int k, m = tiku_workspace_count(app->desk);

        for (k = 0; k < m; k++) {
            tiku_window_t *ww =
                tiku_workspace_at(app->desk, k);
            if (ww != NULL) {
                ww->minimized = 0;
            }
        }
        if (m > 0) {
            tiku_workspace_activate(app->desk,
                tiku_workspace_at(app->desk,
                                       m - 1));
        }
        break;
    }
    case TIKU_TRK_DB_LAUNCH_TEAM: {
        /* Middle click: another instance of the same
         * image (DB-021). */
        const char *cmd =
            tiku_trk_deskbar_team_command(app->deskbar,
                                          pick->team);

        if (cmd[0] != '\0') {
            pid_t child = fork();

            if (child == 0) {
                execlp(cmd, cmd, (char *)NULL);
                _exit(127);
            }
            if (child > 0) {
                (void)waitpid(child, NULL, WNOHANG);
            }
        }
        break;
    }
    case TIKU_TRK_DB_POWER_OFF:
        /* Chosen inside the submenu: no question asked -- the parent's
         * ellipsis is where the asking lives (DB-006).  It still goes
         * down the way it came up; what was skipped is the asking. */
        begin_shutdown(app);
        break;
    case TIKU_TRK_DB_RESTART: {
        /* A real restart of the desktop: the same image, started
         * over.  Where the image is takes a host to answer. */
        char image[TIKU_PATH_MAX];

        if (tiku_sys_self_path(image, sizeof image) == 0) {
            execv(image, restart_argv);
        }
        app->running = 0;       /* exec refused: end instead */
        break;
    }
    case TIKU_TRK_DB_QUIT_TEAM:
    case TIKU_TRK_DB_KILL_TEAM: {
        /* Roster names can group unrelated processes, including the shell.
         * Select an authenticated peer and confirm in the native dialog. */
        desktop_forcequit_open(app);
        break;
    }
    case TIKU_TRK_DB_HIDE_ALL:
    case TIKU_TRK_DB_SHOW_ALL: {
        int k, m = tiku_workspace_count(app->desk);
        for (k = 0; k < m; k++) {
            tiku_window_t *ww =
                tiku_workspace_at(app->desk, k);
            if (ww != NULL) {
                ww->minimized =
                    (pick->what == TIKU_TRK_DB_HIDE_ALL);
            }
        }
        break;
    }
    case TIKU_TRK_DB_CLOSE_ALL: {
        /* Closing every window of the file manager is a
         * request to close its WINDOWS, never to quit
         * it -- the original makes the same
         * distinction. */
        while (tiku_workspace_count(app->desk) > 0) {
            tiku_workspace_close(app->desk,
                tiku_workspace_at(app->desk, 0));
        }
        break;
    }
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief A pick on the leaf menu's own rows: Find, the preferences, an
 * application, the theme, About, quit and a path.  @return 1 for a pick of this
 * group
 */
int
deskbar_pick_system(desktop_t *app, tiku_trk_db_pick_t *pick)
{
    switch (pick->what) {

    case TIKU_TRK_DB_FIND:
        /* The leaf's Find item is the panel: the same
         * entry point as Ctrl+F, reachable by mouse. */
        open_panel(app);
        break;
    case TIKU_TRK_DB_PREFERENCES:
        tiku_trk_deskbar_prefs_window_open(
            &app->deskbar_prefs_window);
        break;
    case TIKU_TRK_DB_TRACKER_PREFERENCES:
        tiku_trk_prefs_window_open(&app->prefs_window);
        break;
    case TIKU_TRK_DB_FONT_PREFERENCES:
        tiku_trk_fonts_window_open(&app->fonts_window);
        break;
    case TIKU_TRK_DB_HELP:
        tiku_trk_help_window_open(&app->help_window);
        break;
    case TIKU_TRK_DB_FORCE_QUIT:
        desktop_forcequit_open(app);
        break;
    case TIKU_TRK_DB_SOUND_PREFERENCES:
        tiku_trk_sounds_window_open(&app->sounds_window);
        break;
    case TIKU_TRK_DB_DEVICE_PREFERENCES:
        desktop_deviceprefs_open(app);
        break;
    case TIKU_TRK_DB_STARTUP_PREFERENCES:
        desktop_startup_open(app);
        break;
    case TIKU_TRK_DB_ASSOC_PREFERENCES:
        desktop_associations_open(app);
        break;
    case TIKU_TRK_DB_DESKTOP_PREFERENCES:
        tiku_trk_desktop_prefs_window_open(&app->desktop_prefs_window);
        break;
    case TIKU_TRK_DB_LAUNCH_APP: {
        if (!strcmp(pick->path, "builtin:reader")) {
            tiku_trk_help_window_open(&app->reader_window);
            break;
        }
        if (!strcmp(pick->path, "builtin:viewer")) {
            tiku_trk_image_window_open(app->image_window);
            break;
        }
        if (!strcmp(pick->path, "builtin:monitor") ||
            !strcmp(pick->path, "builtin:vfs")) {
            desktop_utility_open(app, !strcmp(pick->path, "builtin:vfs"));
            break;
        }
        /* Started, not opened: these are programs, and they place their
         * own windows through the session the same way any application
         * does. */
        tiku_trk_app_t program;

        const char *args[1];
        size_t n = strlen(pick->path);
        int nargs = 0;

        const char *leaf = strrchr(pick->path, '/');

        memset(&program, 0, sizeof program);
        /* Its NAME is its leaf: the field is short, and a path long
         * enough leaves a fragment of some directory where the name
         * should be -- in every message that says which program. */
        snprintf(program.name, sizeof program.name, "%s",
                 (leaf != NULL) ? leaf + 1 : pick->path);
        if (n > 3u && strcmp(pick->path + n - 3, ".so") == 0) {
            /* A loadable application: the runner gives it the process
             * this desktop is not giving it. */
            snprintf(program.cmd, sizeof program.cmd, "%s", app->runner);
            args[nargs++] = pick->path;
        } else {
            snprintf(program.cmd, sizeof program.cmd, "%s", pick->path);
        }
        if (tiku_trk_launch_run(&program, (nargs > 0) ? args : NULL,
                                nargs) != TIKU_TRK_LAUNCH_OK) {
            snprintf(app->status, sizeof app->status,
                     "could not start %s", pick->path);
            app->dirty = 1;
        }
        break;
    }
    case TIKU_TRK_DB_TOGGLE_DARK: {
        /* The switch IS the DarkMode setting: flip it, write it down,
         * and let the one sync path repaint the world -- so the switch,
         * the checkbox and the settings file can never disagree. */
        tiku_trk_prefs_t *prefs = prefs_model(app);

        /* Three seats, so a press walks to the next one and round: the
         * switch is the only control most people will ever touch, and a
         * slider you cannot get back to the start of is a trap. */
        tiku_trk_prefs_set(prefs, "DarkMode",
                           (tiku_trk_prefs_get(prefs, "DarkMode") + 1) % 3);
        (void)tiku_trk_prefs_save(prefs, app->prefs_window.path);
        sync_folder_preferences(app);
        app->dirty = 1;
        break;
    }
    case TIKU_TRK_DB_SHOW_ABOUT:
        (void)embed_open_or_raise(app, &tiku_trk_about_app);
        break;
    case TIKU_TRK_DB_QUIT:
        /* The ellipsis is a promise: Shut down… asks
         * before anything is taken down (DB-006). */
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app),
            TIKU_ALERT_WARN,
            TIKU_TRK_ALERT_TAG_SHUTDOWN,
            "Shut down the desktop?",
            tiku_str("alert.btn.shut_down"));
        break;
    case TIKU_TRK_DB_OPEN_PATH:
        (void)open_folder(app, pick->path);
        break;
    case TIKU_TRK_DB_OPEN_PANEL:
        (void)open_panel_path(app, pick->path);
        break;
    default:
        return 0;
    }
    return 1;
}
