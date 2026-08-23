/*
 * TikuTracker -- the file manager for TikuOS.
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

#include "tiku_logo.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_iconpaint.h"
#include "tiku_strings.h"
#include "tiku_trk_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <locale.h>
#include <time.h>

/* Metrics read out of Deskbar's own source rather than estimated.  Rows do
 * not tile: each is drawn one pixel short and the seam between them is left
 * unpainted, so the pitch is the row plus SEAM. */
#define BAR_W        134     /* gMinimumWindowWidth at the 12 px face      */
#define LEAF_MIN      26     /* what a 12 px face needed  */
#define HLEAF_W 120     /* the leaf button, horizontal (DB-033) */
#define TEAM_MIN      20     /* kMinTeamItemHeight; pitch 21               */
#define WIN_MIN       19     /* pitch 20                                   */
#define SEAM           1     /* kGutter: the unpainted row between items   */
#define WIN_INDENT    22
#define TRAY_ROW      18     /* kGutter + fMaxReplicantHeight + kGutter    */
#define CLOCK_MIN     20
#define PAD            4
#define EXPANDER_W    13     /* ExpanderBounds() is 13 px, not 12          */
#define EXPANDER_ARM   5
#define EXPANDER_PEN   2
#define TRAY_PAD       3     /* fTrayPadding                               */
#define CLOCK_MARGIN  12     /* fClockMargin = fTrayPadding * 4            */
#define SWITCH_ROW    16     /* the theme switch's own strip of tray        */
#define SWITCH_W      26     /* the track */
#define SWITCH_H      12

/*
 * The strip's own rows, from the face rather than from memory.
 *
 * Every number above was measured against a 12 px face, back when that
 * was the only face there was.  The face is a preference now and the
 * Deskbar applies it to ITSELF -- so a row left at 26 pixels is one whose
 * word gets drawn at 16 px and centred into a strip too short to hold it.
 *
 * Each answers exactly the number it replaces at 12 px, and never less,
 * so the bar has not moved for anyone who left the face alone.
 */
static int
at_least(int want, int least)
{
    return (want > least) ? want : least;
}

static int leaf_row(void)
{
    return at_least(tiku_font_plain()->height + 11, LEAF_MIN);
}

static int clock_row(void)
{
    return at_least(tiku_font_plain()->height + 5, CLOCK_MIN);
}

static int team_row(void)
{
    return at_least(tiku_font_plain()->height + 5, TEAM_MIN);
}

static int win_row(void)
{
    return at_least(tiku_font_plain()->height + 4, WIN_MIN);
}

struct tiku_trk_deskbar {
    tiku_backend_t  *backend;
    tiku_store_t     *store;
    tiku_trk_db_dock_t   dock;
    tiku_trk_team_t      teams[TIKU_TRK_DB_TEAMS_MAX];
    int                  order[TIKU_TRK_DB_TEAMS_MAX];
    int                  dragging;
    int                  drop_team;
    int                  team_count;
    tiku_trk_replicant_t tray[TIKU_TRK_DB_TRAY_MAX];
    int                  tray_count;
    int                  next_replicant;   /* the ids handed out (DB-038) */
    int                  width;     /* the short axis, user-set        */
    /* Which of the three tables is in force: 0 light, 1 dark, 2 no hue.
     * The bar only DRAWS it; the shell owns the setting. */
    int                  theme;
    struct {
        char label[TIKU_TRK_DB_LABEL_MAX];
        char command[TIKU_PATH_MAX];
        char icon[24];
    }                    app[TIKU_TRK_DB_APPS_MAX];
    int                  app_count;
    int                  top_inset; /* the global bar owns this much   */
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
    time_t               fixed_now;     /* frozen clock for fixtures, or 0 */
    /* The hot row of each little menu, following the pointer; -1 when the
     * pointer is elsewhere.  The draw has always been able to highlight
     * one; this is the hand that moves it. */
    int                  leaf_hot;
    int                  sub_hot;
    tiku_trk_db_icon_fn  menu_icon;     /* injected by the shell, or NULL */
    void                *menu_icon_ctx;
    int                  win_hot;
    int                  clock_hot;
    int                  calendar_day;
    int                  win_menu_team;    /* -1 = none showing        */
    /* The expander a press is riding: the arrow turns down at once but
     * the state commits only on RELEASE over the same strip (DB-022). */
    int                  expander_press;   /* team index, or -1        */
    /* The row the pointer is resting on whose FULL name deserves saying:
     * a truncated label, or any application row while names are hidden
     * (DB-032). */
    char                 tip[TIKU_TRK_DB_LABEL_MAX];
    int                  tip_y;            /* the row's top, or -1     */
    /* The horizontal strip's layout, computed at draw time so the click
     * and the drawing read the same numbers (DB-033). */
    int                  hbutton_w;
    int                  hleft;            /* first button's left edge */
    int                  hoverflow;
    /* Mini mode's second icon: the running-apps popup (DB-037). */
    int                  apps_menu_open;
    int                  apps_hot;
    int                  screen_h;         /* the surface's, at last draw */
    /* The leaf menu, built from the namespace every time it opens rather
     * than compiled in: a file put in the Deskbar folder has to appear
     * without restarting anything (DB-005). */
    tiku_trk_db_item_t   leaf[TIKU_TRK_DB_LEAF_MAX];
    int                  leaf_count;
    int                  sub_of;           /* leaf row whose submenu is
                                            * open, or -1              */
    tiku_trk_db_item_t   sub[TIKU_TRK_DB_SUB_MAX];
    int                  sub_count;
    char                 menu_dir[TIKU_PATH_MAX];
    char                 menu_dir_fallback[TIKU_PATH_MAX];
    char                 recent[TIKU_TRK_DB_RECENT_KINDS]
                               [TIKU_TRK_DB_RECENT_MAX][TIKU_PATH_MAX];
    int                  recent_count[TIKU_TRK_DB_RECENT_KINDS];
    tiku_rect_t     frame;
};

static void
save_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_PATH_MAX + 20)];
    size_t at = 0;
    int i;

    if (db == NULL || db->store == NULL ||
        !tiku_store_writable(db->store)) {
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
    (void)tiku_state_write(db->store, TIKU_KEY_SESSION,
                               TIKU_ATTR_REPLICANTS, data, at);
    (void)tiku_store_flush(db->store);
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
menu_entry(const tiku_model_t *m, void *ctx)
{
    struct menu_walk *w = ctx;

    if (m == NULL || w->n >= w->max || m->name[0] == '.') {
        return 0;               /* a dot file is not a menu entry */
    }
    if (w->into != NULL) {
        tiku_trk_db_item_t *it = &w->into[w->n++];

        memset(it, 0, sizeof *it);
        it->kind = tiku_model_is_container(m) ? TIKU_TRK_DB_ITEM_DIR
                                                  : TIKU_TRK_DB_ITEM_PATH;
        snprintf(it->label, sizeof it->label, "%.*s",
                 (int)sizeof it->label - 1, m->name);
        snprintf(it->path, sizeof it->path, "%s", m->path);
    } else {
        add_item(w->db, tiku_model_is_container(m)
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
    tiku_model_t m;

    if (db->backend == NULL || db->backend->ops->stat == NULL) {
        return "";
    }
    if (db->menu_dir[0] != '\0' &&
        db->backend->ops->stat(db->backend, db->menu_dir, &m) == 0 &&
        tiku_model_is_container(&m)) {
        return db->menu_dir;
    }
    if (db->menu_dir_fallback[0] != '\0' &&
        db->backend->ops->stat(db->backend, db->menu_dir_fallback, &m) == 0 &&
        tiku_model_is_container(&m)) {
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
    static const char *const kRecentKey[TIKU_TRK_DB_RECENT_KINDS] = {
        "label.recent_documents", "label.recent_folders",
        "label.recent_applications"
    };
    const char *dir;
    int i;

    db->leaf_count = 0;
    db->sub_of = -1;
    add_item(db, TIKU_TRK_DB_ITEM_ABOUT, tiku_str("menu.about"), NULL,
             0);
    add_item(db, TIKU_TRK_DB_ITEM_FIND, tiku_str("menu.find"), NULL, 0);
    /* One row for everything that configures the desktop, the way the
     * programs have one: the leaf says what a person can do here, and
     * the lists live inside it. */
    add_item(db, TIKU_TRK_DB_ITEM_SETTINGS, tiku_str("menu.settings"),
             NULL, 0);
    if (db->app_count > 0) {
        /* One row, not one per program: the leaf is the system's own
         * menu, and a list of what happens to be installed belongs
         * inside it rather than across it. */
        add_item(db, TIKU_TRK_DB_ITEM_APPS, tiku_str("menu.applications"),
                 NULL, 0);
    }
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
                add_item(db, TIKU_TRK_DB_ITEM_RECENT, tiku_str(kRecentKey[i]),
                         NULL,
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
    add_item(db, TIKU_TRK_DB_ITEM_QUIT, tiku_str("menu.shut_down"), NULL,
             0);
}

void
tiku_trk_deskbar_set_menu_dir(tiku_trk_deskbar_t *db, const char *user,
                              const char *fallback)
{
    char u[TIKU_PATH_MAX], fb[TIKU_PATH_MAX];

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
                            const char paths[][TIKU_PATH_MAX], int n)
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
        snprintf(db->recent[which][i], TIKU_PATH_MAX, "%s", paths[i]);
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

/**
 * @brief The window menu's rows for @p team, with their enabled states.
 *
 * Hide all is refused when every window is already minimized and Show all
 * when none is; an application with no windows gets 'No windows' plus a
 * 'Quit application' -- suppressed for the file manager itself (DB-028).
 * @return how many rows.
 */
static int
win_menu_items(const tiku_trk_deskbar_t *db, int team, const char **items,
               int *enabled)
{
    const tiku_trk_team_t *t = &db->teams[team];
    int j, mini = 0;

    if (t->win_count == 0) {
        items[0] = "No windows";
        enabled[0] = 0;
        if (t->takes_anything) {
            return 1;
        }
        items[1] = "Quit application";
        enabled[1] = 1;
        return 2;
    }
    for (j = 0; j < t->win_count; j++) {
        if (t->wins[j].state == TIKU_TRK_DBW_MINIMIZED) {
            mini++;
        }
    }
    items[0] = tiku_str("menu.hide_all");
    enabled[0] = (mini < t->win_count);
    items[1] = tiku_str("menu.show_all");
    enabled[1] = (mini > 0);
    items[2] = "-";
    enabled[2] = 0;
    items[3] = tiku_str("menu.close_all");
    enabled[3] = 1;
    return 4;
}

static void fit_label(const tiku_font_t *f, const char *src,
                      int budget, char *out, size_t max);
static int list_y(const tiku_trk_deskbar_t *db);

/** @brief Where the window menu sits: beside the bar, whichever way. */
static tiku_rect_t
win_menu_rect(const tiku_trk_deskbar_t *db, int rows, int rowh)
{
    tiku_rect_t m;

    m.w = 128;
    m.h = rows * rowh + 4;
    if (tiku_trk_deskbar_vertical(db->dock)) {
        m.x = db->frame.x - 130;
        m.y = list_y(db) + team_row();
    } else {
        m.x = db->frame.x + HLEAF_W + 8;
        m.y = (db->dock == TIKU_TRK_DB_TOP)
                  ? db->frame.y + db->frame.h + 2
                  : db->frame.y - m.h - 2;
    }
    return m;
}

/** @brief What a plain-or-chorded click on an application row asks for. */
static void
team_action(tiku_trk_deskbar_t *db, int team, unsigned mods, int button,
            int clicks, tiku_trk_db_pick_t *pick)
{
    if (button == 3) {
        db->win_menu_team = team;
        db->win_hot = -1;
        return;
    }
    if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
        (mods & TIKU_MOD_OPTION) != 0u &&
        (mods & TIKU_TRK_MOD_SHIFT) != 0u) {
        pick->what = TIKU_TRK_DB_KILL_TEAM;
        pick->team = team;
    } else if (button == 2) {
        if ((mods & TIKU_TRK_MOD_SHIFT) == 0u) {
            pick->what = TIKU_TRK_DB_LAUNCH_TEAM;
            pick->team = team;
        }
    } else if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
               (mods & TIKU_TRK_MOD_SHIFT) != 0u) {
        pick->what = TIKU_TRK_DB_HIDE_ALL;
        pick->team = team;
    } else if ((mods & TIKU_TRK_MOD_CMD) != 0u) {
        pick->what = TIKU_TRK_DB_SHOW_ALL;
        pick->team = team;
    } else if (clicks > 1) {
        pick->what = TIKU_TRK_DB_ACTIVATE_TEAM;
        pick->team = team;
    }
}

/**
 * @brief Where the calendar opens: 4 px below the clock, or flipped
 *        above it when that would fall past the screen bottom (DB-070).
 */
static tiku_rect_t
calendar_rect(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t c = { db->frame.x - 190, 0, 186, 148 };

    c.y = db->frame.y + db->frame.h + 4;
    if (db->screen_h > 0 && c.y + c.h > db->screen_h) {
        c.y = db->frame.y + db->frame.h - clock_row() - PAD - 4 - c.h;
    }
    return c;
}

/** @brief No row is hot until the pointer says so (DB-004). */
static void
reset_menu_hots(tiku_trk_deskbar_t *db)
{
    db->leaf_hot = -1;
    db->sub_hot = -1;
    db->win_hot = -1;
    db->clock_hot = -1;
    db->apps_hot = -1;
}

tiku_trk_deskbar_t *
tiku_trk_deskbar_new(tiku_backend_t *backend)
{
    tiku_trk_deskbar_t *db = calloc(1, sizeof *db);

    if (db != NULL) {
        db->backend = backend;
        db->dock = TIKU_TRK_DB_RIGHT_EXPANDED;
        db->win_menu_team = -1;
        reset_menu_hots(db);
        db->expander_press = -1;
        db->tip_y = -1;
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

void
tiku_trk_deskbar_arrows(const tiku_trk_deskbar_t *db, int *up, int *down)
{
    if (up != NULL) { *up = 0; }
    if (down != NULL) { *down = 0; }
    if (db == NULL || !db->overflow) {
        return;
    }
    /* Overflowing at all means something is out of sight: above when the
     * list has been scrolled, below until it has been scrolled to its
     * end. */
    if (up != NULL) { *up = db->scroll > 0; }
    if (down != NULL) {
        *down = db->scroll < db->team_count - 1;
    }
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
        int at = strcmp(db->teams[a].name, "TikuTracker") == 0;
        int bt = strcmp(db->teams[b].name, "TikuTracker") == 0;

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
    if (!db->settings.show_expander) {
        /* No expander, nothing expanded: rows left open by the control
         * that has just been taken away would be rows nothing can close
         * (DB-023). */
        int i;

        for (i = 0; i < db->team_count; i++) {
            db->teams[i].expanded = 0;
        }
    }
    /* The whole interface reads its metrics from these faces, so the
     * size and the family land everywhere on the next paint. */
    (void)tiku_font_set_family(db->settings.font_family);
    (void)tiku_font_set_size(db->settings.font_size);
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
    /* One entry per application however many instances it has, matched
     * without regard to case as the source matches signatures (DB-015). */
    for (i = 0; i < db->team_count; i++) {
        if (strcasecmp(db->teams[i].name, name) == 0) {
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
    db->teams[i].takes_anything = (strcmp(name, "TikuTracker") == 0);
    db->teams[i].expanded = db->settings.expand_new_apps;
    /* Added without a pid: the shell's own seed, never pruned away. */
    db->teams[i].shell = 1;
    sort_teams(db);
    return i;
}

int
tiku_trk_deskbar_add_team_pid(tiku_trk_deskbar_t *db, const char *name,
                              long pid, const char *command, int *changed)
{
    int i, k, was = -1;

    if (db == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < db->team_count; i++) {
        if (strcasecmp(db->teams[i].name, name) == 0) {
            was = i;
            break;
        }
    }
    if (was < 0) {
        was = tiku_trk_deskbar_add_team(db, name);
        if (was < 0) {
            return -1;
        }
        db->teams[was].shell = 0;
        if (changed != NULL) { *changed = 1; }
    }
    for (k = 0; k < db->teams[was].pid_count; k++) {
        if (db->teams[was].pids[k] == pid) {
            return was;                    /* already counted */
        }
    }
    if (db->teams[was].pid_count <
        (int)(sizeof db->teams[was].pids / sizeof db->teams[was].pids[0])) {
        db->teams[was].pids[db->teams[was].pid_count++] = pid;
        if (changed != NULL) { *changed = 1; }
    }
    if (db->teams[was].command[0] == '\0' && command != NULL) {
        snprintf(db->teams[was].command, sizeof db->teams[was].command,
                 "%s", command);
    }
    return was;
}

void
tiku_trk_deskbar_remove_team(tiku_trk_deskbar_t *db, int team)
{
    int i;

    if (db == NULL || team < 0 || team >= db->team_count) {
        return;
    }
    for (i = team; i < db->team_count - 1; i++) {
        db->teams[i] = db->teams[i + 1];
    }
    db->team_count--;
    /* The display order indexed rows that have moved: rebuilt whole,
     * which is what sort_teams does anyway. */
    for (i = 0; i < db->team_count; i++) {
        db->order[i] = i;
    }
    db->win_menu_team = -1;
    sort_teams(db);
}

int
tiku_trk_deskbar_prune_teams(tiku_trk_deskbar_t *db, const long *live, int n)
{
    int i, changed = 0;

    if (db == NULL) {
        return 0;
    }
    for (i = db->team_count - 1; i >= 0; i--) {
        tiku_trk_team_t *t = &db->teams[i];
        int k, kept = 0;

        if (t->shell) {
            continue;
        }
        for (k = 0; k < t->pid_count; k++) {
            int j, alive = 0;

            for (j = 0; j < n; j++) {
                if (live != NULL && live[j] == t->pids[k]) {
                    alive = 1;
                    break;
                }
            }
            if (alive) {
                t->pids[kept++] = t->pids[k];
            } else {
                changed = 1;
            }
        }
        t->pid_count = kept;
        if (kept == 0) {
            /* The last instance went, and the entry goes with it
             * (DB-015). */
            tiku_trk_deskbar_remove_team(db, i);
            changed = 1;
        }
    }
    return changed;
}

int
tiku_trk_deskbar_apps_menu_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->apps_menu_open : 0;
}

int
tiku_trk_deskbar_hbutton_width(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->hbutton_w : 0;
}

int
tiku_trk_deskbar_team_pids(const tiku_trk_deskbar_t *db, int team, long *out,
                           int max)
{
    int k, n = 0;

    if (db == NULL || team < 0 || team >= db->team_count || out == NULL) {
        return 0;
    }
    for (k = 0; k < db->teams[team].pid_count && n < max; k++) {
        out[n++] = db->teams[team].pids[k];
    }
    return n;
}

const char *
tiku_trk_deskbar_team_command(const tiku_trk_deskbar_t *db, int team)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return "";
    }
    return db->teams[team].command;
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

    if (db == NULL || label == NULL || path == NULL ||
        db->tray_count >= TIKU_TRK_DB_TRAY_MAX) {
        return -1;
    }
    for (int i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return db->tray[i].id;         /* already subscribed */
        }
        if (strncmp(db->tray[i].label, label,
                    sizeof db->tray[i].label - 1u) == 0) {
            /* A DIFFERENT node wearing a taken name is refused, as the
             * original refuses duplicate view names (DB-038). */
            return -1;
        }
    }
    r = &db->tray[db->tray_count++];
    r->id = ++db->next_replicant;
    snprintf(r->label, sizeof r->label, "%s", label);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->valid = 0;
    r->height = TIKU_TRK_DB_MAX_REPLICANT_H;
    /* Ask the node what its reading MEANS, once, so the tray can show
     * "16h 26m" rather than 59182. */
    if (db->backend != NULL && db->backend->ops->stat != NULL) {
        tiku_model_t m;

        if (db->backend->ops->stat(db->backend, path, &m) == 0) {
            r->desc_known = tiku_desc_parse(m.facts.meta, &r->desc);
            r->node_id = m.node_id;
        }
    }
    save_replicants(db);
    return r->id;
}

int
tiku_trk_deskbar_sync_replicants(tiku_trk_deskbar_t *db)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL || db->backend->ops->stat == NULL) {
        return 0;
    }
    for (i = db->tray_count - 1; i >= 0; i--) {
        tiku_trk_replicant_t *r = &db->tray[i];
        tiku_model_t m;
        char moved[TIKU_PATH_MAX];

        if (db->backend->ops->stat(db->backend, r->path, &m) == 0) {
            continue;                   /* still where it was */
        }
        if (r->node_id != 0 && db->backend->ops->path_of_id != NULL &&
            db->backend->ops->path_of_id(db->backend, r->node_id, moved,
                                         sizeof moved) == 0) {
            /* Moved, not gone: re-pointed, keeping its place and its
             * label (DB-040). */
            snprintf(r->path, sizeof r->path, "%s", moved);
            changed = 1;
            continue;
        }
        (void)tiku_trk_deskbar_drop_replicant(db, r->id);
        changed = 1;
    }
    if (changed) {
        save_replicants(db);
    }
    return changed;
}

int
tiku_trk_deskbar_replicant_by_id(const tiku_trk_deskbar_t *db, int id)
{
    int i;

    if (db == NULL) {
        return -1;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (db->tray[i].id == id) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_replicant_by_name(const tiku_trk_deskbar_t *db,
                                   const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].label, name) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_drop_replicant(tiku_trk_deskbar_t *db, int id)
{
    int at = tiku_trk_deskbar_replicant_by_id(db, id);
    int i;

    if (at < 0) {
        return 0;
    }
    for (i = at; i < db->tray_count - 1; i++) {
        db->tray[i] = db->tray[i + 1];
    }
    db->tray_count--;
    save_replicants(db);
    return 1;
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
tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db, tiku_store_t *store)
{
    if (db != NULL) {
        db->store = store;
    }
}

int
tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_PATH_MAX + 20)];
    int n, loaded = 0;
    char *line;

    if (db == NULL || db->store == NULL) {
        return 0;
    }
    n = tiku_state_read(db->store, TIKU_KEY_SESSION,
                            TIKU_ATTR_REPLICANTS, data, sizeof data - 1u);
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
                                                          &(tiku_model_t){0}) == 0 &&
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
    int team_h = (db->settings.icon_size + 4 > team_row())
                     ? db->settings.icon_size + 4 : team_row();

    for (i = 0; i < db->team_count; i++) {
        h += team_h + SEAM;
        if (db->teams[i].expanded) {
            h += db->teams[i].win_count * (win_row() + SEAM);
        }
    }
    return h;
}

static int
list_y(const tiku_trk_deskbar_t *db)
{
    /* Below the leaf, WHEREVER the frame starts: with the global bar on
     * the whole bar sits lower, and a list still measured from the top
     * of the screen would draw across the leaf itself. */
    return db->frame.y + leaf_row() + PAD;
}

static int
tray_y(const tiku_trk_deskbar_t *db)
{
    return list_y(db) + list_rows(db) + PAD;
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
              (db->settings.show_clock ? clock_row() : 0) - SWITCH_ROW +
              (SWITCH_ROW - SWITCH_H) / 2;
    } else {
        /* Along an edge: the strip's far end, past the last button. */
        r.x = db->frame.x + db->frame.w - SWITCH_W - 8;
        r.y = db->frame.y + (db->frame.h - SWITCH_H) / 2;
    }
    return r;
}

/**
 * The theme switch, drawn like the physical part: a recessed track the
 * knob slides in, lit with the accent colour while the dark side is on --
 * a pilot lamp, not a torch.  The knob is a raised cap with grip lines.
 *
 * THREE detents, not two: light at the left, dark at the right, and no
 * hue in the middle.  The middle is where it is because the switch is
 * read as a slider and the third mode is not a further step INTO the
 * dark -- it is a different axis, and the centre is the only position
 * that does not claim otherwise.
 */
static void
draw_switch(const tiku_trk_deskbar_t *db, tiku_surface_t *s)
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
        build_leaf(db);         /* from disk, every time (DB-005) */
        db->menu_open = 1;
        reset_menu_hots(db);
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

static tiku_rect_t
leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows)
{
    tiku_rect_t m;

    m.w = 176;
    m.h = rows * rowh + 4;
    m.y = db->frame.y + leaf_row() + 2;
    m.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           db->dock == TIKU_TRK_DB_LEFT_MINI)
          ? db->frame.x + db->frame.w + 2
          : db->frame.x - m.w - 2;
    if (m.x < 0) {
        m.x = 0;
    }
    return m;
}

static tiku_rect_t sub_menu_rect(const tiku_trk_deskbar_t *db,
                                      tiku_rect_t m, int rowh);

void
tiku_trk_deskbar_set_menu_icon_painter(tiku_trk_deskbar_t *db,
                                       tiku_trk_db_icon_fn fn, void *context)
{
    if (db != NULL) {
        db->menu_icon = fn;
        db->menu_icon_ctx = context;
    }
}

/** @brief Hand one row's item to the shell's painter. */
struct menu_icon_relay {
    tiku_trk_deskbar_t       *db;
    const tiku_trk_db_item_t *items;
};

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
        ry = list_y(db);
        for (i = 0; i < db->team_count; i++) {
            int at = db->order[i];
            tiku_trk_team_t *t = &db->teams[at];
            int team_h = (db->settings.icon_size + 4 > team_row())
                             ? db->settings.icon_size + 4 : team_row();

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
                    ry += win_row() + SEAM;
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
        tiku_rect_t m = leaf_menu_rect(db, rowh, db->leaf_count);

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + db->leaf_count * rowh) {
            leaf = (y - m.y - 2) / rowh;
            if (db->leaf[leaf].kind == TIKU_TRK_DB_ITEM_SEP ||
                db->leaf[leaf].kind == TIKU_TRK_DB_ITEM_DISABLED) {
                leaf = -1;      /* nothing to choose there */
            }
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_rect_t sm = sub_menu_rect(db, m, rowh);

            if (x >= sm.x && x < sm.x + sm.w && y >= sm.y + 2 &&
                y < sm.y + 2 + db->sub_count * rowh) {
                sub = (y - sm.y - 2) / rowh;
            }
        }
    }
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        const char *items[4];
        int enabled[4];
        int n = win_menu_items(db, db->win_menu_team, items, enabled);
        tiku_rect_t m = win_menu_rect(db, n, rowh);

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
        am.y = db->frame.y + 2 + leaf_row() + 2;
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
                               db->frame.h - clock_row() - 32, 170, 4 * 28 };

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
            y >= db->frame.y + db->frame.h - clock_row() - PAD &&
            y < db->frame.y + db->frame.h) {
            /* The clock's tooltip is the whole long-form date (DB-044),
             * from the same clock the display reads -- frozen and all. */
            time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
            struct tm tmv;

            (void)localtime_r(&now, &tmv);
            (void)strftime(tip, sizeof tip, "%A, %B %-e, %Y", &tmv);
            tip_y = db->frame.y + db->frame.h - clock_row() - PAD;
        } else if (!db->menu_open && db->win_menu_team < 0 &&
            !db->clock_menu_open && x >= db->frame.x &&
            x < db->frame.x + db->frame.w) {
            int i, ry = list_y(db);
            int team_h = (db->settings.icon_size + 4 > team_row())
                             ? db->settings.icon_size + 4 : team_row();

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
                    if (y >= ry && y < ry + win_row()) {
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
                    ry += win_row() + SEAM;
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
    m = leaf_menu_rect(db, rowh, db->leaf_count);
    /* Tracker's 20px tracking margin is intentionally outside the visible
     * menu: it lets the pointer cross the gap into a submenu without
     * dismissing the menu first. */
    if (x >= m.x - 20 && x < m.x + m.w + 20 &&
        y >= m.y - 20 && y < m.y + m.h + 20) {
        return 1;
    }
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_rect_t sm = sub_menu_rect(db, m, rowh);

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
    m = leaf_menu_rect(db, rowh, db->leaf_count);
    /* The open submenu first: its rows sit over the leaf's, and a name
     * that appears in both is the one the user can see. */
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_rect_t sm = sub_menu_rect(db, m, rowh);

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
static tiku_rect_t
sub_menu_rect(const tiku_trk_deskbar_t *db, tiku_rect_t m, int rowh)
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
    } else if (it->kind == TIKU_TRK_DB_ITEM_APPS) {
        int i;

        for (i = 0; i < db->app_count && i < TIKU_TRK_DB_SUB_MAX; i++) {
            tiku_trk_db_item_t *si = &db->sub[db->sub_count++];

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
        db->sub_hot = -1;
    }
}

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
    team_h = (db->settings.icon_size + 4 > team_row())
                 ? db->settings.icon_size + 4 : team_row();
    db->screen_h = s->h;
    rows = tiku_trk_deskbar_mini(db->dock) ? 0 : list_rows(db);
    {
        /* How much room the list actually has, once the leaf, the tray and
         * the clock have taken theirs.  A list longer than that is what
         * makes the bar scrollable (DB-034). */
        int room = s->h - db->top_inset - (leaf_row() + PAD) -
                   (db->tray_count * TRAY_ROW) - SWITCH_ROW -
                   (db->settings.show_clock ? clock_row() : 0) - 2 * PAD;

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
    /* The bar starts below whatever owns the top of the screen: with the
     * global menu bar on, a Deskbar at y 0 would put its own leaf under
     * the strip. */
    db->frame.y = db->top_inset;
    if (tiku_trk_deskbar_vertical(db->dock)) {
        db->frame.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                       db->dock == TIKU_TRK_DB_LEFT_MINI)
                          ? 0 : s->w - db->frame.w;
        db->frame.h = leaf_row() + PAD + rows + PAD +
                      (tiku_trk_deskbar_mini(db->dock) ? team_row() + 2 : 0) +
                      db->tray_count * TRAY_ROW + SWITCH_ROW +
                      (db->settings.show_clock ? clock_row() : 0) + PAD;
    } else {
        /* Along an edge: the bar spans the screen and is as deep as one
         * row needs.  The application STRIP lives here -- buttons that
         * shrink as more launch and grow back as they quit (DB-033). */
        db->frame.x = 0;
        db->frame.w = s->w;
        db->frame.h = leaf_row() + PAD;
        db->frame.y = (db->dock == TIKU_TRK_DB_TOP)
                          ? db->top_inset : s->h - db->frame.h;
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
        tiku_fill(s, db->frame, TIKU_C_PANEL);
        return;
    }

    tiku_fill(s, db->frame, TIKU_C_PANEL);
    tiku_bevel(s, db->frame,
                    tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_MAX),
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));

    /* The leaf: the one control that is not a list item, so it is the one
     * thing wearing the tab colour. */
    leaf = (tiku_rect_t){ db->frame.x + 2, db->frame.y + 2,
                               tiku_trk_deskbar_vertical(db->dock)
                                   ? db->frame.w - 4 : HLEAF_W,
                               leaf_row() };
    tiku_fill(s, leaf, TIKU_C_TAB);
    tiku_frame(s, leaf, dark);
    tiku_bevel(s, tiku_inset(leaf, 1),
                    tiku_tint(TIKU_C_TAB, TIKU_LIGHTEN_1),
                    tiku_tint(TIKU_C_TAB, TIKU_DARKEN_2));
    {
        /*
         * The mark, then the name beside it.  Drawn without its own
         * ground: the leaf already has a surface of its own and a pale
         * square on the amber would read as a sticker stuck to it.
         */
        int mark = leaf_row() - 8;
        tiku_rect_t label = leaf;
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

        tiku_logo_paint_with(s, (tiku_rect_t){ leaf.x + 4,
                                               leaf.y + (leaf.h - mark) / 2,
                                               mark, mark }, 0u, NULL,
                             shade);
        label.x += mark + 8;
        label.w -= mark + 8;
        (void)tiku_text_centered(s, fb, label, "TikuOS", TIKU_C_TEXT);
    }

    if (tiku_trk_deskbar_mini(db->dock)) {
        /* The collapsed dock's second icon: the application list folded
         * into one button that pops the running-apps menu (DB-037). */
        tiku_rect_t ab = { db->frame.x + 2,
                                db->frame.y + 2 + leaf_row() + 2,
                                db->frame.w - 4, team_row() };

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
            tiku_rect_t b = { bx, db->frame.y + 2, bw - 2, leaf_row() };
            char label[TIKU_TRK_DB_LABEL_MAX];

            if (bx + bw > right) {
                break;
            }
            draw_row_bg(s, b, t->active);
            (void)tiku_icon_paint(s,
                strcmp(t->name, "TikuTracker") == 0 ? "folder" : "application",
                b.x + 3, b.y + (leaf_row() - 16) / 2, 16);
            fit_label(f, t->name, b.w - 16 - 10, label, sizeof label);
            tiku_text(s, f, b.x + 16 + 6,
                           b.y + (leaf_row() - f->height) / 2 + f->ascent,
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
        y = list_y(db);
        for (i = 0; i < db->team_count; i++) {
            int team = db->order[i];
            tiku_trk_team_t *t = &db->teams[team];
            tiku_rect_t row = { db->frame.x + 2, y, db->frame.w - 4,
                                     team_h };
            int icon_size = db->settings.icon_size;
            tiku_rect_t icon = { row.x + 4,
                                      y + (team_h - icon_size) / 2,
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
            /* The application's icon.  Tracker's own team is the one we can
             * name; anything else gets the generic application icon rather
             * than a blank, so the column never has a hole in it. */
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
                               y + (team_h - f->height) / 2 + f->ascent,
                               label, TIKU_C_TEXT);
            }
            if (db->settings.show_expander) {
                draw_expander(s, row.x + row.w - EXPANDER_W - 2,
                              y + (team_h - 9) / 2,
                              t->expanded ||
                              db->expander_press == db->order[i]);
            }
            y += team_h + SEAM;

            if (!t->expanded) {
                continue;
            }
            for (j = 0; j < t->win_count; j++) {
                tiku_trk_dbwin_t *w = &t->wins[j];
                tiku_rect_t wr = { db->frame.x + 2, y, db->frame.w - 4,
                                        win_row() };
                char wl[TIKU_TRK_DB_LABEL_MAX];

                draw_row_bg(s, wr, w->current);
                draw_win_icon(s, wr.x + WIN_INDENT,
                              y + (win_row() - 9) / 2,
                              w->state == TIKU_TRK_DBW_MINIMIZED,
                              0);
                fit_label(f, w->title, wr.w - WIN_INDENT - 20, wl,
                          sizeof wl);
                tiku_text(s, f, wr.x + WIN_INDENT + 14,
                               y + (win_row() - f->height) / 2 + f->ascent, wl,
                               TIKU_C_TEXT);
                y += win_row() + SEAM;
            }
        }
    }

    /* The tray, then the clock beneath it: the original reserves the
     * clock's room in the first row, and stacking it here is the vertical
     * equivalent. */
    y = tiku_trk_deskbar_mini(db->dock) ? db->frame.y + leaf_row() + PAD
                                        : tray_y(db);
    tiku_hline(s, db->frame.x + 4, y - 3, db->frame.w - 8,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_hline(s, db->frame.x + 4, y - 2, db->frame.w - 8,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX));
    for (i = 0; i < db->tray_count; i++) {
        const tiku_trk_replicant_t *r = &db->tray[i];
        int ty = y + (TRAY_ROW - f->height) / 2 + f->ascent;
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
        y += TRAY_ROW;
    }
    draw_switch(db, s);
    if (tiku_trk_deskbar_vertical(db->dock)) {
        y += SWITCH_ROW;
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
                       y + (clock_row() - f->height) / 2 + f->ascent, clock,
                       TIKU_C_TEXT);
    }

    /* The leaf menu: the head the original always shows, then the recent
     * lists, then whatever is in the Deskbar folder right now. */
    if (db->menu_open) {
        const char *items[TIKU_TRK_DB_LEAF_MAX];
        int rowh = f->height + 6;
        tiku_rect_t m = leaf_menu_rect(db, rowh, db->leaf_count);

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
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_SETTINGS ||
                       db->leaf[i].kind == TIKU_TRK_DB_ITEM_QUIT) {
                /* What expands says so, the way every hierarchical menu
                 * marks the rows that go somewhere (DB-002). */
                tiku_text(s, f, m.x + m.w - 12,
                               m.y + 2 + i * rowh +
                                   (rowh - f->height) / 2 + f->ascent,
                               ">", TIKU_C_TEXT);
            }
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            const char *sitems[TIKU_TRK_DB_SUB_MAX];
            tiku_rect_t sm = sub_menu_rect(db, m, rowh);

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
    /* The window menu for one application: Hide all / Show all / Close all,
     * which the original appends to every application's window list. */
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        const char *items[4];
        int enabled[4];
        int n = win_menu_items(db, db->win_menu_team, items, enabled);
        int rowh = f->height + 6;
        tiku_rect_t m = win_menu_rect(db, n, rowh);
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
                               clock_row() - 32, 170, 4 * 28 };
        tiku_ui_menu(s, m, items, 4, db->clock_hot);
    }
    {
        /* The scroll arrows: shown the moment something is out of sight
         * in the direction they point (DB-034). */
        int up = 0, down = 0, k;
        int ly = list_y(db);
        int lb = db->frame.y + db->frame.h -
                 (db->tray_count * TRAY_ROW) -
                 (db->settings.show_clock ? clock_row() : 0) - PAD;
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
    if (db->tip_y >= 0 && db->tip[0] != '\0') {
        /* The whole name, said beside the row that could not fit it
         * (DB-032). */
        int tw = tiku_text_width(f, db->tip) + 12;
        tiku_rect_t b = { db->frame.x - tw - 4, db->tip_y, tw,
                               f->height + 6 };

        if (b.x < 0) { b.x = 0; }
        tiku_fill(s, b, TIKU_C_NOTE);
        tiku_frame(s, b, TIKU_C_TEXT);
        tiku_text(s, f, b.x + 6, b.y + 3 + f->ascent, db->tip,
                       TIKU_C_TEXT);
    }
    if (db->calendar_open) {
        /* The clock's calendar: a month grid with steppers, popped beside
         * the bar and dismissed by the next click anywhere. */
        struct tm tmv;
        tiku_rect_t c = calendar_rect(db);
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
        days = calendar_days(db->calendar_year, db->calendar_month);
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

/*---------------------------------------------------------------------------*/
/* interaction                                                               */
/*---------------------------------------------------------------------------*/

int
tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick)
{
    int secondary = (button == 3);
    const tiku_font_t *f = tiku_font_plain();
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
            y >= db->frame.y + db->frame.h - clock_row() - PAD) {
            return 1;
        }
        {
            tiku_rect_t c = calendar_rect(db);
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
        tiku_rect_t m = { db->frame.x - 130, db->frame.y + db->frame.h -
                               clock_row() - 32, 170, 4 * 28 };
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
        const char *items[4];
        int enabled[4];
        int n = win_menu_items(db, db->win_menu_team, items, enabled);
        tiku_rect_t m = win_menu_rect(db, n, rowh);
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
    if (db->menu_open) {
        int rowh = f->height + 6;
        tiku_rect_t m = leaf_menu_rect(db, rowh, db->leaf_count);
        int idx = (y - m.y - 2) / rowh;

        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_rect_t sm = sub_menu_rect(db, m, rowh);
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
                           TIKU_TRK_DB_ITEM_SHOW_REPLICANTS) {
                    /* A toggle is not a departure: the menu stays up
                     * with the row saying the other thing now. */
                    int of = db->sub_of;

                    db->show_replicants = !db->show_replicants;
                    open_sub(db, of);
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
            case TIKU_TRK_DB_ITEM_SETTINGS:
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
            case TIKU_TRK_DB_ITEM_TRACKER_PREFS:
                pick->what = TIKU_TRK_DB_TRACKER_PREFERENCES;
                break;
            case TIKU_TRK_DB_ITEM_SHOW_REPLICANTS:
                db->show_replicants = !db->show_replicants;
                build_leaf(db);
                return 1;
            case TIKU_TRK_DB_ITEM_QUIT:
                if (x >= m.x + m.w - 20) {
                    /* The arrow: the immediate pair beside the row,
                     * expanding rather than choosing (DB-006). */
                    open_sub(db, idx);
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
            default:
                break;
            }
        }
        db->menu_open = 0;
        db->sub_of = -1;
        return 1;
    }
    if (db->apps_menu_open) {
        const tiku_font_t *fm = tiku_font_plain();
        int rowh = fm->height + 6;
        int n = (db->team_count > 0) ? db->team_count : 1;
        tiku_rect_t ab = { db->frame.x + 2,
                                db->frame.y + 2 + leaf_row() + 2,
                                db->frame.w - 4, team_row() };
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
    if (!tiku_trk_deskbar_hit(db, x, y)) {
        return 0;
    }
    if (!tiku_trk_deskbar_vertical(db->dock)) {
        /* The horizontal strip: the leaf keeps its own width, and the
         * rest of the bar is application buttons (DB-033). */
        if (x < db->frame.x + 2 + HLEAF_W) {
            build_leaf(db);
            db->menu_open = 1;
            reset_menu_hots(db);
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
                team_action(db, db->order[idx], mods, button, clicks,
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
    if (y < db->frame.y + leaf_row() + 2) {
        build_leaf(db);         /* from disk, every time (DB-005) */
        db->menu_open = 1;
        reset_menu_hots(db);
        return 1;
    }
    if (tiku_trk_deskbar_mini(db->dock) &&
        y < db->frame.y + leaf_row() + 4 + team_row()) {
        /* The second icon: the running-apps menu (DB-037). */
        db->apps_menu_open = 1;
        reset_menu_hots(db);
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
        int ly = list_y(db);
        int lb = db->frame.y + db->frame.h -
                 (db->tray_count * TRAY_ROW) -
                 (db->settings.show_clock ? clock_row() : 0) - PAD;

        if (y >= ly && y < ly + 8) {
            (void)tiku_trk_deskbar_scroll(db, -astep);
            return 1;
        }
        if (y >= lb - 8 && y < lb) {
            (void)tiku_trk_deskbar_scroll(db, astep);
            return 1;
        }
    }

    /* The application list: the expander toggles, the name opens the window
     * menu, a window entry raises (or minimises with Ctrl). */
    ry = list_y(db);
    for (i = 0; i < db->team_count; i++) {
        int team = db->order[i];
        tiku_trk_team_t *t = &db->teams[team];
        int team_h = (db->settings.icon_size + 4 > team_row())
                         ? db->settings.icon_size + 4 : team_row();

        /* The seam belongs to neither row; a click on it is a miss, which
         * is why hit-testing uses the row height and stepping uses the
         * pitch. */
        if (y >= ry && y < ry + team_h) {
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
                team_action(db, team, mods, button, clicks, pick);
            }
            return 1;
        }
        ry += team_h + SEAM;
        if (!t->expanded) {
            continue;
        }
        for (j = 0; j < t->win_count; j++) {
            if (y >= ry && y < ry + win_row()) {
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
            ry += win_row() + SEAM;
        }
    }
    /* Secondary click on the tray opens clock options. */
    if (db->settings.show_clock && y >= db->frame.y + db->frame.h -
        clock_row() - PAD) {
        if (secondary) {
            db->clock_menu_open = 1;
        reset_menu_hots(db);
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
               clock_row() - PAD - TRAY_ROW) {
        db->clock_menu_open = 1;
        reset_menu_hots(db);
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
    if (ry < leaf_row()) {
        return TIKU_TRK_DB_DROP_LEAF;
    }
    ry -= leaf_row() + PAD;
    for (i = 0; i < db->team_count; i++) {
        int team = db->order[i];
        int team_h = (db->settings.icon_size + 4 > team_row())
                         ? db->settings.icon_size + 4 : team_row();

        if (ry >= 0 && ry < team_h) {
            if (which != NULL) { *which = team; }
            return TIKU_TRK_DB_DROP_TEAM;
        }
        ry -= team_h + SEAM;
        if (db->teams[team].expanded) {
            /* A window row is not a drop target of its own: the drop
             * belongs to the application that owns it. */
            ry -= db->teams[team].win_count * (win_row() + SEAM);
        }
    }
    if (tiku_trk_deskbar_vertical(db->dock) &&
        y >= f.y + f.h - (db->tray_count * TRAY_ROW) -
            (db->settings.show_clock ? clock_row() : 0) - PAD) {
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
