/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_events.c - What a turn does after the events: progress,
 * loading, the pump, the shot, the frame, the deadline, and the nap.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/* Used before they are defined. */
int browser_press_list(app_t *app, const tiku_event_t *ev, browser_loop_t *lp);
int browser_press_icons(app_t *app, const tiku_event_t *ev, browser_loop_t *lp);
int browser_press_title(app_t *app, const tiku_event_t *ev, browser_loop_t *lp);
int browser_press_scroll(app_t *app, const tiku_event_t *ev,
                         browser_loop_t *lp);
int browser_script_step(app_t *app, browser_loop_t *lp, tiku_event_t *ev,
                        int *have);
/* End of the forward declarations. */

/**
 * @brief What a turn does after the events: progress, loading, the pump, the
 * shot, the frame, the deadline, and the nap.
 */
void
browser_turn_end(app_t *app, const browser_options_t *o, browser_loop_t *lp)
{
    tiku_sound_pump();
    if (app->fs != NULL) {
        /* What moved since last turn -- a step's doing, or a rename's,
         * which is no operation at all -- told to the hook that follows
         * it, here on the loop. */
        tiku_fs_moved_pump(app->fs);
    }
    if (app->fs != NULL && tiku_fs_busy(app->fs)) {
        const tiku_progress_t *pr;

        /* Stepped, not blocking: the window keeps drawing while a copy
         * runs, which is the whole reason the engine has a cursor. */
        (void)tiku_fs_step(app->fs, 4);
        /* A mixed paste left its second half waiting for the engine to
         * go idle; this is where it gets to start (FS-055). */
        (void)tiku_trk_folder_file_actions_pump(&app->file_actions);
        pr = tiku_fs_progress(app->fs);
        if (pr->conflict) {
            /* Asked, not assumed.  The engine worked out which form
             * the question takes; this puts it up and waits. */
            if (!tiku_trk_status_overlay_alert(&app->status_overlay)->open) {
                tiku_trk_alert_for_conflict(
                    tiku_trk_status_overlay_alert(&app->status_overlay), pr);
            }
        } else if (pr->error_ask) {
            if (!tiku_trk_status_overlay_alert(&app->status_overlay)->open) {
                tiku_trk_alert_for_error(
                    tiku_trk_status_overlay_alert(&app->status_overlay), pr);
            }
        } else {
            snprintf(app->status, sizeof app->status, "%d of %d%s%s",
                     pr->done + pr->skipped, pr->total,
                     pr->skipped ? " (some skipped)" : "",
                     pr->failed ? " -- some failed" : "");
        }
        if (!tiku_fs_busy(app->fs)) {
            browser_after_op(app);
        }
        app->dirty = 1;
    }
    {   /* Tracker's Pulse: the armed rename starts here, not on the
         * click, which is what makes it a slow second click and not a
         * second click (MA-043). */
        int due = tiku_trk_poseview_edit_due(app->view, tiku_trk_now());

        if (due >= 0 && !app->editing) {
            browser_begin_rename_row(app, due);
        }
    }
    if (tiku_trk_poseview_loading(app->view)) {
        /* One batch per turn round the loop, so the window keeps
         * drawing and answering the mouse while a folder arrives. */
        (void)tiku_trk_poseview_load_step(app->view, tiku_trk_now());
        app->dirty = 1;
    }
    tiku_trk_poseview_type_tick(app->view);
    if (tiku_trk_poseview_pump(app->view, 40) > 0) {
        app->dirty = 1;                /* a live value moved */
    }
    if (o->shot != NULL && lp->shot_at_us > 0 &&
        tiku_trk_now() >= lp->shot_at_us) {
        app->dirty = 1;              /* force one draw to capture */
    }
    if (app->dirty) {
        browser_draw(app);
        browser_blit(app);
        if (o->shot != NULL && lp->shot_at_us > 0 &&
            tiku_trk_now() >= lp->shot_at_us) {
            /* Dump what was just drawn: the window is the deliverable,
             * so it must be reviewable without someone watching it. */
            (void)tiku_surface_png(app->surf, o->shot);
            printf("wrote %s\n", o->shot);
            fflush(stdout);
            lp->shot_at_us = 0;
        }
    }
    if (o->deadline_us > 0 && tiku_trk_now() > o->deadline_us) {
        app->running = 0;
    }
    /* A script still lets the clock run, just faster: the notice
     * poll, the armed rename and the barber pole are all timed, and a
     * loop that spun without time passing would test none of them. */
    if (app->loop != NULL) {
        /* A task that changed something on screen dirties the surface
         * itself; running is not, by itself, a repaint. */
        (void)tiku_trk_loop_pulse(app->loop);
        {   /* A held arrow keeps stepping (NeXT UIG ch3). */
            int rep = tiku_scroll_arrow_repeat(&app->sb);

            if (rep != 0) {
                tiku_trk_poseview_scroll_to(app->view,
                    tiku_trk_poseview_scroll(app->view) +
                        rep * tiku_scroll_line(&app->sb));
                app->dirty = 1;
            }
            rep = tiku_scroll_arrow_repeat(&app->hsb);
            if (rep != 0) {
                tiku_trk_poseview_set_hscroll(app->view,
                    tiku_trk_poseview_hscroll(app->view) +
                        rep * tiku_scroll_line(&app->hsb));
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

/**
 * @brief A button came up: clicks resolve, drags land, tracking lets go.
 */
void
browser_pointer_up(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    /* Letting go applies whatever range arrived while the bar
     * was held (PVL-052). */
    tiku_scroll_release(&app->sb);
    tiku_scroll_release(&app->hsb);
    if (tiku_trk_titleview_tracking(&app->tv) !=
            TIKU_TRK_TRACK_NONE) {
        int cx = 0, cy = 0;

        (void)browser_title_hit(app, ev->x, ev->y, &cx,
                        &cy);
        (void)tiku_trk_titleview_mouse_up(&app->tv, cx, cy,
            (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0,
            tiku_trk_now());
        (void)tiku_trk_poseview_save_columns(app->view);
        app->dirty = 1;
        return;
    }
    if (ev->button == 3u && app->press_down) {
        browser_open_context_menu(app, app->press_row, ev->x,
                          ev->y, ev->modifiers);
    }
    app->press_down = 0;
    if (app->drag_active) {
        tiku_rect_t nr = browser_navigator_rect(app);

        if (ev->x >= nr.x &&
            ev->x < nr.x + nr.w &&
            ev->y >= nr.y &&
            ev->y < nr.y + nr.h) {
            tiku_trk_pose_t *p =
                tiku_trk_poseview_active(app->view);
            char dest[TIKU_PATH_MAX];

            app->drag_active = 0;
            if (p != NULL && tiku_trk_navigator_drop_path(
                    &p->model, dest, sizeof dest)) {
                (void)browser_navigate_to(app, dest, 1);
            }
            app->dirty = 1;
            return;
        }
    }
    if (app->drag_active &&
        (ev->modifiers & TIKU_MOD_OPTION) != 0u) {
        /* Option makes the same-folder drag a DUPLICATE, not a
         * reposition (FS-004): the poses go back where they
         * were picked up and the copies are made instead. */
        int i, n = tiku_trk_poseview_count(app->view);
        int bx = app->press_x - ev->x, by = app->press_y - ev->y;

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(app->view, i);

            if (p != NULL && p->selected) {
                tiku_trk_poseview_move_pose(app->view, p,
                    p->loc_x + bx, p->loc_y + by, 0);
            }
        }
        app->drag_active = 0;
        browser_menu_key(app, 'd', TIKU_MOD_CMD);
    }
    if (app->band_active || app->drag_active) {
        app->band_active = 0;
        app->drag_active = 0;
        tiku_trk_poseview_set_band(app->view, 0);
        /* Persist where things ended up: the arrangement is the
         * user's, and it belongs on the objects. */
        (void)tiku_trk_poseview_save_state(app->view);
        app->dirty = 1;
    }
}

/**
 * @brief The pointer moved: drags, bands, hovers and the tracking a press
 * began.
 */
void
browser_pointer_move(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    app->ptr_x = ev->x;
    app->ptr_y = ev->y;
    if (app->popup != NULL) {
        /* The hot row follows the pointer, in the level it is
         * over: without this a menu gives no sign of which
         * item a pick would take. */
        int moved = 0;

        moved |= tiku_menu_track(app->popup, app->popup_frame,
                                     ev->x, ev->y);
        if (app->popup_sub != NULL) {
            moved |= tiku_menu_track(app->popup_sub,
                                         app->popup_sub_frame,
                                         ev->x, ev->y);
        }
        if (moved) {
            app->dirty = 1;
        }
        return;
    }
    if (app->sb.tracking) {
        tiku_trk_poseview_scroll_to(app->view,
            tiku_scroll_drag(&app->sb,
                app->sb_rect.h - 2 * app->sb_rect.w,
                ev->y - app->sb_rect.y - app->sb_rect.w));
        app->dirty = 1;
        return;
    }
    if (app->hsb.tracking) {
        tiku_trk_poseview_set_hscroll(app->view,
            tiku_scroll_drag(&app->hsb,
                app->hsb_rect.w - 2 * app->hsb_rect.h,
                ev->x - app->hsb_rect.x - app->hsb_rect.h));
        app->dirty = 1;
        return;
    }
    int cx = 0, cy = 0;

    if (tiku_trk_titleview_tracking(&app->tv) !=
            TIKU_TRK_TRACK_NONE ||
        browser_title_hit(app, ev->x, ev->y, NULL, NULL)) {
        unsigned tf;

        (void)browser_title_hit(app, ev->x, ev->y, &cx,
                        &cy);
        tf = tiku_trk_titleview_mouse_moved(&app->tv, cx, cy,
                                            tiku_trk_now());
        {   /* TS-013: the pointer says what it is over, and
             * only while nothing is being tracked. */
            /* An inactive window offers no resize cursor: the
             * click it would invite is spent activating. */
            int want = ((tf & TIKU_TRK_TT_CURSOR) != 0u) &&
                       app->active;

            if (want != app->cursor_ew) {
                app->cursor_ew = want;
                tiku_host_set_resize_cursor(app->host, want);
            }
        }
        if ((tf & TIKU_TRK_TT_REDRAW) != 0u) {
            app->dirty = 1;
        }
        if (tiku_trk_titleview_tracking(&app->tv) !=
                TIKU_TRK_TRACK_NONE) {
            return;
        }
    }
    if (app->press_down &&
        (ev->x - app->press_x > 4 ||
         app->press_x - ev->x > 4 ||
         ev->y - app->press_y > 4 ||
         app->press_y - ev->y > 4)) {
        /* PVS-015: a drag is not a slow click. */
        tiku_trk_poseview_cancel_edit_wait(app->view);
        app->press_down = 0;
    }
    if (app->band_active) {
        unsigned mods = 0;
        tiku_rect_t lr = browser_list_rect(app);

        if (ev->modifiers & TIKU_MOD_SHIFT)   { mods |= TIKU_TRK_MOD_SHIFT; }
        if (ev->modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
        app->band_x1 = ev->x;
        app->band_y1 = ev->y;
        (void)tiku_trk_poseview_select_rect(app->view,
            app->band_x0 - lr.x, app->band_y0 - lr.y,
            app->band_x1 - lr.x, app->band_y1 - lr.y, mods);
        app->dirty = 1;
    } else if (app->drag_active) {
        tiku_rect_t lr = browser_list_rect(app);
        int lx = ev->x - lr.x, ly = ev->y - lr.y;
        int dx = lx - app->drag_x, dy = ly - app->drag_y;
        int i, n = tiku_trk_poseview_count(app->view);
        int snap = (ev->modifiers & TIKU_MOD_CMD) ? 1 : 0;

        if (dx != 0 || dy != 0) {
            /* Every selected pose moves by the same delta from
             * where it started, so a group keeps its shape and a
             * snapped drag still crosses cell boundaries; Ctrl
             * snaps to the grid. */
            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(app->view, i);
                if (p == NULL || !p->selected) { continue; }
                tiku_trk_poseview_move_pose(app->view, p,
                                            p->drag_x0 + dx,
                                            p->drag_y0 + dy, snap);
            }
            app->dirty = 1;
        }
    }
}

/**
 * @brief A button went down: what is under the pointer decides everything that
 * follows.
 */
void
browser_pointer_down(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    tiku_trk_loop_note_activity(app->loop);
    if (tiku_trk_status_overlay_pointer(&app->status_overlay,
                                        ev->x, ev->y)) {
        /* A modal alert eats the click wherever it lands. */
        app->dirty = 1;
        return;
    }
    if (ev->button == 1 &&
        browser_menubar_press(app, ev->x, ev->y,
                      ev->modifiers)) {
        browser_end_rename(app, 1);
        return;
    }
    if (browser_popup_press(app, ev->x, ev->y)) {
        return;
    }
    if (ev->button == 1 &&
        browser_navigator_press(app, ev->x, ev->y)) {
        return;
    }
    if (browser_press_scroll(app, ev, lp)) {
        return;
    }
    if (browser_press_title(app, ev, lp)) {
        return;
    }
    if (browser_press_icons(app, ev, lp)) {
        return;
    }
    if (browser_press_list(app, ev, lp)) {
        return;
    }
}

/**
 * @brief The pump: every event the host has queued, or the script's next
 *        step made into one, each dispatched to its case.  Stops when the
 *        queue is empty, when a script step ends the turn, or when the
 *        script is over.
 */
void
browser_turn_begin(app_t *app, browser_loop_t *lp)
{
    for (;;) {
        tiku_event_t ev;
        int have = 0, stepped;

        memset(&ev, 0, sizeof ev);
        stepped = browser_script_step(app, lp, &ev, &have);
        if (stepped == 1) {
            return;
        }
        if (stepped == 2) {
            break;
        }
        if (!have) {
            break;
        }

        switch (ev.type) {
        case TIKU_EVENT_RESIZE:
            if (ev.width > 0 && ev.height > 0 &&
                (ev.width != app->surf->w || ev.height != app->surf->h)) {
                (void)tiku_surface_resize(app->surf, ev.width,
                                               ev.height,
                                               TIKU_C_PANEL);
                /* The overlay centres its alert on these; stale ones
                 * would put the question off a shrunken window. */
                app->status_overlay.screen_w = ev.width;
                app->status_overlay.screen_h = ev.height;
                {
                    /* Both axes re-anchor to the new window: the
                     * vertical clamp always ran through visible_rows,
                     * and the horizontal one runs here (PVL-058). */
                    tiku_rect_t lr = browser_list_rect(app);

                    tiku_trk_poseview_set_viewport(app->view, lr.w - 4,
                                                   lr.h - 4);
                    tiku_trk_poseview_set_hscroll(app->view,
                        tiku_trk_poseview_hscroll(app->view));
                }
            }
            app->dirty = 1;
            break;
        case TIKU_EVENT_EXPOSE:
            app->dirty = 1;
            break;
        case TIKU_EVENT_KEY_DOWN:
            tiku_trk_loop_note_activity(app->loop);
            browser_handle_key(app, &ev);
            break;
        case TIKU_EVENT_POINTER_DOWN:
            browser_pointer_down(app, &ev, lp);
            break;
        case TIKU_EVENT_POINTER_MOVE:
            browser_pointer_move(app, &ev, lp);
            break;
        case TIKU_EVENT_POINTER_UP:
            browser_pointer_up(app, &ev, lp);
            break;
        case TIKU_EVENT_ACTIVATED:
        case TIKU_EVENT_DEACTIVATED:
            /* PVS-023: the selection stops being PAINTED while the
             * window is inactive, and the pivot is parked so a
             * shift-click on return still extends from where the user
             * left off.  A pending rename does not survive the trip. */
            app->active = (ev.type == TIKU_EVENT_ACTIVATED);
            tiku_trk_poseview_window_activated(app->view, app->active);
            if (!app->active) {
                tiku_trk_poseview_cancel_edit_wait(app->view);
                if (app->editing) {
                    browser_end_rename(app, 1);   /* deactivation COMMITS */
                }
            }
            app->dirty = 1;
            break;
        case TIKU_EVENT_CLOSE:
            app->running = 0;
            break;
        default:
            break;
        }
        if (lp->sc != NULL) {
            break;              /* one instruction per turn */
        }
    }
}

/**
 * @brief a press in list mode: a row, the count, or the empty list.  @return 1
 * when this was the command
 */
int
browser_press_list(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    {
        if (ev->button == 1u && browser_count_at(app, ev->x, ev->y)) {
            /* This shell has one content surface, so choosing
             * the first hierarchy entry is represented by the
             * immediate-parent navigation action. */
            browser_navigate_up(app);
            app->dirty = 1;
            return 1;
        }
        int row = browser_row_at(app, ev->x, ev->y);
        unsigned mods = 0;
        unsigned click_flags = 0;
        int64_t now = tiku_trk_now();
        int on_name = 0;
        tiku_trk_pose_t *pose = (row >= 0)
            ? tiku_trk_poseview_at(app->view, row) : NULL;
        int twice;

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
            /* PVS-015: a right-click disarms a pending rename;
             * PVS-018: and normalizes the selection first. */
            tiku_trk_poseview_cancel_edit_wait(app->view);
            (void)tiku_trk_poseview_context_click(app->view, row);
            app->press_at = now;
            app->press_x = ev->x;
            app->press_y = ev->y;
            app->press_row = row;
            app->press_down = 1;
            (void)tiku_trk_click_press(&lp->click, NULL, 0, 0, 3u,
                                       0u, now);
            app->dirty = 1;
            return 1;
        }
        twice = tiku_trk_click_press(&lp->click, pose,
            ev->x, ev->y, ev->button,
            click_flags, now);
        /* Whether the press landed on the NAME, which is the
         * only widget a rename can start from. */
        {
            const tiku_trk_column_t *cs;
            int nn = tiku_trk_poseview_columns(app->view, &cs);
            int cx = BORDER, k;

            for (k = 0; k < nn; k++) {
                if (ev->x >= cx &&
                    ev->x < cx + cs[k].width) {
                    on_name = (cs[k].field == TIKU_TRK_COL_NAME);
                    break;
                }
                cx += cs[k].width;
            }
        }
        {
            tiku_rect_t lr = browser_list_rect(app);
            int ox = 0, oy = 0;

            tiku_trk_poseview_origin(app->view, &ox, &oy);
            tiku_trk_poseview_click_at(app->view, row,
                ev->x - lr.x + ox,
                ev->y - lr.y + oy, mods);
        }
        /* PVS-010: a strict second click on this same pose
         * opens it. */
        if (twice) {
            tiku_rect_t lr = browser_list_rect(app);
            tiku_rect_t from;

            {
                int ax = 0, ay = 0, aw = 0, ah = 0;
                int ox = 0, oy = 0;

                if (tiku_trk_poseview_open_anim_rect(app->view,
                        row, app->row_h, &ax, &ay, &aw, &ah)) {
                    tiku_trk_poseview_origin(app->view, &ox, &oy);
                    from.x = lr.x + ax - ox;
                    from.y = lr.y + ay - oy;
                    from.w = aw;
                    from.h = ah;
                    browser_open_animation(app, from);
                }
            }
            {
                /* PVS-011: a double click INSIDE the Location
                 * column opens the folder the path names --
                 * the column's answer, not the row's. */
                const tiku_trk_column_t *cols = NULL;
                int nc = tiku_trk_poseview_columns(app->view,
                                                   &cols);
                int j, in_path = 0;

                for (j = 0; j < nc; j++) {
                    int cx = browser_col_x(app, &cols[j]);

                    if (cols[j].field == TIKU_TRK_COL_PATH &&
                        ev->x >= cx &&
                        ev->x < cx + cols[j].width) {
                        in_path = 1;
                    }
                }
                if (in_path && pose != NULL) {
                    char parent[TIKU_PATH_MAX];
                    char *slash;

                    snprintf(parent, sizeof parent, "%s",
                             pose->model.path);
                    slash = strrchr(parent, '/');
                    if (slash != NULL) {
                        if (slash == parent) {
                            parent[1] = '\0';
                        } else {
                            *slash = '\0';
                        }
                        (void)browser_navigate_to(app, parent, 1);
                    }
                } else {
                    browser_open_selection(app);
                }
            }
        }
        if (on_name && row >= 0) {
            /* Not started here: armed.  The rename begins only
             * once the double-click window has passed with no
             * further click (PVS-014, MA-043). */
            (void)tiku_trk_poseview_name_clicked(app->view, row, now);
        } else {
            tiku_trk_poseview_cancel_edit_wait(app->view);
        }
        app->press_at = now;
        app->press_x = ev->x;
        app->press_y = ev->y;
        app->press_row = row;
        app->press_down = 1;
        app->dirty = 1;
    }
    return 0;
}

/**
 * @brief a press in icon mode: an icon, a band, or the empty desk.  @return 1
 * when this was the command
 */
int
browser_press_icons(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    if (tiku_trk_poseview_mode(app->view) != TIKU_TRK_MODE_LIST) {
        tiku_rect_t lr = browser_list_rect(app);
        int lx = ev->x - lr.x, ly = ev->y - lr.y;
        tiku_trk_pose_t *hit =
            tiku_trk_poseview_pose_at(app->view, lx, ly);
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

        if (hit != NULL) {
            int idx = -1, k, cnt = tiku_trk_poseview_count(app->view);
            int twice;
            for (k = 0; k < cnt; k++) {
                if (tiku_trk_poseview_at(app->view, k) == hit) { idx = k; break; }
            }
            if (ev->button == 3u) {
                tiku_trk_poseview_cancel_edit_wait(app->view);
                (void)tiku_trk_poseview_context_click(app->view,
                                                      idx);
                app->press_at = now;
                app->press_x = ev->x;
                app->press_y = ev->y;
                app->press_row = idx;
                app->press_down = 1;
                (void)tiku_trk_click_press(&lp->click, NULL, 0, 0,
                                           3u, 0u, now);
                app->dirty = 1;
                return 1;
            }
            twice = tiku_trk_click_press(&lp->click, hit,
                ev->x, ev->y, ev->button,
                click_flags, now);
            tiku_trk_poseview_click_at(app->view, idx, lx, ly, mods);
            if (twice) {
                browser_open_selection(app);
            } else {
                /* A press on an icon begins a move; the drag
                 * carries the whole selection, as Tracker's does. */
                {
                    int k, cnt = tiku_trk_poseview_count(app->view);

                    for (k = 0; k < cnt; k++) {
                        tiku_trk_pose_t *q =
                            tiku_trk_poseview_at(app->view, k);

                        if (q != NULL && q->selected) {
                            tiku_trk_poseview_pose_location(
                                app->view, q, &q->drag_x0,
                                &q->drag_y0);
                        }
                    }
                }
                app->drag_active = 1;
                app->drag_x = lx;
                app->drag_y = ly;
            }
        } else {
            (void)tiku_trk_click_press(&lp->click, NULL, 0, 0,
                                       ev->button, 0u,
                                       now);
            if (ev->button == 3u) {
                /* The background asks for its own menu; the
                 * band is a selection gesture and belongs to
                 * the primary button (CW-042). */
                tiku_trk_poseview_cancel_edit_wait(app->view);
                app->press_at = now;
                app->press_x = ev->x;
                app->press_y = ev->y;
                app->press_row = -1;
                app->press_down = 1;
                app->dirty = 1;
                return 1;
            }
            /* Empty space starts a rubber band.  Without a
             * modifier the selection is cleared FIRST, so the
             * band's toggle rule starts from nothing; with one
             * the existing selection is kept and the sweep
             * toggles on top of it (PVS-027). */
            if (!(mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD))) {
                tiku_trk_poseview_select_none(app->view);
            }
            tiku_trk_poseview_cancel_edit_wait(app->view);
            tiku_trk_poseview_set_band(app->view, 1);
            app->band_active = 1;
            app->band_x0 = app->band_x1 = ev->x;
            app->band_y0 = app->band_y1 = ev->y;
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief the title strip: a column chosen, resized or dragged.  @return 1 when
 * this was the command
 */
int
browser_press_title(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    (void)lp;
    {
        int cx = 0, cy = 0;

        if (browser_title_hit(app, ev->x, ev->y, &cx,
                      &cy)) {
            int btn = (ev->button == 3) ? 2
                    : (ev->button == 2) ? 3 : 1;

            /* TS-015: whatever the press turns out to mean, an
             * edit in progress is committed first -- the strip
             * is outside the row being renamed. */
            browser_end_rename(app, 1);
            tiku_trk_poseview_cancel_edit_wait(app->view);
            {
                unsigned tf = tiku_trk_titleview_mouse_down(
                    &app->tv, cx, cy, btn, app->active,
                    tiku_trk_now());

                if ((tf & TIKU_TRK_TT_MENU) != 0u) {
                    tiku_trk_sc_ctx_t tc = browser_menu_ctx(app, 0u);

                    tiku_trk_cwmenu_rebuild(&app->menus, &tc);
                    app->bar_open = -1;
                    app->menus.bar.open = -1;
                    app->popup_sub = NULL;
                    app->popup = &app->menus.attributes;
                    app->popup_frame = tiku_menu_place(
                        app->popup, ev->x, ev->y,
                        (tiku_rect_t){ 0, 0, app->surf->w,
                                            app->surf->h });
                }
            }
            app->dirty = 1;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief the wheel, the scrollbars and the view button.  @return 1 when this
 * was the command
 */
int
browser_press_scroll(app_t *app, const tiku_event_t *ev, browser_loop_t *lp)
{
    (void)lp;
    if (ev->button == 4 || ev->button == 5) {
        if (ev->modifiers & TIKU_MOD_SHIFT) {
            /* TS-020: one value moves the titles and the rows
             * together, because they read the same one. */
            tiku_trk_poseview_set_hscroll(app->view,
                tiku_trk_poseview_hscroll(app->view) +
                ((ev->button == 4) ? -24 : 24));
        } else {
            tiku_trk_poseview_scroll_to(app->view,
                tiku_trk_poseview_scroll(app->view) +
                ((ev->button == 4) ? -3 : 3));
        }
        app->dirty = 1;
        return 1;
    }
    if (ev->x >= app->sb_rect.x &&
        ev->x < app->sb_rect.x + app->sb_rect.w &&
        ev->y >= app->sb_rect.y &&
        ev->y < app->sb_rect.y + app->sb_rect.h) {
        /* The bar takes it: an ARROW steps half a line, a
         * press in the track pages or takes hold of the
         * thumb, and while it is held the view follows the
         * bar rather than the other way round (PVL-051). */
        int at = ev->y - app->sb_rect.y;
        int btn = app->sb_rect.w;

        if (at < btn) {
            tiku_scroll_arrow_press(&app->sb, -1);
            tiku_trk_poseview_scroll_to(app->view,
                tiku_trk_poseview_scroll(app->view) -
                    tiku_scroll_line(&app->sb));
        } else if (at >= app->sb_rect.h - btn) {
            tiku_scroll_arrow_press(&app->sb, 1);
            tiku_trk_poseview_scroll_to(app->view,
                tiku_trk_poseview_scroll(app->view) +
                    tiku_scroll_line(&app->sb));
        } else {
            tiku_trk_poseview_scroll_to(app->view,
                tiku_scroll_press(&app->sb,
                    app->sb_rect.h - 2 * btn, at - btn));
        }
        app->dirty = 1;
        return 1;
    }
    if (app->viewbtn_rect.w > 0 && ev->x >= app->viewbtn_rect.x &&
        ev->x < app->viewbtn_rect.x + app->viewbtn_rect.w &&
        ev->y >= app->viewbtn_rect.y &&
        ev->y < app->viewbtn_rect.y + app->viewbtn_rect.h) {
        /* The gutter's view-mode popup (NeXT UIG ch7). */
        tiku_trk_sc_ctx_t vc = browser_menu_ctx(app, 0u);

        app->popup = tiku_trk_cwmenu_view_popup(&app->menus, &vc);
        app->popup_sub = NULL;
        app->popup_frame = tiku_menu_place(app->popup,
            app->viewbtn_rect.x, app->viewbtn_rect.y,
            (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
        app->dirty = 1;
        return 1;
    }
    if (app->hsb_rect.w > 0 && ev->x >= app->hsb_rect.x &&
        ev->x < app->hsb_rect.x + app->hsb_rect.w &&
        ev->y >= app->hsb_rect.y &&
        ev->y < app->hsb_rect.y + app->hsb_rect.h) {
        /* The horizontal bar, sharing the count row (TS-044):
         * the same arrows, track and thumb, sideways. */
        int at = ev->x - app->hsb_rect.x;
        int btn = app->hsb_rect.h;

        if (at < btn) {
            tiku_scroll_arrow_press(&app->hsb, -1);
            tiku_trk_poseview_set_hscroll(app->view,
                tiku_trk_poseview_hscroll(app->view) -
                    tiku_scroll_line(&app->hsb));
        } else if (at >= app->hsb_rect.w - btn) {
            tiku_scroll_arrow_press(&app->hsb, 1);
            tiku_trk_poseview_set_hscroll(app->view,
                tiku_trk_poseview_hscroll(app->view) +
                    tiku_scroll_line(&app->hsb));
        } else {
            tiku_trk_poseview_set_hscroll(app->view,
                tiku_scroll_press(&app->hsb,
                    app->hsb_rect.w - 2 * btn, at - btn));
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief The script's next step, made into the event it stands for: a key, a
 * move, a press, a release, a shot, a menu pick, or quit.  @return 0 to go on
 * with the event, 1 when the turn is over here, 2 when the pump loop is
 */
int
browser_script_step(app_t *app, browser_loop_t *lp, tiku_event_t *ev, int *have)
{
    if (lp->sc != NULL) {
        /* One instruction per turn, so the deferred work between
         * turns -- a stepped copy, the armed rename -- happens
         * where it happens on a real clock. */
        tiku_trk_script_step_t sp;
        tiku_trk_script_state_t sst;

        memset(&sst, 0, sizeof sst);
        sst.status = app->status;
        sst.title = app->title;
        sst.alert =
            tiku_trk_status_overlay_alert(&app->status_overlay)->open
                ? tiku_trk_status_overlay_alert(&app->status_overlay)
                      ->text
                : NULL;
        sst.scroll = tiku_trk_poseview_scroll(app->view);
        sst.rows = tiku_trk_poseview_count(app->view);
        {
            static char script_count[240];

            browser_count_label(app, browser_list_rect(app).w,
                                script_count, sizeof script_count);
            sst.count = script_count;
        }
        sst.renaming = app->editing;
        sst.location = tiku_trk_navigator_current(&app->nav);
        sst.nav_back = tiku_trk_navigator_can_back(&app->nav);
        sst.nav_forward = tiku_trk_navigator_can_forward(&app->nav);
        sst.nav_up = strcmp(sst.location, "/") != 0;
        sst.nav_visible = app->show_nav;
        sst.viewport_rows = browser_visible_rows(app);
        {
            const tiku_trk_column_t *script_cols = NULL;

            sst.columns = tiku_trk_poseview_columns(app->view,
                                                    &script_cols);
        }
        {
            static char labels[512];
            tiku_menu_t *open = (app->popup_sub != NULL)
                                          ? app->popup_sub : app->popup;
            int mi;

            labels[0] = '\0';
            for (mi = 0; open != NULL && mi < open->count; mi++) {
                size_t at = strlen(labels);

                snprintf(labels + at, sizeof labels - at, "%s|",
                         open->item[mi].label);
            }
            sst.menu = labels;
        }
        {
            int si, sn = tiku_trk_poseview_count(app->view);

            for (si = 0; si < sn; si++) {
                tiku_trk_pose_t *p =
                    tiku_trk_poseview_at(app->view, si);

                if (p != NULL && p->selected) {
                    if (sst.sel_name == NULL) {
                        sst.sel_name = p->model.name;
                    }
                    sst.selected++;
                }
            }
        }
        sst.row_point = browser_script_row_point;
        sst.pose_at = browser_script_pose_at;
        sst.pose_screen = browser_script_pose_screen;
        sst.pixel_at = browser_script_pixel_at;
        sst.narrate = browser_script_narrate;
        sst.say = browser_script_say;
        sst.say_ctx = app;
        sst.pose_ctx = app;
        if (!tiku_trk_script_pump(lp->sc, &sst, &sp)) {
            app->running = 0;
            return 2;          /* the loop is over: the script said so */
        }
        switch (sp.what) {
        case TIKU_TRK_SC_KEY:
            (*ev).type = TIKU_EVENT_KEY_DOWN;
            (*ev).key = sp.key;
            (*ev).modifiers = sp.modifiers;
            if (sp.key >= 0x20u && sp.key < 0x7fu) {
                (*ev).text[0] = (char)sp.key;
                if ((sp.modifiers & TIKU_MOD_SHIFT) != 0u &&
                    (*ev).text[0] >= 'a' && (*ev).text[0] <= 'z') {
                    (*ev).text[0] = (char)((*ev).text[0] - 'a' + 'A');
                }
            }
            (*have) = 1;
            break;
        case TIKU_TRK_SC_MOVE:
            (*ev).type = TIKU_EVENT_POINTER_MOVE;
            (*ev).x = sp.x;
            (*ev).y = sp.y;
            (*ev).modifiers = sp.modifiers;
            (*have) = 1;
            break;
        case TIKU_TRK_SC_PRESS:
        case TIKU_TRK_SC_RELEASE:
            (*ev).type = (sp.what == TIKU_TRK_SC_PRESS)
                          ? TIKU_EVENT_POINTER_DOWN
                          : TIKU_EVENT_POINTER_UP;
            (*ev).x = sp.x;
            (*ev).y = sp.y;
            (*ev).modifiers = sp.modifiers;
            (*ev).button = sp.button;
            (*ev).time_us = tiku_trk_now();
            (*have) = 1;
            break;
        case TIKU_TRK_SC_SHOT:
            browser_draw(app);
            (void)tiku_surface_png(app->surf, sp.path);
            break;
        case TIKU_TRK_SC_MENU_PICK: {
            tiku_menu_t *m = (app->popup_sub != NULL)
                                     ? app->popup_sub : app->popup;
            tiku_rect_t fr = (app->popup_sub != NULL)
                                      ? app->popup_sub_frame
                                      : app->popup_frame;
            int k;

            if (m == NULL) {
                fprintf(stderr, "script: no menu is open for '%s'\n",
                        sp.path);
                break;
            }
            for (k = 0; k < m->count; k++) {
                if (!m->item[k].separator &&
                    strstr(m->item[k].label, sp.path) != NULL) {
                    (*ev).type = TIKU_EVENT_POINTER_DOWN;
                    (*ev).x = fr.x + 8;
                    (*ev).y = fr.y +
                        tiku_menu_item_top(m, k) +
                        tiku_menu_item_height(m, k) / 2;
                    (*ev).button = 1u;
                    (*ev).time_us = tiku_trk_now();
                    (*have) = 1;
                    break;
                }
            }
            break;
        }
        case TIKU_TRK_SC_QUIT:
            app->running = 0;
            break;
        default:
            break;
        }
    } else if (tiku_host_poll(app->host, ev)) {
        (*have) = 1;
    }
    return 0;
}
