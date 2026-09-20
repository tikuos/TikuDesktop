/*
 * tiku_trk_desktop_keys.c - the desk's own keyboard as responders: F1 on
 * one of its own above the windows; the add-on shortcuts, the chords the
 * menus advertise aimed at the focused folder, the switcher, closing a
 * window and the settings on one under the windows' own keys and over
 * the folders' navigation.  SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"

/** @brief The add-on shortcuts, the Find panel and Select by pattern. */
static int
keys_panels(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    (void)w;
    {
        unsigned addon_mods = 0u;
        int addon_command;

        if (ev->modifiers & TIKU_MOD_OPTION) addon_mods |= 0x1u;
        if (ev->modifiers & TIKU_MOD_CMD) addon_mods |= 0x2u;
        addon_command = tiku_trk_addons_shortcut(
            &app->addons, key_char(ev), addon_mods,
            TIKU_TRK_ADDON_COMMAND_BASE);
        if (addon_command >= 0) {
            menu_invoke(app, fl, addon_command);
            desktop_damage_all(app);
            return 1;
        }
    }
    if (key == 'f' && (ev->modifiers & TIKU_MOD_CMD)) {
        open_panel(app);
        return 1;
    }
    if (key == 'a' && (ev->modifiers & TIKU_MOD_CMD) &&
        (ev->modifiers & TIKU_MOD_SHIFT)) {
        /* PVK-047: Select by pattern, on the window it was
         * invoked from, opened near the pointer. */
        int wx, wy;

        if (!tiku_host_pointer(app->host, &wx, &wy, NULL)) {
            wx = app->surf->w / 2;
            wy = app->surf->h / 2;
        }
        tiku_trk_select_window_open(&app->select_window, fl,
                                    wx, wy);
        return 1;
    }
    return 0;
}

/** @brief The selection's chords: all, unmount, the clipboard's Escape,
 *         Delete, and the Get Info, Open, Edit name, Duplicate and Clean up
 *         chords. */
static int
keys_selection(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    (void)w;
    if (key == 'a' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        tiku_trk_poseview_select_all(fl->view);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'u' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* Cmd+U, which the volume menus advertise (CW-034). */
        menu_invoke(app, fl, TIKU_TRK_CMD_UNMOUNT);
        desktop_damage_all(app);
        return 1;
    }
    if (key == TIKU_KEY_ESCAPE && fl != NULL &&
        !alert_model(app)->open) {
        /* The alert's own Escape lives in the overlay, later
         * in this chain; this one is the clipboard's
         * (PVK-034). */
        if (ev->modifiers & TIKU_MOD_SHIFT) {
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
                    (void)tiku_trk_clip_remove(&app->clip,
                                               &p->model, 1);
                }
            }
            desktop_damage_all(app);
            return 1;
        }
        if (tiku_trk_clip_count(&app->clip) > 0) {
            /* Escape aborts a pending cut before anything
             * else it means. */
            tiku_trk_clip_clear(&app->clip);
            desktop_damage_all(app);
            return 1;
        }
        /* Nothing pending: fall through for whoever else
         * answers to Escape. */
    }
    if (key == TIKU_KEY_DELETE && fl == NULL) {
        /* No window focused: the key speaks to the Desktop's
         * own selection, as it does in Tracker. */
        fl = desktop_folder(app);
    }
    if (key == TIKU_KEY_DELETE && fl != NULL) {
        /* What the key MEANS depends on where the items are: a
         * move that can be undone where there is a Trash to
         * move them to, and a deletion inside it or when Shift
         * says the user has already decided (PVK-038).  The
         * key asks the same controller the menu item does, so
         * the question a delete puts up is one rule and not
         * one per way of reaching it (FS-046). */
        menu_invoke(app, fl,
            (ev->modifiers & TIKU_MOD_SHIFT)
                ? TIKU_TRK_CMD_DELETE
                : TIKU_TRK_CMD_MOVE_TO_TRASH);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'g' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* Cmd+G, which the menus now advertise (CW-033). */
        menu_invoke(app, fl, TIKU_TRK_CMD_EDIT_QUERY);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'i' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* The chord the menus have always advertised; until
         * now nothing was listening for it (AW-065). */
        menu_invoke(app, fl, TIKU_TRK_CMD_GET_INFO);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'o' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        if (p != NULL &&
            tiku_model_is_container(&p->model)) {
            (void)open_folder(app, p->model.path);
        } else if (p != NULL) {
            begin_edit(app, fl);
        } else {
            /* Grey in the menu; said out loud for the chord
             * (NeXT UIG ch6). */
            snprintf(app->status, sizeof app->status,
                     "Open is not available here");
        }
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'e' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        begin_rename(app, fl,
            tiku_trk_poseview_active(fl->view) != NULL
                ? tiku_trk_poseview_active(fl->view)->index : -1);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'd' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        menu_invoke(app, fl, TIKU_TRK_CMD_DUPLICATE);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'k' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        menu_invoke(app, fl,
            (ev->modifiers & TIKU_MOD_SHIFT)
                ? TIKU_TRK_CMD_CLEANUP_ALL
                : TIKU_TRK_CMD_CLEANUP);
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/** @brief Cut, copy, paste, undo and redo, the standard set the menus
 *         draw their shortcuts from, the view modes and the icon size. */
static int
keys_clipboard(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    (void)w;
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        (key == 'x' || key == 'c' || key == 'v')) {
        /* The clipboard chords, which until now only the menu
         * could reach here; Shift turns each into its "more"
         * or "links" variant, as the menu labels do. */
        unsigned m = (ev->modifiers & TIKU_MOD_SHIFT)
                         ? TIKU_TRK_MOD_SHIFT : 0u;
        int base = (key == 'x') ? TIKU_TRK_CMD_CUT
                 : (key == 'c') ? TIKU_TRK_CMD_COPY
                                : TIKU_TRK_CMD_PASTE;

        menu_invoke(app, fl, tiku_trk_sc_command(base, m));
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && key == 'z') {
        /* Cmd+Z / Shift+Cmd+Z: the file operations' undo and
         * redo, in this shell too (CW-061).  The inline editor
         * kept the key above, so a focused text field never
         * loses it. */
        char what[64], err[200];

        if (ev->modifiers & TIKU_MOD_SHIFT) {
            if (tiku_fs_can_redo(app->fs, what,
                                     sizeof what)) {
                int n = tiku_fs_redo(app->fs, err,
                                         sizeof err);

                snprintf(app->status, sizeof app->status,
                         "redid %s (%d item%s)", what, n,
                         (n == 1) ? "" : "s");
            } else {
                snprintf(app->status, sizeof app->status,
                         "nothing to redo");
            }
        } else if (tiku_fs_can_undo(app->fs, what,
                                        sizeof what)) {
            int n = tiku_fs_undo(app->fs, err, sizeof err);

            snprintf(app->status, sizeof app->status,
                     "undid %s (%d item%s)", what, n,
                     (n == 1) ? "" : "s");
        } else {
            snprintf(app->status, sizeof app->status,
                     "nothing to undo");
        }
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        tiku_trk_sc_command_for_key(key, ev->modifiers) != 0) {
        /* The rest of the standard set, resolved from the one
         * table the menus draw their shortcuts from -- a
         * chord the menu advertises must answer (PVK-042). */
        menu_invoke(app, fl,
            tiku_trk_sc_command_for_key(key, ev->modifiers));
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        (key == '1' || key == '2' || key == '3')) {
        /* AW-025/CW-065: the same chords the single-window app
         * binds.  The menu offers these modes; a shortcut that
         * works in one window and not the other is worse than
         * no shortcut at all. */
        menu_invoke(app, fl,
            (key == '1') ? TIKU_TRK_CMD_ICON_VIEW :
            (key == '2') ? TIKU_TRK_CMD_MINI_ICON_VIEW
                         : TIKU_TRK_CMD_LIST_VIEW);
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        (key == '+' || key == '=' || key == '-')) {
        (void)tiku_trk_poseview_step_icon_size(fl->view,
            (key == '-') ? 0 : 1);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_folder_window_content(fl).w);
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/** @brief The Deskbar preferences' keys, the switcher, closing the
 *         window, the desk's own panels and the settings chord. */
static int
keys_windows(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    {
        char printable[2];

        if (tiku_workspace_focused(app->desk) ==
                tiku_trk_deskbar_prefs_window_handle(
                    &app->deskbar_prefs_window) &&
            tiku_trk_deskbar_prefs_window_key(
                &app->deskbar_prefs_window,
                key_name(ev, printable))) {
            desktop_damage_all(app);
            return 1;
        }
    }
    if (tiku_trk_switcher_window_shown(&app->switcher_window)) {
        /* Modal while it is up, and every key goes to it: the
         * switcher IS the focus for as long as it shows. */
        char printable[2];
        const char *nm = key_name(ev, printable);
        tiku_trk_sw_act_t act = tiku_trk_switcher_window_key(
            &app->switcher_window, nm,
            (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0);

        if (act == TIKU_TRK_SW_COMMIT) {
            tiku_trk_switcher_window_close(&app->switcher_window,
                                           1);
        } else if (act == TIKU_TRK_SW_CANCEL) {
            tiku_trk_switcher_window_close(&app->switcher_window,
                                           0);
        } else if (act == TIKU_TRK_SW_QUIT_APP ||
                   act == TIKU_TRK_SW_HIDE_APP) {
            const char *selected = tiku_trk_switcher_window_selected(
                &app->switcher_window);

            snprintf(app->status, sizeof app->status,
                     "%s %.50s",
                     (act == TIKU_TRK_SW_QUIT_APP) ? "quitting"
                                                   : "hiding",
                     (selected != NULL) ? selected : "");
        }
        if (key == TIKU_KEY_TAB) {
            /* Tab while it is up moves on, which is the same
             * gesture that opened it. */
            tiku_trk_switcher_window_open(&app->switcher_window,
                (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0);
        }
        desktop_damage_all(app);
        return 1;
    }
    if (key == TIKU_KEY_TAB && (ev->modifiers & TIKU_MOD_CMD)) {
        tiku_trk_switcher_window_open(&app->switcher_window,
            (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'w' && (ev->modifiers & TIKU_MOD_CMD)) {
        close_window(app, w);
        desktop_damage_all(app);
        return 1;
    }
    if (w != NULL && (w == desktop_deviceprefs_handle(app) ||
                     w == desktop_startup_handle(app) ||
                     w == desktop_associations_handle(app) ||
                     w == desktop_forcequit_handle(app) ||
                     w == tiku_trk_fonts_window_handle(&app->fonts_window)) &&
        !alert_model(app)->open) {
        /* Under the desk's own shortcuts: the panel takes every key it is
         * focused for, but Cmd-Tab above still reaches the switcher. */
        (void)tiku_window_send(w, ev);
        return 1;
    }
    if (key == ',' && (ev->modifiers & TIKU_MOD_CMD)) {
        /* The settings are the application's, not a window's,
         * so this works with no folder focused. */
        tiku_trk_prefs_window_open(&app->prefs_window);
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief The desk's chords, in the order they have always been asked:
 *        help, the add-ons, the panels, the selection and clipboard
 *        chords, undo, the standard set, the view modes, the switcher,
 *        closing the window, the settings.
 */
static int
keys_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned key = ev->key;
    tiku_window_t *w = tiku_workspace_focused(app->desk);
    folder_t *fl = folder_of(app, w);

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    /* The down edge alone: this function is reached on both, and its own
     * key-up guard is further down, so an unguarded F1 opened the page
     * twice and left the reader a step of back history to nowhere. */
    if (keys_panels(app, ev, key, w, fl) ||
        keys_selection(app, ev, key, w, fl) ||
        keys_clipboard(app, ev, key, w, fl) ||
        keys_windows(app, ev, key, w, fl)) {
        return 1;
    }
    return 0;
}

/**
 * @brief F1 opens the help page of the focused window, whatever it is:
 *        asked before the window's own keys, so an application that
 *        takes every key cannot keep the reader away.
 */
static int
help_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned key = ev->key;

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (key == TIKU_KEY_F1 && !app->popup) {
        const char *topic = desktop_help_page_of(app,
            tiku_workspace_focused(app->desk));
        (void)desktop_help_open(app,
            topic ? topic : "index.html");
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/** @brief The desk's keyboard on the responder list. */
void
desktop_keys_attach(desktop_t *app)
{
    app->keys_responder = (tiku_trk_responder_t){
        .name = "keys", .self = app, .key = keys_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->keys_responder);
}

/** @brief The help key, above the Info windows and the frames. */
void
desktop_help_attach(desktop_t *app)
{
    app->help_responder = (tiku_trk_responder_t){
        .name = "help", .self = app, .key = help_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->help_responder);
}
