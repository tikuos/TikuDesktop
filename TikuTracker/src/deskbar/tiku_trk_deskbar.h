/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar.h - the Deskbar.
 *
 * Three sections stacked as the original has them: the leaf menu, the
 * running-application list whose windows nest underneath it in expando mode,
 * and the tray of replicants with the clock.  Behaviour is cited to the
 * DB-* rows in FEATURES.md, which were read from Deskbar's own source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_H_
#define TIKU_TRK_DESKBAR_H_

#include <time.h>
#include "tiku_trk_gfx_dep.h"
#include "tiku_attr.h"
#include "tiku_model.h"
#include "tiku_state.h"

#define TIKU_TRK_DB_TEAMS_MAX   8
#define TIKU_TRK_DB_WINS_MAX   16
#define TIKU_TRK_DB_TRAY_MAX    8
#define TIKU_TRK_DB_LABEL_MAX  48
#define TIKU_TRK_DB_MAX_REPLICANT_H 12

/** @brief Where the bar is docked.  Expanded modes reserve an edge strip. */
typedef enum {
    TIKU_TRK_DB_RIGHT_EXPANDED = 0,   /* the BeOS default              */
    TIKU_TRK_DB_LEFT_EXPANDED,
    TIKU_TRK_DB_RIGHT_MINI,           /* collapsed to the leaf + tray  */
    TIKU_TRK_DB_LEFT_MINI,
    /* Along an edge rather than down one.  A horizontal bar is a
     * different shape, not a rotated one: the list runs across it and the
     * application buttons share the width between them (DB-033). */
    TIKU_TRK_DB_TOP,
    TIKU_TRK_DB_BOTTOM,
    TIKU_TRK_DB_DOCKS               /* how many there are              */
} tiku_trk_db_dock_t;

/** @brief A window's state, which its icon distinguishes (DB row). */
typedef enum {
    TIKU_TRK_DBW_SHOWN = 0,
    TIKU_TRK_DBW_MINIMIZED,
} tiku_trk_dbwin_state_t;

/** @brief One window under an application. */
typedef struct {
    /* app name stripped */
    char                   title[TIKU_TRK_DB_LABEL_MAX];
    tiku_trk_dbwin_state_t state;
    int                    current;      /* the application's front window */
    void                  *tag;          /* the owner's window identity    */
} tiku_trk_dbwin_t;

/** @brief One running application. */
typedef struct {
    char             name[TIKU_TRK_DB_LABEL_MAX];
    int              expanded;           /* windows shown underneath       */
    int              active;             /* owns the focused window        */
    tiku_trk_dbwin_t wins[TIKU_TRK_DB_WINS_MAX];
    int              win_count;
    /* The types this application can open.  A drop on one that cannot
     * read the file would appear to work and do nothing (DB-035). */
    char             type[8][TIKU_TYPE_MAX];
    int              ntype;
    int              takes_anything;
    /* The processes behind the entry: one row however many there are,
     * gone only when the last one is (DB-015). */
    long             pids[16];
    int              pid_count;
    char             command[160];       /* argv[0], for a new instance */
    int              shell;              /* seeded by the shell, kept   */
} tiku_trk_team_t;

/** @brief A tray item: a namespace node rendered as label + value. */
typedef struct {
    int             id;             /* addressable afterwards (DB-038)    */
    uint64_t        node_id;        /* the node's identity, for moves     */
    char            label[16];
    char            path[TIKU_PATH_MAX];
    char            value[24];      /* raw, as the node reports it        */
    tiku_desc_t desc;           /* resolved once, at subscribe time   */
    int             desc_known;
    int             valid;
    int             height;
} tiku_trk_replicant_t;

/** @brief How many rows the leaf menu holds, and its submenu. */
#define TIKU_TRK_DB_LEAF_MAX 32
#define TIKU_TRK_DB_SUB_MAX  50

/** @brief What one leaf-menu row IS, which decides what a click does. */
typedef enum {
    TIKU_TRK_DB_ITEM_ROW,       /* chosen: it acts by its command       */
    TIKU_TRK_DB_ITEM_SEP,
    TIKU_TRK_DB_ITEM_DISABLED,  /* said, not chosen                     */
    TIKU_TRK_DB_ITEM_EXPAND     /* opens the submenu its expand names   */
} tiku_trk_db_item_kind_t;

/** @brief What an expanding row opens: the builder of its submenu. */
typedef enum {
    TIKU_TRK_DB_EXPAND_NONE,
    TIKU_TRK_DB_EXPAND_DIR,      /* a folder's entries, from its path    */
    TIKU_TRK_DB_EXPAND_RECENT,   /* one of the recent lists, by which    */
    TIKU_TRK_DB_EXPAND_APPS,     /* the installed programs of a group    */
    TIKU_TRK_DB_EXPAND_SETTINGS  /* what configures this                 */
} tiku_trk_db_expand_t;


/** @brief Which recent list a RECENT row shows. */
typedef enum {
    TIKU_TRK_DB_RECENT_DOCS = 0,
    TIKU_TRK_DB_RECENT_FOLDERS,
    TIKU_TRK_DB_RECENT_APPS,
    TIKU_TRK_DB_RECENT_KINDS
} tiku_trk_db_recent_t;

/** @brief How many entries one recent list shows. */
#define TIKU_TRK_DB_RECENT_MAX 50

typedef struct {
    char                    label[TIKU_TRK_DB_LABEL_MAX];
    char                    path[TIKU_PATH_MAX];
    tiku_trk_db_item_kind_t kind;
    int                     command; /* ROW: the tiku_trk_db_action_t   */
    tiku_trk_db_expand_t    expand;  /* EXPAND: what it opens           */
    int                     which;   /* RECENT: which list; APPS: group */
    /* The art this row wears, named by whoever offered it.  The bar
     * carries the name and paints nothing itself. */
    char                    icon[24];
} tiku_trk_db_item_t;

typedef struct tiku_trk_deskbar tiku_trk_deskbar_t;
typedef struct tiku_trk_deskbar_settings tiku_trk_deskbar_settings_t;

/**
 * @brief Where the folder whose contents the leaf menu shows lives (DB-003).
 *
 * The user's own, with a fallback: a system copy is what a machine with no
 * per-user menu folder shows, rather than an empty menu.
 */
void tiku_trk_deskbar_set_menu_dir(tiku_trk_deskbar_t *db, const char *user,
                                   const char *fallback);

/**
 * @brief Real art for menu rows, injected by the shell (DB-002).
 *
 * The Deskbar knows what each row IS; the shell knows what that looks
 * like.  @return nonzero when something was painted.
 */
typedef int (*tiku_trk_db_icon_fn)(void *context, tiku_surface_t *s,
                                   const tiku_trk_db_item_t *item,
                                   int x, int y, int size);
/**
 * @brief Give the bar the painter that draws a menu row's icon, with the
 *        context it wants handed back.
 */
void tiku_trk_deskbar_set_menu_icon_painter(tiku_trk_deskbar_t *db,
                                            tiku_trk_db_icon_fn fn,
                                            void *context);

/** @brief The folder the menu is actually reading, or "" (DB-065). */
const char *tiku_trk_deskbar_menu_dir(const tiku_trk_deskbar_t *db);
/** @brief The user's editable folder, even while the fallback is shown. */
const char *tiku_trk_deskbar_user_menu_dir(const tiku_trk_deskbar_t *db);

/**
 * @brief Hand the bar one of the recent lists (DB-002, DB-074).
 *
 * Fed rather than read: the lists are the shell's, shared with the Open
 * panel, and a bar that read them itself would keep a second copy that
 * disagreed.  A list with nothing in it is not shown at all.
 */
void tiku_trk_deskbar_set_recent(tiku_trk_deskbar_t *db,
                                 tiku_trk_db_recent_t which,
                                 const char paths[][TIKU_PATH_MAX],
                                 int n);

/** @brief How many rows the leaf menu has, and what row @p i is. */
int tiku_trk_deskbar_leaf_count(const tiku_trk_deskbar_t *db);
/** @brief Row @p i of the leaf menu, or NULL when there is no such row. */
const tiku_trk_db_item_t *tiku_trk_deskbar_leaf_item(
    const tiku_trk_deskbar_t *db, int i);

/** @brief Whether @p d lays the bar out down an edge rather than along one. */
int tiku_trk_deskbar_vertical(tiku_trk_db_dock_t d);

/** @brief Whether @p d is a collapsed mode: the leaf and the tray only. */
int tiku_trk_deskbar_mini(tiku_trk_db_dock_t d);

/**
 * @brief The dock a point on the screen asks for (DB-048).
 *
 * The screen is divided into zones the way the original's little monitor
 * is: the half the point is nearer decides the edge, and how far into the
 * corner it is decides expanded or mini.  A drag that ends in the middle
 * keeps the dock it started from, which is what makes a mis-drag harmless.
 */
tiku_trk_db_dock_t tiku_trk_deskbar_dock_at(int x, int y, int sw, int sh,
                                            tiku_trk_db_dock_t now);

/** @brief The width (or height, when horizontal) the user has set. */
/* How many programs a shell may offer in the leaf menu.  The table is
 * shared by every group, so a shipped tree that fills it drops whichever
 * group is scanned last -- today that is Programming. */
#define TIKU_TRK_DB_APPS_MAX 24

#define TIKU_TRK_DB_MIN_W 134
#define TIKU_TRK_DB_MAX_W 320

/**
 * @brief Resize the bar's short axis (DB-050).
 *
 * Clamped: below the minimum a row's text is unreadable, and above the
 * maximum the bar is taking screen from the windows it exists to list.
 *
 * @return the width actually taken.
 */
int tiku_trk_deskbar_set_width(tiku_trk_deskbar_t *db, int w);
/** @brief The bar's width, never under TIKU_TRK_DB_MIN_W. */
int tiku_trk_deskbar_width(const tiku_trk_deskbar_t *db);

/*---------------------------------------------------------------------------*/
/* Auto-hide and auto-raise (DB-051, DB-052)                                 */
/*---------------------------------------------------------------------------*/

/** @brief How wide the bar leaves itself when hidden. */
#define TIKU_TRK_DB_SLIVER 2

/** @brief Whether the bar hides until the pointer reaches its edge. */
void tiku_trk_deskbar_set_autohide(tiku_trk_deskbar_t *db, int on);
/**
 * @brief Whether the bar rises over windows when the pointer reaches it;
 *        turning it on takes the bar off always-on-top.
 */
void tiku_trk_deskbar_set_autoraise(tiku_trk_deskbar_t *db, int on);

/**
 * @brief Tell the bar where the pointer is (DB-036, DB-051, DB-052).
 *
 * The one input the bar never had: with auto-hide it decides whether to
 * show itself, with auto-raise whether to come forward, and an open menu
 * uses it to stay open while the pointer is still on its way back.
 *
 * @return 1 when something about the bar changed and it should be redrawn.
 */
int tiku_trk_deskbar_pointer(tiku_trk_deskbar_t *db, int x, int y);
/**
 * @brief The screen point of the open leaf menu's row named @p label.
 *
 * The submenu is searched first, because its rows are drawn over the
 * leaf's.  Zero when no menu is open or nothing is named that.
 */
int tiku_trk_deskbar_menu_point(const tiku_trk_deskbar_t *db,
                                const char *label, int *x, int *y);

/** @brief The open leaf menu's row labels, or its submenu's, "a|b|" style. */
int tiku_trk_deskbar_menu_labels(const tiku_trk_deskbar_t *db, int sub,
                                 char *out, size_t cap);

/**
 * Whether a pointer location is still within the open menu's tracking margin.
 */
int tiku_trk_deskbar_menu_keeps_open(const tiku_trk_deskbar_t *db, int x, int y);
/** @brief Deskbar's close policy in the single-process port (DB-053). */
int tiku_trk_deskbar_quit_policy(int prefs_open, int shortcut_quit);

/** @brief Whether the bar is currently showing more than its sliver. */
int tiku_trk_deskbar_shown(const tiku_trk_deskbar_t *db);

/**
 * @brief Scroll the application list (DB-034).
 *
 * The list scrolls only when there is more of it than fits: arrows appear
 * as soon as it runs past the tray, and until then the wheel over the bar
 * does nothing rather than sliding a list that is entirely visible.
 *
 * @param step Rows, positive downwards.
 * @return 1 when the first shown row changed.
 */
int tiku_trk_deskbar_scroll(tiku_trk_deskbar_t *db, int step);

/** @brief The first application row shown, and whether any are hidden. */
int tiku_trk_deskbar_scroll_at(const tiku_trk_deskbar_t *db);
/** @brief Whether the application list overflows the bar. */
int tiku_trk_deskbar_scrollable(const tiku_trk_deskbar_t *db);

/** @brief Whether the mini dock's running-apps menu is up (DB-037). */
int tiku_trk_deskbar_apps_menu_open(const tiku_trk_deskbar_t *db);

/** @brief The horizontal strip's current button width (DB-033). */
int tiku_trk_deskbar_hbutton_width(const tiku_trk_deskbar_t *db);

/** @brief Whether each scroll arrow should show (DB-034). */
void tiku_trk_deskbar_arrows(const tiku_trk_deskbar_t *db, int *up,
                             int *down);

/**
 * @brief Whether the bar should be drawn ABOVE the windows.
 *
 * Without auto-raise it always is, which is what it has always done.  With
 * auto-raise on it only is while the pointer is on it -- so a window can
 * cover the bar until the user reaches for it (DB-052).
 */
int tiku_trk_deskbar_raised(const tiku_trk_deskbar_t *db);


/** @brief What a click on the Deskbar asked for. */
typedef enum {
    TIKU_TRK_DB_NONE = 0,
    TIKU_TRK_DB_ACTIVATE_WINDOW,  /* `tag` names the window            */
    TIKU_TRK_DB_MINIMIZE_WINDOW,
    TIKU_TRK_DB_CLOSE_WINDOW,
    TIKU_TRK_DB_OPEN_PATH,
    TIKU_TRK_DB_SHOW_ABOUT,
    TIKU_TRK_DB_FIND,
    TIKU_TRK_DB_PREFERENCES,
    TIKU_TRK_DB_TRACKER_PREFERENCES,
    TIKU_TRK_DB_DESKTOP_PREFERENCES,
    TIKU_TRK_DB_FONT_PREFERENCES,
    TIKU_TRK_DB_SOUND_PREFERENCES,
    TIKU_TRK_DB_DEVICE_PREFERENCES,
    TIKU_TRK_DB_STARTUP_PREFERENCES,
    TIKU_TRK_DB_ASSOC_PREFERENCES,
    TIKU_TRK_DB_OPEN_PANEL,     /* path names a described panel */
    TIKU_TRK_DB_HIDE_ALL,
    TIKU_TRK_DB_SHOW_ALL,
    TIKU_TRK_DB_CLOSE_ALL,
    TIKU_TRK_DB_QUIT,
    TIKU_TRK_DB_CLOCK_HIDE,
    TIKU_TRK_DB_CLOCK_SHOW,
    TIKU_TRK_DB_CLOCK_SECONDS,
    TIKU_TRK_DB_CLOCK_DAY,
    TIKU_TRK_DB_CLOCK_ZONE,
    TIKU_TRK_DB_ACTIVATE_TEAM,    /* double-click: the application  */
    TIKU_TRK_DB_LAUNCH_TEAM,      /* middle-click: a new instance   */
    TIKU_TRK_DB_KILL_TEAM,        /* the Vulcan Death Grip (DB-020) */
    TIKU_TRK_DB_QUIT_TEAM,        /* the polite request (DB-028)    */
    TIKU_TRK_DB_TOGGLE_DARK,      /* the little switch on the tray  */
    TIKU_TRK_DB_LAUNCH_APP,       /* `path` is the program to start */
    TIKU_TRK_DB_HELP,             /* the desktop's own pages        */
    TIKU_TRK_DB_TOGGLE_REPLICANTS, /* the bar's own: the row flips it */
    TIKU_TRK_DB_FORCE_QUIT,       /* safe local-app termination     */
} tiku_trk_db_action_t;

typedef struct {
    tiku_trk_db_action_t what;
    void                *tag;
    int                  team;          /* the row the action names */
    char                 path[TIKU_PATH_MAX];
} tiku_trk_db_pick_t;

/** @brief A new bar over @p backend, docked right and expanded, or NULL. */
tiku_trk_deskbar_t *tiku_trk_deskbar_new(tiku_backend_t *backend);

/**
 * @brief Freeze the clock at @p at; 0 returns it to the wall.
 *
 * A scripted run is a fixture, and its clock must be one too: a minute
 * rolling over between an incremental frame and its reference redraw
 * showed up as a fistful of differing clock pixels in nothing else.
 */
void tiku_trk_deskbar_set_clock(tiku_trk_deskbar_t *db, time_t at);

/**
 * @brief The fields the clock's rendering varies with, as one string.
 *
 * The pulse compares this instead of dirtying blindly, so a repaint is
 * owed only when the displayed time actually turned over (DB-044).
 *
 * @return Nonzero while the clock is shown.
 */
int tiku_trk_deskbar_clock_stamp(const tiku_trk_deskbar_t *db, char *out,
                                 size_t max);

/**
 * @brief Let each open little menu's hot row follow the pointer.
 *
 * @return Nonzero when a highlight moved, so the caller knows to paint.
 */
int tiku_trk_deskbar_motion(tiku_trk_deskbar_t *db, int x, int y);

/**
 * @brief What the last tiku_trk_deskbar_motion() changed on screen.
 *
 * The rows and tooltips a hover moves are a strip and a menu, not the
 * desk, so a caller repaints this much and no more.  Taken once: the
 * area is cleared by the call.  Empty when nothing moved.
 */
tiku_rect_t tiku_trk_deskbar_hover_area(tiku_trk_deskbar_t *db);

/** @brief Dwell before opening a submenu, matching other spring menus. */
#define TIKU_TRK_DB_SUBMENU_DELAY_US 500000
/** @brief Advance submenu hover using monotonic microseconds, even at rest.
 * Returns nonzero only when a submenu opens, switches, or closes. Hover never
 * selects a command. Leaving a row cancels its pending opening.
 */
int tiku_trk_deskbar_menu_tick(tiku_trk_deskbar_t *db, int64_t now);

/**
 * @brief A release: an armed expander press commits here (DB-022).
 *
 * @return nonzero when a row expanded or collapsed.
 */
int tiku_trk_deskbar_release(tiku_trk_deskbar_t *db, int x, int y);

/** @brief Whether entry @p team shows its windows inline. */
int tiku_trk_deskbar_team_expanded(const tiku_trk_deskbar_t *db, int team);

/** @brief Attach the durable store used for tray subscriptions. */
void tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db,
                                tiku_store_t *store);
/** @brief Load saved tray paths, pruning entries that no longer resolve. */
int tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db);
/** @brief Remove a tray subscription whose source disappeared. */
int tiku_trk_deskbar_remove_replicant(tiku_trk_deskbar_t *db,
                                      const char *path);
/** @brief Update a tray source after the backing node was moved. */
int tiku_trk_deskbar_repoint_replicant(tiku_trk_deskbar_t *db,
                                       const char *old_path,
                                       const char *new_path);

/** @brief Free the bar. */
void tiku_trk_deskbar_free(tiku_trk_deskbar_t *db);

/** @brief Dock the bar at @p dock; a value outside the docks is ignored. */
void tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db,
                               tiku_trk_db_dock_t dock);
/** @brief Where the bar is docked; right and expanded for no bar. */
tiku_trk_db_dock_t tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db);

/** @brief Apply all live preference effects to the bar (DB-061, DB-073). */
void tiku_trk_deskbar_apply_settings(tiku_trk_deskbar_t *db,
    const tiku_trk_deskbar_settings_t *settings);

/**
 * @brief Register an application in the list.
 *
 * An application appears once however many instances it has, and disappears
 * when its last one quits (DB-014, DB-015).
 *
 * @return Its index, or -1.
 */
int tiku_trk_deskbar_add_team(tiku_trk_deskbar_t *db, const char *name);

/**
 * @brief One instance of @p name, by pid: added to its entry (DB-015).
 *
 * @return the team index, and nonzero in @p changed when a row appeared
 *         or an instance was new.
 */
int tiku_trk_deskbar_add_team_pid(tiku_trk_deskbar_t *db, const char *name,
                                  long pid, const char *command,
                                  int *changed);

/**
 * @brief Drop instances no longer in @p live; a team whose last one went
 *        disappears (DB-015).  Shell-seeded teams stay.
 *
 * @return nonzero when anything changed.
 */
int tiku_trk_deskbar_prune_teams(tiku_trk_deskbar_t *db, const long *live,
                                 int n);

/** @brief Remove one entry outright, without waiting for the roster. */
void tiku_trk_deskbar_remove_team(tiku_trk_deskbar_t *db, int team);

/** @brief The pids behind entry @p team.  @return how many were written. */
int tiku_trk_deskbar_team_pids(const tiku_trk_deskbar_t *db, int team,
                               long *out, int max);

/** @brief The command a new instance of @p team would run, or "". */
const char *tiku_trk_deskbar_team_command(const tiku_trk_deskbar_t *db,
                                          int team);

/** @brief Replace an application's window list, keeping its expanded state. */
void tiku_trk_deskbar_set_windows(tiku_trk_deskbar_t *db, int team,
                                  const tiku_trk_dbwin_t *wins, int n,
                                  int active);

/** @brief Add a tray replicant watching @p path. */
/**
 * @brief Subscribe the tray to @p path under @p label (DB-038).
 *
 * @return the item's numeric id (positive), the existing id when the
 *         same path is already subscribed, or -1 -- including for a
 *         DIFFERENT path wearing an already-taken label, which is the
 *         duplicate-name refusal.
 */
int tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                                   const char *path);

/** @brief Tray index of the item with @p id, or -1. */
int tiku_trk_deskbar_replicant_by_id(const tiku_trk_deskbar_t *db, int id);

/** @brief Tray index of the item labelled @p name, or -1. */
int tiku_trk_deskbar_replicant_by_name(const tiku_trk_deskbar_t *db,
                                       const char *name);

/** @brief Unsubscribe the item with @p id.  @return nonzero if removed. */
int tiku_trk_deskbar_drop_replicant(tiku_trk_deskbar_t *db, int id);

/**
 * @brief Notice moved or deleted tray files (DB-040).
 *
 * A path that no longer resolves is asked about by NODE identity: still
 * somewhere, the item is re-pointed and keeps its place; gone, it is
 * removed.  @return nonzero when anything changed.
 */
int tiku_trk_deskbar_sync_replicants(tiku_trk_deskbar_t *db);
/**
 * @brief Record the height the replicant at @p path asked for, within the
 *        tray's limit, and save the tray.
 * @return 1 when the path is in the tray, 0 otherwise or for a height out
 *         of range.
 */
int tiku_trk_deskbar_set_replicant_height(tiku_trk_deskbar_t *db,
                                          const char *path, int height);
/** @brief The height recorded for the replicant at @p path, or 0. */
int tiku_trk_deskbar_replicant_height(const tiku_trk_deskbar_t *db,
                                      const char *path);
/** @brief Show or hide the tray's replicants. */
void tiku_trk_deskbar_set_show_replicants(tiku_trk_deskbar_t *db, int show);
/** @brief Whether the tray shows its replicants. */
int tiku_trk_deskbar_show_replicants(const tiku_trk_deskbar_t *db);

/** @brief Re-read the tray's nodes.  @return values that changed. */
int tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db);

/**
 * @brief As tiku_trk_deskbar_refresh, but leaving every replicant whose
 *        path begins with @p except to the caller, who reads those its
 *        own way (off the loop, for a board's) and hands the value back
 *        through tiku_trk_deskbar_replicant_set().
 */
int tiku_trk_deskbar_refresh_except(tiku_trk_deskbar_t *db,
                                    const char *except);

/** @brief The paths of the replicants, for a caller reading them itself. */
int tiku_trk_deskbar_replicant_paths(const tiku_trk_deskbar_t *db,
                                     char out[][TIKU_PATH_MAX], int max);

/**
 * @brief A value read elsewhere for the replicant at @p path; NULL for a
 *        read that failed, which removes it as a failed read here would.
 * @return nonzero when something shown changed.
 */
int tiku_trk_deskbar_replicant_set(tiku_trk_deskbar_t *db, const char *path,
                                   const char *value);

/** @brief How many applications the bar is tracking. */
int tiku_trk_deskbar_team_count(const tiku_trk_deskbar_t *db);

/** @brief The name of team @p i, or NULL. */
const char *tiku_trk_deskbar_team_name(const tiku_trk_deskbar_t *db, int i);

/**
 * @brief Strip the owning application's name from a window title.
 *
 * "TikuTracker: /sys" and "/sys - TikuTracker" both become "/sys", which
 * is why the list reads as places rather than as the same word repeated.
 */
void tiku_trk_deskbar_strip_title(const char *app, const char *title,
                                  char *out, size_t max);

/** @brief The strip the bar occupies; windows keep out of it. */
tiku_rect_t tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db);

/** @brief Draw the whole bar onto @p s, laying its frame out first. */
void tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_surface_t *s);

/** @brief Whether the point is inside the bar's frame. */
int tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y);

/**
 * @brief Handle a click.
 *
 * @param mods  TIKU_TRK_MOD_* -- Ctrl minimises rather than raises, as the
 *              original's Ctrl+click does.
 * @return 1 when the click was consumed.
 */
int tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y,
                           unsigned mods, int button, int clicks,
                           tiku_trk_db_pick_t *pick);

/*---------------------------------------------------------------------------*/
/* Drops (DB-008, DB-009, DB-035, DB-066)                                    */
/*---------------------------------------------------------------------------*/

/** @brief What sits under a drag hovering the Deskbar. */
typedef enum {
    TIKU_TRK_DB_DROP_NONE = 0,
    TIKU_TRK_DB_DROP_LEAF,      /* the logo: links go in the Deskbar folder */
    TIKU_TRK_DB_DROP_TEAM,      /* an application: hand the files to it     */
    TIKU_TRK_DB_DROP_MENU_DIR,  /* a folder in the menu: links go there     */
    TIKU_TRK_DB_DROP_MENU_FILE, /* a file in the menu: launch it with them  */
    TIKU_TRK_DB_DROP_BAR,       /* the strip itself: launch the files       */
    TIKU_TRK_DB_DROP_TRAY       /* the tray: subscribe the node (DB-038)    */
} tiku_trk_db_drop_t;

/**
 * @brief What a drag hovering @p x, @p y would drop onto.
 *
 * @param which Receives the team index for DROP_TEAM, or -1.
 */
tiku_trk_db_drop_t tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db,
                                            int x, int y, int *which);

/**
 * @brief Tell the bar a drag is in flight, so its menu can answer for it.
 *
 * While one is, the fixed leaf-menu items are disabled: About and Power off
 * are not drop targets, and leaving them live invites a drop that does
 * something unrelated to dropping (DB-008).
 */
void tiku_trk_deskbar_set_dragging(tiku_trk_deskbar_t *db, int dragging);

/**
 * @brief What a drag over the OPEN leaf menu is on (DB-009).
 *
 * A folder entry takes the links; a fixed entry takes nothing, since
 * those are disabled while a drag is in flight.
 *
 * @param path Receives the folder's path for DROP_MENU_DIR.
 */
tiku_trk_db_drop_t tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db,
                                                 int x, int y, char *path,
                                                 size_t max);

/**
 * @brief Light the application a hovering drag would be handed to.
 *
 * Pass -1 for none.  Held on the bar rather than in the shell because the
 * bar is what draws it, and a highlight the drawing has to be told about
 * separately is one that can disagree with the drop (DB-035).
 */
void tiku_trk_deskbar_set_drop_team(tiku_trk_deskbar_t *db, int i);

/** @brief Which application is lit, or -1. */
int tiku_trk_deskbar_drop_team(const tiku_trk_deskbar_t *db);

/** @brief Whether a fixed leaf-menu item may be chosen right now. */
int tiku_trk_deskbar_item_enabled(const tiku_trk_deskbar_t *db, int fixed);

/**
 * @brief Whether team @p i can be handed files of these types (DB-035).
 *
 * Unknown types are refused unless @p force -- Ctrl is the override, and
 * without it a drop on an application that cannot read the file is a drop
 * that appears to work and does nothing.  Tracker itself takes anything.
 */
int tiku_trk_deskbar_team_accepts(const tiku_trk_deskbar_t *db, int i,
                                  const char *const *types, int ntypes,
                                  int force);

/** @brief Give team @p i a type it can open. */
int tiku_trk_deskbar_team_type(tiku_trk_deskbar_t *db, int i,
                               const char *type);

/**
 * @brief Whether @p path should be launched by a Deskbar drop (DB-066).
 *
 * A directory is silently ignored: launching one has no meaning, and
 * refusing out loud for something the user very likely did by accident
 * would be noise.
 */
int tiku_trk_deskbar_launchable(const tiku_model_t *m);

/**
 * @brief Whether any popup of the bar is open: the leaf menu, the calendar
 *        or the clock menu.
 */
int tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db);
/** @brief Open the leaf menu, as the Menu key does (DB-011). */
void tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db);

/**
 * @brief Close every popup of the bar: the leaf menu, the calendar, the
 *        clock menu and the window menu.
 */
void tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db);

/**
 * @brief Tell the bar which way the theme currently leans.
 *
 * The tray carries a small slide switch drawn like the physical thing: a
 * recessed track and a raised knob with THREE seats -- left for light,
 * middle for no hue, right for dark.  The bar only DRAWS the state;
 * moving it is a pick (TIKU_TRK_DB_TOGGLE_DARK) the shell answers by
 * rewriting the DarkMode setting, so the switch and the Preferences
 * control can never disagree.
 *
 * @param which 0 light, 1 dark, 2 without hue.  Anything else reads as 0.
 */
void tiku_trk_deskbar_set_theme(tiku_trk_deskbar_t *db, int which);

/** @brief Reserve the top @p px of the screen (the global menu bar). */
void tiku_trk_deskbar_set_top_inset(tiku_trk_deskbar_t *db, int px);

/**
 * @brief Offer @p label under Applications, starting @p command when
 *        chosen, drawn with the art named by @p icon.
 *
 * The shell registers what it can find, so a program that is not
 * installed leaves no row behind to disappoint anyone.
 *
 * @return nonzero when the row was taken.
 */
int tiku_trk_deskbar_add_app(tiku_trk_deskbar_t *db, const char *label,
                             const char *command, const char *icon);

/** @brief Which group of the leaf an installed program's row lives in. */
enum {
    TIKU_TRK_DB_GROUP_APPS = 0,
    TIKU_TRK_DB_GROUP_PROGRAMMING,
    TIKU_TRK_DB_GROUP_DEVICE_TOOLS,
    TIKU_TRK_DB_GROUP_DEMOS,
    TIKU_TRK_DB_GROUP_COUNT
};

/**
 * @brief Register a program under a GROUP of the leaf.
 *
 * The groups are rows of the leaf menu, each expanding to its own
 * list: what a person programs WITH is worth a row of its own rather
 * than a place in the middle of everything installed.
 */
int tiku_trk_deskbar_add_app_in(tiku_trk_deskbar_t *db, int group,
                                const char *label, const char *command,
                                const char *icon);

/** @brief Where the theme switch sits, so a test can put a finger on it. */
tiku_rect_t tiku_trk_deskbar_switch_rect(const tiku_trk_deskbar_t *db);

/** @brief Whether the clock's calendar is showing. */
int tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db);
/** @brief Calendar's currently displayed month (1..12) and year/day. */
void tiku_trk_deskbar_calendar_date(const tiku_trk_deskbar_t *db,
                                    int *year, int *month, int *day);
/** @brief Whether the clock is shown. */
int tiku_trk_deskbar_clock_visible(const tiku_trk_deskbar_t *db);
/** @brief Show or hide the clock; hiding it closes the calendar too. */
void tiku_trk_deskbar_set_clock_visible(tiku_trk_deskbar_t *db, int visible);
/** @brief Close the calendar and the clock menu. */
void tiku_trk_deskbar_close_calendar(tiku_trk_deskbar_t *db);

#endif /* TIKU_TRK_DESKBAR_H_ */
