/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_press.c - what a press, a drag and a key do to the bar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/* Used before they are defined. */
int db_click_list(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                  int button, int clicks, tiku_trk_db_pick_t *pick,
                  int secondary, int *ry);
int db_click_apps_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick);
int db_click_leaf_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick,
                       const tiku_font_t *f);
int db_click_win_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                      int button, int clicks, tiku_trk_db_pick_t *pick,
                      const tiku_font_t *f);
int db_click_clock_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                        int button, int clicks, tiku_trk_db_pick_t *pick);
int db_click_calendar(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                      int button, int clicks, tiku_trk_db_pick_t *pick);
/* End of the forward declarations. */

int
tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick)
{
    int r;
    int secondary = (button == 3);
    const tiku_font_t *f = tiku_font_plain();
    int i, j, ry;

    if (db == NULL || pick == NULL) {
        return 0;
    }
    memset(pick, 0, sizeof *pick);

    /* An open popup owns the next click wherever it lands. */
    if ((r = db_click_calendar(db, x, y, mods, button, clicks, pick)) >= 0) {
        return r;
    }
    if ((r = db_click_clock_menu(db, x, y, mods, button, clicks, pick)) >= 0) {
        return r;
    }
    if ((r = db_click_win_menu(db, x, y, mods, button, clicks, pick, f)) >= 0) {
        return r;
    }
    if ((r = db_click_leaf_menu(db, x, y, mods, button, clicks, pick,
        f)) >= 0) {
        return r;
    }
    if ((r = db_click_apps_menu(db, x, y, mods, button, clicks, pick)) >= 0) {
        return r;
    }
    if (!tiku_trk_deskbar_hit(db, x, y)) {
        return 0;
    }
    if (!tiku_trk_deskbar_vertical(db->dock)) {
        /* The horizontal strip: the leaf keeps its own width, and the
         * rest of the bar is application buttons (DB-033). */
        if (x < db->frame.x + 2 + HLEAF_W) {
            db_build_leaf(db);
            db->menu_open = 1;
            db_reset_menu_hots(db);
            return 1;
        }
        if (x >= db->frame.x + db->frame.w - SWITCH_W - 10) {
            /* The strip's far end is the theme switch: the whole
             * reserved end answers, not only the painted track. */
            pick->what = TIKU_TRK_DB_TOGGLE_DARK;
            return 1;
        }
        if (!tiku_trk_deskbar_mini(db->dock) && db->hbutton_w > 0) {
            int astep = ((mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD |
                                  TIKU_MOD_OPTION)) != 0u) ? 3 : 1;
            int idx;

            if (db->hoverflow && x < db->hleft) {
                (void)tiku_trk_deskbar_scroll(db, -astep);
                return 1;
            }
            idx = (db->hoverflow ? db->scroll : 0) +
                  (x - db->hleft) / db->hbutton_w;
            if (idx >= 0 && idx < db->team_count &&
                x >= db->hleft &&
                x < db->hleft + (idx + 1 -
                    (db->hoverflow ? db->scroll : 0)) * db->hbutton_w) {
                db_team_action(db, db->order[idx], mods, button, clicks,
                            pick);
                return 1;
            }
            if (db->hoverflow) {
                (void)tiku_trk_deskbar_scroll(db, astep);
                return 1;
            }
        }
        return 1;
    }
    if (y < db->frame.y + db_leaf_row() + 2) {
        db_build_leaf(db);         /* from disk, every time (DB-005) */
        db->menu_open = 1;
        db_reset_menu_hots(db);
        return 1;
    }
    if (tiku_trk_deskbar_mini(db->dock) &&
        y < db->frame.y + db_leaf_row() + 4 + db_team_row()) {
        /* The second icon: the running-apps menu (DB-037). */
        db->apps_menu_open = 1;
        db_reset_menu_hots(db);
        return 1;
    }
    {
        /* The theme switch's strip, checked before anything below it so
         * the collapsed dock reaches it too.  The whole row answers: a
         * 26 px track is a target for a pointer, the row is one for a
         * finger. */
        tiku_rect_t sw = tiku_trk_deskbar_switch_rect(db);

        if (!secondary && y >= sw.y - (SWITCH_ROW - SWITCH_H) / 2 &&
            y < sw.y + sw.h + (SWITCH_ROW - SWITCH_H) / 2) {
            pick->what = TIKU_TRK_DB_TOGGLE_DARK;
            return 1;
        }
    }
    if (tiku_trk_deskbar_mini(db->dock)) {
        return 1;
    }

    /* The scroll arrows first: while the list overflows, its top and
     * bottom edges step it -- three rows at a time with a modifier held
     * (DB-034). */
    if (db->overflow) {
        int astep = ((mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD |
                              TIKU_MOD_OPTION)) != 0u) ? 3 : 1;
        int ly = db_list_y(db);
        int lb = db->frame.y + db->frame.h -
                 (db->tray_count * TRAY_ROW) -
                 (db->settings.show_clock ? db_clock_row() : 0) - PAD;

        if (y >= ly && y < ly + 8) {
            (void)tiku_trk_deskbar_scroll(db, -astep);
            return 1;
        }
        if (y >= lb - 8 && y < lb) {
            (void)tiku_trk_deskbar_scroll(db, astep);
            return 1;
        }
    }

    if ((r = db_click_list(db, x, y, mods, button, clicks, pick, secondary,
        &ry)) >= 0) {
        return r;
    }
    /* Secondary click on the tray opens clock options. */
    if (db->settings.show_clock && y >= db->frame.y + db->frame.h -
        db_clock_row() - PAD) {
        if (secondary) {
            db->clock_menu_open = 1;
        db_reset_menu_hots(db);
        } else {
            time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
            struct tm tmv;

            (void)localtime_r(&now, &tmv);
            db->calendar_year = 1900 + tmv.tm_year;
            db->calendar_month = 1 + tmv.tm_mon;
            db->calendar_day = tmv.tm_mday;
            db->calendar_open = 1;
        }
    } else if (secondary && y >= db->frame.y + db->frame.h -
               db_clock_row() - PAD - TRAY_ROW) {
        db->clock_menu_open = 1;
        db_reset_menu_hots(db);
    }
    return 1;
}

void
tiku_trk_deskbar_calendar_date(const tiku_trk_deskbar_t *db, int *year,
                               int *month, int *day)
{
    if (year != NULL) { *year = db != NULL ? db->calendar_year : 0; }
    if (month != NULL) { *month = db != NULL ? db->calendar_month : 0; }
    if (day != NULL) { *day = db != NULL ? db->calendar_day : 0; }
}

/**
 * @brief a click on the application list: the expander, the name, a window row.
 * @return -1 when the click was not here, else what the click did
 */
int
db_click_list(tiku_trk_deskbar_t *db, int x, int y, unsigned mods, int button,
              int clicks, tiku_trk_db_pick_t *pick, int secondary, int *ry)
{
    int i;
    int j;

    /* The application list: the expander toggles, the name opens the window
     * menu, a window entry raises (or minimises with Ctrl). */
    (*ry) = db_list_y(db);
    for (i = 0; i < db->team_count; i++) {
        int team = db->order[i];
        tiku_trk_team_t *t = &db->teams[team];
        int team_h = (db->settings.icon_size + 4 > db_team_row())
                         ? db->settings.icon_size + 4 : db_team_row();

        /* The seam belongs to neither row; a click on it is a miss, which
         * is why hit-testing uses the row height and stepping uses the
         * pitch. */
        if (y >= (*ry) && y < (*ry) + team_h) {
            if (db->settings.show_expander &&
                x > db->frame.x + db->frame.w - EXPANDER_W - 6) {
                /* Armed, not flipped: the arrow turns down at once and
                 * the state commits on release over the same strip
                 * (DB-022). */
                db->expander_press = team;
            } else {
                /* The chord map, shared with the horizontal strip: the
                 * grip kills, middle launches, CMD fronts, CMD+Shift
                 * minimizes, a double activates (DB-019/020/021). */
                db_team_action(db, team, mods, button, clicks, pick);
            }
            return 1;
        }
        (*ry) += team_h + SEAM;
        if (!t->expanded) {
            continue;
        }
        for (j = 0; j < t->win_count; j++) {
            if (y >= (*ry) && y < (*ry) + db_win_row()) {
                pick->tag = t->wins[j].tag;
                if (secondary || (button == 2 &&
                                  (mods & TIKU_TRK_MOD_SHIFT) != 0u)) {
                    /* Shift+middle on a window entry closes that window,
                     * the same request the secondary click makes
                     * (DB-021). */
                    pick->what = TIKU_TRK_DB_CLOSE_WINDOW;
                } else if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
                           (mods & TIKU_MOD_OPTION) != 0u) {
                    /* Option means the whole application, on the gesture
                     * that takes one window down (CW-014). */
                    pick->what = TIKU_TRK_DB_HIDE_ALL;
                } else if (mods & TIKU_TRK_MOD_CMD) {
                    pick->what = TIKU_TRK_DB_MINIMIZE_WINDOW;
                } else {
                    pick->what = TIKU_TRK_DB_ACTIVATE_WINDOW;
                }
                return 1;
            }
            (*ry) += db_win_row() + SEAM;
        }
    }
    return -1;
}

/**
 * @brief a click while the applications menu is open.  @return -1 when the
 * click was not here, else what the click did
 */
int
db_click_apps_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                   int button, int clicks, tiku_trk_db_pick_t *pick)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->apps_menu_open) {
        const tiku_font_t *fm = tiku_font_plain();
        int rowh = fm->height + 6;
        int n = (db->team_count > 0) ? db->team_count : 1;
        tiku_rect_t ab = { db->frame.x + 2,
                                db->frame.y + 2 + db_leaf_row() + 2,
                                db->frame.w - 4, db_team_row() };
        tiku_rect_t am;
        int idx;

        am.w = 176;
        am.h = n * rowh + 4;
        am.y = ab.y;
        am.x = (db->frame.x > 0) ? db->frame.x - am.w - 2
                                 : db->frame.x + db->frame.w + 2;
        idx = (y - am.y - 2) / rowh;
        db->apps_menu_open = 0;
        if (x >= am.x && x < am.x + am.w && y >= am.y &&
            y < am.y + am.h && idx >= 0 && idx < db->team_count) {
            /* Choosing an application activates it (DB-037); the empty
             * menu's one row is said, not chosen. */
            pick->what = TIKU_TRK_DB_ACTIVATE_TEAM;
            pick->team = db->order[idx];
        }
        return 1;
    }
    return -1;
}

/**
 * @brief a click while the leaf menu is open: its rows, its submenus, and
 * outside it.  @return -1 when the click was not here, else what the click did
 */
int
db_click_leaf_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                   int button, int clicks, tiku_trk_db_pick_t *pick,
                   const tiku_font_t *f)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->menu_open) {
        int rowh = f->height + 6;
        tiku_rect_t m = db_leaf_menu_rect(db, rowh, db->leaf_count);
        int idx = (y - m.y - 2) / rowh;

        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);
            int si = (y - sm.y - 2) / rowh;

            if (x >= sm.x && x < sm.x + sm.w && y >= sm.y &&
                y < sm.y + sm.h && si >= 0 && si < db->sub_count) {
                if (db->sub[si].kind == TIKU_TRK_DB_ITEM_POWER_OFF) {
                    pick->what = TIKU_TRK_DB_POWER_OFF;
                } else if (db->sub[si].kind == TIKU_TRK_DB_ITEM_RESTART) {
                    pick->what = TIKU_TRK_DB_RESTART;
                } else if (db->sub[si].kind == TIKU_TRK_DB_ITEM_APP) {
                    pick->what = TIKU_TRK_DB_LAUNCH_APP;
                    snprintf(pick->path, sizeof pick->path, "%s",
                             db->sub[si].path);
                } else if (db->sub[si].kind == TIKU_TRK_DB_ITEM_PREFS) {
                    pick->what = TIKU_TRK_DB_PREFERENCES;
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_TRACKER_PREFS) {
                    pick->what = TIKU_TRK_DB_TRACKER_PREFERENCES;
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_DESKTOP_PREFS) {
                    pick->what = TIKU_TRK_DB_DESKTOP_PREFERENCES;
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_FONT_PREFS) {
                    pick->what = TIKU_TRK_DB_FONT_PREFERENCES;
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_SOUND_PREFS) {
                    pick->what = TIKU_TRK_DB_SOUND_PREFERENCES;
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_DEVICE_PREFS) {
                    pick->what = TIKU_TRK_DB_DEVICE_PREFERENCES;
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_STARTUP_PREFS) {
                    pick->what = TIKU_TRK_DB_STARTUP_PREFERENCES;
                } else if (db->sub[si].kind == TIKU_TRK_DB_ITEM_PANEL) {
                    pick->what = TIKU_TRK_DB_OPEN_PANEL;
                    snprintf(pick->path, sizeof pick->path, "%s",
                             db->sub[si].path);
                } else if (db->sub[si].kind ==
                           TIKU_TRK_DB_ITEM_SHOW_REPLICANTS) {
                    /* A toggle is not a departure: the menu stays up
                     * with the row saying the other thing now. */
                    int of = db->sub_of;

                    db->show_replicants = !db->show_replicants;
                    db_open_sub(db, of);
                    return 1;
                } else {
                    pick->what = TIKU_TRK_DB_OPEN_PATH;
                    snprintf(pick->path, sizeof pick->path, "%s",
                             db->sub[si].path);
                }
                db->menu_open = 0;
                db->sub_of = -1;
                return 1;
            }
        }
        if (x >= m.x && x < m.x + m.w && idx >= 0 && idx < db->leaf_count) {
            const tiku_trk_db_item_t *it = &db->leaf[idx];

            switch (it->kind) {
            case TIKU_TRK_DB_ITEM_DIR:
            case TIKU_TRK_DB_ITEM_RECENT:
            case TIKU_TRK_DB_ITEM_APPS:
            case TIKU_TRK_DB_ITEM_PROGRAMMING:
            case TIKU_TRK_DB_ITEM_SETTINGS:
                /* Expanding is not choosing: the menu stays up, with the
                 * row's contents beside it. */
                db_open_sub(db, idx);
                return 1;
            case TIKU_TRK_DB_ITEM_DISABLED:
            case TIKU_TRK_DB_ITEM_SEP:
                return 1;       /* said, not chosen */
            case TIKU_TRK_DB_ITEM_ABOUT:
                pick->what = TIKU_TRK_DB_SHOW_ABOUT;
                break;
            case TIKU_TRK_DB_ITEM_FIND:
                pick->what = TIKU_TRK_DB_FIND;
                break;
            case TIKU_TRK_DB_ITEM_HELP:
                pick->what = TIKU_TRK_DB_HELP;
                break;
            case TIKU_TRK_DB_ITEM_PREFS:
                pick->what = TIKU_TRK_DB_PREFERENCES;
                break;
            case TIKU_TRK_DB_ITEM_TRACKER_PREFS:
                pick->what = TIKU_TRK_DB_TRACKER_PREFERENCES;
                break;
            case TIKU_TRK_DB_ITEM_SHOW_REPLICANTS:
                db->show_replicants = !db->show_replicants;
                db_build_leaf(db);
                return 1;
            case TIKU_TRK_DB_ITEM_QUIT:
                if (x >= m.x + m.w - 20) {
                    /* The arrow: the immediate pair beside the row,
                     * expanding rather than choosing (DB-006). */
                    db_open_sub(db, idx);
                    return 1;
                }
                pick->what = TIKU_TRK_DB_QUIT;
                break;
            case TIKU_TRK_DB_ITEM_APP:
                pick->what = TIKU_TRK_DB_LAUNCH_APP;
                snprintf(pick->path, sizeof pick->path, "%s", it->path);
                break;
            case TIKU_TRK_DB_ITEM_PATH:
                pick->what = TIKU_TRK_DB_OPEN_PATH;
                snprintf(pick->path, sizeof pick->path, "%s", it->path);
                break;
            case TIKU_TRK_DB_ITEM_PANEL:
                pick->what = TIKU_TRK_DB_OPEN_PANEL;
                snprintf(pick->path, sizeof pick->path, "%s", it->path);
                break;
            default:
                break;
            }
        }
        db->menu_open = 0;
        db->sub_of = -1;
        return 1;
    }
    return -1;
}

/**
 * @brief a click while an application's window menu is open.  @return -1 when
 * the click was not here, else what the click did
 */
int
db_click_win_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                  int button, int clicks, tiku_trk_db_pick_t *pick,
                  const tiku_font_t *f)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->win_menu_team >= 0) {
        int rowh = f->height + 6;
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);
        tiku_rect_t m = db_win_menu_rect(db, n, rowh);
        int idx = (y - m.y - 2) / rowh;
        int team = db->win_menu_team;

        db->win_menu_team = -1;
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h &&
            idx >= 0 && idx < n && enabled[idx]) {
            if (db->teams[team].win_count == 0) {
                /* The empty-application variant: its one live row asks
                 * the application to QUIT, politely (DB-028). */
                if (idx == 1) {
                    pick->what = TIKU_TRK_DB_QUIT_TEAM;
                    pick->team = team;
                }
            }
            else if (idx == 0) { pick->what = TIKU_TRK_DB_HIDE_ALL; }
            else if (idx == 1) { pick->what = TIKU_TRK_DB_SHOW_ALL; }
            else if (idx == 3) { pick->what = TIKU_TRK_DB_CLOSE_ALL; }
        }
        return 1;
    }
    return -1;
}

/**
 * @brief a click while the clock menu is open.  @return -1 when the click was
 * not here, else what the click did
 */
int
db_click_clock_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                    int button, int clicks, tiku_trk_db_pick_t *pick)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->clock_menu_open) {
        tiku_rect_t m = { db->frame.x - 130, db->frame.y + db->frame.h -
                               db_clock_row() - 32, 170, 4 * 28 };
        int inside = x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h;
        int item = inside ? (y - m.y) / 28 : -1;
        db->clock_menu_open = 0;
        if (inside && item == 0) {
            pick->what = db->settings.show_clock ? TIKU_TRK_DB_CLOCK_HIDE
                                                  : TIKU_TRK_DB_CLOCK_SHOW;
        } else if (inside && item == 1) {
            pick->what = TIKU_TRK_DB_CLOCK_SECONDS;
        } else if (inside && item == 2) {
            pick->what = TIKU_TRK_DB_CLOCK_DAY;
        } else if (inside && item == 3) {
            pick->what = TIKU_TRK_DB_CLOCK_ZONE;
        }
        return 1;
    }
    return -1;
}

/**
 * @brief a click while the calendar is open.  @return -1 when the click was not
 * here, else what the click did
 */
int
db_click_calendar(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                  int button, int clicks, tiku_trk_db_pick_t *pick)
{
    (void)mods;
    (void)button;
    (void)clicks;
    (void)pick;
    if (db->calendar_open) {
        /* A second click on the clock reactivates the existing popup. */
        if (db->settings.show_clock &&
            x >= db->frame.x && x < db->frame.x + db->frame.w &&
            y >= db->frame.y + db->frame.h - db_clock_row() - PAD) {
            return 1;
        }
        {
            tiku_rect_t c = db_calendar_rect(db);
            int col, rowi, first, day;
            struct tm tmv;
            time_t shown = 0;

            memset(&tmv, 0, sizeof tmv);
            tmv.tm_year = db->calendar_year - 1900;
            tmv.tm_mon = db->calendar_month - 1;
            tmv.tm_mday = 1;
            tmv.tm_isdst = -1;
            shown = mktime(&tmv);
            (void)localtime_r(&shown, &tmv);
            first = tmv.tm_wday;
            if (x >= c.x + 4 && x < c.x + 48 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, -1, 0);
                return 1;
            }
            if (x >= c.x + 48 && x < c.x + 92 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, 1, 0);
                return 1;
            }
            if (x >= c.x + 94 && x < c.x + 138 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, 0, -1);
                return 1;
            }
            if (x >= c.x + 138 && x < c.x + 182 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, 0, 1);
                return 1;
            }
            if (x >= c.x + 6 && x < c.x + 181 && y >= c.y + 22 &&
                y < c.y + 142) {
                col = (x - c.x - 6) / 25;
                rowi = (y - c.y - 22) / 20;
                day = rowi * 7 + col - first + 1;
                if (day >= 1 && day <= db_calendar_days(db->calendar_year,
                                                     db->calendar_month)) {
                    db->calendar_day = day;
                    return 1;
                }
            }
        }
        db->calendar_open = 0;
        return 1;
    }
    return -1;
}
