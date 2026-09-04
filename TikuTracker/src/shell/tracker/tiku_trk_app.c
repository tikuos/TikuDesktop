/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app.c - the window: a real Tracker list view over a real backend.
 *
 * One X11 window drawn entirely by us, wired to the pose engine: the title bar
 * sorts, rows select with Tracker's own rules, the keyboard navigates, and a
 * device's values update live because the loop pumps the backend between
 * events instead of blocking on X.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/** @brief Real art for menu items, dimmed for a disabled one (IV-054). */
int
browser_paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    return dimmed
        ? tiku_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_C_PANEL)
        : tiku_icon_paint(s, icon, x, y, size);
}

/*---------------------------------------------------------------------------*/
/* the run                                                                   */
/*---------------------------------------------------------------------------*/

static int
tracker_run(int argc, char **argv)
{
    app_t a;
    browser_options_t opt;
    browser_loop_t lp;
    int code;

    memset(&a, 0, sizeof a);
    memset(&opt, 0, sizeof opt);
    opt.baud = 115200;
    memset(&lp, 0, sizeof lp);
    (void)tiku_strings_init(NULL, NULL);
    tiku_trk_cwmenu_init(&a.menus);
    a.bar_open = -1;
    a.show_nav = 1;
    /* Zero is a valid row, so the "nothing to select" state has to be set
     * rather than zeroed: left at 0 it would re-select the first row after
     * every operation (FS-050). */
    a.select_next = -1;
    code = browser_parse_args(argc, argv, &opt);
    if (code >= 0) {
        return code;
    }

    code = browser_boot(&a, &opt, &lp);
    if (code >= 0) {
        return code;
    }

    while (a.running) {
        browser_sync_navigator_preference(&a, 0);
        /* Events first, then the device: the window stays responsive while a
         * slow link is being read, which is the whole point of pumping here
         * rather than blocking on either one. */
        for (;;) {
            tiku_event_t ev;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (lp.sc != NULL) {
                /* One instruction per turn, so the deferred work between
                 * turns -- a stepped copy, the armed rename -- happens
                 * where it happens on a real clock. */
                tiku_trk_script_step_t sp;
                tiku_trk_script_state_t sst;

                memset(&sst, 0, sizeof sst);
                sst.status = a.status;
                sst.title = a.title;
                sst.alert =
                    tiku_trk_status_overlay_alert(&a.status_overlay)->open
                        ? tiku_trk_status_overlay_alert(&a.status_overlay)
                              ->text
                        : NULL;
                sst.scroll = tiku_trk_poseview_scroll(a.view);
                sst.rows = tiku_trk_poseview_count(a.view);
                {
                    static char script_count[240];

                    browser_count_label(&a, browser_list_rect(&a).w, script_count,
                                sizeof script_count);
                    sst.count = script_count;
                }
                sst.renaming = a.editing;
                sst.location = tiku_trk_navigator_current(&a.nav);
                sst.nav_back = tiku_trk_navigator_can_back(&a.nav);
                sst.nav_forward = tiku_trk_navigator_can_forward(&a.nav);
                sst.nav_up = strcmp(sst.location, "/") != 0;
                sst.nav_visible = a.show_nav;
                sst.viewport_rows = browser_visible_rows(&a);
                {
                    const tiku_trk_column_t *script_cols = NULL;

                    sst.columns = tiku_trk_poseview_columns(a.view,
                                                            &script_cols);
                }
                {
                    static char labels[512];
                    tiku_menu_t *open = (a.popup_sub != NULL)
                                                  ? a.popup_sub : a.popup;
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
                    int si, sn = tiku_trk_poseview_count(a.view);

                    for (si = 0; si < sn; si++) {
                        tiku_trk_pose_t *p =
                            tiku_trk_poseview_at(a.view, si);

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
                sst.say_ctx = &a;
                sst.pose_ctx = &a;
                if (!tiku_trk_script_pump(lp.sc, &sst, &sp)) {
                    a.running = 0;
                    break;
                }
                switch (sp.what) {
                case TIKU_TRK_SC_KEY:
                    ev.type = TIKU_EVENT_KEY_DOWN;
                    ev.key = sp.key;
                    ev.modifiers = sp.modifiers;
                    if (sp.key >= 0x20u && sp.key < 0x7fu) {
                        ev.text[0] = (char)sp.key;
                        if ((sp.modifiers & TIKU_MOD_SHIFT) != 0u &&
                            ev.text[0] >= 'a' && ev.text[0] <= 'z') {
                            ev.text[0] = (char)(ev.text[0] - 'a' + 'A');
                        }
                    }
                    have = 1;
                    break;
                case TIKU_TRK_SC_MOVE:
                    ev.type = TIKU_EVENT_POINTER_MOVE;
                    ev.x = sp.x;
                    ev.y = sp.y;
                    ev.modifiers = sp.modifiers;
                    have = 1;
                    break;
                case TIKU_TRK_SC_PRESS:
                case TIKU_TRK_SC_RELEASE:
                    ev.type = (sp.what == TIKU_TRK_SC_PRESS)
                                  ? TIKU_EVENT_POINTER_DOWN
                                  : TIKU_EVENT_POINTER_UP;
                    ev.x = sp.x;
                    ev.y = sp.y;
                    ev.modifiers = sp.modifiers;
                    ev.button = sp.button;
                    ev.time_us = tiku_trk_now();
                    have = 1;
                    break;
                case TIKU_TRK_SC_SHOT:
                    browser_draw(&a);
                    (void)tiku_surface_png(a.surf, sp.path);
                    break;
                case TIKU_TRK_SC_MENU_PICK: {
                    tiku_menu_t *m = (a.popup_sub != NULL)
                                             ? a.popup_sub : a.popup;
                    tiku_rect_t fr = (a.popup_sub != NULL)
                                              ? a.popup_sub_frame
                                              : a.popup_frame;
                    int k;

                    if (m == NULL) {
                        fprintf(stderr, "script: no menu is open for '%s'\n",
                                sp.path);
                        break;
                    }
                    for (k = 0; k < m->count; k++) {
                        if (!m->item[k].separator &&
                            strstr(m->item[k].label, sp.path) != NULL) {
                            ev.type = TIKU_EVENT_POINTER_DOWN;
                            ev.x = fr.x + 8;
                            ev.y = fr.y +
                                tiku_menu_item_top(m, k) +
                                tiku_menu_item_height(m, k) / 2;
                            ev.button = 1u;
                            ev.time_us = tiku_trk_now();
                            have = 1;
                            break;
                        }
                    }
                    break;
                }
                case TIKU_TRK_SC_QUIT:
                    a.running = 0;
                    break;
                default:
                    break;
                }
            } else if (tiku_host_poll(a.host, &ev)) {
                have = 1;
            }
            if (!have) {
                break;
            }

            switch (ev.type) {
            case TIKU_EVENT_RESIZE:
                if (ev.width > 0 && ev.height > 0 &&
                    (ev.width != a.surf->w || ev.height != a.surf->h)) {
                    (void)tiku_surface_resize(a.surf, ev.width,
                                                   ev.height,
                                                   TIKU_C_PANEL);
                    /* The overlay centres its alert on these; stale ones
                     * would put the question off a shrunken window. */
                    a.status_overlay.screen_w = ev.width;
                    a.status_overlay.screen_h = ev.height;
                    {
                        /* Both axes re-anchor to the new window: the
                         * vertical clamp always ran through visible_rows,
                         * and the horizontal one runs here (PVL-058). */
                        tiku_rect_t lr = browser_list_rect(&a);

                        tiku_trk_poseview_set_viewport(a.view, lr.w - 4,
                                                       lr.h - 4);
                        tiku_trk_poseview_set_hscroll(a.view,
                            tiku_trk_poseview_hscroll(a.view));
                    }
                }
                a.dirty = 1;
                break;
            case TIKU_EVENT_EXPOSE:
                a.dirty = 1;
                break;
            case TIKU_EVENT_KEY_DOWN:
                tiku_trk_loop_note_activity(a.loop);
                browser_handle_key(&a, &ev);
                break;
            case TIKU_EVENT_POINTER_DOWN:
                browser_pointer_down(&a, &ev, &lp);
                break;
            case TIKU_EVENT_POINTER_MOVE:
                browser_pointer_move(&a, &ev, &lp);
                break;
            case TIKU_EVENT_POINTER_UP:
                browser_pointer_up(&a, &ev, &lp);
                break;
            case TIKU_EVENT_ACTIVATED:
            case TIKU_EVENT_DEACTIVATED:
                /* PVS-023: the selection stops being PAINTED while the
                 * window is inactive, and the pivot is parked so a
                 * shift-click on return still extends from where the user
                 * left off.  A pending rename does not survive the trip. */
                a.active = (ev.type == TIKU_EVENT_ACTIVATED);
                tiku_trk_poseview_window_activated(a.view, a.active);
                if (!a.active) {
                    tiku_trk_poseview_cancel_edit_wait(a.view);
                    if (a.editing) {
                        browser_end_rename(&a, 1);   /* deactivation COMMITS */
                    }
                }
                a.dirty = 1;
                break;
            case TIKU_EVENT_CLOSE:
                a.running = 0;
                break;
            default:
                break;
            }
            if (lp.sc != NULL) {
                break;              /* one instruction per turn */
            }
        }
        browser_turn_end(&a, &opt, &lp);
    }

    return browser_teardown(&a, &lp);
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker.browser",
    .name = "TikuTracker",
    .run = tracker_run
};
