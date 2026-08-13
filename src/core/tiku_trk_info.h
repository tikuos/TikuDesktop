/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_info.h - the Get Info window: everything one entry is.
 *
 * Three tabs over one node.  Information is what the entry IS -- size,
 * dates, kind, where it lives, what a link points at -- read through the
 * same formatters the columns use, so the two cannot say the same fact
 * differently.  Permissions is what it allows.  Attributes is what it
 * carries.
 *
 * The one thing that cannot be answered by looking is a folder's size, so it
 * is walked in the background and the field says "calculating" until the
 * walk finishes or is stopped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_INFO_H_
#define TIKU_TRK_INFO_H_

#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

/** @brief The three tabs, in the order they are shown. */
typedef enum {
    TIKU_TRK_INFO_TAB_INFO = 0,
    TIKU_TRK_INFO_TAB_PERMISSIONS,
    TIKU_TRK_INFO_TAB_ATTRIBUTES,
    TIKU_TRK_INFO_TABS
} tiku_trk_info_tab_t;

/** @brief One row of the Information tab. */
#define TIKU_TRK_INFO_FIELDS   8
#define TIKU_TRK_INFO_VAL_MAX  96

/** @brief Which field, so a caller can tell the clickable ones apart. */
typedef enum {
    TIKU_TRK_INFO_F_NONE = 0,
    TIKU_TRK_INFO_F_SIZE,       /* clicking recalculates a folder's       */
    TIKU_TRK_INFO_F_CAPACITY,
    TIKU_TRK_INFO_F_CREATED,
    TIKU_TRK_INFO_F_MODIFIED,
    TIKU_TRK_INFO_F_KIND,
    TIKU_TRK_INFO_F_LOCATION,   /* clicking opens the parent, selected    */
    TIKU_TRK_INFO_F_LINK,       /* clicking follows it, or re-points it   */
    TIKU_TRK_INFO_F_DIMENSIONS,
    TIKU_TRK_INFO_F_FILESYSTEM
} tiku_trk_info_field_t;

/** @brief How far a folder-size walk has got. */
typedef enum {
    TIKU_TRK_INFO_CALC_IDLE = 0,
    TIKU_TRK_INFO_CALC_RUNNING,
    TIKU_TRK_INFO_CALC_DONE
} tiku_trk_info_calc_t;

/** @brief How many directories deep a size walk will queue. */
#define TIKU_TRK_INFO_CALC_QUEUE 64

/** @brief How many attributes the Attributes tab will list. */
#define TIKU_TRK_INFO_ATTRS 24

/** @brief One listed attribute. */
typedef struct {
    char name[40];
    char value[TIKU_TRK_INFO_VAL_MAX];
    char type[16];              /* the human type name                    */
} tiku_trk_info_attr_t;

/** @brief One info window's state. */
typedef struct {
    int                  used;
    char                 path[TIKU_TRK_PATH_MAX];
    tiku_trk_model_t     model;
    tiku_trk_info_tab_t  tab;
    /* The size walk.  A queue of directories rather than recursion, so a
     * step can return to the caller between any two of them -- a walk that
     * could not be left half-done could not be stopped either. */
    tiku_trk_info_calc_t calc;
    char                 queue[TIKU_TRK_INFO_CALC_QUEUE][TIKU_TRK_PATH_MAX];
    int                  nqueue;
    int64_t              calc_bytes;
    int                  calc_files, calc_dirs;
    int                  calc_overflow;   /* deeper than the queue holds  */
    /* What the last refresh saw, so a poll can say whether anything moved
     * without the caller diffing the strings. */
    int64_t              last_refresh;
    tiku_trk_info_attr_t attr[TIKU_TRK_INFO_ATTRS];
    int                  nattr;
    /* The permissions being shown.  Held separately from the model so a
     * toggle can be applied and then reconciled with what the store
     * actually granted. */
    unsigned             perm;
    int                  perm_editable;
} tiku_trk_info_t;

/**
 * @brief Fill @p in from @p path.  @return 0 when the entry is there.
 *
 * The window is one per NODE: a caller holding several of these finds an
 * existing one by path rather than opening a second on the same entry
 * (AW-065).
 */
int tiku_trk_info_open(tiku_trk_info_t *in, tiku_trk_backend_t *b,
                       const char *path);

/** @brief The window's title: the entry's name and what this window is. */
void tiku_trk_info_title(const tiku_trk_info_t *in, char *out, size_t max);

/** @brief The tab's name, for drawing and for hit-testing. */
const char *tiku_trk_info_tab_name(tiku_trk_info_tab_t tab);

/**
 * @brief One row of the Information tab.
 *
 * The rows shown depend on what the entry IS: a volume reports capacity
 * where a file reports size, a symlink adds where it points, an image adds
 * its dimensions.  Asking for a row past the end returns
 * TIKU_TRK_INFO_F_NONE, which is how a caller finds the count.
 *
 * @param label Receives the field's name.
 * @param value Receives its text, formatted as the columns format it.
 */
tiku_trk_info_field_t tiku_trk_info_field(const tiku_trk_info_t *in,
                                          tiku_trk_store_t *store, int i,
                                          const char **label, char *value,
                                          size_t max);

/*---------------------------------------------------------------------------*/
/* The folder-size walk (AW-068)                                             */
/*---------------------------------------------------------------------------*/

/** @brief Begin (or restart) the walk.  Only containers have one. */
void tiku_trk_info_calc_start(tiku_trk_info_t *in);

/**
 * @brief Read one queued directory.
 *
 * @return 1 when there is more to do, so a caller can drive it from an idle
 *         tick and keep answering the mouse (the same discipline the
 *         thumbnail queue uses).
 */
int tiku_trk_info_calc_step(tiku_trk_info_t *in, tiku_trk_backend_t *b);

/** @brief Abandon the walk; what it had counted so far is discarded. */
void tiku_trk_info_calc_stop(tiku_trk_info_t *in);

/*---------------------------------------------------------------------------*/
/* Liveness (AW-069, AW-077)                                                 */
/*---------------------------------------------------------------------------*/

/** @brief How often a volume's free space and an open file's size re-read. */
#define TIKU_TRK_INFO_REFRESH_US 1000000

/**
 * @brief Re-read the facts if it is time to.
 *
 * Volumes and plain files only: their numbers change without anything
 * telling us, because free space is nobody's node and a file's size is not
 * reported until it is closed.
 *
 * @return 1 when something changed.
 */
int tiku_trk_info_refresh(tiku_trk_info_t *in, tiku_trk_backend_t *b,
                          int64_t now);

/** @brief The entry was renamed or moved; the window follows it (AW-077). */
int tiku_trk_info_moved(tiku_trk_info_t *in, tiku_trk_backend_t *b,
                        const char *from, const char *to);

/**
 * @brief Look for the entry again after it stopped being where it was.
 *
 * By NODE, in the folder it was in: a rename changes the name and nothing
 * else, so the entry answering to the same identity there is the same
 * entry.  A move to somewhere else cannot be found this way and reads as
 * gone, which is what a window with no notice channel can honestly say.
 *
 * @return 1 when it was found and the window re-pointed.
 */
int tiku_trk_info_refind(tiku_trk_info_t *in, tiku_trk_backend_t *b);

/** @brief Whether the entry has gone, so the window must close (AW-077). */
int tiku_trk_info_gone(const tiku_trk_info_t *in, const char *removed);

/*---------------------------------------------------------------------------*/
/* Permissions (AW-078)                                                      */
/*---------------------------------------------------------------------------*/

/** @brief The three columns of the grid. */
#define TIKU_TRK_INFO_PERM_READ  0
#define TIKU_TRK_INFO_PERM_WRITE 1
#define TIKU_TRK_INFO_PERM_EXEC  2

/**
 * @brief Toggle one box and apply it at once.
 *
 * There is no OK button, so there is no moment at which a change is
 * pending; a store that refuses says so through @p err and the box goes
 * back to what it was rather than showing a permission the entry does not
 * have.
 *
 * @return 0 when applied.
 */
int tiku_trk_info_perm_toggle(tiku_trk_info_t *in, tiku_trk_backend_t *b,
                              int which, char *err, size_t errmax);

/** @brief Whether the box for @p which is ticked. */
int tiku_trk_info_perm(const tiku_trk_info_t *in, int which);

/*---------------------------------------------------------------------------*/
/* Attributes (AW-079)                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill the attribute list from the store.
 *
 * @return how many rows the tab will show.
 */
int tiku_trk_info_attrs(tiku_trk_info_t *in, tiku_trk_store_t *store);

/*---------------------------------------------------------------------------*/
/* Where the clickable fields lead (AW-072, AW-074)                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief The folder to open and the entry to select for Location.
 *
 * @return 1 when there is somewhere to go.
 */
int tiku_trk_info_location(const tiku_trk_info_t *in, char *dir, size_t dmax,
                           char *select, size_t smax);

/**
 * @brief The same for "Link to".
 *
 * @return 1 when the target resolves; 0 when the link is broken, which is
 *         the caller's cue to offer a picker rather than an error.
 */
int tiku_trk_info_link_target(const tiku_trk_info_t *in,
                              tiku_trk_backend_t *b, char *dir, size_t dmax,
                              char *select, size_t smax);

/**
 * @brief Point a broken link at @p target, keeping its name and state.
 *
 * The attributes are carried across by hand: the link is replaced, and a
 * replacement that dropped what the old one carried would lose the pose's
 * position and its icon along with the target (AW-074).
 *
 * @return 0, or -1 with @p err filled.
 */
int tiku_trk_info_relink(tiku_trk_info_t *in, tiku_trk_backend_t *b,
                         tiku_trk_store_t *store, const char *target,
                         char *err, size_t errmax);

#endif /* TIKU_TRK_INFO_H_ */
