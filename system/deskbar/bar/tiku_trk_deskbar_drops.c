/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_drops.c - drops onto the bar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

tiku_trk_db_drop_t
tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                         int *which)
{
    tiku_rect_t f;
    int ry, i;

    if (which != NULL) {
        *which = -1;
    }
    if (db == NULL || !tiku_trk_deskbar_hit(db, x, y)) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    f = tiku_trk_deskbar_frame(db);
    ry = y - f.y;
    if (ry < db_leaf_row()) {
        return TIKU_TRK_DB_DROP_LEAF;
    }
    ry -= db_leaf_row() + PAD;
    for (i = 0; i < db->team_count; i++) {
        int team = db->order[i];
        int team_h = (db->settings.icon_size + 4 > db_team_row())
                         ? db->settings.icon_size + 4 : db_team_row();

        if (ry >= 0 && ry < team_h) {
            if (which != NULL) { *which = team; }
            return TIKU_TRK_DB_DROP_TEAM;
        }
        ry -= team_h + SEAM;
        if (db->teams[team].expanded) {
            /* A window row is not a drop target of its own: the drop
             * belongs to the application that owns it. */
            ry -= db->teams[team].win_count * (db_win_row() + SEAM);
        }
    }
    if (tiku_trk_deskbar_vertical(db->dock) &&
        y >= f.y + f.h - (db->tray_count * TRAY_ROW) -
            (db->settings.show_clock ? db_clock_row() : 0) - PAD) {
        /* The tray and its clock: a dropped node is a subscription
         * request (DB-038). */
        return TIKU_TRK_DB_DROP_TRAY;
    }
    return TIKU_TRK_DB_DROP_BAR;
}

void
tiku_trk_deskbar_set_dragging(tiku_trk_deskbar_t *db, int dragging)
{
    if (db != NULL) {
        db->dragging = dragging;
        if (!dragging) {
            /* The highlight belongs to the drag: when one ends there is
             * nothing for it to be promising. */
            db->drop_team = -1;
        }
    }
}

void
tiku_trk_deskbar_set_drop_team(tiku_trk_deskbar_t *db, int i)
{
    if (db != NULL) {
        db->drop_team = i;
    }
}

int
tiku_trk_deskbar_drop_team(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->drop_team : -1;
}

int
tiku_trk_deskbar_item_enabled(const tiku_trk_deskbar_t *db, int fixed)
{
    if (db == NULL) {
        return 1;
    }
    /* Only the FIXED items go dead while a drag is in flight; folders and
     * recents stay live, because those are exactly the drop targets the
     * menu exists to offer during one (DB-008). */
    return fixed ? !db->dragging : 1;
}

int
tiku_trk_deskbar_team_type(tiku_trk_deskbar_t *db, int i, const char *type)
{
    if (db == NULL || i < 0 || i >= db->team_count || type == NULL ||
        db->teams[i].ntype >= 8) {
        return -1;
    }
    snprintf(db->teams[i].type[db->teams[i].ntype],
             sizeof db->teams[i].type[0], "%s", type);
    db->teams[i].ntype++;
    return 0;
}

int
tiku_trk_deskbar_team_accepts(const tiku_trk_deskbar_t *db, int i,
                              const char *const *types, int ntypes,
                              int force)
{
    int t, k;

    if (db == NULL || i < 0 || i >= db->team_count) {
        return 0;
    }
    if (force || db->teams[i].takes_anything) {
        return 1;
    }
    if (ntypes <= 0) {
        return 0;                   /* nothing known about them: refuse */
    }
    /* EVERY type must be one it opens: handing an application a mixed
     * selection it can only half read is a drop that half works. */
    for (t = 0; t < ntypes; t++) {
        int ok = 0;

        for (k = 0; k < db->teams[i].ntype; k++) {
            if (strcmp(db->teams[i].type[k], types[t]) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_deskbar_launchable(const tiku_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    /* A directory is silently ignored rather than refused: launching one
     * has no meaning, and complaining about something the user very likely
     * did by accident would be noise (DB-066). */
    return !tiku_model_is_container(m);
}

tiku_trk_db_drop_t
tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                              char *path, size_t max)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t m;
    int rowh, i;

    if (path != NULL && max > 0u) {
        path[0] = '\0';
    }
    if (db == NULL || !db->menu_open) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    rowh = f->height + 6;
    m = db_leaf_menu_rect(db, rowh, db->leaf_count);
    if (x < m.x || x >= m.x + m.w || y < m.y + 2 ||
        y >= m.y + 2 + db->leaf_count * rowh) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    i = (y - m.y - 2) / rowh;
    if (i < 0 || i >= db->leaf_count) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    /* A FOLDER takes the links; a file in the menu is launched with them;
     * everything else is fixed, and fixed items are not drop targets at
     * all while a drag is in flight (DB-008). */
    if ((db->leaf[i].kind == TIKU_TRK_DB_ITEM_EXPAND &&
         db->leaf[i].expand == TIKU_TRK_DB_EXPAND_DIR) ||
        (db->leaf[i].kind == TIKU_TRK_DB_ITEM_ROW &&
         db->leaf[i].command == TIKU_TRK_DB_OPEN_PATH &&
         db->leaf[i].path[0] == '/' && db->leaf[i].label[0] == '/')) {
        if (path != NULL) {
            snprintf(path, max, "%s", db->leaf[i].path);
        }
        return TIKU_TRK_DB_DROP_MENU_DIR;
    }
    if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_ROW &&
        db->leaf[i].command == TIKU_TRK_DB_OPEN_PATH) {
        if (path != NULL) {
            snprintf(path, max, "%s", db->leaf[i].path);
        }
        return TIKU_TRK_DB_DROP_MENU_FILE;
    }
    return TIKU_TRK_DB_DROP_NONE;
}

void
tiku_trk_deskbar_set_clock(tiku_trk_deskbar_t *db, time_t at)
{
    if (db != NULL) {
        db->fixed_now = at;
    }
}

int
tiku_trk_deskbar_clock_stamp(const tiku_trk_deskbar_t *db, char *out,
                             size_t max)
{
    time_t now;
    struct tm tmv;

    if (db == NULL || out == NULL || max == 0u || !db->settings.show_clock) {
        if (out != NULL && max > 0u) {
            out[0] = '\0';
        }
        return 0;
    }
    now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
    (void)localtime_r(&now, &tmv);
    /* Exactly the fields the rendering varies with: a repaint is owed
     * only when one of them turns over (DB-044). */
    (void)strftime(out, max, db->settings.show_seconds
                                 ? "%a %e %b %H:%M:%S %Z"
                                 : "%a %e %b %H:%M %Z", &tmv);
    return 1;
}
