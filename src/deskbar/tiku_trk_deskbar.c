/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar.c - the Deskbar.
 *
 * Laid out as the original is: the leaf at the head, then the application
 * list -- each application's windows nested underneath when it is expanded --
 * then the tray, whose first row leaves room for the clock.  Behaviour
 * follows the DB-* rows read from Deskbar's own source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>

/* Metrics read out of Deskbar's own source rather than estimated.  Rows do
 * not tile: each is drawn one pixel short and the seam between them is left
 * unpainted, so the pitch is the row plus SEAM. */
#define BAR_W        134     /* gMinimumWindowWidth at the 12 px face      */
#define LEAF_H        26
#define TEAM_H        20     /* kMinTeamItemHeight; pitch 21               */
#define WIN_H         19     /* pitch 20                                   */
#define SEAM           1     /* kGutter: the unpainted row between items   */
#define WIN_INDENT    22
#define TRAY_ROW      18     /* kGutter + fMaxReplicantHeight + kGutter    */
#define CLOCK_H       20
#define PAD            4
#define EXPANDER_W    13     /* ExpanderBounds() is 13 px, not 12          */
#define EXPANDER_ARM   5
#define EXPANDER_PEN   2
#define TRAY_PAD       3     /* fTrayPadding                               */
#define CLOCK_MARGIN  12     /* fClockMargin = fTrayPadding * 4            */

struct tiku_trk_deskbar {
    tiku_trk_backend_t  *backend;
    tiku_trk_store_t     *store;
    tiku_trk_db_dock_t   dock;
    tiku_trk_team_t      teams[TIKU_TRK_DB_TEAMS_MAX];
    int                  order[TIKU_TRK_DB_TEAMS_MAX];
    int                  dragging;
    int                  drop_team;
    int                  team_count;
    tiku_trk_replicant_t tray[TIKU_TRK_DB_TRAY_MAX];
    int                  tray_count;
    int                  width;     /* the short axis, user-set        */
    tiku_trk_deskbar_settings_t settings;
    int                  autohide;
    int                  autoraise;
    int                  show_replicants;
    int                  pointer_on;  /* the pointer is over the bar    */
    int                  scroll;      /* first application row shown    */
    int                  overflow;    /* the list ran past the tray     */
    int                  menu_open;
    int                  calendar_open;
    int                  clock_menu_open;
    int                  calendar_year;
    int                  calendar_month;
    int                  calendar_day;
    int                  win_menu_team;    /* -1 = none showing        */
    /* The leaf menu, built from the namespace every time it opens rather
     * than compiled in: a file put in the Deskbar folder has to appear
     * without restarting anything (DB-005). */
    tiku_trk_db_item_t   leaf[TIKU_TRK_DB_LEAF_MAX];
    int                  leaf_count;
    int                  sub_of;           /* leaf row whose submenu is
                                            * open, or -1              */
    tiku_trk_db_item_t   sub[TIKU_TRK_DB_SUB_MAX];
    int                  sub_count;
    char                 menu_dir[TIKU_TRK_PATH_MAX];
    char                 menu_dir_fallback[TIKU_TRK_PATH_MAX];
    char                 recent[TIKU_TRK_DB_RECENT_KINDS]
                               [TIKU_TRK_DB_RECENT_MAX][TIKU_TRK_PATH_MAX];
    int                  recent_count[TIKU_TRK_DB_RECENT_KINDS];
    tiku_desk_rect_t     frame;
};

static void
save_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_TRK_PATH_MAX + 20)];
    size_t at = 0;
    int i;

    if (db == NULL || db->store == NULL ||
        !tiku_trk_store_writable(db->store)) {
        return;
    }
    data[0] = '\0';
    for (i = 0; i < db->tray_count; i++) {
        int n = snprintf(data + at, sizeof data - at, "%s\t%s\n",
                         db->tray[i].label, db->tray[i].path);
        if (n < 0 || (size_t)n >= sizeof data - at) {
            break;
        }
        at += (size_t)n;
    }
    (void)tiku_trk_state_write(db->store, TIKU_TRK_KEY_SESSION,
                               TIKU_TRK_ATTR_REPLICANTS, data, at);
    (void)tiku_trk_store_flush(db->store);
}

/** @brief The last component of @p path, which is what a menu row shows. */
static const char *
leaf_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

static int
calendar_days(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31,
                                 30, 31 };
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

    if (month == 2) {
        return days[1] + leap;
    }
    return (month >= 1 && month <= 12) ? days[month - 1] : 31;
}

static void
calendar_shift(tiku_trk_deskbar_t *db, int months, int years)
{
    int month;

    if (db == NULL) {
        return;
    }
    month = db->calendar_month + months + years * 12;
    while (month < 1) { month += 12; db->calendar_year--; }
    while (month > 12) { month -= 12; db->calendar_year++; }
    db->calendar_month = month;
    if (db->calendar_day > calendar_days(db->calendar_year, month)) {
        db->calendar_day = calendar_days(db->calendar_year, month);
    }
}

static void
add_item(tiku_trk_deskbar_t *db, tiku_trk_db_item_kind_t kind,
         const char *label, const char *path, int which)
{
    tiku_trk_db_item_t *it;

    if (db->leaf_count >= TIKU_TRK_DB_LEAF_MAX) {
        return;
    }
    it = &db->leaf[db->leaf_count++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->which = which;
    snprintf(it->label, sizeof it->label, "%.*s", (int)sizeof it->label - 1,
             (label != NULL) ? label : "");
    snprintf(it->path, sizeof it->path, "%s", (path != NULL) ? path : "");
}

/** @brief Collects one directory entry into the menu being built. */
struct menu_walk {
    tiku_trk_deskbar_t *db;
    int                 n;
    tiku_trk_db_item_t *into;
    int                 max;
};

static int
menu_entry(const tiku_trk_model_t *m, void *ctx)
{
    struct menu_walk *w = ctx;

    if (m == NULL || w->n >= w->max || m->name[0] == '.') {
        return 0;               /* a dot file is not a menu entry */
    }
    if (w->into != NULL) {
        tiku_trk_db_item_t *it = &w->into[w->n++];

        memset(it, 0, sizeof *it);
        it->kind = tiku_trk_model_is_container(m) ? TIKU_TRK_DB_ITEM_DIR
                                                  : TIKU_TRK_DB_ITEM_PATH;
        snprintf(it->label, sizeof it->label, "%.*s",
                 (int)sizeof it->label - 1, m->name);
        snprintf(it->path, sizeof it->path, "%s", m->path);
    } else {
        add_item(w->db, tiku_trk_model_is_container(m)
                            ? TIKU_TRK_DB_ITEM_DIR : TIKU_TRK_DB_ITEM_PATH,
                 m->name, m->path, 0);
        w->n++;
    }
    return 0;
}

/** @brief The Deskbar folder to read: the user's, or the system copy. */
static const char *
menu_dir_of(const tiku_trk_deskbar_t *db)
{
    tiku_trk_model_t m;

    if (db->backend == NULL || db->backend->ops->stat == NULL) {
        return "";
    }
    if (db->menu_dir[0] != '\0' &&
        db->backend->ops->stat(db->backend, db->menu_dir, &m) == 0 &&
        tiku_trk_model_is_container(&m)) {
        return db->menu_dir;
    }
    if (db->menu_dir_fallback[0] != '\0' &&
        db->backend->ops->stat(db->backend, db->menu_dir_fallback, &m) == 0 &&
        tiku_trk_model_is_container(&m)) {
        /* No per-user folder: the system copy is what the menu shows,
         * rather than nothing at all (DB-003). */
        return db->menu_dir_fallback;
    }
    return "";
}

/** @brief Build the leaf menu from what is there NOW (DB-002, DB-005). */
static void
build_leaf(tiku_trk_deskbar_t *db)
{
    static const char *const kRecentLabel[TIKU_TRK_DB_RECENT_KINDS] = {
        "Recent documents", "Recent folders", "Recent applications"
    };
    const char *dir;
    int i;

    db->leaf_count = 0;
    db->sub_of = -1;
    add_item(db, TIKU_TRK_DB_ITEM_ABOUT, "About this system", NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_FIND, "Find\xe2\x80\xa6", NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_PREFS, "Deskbar preferences\xe2\x80\xa6",
             NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_SHOW_REPLICANTS,
             db->show_replicants ? "Hide replicants" : "Show replicants",
             NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_PATH, "/sys", "/sys", 0);
    add_item(db, TIKU_TRK_DB_ITEM_PATH, "/dev", "/dev", 0);
    add_item(db, TIKU_TRK_DB_ITEM_PATH, "/data", "/data", 0);
    {
        int any = 0;

        for (i = 0; i < TIKU_TRK_DB_RECENT_KINDS; i++) {
            if (db->recent_count[i] > 0 &&
                db->settings.recent_enabled[i] &&
                db->settings.recent_count[i] > 0) {
                if (!any) {
                    add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, 0);
                    any = 1;
                }
                /* A list nobody has anything in is not shown at all --
                 * an empty submenu is a promise of nothing (DB-074). */
                add_item(db, TIKU_TRK_DB_ITEM_RECENT, kRecentLabel[i], NULL,
                         i);
            }
        }
    }
    add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, 0);
    dir = menu_dir_of(db);
    if (dir[0] != '\0' && db->backend->ops->list != NULL) {
        struct menu_walk w;

        w.db = db;
        w.n = 0;
        w.into = NULL;
        w.max = TIKU_TRK_DB_LEAF_MAX;
        (void)db->backend->ops->list(db->backend, dir, menu_entry, &w);
        if (w.n == 0) {
            add_item(db, TIKU_TRK_DB_ITEM_DISABLED,
                     "<Deskbar folder is empty>", NULL, 0);
        }
    } else {
        add_item(db, TIKU_TRK_DB_ITEM_DISABLED, "<Deskbar folder is empty>",
                 NULL, 0);
    }
    add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_QUIT, "Shut down\xe2\x80\xa6", NULL, 0);
}

void
tiku_trk_deskbar_set_menu_dir(tiku_trk_deskbar_t *db, const char *user,
                              const char *fallback)
{
    char u[TIKU_TRK_PATH_MAX], fb[TIKU_TRK_PATH_MAX];

    if (db == NULL) {
        return;
    }
    /* Copied through the stack: a caller may well hand back the strings
     * this very bar reported, and a snprintf onto its own source is
     * undefined. */
    snprintf(u, sizeof u, "%s", (user != NULL) ? user : "");
    snprintf(fb, sizeof fb, "%s", (fallback != NULL) ? fallback : "");
    memcpy(db->menu_dir, u, sizeof db->menu_dir);
    memcpy(db->menu_dir_fallback, fb, sizeof db->menu_dir_fallback);
}

const char *
tiku_trk_deskbar_menu_dir(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? menu_dir_of(db) : "";
}

const char *
tiku_trk_deskbar_user_menu_dir(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->menu_dir : "";
}

void
tiku_trk_deskbar_set_recent(tiku_trk_deskbar_t *db,
                            tiku_trk_db_recent_t which,
                            const char paths[][TIKU_TRK_PATH_MAX], int n)
{
    int i;

    if (db == NULL || (int)which < 0 ||
        (int)which >= TIKU_TRK_DB_RECENT_KINDS) {
        return;
    }
    if (n > TIKU_TRK_DB_RECENT_MAX) {
        n = TIKU_TRK_DB_RECENT_MAX;
    }
    for (i = 0; i < n && paths != NULL; i++) {
        snprintf(db->recent[which][i], TIKU_TRK_PATH_MAX, "%s", paths[i]);
    }
    db->recent_count[which] = (paths != NULL && n > 0) ? n : 0;
}

int
tiku_trk_deskbar_leaf_count(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->leaf_count : 0;
}

const tiku_trk_db_item_t *
tiku_trk_deskbar_leaf_item(const tiku_trk_deskbar_t *db, int i)
{
    if (db == NULL || i < 0 || i >= db->leaf_count) {
        return NULL;
    }
    return &db->leaf[i];
}

tiku_trk_deskbar_t *
tiku_trk_deskbar_new(tiku_trk_backend_t *backend)
{
    tiku_trk_deskbar_t *db = calloc(1, sizeof *db);

    if (db != NULL) {
        db->backend = backend;
        db->dock = TIKU_TRK_DB_RIGHT_EXPANDED;
        db->win_menu_team = -1;
        tiku_trk_deskbar_settings_default(&db->settings);
    }
    return db;
}

void
tiku_trk_deskbar_free(tiku_trk_deskbar_t *db)
{
    free(db);
}

void
tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db, tiku_trk_db_dock_t dock)
{
    if (db != NULL && dock >= 0 && dock < TIKU_TRK_DB_DOCKS) {
        db->dock = dock;
    }
}

int
tiku_trk_deskbar_vertical(tiku_trk_db_dock_t d)
{
    return (d != TIKU_TRK_DB_TOP && d != TIKU_TRK_DB_BOTTOM);
}

int
tiku_trk_deskbar_mini(tiku_trk_db_dock_t d)
{
    return (d == TIKU_TRK_DB_RIGHT_MINI || d == TIKU_TRK_DB_LEFT_MINI);
}

tiku_trk_db_dock_t
tiku_trk_deskbar_dock_at(int x, int y, int sw, int sh,
                         tiku_trk_db_dock_t now)
{
    int dl = x, dr = sw - x, dt = y, db_ = sh - y;
    int nearest;

    if (sw <= 0 || sh <= 0) {
        return now;
    }
    nearest = dl;
    if (dr < nearest) { nearest = dr; }
    if (dt < nearest) { nearest = dt; }
    if (db_ < nearest) { nearest = db_; }
    /* A drop in the MIDDLE keeps the dock it started from: a drag that
     * ended nowhere in particular is a mis-drag, and moving the bar for it
     * would be worse than ignoring it (DB-048). */
    if (nearest > sw / 4 && nearest > sh / 4) {
        return now;
    }
    if (nearest == dt) { return TIKU_TRK_DB_TOP; }
    if (nearest == db_) { return TIKU_TRK_DB_BOTTOM; }
    {
        /* Down the top third of an edge is the expanded form; further down
         * is the collapsed one -- the same split the little monitor makes
         * between a tall strip and a corner. */
        int mini = (y > sh / 3);

        if (nearest == dl) {
            return mini ? TIKU_TRK_DB_LEFT_MINI : TIKU_TRK_DB_LEFT_EXPANDED;
        }
        return mini ? TIKU_TRK_DB_RIGHT_MINI : TIKU_TRK_DB_RIGHT_EXPANDED;
    }
}

int
tiku_trk_deskbar_set_width(tiku_trk_deskbar_t *db, int w)
{
    if (db == NULL) {
        return 0;
    }
    if (w < TIKU_TRK_DB_MIN_W) { w = TIKU_TRK_DB_MIN_W; }
    if (w > TIKU_TRK_DB_MAX_W) { w = TIKU_TRK_DB_MAX_W; }
    db->width = w;
    return w;
}

int
tiku_trk_deskbar_width(const tiku_trk_deskbar_t *db)
{
    if (db == NULL || db->width < TIKU_TRK_DB_MIN_W) {
        return TIKU_TRK_DB_MIN_W;
    }
    return db->width;
}

void
tiku_trk_deskbar_set_autohide(tiku_trk_deskbar_t *db, int on)
{
    if (db != NULL) {
        db->autohide = !!on;
        db->settings.auto_hide = !!on;
    }
}

void
tiku_trk_deskbar_set_autoraise(tiku_trk_deskbar_t *db, int on)
{
    if (db != NULL) {
        db->autoraise = !!on;
        db->settings.always_on_top = on ? 0 :
                                      db->settings.always_on_top;
        db->settings.auto_raise = !!on;
    }
}

int
tiku_trk_deskbar_pointer(tiku_trk_deskbar_t *db, int x, int y)
{
    int was, on;

    if (db == NULL) {
        return 0;
    }
    was = db->pointer_on;
    /* Judged against the frame the bar would have if it were SHOWN: a
     * hidden bar that tested its sliver could never be reached, because
     * the pointer would have to be inside two pixels to bring it back. */
    on = (x >= db->frame.x && x < db->frame.x + db->frame.w &&
          y >= db->frame.y && y < db->frame.y + db->frame.h);
    if (!on && db->autohide) {
        /* The edge the bar hides against still answers: that sliver is the
         * whole of what is left to touch. */
        if (tiku_trk_deskbar_vertical(db->dock)) {
            on = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                  db->dock == TIKU_TRK_DB_LEFT_MINI)
                     ? (x <= TIKU_TRK_DB_SLIVER)
                     : (x >= db->frame.x + db->frame.w -
                            TIKU_TRK_DB_SLIVER);
        } else {
            on = (db->dock == TIKU_TRK_DB_TOP)
                     ? (y <= TIKU_TRK_DB_SLIVER)
                     : (y >= db->frame.y + db->frame.h -
                            TIKU_TRK_DB_SLIVER);
        }
    }
    db->pointer_on = on;
    return (was != on);
}

int
tiku_trk_deskbar_scroll(tiku_trk_deskbar_t *db, int step)
{
    int was;

    if (db == NULL || !db->overflow) {
        /* Nothing hidden, nothing to scroll: a wheel that slid a list
         * which was entirely visible would move it away for no reason
         * (DB-034). */
        return 0;
    }
    was = db->scroll;
    db->scroll += step;
    if (db->scroll < 0) {
        db->scroll = 0;
    }
    if (db->scroll > db->team_count - 1) {
        db->scroll = (db->team_count > 0) ? db->team_count - 1 : 0;
    }
    return (was != db->scroll);
}

int
tiku_trk_deskbar_scroll_at(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->scroll : 0;
}

int
tiku_trk_deskbar_scrollable(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->overflow : 0;
}

int
tiku_trk_deskbar_shown(const tiku_trk_deskbar_t *db)
{
    if (db == NULL) {
        return 0;
    }
    /* An open menu keeps the bar out whatever the pointer is doing: a bar
     * that slid away under its own menu would take the menu with it. */
    return (!db->autohide || db->pointer_on || db->menu_open ||
            db->calendar_open || db->clock_menu_open);
}

int
tiku_trk_deskbar_raised(const tiku_trk_deskbar_t *db)
{
    if (db == NULL) {
        return 1;
    }
    return (db->settings.always_on_top || db->pointer_on || db->menu_open ||
            db->calendar_open || db->clock_menu_open);
}

static int
team_before(const tiku_trk_deskbar_t *db, int a, int b)
{
    if (db->settings.tracker_first) {
        int at = strcmp(db->teams[a].name, "Tracker") == 0;
        int bt = strcmp(db->teams[b].name, "Tracker") == 0;

        if (at != bt) { return at; }
    }
    if (db->settings.sort_apps) {
        return strcmp(db->teams[a].name, db->teams[b].name) < 0;
    }
    return a < b;
}

static void
sort_teams(tiku_trk_deskbar_t *db)
{
    int i, j;

    for (i = 1; i < db->team_count; i++) {
        int key = db->order[i];

        for (j = i - 1; j >= 0 && team_before(db, key, db->order[j]); j--) {
            db->order[j + 1] = db->order[j];
        }
        db->order[j + 1] = key;
    }
}

void
tiku_trk_deskbar_apply_settings(tiku_trk_deskbar_t *db,
                                const tiku_trk_deskbar_settings_t *settings)
{
    if (db == NULL || settings == NULL) { return; }
    db->settings = *settings;
    tiku_trk_deskbar_settings_normalize(&db->settings);
    db->autohide = db->settings.auto_hide;
    db->autoraise = db->settings.auto_raise;
    sort_teams(db);
}

tiku_trk_db_dock_t
tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->dock : TIKU_TRK_DB_RIGHT_EXPANDED;
}

int
tiku_trk_deskbar_team_count(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->team_count : 0;
}

const char *
tiku_trk_deskbar_team_name(const tiku_trk_deskbar_t *db, int i)
{
    if (db == NULL || i < 0 || i >= db->team_count) {
        return NULL;
    }
    return db->teams[db->order[i]].name;
}

int
tiku_trk_deskbar_add_team(tiku_trk_deskbar_t *db, const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    /* One entry per application however many instances it has (DB-015). */
    for (i = 0; i < db->team_count; i++) {
        if (strcmp(db->teams[i].name, name) == 0) {
            return i;
        }
    }
    if (db->team_count >= TIKU_TRK_DB_TEAMS_MAX) {
        return -1;
    }
    i = db->team_count++;
    db->order[i] = i;
    memset(&db->teams[i], 0, sizeof db->teams[i]);
    snprintf(db->teams[i].name, sizeof db->teams[i].name, "%s", name);
    /* Tracker opens everything: it is the thing that would be asked to
     * open anything nothing else claims. */
    db->teams[i].takes_anything = (strcmp(name, "Tracker") == 0);
    db->teams[i].expanded = db->settings.expand_new_apps;
    sort_teams(db);
    return i;
}

void
tiku_trk_deskbar_strip_title(const char *app, const char *title, char *out,
                             size_t max)
{
    size_t al, tl;
    const char *p = title;

    if (app == NULL || title == NULL || out == NULL) {
        return;
    }
    al = strlen(app);
    /* "<app>: rest" -> "rest" */
    if (strncmp(p, app, al) == 0 && p[al] == ':') {
        p += al + 1;
        while (*p == ' ') { p++; }
    }
    snprintf(out, max, "%s", p);
    /* "rest - <app>" -> "rest" */
    tl = strlen(out);
    if (tl > al + 3 && strcmp(out + tl - al, app) == 0 &&
        out[tl - al - 1] == ' ' && out[tl - al - 2] == '-') {
        out[tl - al - 3] = '\0';
    }
}

/** @brief Natural order, so "Doc 2" sorts before "Doc 10" (DB row). */
static int
natural_less(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            long x = 0, y = 0;
            while (*a >= '0' && *a <= '9') { x = x * 10 + (*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { y = y * 10 + (*b++ - '0'); }
            if (x != y) { return (x < y); }
            continue;
        }
        if (*a != *b) { return ((unsigned char)*a < (unsigned char)*b); }
        a++;
        b++;
    }
    return (*a == '\0' && *b != '\0');
}

void
tiku_trk_deskbar_set_windows(tiku_trk_deskbar_t *db, int team,
                             const tiku_trk_dbwin_t *wins, int n, int active)
{
    tiku_trk_team_t *t;
    int i, j;

    if (db == NULL || team < 0 || team >= db->team_count) {
        return;
    }
    t = &db->teams[team];
    if (n > TIKU_TRK_DB_WINS_MAX) {
        n = TIKU_TRK_DB_WINS_MAX;
    }
    for (i = 0; i < n; i++) {
        t->wins[i] = wins[i];
    }
    t->win_count = n;
    t->active = active;
    /* Kept in natural title order, so the list does not reshuffle as
     * windows are raised. */
    for (i = 1; i < n; i++) {
        tiku_trk_dbwin_t key = t->wins[i];
        for (j = i - 1; j >= 0 && natural_less(key.title, t->wins[j].title);
             j--) {
            t->wins[j + 1] = t->wins[j];
        }
        t->wins[j + 1] = key;
    }
}

int
tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                               const char *path)
{
    tiku_trk_replicant_t *r;

    if (db == NULL || db->tray_count >= TIKU_TRK_DB_TRAY_MAX) {
        return -1;
    }
    for (int i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return i;
        }
    }
    r = &db->tray[db->tray_count++];
    snprintf(r->label, sizeof r->label, "%s", label);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->valid = 0;
    r->height = TIKU_TRK_DB_MAX_REPLICANT_H;
    /* Ask the node what its reading MEANS, once, so the tray can show
     * "16h 26m" rather than 59182. */
    if (db->backend != NULL && db->backend->ops->stat != NULL) {
        tiku_trk_model_t m;

        if (db->backend->ops->stat(db->backend, path, &m) == 0) {
            r->desc_known = tiku_trk_desc_parse(m.facts.meta, &r->desc);
        }
    }
    save_replicants(db);
    return 0;
}

int
tiku_trk_deskbar_set_replicant_height(tiku_trk_deskbar_t *db,
                                      const char *path, int height)
{
    int i;

    if (db == NULL || path == NULL || height <= 0 ||
        height > TIKU_TRK_DB_MAX_REPLICANT_H) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            if (db->tray[i].height == height) {
                return 1;
            }
            db->tray[i].height = height;
            /* The fixed row is the tray's layout unit; changing the
             * accepted size still invalidates the whole tray immediately. */
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_replicant_height(const tiku_trk_deskbar_t *db,
                                  const char *path)
{
    int i;

    for (i = 0; db != NULL && path != NULL && i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return db->tray[i].height;
        }
    }
    return 0;
}

void
tiku_trk_deskbar_set_show_replicants(tiku_trk_deskbar_t *db, int show)
{
    if (db != NULL) {
        db->show_replicants = !!show;
    }
}

int
tiku_trk_deskbar_show_replicants(const tiku_trk_deskbar_t *db)
{
    return db != NULL ? db->show_replicants : 0;
}

void
tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db, tiku_trk_store_t *store)
{
    if (db != NULL) {
        db->store = store;
    }
}

int
tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_TRK_PATH_MAX + 20)];
    int n, loaded = 0;
    char *line;

    if (db == NULL || db->store == NULL) {
        return 0;
    }
    n = tiku_trk_state_read(db->store, TIKU_TRK_KEY_SESSION,
                            TIKU_TRK_ATTR_REPLICANTS, data, sizeof data - 1u);
    if (n <= 0) {
        return 0;
    }
    data[n] = '\0';
    line = data;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        char *tab;

        if (next != NULL) { *next++ = '\0'; }
        tab = strchr(line, '\t');
        if (tab != NULL) {
            *tab++ = '\0';
            if (tab[0] != '\0' && db->backend->ops->stat(db->backend, tab,
                                                          &(tiku_trk_model_t){0}) == 0 &&
                tiku_trk_deskbar_add_replicant(db, line, tab) >= 0) {
                loaded++;
            }
        }
        line = next;
    }
    save_replicants(db); /* prune stale entries and normalise the record */
    return loaded;
}

int
tiku_trk_deskbar_remove_replicant(tiku_trk_deskbar_t *db, const char *path)
{
    int i;

    if (db == NULL || path == NULL) { return 0; }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            memmove(&db->tray[i], &db->tray[i + 1],
                    (size_t)(db->tray_count - i - 1) * sizeof db->tray[0]);
            db->tray_count--;
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_repoint_replicant(tiku_trk_deskbar_t *db,
                                   const char *old_path,
                                   const char *new_path)
{
    int i;

    if (db == NULL || old_path == NULL || new_path == NULL) { return 0; }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, old_path) == 0) {
            snprintf(db->tray[i].path, sizeof db->tray[i].path, "%s", new_path);
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        tiku_trk_replicant_t *r = &db->tray[i];
        char buf[64];
        int n = db->backend->ops->read(db->backend, r->path, buf,
                                       sizeof buf - 1);
        char *nl;

        if (n < 0) {
            changed += tiku_trk_deskbar_remove_replicant(db, r->path);
            i--;
            continue;
        }
        buf[n] = '\0';
        nl = strchr(buf, '\n');
        if (nl != NULL) { *nl = '\0'; }
        buf[sizeof r->value - 1u] = '\0';
        if (strcmp(r->value, buf) != 0) {
            memcpy(r->value, buf, strlen(buf) + 1u);
            changed++;
        }
        r->valid = 1;
    }
    return changed;
}

/*---------------------------------------------------------------------------*/
/* layout                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Rows the application list occupies, windows included. */
static int
list_rows(const tiku_trk_deskbar_t *db)
{
    int i, h = 0;
    int team_h = (db->settings.icon_size + 4 > TEAM_H)
                     ? db->settings.icon_size + 4 : TEAM_H;

    for (i = 0; i < db->team_count; i++) {
        h += team_h + SEAM;
        if (db->teams[i].expanded) {
            h += db->teams[i].win_count * (WIN_H + SEAM);
        }
    }
    return h;
}

static int
list_y(const tiku_trk_deskbar_t *db)
{
    (void)db;
    return LEAF_H + PAD;
}

static int
tray_y(const tiku_trk_deskbar_t *db)
{
    return list_y(db) + list_rows(db) + PAD;
}

tiku_desk_rect_t
tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db)
{
    tiku_desk_rect_t r = { 0, 0, 0, 0 };

    return (db != NULL) ? db->frame : r;
}

int
tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y)
{
    tiku_desk_rect_t f;

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
        build_leaf(db);         /* from disk, every time (DB-005) */
        db->menu_open = 1;
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

/*---------------------------------------------------------------------------*/
/* drawing                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief A window's state icon: filled when shown, hollow when minimised. */
static void
draw_win_icon(tiku_desk_surface_t *s, int x, int y, int minimized,
              int current)
{
    tiku_desk_rect_t b = { x, y, 9, 9 };
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);

    tiku_desk_fill(s, b, minimized ? TIKU_DESK_C_PANEL : TIKU_DESK_C_DOC);
    tiku_desk_frame(s, b, dark);
    if (!minimized) {
        /* A shown window carries a title bar on its glyph; a minimised one
         * is an empty outline, so the two are told apart at a glance. */
        tiku_desk_hline(s, b.x + 1, b.y + 1, b.w - 2, TIKU_DESK_C_TAB);
        tiku_desk_hline(s, b.x + 1, b.y + 2, b.w - 2, TIKU_DESK_C_TAB);
    }
    if (current) {
        tiku_desk_frame(s, (tiku_desk_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_DESK_C_SELECT);
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

static tiku_desk_rect_t
leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows)
{
    tiku_desk_rect_t m;

    m.w = 156;
    m.h = rows * rowh + 4;
    m.y = LEAF_H + 2;
    m.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           db->dock == TIKU_TRK_DB_LEFT_MINI)
          ? db->frame.x + db->frame.w + 2
          : db->frame.x - m.w - 2;
    if (m.x < 0) {
        m.x = 0;
    }
    return m;
}

static tiku_desk_rect_t sub_menu_rect(const tiku_trk_deskbar_t *db,
                                      tiku_desk_rect_t m, int rowh);

int
tiku_trk_deskbar_menu_keeps_open(const tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_desk_font_t *font;
    tiku_desk_rect_t m;
    int rowh;

    if (db == NULL || !db->menu_open) {
        return 0;
    }
    font = tiku_desk_font_plain();
    rowh = font->height + 6;
    m = leaf_menu_rect(db, rowh, db->leaf_count);
    /* Tracker's 20px tracking margin is intentionally outside the visible
     * menu: it lets the pointer cross the gap into a submenu without
     * dismissing the menu first. */
    if (x >= m.x - 20 && x < m.x + m.w + 20 &&
        y >= m.y - 20 && y < m.y + m.h + 20) {
        return 1;
    }
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_desk_rect_t sm = sub_menu_rect(db, m, rowh);

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
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t m;
    int rowh, i;

    if (db == NULL || label == NULL || x == NULL || y == NULL ||
        !db->menu_open) {
        return 0;
    }
    rowh = f->height + 6;
    m = leaf_menu_rect(db, rowh, db->leaf_count);
    /* The open submenu first: its rows sit over the leaf's, and a name
     * that appears in both is the one the user can see. */
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_desk_rect_t sm = sub_menu_rect(db, m, rowh);

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
static tiku_desk_rect_t
sub_menu_rect(const tiku_trk_deskbar_t *db, tiku_desk_rect_t m, int rowh)
{
    tiku_desk_rect_t r;

    r.w = 190;
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
static void
open_sub(tiku_trk_deskbar_t *db, int at)
{
    const tiku_trk_db_item_t *it;

    db->sub_count = 0;
    db->sub_of = -1;
    if (at < 0 || at >= db->leaf_count) {
        return;
    }
    it = &db->leaf[at];
    if (it->kind == TIKU_TRK_DB_ITEM_RECENT) {
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
                char nm[TIKU_TRK_PATH_MAX];

                /* Through a local: source and destination are both inside
                 * this bar, and an snprintf between two members of one
                 * object is not something to leave to chance. */
                snprintf(nm, sizeof nm, "%s",
                         leaf_name(db->recent[it->which][i]));
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
        (void)db->backend->ops->list(db->backend, it->path, menu_entry, &w);
        db->sub_count = w.n;
    }
    if (db->sub_count > 0) {
        db->sub_of = at;
    }
}

static void
draw_expander(tiku_desk_surface_t *s, int x, int y, int expanded)
{
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);
    int k;

    /* STROKED along an open three-point polyline with a two-pixel pen, not
     * filled: a solid triangle carries far more weight than the original's
     * arrow and is one of the things that reads as a cheap imitation. */
    for (k = 0; k < EXPANDER_PEN; k++) {
        int i;

        for (i = 0; i < EXPANDER_ARM; i++) {
            if (expanded) {           /* pointing down: \ then /            */
                tiku_desk_pixel(s, x + i, y + i + k, dark);
                tiku_desk_pixel(s, x + 2 * EXPANDER_ARM - 2 - i, y + i + k,
                                dark);
            } else {                  /* pointing right: / then \           */
                tiku_desk_pixel(s, x + i + k, y + i, dark);
                tiku_desk_pixel(s, x + i + k, y + 2 * EXPANDER_ARM - 2 - i,
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
draw_row_bg(tiku_desk_surface_t *s, tiku_desk_rect_t r, int selected)
{
    int i;

    for (i = 0; i < r.h; i++) {
        float t = (r.h > 1) ? (float)i / (float)(r.h - 1) : 0.0f;
        float tint = selected ? (1.25f + (1.20f - 1.25f) * t)
                              : (0.69f + (1.03f - 0.69f) * t);

        tiku_desk_hline(s, r.x, r.y + i, r.w,
                        tiku_desk_tint(TIKU_DESK_C_PANEL, tint));
    }
    if (selected) {
        tiku_desk_hline(s, r.x, r.y, r.w,
                        tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f));
        return;
    }
    tiku_desk_hline(s, r.x, r.y, r.w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 0.50f));
    tiku_desk_vline(s, r.x, r.y, r.h,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 0.70f));
    tiku_desk_vline(s, r.x + r.w - 1, r.y, r.h,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 1.08f));
    tiku_desk_hline(s, r.x, r.y + r.h - 1, r.w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 1.08f));
}

/** @brief Truncate to a width budget, with an ellipsis (DB row). */
static void
fit_label(const tiku_desk_font_t *f, const char *src, int budget, char *out,
          size_t max)
{
    size_t n;

    snprintf(out, max, "%s", src);
    if (tiku_desk_text_width(f, out) <= budget) {
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
        if (tiku_desk_text_width(f, out) <= budget) {
            return;
        }
    }
}

void
tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_desk_surface_t *s)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    const tiku_desk_font_t *fb = tiku_desk_font_bold();
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);
    tiku_desk_rect_t leaf;
    int i, j, y, rows;
    int team_h;

    if (db == NULL || s == NULL) {
        return;
    }
    team_h = (db->settings.icon_size + 4 > TEAM_H)
                 ? db->settings.icon_size + 4 : TEAM_H;
    rows = tiku_trk_deskbar_mini(db->dock) ? 0 : list_rows(db);
    {
        /* How much room the list actually has, once the leaf, the tray and
         * the clock have taken theirs.  A list longer than that is what
         * makes the bar scrollable (DB-034). */
        int room = s->h - (LEAF_H + PAD) - (db->tray_count * TRAY_ROW) -
                   (db->settings.show_clock ? CLOCK_H : 0) - 2 * PAD;

        db->overflow = (!tiku_trk_deskbar_mini(db->dock) && rows > room &&
                        room > 0);
        if (!db->overflow) {
            db->scroll = 0;     /* nothing hidden: nothing scrolled past */
        }
        if (db->overflow && rows > room) {
            rows = room;
        }
    }
    db->frame.w = tiku_trk_deskbar_width(db);
    db->frame.y = 0;
    if (tiku_trk_deskbar_vertical(db->dock)) {
        db->frame.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                       db->dock == TIKU_TRK_DB_LEFT_MINI)
                          ? 0 : s->w - db->frame.w;
        db->frame.h = LEAF_H + PAD + rows + PAD +
                      db->tray_count * TRAY_ROW +
                      (db->settings.show_clock ? CLOCK_H : 0) + PAD;
    } else {
        /* Along an edge: the bar spans the screen and is as deep as one
         * row needs.  The list is not drawn in this mode -- a horizontal
         * application strip is a different control, not this one rotated
         * (DB-033). */
        db->frame.x = 0;
        db->frame.w = s->w;
        db->frame.h = LEAF_H + PAD;
        db->frame.y = (db->dock == TIKU_TRK_DB_TOP)
                          ? 0 : s->h - db->frame.h;
        rows = 0;
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
        tiku_desk_fill(s, db->frame, TIKU_DESK_C_PANEL);
        return;
    }

    tiku_desk_fill(s, db->frame, TIKU_DESK_C_PANEL);
    tiku_desk_bevel(s, db->frame,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_MAX),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));

    /* The leaf: the one control that is not a list item, so it is the one
     * thing wearing the tab colour. */
    leaf = (tiku_desk_rect_t){ db->frame.x + 2, 2, db->frame.w - 4, LEAF_H };
    tiku_desk_fill(s, leaf, TIKU_DESK_C_TAB);
    tiku_desk_frame(s, leaf, dark);
    tiku_desk_bevel(s, tiku_desk_inset(leaf, 1),
                    tiku_desk_tint(TIKU_DESK_C_TAB, TIKU_DESK_LIGHTEN_1),
                    tiku_desk_tint(TIKU_DESK_C_TAB, TIKU_DESK_DARKEN_2));
    (void)tiku_desk_text_centered(s, fb, leaf, "TikuOS", TIKU_DESK_C_TEXT);

    if (!tiku_trk_deskbar_mini(db->dock) &&
        tiku_trk_deskbar_vertical(db->dock)) {
        y = list_y(db);
        for (i = 0; i < db->team_count; i++) {
            int team = db->order[i];
            tiku_trk_team_t *t = &db->teams[team];
            tiku_desk_rect_t row = { db->frame.x + 2, y, db->frame.w - 4,
                                     team_h };
            int icon_size = db->settings.icon_size;
            tiku_desk_rect_t icon = { row.x + 4,
                                      y + (team_h - icon_size) / 2,
                                      icon_size, icon_size };
            char label[TIKU_TRK_DB_LABEL_MAX];

            /* A hovering drag lights the row it would be handed to, so
             * the promise the drop makes is visible before it is made
             * (DB-035). */
            if (db->drop_team == team) {
                tiku_desk_fill(s, row, TIKU_DESK_C_SELECT);
            } else {
                draw_row_bg(s, row, t->active);
            }
            /* The application's icon.  Tracker's own team is the one we can
             * name; anything else gets the generic application icon rather
             * than a blank, so the column never has a hole in it. */
            /* DB-018: when even the generic icon is missing, the slot is
             * left as it is.  A coloured rectangle where an icon failed
             * looks like an application whose icon IS a blue square. */
            (void)tiku_trk_icon_paint(s, strcmp(t->name, "Tracker") == 0
                                         ? "folder" : "application",
                                      icon.x, icon.y, icon_size);

            if (!db->settings.hide_app_names) {
                fit_label(f, t->name,
                          row.w - icon_size - EXPANDER_W - 14, label,
                          sizeof label);
                tiku_desk_text(s, f, icon.x + icon_size + 5,
                               y + (team_h - f->height) / 2 + f->ascent,
                               label, TIKU_DESK_C_TEXT);
            }
            if (db->settings.show_expander) {
                draw_expander(s, row.x + row.w - EXPANDER_W - 2,
                              y + (team_h - 9) / 2, t->expanded);
            }
            y += team_h + SEAM;

            if (!t->expanded) {
                continue;
            }
            for (j = 0; j < t->win_count; j++) {
                tiku_trk_dbwin_t *w = &t->wins[j];
                tiku_desk_rect_t wr = { db->frame.x + 2, y, db->frame.w - 4,
                                        WIN_H };
                char wl[TIKU_TRK_DB_LABEL_MAX];

                draw_row_bg(s, wr, w->current);
                draw_win_icon(s, wr.x + WIN_INDENT,
                              y + (WIN_H - 9) / 2,
                              w->state == TIKU_TRK_DBW_MINIMIZED,
                              0);
                fit_label(f, w->title, wr.w - WIN_INDENT - 20, wl,
                          sizeof wl);
                tiku_desk_text(s, f, wr.x + WIN_INDENT + 14,
                               y + (WIN_H - f->height) / 2 + f->ascent, wl,
                               TIKU_DESK_C_TEXT);
                y += WIN_H + SEAM;
            }
        }
    }

    /* The tray, then the clock beneath it: the original reserves the
     * clock's room in the first row, and stacking it here is the vertical
     * equivalent. */
    y = tiku_trk_deskbar_mini(db->dock) ? LEAF_H + PAD : tray_y(db);
    tiku_desk_hline(s, db->frame.x + 4, y - 3, db->frame.w - 8,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    tiku_desk_hline(s, db->frame.x + 4, y - 2, db->frame.w - 8,
                    tiku_desk_tint(TIKU_DESK_C_PANEL,
                                   TIKU_DESK_LIGHTEN_MAX));
    for (i = 0; i < db->tray_count; i++) {
        const tiku_trk_replicant_t *r = &db->tray[i];
        int ty = y + (TRAY_ROW - f->height) / 2 + f->ascent;
        char val[32];
        int vw;

        /* The tray is the narrowest place on screen, so it is the place a
         * formatted reading earns the most: the column is too thin to read
         * a raw second count or byte count against. */
        (void)tiku_trk_attr_format(r->desc_known ? &r->desc : NULL,
                                   r->valid ? r->value : "",
                                   val, sizeof val);
        vw = tiku_desk_text_width(f, val);

        tiku_desk_text(s, f, db->frame.x + 6, ty, r->label, TIKU_DESK_C_TEXT);
        tiku_desk_text(s, f, db->frame.x + db->frame.w - 6 - vw, ty, val,
                       TIKU_DESK_C_TEXT);
        y += TRAY_ROW;
    }
    if (db->settings.show_clock) {
        time_t now = time(NULL);
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
        cw = tiku_desk_text_width(f, clock);
        tiku_desk_text(s, f, db->frame.x + (db->frame.w - cw) / 2,
                       y + (CLOCK_H - f->height) / 2 + f->ascent, clock,
                       TIKU_DESK_C_TEXT);
    }

    /* The leaf menu: the head the original always shows, then the recent
     * lists, then whatever is in the Deskbar folder right now. */
    if (db->menu_open) {
        const char *items[TIKU_TRK_DB_LEAF_MAX];
        int rowh = f->height + 6;
        tiku_desk_rect_t m = leaf_menu_rect(db, rowh, db->leaf_count);

        for (i = 0; i < db->leaf_count; i++) {
            items[i] = (db->leaf[i].kind == TIKU_TRK_DB_ITEM_SEP)
                           ? "-" : db->leaf[i].label;
        }
        tiku_desk_ui_menu(s, m, items, db->leaf_count, -1);
        for (i = 0; i < db->leaf_count; i++) {
            /* A row that cannot be chosen is drawn as such, and the menu
             * layer has one ink: dimmed here, over what it already drew
             * (DB-004). */
            if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_DISABLED) {
                tiku_desk_rect_t r = { m.x + 2, m.y + 2 + i * rowh,
                                       m.w - 4, rowh };

                tiku_desk_fill(s, r, TIKU_DESK_C_PANEL);
                tiku_desk_text(s, f, r.x + 10,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               db->leaf[i].label,
                               tiku_desk_tint(TIKU_DESK_C_PANEL, 1.55f));
            } else if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_DIR ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_RECENT) {
                /* What expands says so, the way every hierarchical menu
                 * marks the rows that go somewhere (DB-002). */
                tiku_desk_text(s, f, m.x + m.w - 12,
                               m.y + 2 + i * rowh +
                                   (rowh - f->height) / 2 + f->ascent,
                               ">", TIKU_DESK_C_TEXT);
            }
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            const char *sitems[TIKU_TRK_DB_SUB_MAX];
            tiku_desk_rect_t sm = sub_menu_rect(db, m, rowh);

            for (i = 0; i < db->sub_count; i++) {
                sitems[i] = db->sub[i].label;
            }
            tiku_desk_ui_menu(s, sm, sitems, db->sub_count, -1);
        }
    }
    /* The window menu for one application: Hide all / Show all / Close all,
     * which the original appends to every application's window list. */
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        static const char *const items[] = {
            "Hide all", "Show all", "-", "Close all"
        };
        int rowh = f->height + 6;
        tiku_desk_rect_t m = { db->frame.x - 130, list_y(db) + TEAM_H, 128,
                               4 * rowh + 4 };
        tiku_desk_ui_menu(s, m, items, 4, -1);
    }
    if (db->clock_menu_open) {
        const char *items[] = {
            db->settings.show_clock ? "Hide clock" : "Show clock",
            db->settings.show_seconds ? "Hide seconds" : "Show seconds",
            db->settings.show_day_of_week ? "Hide day of week" : "Show day of week",
            db->settings.show_timezone ? "Hide time zone" : "Show time zone"
        };
        tiku_desk_rect_t m = { db->frame.x - 130, db->frame.y + db->frame.h -
                               CLOCK_H - 32, 170, 4 * 28 };
        tiku_desk_ui_menu(s, m, items, 4, -1);
    }
    if (db->calendar_open) {
        /* The clock's calendar: a month grid with steppers, popped beside
         * the bar and dismissed by the next click anywhere. */
        struct tm tmv;
        tiku_desk_rect_t c = { db->frame.x - 190, db->frame.h - 150, 186,
                               148 };
        int col, rowi, first, days;

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = db->calendar_year - 1900;
        tmv.tm_mon = db->calendar_month - 1;
        tmv.tm_mday = 1;
        tmv.tm_isdst = -1;
        (void)mktime(&tmv);
        tiku_desk_fill(s, c, TIKU_DESK_C_PANEL);
        tiku_desk_frame(s, c, dark);
        tiku_desk_bevel(s, tiku_desk_inset(c, 1),
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_LIGHTEN_MAX),
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_DARKEN_2));
        {
            char head[32];
            snprintf(head, sizeof head, "%s %d",
                     (const char *[]){ "January", "February", "March",
                       "April", "May", "June", "July", "August",
                       "September", "October", "November",
                       "December" }[tmv.tm_mon], 1900 + tmv.tm_year);
            (void)tiku_desk_text_centered(s, fb,
                (tiku_desk_rect_t){ c.x, c.y + 2, c.w, 18 }, head,
                TIKU_DESK_C_TEXT);
        }
        first = tmv.tm_wday;
        days = calendar_days(db->calendar_year, db->calendar_month);
        for (rowi = 0; rowi < 6; rowi++) {
            for (col = 0; col < 7; col++) {
                int day = rowi * 7 + col - first + 1;
                char num[12];
                tiku_desk_rect_t cell = { c.x + 6 + col * 25,
                                          c.y + 22 + rowi * 20, 24, 18 };
                if (day < 1 || day > days) {
                    continue;
                }
                if (day == db->calendar_day) {
                    tiku_desk_fill(s, cell, TIKU_DESK_C_SELECT);
                }
                snprintf(num, sizeof num, "%d", day);
                (void)tiku_desk_text_centered(s, f, cell, num,
                    (day == db->calendar_day) ? TIKU_DESK_C_SELTEXT
                                         : TIKU_DESK_C_TEXT);
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* interaction                                                               */
/*---------------------------------------------------------------------------*/

int
tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int secondary, tiku_trk_db_pick_t *pick)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int i, j, ry;

    if (db == NULL || pick == NULL) {
        return 0;
    }
    memset(pick, 0, sizeof *pick);

    /* An open popup owns the next click wherever it lands. */
    if (db->calendar_open) {
        /* A second click on the clock reactivates the existing popup. */
        if (db->settings.show_clock &&
            x >= db->frame.x && x < db->frame.x + db->frame.w &&
            y >= db->frame.y + db->frame.h - CLOCK_H - PAD) {
            return 1;
        }
        {
            tiku_desk_rect_t c = { db->frame.x - 190,
                                   db->frame.y + db->frame.h - 150, 186, 148 };
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
                calendar_shift(db, -1, 0);
                return 1;
            }
            if (x >= c.x + 48 && x < c.x + 92 && y >= c.y + 2 &&
                y < c.y + 20) {
                calendar_shift(db, 1, 0);
                return 1;
            }
            if (x >= c.x + 94 && x < c.x + 138 && y >= c.y + 2 &&
                y < c.y + 20) {
                calendar_shift(db, 0, -1);
                return 1;
            }
            if (x >= c.x + 138 && x < c.x + 182 && y >= c.y + 2 &&
                y < c.y + 20) {
                calendar_shift(db, 0, 1);
                return 1;
            }
            if (x >= c.x + 6 && x < c.x + 181 && y >= c.y + 22 &&
                y < c.y + 142) {
                col = (x - c.x - 6) / 25;
                rowi = (y - c.y - 22) / 20;
                day = rowi * 7 + col - first + 1;
                if (day >= 1 && day <= calendar_days(db->calendar_year,
                                                     db->calendar_month)) {
                    db->calendar_day = day;
                    return 1;
                }
            }
        }
        db->calendar_open = 0;
        return 1;
    }
    if (db->clock_menu_open) {
        tiku_desk_rect_t m = { db->frame.x - 130, db->frame.y + db->frame.h -
                               CLOCK_H - 32, 170, 4 * 28 };
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
    if (db->win_menu_team >= 0) {
        int rowh = f->height + 6;
        tiku_desk_rect_t m = { db->frame.x - 130, list_y(db) + TEAM_H, 128,
                               4 * rowh + 4 };
        int idx = (y - m.y - 2) / rowh;

        db->win_menu_team = -1;
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
            if (idx == 0) { pick->what = TIKU_TRK_DB_HIDE_ALL; }
            else if (idx == 1) { pick->what = TIKU_TRK_DB_SHOW_ALL; }
            else if (idx == 3) { pick->what = TIKU_TRK_DB_CLOSE_ALL; }
        }
        return 1;
    }
    if (db->menu_open) {
        int rowh = f->height + 6;
        tiku_desk_rect_t m = leaf_menu_rect(db, rowh, db->leaf_count);
        int idx = (y - m.y - 2) / rowh;

        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_desk_rect_t sm = sub_menu_rect(db, m, rowh);
            int si = (y - sm.y - 2) / rowh;

            if (x >= sm.x && x < sm.x + sm.w && y >= sm.y &&
                y < sm.y + sm.h && si >= 0 && si < db->sub_count) {
                pick->what = TIKU_TRK_DB_OPEN_PATH;
                snprintf(pick->path, sizeof pick->path, "%s",
                         db->sub[si].path);
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
                /* Expanding is not choosing: the menu stays up, with the
                 * row's contents beside it. */
                open_sub(db, idx);
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
            case TIKU_TRK_DB_ITEM_PREFS:
                pick->what = TIKU_TRK_DB_PREFERENCES;
                break;
            case TIKU_TRK_DB_ITEM_SHOW_REPLICANTS:
                db->show_replicants = !db->show_replicants;
                build_leaf(db);
                return 1;
            case TIKU_TRK_DB_ITEM_QUIT:
                pick->what = TIKU_TRK_DB_QUIT;
                break;
            case TIKU_TRK_DB_ITEM_PATH:
                pick->what = TIKU_TRK_DB_OPEN_PATH;
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
    if (!tiku_trk_deskbar_hit(db, x, y)) {
        return 0;
    }
    if (y < LEAF_H + 2) {
        build_leaf(db);         /* from disk, every time (DB-005) */
        db->menu_open = 1;
        return 1;
    }

    /* The application list: the expander toggles, the name opens the window
     * menu, a window entry raises (or minimises with Ctrl). */
    ry = list_y(db);
    for (i = 0; i < db->team_count; i++) {
        int team = db->order[i];
        tiku_trk_team_t *t = &db->teams[team];
        int team_h = (db->settings.icon_size + 4 > TEAM_H)
                         ? db->settings.icon_size + 4 : TEAM_H;

        /* The seam belongs to neither row; a click on it is a miss, which
         * is why hit-testing uses the row height and stepping uses the
         * pitch. */
        if (y >= ry && y < ry + team_h) {
            if (db->settings.show_expander &&
                x > db->frame.x + db->frame.w - EXPANDER_W - 6) {
                t->expanded = !t->expanded;
            } else if (secondary) {
                db->win_menu_team = team;
            }
            return 1;
        }
        ry += team_h + SEAM;
        if (!t->expanded) {
            continue;
        }
        for (j = 0; j < t->win_count; j++) {
            if (y >= ry && y < ry + WIN_H) {
                pick->tag = t->wins[j].tag;
                if (secondary) {
                    pick->what = TIKU_TRK_DB_CLOSE_WINDOW;
                } else if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
                           (mods & TIKU_DESK_MOD_OPTION) != 0u) {
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
            ry += WIN_H + SEAM;
        }
    }
    /* Secondary click on the tray opens clock options. */
    if (db->settings.show_clock && y >= db->frame.y + db->frame.h -
        CLOCK_H - PAD) {
        if (secondary) {
            db->clock_menu_open = 1;
        } else {
            time_t now = time(NULL);
            struct tm tmv;

            (void)localtime_r(&now, &tmv);
            db->calendar_year = 1900 + tmv.tm_year;
            db->calendar_month = 1 + tmv.tm_mon;
            db->calendar_day = tmv.tm_mday;
            db->calendar_open = 1;
        }
    } else if (secondary && y >= db->frame.y + db->frame.h -
               CLOCK_H - PAD - TRAY_ROW) {
        db->clock_menu_open = 1;
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

/*---------------------------------------------------------------------------*/
/* Drops                                                                     */
/*---------------------------------------------------------------------------*/

tiku_trk_db_drop_t
tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                         int *which)
{
    tiku_desk_rect_t f;
    int ry, i;

    if (which != NULL) {
        *which = -1;
    }
    if (db == NULL || !tiku_trk_deskbar_hit(db, x, y)) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    f = tiku_trk_deskbar_frame(db);
    ry = y - f.y;
    if (ry < LEAF_H) {
        return TIKU_TRK_DB_DROP_LEAF;
    }
    ry -= LEAF_H + PAD;
    for (i = 0; i < db->team_count; i++) {
        int team = db->order[i];
        int team_h = (db->settings.icon_size + 4 > TEAM_H)
                         ? db->settings.icon_size + 4 : TEAM_H;

        if (ry >= 0 && ry < team_h) {
            if (which != NULL) { *which = team; }
            return TIKU_TRK_DB_DROP_TEAM;
        }
        ry -= team_h + SEAM;
        if (db->teams[team].expanded) {
            /* A window row is not a drop target of its own: the drop
             * belongs to the application that owns it. */
            ry -= db->teams[team].win_count * (WIN_H + SEAM);
        }
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
tiku_trk_deskbar_launchable(const tiku_trk_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    /* A directory is silently ignored rather than refused: launching one
     * has no meaning, and complaining about something the user very likely
     * did by accident would be noise (DB-066). */
    return !tiku_trk_model_is_container(m);
}

tiku_trk_db_drop_t
tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                              char *path, size_t max)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rect_t m;
    int rowh, i;

    if (path != NULL && max > 0u) {
        path[0] = '\0';
    }
    if (db == NULL || !db->menu_open) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    rowh = f->height + 6;
    m = leaf_menu_rect(db, rowh, db->leaf_count);
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
    if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_DIR ||
        (db->leaf[i].kind == TIKU_TRK_DB_ITEM_PATH &&
         db->leaf[i].path[0] == '/' && db->leaf[i].label[0] == '/')) {
        if (path != NULL) {
            snprintf(path, max, "%s", db->leaf[i].path);
        }
        return TIKU_TRK_DB_DROP_MENU_DIR;
    }
    if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_PATH) {
        if (path != NULL) {
            snprintf(path, max, "%s", db->leaf[i].path);
        }
        return TIKU_TRK_DB_DROP_MENU_FILE;
    }
    return TIKU_TRK_DB_DROP_NONE;
}
