/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_layout.c - where everything on the bar goes
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/**
 * @brief The height in pixels the application list wants: a team row per
 *        team, plus a window row per window of each expanded team, each
 *        with its seam.
 */
int
db_list_rows(const tiku_trk_deskbar_t *db)
{
    int i, h = 0;
    int team_h = (db->settings.icon_size + 4 > db_team_row())
                     ? db->settings.icon_size + 4 : db_team_row();

    for (i = 0; i < db->team_count; i++) {
        h += team_h + SEAM;
        if (db->teams[i].expanded) {
            h += db->teams[i].win_count * (db_win_row() + SEAM);
        }
    }
    return h;
}

/** @brief Where the application list starts: just below the leaf row. */
int
db_list_y(const tiku_trk_deskbar_t *db)
{
    /* Below the leaf, WHEREVER the frame starts: with the global bar on
     * the whole bar sits lower, and a list still measured from the top
     * of the screen would draw across the leaf itself. */
    return db->frame.y + db_leaf_row() + PAD;
}

/** @brief Where the tray starts: below the list's rows. */
int
db_tray_y(const tiku_trk_deskbar_t *db)
{
    return db_list_y(db) + db_list_rows(db) + PAD;
}

tiku_rect_t
tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t r = { 0, 0, 0, 0 };

    return (db != NULL) ? db->frame : r;
}

tiku_rect_t
tiku_trk_deskbar_switch_rect(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t r = { 0, 0, SWITCH_W, SWITCH_H };

    if (db == NULL) {
        return r;
    }
    if (tiku_trk_deskbar_vertical(db->dock)) {
        /* Its own strip between the tray and the clock, anchored to the
         * bottom the same way the clock is, so the two agree in every
         * dock mode. */
        r.x = db->frame.x + (db->frame.w - SWITCH_W) / 2;
        r.y = db->frame.y + db->frame.h - PAD -
              (db->settings.show_clock ? db_clock_row() : 0) - SWITCH_ROW +
              (SWITCH_ROW - SWITCH_H) / 2;
    } else {
        /* Along an edge: the strip's far end, past the last button. */
        r.x = db->frame.x + db->frame.w - SWITCH_W - 8;
        r.y = db->frame.y + (db->frame.h - SWITCH_H) / 2;
    }
    return r;
}

/**
 * @brief The theme switch, drawn like the physical part.
 *
 * A recessed track the knob slides in, lit with the accent colour while
 * the dark side is on, and a raised cap with grip lines.  Three detents:
 * light at the left, dark at the right, and no hue in the middle, because
 * the third mode is a different axis rather than a further step into the
 * dark, and the centre is the one position that does not claim otherwise.
 */
void
db_draw_switch(const tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    tiku_rect_t t = tiku_trk_deskbar_switch_rect(db);
    tiku_rgb_t well = (db->theme == 1)
        ? TIKU_C_TAB
        : tiku_tint(TIKU_C_PANEL,
                         (db->theme == 2) ? 1.28f : 1.10f);
    tiku_rect_t knob;
    int slot;
    int k;

    /* The well: filled, framed, and shadowed along the top edge the way
     * a recess catches light from above. */
    tiku_fill(s, t, well);
    tiku_frame(s, t, tiku_tint(TIKU_C_PANEL, 1.45f));
    tiku_hline(s, t.x + 1, t.y + 1, t.w - 2,
                    tiku_tint(well, TIKU_DARKEN_2));
    tiku_hline(s, t.x + 1, t.y + t.h, t.w,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX));

    /* The knob rides a shade proud of the track, top and bottom.  Its
     * three seats are the two ends and the exact middle. */
    slot = (db->theme == 1) ? t.x + t.w - SWITCH_H - 1
         : (db->theme == 2) ? t.x + (t.w - SWITCH_H) / 2
                            : t.x + 1;
    knob = (tiku_rect_t){ slot, t.y - 1, SWITCH_H, t.h + 2 };
    tiku_fill(s, knob, TIKU_C_PANEL);
    tiku_frame(s, knob, tiku_tint(TIKU_C_PANEL, 1.45f));
    tiku_bevel(s, tiku_inset(knob, 1),
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX),
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    for (k = 0; k < 3; k++) {
        tiku_vline(s, knob.x + 3 + 3 * k, knob.y + 4, knob.h - 8,
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
    }
}

void
tiku_trk_deskbar_set_theme(tiku_trk_deskbar_t *db, int which)
{
    if (db != NULL) {
        db->theme = (which >= 0 && which <= 2) ? which : 0;
    }
}

int
tiku_trk_deskbar_add_app(tiku_trk_deskbar_t *db, const char *label,
                         const char *command, const char *icon)
{
    return tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_APPS,
                                       label, command, icon);
}

int
tiku_trk_deskbar_add_app_in(tiku_trk_deskbar_t *db, int group,
                            const char *label, const char *command,
                            const char *icon)
{
    if (db == NULL || label == NULL || command == NULL ||
        db->app_count >= (int)(sizeof db->app / sizeof db->app[0])) {
        return 0;
    }
    snprintf(db->app[db->app_count].label,
             sizeof db->app[db->app_count].label, "%s", label);
    snprintf(db->app[db->app_count].command,
             sizeof db->app[db->app_count].command, "%s", command);
    snprintf(db->app[db->app_count].icon,
             sizeof db->app[db->app_count].icon, "%s",
             (icon != NULL) ? icon : "");
    db->app[db->app_count].group = group;
    db->app_count++;
    return 1;
}

void
tiku_trk_deskbar_set_top_inset(tiku_trk_deskbar_t *db, int px)
{
    if (db != NULL) {
        db->top_inset = (px > 0) ? px : 0;
    }
}

int
tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y)
{
    tiku_rect_t f;

    if (db == NULL) {
        return 0;
    }
    f = db->frame;
    return (x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h);
}

int
tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? (db->menu_open || db->calendar_open ||
                           db->clock_menu_open) : 0;
}

void
tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        /* The same state a click on the leaf sets.  Exposed because the
         * keyboard has to reach it too, and a synthetic click would have
         * to invent coordinates for a button whose position is layout's
         * business (DB-011). */
        db_build_leaf(db);         /* from disk, every time (DB-005) */
        db->menu_open = 1;
        db_reset_menu_hots(db);
        db->win_menu_team = -1;
    }
}

void
tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        db->menu_open = 0;
        db->calendar_open = 0;
        db->clock_menu_open = 0;
        db->win_menu_team = -1;
    }
}

int
tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->calendar_open : 0;
}

int
tiku_trk_deskbar_clock_visible(const tiku_trk_deskbar_t *db)
{
    return db != NULL && db->settings.show_clock;
}

void
tiku_trk_deskbar_set_clock_visible(tiku_trk_deskbar_t *db, int visible)
{
    if (db == NULL) { return; }
    db->settings.show_clock = !!visible;
    if (!db->settings.show_clock) {
        db->calendar_open = 0;
    }
}

void
tiku_trk_deskbar_close_calendar(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        db->calendar_open = 0;
        db->clock_menu_open = 0;
    }
}
