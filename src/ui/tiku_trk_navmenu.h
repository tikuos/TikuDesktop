/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navmenu.h - a menu you can walk a folder tree with.
 *
 * The interesting part is not the drawing: it is that the listing must be
 * built a little at a time so a folder of ten thousand items never freezes
 * the menu, must be re-read every time it opens because a menu is not a
 * snapshot, and must hide exactly what the window hides -- a menu that
 * showed a dot file the window does not would be a second, disagreeing
 * view of the same folder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NAVMENU_H_
#define TIKU_TRK_NAVMENU_H_

#include "tiku_model.h"

/** @brief How many entries one level holds. */
#define TIKU_TRK_NAV_MAX 256

/** @brief How many levels deep a walk may go. */
#define TIKU_TRK_NAV_DEPTH 8

/**
 * @brief How much of a folder one fill step takes.
 *
 * Tracker's twenty items or two hundred milliseconds, whichever comes
 * first: the point is that the menu answers the mouse between steps.
 */
#define TIKU_TRK_NAV_CHUNK 20
#define TIKU_TRK_NAV_SLICE_US 200000

/** @brief One entry in a level. */
typedef struct {
    char name[TIKU_NAME_MAX];
    char path[TIKU_PATH_MAX];
    int  is_container;              /* opens a submenu of its own        */
    int  enabled;
} tiku_trk_nav_item_t;

/** @brief One open level of the walk. */
typedef struct {
    char                path[TIKU_PATH_MAX];
    tiku_trk_nav_item_t item[TIKU_TRK_NAV_MAX];
    int                 n;
    int                 sel;        /* highlighted row, or -1            */
    int                 done;       /* the fill has reached the end      */
    void               *walk;       /* the fill's own position           */
} tiku_trk_nav_level_t;

/** @brief A walk: a stack of levels, the last one innermost. */
typedef struct {
    tiku_trk_nav_level_t level[TIKU_TRK_NAV_DEPTH];
    int                  depth;
    tiku_backend_t  *backend;
    int                  hide_dotfiles;
    int                  folders_first;
} tiku_trk_nav_t;

/** @brief Begin a walk at @p path.  Nothing is listed yet. */
void tiku_trk_nav_open(tiku_trk_nav_t *nav, tiku_backend_t *b,
                       const char *path);

/**
 * @brief Fill a little more of the innermost level.
 *
 * @return 1 when there is still more to do, so a caller can keep asking
 *         between events rather than blocking on a huge folder (CW-055).
 */
int tiku_trk_nav_fill(tiku_trk_nav_t *nav, int level);

/** @brief Fill a level completely.  For tests and small folders. */
void tiku_trk_nav_fill_all(tiku_trk_nav_t *nav, int level);

/**
 * @brief Descend into the entry at @p i of the innermost level.
 *
 * Re-read rather than cached: a menu is not a snapshot, and one that
 * showed what the folder held last time would be lying (CW-055).
 *
 * @return 1 when a level was opened.
 */
int tiku_trk_nav_enter(tiku_trk_nav_t *nav, int i);

/** @brief Close the innermost level.  @return 1 when one was closed. */
int tiku_trk_nav_leave(tiku_trk_nav_t *nav);

/** @brief What the arrow keys do (CW-056). */
typedef enum {
    TIKU_TRK_NAV_UP = 0,
    TIKU_TRK_NAV_DOWN,
    TIKU_TRK_NAV_IN,
    TIKU_TRK_NAV_OUT
} tiku_trk_nav_key_t;

/** @brief Move.  @return 1 when something changed. */
int tiku_trk_nav_move(tiku_trk_nav_t *nav, tiku_trk_nav_key_t k);

/** @brief The innermost level, or NULL. */
tiku_trk_nav_level_t *tiku_trk_nav_top(tiku_trk_nav_t *nav);

/** @brief What choosing the highlighted row would open, or NULL. */
const char *tiku_trk_nav_chosen(tiku_trk_nav_t *nav);

/**
 * @brief Whether a folder menu belongs on a context menu for @p m.
 *
 * Only containers get one; a file has nothing to navigate into, and
 * offering an empty submenu is worse than offering none (CW-045).
 */
int tiku_trk_nav_wanted(const tiku_model_t *m);

/**
 * @brief Where a level sits, given where its parent item is.
 *
 * Off the parent's right edge, flipped to the left when that would leave
 * the screen -- the same rule a submenu of an ordinary menu follows.
 */
void tiku_trk_nav_place(const tiku_trk_nav_t *nav, int level, int px,
                        int py, int screen_w, int screen_h,
                        int *x, int *y, int *w, int *h);

/**
 * @brief Enable a SPRING menu's entries by what the drag can land on.
 *
 * Containers are always enabled, a broken entry never, and a file only
 * when it is a registered application claiming one of the dragged types --
 * the same three-way rule SpringLoadedFolderSetMenuStates applies
 * (PVS-052), combined through tiku_trk_springmenu_item_enabled.
 */
struct tiku_trk_apps;
void tiku_trk_nav_spring_states(tiku_trk_nav_t *nav,
                                const struct tiku_trk_apps *apps,
                                const char *const *types, int ntypes);

/** @brief The item at a point in @p level, or -1. */
int tiku_trk_nav_item_at(const tiku_trk_nav_t *nav, int level, int fx,
                         int fy, int x, int y);

/** @brief One row's height, and the width a level needs. */
#define TIKU_TRK_NAV_ROW 18

/** @brief What an empty level shows instead of nothing. */
#define TIKU_TRK_NAV_EMPTY "Empty folder"

#endif /* TIKU_TRK_NAVMENU_H_ */
