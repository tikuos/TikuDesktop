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

/**
 * @brief A button went down: what is under the pointer decides everything that
 * follows.
 */
void
desktop_pointer_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
if (ev->button == 1u && ev->modifiers == 0u) {
    /* The resize strip: four pixels along the bar's
     * inner edge, and the drag sets the short axis by
     * hand (DB-050). */
    tiku_rect_t bf =
        tiku_trk_deskbar_frame(app->deskbar);
    tiku_trk_db_dock_t dk =
        tiku_trk_deskbar_dock(app->deskbar);
    int inner_ok = 0;

    if (dk == TIKU_TRK_DB_RIGHT_EXPANDED &&
        ev->x >= bf.x && ev->x < bf.x + 4) {
        inner_ok = 1;
    }
    if (dk == TIKU_TRK_DB_LEFT_EXPANDED &&
        ev->x >= bf.x + bf.w - 4 && ev->x < bf.x + bf.w) {
        inner_ok = 1;
    }
    if (inner_ok && ev->y >= bf.y && ev->y < bf.y + bf.h) {
        app->bar_resize = 1;
        app->bar_resize_x = ev->x;
        app->bar_resize_w = tiku_trk_deskbar_width(
            app->deskbar);
        return;
    }
}
if (ev->button == 1u &&
    (ev->modifiers & TIKU_MOD_CMD)) {
    /* Before anything else -- but ARMED, not decided: a
     * CMD press that then moves re-docks the bar
     * (DB-049); one that lets go where it landed is a
     * chord the rows answer (DB-019/021). */
    tiku_rect_t bf =
        tiku_trk_deskbar_frame(app->deskbar);

    if (ev->x >= bf.x &&
        ev->x < bf.x + bf.w &&
        ev->y >= bf.y &&
        ev->y < bf.y + bf.h) {
        app->dock_armed = 1;
        app->dock_x = ev->x;
        app->dock_y = ev->y;
        app->dock_mods = ev->modifiers;
        app->dock_button = ev->button;
        return;
    }
}
{   /* An info window takes its own clicks first. */
    tiku_window_t *iw = tiku_workspace_focused(app->desk);
    int islot = tiku_trk_info_windows_slot(&app->info_windows,
                                           iw);
    /* An open menu owns the next click wherever it lands,
     * including over this window: choosing from the menu it
     * just raised is not another click on the icon. */
    if (islot >= 0 && !alert_model(app)->open &&
        app->popup == NULL) {
        int which = -1;
        tiku_trk_info_t *info = tiku_trk_info_windows_model(
            &app->info_windows, islot);
        tiku_trk_info_hit_t ih = tiku_trk_info_windows_hit(
            &app->info_windows, islot, ev->x, ev->y, &which);

        if (ih == TIKU_TRK_INFO_HIT_TAB && which >= 0) {
            /* The tab strip owns which is current; the
             * window re-reads the Attributes tab as it
             * becomes current. */
            tiku_trk_info_windows_select_tab(
                &app->info_windows, islot, which);
            app->dirty = 1;
            return;
        }
        if (ih == TIKU_TRK_INFO_HIT_NAME) {
            tiku_trk_info_windows_begin_rename(
                &app->info_windows, islot);
            return;
        }
        if (ih == TIKU_TRK_INFO_HIT_PERMISSION && which >= 0) {
            char perr[160];

            if (tiku_trk_info_perm_toggle(info,
                    app->backend, which, perr,
                    sizeof perr) != 0) {
                tiku_sound_play(TIKU_SOUND_BEEP);
                tiku_alert_open(alert_model(app),
                    TIKU_ALERT_WARN,
                    TIKU_TRK_ALERT_TAG_ERROR, perr,
                    "Cancel");
            }
            app->dirty = 1;
            return;
        }
        if (ih == TIKU_TRK_INFO_HIT_ICON) {
            if (ev->button == 3u) {
                /* The icon stands for the thing the window
                 * is about, so it answers a secondary click
                 * with that thing's own menu -- built from
                 * the MODEL, and carried out against its
                 * path because there is no selection here
                 * to read (AW-076). */
                tiku_model_t m;
                tiku_trk_sc_ctx_t c =
                    menu_ctx(app, desktop_folder(app), 0u);

                if (app->backend->ops->stat(app->backend,
                        info->path, &m) != 0) {
                    return;
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
                snprintf(app->popup_path,
                         sizeof app->popup_path, "%s",
                         info->path);
                app->popup = tiku_trk_cwmenu_context(
                    &app->menus, &m, &c);
                /* A COPY: every folder draw rebuilds the
                 * shared context menu, and the icon's
                 * extra rows must survive the frame. */
                app->menus.info_ctx = *app->popup;
                app->popup = &app->menus.info_ctx;
                /* The items the source adds for THIS menu
                 * alone (AW-076): a folder recounts, a
                 * link re-points. */
                if (tiku_model_is_container(&m) &&
                    m.kind != TIKU_KIND_VOLUME &&
                    m.kind != TIKU_KIND_ROOT) {
                    (void)tiku_menu_add(app->popup,
                        "Recalculate folder size",
                        TIKU_TRK_CMD_RECALC_SIZE, 0, 0u,
                        1);
                }
                if (m.kind == TIKU_KIND_SYMLINK) {
                    (void)tiku_menu_add(app->popup,
                        "Set new link target",
                        TIKU_TRK_CMD_SET_LINK_TARGET, 0,
                        0u, 1);
                }
                app->popup_sub = NULL;
                app->popup_frame = tiku_menu_place(
                    app->popup, ev->x, ev->y,
                    (tiku_rect_t){ 0, 0, app->surf->w,
                                        app->surf->h });
                app->dirty = 1;
                return;
            }
            /* Double-clicking the icon opens the item, as
             * double-clicking its row would (AW-075). */
            if (tiku_trk_now() - app->press_at < 500000) {
                (void)open_folder(app, info->path);
                app->press_at = tiku_trk_now();
                return;
            }
            app->press_at = tiku_trk_now();
            /* And a press that moves carries the item
             * itself -- except the Trash, which can only
             * ever be opened (AW-075). */
            if (ev->button == 1u &&
                info->model.kind != TIKU_KIND_TRASH) {
                char iparent[TIKU_PATH_MAX];
                tiku_model_t im;

                if (app->backend->ops->stat(app->backend,
                        info->path, &im) == 0) {
                    parent_path(info->path, iparent,
                                sizeof iparent);
                    tiku_trk_drag_arm(&app->drag, ev->x, ev->y,
                                      ev->button, 0u, 1, iw,
                                      iparent);
                    (void)tiku_trk_drag_add_at(&app->drag,
                        info->path, im.type, ev->x, ev->y, 1);
                    tiku_trk_drag_target_reset(
                        &app->drag_target);
                }
            }
            return;
        }
        if (ih == TIKU_TRK_INFO_HIT_FIELD && which >= 0) {
            char value[TIKU_TRK_INFO_VAL_MAX];
            const char *label = "";
            tiku_trk_info_field_t fk =
                tiku_trk_info_field(info,
                    app->store, which, &label, value,
                    sizeof value);
            char dir[TIKU_PATH_MAX];
            char sel[TIKU_NAME_MAX];

            if (fk == TIKU_TRK_INFO_F_OPENS_WITH) {
                /* The pop-up as a walk: each click moves
                 * to the next supporting application and
                 * writes the preference at once
                 * (AW-071). */
                info_opens_with_next(app, info);
                app->dirty = 1;
                return;
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
                folder_t *nf = open_folder(app, dir);

                if (nf != NULL) {
                    select_named(app, nf, sel);
                }
            } else if (fk == TIKU_TRK_INFO_F_LINK) {
                if (tiku_trk_info_link_target(
                        info, app->backend, dir,
                        sizeof dir, sel, sizeof sel)) {
                    folder_t *nf = open_folder(app, dir);

                    if (nf != NULL) {
                        select_named(app, nf, sel);
                    }
                } else {
                    /* Nowhere to follow: the window offers
                     * a picker rather than an error, which
                     * is the whole of AW-074's flow. */
                    folder_t *ffl = folder_of(app,
                        tiku_workspace_focused(app->desk));

                    app->info_relink = islot;
                    fpanel_open(app, (ffl != NULL) ? ffl
                                    : &app->folders[0],
                                TIKU_TRK_CMD_GET_INFO);
                }
            }
            app->dirty = 1;
            return;
        }
    }
}
{
    tiku_window_t *dbprefs =
        tiku_trk_deskbar_prefs_window_handle(
            &app->deskbar_prefs_window);

    if (dbprefs != NULL && dbprefs->open &&
        !alert_model(app)->open) {
        tiku_event_t window_event = { 0 };

        window_event.type = TIKU_EVENT_POINTER_DOWN;
        window_event.time_us = tiku_trk_now();
        window_event.x = ev->x;
        window_event.y = ev->y;
        window_event.button = ev->button;
        if (tiku_window_send(dbprefs, &window_event)) {
            return;
        }
    }
}
{
    /* The same offer to the Desktop's own preferences:
     * a window with controls hears the pointer, and says
     * whether the click was one of its own. */
    tiku_window_t *dkprefs =
        tiku_trk_desktop_prefs_window_handle(
            &app->desktop_prefs_window);

    if (dkprefs != NULL && dkprefs->open &&
        !alert_model(app)->open) {
        tiku_event_t window_event = { 0 };

        window_event.type = TIKU_EVENT_POINTER_DOWN;
        window_event.time_us = tiku_trk_now();
        window_event.x = ev->x;
        window_event.y = ev->y;
        window_event.button = ev->button;
        if (tiku_window_send(dkprefs, &window_event)) {
            return;
        }
    }
}
{
    tiku_window_t *fonts =
        tiku_trk_fonts_window_handle(&app->fonts_window);

    if (fonts != NULL && fonts->open &&
        !alert_model(app)->open) {
        tiku_event_t window_event = { 0 };

        window_event.type = TIKU_EVENT_POINTER_DOWN;
        window_event.time_us = tiku_trk_now();
        window_event.x = ev->x;
        window_event.y = ev->y;
        window_event.button = ev->button;
        if (tiku_window_send(fonts, &window_event)) {
            return;
        }
    }
    {
        tiku_window_t *sounds =
            tiku_trk_sounds_window_handle(&app->sounds_window);
        tiku_window_t *under = NULL;

        /* Only when it is the window under the pointer: the file panel
         * it puts up lies over it, and a press on the panel's rows is
         * the panel's. */
        (void)tiku_workspace_hit(app->desk, ev->x, ev->y, &under);
        if (sounds != NULL && sounds->open && under == sounds &&
            !alert_model(app)->open) {
            tiku_event_t window_event = { 0 };

            window_event.type = TIKU_EVENT_POINTER_DOWN;
            window_event.time_us = tiku_trk_now();
            window_event.x = ev->x;
            window_event.y = ev->y;
            window_event.button = ev->button;
            if (tiku_window_send(sounds, &window_event)) {
                return;
            }
        }
    }
}
{
    tiku_window_t *prefs_window =
        tiku_trk_prefs_window_handle(&app->prefs_window);

    if (prefs_window != NULL && prefs_window->open &&
        !alert_model(app)->open) {
        tiku_event_t window_event = { 0 };

        window_event.type = TIKU_EVENT_POINTER_DOWN;
        window_event.time_us = tiku_trk_now();
        window_event.x = ev->x;
        window_event.y = ev->y;
        window_event.button = ev->button;
        if (tiku_window_send(prefs_window,
                                  &window_event)) {
            return;
        }
    }
}
if (tiku_trk_status_overlay_pointer(&app->status_overlay,
                                    ev->x, ev->y)) {
    return;
}
tiku_window_t *w = NULL;
tiku_hit_t hit;
tiku_trk_db_pick_t pick;

/* A menu that is showing eats the next click wherever it
 * lands: choosing an item, or dismissing. */
if (app->nav_on && app->popup != NULL) {
    /* A click inside a level chooses: a folder walks in,
     * and anything else is not choosable, so the click is
     * simply consumed (CW-056). */
    const tiku_font_t *nf = tiku_font_plain();
    int px2 = app->popup_frame.x + app->popup_frame.w - 4;
    int py2 = app->popup_frame.y + 4;
    int k, took = 0;

    (void)nf;
    for (k = 0; k < app->nav.depth && !took; k++) {
        tiku_rect_t r;
        int at;

        tiku_trk_nav_place(&app->nav, k, px2, py2,
                           app->surf->w, app->surf->h,
                           &r.x, &r.y, &r.w, &r.h);
        at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y,
                                  ev->x,
                                  ev->y);
        if (at >= 0) {
            /* Levels deeper than the one clicked go: the
             * walk follows the pointer rather than
             * accumulating every branch visited. */
            while (app->nav.depth > k + 1) {
                (void)tiku_trk_nav_leave(&app->nav);
            }
            app->nav.level[k].sel = at;
            if (app->nav.level[k].item[at].is_container) {
                (void)tiku_trk_nav_enter(&app->nav, at);
            }
            took = 1;
        }
        px2 = r.x + r.w - 4;
        py2 = r.y + 2 +
              ((app->nav.level[k].sel >= 0)
                   ? app->nav.level[k].sel : 0) *
              TIKU_TRK_NAV_ROW;
    }
    if (took) {
        app->dirty = 1;
        return;
    }
}
if (app->popup != NULL) {
    tiku_window_t *fw = tiku_workspace_focused(app->desk);
    folder_t *ffl = (app->bar_fl != NULL) ? app->bar_fl
                  : (app->popup_fl != NULL) ? app->popup_fl
                                           : folder_of(app, fw);
    int pick = -1;

    if (app->popup_sub != NULL) {
        pick = tiku_menu_item_at(app->popup_sub,
                                     app->popup_sub_frame,
                                     ev->x,
                                     ev->y);
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
        if (pick >= 0 && app->popup_sub->item[pick].enabled) {
            if (app->popup_path[0] != '\0') {
                (void)tiku_trk_folder_file_actions_invoke_path(
                    &app->file_actions, app->popup_path,
                    app->popup_sub->item[pick].command);
                app->dirty = 1;
            } else {
                global_or_invoke(app, ffl,
                    app->popup_sub->item[pick].command);
            }
        }
    }
    if (pick < 0) {
        pick = tiku_menu_item_at(app->popup,
                                     app->popup_frame,
                                     ev->x,
                                     ev->y);
        if (pick >= 0 && app->popup->item[pick].enabled) {
            if (app->popup->item[pick].submenu != NULL) {
                /* Opening a submenu is not a choice; the
                 * menu stays up. */
                app->popup_sub = app->popup->item[pick].submenu;
                app->popup_sub_frame =
                    tiku_menu_place_sub(app->popup, pick,
                        app->popup_frame,
                        (tiku_rect_t){ 0, 0,
                            app->surf->w, app->surf->h });
                app->dirty = 1;
                return;
            }
            if (app->popup_path[0] != '\0') {
                int pc = app->popup->item[pick].command;
                int islot = -1;
                {
                    int fi;

                    for (fi = 0;
                         fi < TIKU_TRK_INFO_WINDOW_MAX;
                         fi++) {
                        tiku_trk_info_t *im =
                            &app->info_windows.model[fi];

                        if (im->used &&
                            strcmp(im->path,
                                   app->popup_path) == 0) {
                            islot = fi;
                            break;
                        }
                    }
                }

                if (pc == TIKU_TRK_CMD_RECALC_SIZE &&
                    islot >= 0) {
                    tiku_trk_info_calc_start(
                        &app->info_windows.model[islot]);
                } else if (pc ==
                               TIKU_TRK_CMD_SET_LINK_TARGET
                           && islot >= 0) {
                    /* The same picker AW-074's broken-
                     * link flow raises. */
                    folder_t *ffl2 = folder_of(app,
                        tiku_workspace_focused(
                            app->desk));

                    app->info_relink = islot;
                    fpanel_open(app,
                        (ffl2 != NULL) ? ffl2
                                       : &app->folders[0],
                        TIKU_TRK_CMD_GET_INFO);
                } else {
                    (void)tiku_trk_folder_file_actions_invoke_path(
                        &app->file_actions, app->popup_path,
                        pc);
                }
                app->dirty = 1;
            } else {
                global_or_invoke(app, ffl,
                    app->popup->item[pick].command);
            }
        }
    }
    app->popup_path[0] = '\0';
    app->popup_fl = NULL;
    app->popup = NULL;
    app->popup_sub = NULL;
    app->bar_fl = NULL;
    app->bar_open = -1;
    app->gbar_open = -1;
    app->gbar.open = -1;
    app->menus.bar.open = -1;
    app->dirty = 1;
    return;
}

if (app->gbar_on == 1 && app->popup == NULL &&
    ev->y < app->menus.bar.height && ev->button == 1u &&
    ev->x >= app->shot_btn.x &&
    ev->x < app->shot_btn.x + app->shot_btn.w) {
    /* The camera on the bar's right: a screenshot, taken next turn so
     * the pointer's press is not frozen into it. */
    app->shot_pending = 1;
    app->shot_pressed = 1;
    app->dirty = 1;
    return;
}
if (app->gbar_on == 1 && app->popup == NULL &&
    ev->y < app->menus.bar.height && ev->button == 1u) {
    /* The strip: a press on a title opens its menu, from
     * the published description alone. */
    tiku_rect_t strip = global_bar_rect(app);
    int bi = tiku_menubar_title_at(&app->gbar, strip,
                                       ev->x, ev->y);

    if (bi >= 0) {
        commit_rename(app, 1);
        app->gbar_open = bi;
        app->gbar.open = bi;
        app->popup = app->gbar.menu[bi];
        app->popup_sub = NULL;
        app->popup_frame = tiku_menubar_panel(&app->gbar,
            strip, (tiku_rect_t){ 0, 0, app->surf->w,
                                       app->surf->h });
        app->nav_on = 0;
        app->dirty = 1;
        return;
    }
}

/* The Deskbar and its menu take precedence: it sits above
 * every window, as a corner strip must. */
if (tiku_trk_deskbar_menu_open(app->deskbar) ||
    tiku_trk_deskbar_hit(app->deskbar, ev->x,
                         ev->y)) {
    int secondary = (ev->button == 3);
    int dclicks = 1;

    /* The Deskbar's own double-click: same spot, inside
     * the interval -- and NOT refreshed when it fires,
     * so a triple click cannot re-trigger (DB-019). */
    if (ev->button == 1u &&
        tiku_trk_now() - app->db_click_at < 500000 &&
        ev->x - app->db_click_x <= 4 &&
        app->db_click_x - ev->x <= 4 &&
        ev->y - app->db_click_y <= 4 &&
        app->db_click_y - ev->y <= 4) {
        dclicks = 2;
        app->db_click_at = 0;
    } else if (ev->button == 1u) {
        app->db_click_at = tiku_trk_now();
        app->db_click_x = ev->x;
        app->db_click_y = ev->y;
    }
if (tiku_trk_deskbar_click(app->deskbar, ev->x,
                               ev->y, ev->modifiers,
                               (int)ev->button, dclicks,
                               &pick)) {
        if (ev->button == 1u && !secondary) {
            app->deskbar_press = 1;
            app->press_at = tiku_trk_now();
            app->press_x = ev->x;
            app->press_y = ev->y;
        }
        deskbar_pick_dispatch(app, &pick,
                              ev->x, ev->y);
        app->dirty = 1;
        return;
    }
}
if (ev->button == 4u || ev->button == 5u) {
    /* The wheel.  In an icon mode it moves the pixel
     * origin; in a list it steps rows, which is what it
     * has always done. */
    tiku_window_t *ww = NULL;
    folder_t *wfl;
    tiku_rect_t bf =
        tiku_trk_deskbar_frame(app->deskbar);

    if (ev->x >= bf.x &&
        ev->x < bf.x + bf.w &&
        ev->y >= bf.y &&
        ev->y < bf.y + bf.h) {
        /* Over the bar: the list scrolls rather than
         * whatever window happens to be under it
         * (DB-034). */
        int wstep = ((ev->modifiers &
                      (TIKU_MOD_SHIFT |
                       TIKU_MOD_CMD |
                       TIKU_MOD_OPTION)) != 0u)
                        ? 3 : 1;

        /* A modifier scrolls three times as fast, the
         * wheel and the arrows alike (DB-034). */
        if (tiku_trk_deskbar_scroll(app->deskbar,
                (ev->button == 4u) ? -wstep : wstep)) {
            app->dirty = 1;
        }
        return;
    }
    (void)tiku_workspace_hit(app->desk, ev->x,
                            ev->y, &ww);
    wfl = folder_of(app, ww);
    if (wfl != NULL) {
        int step = (ev->button == 4u) ? -1 : 1;

        if (tiku_trk_poseview_mode(wfl->view) !=
                TIKU_TRK_MODE_LIST) {
            int ox = 0, oy = 0;

            tiku_trk_poseview_origin(wfl->view, &ox, &oy);
            tiku_trk_poseview_set_origin(wfl->view, ox,
                oy + step * 3 * app->row_h);
        } else {
            tiku_trk_poseview_scroll_to(wfl->view,
                tiku_trk_poseview_scroll(wfl->view) +
                    step * 3);
        }
        app->dirty = 1;
    } else if (ww != NULL) {
        /* Over an application's own page: the wheel is
         * ITS to answer, so it rides in as a WHEEL event
         * in window coordinates -- embedded in this
         * process, or across the wire to the session
         * that owns it, the road a click already takes. */
        struct embed_win *ew =
            embed_win_of_window(app, ww);
        tiku_rect_t rc = tiku_window_content(ww);
        tiku_event_t we;

        memset(&we, 0, sizeof we);
        we.type = TIKU_EVENT_WHEEL;
        we.x = ev->x - rc.x;
        we.y = ev->y - rc.y;
        we.dy = (ev->button == 4u) ? -3 : 3;
        we.time_us = tiku_trk_now();
        if (ew != NULL) {
            if (embed_deliver(ew, &we)) {
                embed_stop(ew->slot);
            }
            app->dirty = 1;
        } else if (tiku_remote_owner(&app->remote, ww)
                       != NULL) {
            tiku_remote_event(&app->remote, ww, &we);
            app->dirty = 1;
        }
    }
    return;
}
hit = tiku_workspace_hit(app->desk, ev->x, ev->y,
                        &w);
if (tiku_trk_find_window_popup_open(&app->find_window)) {
    /* Its menus hang below the panel's window and over
     * whatever is behind it, so the click goes to the
     * panel wherever it lands -- the panel eats it either
     * way, which is what closes a menu clicked off. */
    w = tiku_trk_find_window_handle(&app->find_window);
    hit = TIKU_HIT_CONTENT;
}
if (w == NULL && desktop_folder(app)->view != NULL) {
    /* The backdrop is a pose area, so a click on it is a
     * click in a view -- the same branch every window's
     * content goes through, which is what keeps the
     * Desktop's icons behaving like a folder's (AW-018). */
    hit = TIKU_HIT_CONTENT;
}
if (w != NULL && ev->button == 1u) {
    folder_t *bfl = folder_of(app, w);

    if (bfl != NULL) {
        tiku_rect_t bf = tiku_trk_folder_window_bar(bfl);
        tiku_rect_t fi =
            tiku_trk_folder_window_icon(bfl);
        int bi;

        if (ev->x >= fi.x && ev->x < fi.x + fi.w &&
            ev->y >= fi.y && ev->y < fi.y + fi.h) {
            tiku_model_t model;
            char parent[TIKU_PATH_MAX];
            char art[TIKU_NAME_MAX];

            art[0] = '\0';

            /* IV-057: the drag arms only from a pixel
             * that is part of the PICTURE -- an icon is
             * not a rectangle, and its transparent corner
             * is not a handle. */
            if (app->backend->ops->stat(app->backend, bfl->path,
                                       &model) == 0) {
                (void)tiku_trk_nodeicon_resolve(&app->nicons,
                    app->store, &model, art, sizeof art);
            }
            if (art[0] != '\0' &&
                tiku_icon_hit(art, fi.w, ev->x - fi.x,
                                  ev->y - fi.y)) {
                parent_path(bfl->path, parent, sizeof parent);
                tiku_workspace_activate(app->desk, w);
                app->press_at = tiku_trk_now();
                app->press_x = ev->x;
                app->press_y = ev->y;
                app->press_row = -1;
                app->press_fl = bfl;
                app->press_down = 1;
                app->press_menu = 0;
                tiku_trk_drag_arm(&app->drag, ev->x, ev->y,
                                  ev->button, 0u, 1, w,
                                  parent);
                (void)tiku_trk_drag_add_at(&app->drag,
                    bfl->path, model.type, fi.x, fi.y, 1);
                tiku_trk_drag_target_reset(&app->drag_target);
                app->dirty = 1;
                return;
            }
        }

        build_open_with(app, bfl);
        build_per_type(app, bfl);
        {
            tiku_trk_sc_ctx_t bc =
                menu_ctx(app, bfl, ev->modifiers);

            tiku_trk_cwmenu_rebuild(&app->menus, &bc);
        }
        bi = tiku_menubar_title_at(&app->menus.bar, bf,
                ev->x, ev->y);
        if (bi >= 0) {
            commit_rename(app, 1);
            tiku_workspace_activate(app->desk, w);
            app->bar_fl = bfl;
            app->bar_open = bi;
            app->menus.bar.open = bi;
            app->popup = app->menus.bar.menu[bi];
            app->popup_sub = NULL;
            app->popup_frame = tiku_menubar_panel(
                &app->menus.bar, bf,
                (tiku_rect_t){ 0, 0, app->surf->w,
                                    app->surf->h });
            app->nav_on = 0;
            app->dirty = 1;
            return;
        }
    }
}
if (w != NULL && ev->button == 1u) {
    /* The window's own scroll bar (PVL-051): arrows step,
     * the track pages, the thumb is held. */
    folder_t *sfl = folder_of(app, w);
    tiku_rect_t sb = (sfl != NULL)
        ? tiku_trk_folder_window_scrollbar(sfl)
        : (tiku_rect_t){ 0, 0, 0, 0 };

    if (sfl != NULL && sb.w > 0 && ev->x >= sb.x &&
        ev->x < sb.x + sb.w && ev->y >= sb.y &&
        ev->y < sb.y + sb.h) {
        int at = ev->y - sb.y;
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
        app->sbar_fl = sfl;
        app->dirty = 1;
        return;
    }
}
if (w != NULL && ev->button == 1u) {
    /* The column strip: a press starts the one tracker
     * every heading gesture lives behind -- click sorts,
     * Shift-click seconds, an edge drags a new width, a
     * title slides to reorder, and out of the strip tears
     * the column off (TS-063). */
    folder_t *tfl = folder_of(app, w);
    tiku_rect_t title = (tfl != NULL)
        ? tiku_trk_folder_window_title(tfl)
        : (tiku_rect_t){ 0, 0, 0, 0 };

    if (tfl != NULL && title.w > 0 && ev->x >= title.x &&
        ev->x < title.x + title.w && ev->y >= title.y &&
        ev->y < title.y + title.h) {
        unsigned tf;

        commit_rename(app, 1);
        tiku_trk_poseview_cancel_edit_wait(tfl->view);
        if (app->tstrip_fl != tfl) {
            tiku_trk_titleview_init(&app->tstrip, tfl->view,
                                    title.h);
            app->tstrip_fl = tfl;
        }
        tf = tiku_trk_titleview_mouse_down(&app->tstrip,
            ev->x - title.x, ev->y - title.y, 1,
            w == tiku_workspace_focused(app->desk),
            tiku_trk_now());
        if ((tf & TIKU_TRK_TT_ACTIVATE) != 0u) {
            tiku_workspace_activate(app->desk, w);
        }
        if (tiku_trk_titleview_tracking(&app->tstrip) !=
                TIKU_TRK_TRACK_NONE) {
            /* The press engaged a title or an edge.  A
             * press in the strip's empty gutter is NOT the
             * strip's: it falls through to the view, where
             * it starts a band as it always has. */
            app->strip_ptr_x = ev->x;
            app->strip_ptr_y = ev->y;
            app->dirty = 1;
            return;
        }
    }
}
if (w != NULL && ev->button == 3u) {
    folder_t *tfl = folder_of(app, w);
    tiku_rect_t title = (tfl != NULL)
        ? tiku_trk_folder_window_title(tfl)
        : (tiku_rect_t){ 0, 0, 0, 0 };

    if (tfl != NULL && ev->x >= title.x &&
        ev->x < title.x + title.w && ev->y >= title.y &&
        ev->y < title.y + title.h) {
        tiku_trk_sc_ctx_t tc = menu_ctx(app, tfl,
                                         ev->modifiers);

        build_open_with(app, tfl);
        build_per_type(app, tfl);
        tiku_trk_cwmenu_rebuild(&app->menus, &tc);
        tiku_workspace_activate(app->desk, w);
        app->bar_fl = NULL;
        app->bar_open = -1;
        app->menus.bar.open = -1;
        app->popup = &app->menus.attributes;
        app->popup_sub = NULL;
        app->popup_frame = tiku_menu_place(app->popup,
            ev->x, ev->y,
            (tiku_rect_t){ 0, 0, app->surf->w,
                                app->surf->h });
        app->press_down = 0;
        app->dirty = 1;
        return;
    }
}
if (w != NULL && hit != TIKU_HIT_CONTENT) {
    /* The CHROME activates a window; the content does not.
     * That is what lets a click reach a list in a window
     * behind another without the raise eating it -- the
     * click-through a spatial desktop depends on. */
    if (app->folder_edit.renaming &&
        app->folder_edit.rename_folder != NULL &&
        app->folder_edit.rename_folder->win != NULL &&
        app->folder_edit.rename_folder->win != w) {
        /* An open rename commits when its window loses
         * the keyboard, as the other four activation
         * roads already commit it: left open, the lit
         * field in the background window was a lie, and
         * the editor a place keys could still land. */
        commit_rename(app, 1);
    }
    tiku_workspace_activate(app->desk, w);
}
if (hit == TIKU_HIT_ZOOM) {
    folder_t *fl = folder_of(app, w);

    if (fl != NULL) {
        tiku_sound_play(TIKU_SOUND_WINDOW_ZOOM);
        (void)tiku_workspace_zoom(app->desk, w,
                                 tiku_trk_folder_window_fit_width(fl),
                                 tiku_trk_folder_window_fit_height(fl));
    }
    app->dirty = 1;
    return;
}
if (hit == TIKU_HIT_CLOSE) {
    /* Control+close closes every window rather than this
     * one, which is the only way to clear a spatial desktop
     * without visiting each window. */
    if (ev->modifiers & TIKU_MOD_CMD) {
        int k;

        for (k = 0; k < app->folder_count; k++) {
            if (app->folders[k].win != NULL) {
                tiku_workspace_close(app->desk,
                                    app->folders[k].win);
            }
        }
        if (tiku_trk_find_window_handle(&app->find_window) !=
                NULL) {
            tiku_trk_find_window_close(&app->find_window);
        }
        app->dirty = 1;
        return;
    }
    /* Option+close takes the folder's whole subtree with
     * it: the windows a descent through a deep folder left
     * behind are exactly the ones nobody wants to close one
     * at a time (AW-012). */
    if (ev->modifiers & TIKU_MOD_OPTION) {
        folder_t *cf = folder_of(app, w);

        if (cf != NULL) {
            close_windows_under(app, cf->path);
            app->dirty = 1;
            return;
        }
    }
}
if (hit == TIKU_HIT_CLOSE) {
    close_window(app, w);
} else if (hit == TIKU_HIT_TAB ||
           hit == TIKU_HIT_RESIZE) {
    /* An application window being RESHAPED will need to
     * hear its new size when the hand lets go. */
    app->resize_track =
        (hit == TIKU_HIT_RESIZE) ? w : NULL;
    tiku_workspace_begin_track(app->desk, w, hit, ev->x,
                              ev->y);
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           w == tiku_trk_select_window_handle(
                    &app->select_window)) {
    (void)tiku_window_send(w, ev);
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           w == tiku_trk_panel_window_handle(
                    &app->file_panel)) {
    (void)tiku_window_send(w, ev);
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           panel_of_window(app, w) != NULL) {
    /* A panel a device described: its controls are the
     * shell's own, so its clicks come here like any
     * other window's. */
    tiku_workspace_activate(app->desk, w);
    (void)tiku_window_send(w, ev);
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           embed_win_of_window(app, w) != NULL) {
    struct embed_win *ew = embed_win_of_window(app, w);
    tiku_event_t rev = *ev;
    tiku_rect_t rc = tiku_window_content(w);

    tiku_workspace_activate(app->desk, w);
    rev.x -= rc.x;
    rev.y -= rc.y;
    if (embed_deliver(ew, &rev)) {
        embed_stop(ew->slot);
    } else {
        app->embed_press = ew;
    }
    app->dirty = 1;
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           tiku_remote_owner(&app->remote, w) != NULL) {
    /* A remote window's content belongs to its session:
     * the event rides the wire in window coordinates. */
    tiku_event_t rev = *ev;
    tiku_rect_t rc = tiku_window_content(w);

    tiku_workspace_activate(app->desk, w);
    rev.x -= rc.x;
    rev.y -= rc.y;
    tiku_remote_event(&app->remote, w, &rev);
    app->remote_press = w;
    app->dirty = 1;
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           w == tiku_trk_find_window_handle(
                    &app->find_window) &&
           ev->button == 1u &&
           tiku_trk_find_window_icon_at(&app->find_window,
                                        ev->x, ev->y)) {
    /* Picking up the query icon (Q-034): the search
     * travels with the pointer until it is dropped. */
    app->qdrag_on = 1;
    app->dirty = 1;
} else if (hit == TIKU_HIT_CONTENT && w != NULL &&
           w == tiku_trk_find_window_handle(
                    &app->find_window)) {
    (void)tiku_window_send(w, ev);
} else if (hit == TIKU_HIT_CONTENT) {
    folder_t *fl = (w != NULL) ? folder_of(app, w)
                               : desktop_folder(app);
    if (fl != NULL && fl->view != NULL) {
        if (ev->button == 1u &&
            tiku_trk_folder_window_count_at(fl, ev->x, ev->y)) {
            /* The desktop shell exposes the same parent
             * hierarchy through the existing Open Parent
             * action; a count-strip click starts at its
             * nearest ancestor. */
            menu_invoke(app, fl, TIKU_TRK_CMD_OPEN_PARENT);
            app->dirty = 1;
            return;
        }
        int row = tiku_trk_folder_window_row_at(fl, ev->x,
                                               ev->y);
        unsigned mods = 0;
        unsigned click_flags = 0;
        int64_t now = tiku_trk_now();

        if (ev->modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
        if (ev->modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
        if (ev->modifiers & TIKU_MOD_SHIFT) {
            click_flags |= TIKU_TRK_CLICK_EXTEND;
        }
        if (ev->modifiers & TIKU_MOD_CMD) {
            click_flags |= TIKU_TRK_CLICK_EXTEND |
                           TIKU_TRK_CLICK_CONTROL;
        }
        if (ev->button == 3) {
            int k, kn;
            int lst = (tiku_trk_poseview_mode(fl->view) ==
                       TIKU_TRK_MODE_LIST);

            /* Normalize on DOWN, but do not build the menu
             * until UP: a secondary press is still allowed
             * to turn into a drag (PVS-016). */
            tiku_trk_poseview_cancel_edit_wait(fl->view);
            commit_rename(app, 1);
            (void)tiku_trk_poseview_context_click(fl->view,
                                                  row);
            tiku_workspace_activate(app->desk, w);
            app->press_at = now;
            app->press_x = ev->x;
            app->press_y = ev->y;
            app->press_row = row;
            app->press_fl = fl;
            app->press_window = w;
            app->press_focus =
                tiku_workspace_focused(app->desk);
            app->press_down = 1;
            app->press_menu = 0;
            tiku_trk_drag_arm(&app->drag, ev->x,
                              ev->y, 3u, mods, 1,
                              w, fl->path);
            {
                tiku_model_t source;
                tiku_trk_drag_set_source_virtual(&app->drag,
                    app->backend->ops->stat(app->backend,
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
                    (void)tiku_trk_drag_add_at(&app->drag,
                        q->model.path, q->model.type, qx, qy,
                        lst);
                }
            }
            tiku_trk_drag_target_reset(&app->drag_target);
            (void)tiku_trk_click_press(&lp->click, NULL, 0, 0,
                                       3u, 0u, now);
        } else if (ev->button == 4 ||
                   ev->button == 5) {
            tiku_trk_poseview_scroll_to(fl->view,
                tiku_trk_poseview_scroll(fl->view) +
                ((ev->button == 4) ? -3 : 3));
        } else {
            tiku_trk_pose_t *clicked = (row >= 0)
                ? tiku_trk_poseview_at(fl->view, row) : NULL;
            int twice = tiku_trk_click_press(&lp->click, clicked,
                ev->x, ev->y,
                ev->button, click_flags, now);

            {
                tiku_rect_t c = tiku_trk_folder_window_content(fl);
                int ox = 0, oy = 0;

                tiku_trk_poseview_origin(fl->view, &ox, &oy);
                tiku_trk_poseview_click_at(fl->view, row,
                    ev->x - (c.x + 2) + ox,
                    ev->y - (c.y + 2) + oy, mods);
            }
            app->press_at = now;
            app->press_x = ev->x;
            app->press_y = ev->y;
            app->press_row = row;
            app->press_fl = fl;
            app->press_window = w;
            app->press_focus =
                tiku_workspace_focused(app->desk);
            app->press_down = 1;
            app->press_menu = 0;
            /* Armed on a pose, started only if the pointer
             * then moves far enough (PVS-032).  The whole
             * selection travels, not the pose under the
             * pointer (PVS-033). */
            tiku_trk_drag_arm(&app->drag, ev->x,
                              ev->y,
                              ev->button, mods,
                              twice ? 2 : 1,
                              w, fl->path);
            {
                tiku_model_t source;
                tiku_trk_drag_set_source_virtual(&app->drag,
                    app->backend->ops->stat(app->backend,
                        fl->path, &source) == 0 &&
                    source.kind == TIKU_KIND_VIRTUAL_DIR);
            }
            if (row < 0 && ev->button == 1u) {
                /* Empty space: a sweep, not a click.  In
                 * EVERY mode -- a list's rows are swept by
                 * the same rectangle, and this shell had
                 * no band at all, so the only way to
                 * gather several was one modifier click at
                 * a time (PVS-024). */
                unsigned bm = 0u;

                if (ev->modifiers & TIKU_MOD_SHIFT) {
                    bm |= TIKU_TRK_MOD_SHIFT;
                }
                if (ev->modifiers & TIKU_MOD_CMD) {
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
                    &app->band_controller, fl, ev->x, ev->y,
                    bm);
            /* A sweep between two poses also ENDS any
                 * half-finished double click: without this
                 * the click that starts the band is
                 * invisible to the counter, and the next
                 * click on the icon just left completes a
                 * double click the user did not make. */
                (void)tiku_trk_click_press(&lp->click, NULL, 0,
                    0, ev->button, 0u, now);
                app->dirty = 1;
                return;
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
                        &app->drag, q->model.path,
                        q->model.type, qx, qy, lst);
                }
            }
            tiku_trk_drag_target_reset(&app->drag_target);
            if (row >= 0 && tiku_trk_folder_window_name_at(
                    fl, row, ev->x, ev->y)) {
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
                        ev->x) == TIKU_TRK_COL_PATH) {
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
                        (void)open_folder(app, parent);
                    }
                } else if (p != NULL &&
                    tiku_model_is_container(&p->model)) {
                    /* A folder opens its OWN window, which
                     * is what makes this spatial rather
                     * than a single browser. */
                    (void)open_folder(app, p->model.path);
                    if (ev->modifiers & TIKU_MOD_OPTION) {
                        /* PVK-018: Option-open closes the
                         * window it came from, which is how
                         * a spatial desktop is walked
                         * without leaving a trail. */
                        close_window(app, w);
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
                        (void)open_folder(app, parent);
                    }
                } else if (p != NULL &&
                           p->model.facts.link_broken) {
                    offer_broken_link(app, p->model.path);
                } else if (p != NULL) {
                    /* Not a container: it is LAUNCHED, in
                     * whatever opens its type -- which is
                     * what a double click has always meant
                     * and this shell could not do (AW-030,
                     * PVS-010). */
                    int went = open_one(app, &p->model);

                    if ((ev->modifiers & TIKU_MOD_OPTION) &&
                        went) {
                        /* Option-open closes the window it
                         * came from, once the launch has
                         * actually happened (AW-032). */
                        close_window(app, w);
                    }
                }
            }
        }
    }
}
app->dirty = 1;
}

/**
 * @brief The pointer moved: drags, bands, hovers and the tracking a press
 * began.
 */
void
desktop_pointer_move(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
app->ptr_x = ev->x;
app->ptr_y = ev->y;
if (tiku_trk_sounds_window_tracking(&app->sounds_window)) {
    /* The volume knob is in hand: every move is its. */
    (void)tiku_window_send(
        tiku_trk_sounds_window_handle(&app->sounds_window), ev);
    app->dirty = 1;
    return;
}
if (panel_tracking(app) != NULL) {
    (void)tiku_window_send(panel_tracking(app)->window, ev);
    return;
}
if (app->embed_press != NULL &&
    app->embed_press->window != NULL) {
    struct embed_win *ew = app->embed_press;
    tiku_event_t rev = *ev;
    tiku_rect_t rc =
        tiku_window_content(ew->window);

    rev.x -= rc.x;
    rev.y -= rc.y;
    if (embed_deliver(ew, &rev)) {
        embed_stop(ew->slot);
    }
    app->dirty = 1;
    return;
}
if (app->remote_press != NULL &&
    tiku_remote_owner(&app->remote,
                           app->remote_press) != NULL) {
    tiku_event_t rev = *ev;
    tiku_rect_t rc =
        tiku_window_content(app->remote_press);

    rev.x -= rc.x;
    rev.y -= rc.y;
    tiku_remote_event(&app->remote, app->remote_press,
                           &rev);
    app->dirty = 1;
    return;
}
if (!tiku_trk_drag_active(&app->drag)) {
    /* No press held anywhere: the window UNDER the
     * pointer still hears it passing, which is what a
     * toolbar's tips and every other hover are made
     * of.  An application that wants none returns 0
     * and the move cost a message. */
    tiku_window_t *hw = NULL;

    if (tiku_workspace_hit(app->desk, ev->x, ev->y,
                                &hw) == TIKU_HIT_CONTENT &&
        hw != NULL) {
        struct embed_win *hew =
            embed_win_of_window(app, hw);

        if (hew != NULL) {
            tiku_event_t rev = *ev;
            tiku_rect_t rc = tiku_window_content(hw);

            rev.x -= rc.x;
            rev.y -= rc.y;
            if (embed_deliver(hew, &rev)) {
                embed_stop(hew->slot);
            }
        } else if (tiku_remote_owner(&app->remote, hw)
                       != NULL) {
            tiku_event_t rev = *ev;
            tiku_rect_t rc = tiku_window_content(hw);

            rev.x -= rc.x;
            rev.y -= rc.y;
            tiku_remote_event(&app->remote, hw, &rev);
        }
    }
}
if (app->qdrag_on) {
    app->dirty = 1;
}
if (tiku_trk_deskbar_motion(app->deskbar, ev->x, ev->y)) {
    app->dirty = 1;
}
if (app->popup != NULL) {
    /* The hot row follows the pointer, in the level it is
     * over: without this a menu gives no sign of which
     * item a pick would take. */
    int menu_moved = 0;

    menu_moved |= tiku_menu_track(app->popup,
                                      app->popup_frame,
                                      ev->x, ev->y);
    if (app->popup_sub != NULL) {
        menu_moved |= tiku_menu_track(app->popup_sub,
                                          app->popup_sub_frame,
                                          ev->x, ev->y);
    }
    if (menu_moved) {
        app->dirty = 1;
    }
}
if (app->deskbar_press &&
    (ev->x - app->press_x > 4 || app->press_x - ev->x > 4 ||
     ev->y - app->press_y > 4 || app->press_y - ev->y > 4)) {
    app->deskbar_press = 0;
}
if (app->press_down &&
    (ev->x - app->press_x > 4 ||
     app->press_x - ev->x > 4 ||
     ev->y - app->press_y > 4 ||
     app->press_y - ev->y > 4)) {
    /* Moved out of the long-press radius: this is a drag,
     * not a held click, so neither the menu nor the rename
     * is owed (PVS-015, PVS-017). */
    if (app->press_fl != NULL) {
        tiku_trk_poseview_cancel_edit_wait(app->press_fl->view);
    }
    app->press_down = 0;
}
if (app->sbar_fl != NULL && app->sbar_fl->sbar.tracking) {
    tiku_rect_t sb =
        tiku_trk_folder_window_scrollbar(app->sbar_fl);

    tiku_trk_poseview_scroll_to(app->sbar_fl->view,
        tiku_scroll_drag(&app->sbar_fl->sbar,
            sb.h - 2 * sb.w, ev->y - sb.y - sb.w));
    app->dirty = 1;
    return;
}
if (tiku_trk_titleview_tracking(&app->tstrip) !=
        TIKU_TRK_TRACK_NONE && app->tstrip_fl != NULL) {
    tiku_rect_t st =
        tiku_trk_folder_window_title(app->tstrip_fl);
    unsigned tf = tiku_trk_titleview_mouse_moved(
        &app->tstrip, ev->x - st.x, ev->y - st.y,
        tiku_trk_now());

    app->strip_ptr_x = ev->x;
    app->strip_ptr_y = ev->y;
    if ((tf & (TIKU_TRK_TT_REDRAW | TIKU_TRK_TT_LINE |
               TIKU_TRK_TT_NO_LINE)) != 0u) {
        app->dirty = 1;
    }
    return;
}
if (tiku_trk_drag_moved(&app->drag, ev->x, ev->y) ||
    tiku_trk_drag_active(&app->drag)) {
    if (app->spring_nav_on) {
        spring_nav_track(app, ev->x, ev->y);
    }
    (void)tiku_trk_drag_target_motion(&app->drag_target,
                                      ev->x, ev->y,
                                      tiku_trk_now());
    /* Read at the badge, so the copy indication follows
     * the modifier live, as the cursor did (PVS-036). */
    if (app->drag_live_mods != ev->modifiers) {
        app->drag_live_mods = ev->modifiers;
        app->dirty = 1;
    }
}
{
    /* An Info window's icon answers a hovering drag with
     * its selected variant (IV-058). */
    int hot = tiku_trk_drag_active(&app->drag)
                  ? info_icon_drop_slot(app, ev->x, ev->y)
                  : -1;

    if (hot != app->info_windows.drop_hot) {
        app->info_windows.drop_hot = hot;
        app->dirty = 1;
    }
}
{
    /* The pointer resting on a truncated Info field opens
     * the expanding pop-up; leaving takes it away
     * (AW-073). */
    tiku_window_t *hw = NULL;
    int hslot = -1, hfield = -1, hwhich = -1;

    if (!tiku_trk_drag_active(&app->drag)) {
        (void)tiku_workspace_hit(app->desk, ev->x, ev->y,
                                      &hw);
        hslot = tiku_trk_info_windows_slot(
            &app->info_windows, hw);
        if (hslot >= 0 &&
            hw == tiku_workspace_focused(app->desk) &&
            tiku_trk_info_windows_hit(&app->info_windows,
                hslot, ev->x, ev->y, &hwhich) ==
                TIKU_TRK_INFO_HIT_FIELD) {
            hfield = hwhich;
        } else {
            hslot = -1;
        }
    }
    if (hslot != app->info_windows.hover_slot ||
        hfield != app->info_windows.hover_field) {
        app->info_windows.hover_slot = hslot;
        app->info_windows.hover_field = hfield;
        app->dirty = 1;
    }
}
{
    unsigned bm = 0u;

    if (ev->modifiers & TIKU_MOD_SHIFT) {
        bm |= TIKU_TRK_MOD_SHIFT;
    }
    if (ev->modifiers & TIKU_MOD_CMD) {
        bm |= TIKU_TRK_MOD_CMD;
    }
    (void)tiku_trk_band_controller_motion(
        &app->band_controller, ev->x, ev->y, bm);
}
if (app->bar_resize) {
    int dw = (tiku_trk_deskbar_dock(app->deskbar) ==
              TIKU_TRK_DB_RIGHT_EXPANDED)
                 ? app->bar_resize_x - ev->x
                 : ev->x - app->bar_resize_x;

    if (tiku_trk_deskbar_set_width(app->deskbar,
            app->bar_resize_w + dw) > 0) {
        app->dirty = 1;
    }
}
if (app->dock_armed &&
    ((ev->x - app->dock_x) * (ev->x - app->dock_x) +
     (ev->y - app->dock_y) * (ev->y - app->dock_y) >
     TIKU_TRK_DRAG_SLOP * TIKU_TRK_DRAG_SLOP)) {
    app->dock_armed = 0;
    app->dock_drag = 1;
}
if (app->dock_drag) {
    /* Ctrl held on the bar: the pointer IS the picker.
     * Where it lets go decides the dock, and a release in
     * the middle keeps the one it started from (DB-049). */
        tiku_trk_db_dock_t want =
        tiku_trk_deskbar_dock_at(ev->x, ev->y,
            app->surf->w, app->surf->h,
            tiku_trk_deskbar_dock(app->deskbar));

    if (want != tiku_trk_deskbar_dock(app->deskbar)) {
        tiku_trk_deskbar_set_dock(app->deskbar, want);
        app->dirty = 1;
    }
}
if (tiku_trk_deskbar_pointer(app->deskbar, ev->x,
                             ev->y)) {
    /* The one input the bar never had.  Auto-hide and
     * auto-raise are both answers to "where is the
     * pointer", and neither could be asked (DB-051,
     * DB-052). */
    app->dirty = 1;
}
if (tiku_workspace_tracking(app->desk)) {
    if (tiku_workspace_track(app->desk, ev->x,
                            ev->y)) {
        app->dirty = 1;
    }
}
}

/** @brief A button came up: drops land, clicks resolve, tracking lets go. */
void
desktop_pointer_up(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
int dropped = tiku_trk_drag_active(&app->drag);

if (tiku_trk_sounds_window_tracking(&app->sounds_window)) {
    (void)tiku_window_send(
        tiku_trk_sounds_window_handle(&app->sounds_window), ev);
    app->dirty = 1;
    return;
}
if (panel_tracking(app) != NULL) {
    (void)tiku_window_send(panel_tracking(app)->window, ev);
    return;
}
if (app->embed_press != NULL) {
    struct embed_win *ew = app->embed_press;

    app->embed_press = NULL;
    if (ew->slot->running && ew->window != NULL) {
        tiku_event_t rev = *ev;
        tiku_rect_t rc =
            tiku_window_content(ew->window);

        rev.x -= rc.x;
        rev.y -= rc.y;
        if (embed_deliver(ew, &rev)) {
            embed_stop(ew->slot);
        }
    }
    app->dirty = 1;
    return;
}
if (app->remote_press != NULL) {
    if (tiku_remote_owner(&app->remote,
                               app->remote_press) != NULL) {
        tiku_event_t rev = *ev;
        tiku_rect_t rc =
            tiku_window_content(app->remote_press);

        rev.x -= rc.x;
        rev.y -= rc.y;
        tiku_remote_event(&app->remote,
                               app->remote_press, &rev);
    }
    app->remote_press = NULL;
    app->dirty = 1;
    return;
}

if (app->qdrag_on) {
    /* The query icon lands (Q-034): over a folder window
     * or the desktop, the drop menu asks what to make of
     * it; back on the Find window it is just put down. */
    tiku_window_t *tw = NULL;
    folder_t *tfl;

    (void)tiku_workspace_hit(app->desk, ev->x, ev->y,
                                  &tw);
    tfl = (tw != NULL) ? folder_of(app, tw)
                       : desktop_folder(app);
    app->qdrag_on = 0;
    if (tfl != NULL && tfl->view != NULL &&
        (tw == NULL ||
         tw != tiku_trk_find_window_handle(
                   &app->find_window))) {
        tiku_model_t dm;
        int ro = 1;

        if (app->backend->ops->stat(app->backend, tfl->path,
                                   &dm) == 0) {
            ro = !tiku_model_is_mutable_container(&dm);
        }
        snprintf(app->qdrop_dest, sizeof app->qdrop_dest,
                 "%s", tfl->path);
        app->popup = tiku_trk_cwmenu_query_drop(&app->menus,
                                               ro);
        app->popup_fl = tfl;
        app->popup_sub = NULL;
        app->popup_frame = tiku_menu_place(app->popup,
            ev->x, ev->y,
            (tiku_rect_t){ 0, 0, app->surf->w,
                                app->surf->h });
    }
    app->dirty = 1;
    return;
}

if (tiku_trk_titleview_tracking(&app->tstrip) !=
        TIKU_TRK_TRACK_NONE && app->tstrip_fl != NULL) {
    tiku_rect_t st =
        tiku_trk_folder_window_title(app->tstrip_fl);
    unsigned tf = tiku_trk_titleview_mouse_up(&app->tstrip,
        ev->x - st.x, ev->y - st.y,
        (ev->modifiers & TIKU_MOD_SHIFT) != 0,
        tiku_trk_now());

    (void)tf;
    app->dirty = 1;
    return;
}

app->dock_drag = 0;
app->bar_resize = 0;
if (app->dock_armed) {
    /* The press never moved: it was the chord after all
     * (DB-019/020/021). */
    tiku_trk_db_pick_t hpick;

    app->dock_armed = 0;
    if (tiku_trk_deskbar_click(app->deskbar, app->dock_x,
                               app->dock_y, app->dock_mods,
                               (int)app->dock_button, 1,
                               &hpick)) {
        deskbar_pick_dispatch(app, &hpick, app->dock_x,
                              app->dock_y);
        app->dirty = 1;
    }
}
app->deskbar_press = 0;
if (tiku_trk_deskbar_release(app->deskbar, ev->x, ev->y)) {
    app->dirty = 1;
}
(void)tiku_trk_band_controller_finish(&app->band_controller);
if (dropped && app->spring_nav_on) {
    /* Choosing a spring entry IS the drop (PVS-051): the
     * chosen row is where the items land, a disabled row
     * swallows the release, and a release outside the
     * menu falls back to the drop under the pointer. */
    char sdest[TIKU_PATH_MAX];
    int got = spring_nav_hit(app, ev->x, ev->y, sdest,
                             sizeof sdest);

    spring_nav_close(app);
    if (got == 1) {
        tiku_trk_drop_controller_execute_into(
            &app->drop_controller, sdest, ev->x, ev->y,
            ev->modifiers, ev->button, 0);
    } else if (got == 0) {
        do_drop(app, ev->x, ev->y, ev->modifiers, ev->button);
    }
} else if (dropped && drop_on_panel(app, ev->x, ev->y)) {
    /* Consumed by the panel, action or not (AW-059). */
} else if (dropped && drop_on_info_icon(app, ev->x, ev->y,
                                 ev->modifiers)) {
    /* Taken by an Info window's icon: the folder drop
     * machinery must not run a second time. */
} else if (dropped) {
    do_drop(app, ev->x, ev->y,
            ev->modifiers, ev->button);
} else if (ev->button == 3u && app->press_down &&
           !app->press_menu && app->press_fl != NULL) {
    folder_t *pfl = app->press_fl;
    tiku_trk_pose_t *p = (app->press_row >= 0)
        ? tiku_trk_poseview_at(pfl->view, app->press_row)
        : NULL;
    unsigned cmods = 0u;
    tiku_trk_sc_ctx_t c;

    if (ev->modifiers & TIKU_MOD_SHIFT) {
        cmods |= TIKU_MENU_MOD_SHIFT;
    }
    if (ev->modifiers & TIKU_MOD_CMD) {
        cmods |= TIKU_MENU_MOD_CMD;
    }
    c = menu_ctx(app, pfl, cmods);
    app->popup_fl = pfl;

    build_open_with(app, pfl);
    build_per_type(app, pfl);
    app->popup = tiku_trk_cwmenu_context(&app->menus,
                        (p != NULL) ? &p->model : NULL, &c);
    app->bar_fl = NULL;
    app->bar_open = -1;
    app->menus.bar.open = -1;
    app->nav_on = 0;
    if (p != NULL && tiku_trk_nav_wanted(&p->model)) {
        tiku_trk_nav_open(&app->nav, app->backend,
                          p->model.path);
        app->nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(app),
                                       "HideDotFiles");
        app->nav.folders_first = tiku_trk_prefs_get(prefs_model(app),
                                   "SortFolderNamesFirst");
        app->nav_on = 1;
    }
    app->popup_sub = NULL;
    app->popup_frame = tiku_menu_place(app->popup,
        ev->x, ev->y,
        (tiku_rect_t){ 0, 0, app->surf->w,
                            app->surf->h });
    app->dirty = 1;
}
if (!dropped && app->press_down && !app->press_menu &&
    app->press_window != NULL &&
    tiku_workspace_focused(app->desk) ==
        app->press_focus) {
    /* Content clicks raise on release, while movement has
     * already cleared press_down, leaving drag-out from a
     * background window click-through.  A click that opened
     * something is exempt: the raise would put the window
     * clicked in back over the window it just opened. */
    tiku_workspace_activate(app->desk,
                                  app->press_window);
}
if (app->sbar_fl != NULL) {
    tiku_scroll_release(&app->sbar_fl->sbar);
    app->sbar_fl = NULL;
}
tiku_trk_drag_reset(&app->drag);
tiku_trk_drag_target_reset(&app->drag_target);
app->info_windows.drop_hot = -1;
app->info_windows.hover_slot = -1;
app->info_windows.hover_field = -1;
/* A release ends whatever the press began -- a drag with a
 * picture under the pointer, a band, a drop that raised an
 * alert.  All of it is on the screen until a frame says
 * otherwise, and a shell with no pointer moving over it
 * gets no other reason to draw one. */
app->dirty = 1;
/* A long press already put the menu up, so its release must
 * not put a second one up (PVS-017). */
app->press_down = 0;
app->press_menu = 0;
app->press_window = NULL;
app->press_focus = NULL;
tiku_workspace_end_track(app->desk);
if (app->resize_track != NULL) {
    /* The hand let go: whoever owns the window is told
     * the content's new size, and answers with a frame
     * painted at it. */
    tiku_window_t *rw2 = app->resize_track;
    tiku_rect_t rc = tiku_window_content(rw2);
    tiku_event_t rev;
    struct embed_win *ew =
        embed_win_of_window(app, rw2);

    memset(&rev, 0, sizeof rev);
    rev.type = TIKU_EVENT_RESIZE;
    rev.width = rc.w;
    rev.height = rc.h;
    if (ew != NULL) {
        (void)embed_deliver(ew, &rev);
    } else if (tiku_remote_owner(&app->remote,
                                      rw2) != NULL) {
        tiku_remote_event(&app->remote, rw2, &rev);
    }
    app->resize_track = NULL;
    app->dirty = 1;
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
/* A menu that is open rewrites itself as modifiers move:
 * nine of its items change label AND command together while
 * Shift is held.  The table already knows how; this is the
 * edge that tells it to.  Handled on BOTH key edges, since
 * releasing Shift has to put the labels back. */
if (app->popup != NULL) {
    unsigned mk = ev->key;

    if (mk == TIKU_KEY_SHIFT ||
        mk == TIKU_KEY_CMD) {
        tiku_window_t *fw = tiku_workspace_focused(app->desk);
        folder_t *ffl = folder_of(app, fw);
        unsigned m2 = 0;
        tiku_trk_sc_ctx_t c2;

        /* TIKU_EVENT_KEY_UP reports the state BEFORE the release,
         * so the bit has to be taken out by hand or the menu
         * stays in its shifted face. */
        if (ev->modifiers & TIKU_MOD_SHIFT) {
            m2 |= TIKU_TRK_MOD_SHIFT;
        }
        if (ev->type == TIKU_EVENT_KEY_UP &&
            mk == TIKU_KEY_SHIFT) {
            m2 &= ~(unsigned)TIKU_TRK_MOD_SHIFT;
        }
        if (ev->type == TIKU_EVENT_KEY_DOWN &&
            mk == TIKU_KEY_SHIFT) {
            m2 |= TIKU_TRK_MOD_SHIFT;
        }
        c2 = menu_ctx(app, ffl, m2);
        tiku_trk_sc_update(app->popup, &c2);
        if (app->popup_sub != NULL) {
            tiku_trk_sc_update(app->popup_sub, &c2);
        }
        app->dirty = 1;
    }
    return;
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
if (key == TIKU_KEY_ESCAPE &&
    tiku_trk_deskbar_calendar_open(app->deskbar)) {
    /* Escape is a focus-loss equivalent for the small clock
     * popups; it must not close a folder or edit behind it. */
    tiku_trk_deskbar_close_calendar(app->deskbar);
    app->dirty = 1;
    return;
}
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
            app->find_buf[--app->find_len] = '\0';
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
if (tiku_trk_status_overlay_key(&app->status_overlay, key)) {
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
if (key == TIKU_KEY_MENU && (ev->modifiers &
        (TIKU_MOD_CMD | TIKU_MOD_SHIFT | TIKU_MOD_OPTION)) == 0) {
    /* The Menu key alone opens the leaf menu -- the one
     * gesture that reaches the Deskbar without the
     * pointer (DB-011). */
    if (tiku_trk_deskbar_menu_open(app->deskbar)) {
        tiku_trk_deskbar_close_menu(app->deskbar);
    } else {
        tiku_trk_deskbar_open_menu(app->deskbar);
    }
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
if (key == 'w' && (ev->modifiers & TIKU_MOD_CMD)) {
    close_window(app, w);
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

/**
 * @brief What a turn does before it reads events: the settings file, the
 * script transport, the remote sessions and what travels with them.
 */
void
desktop_turn_begin(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
        (void)lp;
    (void)o;
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
    desktop_spring_tick(app);
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
    if (app->dirty) {
        draw_all(app);
        blit(app);
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
        usleep((useconds_t)nap);
    }
}
