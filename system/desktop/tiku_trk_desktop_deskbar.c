/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_deskbar.c - the Deskbar's picks: what each row of its
 * menus does, from opening a window to shutting the desktop down
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_sys.h"
#include "tiku_trk_desktop_private.h"

/* Used before they are defined. */
int deskbar_pick_clock(desktop_t *app, tiku_trk_db_pick_t *pick);
static void team_activate(desktop_t *app, int team);
static void team_launch(desktop_t *app, int team);
static void team_close(desktop_t *app, int team);
int deskbar_pick_team(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y);
int deskbar_pick_system(desktop_t *app, tiku_trk_db_pick_t *pick);
/* End of the forward declarations. */


/** @brief Preferences, as this shell shows them: a window of its own. */
void
open_preferences(void *context)
{
    desktop_t *app = context;

    tiku_trk_prefs_window_open(&app->prefs_window);
}

/** @brief The art a Deskbar row named for itself when it was built (DB-002). */
int
deskbar_menu_icon(void *context, tiku_surface_t *s,
                  const tiku_trk_db_item_t *item, int x, int y, int size)
{
    (void)context;
    if ((item->kind != TIKU_TRK_DB_ITEM_ROW &&
         item->kind != TIKU_TRK_DB_ITEM_EXPAND) || item->icon[0] == '\0') {
        return 0;               /* a separator, a note, or a row with none */
    }
    return tiku_icon_paint(s, item->icon, x, y, size);
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

/** @brief Application @p team to the front: its windows back on the desk,
 *         the topmost of them focused (DB-019). */
static void
team_activate(desktop_t *app, int team)
{
    tiku_window_t *top = NULL;
    int k, m = tiku_workspace_count(app->desk);

    for (k = 0; k < m; k++) {
        tiku_window_t *ww = tiku_workspace_at(app->desk, k);

        if (desktop_team_has(app, team, ww)) {
            ww->minimized = 0;
            top = ww;
        }
    }
    if (top != NULL) {
        tiku_workspace_activate(app->desk, top);
    }
}

/** @brief Another instance of application @p team, the way the Tiku menu
 *         would start one (DB-021). */
static void
team_launch(desktop_t *app, int team)
{
    const char *name = tiku_trk_deskbar_team_app(app->deskbar, team);

    for (int i = 0; name != NULL && i < app->apps.count; i++) {
        if (strcasecmp(app->apps.app[i].name, name) == 0) {
            (void)tiku_trk_launch_run(&app->apps.app[i], NULL, 0);
            return;
        }
    }
}

/** @brief Every window of application @p team closed: an application's
 *         each told, as its own Close would; this shell's taken down. */
static void
team_close(desktop_t *app, int team)
{
    int shell = !desktop_team_is_app(app, team);

    for (int k = tiku_workspace_count(app->desk) - 1; k >= 0; k--) {
        tiku_window_t *ww = tiku_workspace_at(app->desk, k);

        if (!desktop_team_has(app, team, ww)) {
            continue;
        }
        if (shell) {
            tiku_workspace_close(app->desk, ww);
        } else {
            close_window(app, ww);
        }
    }
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
    if (pick->what == TIKU_TRK_DB_TRAY_ICON) {     /* its owner's to hear */
        desktop_tray_hit(app, pick->owner, pick->path);
        return;
    }
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
            /* Back on the desk, rising as a window just opened does;
             * only when the state really changes (DB-030). */
            tiku_workspace_restore(app->desk, t);
            tiku_workspace_activate(app->desk, t);
        }
        break;
    case TIKU_TRK_DB_MINIMIZE_WINDOW:
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            /* Away to the row that keeps it: a ghost of the window
             * shrinks and fades toward where the hand is. */
            if (!t->minimized) {
                tiku_workspace_minimize(app->desk, t,
                    (tiku_rect_t){ x - 8, y - 8, 16, 16 });
            }
            tiku_sound_play(TIKU_SOUND_WINDOW_MINIMIZE);
        }
        break;
    case TIKU_TRK_DB_CLOSE_WINDOW:
        t = tiku_workspace_find(app->desk, pick->tag);
        close_window(app, t);
        break;
    case TIKU_TRK_DB_ACTIVATE_TEAM:
        /* Activating the application: its windows to
         * the front, the top one focused (DB-019). */
        team_activate(app, pick->team);
        break;
    case TIKU_TRK_DB_LAUNCH_TEAM: {
        /* Middle click: another instance of the same
         * image (DB-021). */
        const char *cmd =
            tiku_trk_deskbar_team_command(app->deskbar,
                                          pick->team);

        if (cmd[0] == '\0') {      /* an application's row */
            team_launch(app, pick->team);
        } else {
            pid_t child = fork();

            if (child == 0) {
                execlp(cmd, cmd, (char *)NULL);
                _exit(127);
            }
            if (child > 0) {
                const char *leaf = strrchr(cmd, '/');
                tiku_trk_launch_note(child, leaf ? leaf + 1 : cmd);
            }
        }
        break;
    }
    case TIKU_TRK_DB_QUIT_TEAM:
    case TIKU_TRK_DB_KILL_TEAM:
        /* Quit on an application's own row: its windows asked to close,
         * the way its own Close would, and it ends with the last. */
        if (pick->what == TIKU_TRK_DB_QUIT_TEAM &&
            desktop_team_is_app(app, pick->team)) {
            team_close(app, pick->team);
            break;
        }
        /* Roster names can group unrelated processes, including the shell.
         * Select an authenticated peer and confirm in the native dialog. */
        desktop_forcequit_open(app);
        break;
    case TIKU_TRK_DB_HIDE_ALL:
    case TIKU_TRK_DB_SHOW_ALL: {
        int k, m = tiku_workspace_count(app->desk);
        for (k = 0; k < m; k++) {
            tiku_window_t *ww =
                tiku_workspace_at(app->desk, k);
            if (desktop_team_has(app, pick->team, ww)) {
                ww->minimized =
                    (pick->what == TIKU_TRK_DB_HIDE_ALL);
            }
        }
        break;
    }
    case TIKU_TRK_DB_CLOSE_ALL:
        /* Closing every window of the file manager is a
         * request to close its WINDOWS, never to quit
         * it -- the original makes the same
         * distinction. */
        team_close(app, pick->team);
        break;
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
        (void)desktop_help_open(app, NULL);
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
            (void)desktop_launch_app(app, "reader", NULL);
            break;
        }
        if (!strcmp(pick->path, "builtin:viewer")) {
            (void)desktop_launch_app(app, "image", NULL);
            break;
        }
        if (!strcmp(pick->path, "builtin:monitor") ||
            !strcmp(pick->path, "builtin:vfs")) {
            (void)desktop_launch_app(app,
                !strcmp(pick->path, "builtin:vfs") ? "vfs" : "monitor", NULL);
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
            desktop_damage_all(app);
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
        desktop_damage_all(app);
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
