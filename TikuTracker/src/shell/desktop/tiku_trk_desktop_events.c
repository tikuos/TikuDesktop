/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_events.c - what the desktop does with an event.
 *
 * The four cases that carry the weight of the shell's loop: a button
 * down, the pointer moving, a button up, a key down -- each handed the
 * event and the state the loop keeps between events -- and the turn
 * around them: what is done before the events are read and after.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"
#include "tiku_work.h"


/** @brief Committed text bypasses menus and keyboard command dispatch. */
void desktop_text_input(desktop_t *app, const tiku_event_t *ev)
{
    tiku_window_t *w = tiku_workspace_focused(app->desk);
    struct embed_win *ew = embed_win_of_window(app, w);
    if (alert_model(app)->open || app->popup) {
        tiku_text_input_reset(&app->legacy_commit);
        return;
    }
    if (ew != NULL) {
        if (embed_deliver(ew, ev)) embed_stop(ew->slot);
    } else if (tiku_remote_owner(&app->remote, w) != NULL) {
        tiku_remote_event(&app->remote, w, ev);
    } else if (w && w == desktop_deviceprefs_handle(app)) {
        (void)tiku_window_send(w, ev);
    } else {
        if (ev->button & TIKU_TEXT_BEGIN) app->legacy_target = w;
        if (app->legacy_target != w) tiku_text_input_reset(&app->legacy_commit);
        int state = tiku_text_input_event(&app->legacy_commit, ev);
        int result = state == TIKU_TEXT_REJECTED ? -1 : 0;
        if (state == TIKU_TEXT_COMPLETE) {
            const char *text = app->legacy_commit.text;
            result = tiku_trk_info_windows_text(&app->info_windows, text);
            if (!result)
                result = tiku_trk_panel_window_text(&app->file_panel, text);
            if (!result)
                result = tiku_trk_find_window_text(&app->find_window, text);
            if (!result)
                result = tiku_trk_select_window_text(&app->select_window, text);
            tiku_trk_described_t *panel = panel_of_window(app, w);
            if (!result && panel && tiku_form_editing(&panel->form) >= 0)
                result = tiku_form_text(&panel->form, text) ? 1 : -1;
            if (!result && app->finding) {
                result = tiku_text_input_append(app->find_buf,
                    sizeof app->find_buf, text) ? 1 : -1;
                app->find_len = (int)strlen(app->find_buf);
            }
            folder_t *fl = folder_of(app, w);
            if (!result && (!w || fl))
                result = tiku_trk_folder_edit_text(&app->folder_edit, fl, text);
        }
        if (result < 0)
            snprintf(app->status, sizeof app->status,
                     "Text input is invalid or does not fit this field.");
    }
    app->dirty = 1;
}

/**
 * @brief A button went down: what is under the pointer decides everything that
 * follows.
 */
void
desktop_pointer_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
{
    /* The surfaces that answer for themselves, top down: the picker, the
     * alert, a menu that is showing, the Deskbar. */
    tiku_rect_t taken;

    if (tiku_trk_responders_deliver(&app->responders, ev, &taken)) {
        desktop_take_damage(app, taken);
        return;
    }
    desktop_take_damage(app, taken);
}
}

/**
 * @brief The pointer says what is under it: a window's corner grip
 *        offers the diagonal resize, anything else the arrow.  Only
 *        while nothing is being tracked, and only for the front
 *        window -- a click on another is spent activating it.
 */
static void
pointer_says(desktop_t *app, int x, int y)
{
    tiku_window_t *under = NULL;
    int want = TIKU_CURSOR_ARROW;

    if (app->host == NULL) {
        return;
    }
    if (!tiku_trk_drag_active(&app->drag) && app->popup == NULL &&
        tiku_workspace_hit(app->desk, x, y, &under) == TIKU_HIT_RESIZE &&
        under != NULL && under == tiku_workspace_focused(app->desk) &&
        under->resizable) {
        want = TIKU_CURSOR_RESIZE_DIAG;
    }
    if (want != app->cursor_kind) {
        app->cursor_kind = want;
        tiku_host_set_cursor(app->host, want);
    }
}

/**
 * @brief The pointer moved: drags, bands, hovers and the tracking a press
 * began.
 */
void
desktop_pointer_move(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
int was_x = app->ptr_x, was_y = app->ptr_y;
app->ptr_x = ev->x;
app->ptr_y = ev->y;
pointer_says(app, ev->x, ev->y);
{
    /* The surfaces that answer for themselves, top down: the picker, the
     * alert, a menu that is showing, the Deskbar. */
    tiku_rect_t taken;

    if (tiku_trk_responders_deliver(&app->responders, ev, &taken)) {
        desktop_take_damage(app, taken);
        return;
    }
    desktop_take_damage(app, taken);
}
if (app->qdrag_on) {
    /* The icon in flight: where it was and where it is, nothing else. */
    desktop_damage(app, tiku_rect_union(
        (tiku_rect_t){ was_x - 16, was_y - 16, 32, 32 },
        (tiku_rect_t){ ev->x - 16, ev->y - 16, 32, 32 }));
}
}

/** @brief A button came up: drops land, clicks resolve, tracking lets go. */
void
desktop_pointer_up(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
{
    /* The surfaces that answer for themselves, top down: the picker, the
     * alert, a menu that is showing, the Deskbar. */
    tiku_rect_t taken;

    if (tiku_trk_responders_deliver(&app->responders, ev, &taken)) {
        desktop_take_damage(app, taken);
        return;
    }
    desktop_take_damage(app, taken);
}
}

/**
 * @brief A key went down: shortcuts, navigation, typing into whatever holds the
 * keyboard.
 */
void
desktop_key_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
{
    /* The surfaces that answer for themselves, top down: the picker, the
     * alert, a menu that is showing, the Deskbar. */
    tiku_rect_t taken;

    if (tiku_trk_responders_deliver(&app->responders, ev, &taken)) {
        desktop_take_damage(app, taken);
        return;
    }
    desktop_take_damage(app, taken);
}
    /* The down edge alone: this function is reached on both, and its own
     * key-up guard is further down, so an unguarded F1 opened the page
     * twice and left the reader a step of back history to nowhere. */
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_F1 &&
        !app->popup) {
        const char *topic = desktop_help_page_of(app,
            tiku_workspace_focused(app->desk));
        (void)desktop_help_open(app,
            topic ? topic : "index.html");
        app->dirty = 1; return;
    }
if (ev->type == TIKU_EVENT_KEY_UP) {
    return;
}
{
unsigned key = ev->key;
tiku_window_t *w = tiku_workspace_focused(app->desk);
folder_t *fl = folder_of(app, w);
unsigned mods = 0;

if (ev->modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
if (ev->modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
/* Get Info owns its rename editor and window shortcuts. */
if (tiku_trk_info_windows_key(&app->info_windows, key,
                              ev->modifiers,
                              key_char(ev))) {
    return;
}
if (panel_key(app, key, key_char(ev))) {
    return;
}
if (tiku_trk_panel_window_key(&app->file_panel, key,
                              ev->modifiers,
                              key_char(ev))) {
    if (tiku_trk_panel_window_refused(&app->file_panel)) {
        snprintf(app->status, sizeof app->status,
                 "The clipboard does not fit this name.");
        app->dirty = 1;
    }
    return;
}
if (tiku_workspace_focused(app->desk) ==
        tiku_trk_find_window_handle(&app->find_window) &&
    app->find_window.model.focus ==
        TIKU_TRK_FIND_FOCUS_NAME &&
    key == ' ' && (ev->modifiers & TIKU_MOD_CMD)) {
    /* Cmd-Space completes the name against the first
     * scoped place, or home when the search is everywhere
     * (NeXT UIG ch3). */
    tiku_trk_find_t *fm = &app->find_window.model;
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
    if (tiku_trk_panel_complete(app->backend, dir,
            fm->name_text, full, sizeof full) > 0) {
        snprintf(fm->name_text, sizeof fm->name_text,
                 "%s", full);
    }
    app->dirty = 1;
    return;
}
{
    tiku_window_t *rfw =
        tiku_workspace_focused(app->desk);

    if (embed_win_of_window(app, rfw) != NULL) {
        struct embed_win *ew =
            embed_win_of_window(app, rfw);
        tiku_event_t rev = *ev;

        /* Closing a window is the RUNTIME's gesture, not
         * a key the application may swallow: rule 6 gives
         * the runtime the lifecycle, and an application
         * that could eat Cmd-W would be unclosable from
         * the keyboard.  It closes THIS window, which is
         * the whole window when there is only one. */
        if (key == 'w' &&
            (ev->modifiers & TIKU_MOD_CMD)) {
            embed_drop_window(ew, 1);
            app->dirty = 1;
            return;
        }
        if (embed_deliver(ew, &rev)) {
            embed_stop(ew->slot);
        }
        app->dirty = 1;
        return;
    }
    if (tiku_remote_owner(&app->remote, rfw) != NULL) {
        tiku_event_t rev = *ev;

        tiku_remote_event(&app->remote, rfw, &rev);
        app->dirty = 1;
        return;
    }
}
if (tiku_trk_find_window_key(&app->find_window, key,
                             key_char(ev))) {
    return;
}
if (app->finding) {
    if (key == TIKU_KEY_RETURN) {
        app->finding = 0;
        run_find(app, fl);
    } else if (key == TIKU_KEY_ESCAPE) {
        app->finding = 0;
        app->status[0] = '\0';
        app->dirty = 1;
    } else if (key == TIKU_KEY_BACKSPACE) {
        if (app->find_len > 0) {
            tiku_text_input_backspace(app->find_buf, sizeof app->find_buf);
            app->find_len = (int)strlen(app->find_buf);
        }
        app->dirty = 1;
    } else {
        char typed = key_char(ev);

        if (typed >= 0x20 && typed < 0x7F &&
            app->find_len < (int)sizeof app->find_buf - 1) {
            app->find_buf[app->find_len++] = typed;
            app->find_buf[app->find_len] = '\0';
            app->dirty = 1;
        }
    }
    return;
}
if (tiku_trk_select_window_key(&app->select_window, key,
                               ev->modifiers,
                               key_char(ev))) {
    return;
}
if (tiku_trk_open_with_window_key(&app->open_with_window,
                                  key, ev->modifiers)) {
    app->dirty = 1;
    return;
}
if (tiku_trk_folder_edit_key(&app->folder_edit, fl, key,
        ev->modifiers, key_char(ev))) {
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
    tiku_workspace_focused(app->desk) ==
        tiku_trk_prefs_window_handle(&app->prefs_window)) {
    tiku_trk_prefs_window_close(&app->prefs_window);
    app->dirty = 1;
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
    tiku_workspace_focused(app->desk) ==
        tiku_trk_deskbar_prefs_window_handle(
            &app->deskbar_prefs_window)) {
    /* Deskbar preferences close; the desktop stays: quit
     * belongs to the desktop, not to a settings window
     * (DB-053). */
    tiku_trk_deskbar_prefs_window_close(
        &app->deskbar_prefs_window);
    app->dirty = 1;
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
    desktop_startup_handle(app) != NULL &&
    tiku_workspace_focused(app->desk) == desktop_startup_handle(app)) {
    /* Startup preferences close, and the draft with them; the desktop
     * stays, as it does for the other settings windows (DB-053). */
    desktop_startup_close(app);
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
    desktop_forcequit_handle(app) != NULL &&
    tiku_workspace_focused(app->desk) == desktop_forcequit_handle(app)) {
    desktop_forcequit_close(app);
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
    desktop_associations_handle(app) != NULL &&
    tiku_workspace_focused(app->desk) == desktop_associations_handle(app)) {
    desktop_associations_close(app);
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
    desktop_deviceprefs_handle(app) != NULL &&
    tiku_workspace_focused(app->desk) == desktop_deviceprefs_handle(app)) {
    desktop_deviceprefs_close(app);
    return;
}
if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD)) {
    begin_shutdown(app);
    return;
}
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
        app->dirty = 1;
        return;
    }
}
if (key == 'f' && (ev->modifiers & TIKU_MOD_CMD)) {
    open_panel(app);
    return;
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
    return;
}
if (key == 'a' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    tiku_trk_poseview_select_all(fl->view);
    app->dirty = 1;
    return;
}
if (key == 'u' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    /* Cmd+U, which the volume menus advertise (CW-034). */
    menu_invoke(app, fl, TIKU_TRK_CMD_UNMOUNT);
    app->dirty = 1;
    return;
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
        app->dirty = 1;
        return;
    }
    if (tiku_trk_clip_count(&app->clip) > 0) {
        /* Escape aborts a pending cut before anything
         * else it means. */
        tiku_trk_clip_clear(&app->clip);
        app->dirty = 1;
        return;
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
    app->dirty = 1;
    return;
}
if (key == 'g' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    /* Cmd+G, which the menus now advertise (CW-033). */
    menu_invoke(app, fl, TIKU_TRK_CMD_EDIT_QUERY);
    app->dirty = 1;
    return;
}
if (key == 'i' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    /* The chord the menus have always advertised; until
     * now nothing was listening for it (AW-065). */
    menu_invoke(app, fl, TIKU_TRK_CMD_GET_INFO);
    app->dirty = 1;
    return;
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
    app->dirty = 1;
    return;
}
if (key == 'e' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    begin_rename(app, fl,
        tiku_trk_poseview_active(fl->view) != NULL
            ? tiku_trk_poseview_active(fl->view)->index : -1);
    app->dirty = 1;
    return;
}
if (key == 'd' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    menu_invoke(app, fl, TIKU_TRK_CMD_DUPLICATE);
    app->dirty = 1;
    return;
}
if (key == 'k' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    menu_invoke(app, fl,
        (ev->modifiers & TIKU_MOD_SHIFT)
            ? TIKU_TRK_CMD_CLEANUP_ALL
            : TIKU_TRK_CMD_CLEANUP);
    app->dirty = 1;
    return;
}
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
    app->dirty = 1;
    return;
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
    app->dirty = 1;
    return;
}
if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
    tiku_trk_sc_command_for_key(key, ev->modifiers) != 0) {
    /* The rest of the standard set, resolved from the one
     * table the menus draw their shortcuts from -- a
     * chord the menu advertises must answer (PVK-042). */
    menu_invoke(app, fl,
        tiku_trk_sc_command_for_key(key, ev->modifiers));
    app->dirty = 1;
    return;
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
    app->dirty = 1;
    return;
}
if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
    (key == '+' || key == '=' || key == '-')) {
    (void)tiku_trk_poseview_step_icon_size(fl->view,
        (key == '-') ? 0 : 1);
    tiku_trk_poseview_cleanup(fl->view,
                              tiku_trk_folder_window_content(fl).w);
    app->dirty = 1;
    return;
}
{
    char printable[2];

    if (tiku_workspace_focused(app->desk) ==
            tiku_trk_deskbar_prefs_window_handle(
                &app->deskbar_prefs_window) &&
        tiku_trk_deskbar_prefs_window_key(
            &app->deskbar_prefs_window,
            key_name(ev, printable))) {
        app->dirty = 1;
        return;
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
    app->dirty = 1;
    return;
}
if (key == TIKU_KEY_TAB && (ev->modifiers & TIKU_MOD_CMD)) {
    tiku_trk_switcher_window_open(&app->switcher_window,
        (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0);
    app->dirty = 1;
    return;
}
if (key == 'w' && (ev->modifiers & TIKU_MOD_CMD)) {
    close_window(app, w);
    app->dirty = 1;
    return;
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
    return;
}
if (key == ',' && (ev->modifiers & TIKU_MOD_CMD)) {
    /* The settings are the application's, not a window's,
     * so this works with no folder focused. */
    tiku_trk_prefs_window_open(&app->prefs_window);
    app->dirty = 1;
    return;
}
if (key == 'n' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    menu_invoke(app, fl, TIKU_TRK_CMD_NEW_FOLDER);
    app->dirty = 1;
    return;
}
if (key == 's' && (ev->modifiers & TIKU_MOD_CMD) &&
    fl != NULL) {
    /* PVK-046: invert -- the only way to say "everything
     * except these" without clicking the rest. */
    tiku_trk_poseview_invert_selection(fl->view);
    app->dirty = 1;
    return;
}
if (key == TIKU_KEY_TAB && fl != NULL) {
    /* Tab is a name-order walk over poses.  While filtering
     * is active it is deliberately swallowed. */
    if (!tiku_trk_poseview_is_filtering(fl->view)) {
        (void)tiku_trk_poseview_tab_next(fl->view,
            (ev->modifiers & TIKU_MOD_SHIFT) != 0u);
    }
    app->dirty = 1;
    return;
}
if (fl != NULL && key == TIKU_KEY_DOWN &&
    (ev->modifiers & TIKU_MOD_CMD) &&
    (ev->modifiers & TIKU_MOD_OPTION)) {
    tiku_trk_pose_t *p =
        tiku_trk_poseview_active(fl->view);

    if (p != NULL &&
        tiku_model_is_container(&p->model)) {
        tiku_window_t *from = fl->win;

        if (open_folder(app, p->model.path) != NULL) {
            close_window(app, from);
        }
    }
    app->dirty = 1;
    return;
}
if (fl != NULL && key == TIKU_KEY_DOWN &&
    (ev->modifiers & TIKU_MOD_CMD)) {
    tiku_trk_pose_t *p =
        tiku_trk_poseview_active(fl->view);

    if (p != NULL &&
        tiku_model_is_container(&p->model)) {
        (void)open_folder(app, p->model.path);
    } else if (p != NULL) {
        begin_edit(app, fl);
    }
    app->dirty = 1;
    return;
}
if (fl != NULL && key == TIKU_KEY_UP &&
    (ev->modifiers & TIKU_MOD_CMD)) {
    /* PVK-040/CW-051: Cmd+Up opens the parent; Option
     * additionally takes the spatial window that was left. */
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
            open_folder(app, parent) != NULL &&
            (ev->modifiers & TIKU_MOD_OPTION)) {
            close_window(app, from);
        }
    }
    app->dirty = 1;
    return;
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
            int chase = (ev->modifiers & TIKU_MOD_OPTION) != 0;
            tiku_window_t *from = fl->win;

            if (open_folder(app, p->model.path) != NULL &&
                chase) {
                close_window(app, from);
            }
        } else if (p != NULL) {
            /* Opening a leaf means editing its value: there
             * is no other window for it to open. */
            begin_edit(app, fl);
        }
    } else if (key == TIKU_KEY_BACKSPACE) {
        char parent[TIKU_PATH_MAX];
        char *slash;
        snprintf(parent, sizeof parent, "%s", fl->path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent) { parent[1] = '\0'; }
            else { *slash = '\0'; }
            (void)open_folder(app, parent);
        }
    }
    app->dirty = 1;
}
return;
}
}

/*
 * How long a scripted turn may spend on a child that has not connected
 * yet.  It can be generous because it is only ever reached in the gap
 * between starting a program and that program saying hello, and it
 * ends the moment it does: a turn pays this in full only when the
 * child is still loading, and nothing at all once it is up.
 */
#define SCRIPT_CHILD_SLICE_US 20000

/**
 * @brief What a turn does before it reads events: the settings file, the
 * script transport, the remote sessions and what travels with them.
 */
void
desktop_turn_begin(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    /*
     * A scripted wait is counted in TURNS, and turns are this process's
     * own.  Everything a wait was written for -- a stepped copy, an
     * armed rename -- is work done a slice of per turn, so counting
     * them is exactly right for it.  A child that was started and has
     * not drawn yet is not that: a fork, an exec, a library loaded are
     * work another process does, and no number of turns here advances
     * any of it.  Under a script the loop does not sleep, so a wait of
     * a dozen turns can pass in less time than the fork takes and the
     * window is simply not there when the next line asks for it.
     * Whether it was depended on how busy the machine happened to be,
     * which is why this looked like four flaky tests for so long.
     *
     * So when a script is waiting and something this desktop started is
     * still coming up, the turn is spent ON that child instead of past
     * it.  "Still coming up" is the test, not "still running": a child
     * that has connected is one more session, so a desktop with as many
     * sessions as children has nothing left to wait for and goes back
     * to counting turns like everything else.  Deliberately CONNECTED
     * and not "has shown a window", though a script asks about the
     * window: a session that never opens one would leave this waiting
     * on every turn for the rest of the run, and a fixed cost on every
     * wait is a worse thing to own than a rare late window.  Nothing is
     * read here -- the pump below takes what arrived, as it always
     * did.
     */
    if (lp != NULL && lp->sc != NULL && tiku_trk_script_waiting(lp->sc) &&
        tiku_trk_launch_running() > tiku_remote_sessions(&app->remote)) {
        (void)tiku_remote_wait(&app->remote, SCRIPT_CHILD_SLICE_US);
    }
    desktop_report_poll(app);
    /* What the workers finished lands before events are taken, so the
     * screen a click meets already shows what arrived. */
    (void)tiku_work_collect();
    {   /* The settings FILE is a doorway too: a change written by
         * anything -- another shell, a script, an editor -- lands in
         * the live model, the reload the browser shell already does
         * (TS-052, TS-047). */
        static time_t settings_mtime;
        static off_t settings_size;
        struct stat sst;

        if (app->prefs_window.path[0] != '\0' &&
            stat(app->prefs_window.path, &sst) == 0 &&
            (sst.st_mtime != settings_mtime ||
             sst.st_size != settings_size)) {
            settings_mtime = sst.st_mtime;
            settings_size = sst.st_size;
            (void)tiku_trk_prefs_load(prefs_model(app),
                                      app->prefs_window.path);
            tiku_trk_soundprefs_apply(prefs_model(app));
            app->dirty = 1;
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
                script_command_file(app, cpath);
            }
        }
    }
    if (tiku_remote_poll(&app->remote)) {
        app->dirty = 1;
    }
    if (app->conduct_on) {
        (void)tiku_conduct_poll(&app->conduct);
    }
    remote_reconcile(app);
    remote_hear(app);
    clip_mirror(app);
    /* The boards a session may choose from, on the same beat and for the
     * same reason: warm before a menu asks for them. */
    desktop_board_names_tell(app);
    pick_sweep(app);
    remote_sweep(app);
    sync_folder_preferences(app);
    sync_addons(app, 0);
}

/**
 * @brief What a turn does after the events: timers, hovers, tracking, the
 * hints and thumbnails, the -shot frame, the frame itself, the deadlines,
 * and the nap until the next task is due.
 */
void
desktop_turn_end(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    tiku_window_t *focused = tiku_workspace_focused(app->desk);
    if (app->input_focus != focused || alert_model(app)->open) {
        tiku_host_cancel_composition(app->host);
        tiku_text_input_reset(&app->legacy_commit);
        app->input_focus = focused;
    }
    tiku_event_t deactivate = {.type = TIKU_EVENT_DEACTIVATED};
    if (app->prefs_track &&
        (app->prefs_track != focused || alert_model(app)->open)) {
        (void)tiku_window_send(app->prefs_track, &deactivate);
        app->prefs_track = NULL;
    }
    if (desktop_deviceprefs_popup(app) &&
        (focused != desktop_deviceprefs_handle(app) ||
         alert_model(app)->open)) {
        (void)tiku_window_send(desktop_deviceprefs_handle(app), &deactivate);
    }
    desktop_spring_tick(app);
    {
        tiku_rect_t ticked;

        (void)tiku_trk_responders_tick(&app->responders, tiku_trk_now(),
                                       &ticked);
        desktop_take_damage(app, ticked);
    }
    desktop_idle_work(app);
    desktop_press_hold(app);
    desktop_rename_pulse(app);
    desktop_focus_paint(app);

    desktop_device_pump(app);
    desktop_sweep(app, lp);
    desktop_notice_pump(app, lp);
    if (o->shot != NULL && lp->shot_at > 0 && tiku_trk_now() >= lp->shot_at) {
        app->dirty = 1;
    }
        if (app->shot_pending) {
        app->dirty = 1;         /* redraw without the menu, then capture */
    }
    if (app->dirty && (lp->sc ||
        tiku_trk_now()-app->last_present >= 16667)) {
        draw_all(app);
        int64_t before_present = tiku_trk_now();
        blit(app);
        app->present_us += tiku_trk_now()-before_present;
        /* Frame start, not frame end: drawing time is inside the budget. */
        app->last_present = before_present -
            app->frame_samples[(app->frame_sample_count-1) % 128];
        if (o->shot != NULL && lp->shot_at > 0 && tiku_trk_now() >= lp->shot_at) {
            (void)tiku_surface_png(app->surf, o->shot);
            printf("wrote %s\n", o->shot);
            fflush(stdout);
            lp->shot_at = 0;
        }
        desktop_screenshot_tick(app);
    }
    desktop_progress_tick(app);
    if (o->deadline > 0 && tiku_trk_now() > o->deadline) {
        app->running = 0;
    }
    if (app->closing &&
        tiku_trk_splash_closed(&app->splash, tiku_trk_now())) {
        /* The black has arrived and held.  What is left is the
         * putting away, and it is named on the screen below. */
        app->running = 0;
    }
    /* A script still lets the clock run, just faster: the notice
     * poll, the armed rename and the barber pole are all timed, and a
     * loop that spun without time passing would test none of them. */
    if (app->loop != NULL) {
        /* A task that changed something on screen dirties the surface
         * itself; running is not, by itself, a repaint. */
        (void)tiku_trk_loop_pulse(app->loop);
        if (app->sbar_fl != NULL && app->sbar_fl->view != NULL) {
            /* A held arrow keeps stepping (NeXT UIG ch3). */
            int rep = tiku_scroll_arrow_repeat(&app->sbar_fl->sbar);

            if (rep != 0) {
                tiku_trk_poseview_scroll_to(app->sbar_fl->view,
                    tiku_trk_poseview_scroll(app->sbar_fl->view) +
                        rep * tiku_scroll_line(&app->sbar_fl->sbar));
                app->dirty = 1;
            }
        }
    }
    {
        /* Sleep until the next task is due, or one heartbeat,
         * whichever is sooner (AW-086). */
        int64_t nap = (lp->sc != NULL) ? 20000 : 10000;
        int64_t due = (app->loop != NULL)
                          ? tiku_trk_loop_next_due(app->loop) : -1;

        if (due >= 0 && due < nap) {
            nap = (due < 1000) ? 1000 : due;
        }
        if (lp->sc) usleep((useconds_t)nap);
        else {
            if (app->dirty) {
                int64_t frame_due = app->last_present+16667-tiku_trk_now();
                if (frame_due < nap) nap = frame_due;
            }
            if (app->cq_head == app->cq_tail)
                tiku_host_wait(app->host, nap);
        }
    }
}
