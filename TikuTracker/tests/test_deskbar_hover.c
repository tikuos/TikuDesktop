/* Delayed Deskbar submenus, without sleeping or sending real commands.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../src/deskbar/bar/tiku_trk_deskbar_private.h"

/** @brief A leaf's real on-screen center, for every dock and font size. */
static tiku_rect_t row_rect(tiku_trk_deskbar_t *db, int row)
{
    int h = tiku_font_plain()->height+6;
    tiku_rect_t r = db_leaf_menu_rect(db, h, db->leaf_count);
    r.y += 2+row*h; r.h = h; return r;
}

/** @brief Input geometry, not direct mutation of the pending timer. */
static void hover(tiku_trk_deskbar_t *db, int row, int64_t now)
{
    tiku_rect_t r = row_rect(db, row);
    (void)tiku_trk_deskbar_motion(db, r.x+r.w/2, r.y+r.h/2);
    assert(!tiku_trk_deskbar_menu_tick(db, now));
}

/** @brief Find a plain row by what it does, independent of labels. */
static int row_of(tiku_trk_deskbar_t *db, int command)
{
    for (int i = 0; i < db->leaf_count; i++)
        if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_ROW &&
            db->leaf[i].command == command) return i;
    return -1;
}

/** @brief Find an expanding row by what it opens, independent of labels. */
static int parent(tiku_trk_deskbar_t *db, int expand, int which)
{
    for (int i = 0; i < db->leaf_count; i++)
        if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_EXPAND &&
            (int)db->leaf[i].expand == expand && db->leaf[i].which == which)
            return i;
    assert(0); return -1;
}

/** @brief A known menu, with all application categories and recent folders. */
static tiku_trk_deskbar_t *fixture(tiku_surface_t *s, int dock)
{
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL); assert(db);
    tiku_trk_deskbar_set_dock(db, dock);
    for (int i = 0; i < TIKU_TRK_DB_GROUP_COUNT; i++)
        assert(tiku_trk_deskbar_add_app_in(db, i, "Example",
                                           "/test/example.so", "application"));
    const char paths[1][TIKU_PATH_MAX] = {"/test/recent"};
    tiku_trk_deskbar_set_recent(db, TIKU_TRK_DB_RECENT_FOLDERS, paths, 1);
    tiku_trk_deskbar_draw(db, s);
    tiku_trk_deskbar_open_menu(db);
    for (int i = 0; i < db->leaf_count; i++) {
        assert(db->leaf[i].command != TIKU_TRK_DB_SHOW_ABOUT);
        assert(db->leaf[i].command != TIKU_TRK_DB_HELP);
    }
    return db;
}

/** @brief Exact deadline, stable hover, moving into the child, and switching. */
static void tracking(tiku_surface_t *s, int dock)
{
    tiku_trk_deskbar_t *db = fixture(s, dock);
    int settings = parent(db, TIKU_TRK_DB_EXPAND_SETTINGS, 0);
    int apps = parent(db, TIKU_TRK_DB_EXPAND_APPS, TIKU_TRK_DB_GROUP_APPS);
    int64_t delay = TIKU_TRK_DB_SUBMENU_DELAY_US, now = 1000000;
    hover(db, settings, now);
    hover(db, settings, now+delay/2); /* Jitter doesn't restart the dwell. */
    assert(!tiku_trk_deskbar_menu_tick(db, now+delay-1) && db->sub_of == -1);
    assert(tiku_trk_deskbar_menu_tick(db, now+delay));
    assert(db->sub_of == settings && db->sub_count >= 6);
    assert(!tiku_trk_deskbar_menu_tick(db, now+5*delay));
    tiku_rect_t main = row_rect(db, 0);
    main = db_leaf_menu_rect(db, main.h, db->leaf_count);
    tiku_rect_t sub = db_sub_menu_rect(db, main, tiku_font_plain()->height+6);
    int gap = sub.x < main.x ? main.x-1 : main.x+main.w;
    (void)tiku_trk_deskbar_motion(db, gap, sub.y+10);
    assert(tiku_trk_deskbar_menu_keeps_open(db, gap, sub.y+10));
    assert(!tiku_trk_deskbar_menu_tick(db, now+6*delay));
    (void)tiku_trk_deskbar_motion(db, sub.x+8, sub.y+10);
    assert(db->sub_hot == 0 && db->leaf_hot == -1);
    assert(!tiku_trk_deskbar_menu_tick(db, now+7*delay));
    assert(db->sub_of == settings);
    now += 8*delay;
    hover(db, apps, now);
    assert(!tiku_trk_deskbar_menu_tick(db, now+delay-1));
    assert(db->sub_of == settings);
    assert(tiku_trk_deskbar_menu_tick(db, now+delay));
    assert(db->sub_of == apps && db->sub_count == 1);
    now += 2*delay;
    hover(db, row_of(db, TIKU_TRK_DB_FIND), now);
    assert(tiku_trk_deskbar_menu_tick(db, now+delay));
    assert(db->menu_open && db->sub_of == -1); /* No Find command selected. */
    tiku_trk_deskbar_free(db);
}

/** @brief Leaving/reopening cancels old timers; clicks still open immediately. */
static void cancellation(tiku_surface_t *s)
{
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED);
    int settings = parent(db,TIKU_TRK_DB_EXPAND_SETTINGS,0);
    int apps = parent(db,TIKU_TRK_DB_EXPAND_APPS,TIKU_TRK_DB_GROUP_APPS);
    int64_t delay = TIKU_TRK_DB_SUBMENU_DELAY_US;
    hover(db,settings,0);
    (void)tiku_trk_deskbar_motion(db,10,600);
    assert(!tiku_trk_deskbar_menu_tick(db,2*delay) && db->sub_of == -1);
    hover(db,settings,3*delay);
    tiku_trk_deskbar_close_menu(db);
    tiku_trk_deskbar_open_menu(db);
    assert(!tiku_trk_deskbar_menu_tick(db,5*delay) && db->sub_of == -1);
    hover(db,settings,6*delay);
    tiku_rect_t r = row_rect(db,apps);
    tiku_trk_db_pick_t pick;
    assert(tiku_trk_deskbar_click(db,r.x+8,r.y+8,0,1,1,&pick));
    assert(pick.what == TIKU_TRK_DB_NONE && db->sub_of == apps);
    assert(!tiku_trk_deskbar_menu_tick(db,8*delay) && db->sub_of == apps);
    /* Cross out and back between ticks: the earlier dwell is still canceled. */
    hover(db,settings,9*delay);
    (void)tiku_trk_deskbar_motion(db,10,600);
    hover(db,settings,10*delay);
    assert(!tiku_trk_deskbar_menu_tick(db,11*delay-1));
    assert(tiku_trk_deskbar_menu_tick(db,11*delay));
    /* Separators and disabled rows never arm a submenu. */
    for (int i = 0; i < 2; i++) {
        int row = db->leaf_count++;
        db->leaf[row].kind = i ? TIKU_TRK_DB_ITEM_DISABLED : TIKU_TRK_DB_ITEM_SEP;
        hover(db,row,12*delay);
        assert(!tiku_trk_deskbar_menu_tick(db,14*delay));
        assert(db->sub_of == settings);
    }
    tiku_trk_deskbar_free(db);
}

/** @brief Every advertised kind expands, including safe shutdown choices. */
static void categories(tiku_surface_t *s)
{
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED);
    int64_t now = 0, delay = TIKU_TRK_DB_SUBMENU_DELAY_US;
    int tested = 0;
    for (int i = 0; i < db->leaf_count; i++) {
        int kind = db->leaf[i].kind;
        if (kind != TIKU_TRK_DB_ITEM_EXPAND) continue;
        hover(db,i,now);
        assert(tiku_trk_deskbar_menu_tick(db,now+delay));
        assert(db->sub_of == i && db->sub_count > 0 && db->menu_open);
        now += 2*delay; tested++;
    }
    assert(tested == 6);
    tiku_trk_deskbar_free(db);
}

static int scans;
/** @brief An empty device/folder must not be polled repeatedly while hovered. */
static int empty_list(tiku_backend_t *b, const char *path,
                       tiku_entry_fn fn, void *ctx)
{
    (void)b; (void)path; (void)fn; (void)ctx;
    scans++; return 0;
}

/** @brief Empty directory attempts are latched until the pointer leaves. */
static void empty_folder(tiku_surface_t *s)
{
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED);
    const tiku_backend_ops_t ops = {.list=empty_list};
    tiku_backend_t backend = {.ops=&ops};
    db->backend = &backend;
    int row = db->leaf_count++;
    db->leaf[row].kind = TIKU_TRK_DB_ITEM_EXPAND;
    db->leaf[row].expand = TIKU_TRK_DB_EXPAND_DIR;
    hover(db,row,0);
    assert(!tiku_trk_deskbar_menu_tick(db,500000));
    for (int64_t t = 600000; t < 5000000; t += 100000)
        assert(!tiku_trk_deskbar_menu_tick(db,t));
    assert(scans == 1 && db->sub_of == -1);
    (void)tiku_trk_deskbar_motion(db,10,600);
    hover(db,row,6000000);
    assert(!tiku_trk_deskbar_menu_tick(db,6500000) && scans == 2);
    tiku_trk_deskbar_free(db);
}

/** @brief What a hover changed is a menu's worth, said once, then empty. */
static void hover_area(tiku_surface_t *s)
{
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED);
    int h = tiku_font_plain()->height+6;
    tiku_rect_t menu = db_leaf_menu_rect(db, h, db->leaf_count);
    int settings = parent(db, TIKU_TRK_DB_EXPAND_SETTINGS, 0);
    tiku_rect_t r = row_rect(db, settings), area;
    assert(tiku_trk_deskbar_hover_area(db).w == 0); /* nothing yet */
    assert(tiku_trk_deskbar_motion(db, r.x+r.w/2, r.y+r.h/2));
    area = tiku_trk_deskbar_hover_area(db);
    assert(area.w > 0 && area.h > 0);
    assert(area.x <= menu.x && area.y <= menu.y &&
           area.x+area.w >= menu.x+menu.w && area.y+area.h >= menu.y+menu.h);
    assert(area.w < s->w/2 && area.h < s->h); /* a menu, not the desk */
    assert(tiku_trk_deskbar_hover_area(db).w == 0); /* taken once */
    assert(!tiku_trk_deskbar_motion(db, r.x+r.w/2+1, r.y+r.h/2));
    assert(tiku_trk_deskbar_hover_area(db).w == 0); /* nothing moved */
    tiku_trk_deskbar_free(db);
}

/** @brief The leaf is drawn held down while its menu is out, and not after. */
static void pressed_look(tiku_surface_t *s)
{
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED);
    /* The inner top-left corner of the leaf is its bevel's light edge. */
    int x = db->frame.x + 3, y = db->frame.y + 3;

    tiku_trk_deskbar_close_menu(db);
    tiku_trk_deskbar_draw(db, s);
    tiku_rgb_t up = s->px[y * s->w + x];
    tiku_trk_deskbar_open_menu(db);
    tiku_trk_deskbar_draw(db, s);
    tiku_rgb_t held = s->px[y * s->w + x];
    assert(held != up);
    assert((held & 0xff) < (up & 0xff));         /* darker: the light left */
    tiku_trk_deskbar_close_menu(db);
    tiku_trk_deskbar_draw(db, s);
    assert(s->px[y * s->w + x] == up);           /* and back when it goes */
    tiku_trk_deskbar_free(db);
}

int main(void)
{
    tiku_surface_t *s = tiku_surface_new(1000,680,0); assert(s);
    tracking(s,TIKU_TRK_DB_RIGHT_EXPANDED);
    tracking(s,TIKU_TRK_DB_LEFT_EXPANDED);
    cancellation(s); categories(s); empty_folder(s); hover_area(s);
    pressed_look(s);
    tiku_surface_free(s);
    puts("PASS: Deskbar hover delay, switching, cancellation, clicks and safety");
    return 0;
}
