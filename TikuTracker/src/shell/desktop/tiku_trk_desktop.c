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

/* Used before they are defined. */
static void draw_nav_levels(desktop_t *app, int px, int py);
/* End of the forward declarations. */

static void
set_backdrop_color(void *context, const tiku_trk_color_t *color)
{
    desktop_t *app = context;

    if (app != NULL && color != NULL) {
        app->backdrop_color = TIKU_RGB(color->r, color->g, color->b);
    }
}

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

/** @brief The clock's own pulse: a minute must not go stale (DB-044). */
static int
tick_clock(void *ctx)
{
    desktop_t *app = ctx;
    char stamp[64];

    if (tiku_trk_deskbar_clock_stamp(app->deskbar, stamp,
                                     sizeof stamp) &&
        strcmp(stamp, app->clock_stamp) != 0) {
        /* Only when the displayed time turned over: a blind dirty every
         * second would repaint rows the economy checks rightly budget.
         * The FIRST observation is a baseline, not a change -- settings
         * and the fixture freeze both arrive after the loop starts. */
        if (app->clock_stamp[0] != '\0') {
            app->dirty = 1;
        }
        snprintf(app->clock_stamp, sizeof app->clock_stamp, "%s", stamp);
    }
    return 0;                       /* periodic: never done */
}

/** @brief One destination row of a picker (CW-035). */
static void
picker_add(desktop_t *app, int k, const char *label, const char *path,
           int move_dev_ok)
{
    tiku_trk_cwmenu_t *cw = &app->menus;
    int n = cw->picker_paths[k];
    int i;

    if (path == NULL || path[0] == '\0' || n >= TIKU_TRK_CMD_DEST_MAX ||
        (k == 0 && !move_dev_ok)) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(cw->picker_path[k][i], path) == 0) {
            return;             /* one row per destination */
        }
    }
    if (tiku_menu_add(&cw->picker[k], label,
            TIKU_TRK_CMD_DEST_BASE + k * TIKU_TRK_CMD_DEST_MAX + n,
            0, 0u, 1) >= 0) {
        snprintf(cw->picker_path[k][n], TIKU_PATH_MAX, "%s", path);
        cw->picker_paths[k] = n + 1;
    }
}

/**
 * @brief The Move to / Copy to / Create link pickers (CW-035, CW-058).
 *
 * Current folder, Recent folders, Desktop, Home, then volumes -- and Move
 * offers only destinations on the item's own device, the addLocalOnly
 * rule: a cross-device move is a copy wearing the wrong name.
 */
void
build_pickers(desktop_t *app, folder_t *fl)
{
    static const char *title[3] = { "Move to", "Copy to", "Create link" };
    const char *home = getenv("HOME");
    char recent[8][TIKU_PATH_MAX];
    char desk[TIKU_PATH_MAX];
    struct stat here;
    tiku_volumes_t vs;
    int have_here;
    int nrecent;
    int nvol;
    int k, i;

    if (fl == NULL) {
        return;
    }
    have_here = stat(fl->path, &here) == 0;
    nrecent = tiku_trk_recent_list(app->store, TIKU_TRK_RECENT_FOLDERS, 6,
                                   recent, 8);
    nvol = (tiku_volumes_scan(&vs) > 0) ? vs.n : 0;
    (void)tiku_desktop_dir(desk, sizeof desk);
    for (k = 0; k < 3; k++) {
        tiku_menu_clear(&app->menus.picker[k]);
        snprintf(app->menus.picker[k].title,
                 sizeof app->menus.picker[k].title, "%s", title[k]);
        app->menus.picker_paths[k] = 0;
        picker_add(app, k, "Current folder", fl->path, 1);
        for (i = 0; i < nrecent; i++) {
            struct stat rst;
            const char *leaf = strrchr(recent[i], '/');
            const char *rows[8];
            char label[TIKU_PATH_MAX];
            int r;

            if (stat(recent[i], &rst) != 0) {
                continue;       /* a destination that went away */
            }
            /* Two recents can share a leaf name; the one that does gains
             * the minimum distinguishing path (NeXT UIG ch8). */
            for (r = 0; r < nrecent; r++) {
                rows[r] = recent[r];
            }
            (void)tiku_attr_disambiguate(recent[i], rows, nrecent,
                                             label, sizeof label);
            if (strcmp(recent[i], desk) == 0 ||
                (home != NULL && strcmp(recent[i], home) == 0)) {
                /* Desktop and Home keep their own NAMED rows below; a
                 * recent that is one of them would shadow the name. */
                continue;
            }
            (void)leaf;
            picker_add(app, k, label, recent[i],
                       have_here && rst.st_dev == here.st_dev);
        }
        {
            struct stat dst;

            if (stat(desk, &dst) == 0) {
                picker_add(app, k, "Desktop", desk,
                           have_here && dst.st_dev == here.st_dev);
            }
            if (home != NULL && stat(home, &dst) == 0) {
                picker_add(app, k, "Home", home,
                           have_here && dst.st_dev == here.st_dev);
            }
        }
        for (i = 0; i < nvol; i++) {
            struct stat vst;

            if (!tiku_volume_shows_icon(&vs, &vs.v[i]) ||
                stat(vs.v[i].mount, &vst) != 0) {
                continue;
            }
            picker_add(app, k, vs.v[i].name, vs.v[i].mount,
                       have_here && vst.st_dev == here.st_dev);
        }
        if (k < 2 && app->menus.picker[k].count > 0) {
            /* Somewhere deeper than a row can name: the panel is the
             * navigator (AW-050), reached from inside the picker. */
            (void)tiku_menu_add_separator(&app->menus.picker[k]);
            (void)tiku_menu_add(&app->menus.picker[k],
                "Choose folder\xe2\x80\xa6",
                (k == 0) ? TIKU_TRK_CMD_MOVE_TO : TIKU_TRK_CMD_COPY_TO,
                0, 0u, 1);
        }
    }
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
/** @brief Apply the destination panel answer to its opaque folder target. */
static void
blit(desktop_t *app)
{
    if (app->host == NULL) {
        return;                     /* displayless: the surface IS the
                                     * output, and -shot writes it */
    }
    (void)tiku_host_present(app->host, app->surf);
}

/** @brief Adopt a native host frame throughout the desktop controllers. */
static int
resize_desktop(desktop_t *app, int width, int height)
{
    if (app == NULL || width <= 0 || height <= 0 ||
        (width == app->surf->w && height == app->surf->h)) {
        return 0;
    }
    if (tiku_surface_resize(app->surf, width, height,
                                 app->backdrop_color) != 0) {
        return -1;
    }
    app->status_overlay.screen_w = width;
    app->status_overlay.screen_h = height;
    app->find_window.screen_w = width;
    app->find_window.screen_h = height;
    app->select_window.screen_w = width;
    app->select_window.screen_h = height;
    app->prefs_window.screen_w = width;
    app->prefs_window.screen_h = height;
    app->deskbar_prefs_window.screen_w = width;
    app->deskbar_prefs_window.screen_h = height;
    app->switcher_window.screen_w = width;
    app->switcher_window.screen_h = height;
    app->file_panel.screen_w = width;
    app->file_panel.screen_h = height;
    app->folder_render.screen_w = width;
    app->folder_render.screen_h = height;
    app->folder_lifecycle.screen_w = width;
    app->folder_lifecycle.screen_h = height;
    (void)tiku_trk_desktop_view_resize(&app->desktop_view, width, height);
    app->drop_controller.screen_w = width;
    app->drop_controller.screen_h = height;
    return 1;
}


/**
 * @brief Follow the face, whatever changed it.
 *
 * A row has always been as tall as the letters in it -- but it was worked
 * out ONCE, at startup, and handed to everything that draws a list.  The
 * face is a preference now, changeable at any moment from either of two
 * windows, so a row worked out at launch is a row that stops fitting its
 * own words the moment somebody changes it.
 *
 * Taken every frame instead, and pushed to the copies.  Which is cheap:
 * it is one compare until it is not, and when it is, a redraw was owed
 * anyway.  Noticing here rather than at each place that can change the
 * face is deliberate -- there is no third window somebody might add later
 * that this would miss.
 */
static void
follow_font(desktop_t *app)
{
    int want = tiku_font_plain()->height + 5;

    if (want == app->row_h) {
        return;
    }
    app->row_h = want;
    app->folder_render.row_h = want;
    app->band_controller.row_h = want;
    app->drag_target.row_h = want;
    app->file_panel.row_h = want;
    app->script_adapter.row_h = want;
    /*
     * The open views are not touched here: a folder sets its own view's
     * row height from folder_render every time it draws, and this runs
     * first.  Reaching into them as well would be a second way of saying
     * the same thing, and the two would eventually disagree.
     */
    app->dirty = 1;
}

void
draw_all(desktop_t *app)
{
    follow_font(app);
    /* The strip tracker's owner, refreshed per frame so only the window a
     * gesture is running in draws the slide and the guide (TS-063). */
    app->folder_render.strip_owner =
        (tiku_trk_titleview_tracking(&app->tstrip) != TIKU_TRK_TRACK_NONE)
            ? app->tstrip_fl : NULL;

    tiku_fill(app->surf,
                   (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h },
                   app->backdrop_color);
    tiku_trk_deskbar_publish_workspace(app->deskbar, app->team, app->desk);
    if (!tiku_trk_deskbar_raised(app->deskbar)) {
        /* Under the windows: with auto-raise on, a window may cover the
         * bar until the user reaches for it.  Draw order IS the z-order
         * here -- there is no window server to ask (DB-052). */
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
        tiku_workspace_draw(app->desk);
    } else {
        tiku_workspace_draw(app->desk);
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
    }
    if (app->zoom_t0 != 0) {
        /* The window frame sliding out of or into the menu item's
         * rectangle: four steps of an inverted outline, done in an eye
         * blink (DB-030). */
        int64_t age = tiku_trk_now() - app->zoom_t0;

        if (age > 120000) {
            app->zoom_t0 = 0;
        } else {
            int num = (int)(age / 30000) + 1;   /* 1..4 */
            tiku_rect_t z;

            z.x = app->zoom_from.x +
                  (app->zoom_to.x - app->zoom_from.x) * num / 4;
            z.y = app->zoom_from.y +
                  (app->zoom_to.y - app->zoom_from.y) * num / 4;
            z.w = app->zoom_from.w +
                  (app->zoom_to.w - app->zoom_from.w) * num / 4;
            z.h = app->zoom_from.h +
                  (app->zoom_to.h - app->zoom_from.h) * num / 4;
            tiku_invert_frame(app->surf, z);
            app->dirty = 1;     /* the next frame moves it again */
        }
    }
    if (app->info_windows.hover_slot >= 0 &&
        app->info_windows.hover_field >= 0) {
        /* The truncated field's full text, floated over everything and
         * clamped to the screen (AW-073).  Only when the value really is
         * wider than its row -- a pop-up repeating what is already
         * readable would be noise. */
        int slot = app->info_windows.hover_slot;
        tiku_trk_info_t *info = &app->info_windows.model[slot];
        char value[TIKU_TRK_INFO_VAL_MAX];
        const char *label = "";
        int fx, fy, fw, fh;

        if (tiku_trk_info_field(info, app->store,
                app->info_windows.hover_field, &label, value,
                sizeof value) != TIKU_TRK_INFO_F_NONE &&
            tiku_trk_info_windows_field_rect(&app->info_windows, slot,
                app->info_windows.hover_field, &fx, &fy, &fw, &fh)) {
            const tiku_font_t *font = tiku_font_plain();
            int tw = tiku_text_width(font, value);

            if (tw > fw) {
                tiku_rect_t box = { fx - 4, fy - 2, tw + 12,
                                         font->height + 6 };

                if (box.x + box.w > app->surf->w) {
                    box.x = app->surf->w - box.w;
                }
                if (box.x < 0) {
                    box.x = 0;
                }
                tiku_fill(app->surf, box, TIKU_C_NOTE);
                tiku_frame(app->surf, box, TIKU_C_TEXT);
                tiku_text(app->surf, font, box.x + 5,
                               box.y + 2 + font->ascent, value,
                               TIKU_C_TEXT);
            }
        }
    }
    if (tiku_trk_titleview_tracking(&app->tstrip) == TIKU_TRK_TRACK_DRAG &&
        app->tstrip.removed) {
        /* The torn-out column follows the pointer as a dotted outline
         * (TS-008): dashes, drawn over everything, the column's own width
         * and the strip's height. */
        int wds = app->tstrip.removed_width;
        int x0 = app->strip_ptr_x - wds / 2;
        int y0 = app->strip_ptr_y - 20 /* TITLE_HEIGHT */ / 2;
        int d;

        for (d = 0; d < wds; d += 4) {
            tiku_rect_t dash = { x0 + d, y0, 2, 1 };

            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
            dash.y = y0 + 20 /* TITLE_HEIGHT */ - 1;
            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
        }
        for (d = 0; d < 20 /* TITLE_HEIGHT */; d += 4) {
            tiku_rect_t dash = { x0, y0 + d, 1, 2 };

            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
            dash.x = x0 + wds - 1;
            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
        }
    }
    if (tiku_trk_drag_active(&app->drag)) {
        /* What is being carried, drawn at the pointer: a drag with no
         * picture is a drag the user has to remember the contents of.
         * Translucent, and faded further the more it holds, so what it is
         * being dropped ON stays readable (IV-053). */
        int i, shown = (app->drag.count < 4) ? app->drag.count : 4;
        int alpha = (app->drag.count > 3) ? 120 : 168;

        for (i = 0; i < shown; i++) {
            tiku_rect_t box;
            char art[TIKU_NAME_MAX];
            tiku_model_t dm;

            if (app->backend->ops->stat(app->backend, app->drag.path[i],
                                        &dm) != 0) {
                continue;
            }
            (void)tiku_trk_nodeicon_resolve(&app->nicons, app->store, &dm,
                                            art, sizeof art);
            box.x = app->drag.x - 16 + i * 6;
            box.y = app->drag.y - 16 + i * 6;
            box.w = 32;
            box.h = 32;
            (void)tiku_icon_paint(app->surf, art, box.x, box.y, 32);
            /* Tinted over its own pixels rather than composited in an
             * offscreen: the picture is what the pointer carries, and it
             * has to sit over whatever is under it. */
            tiku_paint_tint(app->surf, box, app->backdrop_color,
                                255 - alpha);
            if (app->drag.count > 3) {
                /* Large selections get a short fade at the edge of the
                 * pointer picture.  The surface is opaque, so the fade is
                 * expressed as progressively stronger backdrop tinting over
                 * the four edge bands rather than an off-screen alpha
                 * bitmap (PVS-034). */
                tiku_rect_t edge = box;
                edge.w = 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge.x = box.x + box.w - 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge = box;
                edge.h = 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge.y = box.y + box.h - 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
            }
        }
        /* The copy cursor's spelling here: when the drop in front of the
         * pointer would COPY -- a directory on another store, or Option
         * forcing it -- the ghost wears a small plus badge, so the user is
         * told before the release which of the two acts this is
         * (PVS-036). */
        if (app->drag_target.folder != NULL) {
            char dest[TIKU_PATH_MAX];
            unsigned dmods = 0u;
            int same;

            if (app->drag_target.row >= 0) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(
                    app->drag_target.folder->view, app->drag_target.row);

                snprintf(dest, sizeof dest, "%s",
                         (p != NULL) ? p->model.path : "");
            } else {
                snprintf(dest, sizeof dest, "%s",
                         app->drag_target.folder->path);
            }
            if ((app->drag_live_mods & TIKU_MOD_OPTION) != 0u) {
                dmods |= TIKU_TRK_DRAG_MOD_COPY;
            }
            if ((app->drag_live_mods & TIKU_MOD_CMD) != 0u) {
                dmods |= TIKU_TRK_DRAG_MOD_LINK;
            }
            same = dest[0] != '\0' &&
                   tiku_fs_drop_action(app->fs, app->drag.path[0],
                                           dest, 0) == TIKU_OP_MOVE;
            if (dest[0] != '\0' &&
                tiku_trk_drag_action(&app->drag, dest, dmods, same) ==
                    TIKU_TRK_DROP_COPY) {
                tiku_rect_t badge = { app->drag.x + 10,
                                           app->drag.y + 10, 13, 13 };
                tiku_rect_t stroke;

                tiku_fill(app->surf, badge,
                               TIKU_RGB(255, 255, 190));
                tiku_frame(app->surf, badge, TIKU_C_TEXT);
                stroke = (tiku_rect_t){ badge.x + 3, badge.y + 6,
                                             7, 1 };
                tiku_fill(app->surf, stroke, TIKU_C_TEXT);
                stroke = (tiku_rect_t){ badge.x + 6, badge.y + 3,
                                             1, 7 };
                tiku_fill(app->surf, stroke, TIKU_C_TEXT);
            }
        }
    }
    /* Drawing another folder rebuilt the one shared menu model for that
     * folder.  Put it back on the bar that actually owns the popup before
     * painting or hit-testing it. */
    if (app->bar_fl == NULL && app->bar_open < 0 && app->popup != NULL &&
        app->popup_fl != NULL) {
        tiku_trk_sc_ctx_t c;

        build_open_with(app, app->popup_fl);
        build_per_type(app, app->popup_fl);
        c = menu_ctx(app, app->popup_fl, 0u);
        tiku_trk_cwmenu_rebuild(&app->menus, &c);
    }
    if (app->bar_fl != NULL && app->bar_open >= 0) {
        tiku_trk_sc_ctx_t c;

        build_open_with(app, app->bar_fl);
        build_per_type(app, app->bar_fl);
        c = menu_ctx(app, app->bar_fl, 0u);
        tiku_trk_cwmenu_rebuild(&app->menus, &c);
        app->menus.bar.open = app->bar_open;
        if (app->bar_open < app->menus.bar.count) {
            app->popup = app->menus.bar.menu[app->bar_open];
        }
    }
    if (app->gbar_on == 1) {
        /* The global bar: the focused folder's menus are REBUILT and
         * PUBLISHED, and the strip renders only what was published --
         * the same data a foreign window would hand in. */
        tiku_rect_t strip;
        tiku_window_t *fw = tiku_workspace_focused(app->desk);
        folder_t *ffl = folder_of(app, fw);

        if (ffl != NULL) {
            tiku_trk_sc_ctx_t gc;

            build_open_with(app, ffl);
            build_per_type(app, ffl);
            gc = menu_ctx(app, ffl, 0u);
            tiku_trk_cwmenu_rebuild(&app->menus, &gc);
            publish_folder_menus(app, ffl);
        }
        rebuild_global_bar(app);
        strip = global_bar_rect(app);
        tiku_fill(app->surf, strip, TIKU_C_PANEL);
        tiku_hline(app->surf, strip.x, strip.y + strip.h - 1,
                        strip.w, tiku_tint(TIKU_C_PANEL, 1.40f));
        tiku_menubar_draw(&app->gbar, app->surf, strip);
        if (app->gbar_open >= 0 && app->gbar_open < app->gbar.count) {
            app->popup = app->gbar.menu[app->gbar_open];
            app->popup_frame = tiku_menubar_panel(&app->gbar, strip,
                (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
        }
    }
    /* A menu is drawn last because it is above everything, including the
     * Deskbar strip. */
    if (app->popup != NULL) {
        /* Re-aimed here because the menus are rebuilt per draw, which
         * clears the hot row a moment after the motion set it. */
        (void)tiku_menu_track(app->popup, app->popup_frame,
                                  app->ptr_x, app->ptr_y);
        tiku_menu_draw(app->popup, app->surf, app->popup_frame);
        if (app->popup_sub != NULL) {
            (void)tiku_menu_track(app->popup_sub,
                                      app->popup_sub_frame, app->ptr_x,
                                      app->ptr_y);
            tiku_menu_draw(app->popup_sub, app->surf,
                               app->popup_sub_frame);
        }
    }
    if (app->qdrag_on) {
        /* The query icon in flight (Q-034), riding under the pointer. */
        (void)tiku_icon_paint(app->surf, "query", app->ptr_x - 16,
                                  app->ptr_y - 16, 32);
    }
    if (app->nav_on && app->popup != NULL) {
        draw_nav_levels(app, app->popup_frame.x + app->popup_frame.w - 4,
                        app->popup_frame.y + 4);
    } else if (app->spring_nav_on) {
        /* The spring's walk hangs off the sprung pose (PVS-051). */
        draw_nav_levels(app, app->spring_nav_x, app->spring_nav_y);
    }
    tiku_trk_status_overlay_draw(&app->status_overlay);
    app->dirty = 0;
    /*
     * Last of all, over everything: the boot screen on its way out.  The
     * desktop underneath is fully drawn by now -- the fade is the black
     * being lifted off a finished picture, not a picture being built up
     * behind one.
     */
    if (tiku_trk_splash_visible(&app->splash, tiku_trk_now())) {
        tiku_trk_splash_draw(&app->splash, app->surf,
                             (tiku_rect_t){ 0, 0, DESK_W, DESK_H },
                             tiku_trk_now());
        app->dirty = 1;         /* not finished going: come back */
    }
}

/**
 * @brief Every open nav level, outermost first, each hanging off the row
 *        that opened it -- so the trail back out is on screen (CW-045).
 */
static void
draw_nav_levels(desktop_t *app, int px, int py)
{
    {
        const tiku_font_t *nf = tiku_font_plain();
        int k;

        for (k = 0; k < app->nav.depth; k++) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];
            tiku_rect_t r;
            int i;

            tiku_trk_nav_place(&app->nav, k, px, py, app->surf->w,
                               app->surf->h,
                               &r.x, &r.y, &r.w, &r.h);
            tiku_fill(app->surf, r, TIKU_C_PANEL);
            tiku_bevel(app->surf, r,
                tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_2),
                tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
            if (lv->n == 0) {
                tiku_text(app->surf, nf, r.x + 8,
                               r.y + 2 + nf->ascent, TIKU_TRK_NAV_EMPTY,
                               tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
            }
            for (i = 0; i < lv->n; i++) {
                int ty = r.y + 2 + i * TIKU_TRK_NAV_ROW;
                tiku_rect_t row2;

                row2.x = r.x + 1; row2.y = ty; row2.w = r.w - 2;
                row2.h = TIKU_TRK_NAV_ROW;
                if (i == lv->sel) {
                    tiku_fill(app->surf, row2, TIKU_C_SELECT);
                }
                tiku_text(app->surf, nf, r.x + 8, ty + nf->ascent + 2,
                               lv->item[i].name,
                               (i == lv->sel) ? TIKU_C_SELTEXT
                               : (lv->item[i].enabled ? TIKU_C_TEXT
                                  : tiku_dim(TIKU_C_TEXT,
                                             TIKU_C_PANEL)));
                if (lv->item[i].is_container) {
                    /* The arrow says a row goes further, which is the one
                     * thing a nested menu must announce. */
                    tiku_ui_submenu_arrow(app->surf,
                        (tiku_rect_t){ r.x, row2.y, r.w, row2.h },
                        (i == lv->sel) ? TIKU_C_SELTEXT : TIKU_C_TEXT);
                }
            }
            /* The next level hangs off this level's chosen row. */
            px = r.x + r.w - 4;
            py = r.y + 2 + ((lv->sel >= 0) ? lv->sel : 0) *
                 TIKU_TRK_NAV_ROW;
        }
    }
}

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
/**
 * @brief Locate a companion program shipped beside this shell.
 *
 * Looked for where the build puts it, relative to THIS binary, so a tree
 * that was moved or built elsewhere still finds its own programs.
 *
 * @return nonzero when @p out names a program that exists.
 */
static int
find_companion(const char *argv0, const char *name, char *out, size_t max)
{
    static const char *const where[] = {
        "%s/%s",                        /* installed beside this shell   */
        "%s/build/%s",                  /* the tree's own build          */
        "%s/libexec/tiku/%s"
    };
    char dir[TIKU_PATH_MAX];
    char *slash;
    size_t i;
    int up;

    /* TIKU_APPS says where APPLICATIONS are installed, which is not
     * where this desktop's own companion programs live; a runner looked
     * for there would be missing on every install that separates the
     * two. */
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    /* Up the tree rather than a fixed number of dots: a development
     * build sits one directory deeper than an installed one, and a count
     * that is right for either is wrong for the other.  Climbing by
     * APPENDING "/.." rather than trimming keeps a relative argv[0]
     * working, which one trimmed down to "." cannot. */
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        for (i = 0; i < sizeof where / sizeof where[0]; i++) {
            char found[TIKU_PATH_MAX];
            char real[PATH_MAX];

            snprintf(found, sizeof found, where[i], step, name);
            if (access(found, X_OK) != 0) {
                continue;
            }
            /* ABSOLUTE, because everything launched starts in the user's
             * home: a path relative to this shell's directory names
             * nothing once the child has moved there.
             *
             * Resolved through a buffer of realpath()'s own size, which
             * is PATH_MAX and not this tree's shorter path limit: handed
             * a smaller one, a fortified libc aborts the process instead
             * of truncating. */
            if (realpath(found, real) != NULL) {
                snprintf(out, max, "%s", real);
            } else {
                snprintf(out, max, "%s", found);
            }
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/**
 * @brief Find the directory of installed applications, if there is one.
 *
 * Climbed from this binary the same way a companion program is, so a
 * development tree and an installed one both answer.
 */
static int
find_apps_dir(const char *argv0, char *out, size_t max)
{
    char dir[TIKU_PATH_MAX];
    char *slash;
    int up;

    if (getenv("TIKU_APPS") != NULL) {
        snprintf(out, max, "%s", getenv("TIKU_APPS"));
        return access(out, X_OK) == 0;
    }
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        char found[TIKU_PATH_MAX];
        char real[PATH_MAX];
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        /* Through real[], for the reason find_companion() gives. */
        snprintf(found, sizeof found, "%s/build/apps", step);
        if (access(found, X_OK) == 0 && realpath(found, real) != NULL) {
            snprintf(out, max, "%s", real);
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/** @brief The label an installed file is offered under. */
static void
app_label(const char *file, char *out, size_t max)
{
    size_t i;

    /* From the file's own name: reading it would mean loading it, and a
     * file the desktop will not load has no name to read. */
    if (strncmp(file, "tiku_", 5) == 0) {
        file += 5;
    }
    snprintf(out, max, "%s", file);
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '_') {
            out[i] = ' ';
        }
    }
    i = strlen(out);
    if (i > 3u && strcmp(out + i - 3, ".so") == 0) {
        out[i - 3] = '\0';
    }
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

/**
 * @brief The art a program's row wears, guessed from its file name.
 *
 * A hint, not a fact: nothing but the file's name is known without
 * loading it, and a program the desktop will not load has nothing else
 * to say for itself.
 */
static const char *
app_icon(const char *file)
{
    if (strstr(file, "edit") != NULL) {
        return "text_file";
    }
    return "application";
}

/**
 * @brief Offer every application installed beside this desktop.
 *
 * A file in `apps` is started in a process of its own through the
 * runner; one in `apps/trusted` is loaded into this process.  Moving the
 * file between the two is the whole of the decision.
 */
static void
offer_installed(desktop_t *app, const char *argv0)
{
    char root[TIKU_PATH_MAX];
    char runner[TIKU_PATH_MAX];
    char trusted[TIKU_PATH_MAX];
    struct dirent *entry;
    DIR *dir;
    int have_runner;

    if (!find_apps_dir(argv0, root, sizeof root)) {
        return;
    }
    have_runner = find_companion(argv0, "tiku-run", runner, sizeof runner);
    if (have_runner) {
        snprintf(app->runner, sizeof app->runner, "%s", runner);
    }

    /* Loaded first, so a trusted application is running before anything
     * the user could click. */
    snprintf(trusted, sizeof trusted, "%s/trusted", root);
    dir = opendir(trusted);
    while (dir != NULL && (entry = readdir(dir)) != NULL) {
        char path[TIKU_PATH_MAX];
        char why[192];
        const tiku_app_descriptor_t *loaded;
        size_t n = strlen(entry->d_name);

        if (n < 4u || strcmp(entry->d_name + n - 3, ".so") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", trusted, entry->d_name);
        loaded = tiku_app_load(path, why, sizeof why);
        if (loaded == NULL || !embed_start(app, loaded)) {
            /* Said, not swallowed: an application that was installed and
             * did not appear is a fault the user has to be told about. */
            snprintf(app->status, sizeof app->status, "%s: %s",
                     entry->d_name, (loaded == NULL) ? why : "no room");
            app->dirty = 1;
        }
    }
    if (dir != NULL) {
        (void)closedir(dir);
    }

    {
        /* Sorted, because a directory hands its entries back in whatever
         * order it likes and a menu whose rows move between runs is a
         * menu nobody can learn. */
        char name[TIKU_TRK_DB_APPS_MAX][64];
        int count = 0;
        int i, j;

        dir = opendir(root);
        while (dir != NULL && (entry = readdir(dir)) != NULL &&
               count < TIKU_TRK_DB_APPS_MAX) {
            size_t n = strlen(entry->d_name);

            if (n < 4u || n >= sizeof name[0] ||
                strcmp(entry->d_name + n - 3, ".so") != 0 || !have_runner) {
                continue;
            }
            snprintf(name[count++], sizeof name[0], "%s", entry->d_name);
        }
        if (dir != NULL) {
            (void)closedir(dir);
        }
        for (i = 1; i < count; i++) {
            char hold[64];

            snprintf(hold, sizeof hold, "%s", name[i]);
            for (j = i; j > 0 && strcmp(name[j - 1], hold) > 0; j--) {
                snprintf(name[j], sizeof name[0], "%s", name[j - 1]);
            }
            snprintf(name[j], sizeof name[0], "%s", hold);
        }
        for (i = 0; i < count; i++) {
            char path[TIKU_PATH_MAX];
            char label[TIKU_TRK_DB_LABEL_MAX];

            snprintf(path, sizeof path, "%s/%s", root, name[i]);
            app_label(name[i], label, sizeof label);
            /* The row carries the FILE; how it is started is decided
             * where the pick is answered, which is the one place that
             * knows the runner's own path. */
            (void)tiku_trk_deskbar_add_app(app->deskbar, label, path,
                                           app_icon(name[i]));
        }
    }

    {
        /* Where a file is installed decides where it is OFFERED, the
         * rule trusted/ already keeps: apps/programming/ is the
         * Programming row's list -- the tools a person makes programs
         * with, not one more thing that happens to be installed. */
        char progdir[TIKU_PATH_MAX];
        char name[TIKU_TRK_DB_APPS_MAX][64];
        int count = 0;
        int i, j;

        snprintf(progdir, sizeof progdir, "%s/programming", root);
        dir = opendir(progdir);
        while (dir != NULL && (entry = readdir(dir)) != NULL &&
               count < TIKU_TRK_DB_APPS_MAX) {
            size_t n = strlen(entry->d_name);

            if (n < 4u || n >= sizeof name[0] ||
                strcmp(entry->d_name + n - 3, ".so") != 0 ||
                !have_runner) {
                continue;
            }
            snprintf(name[count++], sizeof name[0], "%s", entry->d_name);
        }
        if (dir != NULL) {
            (void)closedir(dir);
        }
        for (i = 1; i < count; i++) {
            char hold[64];

            snprintf(hold, sizeof hold, "%s", name[i]);
            for (j = i; j > 0 && strcmp(name[j - 1], hold) > 0; j--) {
                snprintf(name[j], sizeof name[0], "%s", name[j - 1]);
            }
            snprintf(name[j], sizeof name[0], "%s", hold);
        }
        for (i = 0; i < count; i++) {
            char path[TIKU_PATH_MAX];
            char label[TIKU_TRK_DB_LABEL_MAX];

            snprintf(path, sizeof path, "%s/%s", progdir, name[i]);
            app_label(name[i], label, sizeof label);
            (void)tiku_trk_deskbar_add_app_in(app->deskbar,
                TIKU_TRK_DB_GROUP_PROGRAMMING, label, path,
                app_icon(name[i]));
        }
    }
}

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
    tiku_trk_click_t click = { 0 };
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
            case TIKU_EVENT_POINTER_DOWN: {
                if (ev.button == 1u && ev.modifiers == 0u) {
                    /* The resize strip: four pixels along the bar's
                     * inner edge, and the drag sets the short axis by
                     * hand (DB-050). */
                    tiku_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);
                    tiku_trk_db_dock_t dk =
                        tiku_trk_deskbar_dock(app.deskbar);
                    int inner_ok = 0;

                    if (dk == TIKU_TRK_DB_RIGHT_EXPANDED &&
                        ev.x >= bf.x && ev.x < bf.x + 4) {
                        inner_ok = 1;
                    }
                    if (dk == TIKU_TRK_DB_LEFT_EXPANDED &&
                        ev.x >= bf.x + bf.w - 4 && ev.x < bf.x + bf.w) {
                        inner_ok = 1;
                    }
                    if (inner_ok && ev.y >= bf.y && ev.y < bf.y + bf.h) {
                        app.bar_resize = 1;
                        app.bar_resize_x = ev.x;
                        app.bar_resize_w = tiku_trk_deskbar_width(
                            app.deskbar);
                        break;
                    }
                }
                if (ev.button == 1u &&
                    (ev.modifiers & TIKU_MOD_CMD)) {
                    /* Before anything else -- but ARMED, not decided: a
                     * CMD press that then moves re-docks the bar
                     * (DB-049); one that lets go where it landed is a
                     * chord the rows answer (DB-019/021). */
                    tiku_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.x >= bf.x &&
                        ev.x < bf.x + bf.w &&
                        ev.y >= bf.y &&
                        ev.y < bf.y + bf.h) {
                        app.dock_armed = 1;
                        app.dock_x = ev.x;
                        app.dock_y = ev.y;
                        app.dock_mods = ev.modifiers;
                        app.dock_button = ev.button;
                        break;
                    }
                }
                {   /* An info window takes its own clicks first. */
                    tiku_window_t *iw = tiku_workspace_focused(app.desk);
                    int islot = tiku_trk_info_windows_slot(&app.info_windows,
                                                           iw);
                    /* An open menu owns the next click wherever it lands,
                     * including over this window: choosing from the menu it
                     * just raised is not another click on the icon. */
                    if (islot >= 0 && !alert_model(&app)->open &&
                        app.popup == NULL) {
                        int which = -1;
                        tiku_trk_info_t *info = tiku_trk_info_windows_model(
                            &app.info_windows, islot);
                        tiku_trk_info_hit_t ih = tiku_trk_info_windows_hit(
                            &app.info_windows, islot, ev.x, ev.y, &which);

                        if (ih == TIKU_TRK_INFO_HIT_TAB && which >= 0) {
                            /* The tab strip owns which is current; the
                             * window re-reads the Attributes tab as it
                             * becomes current. */
                            tiku_trk_info_windows_select_tab(
                                &app.info_windows, islot, which);
                            app.dirty = 1;
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_NAME) {
                            tiku_trk_info_windows_begin_rename(
                                &app.info_windows, islot);
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_PERMISSION && which >= 0) {
                            char perr[160];

                            if (tiku_trk_info_perm_toggle(info,
                                    app.backend, which, perr,
                                    sizeof perr) != 0) {
                                tiku_alert_open(alert_model(&app),
                                    TIKU_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_ERROR, perr,
                                    "Cancel");
                            }
                            app.dirty = 1;
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_ICON) {
                            if (ev.button == 3u) {
                                /* The icon stands for the thing the window
                                 * is about, so it answers a secondary click
                                 * with that thing's own menu -- built from
                                 * the MODEL, and carried out against its
                                 * path because there is no selection here
                                 * to read (AW-076). */
                                tiku_model_t m;
                                tiku_trk_sc_ctx_t c =
                                    menu_ctx(&app, desktop_folder(&app), 0u);

                                if (app.backend->ops->stat(app.backend,
                                        info->path, &m) != 0) {
                                    break;
                                }
                                /* The menu acts on ONE NAMED THING: the
                                 * enable rules must see it as the
                                 * selection, whatever the desktop's
                                 * selection happens to be (AW-076). */
                                c.selection = 1;
                                c.selection_is_volume =
                                    m.kind == TIKU_KIND_VOLUME;
                                c.selection_ro =
                                    !tiku_model_is_writable(&m);
                                c.can_trash_or_dup = !c.selection_ro;
                                c.can_edit_name =
                                    m.kind != TIKU_KIND_TRASH &&
                                    m.kind != TIKU_KIND_DESKTOP &&
                                    !c.selection_ro;
                                snprintf(app.popup_path,
                                         sizeof app.popup_path, "%s",
                                         info->path);
                                app.popup = tiku_trk_cwmenu_context(
                                    &app.menus, &m, &c);
                                /* A COPY: every folder draw rebuilds the
                                 * shared context menu, and the icon's
                                 * extra rows must survive the frame. */
                                app.menus.info_ctx = *app.popup;
                                app.popup = &app.menus.info_ctx;
                                /* The items the source adds for THIS menu
                                 * alone (AW-076): a folder recounts, a
                                 * link re-points. */
                                if (tiku_model_is_container(&m) &&
                                    m.kind != TIKU_KIND_VOLUME &&
                                    m.kind != TIKU_KIND_ROOT) {
                                    (void)tiku_menu_add(app.popup,
                                        "Recalculate folder size",
                                        TIKU_TRK_CMD_RECALC_SIZE, 0, 0u,
                                        1);
                                }
                                if (m.kind == TIKU_KIND_SYMLINK) {
                                    (void)tiku_menu_add(app.popup,
                                        "Set new link target",
                                        TIKU_TRK_CMD_SET_LINK_TARGET, 0,
                                        0u, 1);
                                }
                                app.popup_sub = NULL;
                                app.popup_frame = tiku_menu_place(
                                    app.popup, ev.x, ev.y,
                                    (tiku_rect_t){ 0, 0, app.surf->w,
                                                        app.surf->h });
                                app.dirty = 1;
                                break;
                            }
                            /* Double-clicking the icon opens the item, as
                             * double-clicking its row would (AW-075). */
                            if (tiku_trk_now() - app.press_at < 500000) {
                                (void)open_folder(&app, info->path);
                                app.press_at = tiku_trk_now();
                                break;
                            }
                            app.press_at = tiku_trk_now();
                            /* And a press that moves carries the item
                             * itself -- except the Trash, which can only
                             * ever be opened (AW-075). */
                            if (ev.button == 1u &&
                                info->model.kind != TIKU_KIND_TRASH) {
                                char iparent[TIKU_PATH_MAX];
                                tiku_model_t im;

                                if (app.backend->ops->stat(app.backend,
                                        info->path, &im) == 0) {
                                    parent_path(info->path, iparent,
                                                sizeof iparent);
                                    tiku_trk_drag_arm(&app.drag, ev.x, ev.y,
                                                      ev.button, 0u, 1, iw,
                                                      iparent);
                                    (void)tiku_trk_drag_add_at(&app.drag,
                                        info->path, im.type, ev.x, ev.y, 1);
                                    tiku_trk_drag_target_reset(
                                        &app.drag_target);
                                }
                            }
                            break;
                        }
                        if (ih == TIKU_TRK_INFO_HIT_FIELD && which >= 0) {
                            char value[TIKU_TRK_INFO_VAL_MAX];
                            const char *label = "";
                            tiku_trk_info_field_t fk =
                                tiku_trk_info_field(info,
                                    app.store, which, &label, value,
                                    sizeof value);
                            char dir[TIKU_PATH_MAX];
                            char sel[TIKU_NAME_MAX];

                            if (fk == TIKU_TRK_INFO_F_OPENS_WITH) {
                                /* The pop-up as a walk: each click moves
                                 * to the next supporting application and
                                 * writes the preference at once
                                 * (AW-071). */
                                info_opens_with_next(&app, info);
                                app.dirty = 1;
                                break;
                            }
                            if (fk == TIKU_TRK_INFO_F_SIZE &&
                                tiku_model_is_container(
                                    &info->model)) {
                                /* Clicking the size asks again: a folder
                                 * counted an hour ago is a guess (AW-068). */
                                tiku_trk_info_calc_start(info);
                            } else if (fk == TIKU_TRK_INFO_F_LOCATION &&
                                tiku_trk_info_location(info,
                                    dir, sizeof dir, sel, sizeof sel)) {
                                folder_t *nf = open_folder(&app, dir);

                                if (nf != NULL) {
                                    select_named(&app, nf, sel);
                                }
                            } else if (fk == TIKU_TRK_INFO_F_LINK) {
                                if (tiku_trk_info_link_target(
                                        info, app.backend, dir,
                                        sizeof dir, sel, sizeof sel)) {
                                    folder_t *nf = open_folder(&app, dir);

                                    if (nf != NULL) {
                                        select_named(&app, nf, sel);
                                    }
                                } else {
                                    /* Nowhere to follow: the window offers
                                     * a picker rather than an error, which
                                     * is the whole of AW-074's flow. */
                                    folder_t *ffl = folder_of(&app,
                                        tiku_workspace_focused(app.desk));

                                    app.info_relink = islot;
                                    fpanel_open(&app, (ffl != NULL) ? ffl
                                                    : &app.folders[0],
                                                TIKU_TRK_CMD_GET_INFO);
                                }
                            }
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                {
                    tiku_window_t *dbprefs =
                        tiku_trk_deskbar_prefs_window_handle(
                            &app.deskbar_prefs_window);

                    if (dbprefs != NULL && dbprefs->open &&
                        !alert_model(&app)->open) {
                        tiku_event_t window_event = { 0 };

                        window_event.type = TIKU_EVENT_POINTER_DOWN;
                        window_event.time_us = tiku_trk_now();
                        window_event.x = ev.x;
                        window_event.y = ev.y;
                        window_event.button = ev.button;
                        if (tiku_window_send(dbprefs, &window_event)) {
                            break;
                        }
                    }
                }
                {
                    /* The same offer to the Desktop's own preferences:
                     * a window with controls hears the pointer, and says
                     * whether the click was one of its own. */
                    tiku_window_t *dkprefs =
                        tiku_trk_desktop_prefs_window_handle(
                            &app.desktop_prefs_window);

                    if (dkprefs != NULL && dkprefs->open &&
                        !alert_model(&app)->open) {
                        tiku_event_t window_event = { 0 };

                        window_event.type = TIKU_EVENT_POINTER_DOWN;
                        window_event.time_us = tiku_trk_now();
                        window_event.x = ev.x;
                        window_event.y = ev.y;
                        window_event.button = ev.button;
                        if (tiku_window_send(dkprefs, &window_event)) {
                            break;
                        }
                    }
                }
                {
                    tiku_window_t *fonts =
                        tiku_trk_fonts_window_handle(&app.fonts_window);

                    if (fonts != NULL && fonts->open &&
                        !alert_model(&app)->open) {
                        tiku_event_t window_event = { 0 };

                        window_event.type = TIKU_EVENT_POINTER_DOWN;
                        window_event.time_us = tiku_trk_now();
                        window_event.x = ev.x;
                        window_event.y = ev.y;
                        window_event.button = ev.button;
                        if (tiku_window_send(fonts, &window_event)) {
                            break;
                        }
                    }
                }
                {
                    tiku_window_t *prefs_window =
                        tiku_trk_prefs_window_handle(&app.prefs_window);

                    if (prefs_window != NULL && prefs_window->open &&
                        !alert_model(&app)->open) {
                        tiku_event_t window_event = { 0 };

                        window_event.type = TIKU_EVENT_POINTER_DOWN;
                        window_event.time_us = tiku_trk_now();
                        window_event.x = ev.x;
                        window_event.y = ev.y;
                        window_event.button = ev.button;
                        if (tiku_window_send(prefs_window,
                                                  &window_event)) {
                            break;
                        }
                    }
                }
                if (tiku_trk_status_overlay_pointer(&app.status_overlay,
                                                    ev.x, ev.y)) {
                    break;
                }
                tiku_window_t *w = NULL;
                tiku_hit_t hit;
                tiku_trk_db_pick_t pick;

                /* A menu that is showing eats the next click wherever it
                 * lands: choosing an item, or dismissing. */
                if (app.nav_on && app.popup != NULL) {
                    /* A click inside a level chooses: a folder walks in,
                     * and anything else is not choosable, so the click is
                     * simply consumed (CW-056). */
                    const tiku_font_t *nf = tiku_font_plain();
                    int px2 = app.popup_frame.x + app.popup_frame.w - 4;
                    int py2 = app.popup_frame.y + 4;
                    int k, took = 0;

                    (void)nf;
                    for (k = 0; k < app.nav.depth && !took; k++) {
                        tiku_rect_t r;
                        int at;

                        tiku_trk_nav_place(&app.nav, k, px2, py2,
                                           app.surf->w, app.surf->h,
                                           &r.x, &r.y, &r.w, &r.h);
                        at = tiku_trk_nav_item_at(&app.nav, k, r.x, r.y,
                                                  ev.x,
                                                  ev.y);
                        if (at >= 0) {
                            /* Levels deeper than the one clicked go: the
                             * walk follows the pointer rather than
                             * accumulating every branch visited. */
                            while (app.nav.depth > k + 1) {
                                (void)tiku_trk_nav_leave(&app.nav);
                            }
                            app.nav.level[k].sel = at;
                            if (app.nav.level[k].item[at].is_container) {
                                (void)tiku_trk_nav_enter(&app.nav, at);
                            }
                            took = 1;
                        }
                        px2 = r.x + r.w - 4;
                        py2 = r.y + 2 +
                              ((app.nav.level[k].sel >= 0)
                                   ? app.nav.level[k].sel : 0) *
                              TIKU_TRK_NAV_ROW;
                    }
                    if (took) {
                        app.dirty = 1;
                        break;
                    }
                }
                if (app.popup != NULL) {
                    tiku_window_t *fw = tiku_workspace_focused(app.desk);
                    folder_t *ffl = (app.bar_fl != NULL) ? app.bar_fl
                                  : (app.popup_fl != NULL) ? app.popup_fl
                                                           : folder_of(&app, fw);
                    int pick = -1;

                    if (app.popup_sub != NULL) {
                        pick = tiku_menu_item_at(app.popup_sub,
                                                     app.popup_sub_frame,
                                                     ev.x,
                                                     ev.y);
                        /*
                         * Enabled, like the parent menu below and like
                         * the other shell: a disabled row is drawn grey
                         * and refuses to light under the pointer, and
                         * this branch alone still ran it.  Mount's rows
                         * are built with enabled = NOT mounted, so
                         * clicking a greyed partition mounted it again.
                         *
                         * pick stays set on purpose: the click is then
                         * swallowed by the dismissal below rather than
                         * being offered to the parent menu underneath.
                         */
                        if (pick >= 0 && app.popup_sub->item[pick].enabled) {
                            if (app.popup_path[0] != '\0') {
                                (void)tiku_trk_folder_file_actions_invoke_path(
                                    &app.file_actions, app.popup_path,
                                    app.popup_sub->item[pick].command);
                                app.dirty = 1;
                            } else {
                                global_or_invoke(&app, ffl,
                                    app.popup_sub->item[pick].command);
                            }
                        }
                    }
                    if (pick < 0) {
                        pick = tiku_menu_item_at(app.popup,
                                                     app.popup_frame,
                                                     ev.x,
                                                     ev.y);
                        if (pick >= 0 && app.popup->item[pick].enabled) {
                            if (app.popup->item[pick].submenu != NULL) {
                                /* Opening a submenu is not a choice; the
                                 * menu stays up. */
                                app.popup_sub = app.popup->item[pick].submenu;
                                app.popup_sub_frame =
                                    tiku_menu_place_sub(app.popup, pick,
                                        app.popup_frame,
                                        (tiku_rect_t){ 0, 0,
                                            app.surf->w, app.surf->h });
                                app.dirty = 1;
                                break;
                            }
                            if (app.popup_path[0] != '\0') {
                                int pc = app.popup->item[pick].command;
                                int islot = -1;
                                {
                                    int fi;

                                    for (fi = 0;
                                         fi < TIKU_TRK_INFO_WINDOW_MAX;
                                         fi++) {
                                        tiku_trk_info_t *im =
                                            &app.info_windows.model[fi];

                                        if (im->used &&
                                            strcmp(im->path,
                                                   app.popup_path) == 0) {
                                            islot = fi;
                                            break;
                                        }
                                    }
                                }

                                if (pc == TIKU_TRK_CMD_RECALC_SIZE &&
                                    islot >= 0) {
                                    tiku_trk_info_calc_start(
                                        &app.info_windows.model[islot]);
                                } else if (pc ==
                                               TIKU_TRK_CMD_SET_LINK_TARGET
                                           && islot >= 0) {
                                    /* The same picker AW-074's broken-
                                     * link flow raises. */
                                    folder_t *ffl2 = folder_of(&app,
                                        tiku_workspace_focused(
                                            app.desk));

                                    app.info_relink = islot;
                                    fpanel_open(&app,
                                        (ffl2 != NULL) ? ffl2
                                                       : &app.folders[0],
                                        TIKU_TRK_CMD_GET_INFO);
                                } else {
                                    (void)tiku_trk_folder_file_actions_invoke_path(
                                        &app.file_actions, app.popup_path,
                                        pc);
                                }
                                app.dirty = 1;
                            } else {
                                global_or_invoke(&app, ffl,
                                    app.popup->item[pick].command);
                            }
                        }
                    }
                    app.popup_path[0] = '\0';
                    app.popup_fl = NULL;
                    app.popup = NULL;
                    app.popup_sub = NULL;
                    app.bar_fl = NULL;
                    app.bar_open = -1;
                    app.gbar_open = -1;
                    app.gbar.open = -1;
                    app.menus.bar.open = -1;
                    app.dirty = 1;
                    break;
                }

                if (app.gbar_on == 1 && app.popup == NULL &&
                    ev.y < app.menus.bar.height && ev.button == 1u) {
                    /* The strip: a press on a title opens its menu, from
                     * the published description alone. */
                    tiku_rect_t strip = global_bar_rect(&app);
                    int bi = tiku_menubar_title_at(&app.gbar, strip,
                                                       ev.x, ev.y);

                    if (bi >= 0) {
                        commit_rename(&app, 1);
                        app.gbar_open = bi;
                        app.gbar.open = bi;
                        app.popup = app.gbar.menu[bi];
                        app.popup_sub = NULL;
                        app.popup_frame = tiku_menubar_panel(&app.gbar,
                            strip, (tiku_rect_t){ 0, 0, app.surf->w,
                                                       app.surf->h });
                        app.nav_on = 0;
                        app.dirty = 1;
                        break;
                    }
                }

                /* The Deskbar and its menu take precedence: it sits above
                 * every window, as a corner strip must. */
                if (tiku_trk_deskbar_menu_open(app.deskbar) ||
                    tiku_trk_deskbar_hit(app.deskbar, ev.x,
                                         ev.y)) {
                    int secondary = (ev.button == 3);
                    int dclicks = 1;

                    /* The Deskbar's own double-click: same spot, inside
                     * the interval -- and NOT refreshed when it fires,
                     * so a triple click cannot re-trigger (DB-019). */
                    if (ev.button == 1u &&
                        tiku_trk_now() - app.db_click_at < 500000 &&
                        ev.x - app.db_click_x <= 4 &&
                        app.db_click_x - ev.x <= 4 &&
                        ev.y - app.db_click_y <= 4 &&
                        app.db_click_y - ev.y <= 4) {
                        dclicks = 2;
                        app.db_click_at = 0;
                    } else if (ev.button == 1u) {
                        app.db_click_at = tiku_trk_now();
                        app.db_click_x = ev.x;
                        app.db_click_y = ev.y;
                    }
                if (tiku_trk_deskbar_click(app.deskbar, ev.x,
                                               ev.y, ev.modifiers,
                                               (int)ev.button, dclicks,
                                               &pick)) {
                        if (ev.button == 1u && !secondary) {
                            app.deskbar_press = 1;
                            app.press_at = tiku_trk_now();
                            app.press_x = ev.x;
                            app.press_y = ev.y;
                        }
                        deskbar_pick_dispatch(&app, &pick,
                                              ev.x, ev.y);
                        app.dirty = 1;
                        break;
                    }
                }
                if (ev.button == 4u || ev.button == 5u) {
                    /* The wheel.  In an icon mode it moves the pixel
                     * origin; in a list it steps rows, which is what it
                     * has always done. */
                    tiku_window_t *ww = NULL;
                    folder_t *wfl;
                    tiku_rect_t bf =
                        tiku_trk_deskbar_frame(app.deskbar);

                    if (ev.x >= bf.x &&
                        ev.x < bf.x + bf.w &&
                        ev.y >= bf.y &&
                        ev.y < bf.y + bf.h) {
                        /* Over the bar: the list scrolls rather than
                         * whatever window happens to be under it
                         * (DB-034). */
                        int wstep = ((ev.modifiers &
                                      (TIKU_MOD_SHIFT |
                                       TIKU_MOD_CMD |
                                       TIKU_MOD_OPTION)) != 0u)
                                        ? 3 : 1;

                        /* A modifier scrolls three times as fast, the
                         * wheel and the arrows alike (DB-034). */
                        if (tiku_trk_deskbar_scroll(app.deskbar,
                                (ev.button == 4u) ? -wstep : wstep)) {
                            app.dirty = 1;
                        }
                        break;
                    }
                    (void)tiku_workspace_hit(app.desk, ev.x,
                                            ev.y, &ww);
                    wfl = folder_of(&app, ww);
                    if (wfl != NULL) {
                        int step = (ev.button == 4u) ? -1 : 1;

                        if (tiku_trk_poseview_mode(wfl->view) !=
                                TIKU_TRK_MODE_LIST) {
                            int ox = 0, oy = 0;

                            tiku_trk_poseview_origin(wfl->view, &ox, &oy);
                            tiku_trk_poseview_set_origin(wfl->view, ox,
                                oy + step * 3 * app.row_h);
                        } else {
                            tiku_trk_poseview_scroll_to(wfl->view,
                                tiku_trk_poseview_scroll(wfl->view) +
                                    step * 3);
                        }
                        app.dirty = 1;
                    } else if (ww != NULL) {
                        /* Over an application's own page: the wheel is
                         * ITS to answer, so it rides in as a WHEEL event
                         * in window coordinates -- embedded in this
                         * process, or across the wire to the session
                         * that owns it, the road a click already takes. */
                        struct embed_win *ew =
                            embed_win_of_window(&app, ww);
                        tiku_rect_t rc = tiku_window_content(ww);
                        tiku_event_t we;

                        memset(&we, 0, sizeof we);
                        we.type = TIKU_EVENT_WHEEL;
                        we.x = ev.x - rc.x;
                        we.y = ev.y - rc.y;
                        we.dy = (ev.button == 4u) ? -3 : 3;
                        we.time_us = tiku_trk_now();
                        if (ew != NULL) {
                            if (embed_deliver(ew, &we)) {
                                embed_stop(ew->slot);
                            }
                            app.dirty = 1;
                        } else if (tiku_remote_owner(&app.remote, ww)
                                       != NULL) {
                            tiku_remote_event(&app.remote, ww, &we);
                            app.dirty = 1;
                        }
                    }
                    break;
                }
                hit = tiku_workspace_hit(app.desk, ev.x, ev.y,
                                        &w);
                if (tiku_trk_find_window_popup_open(&app.find_window)) {
                    /* Its menus hang below the panel's window and over
                     * whatever is behind it, so the click goes to the
                     * panel wherever it lands -- the panel eats it either
                     * way, which is what closes a menu clicked off. */
                    w = tiku_trk_find_window_handle(&app.find_window);
                    hit = TIKU_HIT_CONTENT;
                }
                if (w == NULL && desktop_folder(&app)->view != NULL) {
                    /* The backdrop is a pose area, so a click on it is a
                     * click in a view -- the same branch every window's
                     * content goes through, which is what keeps the
                     * Desktop's icons behaving like a folder's (AW-018). */
                    hit = TIKU_HIT_CONTENT;
                }
                if (w != NULL && ev.button == 1u) {
                    folder_t *bfl = folder_of(&app, w);

                    if (bfl != NULL) {
                        tiku_rect_t bf = tiku_trk_folder_window_bar(bfl);
                        tiku_rect_t fi =
                            tiku_trk_folder_window_icon(bfl);
                        int bi;

                        if (ev.x >= fi.x && ev.x < fi.x + fi.w &&
                            ev.y >= fi.y && ev.y < fi.y + fi.h) {
                            tiku_model_t model;
                            char parent[TIKU_PATH_MAX];
                            char art[TIKU_NAME_MAX];

                            art[0] = '\0';

                            /* IV-057: the drag arms only from a pixel
                             * that is part of the PICTURE -- an icon is
                             * not a rectangle, and its transparent corner
                             * is not a handle. */
                            if (app.backend->ops->stat(app.backend, bfl->path,
                                                       &model) == 0) {
                                (void)tiku_trk_nodeicon_resolve(&app.nicons,
                                    app.store, &model, art, sizeof art);
                            }
                            if (art[0] != '\0' &&
                                tiku_icon_hit(art, fi.w, ev.x - fi.x,
                                                  ev.y - fi.y)) {
                                parent_path(bfl->path, parent, sizeof parent);
                                tiku_workspace_activate(app.desk, w);
                                app.press_at = tiku_trk_now();
                                app.press_x = ev.x;
                                app.press_y = ev.y;
                                app.press_row = -1;
                                app.press_fl = bfl;
                                app.press_down = 1;
                                app.press_menu = 0;
                                tiku_trk_drag_arm(&app.drag, ev.x, ev.y,
                                                  ev.button, 0u, 1, w,
                                                  parent);
                                (void)tiku_trk_drag_add_at(&app.drag,
                                    bfl->path, model.type, fi.x, fi.y, 1);
                                tiku_trk_drag_target_reset(&app.drag_target);
                                app.dirty = 1;
                                break;
                            }
                        }

                        build_open_with(&app, bfl);
                        build_per_type(&app, bfl);
                        {
                            tiku_trk_sc_ctx_t bc =
                                menu_ctx(&app, bfl, ev.modifiers);

                            tiku_trk_cwmenu_rebuild(&app.menus, &bc);
                        }
                        bi = tiku_menubar_title_at(&app.menus.bar, bf,
                                ev.x, ev.y);
                        if (bi >= 0) {
                            commit_rename(&app, 1);
                            tiku_workspace_activate(app.desk, w);
                            app.bar_fl = bfl;
                            app.bar_open = bi;
                            app.menus.bar.open = bi;
                            app.popup = app.menus.bar.menu[bi];
                            app.popup_sub = NULL;
                            app.popup_frame = tiku_menubar_panel(
                                &app.menus.bar, bf,
                                (tiku_rect_t){ 0, 0, app.surf->w,
                                                    app.surf->h });
                            app.nav_on = 0;
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (w != NULL && ev.button == 1u) {
                    /* The window's own scroll bar (PVL-051): arrows step,
                     * the track pages, the thumb is held. */
                    folder_t *sfl = folder_of(&app, w);
                    tiku_rect_t sb = (sfl != NULL)
                        ? tiku_trk_folder_window_scrollbar(sfl)
                        : (tiku_rect_t){ 0, 0, 0, 0 };

                    if (sfl != NULL && sb.w > 0 && ev.x >= sb.x &&
                        ev.x < sb.x + sb.w && ev.y >= sb.y &&
                        ev.y < sb.y + sb.h) {
                        int at = ev.y - sb.y;
                        int btn = sb.w;

                        if (at < btn) {
                            tiku_scroll_arrow_press(&sfl->sbar, -1);
                            tiku_trk_poseview_scroll_to(sfl->view,
                                tiku_trk_poseview_scroll(sfl->view) -
                                    tiku_scroll_line(&sfl->sbar));
                        } else if (at >= sb.h - btn) {
                            tiku_scroll_arrow_press(&sfl->sbar, 1);
                            tiku_trk_poseview_scroll_to(sfl->view,
                                tiku_trk_poseview_scroll(sfl->view) +
                                    tiku_scroll_line(&sfl->sbar));
                        } else {
                            tiku_trk_poseview_scroll_to(sfl->view,
                                tiku_scroll_press(&sfl->sbar,
                                    sb.h - 2 * btn, at - btn));
                        }
                        app.sbar_fl = sfl;
                        app.dirty = 1;
                        break;
                    }
                }
                if (w != NULL && ev.button == 1u) {
                    /* The column strip: a press starts the one tracker
                     * every heading gesture lives behind -- click sorts,
                     * Shift-click seconds, an edge drags a new width, a
                     * title slides to reorder, and out of the strip tears
                     * the column off (TS-063). */
                    folder_t *tfl = folder_of(&app, w);
                    tiku_rect_t title = (tfl != NULL)
                        ? tiku_trk_folder_window_title(tfl)
                        : (tiku_rect_t){ 0, 0, 0, 0 };

                    if (tfl != NULL && title.w > 0 && ev.x >= title.x &&
                        ev.x < title.x + title.w && ev.y >= title.y &&
                        ev.y < title.y + title.h) {
                        unsigned tf;

                        commit_rename(&app, 1);
                        tiku_trk_poseview_cancel_edit_wait(tfl->view);
                        if (app.tstrip_fl != tfl) {
                            tiku_trk_titleview_init(&app.tstrip, tfl->view,
                                                    title.h);
                            app.tstrip_fl = tfl;
                        }
                        tf = tiku_trk_titleview_mouse_down(&app.tstrip,
                            ev.x - title.x, ev.y - title.y, 1,
                            w == tiku_workspace_focused(app.desk),
                            tiku_trk_now());
                        if ((tf & TIKU_TRK_TT_ACTIVATE) != 0u) {
                            tiku_workspace_activate(app.desk, w);
                        }
                        if (tiku_trk_titleview_tracking(&app.tstrip) !=
                                TIKU_TRK_TRACK_NONE) {
                            /* The press engaged a title or an edge.  A
                             * press in the strip's empty gutter is NOT the
                             * strip's: it falls through to the view, where
                             * it starts a band as it always has. */
                            app.strip_ptr_x = ev.x;
                            app.strip_ptr_y = ev.y;
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (w != NULL && ev.button == 3u) {
                    folder_t *tfl = folder_of(&app, w);
                    tiku_rect_t title = (tfl != NULL)
                        ? tiku_trk_folder_window_title(tfl)
                        : (tiku_rect_t){ 0, 0, 0, 0 };

                    if (tfl != NULL && ev.x >= title.x &&
                        ev.x < title.x + title.w && ev.y >= title.y &&
                        ev.y < title.y + title.h) {
                        tiku_trk_sc_ctx_t tc = menu_ctx(&app, tfl,
                                                         ev.modifiers);

                        build_open_with(&app, tfl);
                        build_per_type(&app, tfl);
                        tiku_trk_cwmenu_rebuild(&app.menus, &tc);
                        tiku_workspace_activate(app.desk, w);
                        app.bar_fl = NULL;
                        app.bar_open = -1;
                        app.menus.bar.open = -1;
                        app.popup = &app.menus.attributes;
                        app.popup_sub = NULL;
                        app.popup_frame = tiku_menu_place(app.popup,
                            ev.x, ev.y,
                            (tiku_rect_t){ 0, 0, app.surf->w,
                                                app.surf->h });
                        app.press_down = 0;
                        app.dirty = 1;
                        break;
                    }
                }
                if (w != NULL && hit != TIKU_HIT_CONTENT) {
                    /* The CHROME activates a window; the content does not.
                     * That is what lets a click reach a list in a window
                     * behind another without the raise eating it -- the
                     * click-through a spatial desktop depends on. */
                    if (app.folder_edit.renaming &&
                        app.folder_edit.rename_folder != NULL &&
                        app.folder_edit.rename_folder->win != NULL &&
                        app.folder_edit.rename_folder->win != w) {
                        /* An open rename commits when its window loses
                         * the keyboard, as the other four activation
                         * roads already commit it: left open, the lit
                         * field in the background window was a lie, and
                         * the editor a place keys could still land. */
                        commit_rename(&app, 1);
                    }
                    tiku_workspace_activate(app.desk, w);
                }
                if (hit == TIKU_HIT_ZOOM) {
                    folder_t *fl = folder_of(&app, w);

                    if (fl != NULL) {
                        (void)tiku_workspace_zoom(app.desk, w,
                                                 tiku_trk_folder_window_fit_width(fl),
                                                 tiku_trk_folder_window_fit_height(fl));
                    }
                    app.dirty = 1;
                    break;
                }
                if (hit == TIKU_HIT_CLOSE) {
                    /* Control+close closes every window rather than this
                     * one, which is the only way to clear a spatial desktop
                     * without visiting each window. */
                    if (ev.modifiers & TIKU_MOD_CMD) {
                        int k;

                        for (k = 0; k < app.folder_count; k++) {
                            if (app.folders[k].win != NULL) {
                                tiku_workspace_close(app.desk,
                                                    app.folders[k].win);
                            }
                        }
                        if (tiku_trk_find_window_handle(&app.find_window) !=
                                NULL) {
                            tiku_trk_find_window_close(&app.find_window);
                        }
                        app.dirty = 1;
                        break;
                    }
                    /* Option+close takes the folder's whole subtree with
                     * it: the windows a descent through a deep folder left
                     * behind are exactly the ones nobody wants to close one
                     * at a time (AW-012). */
                    if (ev.modifiers & TIKU_MOD_OPTION) {
                        folder_t *cf = folder_of(&app, w);

                        if (cf != NULL) {
                            close_windows_under(&app, cf->path);
                            app.dirty = 1;
                            break;
                        }
                    }
                }
                if (hit == TIKU_HIT_CLOSE) {
                    close_window(&app, w);
                } else if (hit == TIKU_HIT_TAB ||
                           hit == TIKU_HIT_RESIZE) {
                    /* An application window being RESHAPED will need to
                     * hear its new size when the hand lets go. */
                    app.resize_track =
                        (hit == TIKU_HIT_RESIZE) ? w : NULL;
                    tiku_workspace_begin_track(app.desk, w, hit, ev.x,
                                              ev.y);
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_select_window_handle(
                                    &app.select_window)) {
                    (void)tiku_window_send(w, &ev);
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_panel_window_handle(
                                    &app.file_panel)) {
                    (void)tiku_window_send(w, &ev);
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           w == app.device_panel.window) {
                    /* A panel a device described: its controls are the
                     * shell's own, so its clicks come here like any
                     * other window's. */
                    tiku_workspace_activate(app.desk, w);
                    (void)tiku_window_send(w, &ev);
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           embed_win_of_window(&app, w) != NULL) {
                    struct embed_win *ew = embed_win_of_window(&app, w);
                    tiku_event_t rev = ev;
                    tiku_rect_t rc = tiku_window_content(w);

                    tiku_workspace_activate(app.desk, w);
                    rev.x -= rc.x;
                    rev.y -= rc.y;
                    if (embed_deliver(ew, &rev)) {
                        embed_stop(ew->slot);
                    } else {
                        app.embed_press = ew;
                    }
                    app.dirty = 1;
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           tiku_remote_owner(&app.remote, w) != NULL) {
                    /* A remote window's content belongs to its session:
                     * the event rides the wire in window coordinates. */
                    tiku_event_t rev = ev;
                    tiku_rect_t rc = tiku_window_content(w);

                    tiku_workspace_activate(app.desk, w);
                    rev.x -= rc.x;
                    rev.y -= rc.y;
                    tiku_remote_event(&app.remote, w, &rev);
                    app.remote_press = w;
                    app.dirty = 1;
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_find_window_handle(
                                    &app.find_window) &&
                           ev.button == 1u &&
                           tiku_trk_find_window_icon_at(&app.find_window,
                                                        ev.x, ev.y)) {
                    /* Picking up the query icon (Q-034): the search
                     * travels with the pointer until it is dropped. */
                    app.qdrag_on = 1;
                    app.dirty = 1;
                } else if (hit == TIKU_HIT_CONTENT && w != NULL &&
                           w == tiku_trk_find_window_handle(
                                    &app.find_window)) {
                    (void)tiku_window_send(w, &ev);
                } else if (hit == TIKU_HIT_CONTENT) {
                    folder_t *fl = (w != NULL) ? folder_of(&app, w)
                                               : desktop_folder(&app);
                    if (fl != NULL && fl->view != NULL) {
                        if (ev.button == 1u &&
                            tiku_trk_folder_window_count_at(fl, ev.x, ev.y)) {
                            /* The desktop shell exposes the same parent
                             * hierarchy through the existing Open Parent
                             * action; a count-strip click starts at its
                             * nearest ancestor. */
                            menu_invoke(&app, fl, TIKU_TRK_CMD_OPEN_PARENT);
                            app.dirty = 1;
                            break;
                        }
                        int row = tiku_trk_folder_window_row_at(fl, ev.x,
                                                               ev.y);
                        unsigned mods = 0;
                        unsigned click_flags = 0;
                        int64_t now = tiku_trk_now();

                        if (ev.modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                        if (ev.modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                        if (ev.modifiers & TIKU_MOD_SHIFT) {
                            click_flags |= TIKU_TRK_CLICK_EXTEND;
                        }
                        if (ev.modifiers & TIKU_MOD_CMD) {
                            click_flags |= TIKU_TRK_CLICK_EXTEND |
                                           TIKU_TRK_CLICK_CONTROL;
                        }
                        if (ev.button == 3) {
                            int k, kn;
                            int lst = (tiku_trk_poseview_mode(fl->view) ==
                                       TIKU_TRK_MODE_LIST);

                            /* Normalize on DOWN, but do not build the menu
                             * until UP: a secondary press is still allowed
                             * to turn into a drag (PVS-016). */
                            tiku_trk_poseview_cancel_edit_wait(fl->view);
                            commit_rename(&app, 1);
                            (void)tiku_trk_poseview_context_click(fl->view,
                                                                  row);
                            tiku_workspace_activate(app.desk, w);
                            app.press_at = now;
                            app.press_x = ev.x;
                            app.press_y = ev.y;
                            app.press_row = row;
                            app.press_fl = fl;
                            app.press_window = w;
                            app.press_focus =
                                tiku_workspace_focused(app.desk);
                            app.press_down = 1;
                            app.press_menu = 0;
                            tiku_trk_drag_arm(&app.drag, ev.x,
                                              ev.y, 3u, mods, 1,
                                              w, fl->path);
                            {
                                tiku_model_t source;
                                tiku_trk_drag_set_source_virtual(&app.drag,
                                    app.backend->ops->stat(app.backend,
                                        fl->path, &source) == 0 &&
                                    source.kind == TIKU_KIND_VIRTUAL_DIR);
                            }
                            kn = tiku_trk_poseview_count(fl->view);
                            for (k = 0; k < kn; k++) {
                                tiku_trk_pose_t *q =
                                    tiku_trk_poseview_at(fl->view, k);
                                int qx = 0, qy = 0, qw, qh;

                                if (q != NULL && q->selected) {
                                    if (!lst) {
                                        tiku_trk_poseview_pose_rect(fl->view,
                                            q, &qx, &qy, &qw, &qh);
                                    }
                                    (void)tiku_trk_drag_add_at(&app.drag,
                                        q->model.path, q->model.type, qx, qy,
                                        lst);
                                }
                            }
                            tiku_trk_drag_target_reset(&app.drag_target);
                            (void)tiku_trk_click_press(&click, NULL, 0, 0,
                                                       3u, 0u, now);
                        } else if (ev.button == 4 ||
                                   ev.button == 5) {
                            tiku_trk_poseview_scroll_to(fl->view,
                                tiku_trk_poseview_scroll(fl->view) +
                                ((ev.button == 4) ? -3 : 3));
                        } else {
                            tiku_trk_pose_t *clicked = (row >= 0)
                                ? tiku_trk_poseview_at(fl->view, row) : NULL;
                            int twice = tiku_trk_click_press(&click, clicked,
                                ev.x, ev.y,
                                ev.button, click_flags, now);

                            {
                                tiku_rect_t c = tiku_trk_folder_window_content(fl);
                                int ox = 0, oy = 0;

                                tiku_trk_poseview_origin(fl->view, &ox, &oy);
                                tiku_trk_poseview_click_at(fl->view, row,
                                    ev.x - (c.x + 2) + ox,
                                    ev.y - (c.y + 2) + oy, mods);
                            }
                            app.press_at = now;
                            app.press_x = ev.x;
                            app.press_y = ev.y;
                            app.press_row = row;
                            app.press_fl = fl;
                            app.press_window = w;
                            app.press_focus =
                                tiku_workspace_focused(app.desk);
                            app.press_down = 1;
                            app.press_menu = 0;
                            /* Armed on a pose, started only if the pointer
                             * then moves far enough (PVS-032).  The whole
                             * selection travels, not the pose under the
                             * pointer (PVS-033). */
                            tiku_trk_drag_arm(&app.drag, ev.x,
                                              ev.y,
                                              ev.button, mods,
                                              twice ? 2 : 1,
                                              w, fl->path);
                            {
                                tiku_model_t source;
                                tiku_trk_drag_set_source_virtual(&app.drag,
                                    app.backend->ops->stat(app.backend,
                                        fl->path, &source) == 0 &&
                                    source.kind == TIKU_KIND_VIRTUAL_DIR);
                            }
                            if (row < 0 && ev.button == 1u) {
                                /* Empty space: a sweep, not a click.  In
                                 * EVERY mode -- a list's rows are swept by
                                 * the same rectangle, and this shell had
                                 * no band at all, so the only way to
                                 * gather several was one modifier click at
                                 * a time (PVS-024). */
                                unsigned bm = 0u;

                                if (ev.modifiers & TIKU_MOD_SHIFT) {
                                    bm |= TIKU_TRK_MOD_SHIFT;
                                }
                                if (ev.modifiers & TIKU_MOD_CMD) {
                                    bm |= TIKU_TRK_MOD_CMD;
                                }
                                if (bm == 0u) {
                                    /* Without a modifier the band starts
                                     * from nothing; with one it toggles on
                                     * top of what is there (PVS-027). */
                                    tiku_trk_poseview_select_none(fl->view);
                                }
                                /* ARMED, not started: a band begins only
                                 * once the pointer has moved past the
                                 * slop, so a click on empty space that
                                 * clears the selection does not also flash
                                 * a rectangle (PVS-024). */
                                tiku_trk_band_controller_arm(
                                    &app.band_controller, fl, ev.x, ev.y,
                                    bm);
                            /* A sweep between two poses also ENDS any
                                 * half-finished double click: without this
                                 * the click that starts the band is
                                 * invisible to the counter, and the next
                                 * click on the icon just left completes a
                                 * double click the user did not make. */
                                (void)tiku_trk_click_press(&click, NULL, 0,
                                    0, ev.button, 0u, now);
                                app.dirty = 1;
                                break;
                            }
                            if (row >= 0) {
                                int k, kn = tiku_trk_poseview_count(fl->view);
                                int lst = (tiku_trk_poseview_mode(fl->view) ==
                                           TIKU_TRK_MODE_LIST);

                                for (k = 0; k < kn; k++) {
                                    tiku_trk_pose_t *q =
                                        tiku_trk_poseview_at(fl->view, k);
                                    int qx = 0, qy = 0, qw, qh;

                                    if (q == NULL || !q->selected) {
                                        continue;
                                    }
                                    if (!lst) {
                                        /* Where it sat, so a drop can put
                                         * the selection down in the shape
                                         * it was picked up in (PVS-045). */
                                        tiku_trk_poseview_pose_rect(fl->view,
                                                                    q, &qx,
                                                                    &qy, &qw,
                                                                    &qh);
                                    }
                                    (void)tiku_trk_drag_add_at(
                                        &app.drag, q->model.path,
                                        q->model.type, qx, qy, lst);
                                }
                            }
                            tiku_trk_drag_target_reset(&app.drag_target);
                            if (row >= 0 && tiku_trk_folder_window_name_at(
                                    fl, row, ev.x, ev.y)) {
                                /* Armed, not started: the rename begins only
                                 * once the double-click window passes with
                                 * no further click (PVS-014, MA-043). */
                                (void)tiku_trk_poseview_name_clicked(fl->view,
                                                                     row, now);
                            }
                            if (twice) {
                                tiku_trk_pose_t *p =
                                    tiku_trk_poseview_at(fl->view, row);
                                if (p != NULL &&
                                    tiku_trk_folder_window_column_at(fl,
                                        ev.x) == TIKU_TRK_COL_PATH) {
                                    /* PVS-011: a double click INSIDE the
                                     * Location column opens the folder the
                                     * path names -- the column's answer,
                                     * not the row's. */
                                    char parent[TIKU_PATH_MAX];
                                    char *slash;

                                    snprintf(parent, sizeof parent, "%s",
                                             p->model.path);
                                    slash = strrchr(parent, '/');
                                    if (slash != NULL) {
                                        if (slash == parent) {
                                            parent[1] = '\0';
                                        } else {
                                            *slash = '\0';
                                        }
                                        (void)open_folder(&app, parent);
                                    }
                                } else if (p != NULL &&
                                    tiku_model_is_container(&p->model)) {
                                    /* A folder opens its OWN window, which
                                     * is what makes this spatial rather
                                     * than a single browser. */
                                    (void)open_folder(&app, p->model.path);
                                    if (ev.modifiers & TIKU_MOD_OPTION) {
                                        /* PVK-018: Option-open closes the
                                         * window it came from, which is how
                                         * a spatial desktop is walked
                                         * without leaving a trail. */
                                        close_window(&app, w);
                                    }
                                } else if (p != NULL &&
                                           tiku_trk_poseview_is_query(
                                               fl->view)) {
                                    /* PVS-011: a result is identified by its
                                     * PATH here, and the useful thing to do
                                     * with a path is open what holds it --
                                     * the result itself is a value, not a
                                     * place. */
                                    char parent[TIKU_PATH_MAX];
                                    char *slash;

                                    snprintf(parent, sizeof parent, "%s",
                                             p->model.path);
                                    slash = strrchr(parent, '/');
                                    if (slash != NULL) {
                                        if (slash == parent) {
                                            parent[1] = '\0';
                                        } else {
                                            *slash = '\0';
                                        }
                                        (void)open_folder(&app, parent);
                                    }
                                } else if (p != NULL &&
                                           p->model.facts.link_broken) {
                                    offer_broken_link(&app, p->model.path);
                                } else if (p != NULL) {
                                    /* Not a container: it is LAUNCHED, in
                                     * whatever opens its type -- which is
                                     * what a double click has always meant
                                     * and this shell could not do (AW-030,
                                     * PVS-010). */
                                    int went = open_one(&app, &p->model);

                                    if ((ev.modifiers & TIKU_MOD_OPTION) &&
                                        went) {
                                        /* Option-open closes the window it
                                         * came from, once the launch has
                                         * actually happened (AW-032). */
                                        close_window(&app, w);
                                    }
                                }
                            }
                        }
                    }
                }
                app.dirty = 1;
                break;
            }
            case TIKU_EVENT_POINTER_MOVE:
                app.ptr_x = ev.x;
                app.ptr_y = ev.y;
                if (app.embed_press != NULL &&
                    app.embed_press->window != NULL) {
                    struct embed_win *ew = app.embed_press;
                    tiku_event_t rev = ev;
                    tiku_rect_t rc =
                        tiku_window_content(ew->window);

                    rev.x -= rc.x;
                    rev.y -= rc.y;
                    if (embed_deliver(ew, &rev)) {
                        embed_stop(ew->slot);
                    }
                    app.dirty = 1;
                    break;
                }
                if (app.remote_press != NULL &&
                    tiku_remote_owner(&app.remote,
                                           app.remote_press) != NULL) {
                    tiku_event_t rev = ev;
                    tiku_rect_t rc =
                        tiku_window_content(app.remote_press);

                    rev.x -= rc.x;
                    rev.y -= rc.y;
                    tiku_remote_event(&app.remote, app.remote_press,
                                           &rev);
                    app.dirty = 1;
                    break;
                }
                if (!tiku_trk_drag_active(&app.drag)) {
                    /* No press held anywhere: the window UNDER the
                     * pointer still hears it passing, which is what a
                     * toolbar's tips and every other hover are made
                     * of.  An application that wants none returns 0
                     * and the move cost a message. */
                    tiku_window_t *hw = NULL;

                    if (tiku_workspace_hit(app.desk, ev.x, ev.y,
                                                &hw) == TIKU_HIT_CONTENT &&
                        hw != NULL) {
                        struct embed_win *hew =
                            embed_win_of_window(&app, hw);

                        if (hew != NULL) {
                            tiku_event_t rev = ev;
                            tiku_rect_t rc = tiku_window_content(hw);

                            rev.x -= rc.x;
                            rev.y -= rc.y;
                            if (embed_deliver(hew, &rev)) {
                                embed_stop(hew->slot);
                            }
                        } else if (tiku_remote_owner(&app.remote, hw)
                                       != NULL) {
                            tiku_event_t rev = ev;
                            tiku_rect_t rc = tiku_window_content(hw);

                            rev.x -= rc.x;
                            rev.y -= rc.y;
                            tiku_remote_event(&app.remote, hw, &rev);
                        }
                    }
                }
                if (app.qdrag_on) {
                    app.dirty = 1;
                }
                if (tiku_trk_deskbar_motion(app.deskbar, ev.x, ev.y)) {
                    app.dirty = 1;
                }
                if (app.popup != NULL) {
                    /* The hot row follows the pointer, in the level it is
                     * over: without this a menu gives no sign of which
                     * item a pick would take. */
                    int menu_moved = 0;

                    menu_moved |= tiku_menu_track(app.popup,
                                                      app.popup_frame,
                                                      ev.x, ev.y);
                    if (app.popup_sub != NULL) {
                        menu_moved |= tiku_menu_track(app.popup_sub,
                                                          app.popup_sub_frame,
                                                          ev.x, ev.y);
                    }
                    if (menu_moved) {
                        app.dirty = 1;
                    }
                }
                if (app.deskbar_press &&
                    (ev.x - app.press_x > 4 || app.press_x - ev.x > 4 ||
                     ev.y - app.press_y > 4 || app.press_y - ev.y > 4)) {
                    app.deskbar_press = 0;
                }
                if (app.press_down &&
                    (ev.x - app.press_x > 4 ||
                     app.press_x - ev.x > 4 ||
                     ev.y - app.press_y > 4 ||
                     app.press_y - ev.y > 4)) {
                    /* Moved out of the long-press radius: this is a drag,
                     * not a held click, so neither the menu nor the rename
                     * is owed (PVS-015, PVS-017). */
                    if (app.press_fl != NULL) {
                        tiku_trk_poseview_cancel_edit_wait(app.press_fl->view);
                    }
                    app.press_down = 0;
                }
                if (app.sbar_fl != NULL && app.sbar_fl->sbar.tracking) {
                    tiku_rect_t sb =
                        tiku_trk_folder_window_scrollbar(app.sbar_fl);

                    tiku_trk_poseview_scroll_to(app.sbar_fl->view,
                        tiku_scroll_drag(&app.sbar_fl->sbar,
                            sb.h - 2 * sb.w, ev.y - sb.y - sb.w));
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_titleview_tracking(&app.tstrip) !=
                        TIKU_TRK_TRACK_NONE && app.tstrip_fl != NULL) {
                    tiku_rect_t st =
                        tiku_trk_folder_window_title(app.tstrip_fl);
                    unsigned tf = tiku_trk_titleview_mouse_moved(
                        &app.tstrip, ev.x - st.x, ev.y - st.y,
                        tiku_trk_now());

                    app.strip_ptr_x = ev.x;
                    app.strip_ptr_y = ev.y;
                    if ((tf & (TIKU_TRK_TT_REDRAW | TIKU_TRK_TT_LINE |
                               TIKU_TRK_TT_NO_LINE)) != 0u) {
                        app.dirty = 1;
                    }
                    break;
                }
                if (tiku_trk_drag_moved(&app.drag, ev.x, ev.y) ||
                    tiku_trk_drag_active(&app.drag)) {
                    if (app.spring_nav_on) {
                        spring_nav_track(&app, ev.x, ev.y);
                    }
                    (void)tiku_trk_drag_target_motion(&app.drag_target,
                                                      ev.x, ev.y,
                                                      tiku_trk_now());
                    /* Read at the badge, so the copy indication follows
                     * the modifier live, as the cursor did (PVS-036). */
                    if (app.drag_live_mods != ev.modifiers) {
                        app.drag_live_mods = ev.modifiers;
                        app.dirty = 1;
                    }
                }
                {
                    /* An Info window's icon answers a hovering drag with
                     * its selected variant (IV-058). */
                    int hot = tiku_trk_drag_active(&app.drag)
                                  ? info_icon_drop_slot(&app, ev.x, ev.y)
                                  : -1;

                    if (hot != app.info_windows.drop_hot) {
                        app.info_windows.drop_hot = hot;
                        app.dirty = 1;
                    }
                }
                {
                    /* The pointer resting on a truncated Info field opens
                     * the expanding pop-up; leaving takes it away
                     * (AW-073). */
                    tiku_window_t *hw = NULL;
                    int hslot = -1, hfield = -1, hwhich = -1;

                    if (!tiku_trk_drag_active(&app.drag)) {
                        (void)tiku_workspace_hit(app.desk, ev.x, ev.y,
                                                      &hw);
                        hslot = tiku_trk_info_windows_slot(
                            &app.info_windows, hw);
                        if (hslot >= 0 &&
                            hw == tiku_workspace_focused(app.desk) &&
                            tiku_trk_info_windows_hit(&app.info_windows,
                                hslot, ev.x, ev.y, &hwhich) ==
                                TIKU_TRK_INFO_HIT_FIELD) {
                            hfield = hwhich;
                        } else {
                            hslot = -1;
                        }
                    }
                    if (hslot != app.info_windows.hover_slot ||
                        hfield != app.info_windows.hover_field) {
                        app.info_windows.hover_slot = hslot;
                        app.info_windows.hover_field = hfield;
                        app.dirty = 1;
                    }
                }
                {
                    unsigned bm = 0u;

                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        bm |= TIKU_TRK_MOD_SHIFT;
                    }
                    if (ev.modifiers & TIKU_MOD_CMD) {
                        bm |= TIKU_TRK_MOD_CMD;
                    }
                    (void)tiku_trk_band_controller_motion(
                        &app.band_controller, ev.x, ev.y, bm);
                }
                if (app.bar_resize) {
                    int dw = (tiku_trk_deskbar_dock(app.deskbar) ==
                              TIKU_TRK_DB_RIGHT_EXPANDED)
                                 ? app.bar_resize_x - ev.x
                                 : ev.x - app.bar_resize_x;

                    if (tiku_trk_deskbar_set_width(app.deskbar,
                            app.bar_resize_w + dw) > 0) {
                        app.dirty = 1;
                    }
                }
                if (app.dock_armed &&
                    ((ev.x - app.dock_x) * (ev.x - app.dock_x) +
                     (ev.y - app.dock_y) * (ev.y - app.dock_y) >
                     TIKU_TRK_DRAG_SLOP * TIKU_TRK_DRAG_SLOP)) {
                    app.dock_armed = 0;
                    app.dock_drag = 1;
                }
                if (app.dock_drag) {
                    /* Ctrl held on the bar: the pointer IS the picker.
                     * Where it lets go decides the dock, and a release in
                     * the middle keeps the one it started from (DB-049). */
                        tiku_trk_db_dock_t want =
                        tiku_trk_deskbar_dock_at(ev.x, ev.y,
                            app.surf->w, app.surf->h,
                            tiku_trk_deskbar_dock(app.deskbar));

                    if (want != tiku_trk_deskbar_dock(app.deskbar)) {
                        tiku_trk_deskbar_set_dock(app.deskbar, want);
                        app.dirty = 1;
                    }
                }
                if (tiku_trk_deskbar_pointer(app.deskbar, ev.x,
                                             ev.y)) {
                    /* The one input the bar never had.  Auto-hide and
                     * auto-raise are both answers to "where is the
                     * pointer", and neither could be asked (DB-051,
                     * DB-052). */
                    app.dirty = 1;
                }
                if (tiku_workspace_tracking(app.desk)) {
                    if (tiku_workspace_track(app.desk, ev.x,
                                            ev.y)) {
                        app.dirty = 1;
                    }
                }
                break;
            case TIKU_EVENT_POINTER_UP: {
                int dropped = tiku_trk_drag_active(&app.drag);

                if (app.embed_press != NULL) {
                    struct embed_win *ew = app.embed_press;

                    app.embed_press = NULL;
                    if (ew->slot->running && ew->window != NULL) {
                        tiku_event_t rev = ev;
                        tiku_rect_t rc =
                            tiku_window_content(ew->window);

                        rev.x -= rc.x;
                        rev.y -= rc.y;
                        if (embed_deliver(ew, &rev)) {
                            embed_stop(ew->slot);
                        }
                    }
                    app.dirty = 1;
                    break;
                }
                if (app.remote_press != NULL) {
                    if (tiku_remote_owner(&app.remote,
                                               app.remote_press) != NULL) {
                        tiku_event_t rev = ev;
                        tiku_rect_t rc =
                            tiku_window_content(app.remote_press);

                        rev.x -= rc.x;
                        rev.y -= rc.y;
                        tiku_remote_event(&app.remote,
                                               app.remote_press, &rev);
                    }
                    app.remote_press = NULL;
                    app.dirty = 1;
                    break;
                }

                if (app.qdrag_on) {
                    /* The query icon lands (Q-034): over a folder window
                     * or the desktop, the drop menu asks what to make of
                     * it; back on the Find window it is just put down. */
                    tiku_window_t *tw = NULL;
                    folder_t *tfl;

                    (void)tiku_workspace_hit(app.desk, ev.x, ev.y,
                                                  &tw);
                    tfl = (tw != NULL) ? folder_of(&app, tw)
                                       : desktop_folder(&app);
                    app.qdrag_on = 0;
                    if (tfl != NULL && tfl->view != NULL &&
                        (tw == NULL ||
                         tw != tiku_trk_find_window_handle(
                                   &app.find_window))) {
                        tiku_model_t dm;
                        int ro = 1;

                        if (app.backend->ops->stat(app.backend, tfl->path,
                                                   &dm) == 0) {
                            ro = !tiku_model_is_mutable_container(&dm);
                        }
                        snprintf(app.qdrop_dest, sizeof app.qdrop_dest,
                                 "%s", tfl->path);
                        app.popup = tiku_trk_cwmenu_query_drop(&app.menus,
                                                               ro);
                        app.popup_fl = tfl;
                        app.popup_sub = NULL;
                        app.popup_frame = tiku_menu_place(app.popup,
                            ev.x, ev.y,
                            (tiku_rect_t){ 0, 0, app.surf->w,
                                                app.surf->h });
                    }
                    app.dirty = 1;
                    break;
                }

                if (tiku_trk_titleview_tracking(&app.tstrip) !=
                        TIKU_TRK_TRACK_NONE && app.tstrip_fl != NULL) {
                    tiku_rect_t st =
                        tiku_trk_folder_window_title(app.tstrip_fl);
                    unsigned tf = tiku_trk_titleview_mouse_up(&app.tstrip,
                        ev.x - st.x, ev.y - st.y,
                        (ev.modifiers & TIKU_MOD_SHIFT) != 0,
                        tiku_trk_now());

                    (void)tf;
                    app.dirty = 1;
                    break;
                }

                app.dock_drag = 0;
                app.bar_resize = 0;
                if (app.dock_armed) {
                    /* The press never moved: it was the chord after all
                     * (DB-019/020/021). */
                    tiku_trk_db_pick_t hpick;

                    app.dock_armed = 0;
                    if (tiku_trk_deskbar_click(app.deskbar, app.dock_x,
                                               app.dock_y, app.dock_mods,
                                               (int)app.dock_button, 1,
                                               &hpick)) {
                        deskbar_pick_dispatch(&app, &hpick, app.dock_x,
                                              app.dock_y);
                        app.dirty = 1;
                    }
                }
                app.deskbar_press = 0;
                if (tiku_trk_deskbar_release(app.deskbar, ev.x, ev.y)) {
                    app.dirty = 1;
                }
                (void)tiku_trk_band_controller_finish(&app.band_controller);
                if (dropped && app.spring_nav_on) {
                    /* Choosing a spring entry IS the drop (PVS-051): the
                     * chosen row is where the items land, a disabled row
                     * swallows the release, and a release outside the
                     * menu falls back to the drop under the pointer. */
                    char sdest[TIKU_PATH_MAX];
                    int got = spring_nav_hit(&app, ev.x, ev.y, sdest,
                                             sizeof sdest);

                    spring_nav_close(&app);
                    if (got == 1) {
                        tiku_trk_drop_controller_execute_into(
                            &app.drop_controller, sdest, ev.x, ev.y,
                            ev.modifiers, ev.button, 0);
                    } else if (got == 0) {
                        do_drop(&app, ev.x, ev.y, ev.modifiers, ev.button);
                    }
                } else if (dropped && drop_on_panel(&app, ev.x, ev.y)) {
                    /* Consumed by the panel, action or not (AW-059). */
                } else if (dropped && drop_on_info_icon(&app, ev.x, ev.y,
                                                 ev.modifiers)) {
                    /* Taken by an Info window's icon: the folder drop
                     * machinery must not run a second time. */
                } else if (dropped) {
                    do_drop(&app, ev.x, ev.y,
                            ev.modifiers, ev.button);
                } else if (ev.button == 3u && app.press_down &&
                           !app.press_menu && app.press_fl != NULL) {
                    folder_t *pfl = app.press_fl;
                    tiku_trk_pose_t *p = (app.press_row >= 0)
                        ? tiku_trk_poseview_at(pfl->view, app.press_row)
                        : NULL;
                    unsigned cmods = 0u;
                    tiku_trk_sc_ctx_t c;

                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        cmods |= TIKU_MENU_MOD_SHIFT;
                    }
                    if (ev.modifiers & TIKU_MOD_CMD) {
                        cmods |= TIKU_MENU_MOD_CMD;
                    }
                    c = menu_ctx(&app, pfl, cmods);
                    app.popup_fl = pfl;

                    build_open_with(&app, pfl);
                    build_per_type(&app, pfl);
                    app.popup = tiku_trk_cwmenu_context(&app.menus,
                                        (p != NULL) ? &p->model : NULL, &c);
                    app.bar_fl = NULL;
                    app.bar_open = -1;
                    app.menus.bar.open = -1;
                    app.nav_on = 0;
                    if (p != NULL && tiku_trk_nav_wanted(&p->model)) {
                        tiku_trk_nav_open(&app.nav, app.backend,
                                          p->model.path);
                        app.nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(&app),
                                                       "HideDotFiles");
                        app.nav.folders_first = tiku_trk_prefs_get(prefs_model(&app),
                                                   "SortFolderNamesFirst");
                        app.nav_on = 1;
                    }
                    app.popup_sub = NULL;
                    app.popup_frame = tiku_menu_place(app.popup,
                        ev.x, ev.y,
                        (tiku_rect_t){ 0, 0, app.surf->w,
                                            app.surf->h });
                    app.dirty = 1;
                }
                if (!dropped && app.press_down && !app.press_menu &&
                    app.press_window != NULL &&
                    tiku_workspace_focused(app.desk) ==
                        app.press_focus) {
                    /* Content clicks raise on release, while movement has
                     * already cleared press_down, leaving drag-out from a
                     * background window click-through.  A click that opened
                     * something is exempt: the raise would put the window
                     * clicked in back over the window it just opened. */
                    tiku_workspace_activate(app.desk,
                                                  app.press_window);
                }
                if (app.sbar_fl != NULL) {
                    tiku_scroll_release(&app.sbar_fl->sbar);
                    app.sbar_fl = NULL;
                }
                tiku_trk_drag_reset(&app.drag);
                tiku_trk_drag_target_reset(&app.drag_target);
                app.info_windows.drop_hot = -1;
                app.info_windows.hover_slot = -1;
                app.info_windows.hover_field = -1;
                /* A release ends whatever the press began -- a drag with a
                 * picture under the pointer, a band, a drop that raised an
                 * alert.  All of it is on the screen until a frame says
                 * otherwise, and a shell with no pointer moving over it
                 * gets no other reason to draw one. */
                app.dirty = 1;
                /* A long press already put the menu up, so its release must
                 * not put a second one up (PVS-017). */
                app.press_down = 0;
                app.press_menu = 0;
                app.press_window = NULL;
                app.press_focus = NULL;
                tiku_workspace_end_track(app.desk);
                if (app.resize_track != NULL) {
                    /* The hand let go: whoever owns the window is told
                     * the content's new size, and answers with a frame
                     * painted at it. */
                    tiku_window_t *rw2 = app.resize_track;
                    tiku_rect_t rc = tiku_window_content(rw2);
                    tiku_event_t rev;
                    struct embed_win *ew =
                        embed_win_of_window(&app, rw2);

                    memset(&rev, 0, sizeof rev);
                    rev.type = TIKU_EVENT_RESIZE;
                    rev.width = rc.w;
                    rev.height = rc.h;
                    if (ew != NULL) {
                        (void)embed_deliver(ew, &rev);
                    } else if (tiku_remote_owner(&app.remote,
                                                      rw2) != NULL) {
                        tiku_remote_event(&app.remote, rw2, &rev);
                    }
                    app.resize_track = NULL;
                    app.dirty = 1;
                }
                break;
            }
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
            case TIKU_EVENT_KEY_DOWN: {
                /* A menu that is open rewrites itself as modifiers move:
                 * nine of its items change label AND command together while
                 * Shift is held.  The table already knows how; this is the
                 * edge that tells it to.  Handled on BOTH key edges, since
                 * releasing Shift has to put the labels back. */
                if (app.popup != NULL) {
                    unsigned mk = ev.key;

                    if (mk == TIKU_KEY_SHIFT ||
                        mk == TIKU_KEY_CMD) {
                        tiku_window_t *fw = tiku_workspace_focused(app.desk);
                        folder_t *ffl = folder_of(&app, fw);
                        unsigned m2 = 0;
                        tiku_trk_sc_ctx_t c2;

                        /* TIKU_EVENT_KEY_UP reports the state BEFORE the release,
                         * so the bit has to be taken out by hand or the menu
                         * stays in its shifted face. */
                        if (ev.modifiers & TIKU_MOD_SHIFT) {
                            m2 |= TIKU_TRK_MOD_SHIFT;
                        }
                        if (ev.type == TIKU_EVENT_KEY_UP &&
                            mk == TIKU_KEY_SHIFT) {
                            m2 &= ~(unsigned)TIKU_TRK_MOD_SHIFT;
                        }
                        if (ev.type == TIKU_EVENT_KEY_DOWN &&
                            mk == TIKU_KEY_SHIFT) {
                            m2 |= TIKU_TRK_MOD_SHIFT;
                        }
                        c2 = menu_ctx(&app, ffl, m2);
                        tiku_trk_sc_update(app.popup, &c2);
                        if (app.popup_sub != NULL) {
                            tiku_trk_sc_update(app.popup_sub, &c2);
                        }
                        app.dirty = 1;
                    }
                    break;
                }
                if (ev.type == TIKU_EVENT_KEY_UP) {
                    break;
                }
                {
                unsigned key = ev.key;
                tiku_window_t *w = tiku_workspace_focused(app.desk);
                folder_t *fl = folder_of(&app, w);
                unsigned mods = 0;

                if (ev.modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
                if (ev.modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
                if (key == TIKU_KEY_ESCAPE &&
                    tiku_trk_deskbar_calendar_open(app.deskbar)) {
                    /* Escape is a focus-loss equivalent for the small clock
                     * popups; it must not close a folder or edit behind it. */
                    tiku_trk_deskbar_close_calendar(app.deskbar);
                    app.dirty = 1;
                    break;
                }
                /* Get Info owns its rename editor and window shortcuts. */
                if (tiku_trk_info_windows_key(&app.info_windows, key,
                                              ev.modifiers,
                                              key_char(&ev))) {
                    break;
                }
                if (tiku_trk_panel_window_key(&app.file_panel, key,
                                              ev.modifiers,
                                              key_char(&ev))) {
                    break;
                }
                if (tiku_workspace_focused(app.desk) ==
                        tiku_trk_find_window_handle(&app.find_window) &&
                    app.find_window.model.focus ==
                        TIKU_TRK_FIND_FOCUS_NAME &&
                    key == ' ' && (ev.modifiers & TIKU_MOD_CMD)) {
                    /* Cmd-Space completes the name against the first
                     * scoped place, or home when the search is everywhere
                     * (NeXT UIG ch3). */
                    tiku_trk_find_t *fm = &app.find_window.model;
                    char dir[TIKU_PATH_MAX];
                    char full[TIKU_NAME_MAX];
                    const char *home = getenv("HOME");
                    int pi, chosen = -1;

                    for (pi = 0; pi < fm->place_count; pi++) {
                        if (fm->place_mask & (1u << pi)) {
                            chosen = pi;
                            break;
                        }
                    }
                    if (chosen >= 0) {
                        snprintf(dir, sizeof dir, "/%s", fm->place[chosen]);
                    } else {
                        snprintf(dir, sizeof dir, "%s",
                                 (home != NULL && home[0] != '\0') ? home
                                                                    : "/");
                    }
                    if (tiku_trk_panel_complete(app.backend, dir,
                            fm->name_text, full, sizeof full) > 0) {
                        snprintf(fm->name_text, sizeof fm->name_text,
                                 "%s", full);
                    }
                    app.dirty = 1;
                    break;
                }
                {
                    tiku_window_t *rfw =
                        tiku_workspace_focused(app.desk);

                    if (embed_win_of_window(&app, rfw) != NULL) {
                        struct embed_win *ew =
                            embed_win_of_window(&app, rfw);
                        tiku_event_t rev = ev;

                        /* Closing a window is the RUNTIME's gesture, not
                         * a key the application may swallow: rule 6 gives
                         * the runtime the lifecycle, and an application
                         * that could eat Cmd-W would be unclosable from
                         * the keyboard.  It closes THIS window, which is
                         * the whole window when there is only one. */
                        if (key == 'w' &&
                            (ev.modifiers & TIKU_MOD_CMD)) {
                            embed_drop_window(ew, 1);
                            app.dirty = 1;
                            break;
                        }
                        if (embed_deliver(ew, &rev)) {
                            embed_stop(ew->slot);
                        }
                        app.dirty = 1;
                        break;
                    }
                    if (tiku_remote_owner(&app.remote, rfw) != NULL) {
                        tiku_event_t rev = ev;

                        tiku_remote_event(&app.remote, rfw, &rev);
                        app.dirty = 1;
                        break;
                    }
                }
                if (tiku_trk_find_window_key(&app.find_window, key,
                                             key_char(&ev))) {
                    break;
                }
                if (app.finding) {
                    if (key == TIKU_KEY_RETURN) {
                        app.finding = 0;
                        run_find(&app, fl);
                    } else if (key == TIKU_KEY_ESCAPE) {
                        app.finding = 0;
                        app.status[0] = '\0';
                        app.dirty = 1;
                    } else if (key == TIKU_KEY_BACKSPACE) {
                        if (app.find_len > 0) {
                            app.find_buf[--app.find_len] = '\0';
                        }
                        app.dirty = 1;
                    } else {
                        char typed = key_char(&ev);

                        if (typed >= 0x20 && typed < 0x7F &&
                            app.find_len < (int)sizeof app.find_buf - 1) {
                            app.find_buf[app.find_len++] = typed;
                            app.find_buf[app.find_len] = '\0';
                            app.dirty = 1;
                        }
                    }
                    break;
                }
                if (tiku_trk_select_window_key(&app.select_window, key,
                                               ev.modifiers,
                                               key_char(&ev))) {
                    break;
                }
                if (tiku_trk_open_with_window_key(&app.open_with_window,
                                                  key, ev.modifiers)) {
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_folder_edit_key(&app.folder_edit, fl, key,
                        ev.modifiers, key_char(&ev))) {
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_MOD_CMD) &&
                    tiku_workspace_focused(app.desk) ==
                        tiku_trk_prefs_window_handle(&app.prefs_window)) {
                    tiku_trk_prefs_window_close(&app.prefs_window);
                    app.dirty = 1;
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_MOD_CMD) &&
                    tiku_workspace_focused(app.desk) ==
                        tiku_trk_deskbar_prefs_window_handle(
                            &app.deskbar_prefs_window)) {
                    /* Deskbar preferences close; the desktop stays: quit
                     * belongs to the desktop, not to a settings window
                     * (DB-053). */
                    tiku_trk_deskbar_prefs_window_close(
                        &app.deskbar_prefs_window);
                    app.dirty = 1;
                    break;
                }
                if (key == 'q' && (ev.modifiers & TIKU_MOD_CMD)) {
                    begin_shutdown(&app);
                    break;
                }
                {
                    unsigned addon_mods = 0u;
                    int addon_command;

                    if (ev.modifiers & TIKU_MOD_OPTION) addon_mods |= 0x1u;
                    if (ev.modifiers & TIKU_MOD_CMD) addon_mods |= 0x2u;
                    addon_command = tiku_trk_addons_shortcut(
                        &app.addons, key_char(&ev), addon_mods,
                        TIKU_TRK_ADDON_COMMAND_BASE);
                    if (addon_command >= 0) {
                        menu_invoke(&app, fl, addon_command);
                        app.dirty = 1;
                        break;
                    }
                }
                if (key == 'f' && (ev.modifiers & TIKU_MOD_CMD)) {
                    open_panel(&app);
                    break;
                }
                if (key == 'a' && (ev.modifiers & TIKU_MOD_CMD) &&
                    (ev.modifiers & TIKU_MOD_SHIFT)) {
                    /* PVK-047: Select by pattern, on the window it was
                     * invoked from, opened near the pointer. */
                    int wx, wy;

                    if (!tiku_host_pointer(app.host, &wx, &wy, NULL)) {
                        wx = app.surf->w / 2;
                        wy = app.surf->h / 2;
                    }
                    tiku_trk_select_window_open(&app.select_window, fl,
                                                wx, wy);
                    break;
                }
                if (key == 'a' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    tiku_trk_poseview_select_all(fl->view);
                    app.dirty = 1;
                    break;
                }
                if (key == 'u' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    /* Cmd+U, which the volume menus advertise (CW-034). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_UNMOUNT);
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_KEY_ESCAPE && fl != NULL &&
                    !alert_model(&app)->open) {
                    /* The alert's own Escape lives in the overlay, later
                     * in this chain; this one is the clipboard's
                     * (PVK-034). */
                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        /* Shift+Escape removes the selection from the
                         * clipboard, or every pose in the folder when
                         * nothing is selected. */
                        int ci, cn = tiku_trk_poseview_count(fl->view);
                        int any =
                            tiku_trk_poseview_selection_count(fl->view) > 0;

                        for (ci = 0; ci < cn; ci++) {
                            tiku_trk_pose_t *p =
                                tiku_trk_poseview_at(fl->view, ci);

                            if (p != NULL && (!any || p->selected)) {
                                (void)tiku_trk_clip_remove(&app.clip,
                                                           &p->model, 1);
                            }
                        }
                        app.dirty = 1;
                        break;
                    }
                    if (tiku_trk_clip_count(&app.clip) > 0) {
                        /* Escape aborts a pending cut before anything
                         * else it means. */
                        tiku_trk_clip_clear(&app.clip);
                        app.dirty = 1;
                        break;
                    }
                    /* Nothing pending: fall through for whoever else
                     * answers to Escape. */
                }
                if (key == TIKU_KEY_DELETE && fl == NULL) {
                    /* No window focused: the key speaks to the Desktop's
                     * own selection, as it does in Tracker. */
                    fl = desktop_folder(&app);
                }
                if (key == TIKU_KEY_DELETE && fl != NULL) {
                    /* What the key MEANS depends on where the items are: a
                     * move that can be undone where there is a Trash to
                     * move them to, and a deletion inside it or when Shift
                     * says the user has already decided (PVK-038).  The
                     * key asks the same controller the menu item does, so
                     * the question a delete puts up is one rule and not
                     * one per way of reaching it (FS-046). */
                    menu_invoke(&app, fl,
                        (ev.modifiers & TIKU_MOD_SHIFT)
                            ? TIKU_TRK_CMD_DELETE
                            : TIKU_TRK_CMD_MOVE_TO_TRASH);
                    app.dirty = 1;
                    break;
                }
                if (key == 'g' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    /* Cmd+G, which the menus now advertise (CW-033). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_EDIT_QUERY);
                    app.dirty = 1;
                    break;
                }
                if (key == 'i' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    /* The chord the menus have always advertised; until
                     * now nothing was listening for it (AW-065). */
                    menu_invoke(&app, fl, TIKU_TRK_CMD_GET_INFO);
                    app.dirty = 1;
                    break;
                }
                if (key == 'o' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

                    if (p != NULL &&
                        tiku_model_is_container(&p->model)) {
                        (void)open_folder(&app, p->model.path);
                    } else if (p != NULL) {
                        begin_edit(&app, fl);
                    } else {
                        /* Grey in the menu; said out loud for the chord
                         * (NeXT UIG ch6). */
                        snprintf(app.status, sizeof app.status,
                                 "Open is not available here");
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 'e' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    begin_rename(&app, fl,
                        tiku_trk_poseview_active(fl->view) != NULL
                            ? tiku_trk_poseview_active(fl->view)->index : -1);
                    app.dirty = 1;
                    break;
                }
                if (key == 'd' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    menu_invoke(&app, fl, TIKU_TRK_CMD_DUPLICATE);
                    app.dirty = 1;
                    break;
                }
                if (key == 'k' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    menu_invoke(&app, fl,
                        (ev.modifiers & TIKU_MOD_SHIFT)
                            ? TIKU_TRK_CMD_CLEANUP_ALL
                            : TIKU_TRK_CMD_CLEANUP);
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_MOD_CMD) && fl != NULL &&
                    (key == 'x' || key == 'c' || key == 'v')) {
                    /* The clipboard chords, which until now only the menu
                     * could reach here; Shift turns each into its "more"
                     * or "links" variant, as the menu labels do. */
                    unsigned m = (ev.modifiers & TIKU_MOD_SHIFT)
                                     ? TIKU_TRK_MOD_SHIFT : 0u;
                    int base = (key == 'x') ? TIKU_TRK_CMD_CUT
                             : (key == 'c') ? TIKU_TRK_CMD_COPY
                                            : TIKU_TRK_CMD_PASTE;

                    menu_invoke(&app, fl, tiku_trk_sc_command(base, m));
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_MOD_CMD) && key == 'z') {
                    /* Cmd+Z / Shift+Cmd+Z: the file operations' undo and
                     * redo, in this shell too (CW-061).  The inline editor
                     * kept the key above, so a focused text field never
                     * loses it. */
                    char what[64], err[200];

                    if (ev.modifiers & TIKU_MOD_SHIFT) {
                        if (tiku_fs_can_redo(app.fs, what,
                                                 sizeof what)) {
                            int n = tiku_fs_redo(app.fs, err,
                                                     sizeof err);

                            snprintf(app.status, sizeof app.status,
                                     "redid %s (%d item%s)", what, n,
                                     (n == 1) ? "" : "s");
                        } else {
                            snprintf(app.status, sizeof app.status,
                                     "nothing to redo");
                        }
                    } else if (tiku_fs_can_undo(app.fs, what,
                                                    sizeof what)) {
                        int n = tiku_fs_undo(app.fs, err, sizeof err);

                        snprintf(app.status, sizeof app.status,
                                 "undid %s (%d item%s)", what, n,
                                 (n == 1) ? "" : "s");
                    } else {
                        snprintf(app.status, sizeof app.status,
                                 "nothing to undo");
                    }
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_MOD_CMD) && fl != NULL &&
                    tiku_trk_sc_command_for_key(key, ev.modifiers) != 0) {
                    /* The rest of the standard set, resolved from the one
                     * table the menus draw their shortcuts from -- a
                     * chord the menu advertises must answer (PVK-042). */
                    menu_invoke(&app, fl,
                        tiku_trk_sc_command_for_key(key, ev.modifiers));
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_MOD_CMD) && fl != NULL &&
                    (key == '1' || key == '2' || key == '3')) {
                    /* AW-025/CW-065: the same chords the single-window app
                     * binds.  The menu offers these modes; a shortcut that
                     * works in one window and not the other is worse than
                     * no shortcut at all. */
                    menu_invoke(&app, fl,
                        (key == '1') ? TIKU_TRK_CMD_ICON_VIEW :
                        (key == '2') ? TIKU_TRK_CMD_MINI_ICON_VIEW
                                     : TIKU_TRK_CMD_LIST_VIEW);
                    app.dirty = 1;
                    break;
                }
                if ((ev.modifiers & TIKU_MOD_CMD) && fl != NULL &&
                    (key == '+' || key == '=' || key == '-')) {
                    (void)tiku_trk_poseview_step_icon_size(fl->view,
                        (key == '-') ? 0 : 1);
                    tiku_trk_poseview_cleanup(fl->view,
                                              tiku_trk_folder_window_content(fl).w);
                    app.dirty = 1;
                    break;
                }
                if (tiku_trk_status_overlay_key(&app.status_overlay, key)) {
                    break;
                }
                {
                    char printable[2];

                    if (tiku_workspace_focused(app.desk) ==
                            tiku_trk_deskbar_prefs_window_handle(
                                &app.deskbar_prefs_window) &&
                        tiku_trk_deskbar_prefs_window_key(
                            &app.deskbar_prefs_window,
                            key_name(&ev, printable))) {
                        app.dirty = 1;
                        break;
                    }
                }
                if (tiku_trk_switcher_window_shown(&app.switcher_window)) {
                    /* Modal while it is up, and every key goes to it: the
                     * switcher IS the focus for as long as it shows. */
                    char printable[2];
                    const char *nm = key_name(&ev, printable);
                    tiku_trk_sw_act_t act = tiku_trk_switcher_window_key(
                        &app.switcher_window, nm,
                        (ev.modifiers & TIKU_MOD_SHIFT) ? 1 : 0);

                    if (act == TIKU_TRK_SW_COMMIT) {
                        tiku_trk_switcher_window_close(&app.switcher_window,
                                                       1);
                    } else if (act == TIKU_TRK_SW_CANCEL) {
                        tiku_trk_switcher_window_close(&app.switcher_window,
                                                       0);
                    } else if (act == TIKU_TRK_SW_QUIT_APP ||
                               act == TIKU_TRK_SW_HIDE_APP) {
                        const char *selected = tiku_trk_switcher_window_selected(
                            &app.switcher_window);

                        snprintf(app.status, sizeof app.status,
                                 "%s %.50s",
                                 (act == TIKU_TRK_SW_QUIT_APP) ? "quitting"
                                                               : "hiding",
                                 (selected != NULL) ? selected : "");
                    }
                    if (key == TIKU_KEY_TAB) {
                        /* Tab while it is up moves on, which is the same
                         * gesture that opened it. */
                        tiku_trk_switcher_window_open(&app.switcher_window,
                            (ev.modifiers & TIKU_MOD_SHIFT) ? 1 : 0);
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_KEY_TAB && (ev.modifiers & TIKU_MOD_CMD)) {
                    tiku_trk_switcher_window_open(&app.switcher_window,
                        (ev.modifiers & TIKU_MOD_SHIFT) ? 1 : 0);
                    app.dirty = 1;
                    break;
                }
                if (key == ',' && (ev.modifiers & TIKU_MOD_CMD)) {
                    /* The settings are the application's, not a window's,
                     * so this works with no folder focused. */
                    tiku_trk_prefs_window_open(&app.prefs_window);
                    app.dirty = 1;
                    break;
                }
                if (key == 'n' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    menu_invoke(&app, fl, TIKU_TRK_CMD_NEW_FOLDER);
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_KEY_MENU && (ev.modifiers &
                        (TIKU_MOD_CMD | TIKU_MOD_SHIFT | TIKU_MOD_OPTION)) == 0) {
                    /* The Menu key alone opens the leaf menu -- the one
                     * gesture that reaches the Deskbar without the
                     * pointer (DB-011). */
                    if (tiku_trk_deskbar_menu_open(app.deskbar)) {
                        tiku_trk_deskbar_close_menu(app.deskbar);
                    } else {
                        tiku_trk_deskbar_open_menu(app.deskbar);
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 's' && (ev.modifiers & TIKU_MOD_CMD) &&
                    fl != NULL) {
                    /* PVK-046: invert -- the only way to say "everything
                     * except these" without clicking the rest. */
                    tiku_trk_poseview_invert_selection(fl->view);
                    app.dirty = 1;
                    break;
                }
                if (key == TIKU_KEY_TAB && fl != NULL) {
                    /* Tab is a name-order walk over poses.  While filtering
                     * is active it is deliberately swallowed. */
                    if (!tiku_trk_poseview_is_filtering(fl->view)) {
                        (void)tiku_trk_poseview_tab_next(fl->view,
                            (ev.modifiers & TIKU_MOD_SHIFT) != 0u);
                    }
                    app.dirty = 1;
                    break;
                }
                if (key == 'w' && (ev.modifiers & TIKU_MOD_CMD)) {
                    close_window(&app, w);
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && key == TIKU_KEY_DOWN &&
                    (ev.modifiers & TIKU_MOD_CMD) &&
                    (ev.modifiers & TIKU_MOD_OPTION)) {
                    tiku_trk_pose_t *p =
                        tiku_trk_poseview_active(fl->view);

                    if (p != NULL &&
                        tiku_model_is_container(&p->model)) {
                        tiku_window_t *from = fl->win;

                        if (open_folder(&app, p->model.path) != NULL) {
                            close_window(&app, from);
                        }
                    }
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && key == TIKU_KEY_DOWN &&
                    (ev.modifiers & TIKU_MOD_CMD)) {
                    tiku_trk_pose_t *p =
                        tiku_trk_poseview_active(fl->view);

                    if (p != NULL &&
                        tiku_model_is_container(&p->model)) {
                        (void)open_folder(&app, p->model.path);
                    } else if (p != NULL) {
                        begin_edit(&app, fl);
                    }
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL && key == TIKU_KEY_UP &&
                    (ev.modifiers & TIKU_MOD_CMD)) {
                    /* PVK-040/CW-051: Cmd+Up opens the parent; Option
                     * additionally takes the spatial window we left. */
                    if (strcmp(fl->path, "/") != 0) {
                        char parent[TIKU_PATH_MAX];
                        char *slash;
                        tiku_window_t *from = fl->win;

                        snprintf(parent, sizeof parent, "%s", fl->path);
                        slash = strrchr(parent, '/');
                        if (slash == parent) {
                            parent[1] = '\0';
                        } else if (slash != NULL) {
                            *slash = '\0';
                        }
                        if (slash != NULL &&
                            open_folder(&app, parent) != NULL &&
                            (ev.modifiers & TIKU_MOD_OPTION)) {
                            close_window(&app, from);
                        }
                    }
                    app.dirty = 1;
                    break;
                }
                if (fl != NULL) {
                    /* Left and Right matter in an icon mode, where the
                     * arrangement is two-dimensional; the pages matter in
                     * both and were bound in neither shell's multi-window
                     * half (PVK-007, PVK-011). */
                    int nav = (key == TIKU_KEY_UP)    ? TIKU_TRK_KEY_UP
                            : (key == TIKU_KEY_DOWN)  ? TIKU_TRK_KEY_DOWN
                            : (key == TIKU_KEY_LEFT)  ? TIKU_TRK_KEY_LEFT
                            : (key == TIKU_KEY_RIGHT) ? TIKU_TRK_KEY_RIGHT
                            : (key == TIKU_KEY_PAGE_UP) ? TIKU_TRK_KEY_PGUP
                            : (key == TIKU_KEY_PAGE_DOWN)  ? TIKU_TRK_KEY_PGDN
                            : (key == TIKU_KEY_HOME)  ? TIKU_TRK_KEY_HOME
                            : (key == TIKU_KEY_END)   ? TIKU_TRK_KEY_END : 0;
                    if (nav != 0) {
                        (void)tiku_trk_poseview_key_nav(fl->view, nav, mods);
                    } else if (key == TIKU_KEY_RETURN) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_active(fl->view);
                        if (p != NULL &&
                            tiku_model_is_container(&p->model)) {
                            /* Option+Return opens and takes the window it
                             * came from with it, the keyboard's version of
                             * an Option double-click (PVK-017). */
                            int chase = (ev.modifiers & TIKU_MOD_OPTION) != 0;
                            tiku_window_t *from = fl->win;

                            if (open_folder(&app, p->model.path) != NULL &&
                                chase) {
                                close_window(&app, from);
                            }
                        } else if (p != NULL) {
                            /* Opening a leaf means editing its value: there
                             * is no other window for it to open. */
                            begin_edit(&app, fl);
                        }
                    } else if (key == TIKU_KEY_BACKSPACE) {
                        char parent[TIKU_PATH_MAX];
                        char *slash;
                        snprintf(parent, sizeof parent, "%s", fl->path);
                        slash = strrchr(parent, '/');
                        if (slash != NULL) {
                            if (slash == parent) { parent[1] = '\0'; }
                            else { *slash = '\0'; }
                            (void)open_folder(&app, parent);
                        }
                    }
                    app.dirty = 1;
                }
                break;
            }
            }
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
