/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_leaf.c - the leaf menu, built from the namespace every
 * time it opens
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/** @brief The last component of @p path; the path itself when it ends in
 *         a slash or has none. */
const char *
db_leaf_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/** @brief Days in @p month of @p year, leap years counted. */
int
db_calendar_days(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31,
                                 30, 31 };
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

    if (month == 2) {
        return days[1] + leap;
    }
    return (month >= 1 && month <= 12) ? days[month - 1] : 31;
}

/** @brief Move the calendar by months and years, the day clamped to fit. */
void
db_calendar_shift(tiku_trk_deskbar_t *db, int months, int years)
{
    int month;

    if (db == NULL) {
        return;
    }
    month = db->calendar_month + months + years * 12;
    while (month < 1) { month += 12; db->calendar_year--; }
    while (month > 12) { month -= 12; db->calendar_year++; }
    db->calendar_month = month;
    if (db->calendar_day > db_calendar_days(db->calendar_year, month)) {
        db->calendar_day = db_calendar_days(db->calendar_year, month);
    }
}

/** @brief Append a leaf-menu row; a full menu drops it without a word. */
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

/** @brief One form under a board's gui/: a row that opens its panel. */
static int
db_form_entry(const tiku_model_t *m, void *ctx)
{
    tiku_trk_deskbar_t *db = ctx;
    char path[TIKU_PATH_MAX];

    if (m == NULL || m->name[0] == '.' || !tiku_model_is_container(m)) {
        return 0;
    }
    snprintf(path, sizeof path, "%.*s/panel", (int)sizeof path - 8, m->path);
    add_item(db, TIKU_TRK_DB_ITEM_PANEL, m->name, path, 0);
    return 0;
}

/**
 * @brief The forms a board at @p prefix publishes, as rows after its
 *        own: a board with none adds nothing.
 */
static void
add_forms(tiku_trk_deskbar_t *db, const char *prefix)
{
    char dir[TIKU_PATH_MAX];

    if (db->backend == NULL || db->backend->ops->list == NULL) {
        return;
    }
    snprintf(dir, sizeof dir, "%s/gui", prefix);
    (void)db->backend->ops->list(db->backend, dir, db_form_entry, db);
}

/**
 * @brief The listing callback that makes a leaf-menu row of an entry: into
 *        the walk's own table when it has one, else onto the bar's leaf.
 *        A dot file is not a row.
 */
int
db_menu_entry(const tiku_model_t *m, void *ctx)
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
void
db_build_leaf(tiku_trk_deskbar_t *db)
{
    static const char *const kRecentKey[TIKU_TRK_DB_RECENT_KINDS] = {
        "label.recent_documents", "label.recent_folders",
        "label.recent_applications"
    };
    const char *dir;
    int i;

    db->leaf_count = 0;
    db->sub_of = -1;
    db->sub_count = 0;
    db_reset_menu_hots(db);
    add_item(db, TIKU_TRK_DB_ITEM_ABOUT, tiku_str("menu.about"), NULL,
             0);
    add_item(db, TIKU_TRK_DB_ITEM_FIND, tiku_str("menu.find"), NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_HELP, tiku_str("menu.help"), NULL, 0);
    add_item(db, TIKU_TRK_DB_ITEM_FORCE_QUIT, tiku_str("menu.force_quit"),
             NULL, 0);
    /* One row for everything that configures the desktop, the way the
     * programs have one: the leaf says what a person can do here, and
     * the lists live inside it. */
    add_item(db, TIKU_TRK_DB_ITEM_SETTINGS, tiku_str("menu.settings"),
             NULL, 0);
    {
        const char *keys[] = {"menu.applications", "menu.programming",
                              "menu.device_tools", "menu.demos"};
        /* A group added without its name would read this table past its
         * end: the two are one list, and the compiler says so. */
        _Static_assert(sizeof keys / sizeof keys[0] ==
                       TIKU_TRK_DB_GROUP_COUNT, "a group has no name");
        int counts[TIKU_TRK_DB_GROUP_COUNT] = {0};
        for (i = 0; i < db->app_count; i++) {
            int group = db->app[i].group;
            if (group >= 0 && group < TIKU_TRK_DB_GROUP_COUNT) counts[group]++;
        }
        for (i = 0; i < TIKU_TRK_DB_GROUP_COUNT; i++) {
            if (counts[i]) add_item(db,
                i == TIKU_TRK_DB_GROUP_PROGRAMMING ?
                    TIKU_TRK_DB_ITEM_PROGRAMMING : TIKU_TRK_DB_ITEM_APPS,
                tiku_str(keys[i]), NULL, i);
        }
    }
    {
        /*
         * The stores that are ATTACHED, one row each, opening the place
         * they are mounted at.
         *
         * Not a fixed row each for /sys, /dev and /data, written into
         * every menu whether the machine has them or not: a device that
         * is not there does not get a row, so what is in the menu is
         * what a person can actually reach.  The rule the recents already
         * keep, applied to places instead of history.
         *
         * The namespace is asked directly rather than told, so a mount
         * that came or went is in the next menu opened; a list published
         * to the bar would be a second copy to keep agreeing.
         */
        int slots = tiku_mount_slots(db->backend), any = 0;

        for (i = 0; i < slots; i++) {
            const char *prefix = tiku_mount_prefix_at(db->backend, i);
            const char *leaf;

            if (prefix == NULL ||
                (prefix[0] == '/' && prefix[1] == '\0')) {
                continue;       /* the root is where the menu already is */
            }
            leaf = strrchr(prefix, '/');
            leaf = (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : prefix;
            if (!any) {
                add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, 0);
                any = 1;
            }
            add_item(db, TIKU_TRK_DB_ITEM_PATH, leaf, prefix, 0);
            add_forms(db, prefix);
        }
    }
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
    {
        /*
         * The Deskbar folder's own items, under a rule that is only put
         * in if something follows it.
         *
         * An empty folder gets no row saying it is empty: an entry whose
         * whole content is that there is no entry, and one that cannot be
         * chosen at that, is worse than nothing, which is what an empty
         * recents list gets (DB-074).
         *
         * The rule has to go in before the folder is walked, because the
         * walk is what adds the items; so it is taken out again if the
         * walk added none.  Rolling the count back is the whole of it --
         * what is past leaf_count is not in the menu.
         */
        int before = db->leaf_count;

        add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, 0);
        dir = menu_dir_of(db);
        if (dir[0] != '\0' && db->backend->ops->list != NULL) {
            struct menu_walk w;

            w.db = db;
            w.n = 0;
            w.into = NULL;
            /* Two rows held back for the rule and Shut down, which are
             * added after this walk and would otherwise be the first
             * rows a full folder pushed out of the menu. */
            w.max = TIKU_TRK_DB_LEAF_MAX - 2 - db->leaf_count;
            if (w.max < 0) {
                w.max = 0;
            }
            (void)db->backend->ops->list(db->backend, dir, db_menu_entry, &w);
        }
        if (db->leaf_count == before + 1) {
            db->leaf_count = before;
        }
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
