/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_draw.c - drawing the bar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/* Used before they are defined. */
static void fit_label(const tiku_font_t *f, const char *src, int budget,
                      char *out, size_t max);
void db_draw_calendar(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                      const tiku_font_t *f, const tiku_font_t *fb,
                      tiku_rgb_t dark);
void db_draw_tip(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_switch_strip(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_clock_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_win_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                      const tiku_font_t *f);
void db_draw_leaf_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                       const tiku_font_t *f);
void db_draw_clock(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                   const tiku_font_t *f, int *y);
void db_draw_tray(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f, int *y);
void db_draw_teams(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                   const tiku_font_t *f, int team_h, int *y);
void db_draw_mini(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f);
void db_draw_leaf(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *fb, tiku_rgb_t dark, tiku_rect_t *leaf);
void db_draw_frame(tiku_trk_deskbar_t *db, tiku_surface_t *s, int *rows);
/* End of the forward declarations. */

/** @brief A window's state icon: filled when shown, hollow when minimised. */
static void
draw_win_icon(tiku_surface_t *s, int x, int y, int minimized,
              int current)
{
    tiku_rect_t b = { x, y, 9, 9 };
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);

    tiku_fill(s, b, minimized ? TIKU_C_PANEL : TIKU_C_DOC);
    tiku_frame(s, b, dark);
    if (!minimized) {
        /* A shown window carries a title bar on its glyph; a minimised one
         * is an empty outline, so the two are told apart at a glance. */
        tiku_hline(s, b.x + 1, b.y + 1, b.w - 2, TIKU_C_TAB);
        tiku_hline(s, b.x + 1, b.y + 2, b.w - 2, TIKU_C_TAB);
    }
    if (current) {
        tiku_frame(s, (tiku_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_C_SELECT);
    }
}

/** @brief The expander triangle: right when collapsed, down when expanded. */
/**
 * @brief Where the leaf menu goes, given which edge the bar is docked to.
 *
 * Docked LEFT the panel has to fall to the right of the bar; the fixed
 * "left of the bar" offset put it at a negative x, i.e. off the screen
 * entirely (DB-010).
 */

tiku_rect_t
db_leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows)
{
    tiku_rect_t m;

    m.w = 176;
    m.h = rows * rowh + 4;
    m.y = db->frame.y + db_leaf_row() + 2;
    m.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           db->dock == TIKU_TRK_DB_LEFT_MINI)
          ? db->frame.x + db->frame.w + 2
          : db->frame.x - m.w - 2;
    if (m.x < 0) {
        m.x = 0;
    }
    return m;
}

void
tiku_trk_deskbar_set_menu_icon_painter(tiku_trk_deskbar_t *db,
                                       tiku_trk_db_icon_fn fn, void *context)
{
    if (db != NULL) {
        db->menu_icon = fn;
        db->menu_icon_ctx = context;
    }
}

/** @brief The menu's icon slot, handed to the painter the shell registered. */
static void
relay_menu_icon(tiku_surface_t *s, int index, int x, int y, int size,
                void *context)
{
    struct menu_icon_relay *relay = context;

    (void)relay->db->menu_icon(relay->db->menu_icon_ctx, s,
                               &relay->items[index], x, y, size);
}

int
tiku_trk_deskbar_release(tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_font_t *font = tiku_font_plain();
    int team = (db != NULL) ? db->expander_press : -1;
    int i, ry, changed = 0;

    if (db == NULL || team < 0) {
        return 0;
    }
    db->expander_press = -1;
    /* Still over the same team's strip: the press commits (DB-022). */
    if (db->settings.show_expander &&
        x > db->frame.x + db->frame.w - EXPANDER_W - 6) {
        ry = db_list_y(db);
        for (i = 0; i < db->team_count; i++) {
            int at = db->order[i];
            tiku_trk_team_t *t = &db->teams[at];
            int team_h = (db->settings.icon_size + 4 > db_team_row())
                             ? db->settings.icon_size + 4 : db_team_row();

            if (y >= ry && y < ry + team_h) {
                if (at == team) {
                    t->expanded = !t->expanded;
                    changed = 1;
                }
                break;
            }
            ry += team_h + SEAM;
            if (t->expanded) {
                int j;

                for (j = 0; j < t->win_count; j++) {
                    ry += db_win_row() + SEAM;
                }
            }
        }
    }
    (void)font;
    return changed;
}

int
tiku_trk_deskbar_team_expanded(const tiku_trk_deskbar_t *db, int team)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return 0;
    }
    return db->teams[team].expanded;
}

int
tiku_trk_deskbar_motion(tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_font_t *font = tiku_font_plain();
    int rowh, changed = 0;
    int leaf = -1, sub = -1, win = -1, clock = -1;

    if (db == NULL) {
        return 0;
    }
    rowh = font->height + 6;
    if (db->menu_open) {
        tiku_rect_t m = db_leaf_menu_rect(db, rowh, db->leaf_count);

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + db->leaf_count * rowh) {
            leaf = (y - m.y - 2) / rowh;
            if (db->leaf[leaf].kind == TIKU_TRK_DB_ITEM_SEP ||
                db->leaf[leaf].kind == TIKU_TRK_DB_ITEM_DISABLED) {
                leaf = -1;      /* nothing to choose there */
            }
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

            if (x >= sm.x && x < sm.x + sm.w && y >= sm.y + 2 &&
                y < sm.y + 2 + db->sub_count * rowh) {
                sub = (y - sm.y - 2) / rowh;
            }
        }
    }
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);
        tiku_rect_t m = db_win_menu_rect(db, n, rowh);

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + n * rowh) {
            win = (y - m.y - 2) / rowh;
            if (win >= n || !enabled[win]) {
                win = -1;       /* a separator or a refused row */
            }
        }
    }
    if (db->apps_menu_open) {
        const tiku_font_t *fm = tiku_font_plain();
        int mrowh = fm->height + 6;
        int nn = (db->team_count > 0) ? db->team_count : 1;
        tiku_rect_t am;
        int ahot = -1;

        am.w = 176;
        am.h = nn * mrowh + 4;
        am.y = db->frame.y + 2 + db_leaf_row() + 2;
        am.x = (db->frame.x > 0) ? db->frame.x - am.w - 2
                                 : db->frame.x + db->frame.w + 2;
        if (x >= am.x && x < am.x + am.w && y >= am.y + 2 &&
            y < am.y + 2 + nn * mrowh) {
            ahot = (y - am.y - 2) / mrowh;
            if (ahot >= db->team_count) {
                ahot = -1;      /* the disabled empty row */
            }
        }
        if (ahot != db->apps_hot) {
            db->apps_hot = ahot;
            changed = 1;
        }
    }
    if (db->clock_menu_open) {
        tiku_rect_t m = { db->frame.x - 130, db->frame.y +
                               db->frame.h - db_clock_row() - 32, 170, 4 * 28 };

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + 4 * 28) {
            clock = (y - m.y - 2) / 28;
        }
    }
    {
        /* The tooltip half of DB-032: resting on a row whose label was
         * cut -- or any application row while names are hidden -- says
         * the whole name.  Menus own the pointer when open. */
        char tip[TIKU_TRK_DB_LABEL_MAX] = "";
        int tip_y = -1;

        if (!db->menu_open && db->win_menu_team < 0 &&
            !db->clock_menu_open && db->settings.show_clock &&
            !db->calendar_open && x >= db->frame.x &&
            x < db->frame.x + db->frame.w &&
            y >= db->frame.y + db->frame.h - db_clock_row() - PAD &&
            y < db->frame.y + db->frame.h) {
            /* The clock's tooltip is the whole long-form date (DB-044),
             * from the same clock the display reads -- frozen and all. */
            time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
            struct tm tmv;

            (void)localtime_r(&now, &tmv);
            (void)strftime(tip, sizeof tip, "%A, %B %-e, %Y", &tmv);
            tip_y = db->frame.y + db->frame.h - db_clock_row() - PAD;
        } else if (!db->menu_open && db->win_menu_team < 0 &&
            !db->clock_menu_open && x >= db->frame.x &&
            x < db->frame.x + db->frame.w) {
            int i, ry = db_list_y(db);
            int team_h = (db->settings.icon_size + 4 > db_team_row())
                             ? db->settings.icon_size + 4 : db_team_row();

            for (i = 0; i < db->team_count && tip_y < 0; i++) {
                tiku_trk_team_t *t = &db->teams[db->order[i]];
                int j;

                if (y >= ry && y < ry + team_h) {
                    char cut[TIKU_TRK_DB_LABEL_MAX];

                    fit_label(font, t->name,
                              db->frame.w - 4 - db->settings.icon_size -
                              EXPANDER_W - 14, cut, sizeof cut);
                    if (db->settings.hide_app_names ||
                        strcmp(cut, t->name) != 0) {
                        snprintf(tip, sizeof tip, "%s", t->name);
                        tip_y = ry;
                    }
                    break;
                }
                ry += team_h + SEAM;
                if (!t->expanded) {
                    continue;
                }
                for (j = 0; j < t->win_count; j++) {
                    if (y >= ry && y < ry + db_win_row()) {
                        char cut[TIKU_TRK_DB_LABEL_MAX];

                        fit_label(font, t->wins[j].title,
                                  db->frame.w - 4 - WIN_INDENT - 20, cut,
                                  sizeof cut);
                        if (strcmp(cut, t->wins[j].title) != 0) {
                            snprintf(tip, sizeof tip, "%s",
                                     t->wins[j].title);
                            tip_y = ry;
                        }
                        break;
                    }
                    ry += db_win_row() + SEAM;
                }
            }
        }
        if (strcmp(tip, db->tip) != 0 || tip_y != db->tip_y) {
            snprintf(db->tip, sizeof db->tip, "%s", tip);
            db->tip_y = tip_y;
            changed = 1;
        }
    }
    if (leaf != db->leaf_hot)   { db->leaf_hot = leaf;   changed = 1; }
    if (sub != db->sub_hot)     { db->sub_hot = sub;     changed = 1; }
    if (win != db->win_hot)     { db->win_hot = win;     changed = 1; }
    if (clock != db->clock_hot) { db->clock_hot = clock; changed = 1; }
    return changed;
}

int
tiku_trk_deskbar_menu_keeps_open(const tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_font_t *font;
    tiku_rect_t m;
    int rowh;

    if (db == NULL || !db->menu_open) {
        return 0;
    }
    font = tiku_font_plain();
    rowh = font->height + 6;
    m = db_leaf_menu_rect(db, rowh, db->leaf_count);
    /* Tracker's 20px tracking margin is intentionally outside the visible
     * menu: it lets the pointer cross the gap into a submenu without
     * dismissing the menu first. */
    if (x >= m.x - 20 && x < m.x + m.w + 20 &&
        y >= m.y - 20 && y < m.y + m.h + 20) {
        return 1;
    }
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

        if (x >= sm.x - 20 && x < sm.x + sm.w + 20 &&
            y >= sm.y - 20 && y < sm.y + sm.h + 20) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_menu_labels(const tiku_trk_deskbar_t *db, int sub,
                            char *out, size_t cap)
{
    int i, n;

    if (db == NULL || out == NULL || cap == 0) { return 0; }
    out[0] = '\0';
    if (!db->menu_open) { return 0; }
    n = sub ? ((db->sub_of >= 0) ? db->sub_count : 0) : db->leaf_count;
    for (i = 0; i < n; i++) {
        size_t used = strlen(out);

        snprintf(out + used, cap - used, "%s|",
                 sub ? db->sub[i].label : db->leaf[i].label);
    }
    return n;
}

int
tiku_trk_deskbar_menu_point(const tiku_trk_deskbar_t *db, const char *label,
                            int *x, int *y)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t m;
    int rowh, i;

    if (db == NULL || label == NULL || x == NULL || y == NULL ||
        !db->menu_open) {
        return 0;
    }
    rowh = f->height + 6;
    m = db_leaf_menu_rect(db, rowh, db->leaf_count);
    /* The open submenu first: its rows sit over the leaf's, and a name
     * that appears in both is the one the user can see. */
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

        for (i = 0; i < db->sub_count; i++) {
            if (strstr(db->sub[i].label, label) != NULL) {
                *x = sm.x + 8;
                *y = sm.y + 2 + i * rowh + rowh / 2;
                return 1;
            }
        }
    }
    for (i = 0; i < db->leaf_count; i++) {
        if (db->leaf[i].kind != TIKU_TRK_DB_ITEM_SEP &&
            strstr(db->leaf[i].label, label) != NULL) {
            *x = m.x + 8;
            *y = m.y + 2 + i * rowh + rowh / 2;
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_quit_policy(int prefs_open, int shortcut_quit)
{
    /* The bar is hosted by the desktop process, so it has no independent
     * close/minimize operation.  A quit shortcut is consumed by preferences
     * when that window is open; otherwise it is refused by the bar itself. */
    return (shortcut_quit && prefs_open) ? 1 : 0;
}

/** @brief Where a submenu hangs: beside its row, on the menu's open side. */
tiku_rect_t
db_sub_menu_rect(const tiku_trk_deskbar_t *db, tiku_rect_t m, int rowh)
{
    tiku_rect_t r;

    r.w = 210;
    r.h = db->sub_count * rowh + 4;
    r.y = m.y + 2 + db->sub_of * rowh;
    r.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           db->dock == TIKU_TRK_DB_LEFT_MINI)
              ? m.x + m.w + 2 : m.x - r.w - 2;
    if (r.x < 0) {
        r.x = 0;
    }
    return r;
}

/** @brief Fill the submenu for leaf row @p at, or close it. */
void
db_open_sub(tiku_trk_deskbar_t *db, int at)
{
    const tiku_trk_db_item_t *it;

    db->sub_count = 0;
    db->sub_of = -1;
    if (at < 0 || at >= db->leaf_count) {
        return;
    }
    it = &db->leaf[at];
    if (it->kind == TIKU_TRK_DB_ITEM_QUIT) {
        /* The confirm-vs-immediate split (DB-006): the parent asks, the
         * submenu's two rows act the moment they are chosen. */
        tiku_trk_db_item_t *si = &db->sub[db->sub_count++];

        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_POWER_OFF;
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str("menu.power_off"));
        si = &db->sub[db->sub_count++];
        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_RESTART;
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str("menu.restart"));
        db->sub_of = at;
        db->sub_hot = -1;
        return;
    }
    if (it->kind == TIKU_TRK_DB_ITEM_SETTINGS) {
        tiku_trk_db_item_t *si = &db->sub[db->sub_count++];

        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_PREFS;
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str("menu.deskbar_prefs"));
        si = &db->sub[db->sub_count++];
        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_TRACKER_PREFS;
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str("menu.tracker_prefs"));
        si = &db->sub[db->sub_count++];
        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_DESKTOP_PREFS;
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str("menu.desktop_prefs"));
        si = &db->sub[db->sub_count++];
        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_FONT_PREFS;
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str("menu.fonts"));
        si = &db->sub[db->sub_count++];
        memset(si, 0, sizeof *si);
        si->kind = TIKU_TRK_DB_ITEM_SHOW_REPLICANTS;
        /* Says what choosing it DOES, which is the other state: a row
         * labelled with the state it is in reads as a heading. */
        snprintf(si->label, sizeof si->label, "%s",
                 tiku_str(db->show_replicants ? "menu.hide_replicants"
                                                  : "menu.show_replicants"));
    } else if (it->kind == TIKU_TRK_DB_ITEM_APPS ||
               it->kind == TIKU_TRK_DB_ITEM_PROGRAMMING) {
        int want = (it->kind == TIKU_TRK_DB_ITEM_PROGRAMMING)
                       ? TIKU_TRK_DB_GROUP_PROGRAMMING
                       : TIKU_TRK_DB_GROUP_APPS;
        int i;

        for (i = 0; i < db->app_count &&
                    db->sub_count < TIKU_TRK_DB_SUB_MAX; i++) {
            tiku_trk_db_item_t *si;

            if (db->app[i].group != want) {
                continue;
            }
            si = &db->sub[db->sub_count++];
            memset(si, 0, sizeof *si);
            si->kind = TIKU_TRK_DB_ITEM_APP;
            snprintf(si->label, sizeof si->label, "%s", db->app[i].label);
            snprintf(si->path, sizeof si->path, "%s", db->app[i].command);
            snprintf(si->icon, sizeof si->icon, "%s", db->app[i].icon);
        }
    } else if (it->kind == TIKU_TRK_DB_ITEM_RECENT) {
        int i;

        for (i = 0; i < db->recent_count[it->which] &&
                    i < db->settings.recent_count[it->which] &&
                    i < TIKU_TRK_DB_SUB_MAX; i++) {
            tiku_trk_db_item_t *si = &db->sub[db->sub_count++];

            memset(si, 0, sizeof *si);
            si->kind = TIKU_TRK_DB_ITEM_PATH;
            memcpy(si->path, db->recent[it->which][i], sizeof si->path);
            si->path[sizeof si->path - 1u] = '\0';
            /* The NAME, not the path: a menu of paths is a menu nobody
             * can read across at a glance. */
            {
                char nm[TIKU_PATH_MAX];

                /* Through a local: source and destination are both inside
                 * this bar, and an snprintf between two members of one
                 * object is not something to leave to chance. */
                snprintf(nm, sizeof nm, "%s",
                         db_leaf_name(db->recent[it->which][i]));
                snprintf(si->label, sizeof si->label, "%.*s",
                         (int)sizeof si->label - 1, nm);
            }
        }
    } else if (it->kind == TIKU_TRK_DB_ITEM_DIR &&
               db->backend != NULL && db->backend->ops->list != NULL) {
        struct menu_walk w;

        w.db = db;
        w.n = 0;
        w.into = db->sub;
        w.max = TIKU_TRK_DB_SUB_MAX;
        (void)db->backend->ops->list(db->backend, it->path, db_menu_entry, &w);
        db->sub_count = w.n;
    }
    if (db->sub_count > 0) {
        db->sub_of = at;
        db->sub_hot = -1;
    }
}

/**
 * @brief The expander arrow beside a team: pointing down when its windows
 *        are shown, right when they are not.
 */
static void
draw_expander(tiku_surface_t *s, int x, int y, int expanded)
{
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);
    int k;

    /* STROKED along an open three-point polyline with a two-pixel pen, not
     * filled: a solid triangle carries far more weight than the original's
     * arrow and is one of the things that reads as a cheap imitation. */
    for (k = 0; k < EXPANDER_PEN; k++) {
        int i;

        for (i = 0; i < EXPANDER_ARM; i++) {
            if (expanded) {           /* pointing down: \ then /            */
                tiku_pixel(s, x + i, y + i + k, dark);
                tiku_pixel(s, x + 2 * EXPANDER_ARM - 2 - i, y + i + k,
                                dark);
            } else {                  /* pointing right: / then \           */
                tiku_pixel(s, x + i + k, y + i, dark);
                tiku_pixel(s, x + i + k, y + 2 * EXPANDER_ARM - 2 - i,
                                dark);
            }
        }
    }
}

/**
 * @brief A list row's background.
 *
 * The two recipes are structurally different, not one darker than the other:
 * an idle row is bevelled on every edge over a light gradient, a selected one
 * is a dark slab with a single dark cap and no side bevel at all.  The label
 * colour does NOT change either way -- the original draws selected and
 * unselected menu text in the same black.
 */
static void
draw_row_bg(tiku_surface_t *s, tiku_rect_t r, int selected)
{
    int i;

    for (i = 0; i < r.h; i++) {
        float t = (r.h > 1) ? (float)i / (float)(r.h - 1) : 0.0f;
        float tint = selected ? (1.25f + (1.20f - 1.25f) * t)
                              : (0.69f + (1.03f - 0.69f) * t);

        tiku_hline(s, r.x, r.y + i, r.w,
                        tiku_tint(TIKU_C_PANEL, tint));
    }
    if (selected) {
        tiku_hline(s, r.x, r.y, r.w,
                        tiku_tint(TIKU_C_PANEL, 1.40f));
        return;
    }
    tiku_hline(s, r.x, r.y, r.w,
                    tiku_tint(TIKU_C_PANEL, 0.50f));
    tiku_vline(s, r.x, r.y, r.h,
                    tiku_tint(TIKU_C_PANEL, 0.70f));
    tiku_vline(s, r.x + r.w - 1, r.y, r.h,
                    tiku_tint(TIKU_C_PANEL, 1.08f));
    tiku_hline(s, r.x, r.y + r.h - 1, r.w,
                    tiku_tint(TIKU_C_PANEL, 1.08f));
}

/** @brief Truncate to a width budget, with an ellipsis (DB row). */
static void
fit_label(const tiku_font_t *f, const char *src, int budget, char *out,
          size_t max)
{
    size_t n;

    snprintf(out, max, "%s", src);
    if (tiku_text_width(f, out) <= budget) {
        return;
    }
    n = strlen(out);
    while (n > 1u) {
        out[--n] = '\0';
        if (n > 3u) {
            out[n - 1u] = '.';
            out[n - 2u] = '.';
            out[n - 3u] = '.';
        }
        if (tiku_text_width(f, out) <= budget) {
            return;
        }
    }
}

void
tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    const tiku_font_t *f = tiku_font_plain();
    const tiku_font_t *fb = tiku_font_bold();
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);
    tiku_rect_t leaf;
    int i, j, y, rows;
    int team_h;

    if (db == NULL || s == NULL) {
        return;
    }
    team_h = (db->settings.icon_size + 4 > db_team_row())
                 ? db->settings.icon_size + 4 : db_team_row();
    db->screen_h = s->h;
    rows = tiku_trk_deskbar_mini(db->dock) ? 0 : db_list_rows(db);
    db_draw_frame(db, s, &rows);

    tiku_fill(s, db->frame, TIKU_C_PANEL);
    tiku_bevel(s, db->frame,
                    tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_MAX),
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));

    db_draw_leaf(db, s, fb, dark, &leaf);

    db_draw_mini(db, s, f);

    db_draw_teams(db, s, f, team_h, &y);

    db_draw_tray(db, s, f, &y);
    db_draw_switch(db, s);
    db_draw_clock(db, s, f, &y);

    db_draw_leaf_menu(db, s, f);
    db_draw_win_menu(db, s, f);
    db_draw_clock_menu(db, s);
    db_draw_switch_strip(db, s);
    db_draw_tip(db, s);
    db_draw_calendar(db, s, f, fb, dark);
}

/**
 * @brief the calendar under the clock
 */
void
db_draw_calendar(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                 const tiku_font_t *f, const tiku_font_t *fb, tiku_rgb_t dark)
{
    if (db->calendar_open) {
        /* The clock's calendar: a month grid with steppers, popped beside
         * the bar and dismissed by the next click anywhere. */
        struct tm tmv;
        tiku_rect_t c = db_calendar_rect(db);
        int col, rowi, first, days;

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = db->calendar_year - 1900;
        tmv.tm_mon = db->calendar_month - 1;
        tmv.tm_mday = 1;
        tmv.tm_isdst = -1;
        (void)mktime(&tmv);
        tiku_fill(s, c, TIKU_C_PANEL);
        tiku_frame(s, c, dark);
        tiku_bevel(s, tiku_inset(c, 1),
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_LIGHTEN_MAX),
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
        {
            char head[32];
            snprintf(head, sizeof head, "%s %d",
                     tiku_str((const char *[]){ "month.1", "month.2",
                       "month.3", "month.4", "month.5", "month.6", "month.7",
                       "month.8", "month.9", "month.10", "month.11",
                       "month.12" }[tmv.tm_mon]), 1900 + tmv.tm_year);
            (void)tiku_text_centered(s, fb,
                (tiku_rect_t){ c.x, c.y + 2, c.w, 18 }, head,
                TIKU_C_TEXT);
        }
        first = tmv.tm_wday;
        days = db_calendar_days(db->calendar_year, db->calendar_month);
        for (rowi = 0; rowi < 6; rowi++) {
            for (col = 0; col < 7; col++) {
                int day = rowi * 7 + col - first + 1;
                char num[12];
                tiku_rect_t cell = { c.x + 6 + col * 25,
                                          c.y + 22 + rowi * 20, 24, 18 };
                if (day < 1 || day > days) {
                    continue;
                }
                if (day == db->calendar_day) {
                    tiku_fill(s, cell, TIKU_C_SELECT);
                }
                snprintf(num, sizeof num, "%d", day);
                (void)tiku_text_centered(s, f, cell, num,
                    (day == db->calendar_day) ? TIKU_C_SELTEXT
                                         : TIKU_C_TEXT);
            }
        }
    }
}

/**
 * @brief the tip over a row
 */
void
db_draw_tip(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    if (db->tip_y >= 0 && db->tip[0] != '\0') {
        /* The whole name, said beside the row that could not fit it
         * (DB-032). */
        tiku_rect_t b = tiku_ui_tip_size(db->tip);

        b.x = db->frame.x - b.w - 4;
        b.y = db->tip_y;
        if (b.x < 0) { b.x = 0; }
        tiku_ui_tip(s, b, db->tip);
    }
}

/**
 * @brief the theme switch in its own strip of tray
 */
void
db_draw_switch_strip(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    {
        /* The scroll arrows: shown the moment something is out of sight
         * in the direction they point (DB-034). */
        int up = 0, down = 0, k;
        int ly = db_list_y(db);
        int lb = db->frame.y + db->frame.h -
                 (db->tray_count * TRAY_ROW) -
                 (db->settings.show_clock ? db_clock_row() : 0) - PAD;
        int cx = db->frame.x + db->frame.w / 2;

        tiku_trk_deskbar_arrows(db, &up, &down);
        for (k = 0; k < 4 && up; k++) {
            tiku_hline(s, cx - k, ly + 1 + k, 2 * k + 1,
                            TIKU_C_TEXT);
        }
        for (k = 0; k < 4 && down; k++) {
            tiku_hline(s, cx - (3 - k), lb - 5 + k, 2 * (3 - k) + 1,
                            TIKU_C_TEXT);
        }
    }
}

/**
 * @brief the clock menu
 */
void
db_draw_clock_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    if (db->clock_menu_open) {
        const char *items[] = {
            tiku_str(db->settings.show_clock ? "menu.clock.hide"
                                                 : "menu.clock.show"),
            tiku_str(db->settings.show_seconds ? "menu.seconds.hide"
                                                   : "menu.seconds.show"),
            tiku_str(db->settings.show_day_of_week ? "menu.day.hide"
                                                       : "menu.day.show"),
            tiku_str(db->settings.show_timezone ? "menu.zone.hide"
                                                    : "menu.zone.show")
        };
        tiku_rect_t m = { db->frame.x - 130, db->frame.y + db->frame.h -
                               db_clock_row() - 32, 170, 4 * 28 };
        tiku_ui_menu(s, m, items, 4, db->clock_hot);
    }
}

/**
 * @brief the window menu for one application
 */
void
db_draw_win_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                 const tiku_font_t *f)
{
    /* The window menu for one application: Hide all / Show all / Close all,
     * which the original appends to every application's window list. */
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);
        int rowh = f->height + 6;
        tiku_rect_t m = db_win_menu_rect(db, n, rowh);
        int k;

        tiku_ui_menu(s, m, items, n, db->win_hot);
        for (k = 0; k < n; k++) {
            /* A row that cannot be chosen is drawn as such, with the one
             * dimmed ink the menu layer has (DB-004, DB-028). */
            if (!enabled[k] && items[k][0] != '-') {
                tiku_rect_t r = { m.x + 2, m.y + 2 + k * rowh,
                                       m.w - 4, rowh };

                tiku_fill(s, r, TIKU_C_PANEL);
                tiku_text(s, f, r.x + 8,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               items[k],
                               tiku_tint(TIKU_C_PANEL, 1.55f));
            }
        }
    }
}

/**
 * @brief the leaf menu: the head, the recent folders, the places
 */
void
db_draw_leaf_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f)
{
    int i;

    /* The leaf menu: the head the original always shows, then the recent
     * lists, then whatever is in the Deskbar folder right now. */
    if (db->menu_open) {
        const char *items[TIKU_TRK_DB_LEAF_MAX];
        int rowh = f->height + 6;
        tiku_rect_t m = db_leaf_menu_rect(db, rowh, db->leaf_count);

        for (i = 0; i < db->leaf_count; i++) {
            items[i] = (db->leaf[i].kind == TIKU_TRK_DB_ITEM_SEP)
                           ? "-" : db->leaf[i].label;
        }
        if (db->menu_icon != NULL) {
            struct menu_icon_relay relay = { db, db->leaf };

            tiku_ui_menu_icons(s, m, items, db->leaf_count,
                                    db->leaf_hot, relay_menu_icon, &relay);
        } else {
            tiku_ui_menu(s, m, items, db->leaf_count, db->leaf_hot);
        }
        for (i = 0; i < db->leaf_count; i++) {
            /* A row that cannot be chosen is drawn as such, and the menu
             * layer has one ink: dimmed here, over what it already drew
             * (DB-004). */
            if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_DISABLED) {
                tiku_rect_t r = { m.x + 2, m.y + 2 + i * rowh,
                                       m.w - 4, rowh };

                tiku_fill(s, r, TIKU_C_PANEL);
                tiku_text(s, f, r.x + 10,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               db->leaf[i].label,
                               tiku_tint(TIKU_C_PANEL, 1.55f));
            } else if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_DIR ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_RECENT ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_APPS ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_PROGRAMMING ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_SETTINGS ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_QUIT) {
                /* What expands says so, the way every hierarchical menu
                 * marks the rows that go somewhere (DB-002) -- with the
                 * drawn mark the rest of the menus use, rather than a
                 * greater-than typed in the body face. */
                tiku_ui_submenu_arrow(s,
                    (tiku_rect_t){ m.x, m.y + 2 + i * rowh, m.w, rowh },
                    TIKU_C_TEXT);
            }
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            const char *sitems[TIKU_TRK_DB_SUB_MAX];
            tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

            for (i = 0; i < db->sub_count; i++) {
                sitems[i] = db->sub[i].label;
            }
            if (db->menu_icon != NULL) {
                struct menu_icon_relay relay = { db, db->sub };

                tiku_ui_menu_icons(s, sm, sitems, db->sub_count,
                                        db->sub_hot, relay_menu_icon,
                                        &relay);
            } else {
                tiku_ui_menu(s, sm, sitems, db->sub_count, db->sub_hot);
            }
        }
    }
}

/**
 * @brief the clock beneath the tray
 */
void
db_draw_clock(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f,
              int *y)
{
    if (tiku_trk_deskbar_vertical(db->dock)) {
        (*y) += SWITCH_ROW;
    }
    if (db->settings.show_clock) {
        time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
        struct tm tmv;
        char clock[64];
        char date[32];
        char time_text[32];
        char time_fmt[16] = "%H:%M";
        int cw;

        (void)localtime_r(&now, &tmv);
        if (db->settings.show_seconds) {
            snprintf(time_fmt, sizeof time_fmt, "%%H:%%M:%%S");
        }
        if (strftime(date, sizeof date,
                     db->settings.show_day_of_week ? "%a %e %b" : "%e %b",
                     &tmv) == 0) {
            snprintf(date, sizeof date, "%d", tmv.tm_mday);
        }
        if (strftime(time_text, sizeof time_text, time_fmt, &tmv) == 0) {
            snprintf(time_text, sizeof time_text, "%02d:%02d",
                     tmv.tm_hour, tmv.tm_min);
        }
        if (db->settings.show_timezone) {
            char zone[16];
            size_t used;

            if (strftime(zone, sizeof zone, "%Z", &tmv) == 0 ||
                zone[0] == '\0') {
                snprintf(zone, sizeof zone, "local");
            }
            snprintf(clock, sizeof clock, "%s %s", date, time_text);
            used = strlen(clock);
            if (used + 1u < sizeof clock) {
                snprintf(clock + used, sizeof clock - used, " %s", zone);
            }
        } else {
            snprintf(clock, sizeof clock, "%s %s", date, time_text);
        }
        cw = tiku_text_width(f, clock);
        tiku_text(s, f, db->frame.x + (db->frame.w - cw) / 2,
                  (*y) + (db_clock_row() - f->height) / 2 + f->ascent,
                  clock, TIKU_C_TEXT);
    }
}

/**
 * @brief the tray: the replicants, one row each
 */
void
db_draw_tray(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f,
             int *y)
{
    int i;

    /* The tray, then the clock beneath it: the original reserves the
     * clock's room in the first row, and stacking it here is the vertical
     * equivalent. */
    (*y) = tiku_trk_deskbar_mini(db->dock) ? db->frame.y + db_leaf_row() + PAD
                                        : db_tray_y(db);
    tiku_hline(s, db->frame.x + 4, (*y) - 3, db->frame.w - 8,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_hline(s, db->frame.x + 4, (*y) - 2, db->frame.w - 8,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX));
    for (i = 0; i < db->tray_count; i++) {
        const tiku_trk_replicant_t *r = &db->tray[i];
        int ty = (*y) + (TRAY_ROW - f->height) / 2 + f->ascent;
        char val[32];
        int vw;

        /* The tray is the narrowest place on screen, so it is the place a
         * formatted reading earns the most: the column is too thin to read
         * a raw second count or byte count against. */
        (void)tiku_attr_format(r->desc_known ? &r->desc : NULL,
                                   r->valid ? r->value : "",
                                   val, sizeof val);
        vw = tiku_text_width(f, val);

        tiku_text(s, f, db->frame.x + 6, ty, r->label, TIKU_C_TEXT);
        tiku_text(s, f, db->frame.x + db->frame.w - 6 - vw, ty, val,
                       TIKU_C_TEXT);
        (*y) += TRAY_ROW;
    }
}

/**
 * @brief the application list: a row per team, its windows beneath it
 */
void
db_draw_teams(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f,
              int team_h, int *y)
{
    int i, j;

    if (!tiku_trk_deskbar_mini(db->dock) &&
        !tiku_trk_deskbar_vertical(db->dock)) {
        /* The horizontal strip: as many buttons as fit, each between a
         * font-derived minimum and 1.25x it, shrinking as applications
         * launch and growing back as they quit; past the minimum, the
         * ends grow arrows (DB-033). */
        int n = db->team_count;
        int min_w = 24 + tiku_text_width(f, "abcdefgh");
        int max_w = min_w + min_w / 4;
        int left = db->frame.x + 2 + HLEAF_W + 4;
        int right = db->frame.x + db->frame.w - (SWITCH_W + 14);
        int avail = right - left;
        int bw = (n > 0) ? avail / n : max_w;
        int bx, i2, k;

        if (bw > max_w) { bw = max_w; }
        db->hoverflow = (n > 0 && bw < min_w);
        if (db->hoverflow) {
            bw = min_w;
            left += 12;
            right -= 12;
        } else {
            db->scroll = 0;
        }
        db->overflow = db->hoverflow;
        db->hbutton_w = bw;
        db->hleft = left;
        bx = left;
        for (i2 = (db->hoverflow ? db->scroll : 0); i2 < n; i2++) {
            tiku_trk_team_t *t = &db->teams[db->order[i2]];
            tiku_rect_t b = { bx, db->frame.y + 2, bw - 2, db_leaf_row() };
            char label[TIKU_TRK_DB_LABEL_MAX];

            if (bx + bw > right) {
                break;
            }
            draw_row_bg(s, b, t->active);
            (void)tiku_icon_paint(s,
                strcmp(t->name, "TikuTracker") == 0 ? "folder" : "application",
                b.x + 3, b.y + (db_leaf_row() - 16) / 2, 16);
            fit_label(f, t->name, b.w - 16 - 10, label, sizeof label);
            tiku_text(s, f, b.x + 16 + 6,
                           b.y + (db_leaf_row() - f->height) / 2 + f->ascent,
                           label, TIKU_C_TEXT);
            bx += bw;
        }
        if (db->hoverflow) {
            int ay = db->frame.y + db->frame.h / 2;

            for (k = 0; k < 4; k++) {
                if (db->scroll > 0) {
                    tiku_vline(s, left - 10 + k, ay - k, 2 * k + 1,
                                    TIKU_C_TEXT);
                }
                tiku_vline(s, right + 3 + k, ay - (3 - k),
                                2 * (3 - k) + 1, TIKU_C_TEXT);
            }
        }
    }
    if (!tiku_trk_deskbar_mini(db->dock) &&
        tiku_trk_deskbar_vertical(db->dock)) {
        (*y) = db_list_y(db);
        for (i = 0; i < db->team_count; i++) {
            int team = db->order[i];
            tiku_trk_team_t *t = &db->teams[team];
            tiku_rect_t row = { db->frame.x + 2, (*y), db->frame.w - 4,
                                     team_h };
            int icon_size = db->settings.icon_size;
            tiku_rect_t icon = { row.x + 4,
                                      (*y) + (team_h - icon_size) / 2,
                                      icon_size, icon_size };
            char label[TIKU_TRK_DB_LABEL_MAX];

            /* A hovering drag lights the row it would be handed to, so
             * the promise the drop makes is visible before it is made
             * (DB-035). */
            if (db->drop_team == team) {
                tiku_fill(s, row, TIKU_C_SELECT);
            } else {
                draw_row_bg(s, row, t->active);
            }
            /* The application's icon.  Tracker's own team is the one that
             * can be named; anything else gets the generic application icon
             * rather than a blank, so the column never has a hole in it. */
            /* DB-018: when even the generic icon is missing, the slot is
             * left as it is.  A coloured rectangle where an icon failed
             * looks like an application whose icon IS a blue square. */
            (void)tiku_icon_paint(s, strcmp(t->name, "TikuTracker") == 0
                                         ? "folder" : "application",
                                      icon.x, icon.y, icon_size);

            if (!db->settings.hide_app_names) {
                fit_label(f, t->name,
                          row.w - icon_size - EXPANDER_W - 14, label,
                          sizeof label);
                tiku_text(s, f, icon.x + icon_size + 5,
                               (*y) + (team_h - f->height) / 2 + f->ascent,
                               label, TIKU_C_TEXT);
            }
            if (db->settings.show_expander) {
                draw_expander(s, row.x + row.w - EXPANDER_W - 2,
                              (*y) + (team_h - 9) / 2,
                              t->expanded ||
                              db->expander_press == db->order[i]);
            }
            (*y) += team_h + SEAM;

            if (!t->expanded) {
                continue;
            }
            for (j = 0; j < t->win_count; j++) {
                tiku_trk_dbwin_t *w = &t->wins[j];
                tiku_rect_t wr = { db->frame.x + 2, (*y), db->frame.w - 4,
                                        db_win_row() };
                char wl[TIKU_TRK_DB_LABEL_MAX];

                draw_row_bg(s, wr, w->current);
                draw_win_icon(s, wr.x + WIN_INDENT,
                              (*y) + (db_win_row() - 9) / 2,
                              w->state == TIKU_TRK_DBW_MINIMIZED,
                              0);
                fit_label(f, w->title, wr.w - WIN_INDENT - 20, wl,
                          sizeof wl);
                tiku_text(s, f, wr.x + WIN_INDENT + 14,
                          (*y) + (db_win_row() - f->height) / 2 + f->ascent,
                          wl, TIKU_C_TEXT);
                (*y) += db_win_row() + SEAM;
            }
        }
    }
}

/**
 * @brief the mini bar: one row of icons
 */
void
db_draw_mini(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f)
{
    if (tiku_trk_deskbar_mini(db->dock)) {
        /* The collapsed dock's second icon: the application list folded
         * into one button that pops the running-apps menu (DB-037). */
        tiku_rect_t ab = { db->frame.x + 2,
                                db->frame.y + 2 + db_leaf_row() + 2,
                                db->frame.w - 4, db_team_row() };

        draw_row_bg(s, ab, db->apps_menu_open);
        (void)tiku_icon_paint(s, "application",
                                  ab.x + (ab.w - 16) / 2,
                                  ab.y + (ab.h - 16) / 2, 16);
        if (db->apps_menu_open) {
            const char *items[TIKU_TRK_DB_TEAMS_MAX];
            int n = db->team_count, k;
            int rowh = f->height + 6;
            tiku_rect_t am;

            if (n == 0) {
                items[0] = "no application running";
                n = 1;
            } else {
                for (k = 0; k < n; k++) {
                    items[k] = db->teams[db->order[k]].name;
                }
            }
            am.w = 176;
            am.h = n * rowh + 4;
            am.y = ab.y;
            am.x = (db->frame.x > 0) ? db->frame.x - am.w - 2
                                     : db->frame.x + db->frame.w + 2;
            tiku_ui_menu(s, am, items, n, db->apps_hot);
            if (db->team_count == 0) {
                /* Disabled the one way this layer says it (DB-004). */
                tiku_rect_t r = { am.x + 2, am.y + 2, am.w - 4,
                                       rowh };

                tiku_fill(s, r, TIKU_C_PANEL);
                tiku_text(s, f, r.x + 10,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               items[0],
                               tiku_tint(TIKU_C_PANEL, 1.55f));
            }
        }
    }
}

/**
 * @brief the leaf, the one control that is not a list item
 */
void
db_draw_leaf(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *fb,
             tiku_rgb_t dark, tiku_rect_t *leaf)
{
    /* The leaf: the one control that is not a list item, so it is the one
     * thing wearing the tab colour. */
    (*leaf) = (tiku_rect_t){ db->frame.x + 2, db->frame.y + 2,
                               tiku_trk_deskbar_vertical(db->dock)
                                   ? db->frame.w - 4 : HLEAF_W,
                               db_leaf_row() };
    tiku_fill(s, (*leaf), TIKU_C_TAB);
    tiku_frame(s, (*leaf), dark);
    tiku_bevel(s, tiku_inset((*leaf), 1),
                    tiku_tint(TIKU_C_TAB, TIKU_LIGHTEN_1),
                    tiku_tint(TIKU_C_TAB, TIKU_DARKEN_2));
    {
        /*
         * The mark, then the name beside it.  Drawn without its own
         * ground: the leaf already has a surface of its own and a pale
         * square on the amber would read as a sticker stuck to it.
         */
        int mark = db_leaf_row() - 10;  /* 24 at the default face */
        tiku_rect_t label = (*leaf);
        /*
         * Shaded to the ground it is standing on, which is the leaf and
         * so is a different colour in each of the three modes.  Light:
         * as drawn -- near-black edges on amber.  Dark: lifted well up,
         * because the deepened amber under it is dark enough that the
         * mark's own edges would sink into the button and leave a smudge.
         * Without hue: a touch down, so it sits on the mid-grey tab
         * rather than floating over it.
         */
        float shade = (db->theme == 1) ? 0.35f
                    : (db->theme == 2) ? 1.15f : 1.0f;

        tiku_logo_paint_with(s, (tiku_rect_t){ (*leaf).x + 4,
                                               (*leaf).y +
                                                   ((*leaf).h - mark) / 2,
                                               mark, mark }, 0u, NULL,
                             shade);
        label.x += mark + 8;
        label.w -= mark + 8;
        (void)tiku_text_centered(s, fb, label, "TikuOS", TIKU_C_TEXT);
    }
}

/**
 * @brief the frame: the backdrop, the bevel, and where the bar sits when hidden
 * or vertical
 */
void
db_draw_frame(tiku_trk_deskbar_t *db, tiku_surface_t *s, int *rows)
{
    {
        /* How much room the list actually has, once the leaf, the tray and
         * the clock have taken theirs.  A list longer than that is what
         * makes the bar scrollable (DB-034). */
        int room = s->h - db->top_inset - (db_leaf_row() + PAD) -
                   (db->tray_count * TRAY_ROW) - SWITCH_ROW -
                   (db->settings.show_clock ? db_clock_row() : 0) - 2 * PAD;

        db->overflow = (!tiku_trk_deskbar_mini(db->dock) && (*rows) > room &&
                        room > 0);
        if (!db->overflow) {
            db->scroll = 0;     /* nothing hidden: nothing scrolled past */
        }
        if (db->overflow && (*rows) > room) {
            (*rows) = room;
        }
    }
    db->frame.w = tiku_trk_deskbar_width(db);
    /* The bar starts below whatever owns the top of the screen: with the
     * global menu bar on, a Deskbar at y 0 would put its own leaf under
     * the strip. */
    db->frame.y = db->top_inset;
    if (tiku_trk_deskbar_vertical(db->dock)) {
        db->frame.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                       db->dock == TIKU_TRK_DB_LEFT_MINI)
                          ? 0 : s->w - db->frame.w;
        db->frame.h = db_leaf_row() + PAD + (*rows) + PAD +
                      (tiku_trk_deskbar_mini(db->dock) ? db_team_row() + 2
                                                       : 0) +
                      db->tray_count * TRAY_ROW + SWITCH_ROW +
                      (db->settings.show_clock ? db_clock_row() : 0) + PAD;
    } else {
        /* Along an edge: the bar spans the screen and is as deep as one
         * row needs.  The application STRIP lives here -- buttons that
         * shrink as more launch and grow back as they quit (DB-033). */
        db->frame.x = 0;
        db->frame.w = s->w;
        db->frame.h = db_leaf_row() + PAD;
        db->frame.y = (db->dock == TIKU_TRK_DB_TOP)
                          ? db->top_inset : s->h - db->frame.h;
        (*rows) = 0;
    }
    if (!tiku_trk_deskbar_shown(db)) {
        /* Hidden: a sliver against its own edge, which is all there is
         * left to touch (DB-051).  The frame is what the hit test and the
         * drawing both read, so hiding is a geometry change and not a
         * second drawing path. */
        if (tiku_trk_deskbar_vertical(db->dock)) {
            if (db->frame.x > 0) {
                db->frame.x = s->w - TIKU_TRK_DB_SLIVER;
            }
            db->frame.w = TIKU_TRK_DB_SLIVER;
        } else {
            if (db->frame.y > 0) {
                db->frame.y = s->h - TIKU_TRK_DB_SLIVER;
            }
            db->frame.h = TIKU_TRK_DB_SLIVER;
        }
        tiku_fill(s, db->frame, TIKU_C_PANEL);
        return;
    }
}
