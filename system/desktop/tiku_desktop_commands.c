/* tiku_desktop_commands.c -- commands.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_desktop_private.h"

/** @brief Fill the Attributes menu's per-type submenus. */
void
build_per_type(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_menu_build_per_type(&app->folder_menu, fl);
}

/** @brief Carry out a menu choice on the focused folder. */
void
menu_invoke(desktop_t *app, folder_t *fl, int command)
{
    const char *template_path;
    const char *template_parent;
    int seeded = 0, seed_x = 0, seed_y = 0;

    if (fl == NULL) {
        return;
    }
    {
        /* The one enablement the menus use gates EVERY route here: a
         * chord for a command the menu would show grey answers out loud
         * instead of being silently swallowed (NeXT UIG ch6). */
        tiku_trk_sc_ctx_t gc = menu_ctx(app, fl, 0u);

        /* Three commands are exempt because their handlers carry WIDER
         * semantics than the menu rule and say their own refusals:
         * Unmount acts on the volume being SHOWN when nothing is
         * selected (AW-096), and the trash/delete pair routes a selected
         * VOLUME to an unmount with its own boot-volume refusal
         * (FS-041). */
        if (command != TIKU_TRK_CMD_UNMOUNT &&
            command != TIKU_TRK_CMD_MOVE_TO_TRASH &&
            command != TIKU_TRK_CMD_DELETE &&
            !tiku_trk_sc_enabled(command, &gc)) {
            snprintf(app->status, sizeof app->status,
                     "%s is not available here",
                     tiku_trk_sc_label(command, 0u));
            desktop_damage_all(app);
            return;
        }
    }
    if (desktop_cmd_system(app, fl, command)) {
        return;
    }
    if (desktop_cmd_query(app, fl, command)) {
        return;
    }
    if (desktop_cmd_destination(app, fl, command, &template_path)) {
        return;
    }
    if (desktop_cmd_template(app, fl, command, template_path, &template_parent,
        &seeded, &seed_x, &seed_y)) {
        return;
    }
    if (tiku_trk_addons_invoke(&app->addons, command,
                               TIKU_TRK_ADDON_COMMAND_BASE,
                               desktop_invoke_addon, app)) {
        desktop_damage_all(app);
        return;
    }
    if (desktop_cmd_layout(app, fl, command)) {
        return;
    }
    if (desktop_cmd_mount(app, fl, command)) {
        return;
    }
    if (desktop_cmd_open_with(app, fl, command)) {
        return;
    }
    if (desktop_cmd_unmount(app, fl, command)) {
        return;
    }
    if (desktop_cmd_new_folder(app, fl, command, seeded, seed_x, seed_y)) {
        return;
    }
    if (tiku_trk_folder_actions_invoke(app->desk, fl, command)) {
        desktop_damage_all(app);
        return;
    }
    if (tiku_trk_folder_file_actions_invoke(&app->file_actions, fl,
                                             command)) {
        desktop_damage_all(app);
        return;
    }
    switch (command) {
    case TIKU_TRK_CMD_OPEN: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        if (p != NULL && p->model.facts.link_broken) {
            offer_broken_link(app, p->model.path);
        } else if (p != NULL && tiku_model_is_container(&p->model)) {
            (void)open_folder(app, p->model.path);
        }
        break;
    }
    case TIKU_TRK_CMD_OPEN_PARENT: {
        char parent[TIKU_PATH_MAX];
        char *slash;

        snprintf(parent, sizeof parent, "%s", fl->path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent) { parent[1] = '\0'; } else { *slash = '\0'; }
            (void)open_folder(app, parent);
        }
        break;
    }
    case TIKU_TRK_CMD_GET_INFO: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        /* The selection if there is one, else the folder itself: asking
         * about "this window" with nothing picked is asking about the
         * folder it is showing. */
        {
            int islot = tiku_trk_info_windows_open(&app->info_windows,
                (p != NULL && p->selected) ? p->model.path : fl->path);

            if (islot >= 0) {
                info_opens_with_fill(app, &app->info_windows.model[islot]);
            }
        }
        break;
    }
    case TIKU_TRK_CMD_EDIT_QUERY: {
        /* Re-open the panel ON the query rather than running it again:
         * what the user wants from a result set is usually one more
         * condition, not the same search a second time (Q-045). */
        int whole = (fl->qname[0] != '\0')
                        ? tiku_trk_find_load_named(find_model(app), fl->qname)
                        : -1;

        if (whole < 0) {
            snprintf(app->status, sizeof app->status,
                     "that query cannot be opened");
            break;
        }
        if (!whole) {
            /* Said rather than swallowed: a scope this panel cannot offer
             * means the restored search is not the saved one. */
            snprintf(app->status, sizeof app->status,
                     "one of its places is no longer here");
        }
        open_panel(app);
        break;
    }
    case TIKU_TRK_CMD_MOVE_TO:
    case TIKU_TRK_CMD_COPY_TO:
        /* Both ask the same question -- WHERE -- so both open the same
         * picker and it is the command that decides what happens with the
         * answer. */
        fpanel_open(app, fl, command);
        break;
    case TIKU_TRK_CMD_SELECT_MATCHING:
        /* The same window the keyboard opens: an item that named it and
         * did nothing was the menu disagreeing with the shortcut. */
        tiku_trk_select_window_open(&app->select_window, fl, 520, 300);
        break;
    case TIKU_TRK_CMD_CLOSE:
        close_window(app, fl->win);
        break;
    case TIKU_TRK_CMD_CLOSE_ALL:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE: {
        int i;

        for (i = 0; i < app->folder_count; i++) {
            if (app->folders[i].win != NULL) {
                close_window(app, app->folders[i].win);
            }
        }
        break;
    }
    default:
        if (command >= TIKU_TRK_CMD_COL_ATTR_BASE &&
            command < TIKU_TRK_CMD_OPEN_WITH_BASE) {
            (void)tiku_trk_poseview_toggle_column(fl->view,
                      command - TIKU_TRK_CMD_COL_ATTR_BASE);
            break;
        }
        if (command >= TIKU_TRK_CMD_OPEN_WITH_BASE) {
            open_with_pick(app, fl, command - TIKU_TRK_CMD_OPEN_WITH_BASE);
            break;
        }
        /* Everything else is built and enabled correctly but has no
         * implementation behind it yet; it must do nothing rather than
         * something approximate. */
        break;
    }
    desktop_damage_all(app);
}

/**
 * @brief A button held still for the click interval opens the context menu, on
 * the bar or on a pose (PVS-017).
 */
void
desktop_press_hold(desktop_t *app)
{
    /* PVS-017: a button held still for the click interval opens the
     * context menu without waiting for the release.  The Deskbar's own
     * long press is its responder's tick. */
    if (app->press.down && !app->press.menu && app->popup == NULL &&
        app->press.folder != NULL &&
        tiku_trk_now() - app->press.at > TIKU_TRK_CLICK_SPEED_US) {
        folder_t *pfl = app->press.folder;
        tiku_trk_pose_t *p = (app->press.row >= 0)
            ? tiku_trk_poseview_at(pfl->view, app->press.row) : NULL;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_cancel_edit_wait(pfl->view);
        commit_rename(app, 1);            /* CW-029: mouse-opened */
        (void)tiku_trk_poseview_context_click(pfl->view, app->press.row);
        c = menu_ctx(app, pfl, 0u);
        build_open_with(app, pfl);
        build_per_type(app, pfl);
        app->popup = tiku_trk_cwmenu_context(&app->menus,
                        (p != NULL) ? &p->model : NULL, &c);
        app->popup_sub = NULL;
        app->popup_frame = tiku_menu_place(app->popup, app->press.x,
            app->press.y, (tiku_rect_t){ 0, 0, app->surf->w,
                                             app->surf->h });
        app->press.menu = 1;
        app->press.down = 0;
        desktop_damage_all(app);
    }
}

/**
 * @brief the Open With window.  @return 1 when this was the command
 */
int
desktop_cmd_open_with(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_OPEN_WITH_WINDOW) {
        char wpaths[TIKU_TRK_OW_PATHS][TIKU_PATH_MAX];
        const char *wsel[TIKU_TRK_OW_PATHS];
        const char *wtypes[TIKU_TRK_APP_TYPES_MAX];
        int wn, wt = 0, wi, wk, wcount;

        wn = tiku_trk_folder_file_actions_selection(fl, wpaths, wsel,
                                                    TIKU_TRK_OW_PATHS);
        if (wn > 0) {
            wcount = tiku_trk_poseview_count(fl->view);
            for (wi = 0; wi < wcount && wt < TIKU_TRK_APP_TYPES_MAX; wi++) {
                tiku_trk_pose_t *wp = tiku_trk_poseview_at(fl->view, wi);

                if (wp == NULL || !wp->selected) {
                    continue;
                }
                for (wk = 0; wk < wt; wk++) {
                    if (strcmp(wtypes[wk], wp->model.type) == 0) {
                        break;
                    }
                }
                if (wk == wt) {
                    wtypes[wt++] = wp->model.type;
                }
            }
            tiku_trk_open_with_window_open(&app->open_with_window,
                &app->apps, wtypes, wt, wsel, wn);
        }
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief copying and pasting a window layout.  @return 1 when this was the
 * command
 */
int
desktop_cmd_layout(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_COPY_LAYOUT) {
        (void)tiku_trk_poseview_copy_layout(fl->view);
        desktop_damage_all(app);
        return 1;
    }
    if (command == TIKU_TRK_CMD_PASTE_LAYOUT) {
        if (tiku_trk_poseview_paste_layout(fl->view) > 0) {
            (void)tiku_trk_poseview_save_columns(fl->view);
            snprintf(app->status, sizeof app->status, "pasted layout");
        }
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief creating and editing a query.  @return 1 when this was the command
 */
int
desktop_cmd_query(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_CREATE_QUERY ||
        command == TIKU_TRK_CMD_CREATE_QUERY_TPL) {
        /* The Find icon's drop chose (Q-034): the current search becomes
         * a record in the folder it was dropped on, under the first free
         * "New Query" name, as a query or as a template. */
        char dest[TIKU_PATH_MAX];
        tiku_model_t probe;
        int k;

        if (app->qdrop_dest[0] == '\0') {
            return 1;
        }
        tiku_trk_find_build(&app->find_window.model, &app->pending_query);
        app->pending_query.kind =
            (command == TIKU_TRK_CMD_CREATE_QUERY_TPL)
                ? TIKU_QREC_TEMPLATE : TIKU_QREC_QUERY;
        for (k = 0; k < 100; k++) {
            if (k == 0) {
                snprintf(dest, sizeof dest, "%s/New Query",
                         app->qdrop_dest);
            } else {
                snprintf(dest, sizeof dest, "%s/New Query %d",
                         app->qdrop_dest, k + 1);
            }
            if (app->backend->ops->stat(app->backend, dest, &probe) != 0) {
                break;
            }
        }
        fpanel_commit(app, fl, TIKU_TRK_CMD_SAVE_QUERY, dest);
        app->qdrop_dest[0] = '\0';
        return 1;
    }
    return 0;
}

/**
 * @brief About and Shut down.  @return 1 when this was the command
 */
int
desktop_cmd_system(desktop_t *app, folder_t *fl, int command)
{
    (void)fl;
    if (command == TIKU_TRK_CMD_ABOUT) {
        (void)embed_open_or_raise(app, &tiku_trk_about_app);
        return 1;
    }
    if (command == TIKU_TRK_CMD_SHUTDOWN) {
        /* The system menu's corner ritual, by the same question the
         * Deskbar asks (DB-006). */
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_SHUTDOWN,
                            tiku_str("alert.msg.shut_down"),
                            tiku_str("alert.btn.shut_down"));
        return 1;
    }
    return 0;
}
