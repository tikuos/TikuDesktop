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
    desktop_damage_all(app);
}

/** @brief One event to the surfaces, and what they changed to the frame. */
static void
deliver(desktop_t *app, const tiku_event_t *ev)
{
    tiku_rect_t taken;

    (void)tiku_trk_responders_deliver(&app->responders, ev, &taken);
    desktop_take_damage(app, taken);
}

/** @brief A button went down: the surface under the pointer answers. */
void
desktop_pointer_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
    deliver(app, ev);
}

/**
 * @brief The pointer moved: where it is now, what the cursor says, and the
 *        surfaces' own tracking.
 */
void
desktop_pointer_move(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    int was_x = app->ptr_x, was_y = app->ptr_y;

    (void)lp;
    app->ptr_x = ev->x;
    app->ptr_y = ev->y;
    desktop_pointer_says(app, ev->x, ev->y);
    deliver(app, ev);
    if (app->qdrag_on) {
        /* The icon in flight: where it was and where it is, nothing else. */
        desktop_damage(app, tiku_rect_union(
            (tiku_rect_t){ was_x - 16, was_y - 16, 32, 32 },
            (tiku_rect_t){ ev->x - 16, ev->y - 16, 32, 32 }));
    }
}

/** @brief A button came up: the holder of the capture answers first. */
void
desktop_pointer_up(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
    deliver(app, ev);
    /* The corner let go: the arrow back. */
    desktop_pointer_says(app, ev->x, ev->y);
}

/**
 * @brief A key went down or up: the surfaces answer in their order, the
 *        topmost first.
 */
void
desktop_key_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
    deliver(app, ev);
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
            desktop_damage_all(app);
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
        desktop_damage_all(app);
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
        desktop_damage_all(app);
    }
        if (app->shot_pending) {
        /* Redraw without the menu, then capture. */
        desktop_damage_all(app);
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
                desktop_damage_all(app);
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
