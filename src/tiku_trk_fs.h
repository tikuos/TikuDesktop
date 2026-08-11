/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fs.h - file operations.
 *
 * Copy, move, duplicate, rename, trash and undo, with Tracker's rules: the
 * same-volume test decides move versus copy, duplicate invents a name rather
 * than asking, and an operation reports progress so a window never freezes
 * waiting for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FS_H_
#define TIKU_TRK_FS_H_

#include <stddef.h>
#include "tiku_trk_model.h"

/** @brief What an operation is doing, for the progress window. */
typedef enum {
    TIKU_TRK_OP_IDLE = 0,
    TIKU_TRK_OP_COPY,
    TIKU_TRK_OP_MOVE,
    TIKU_TRK_OP_DUPLICATE,
    TIKU_TRK_OP_TRASH,
    TIKU_TRK_OP_DELETE,
    TIKU_TRK_OP_RENAME,
    TIKU_TRK_OP_LINK,
} tiku_trk_op_t;

/** @brief How a name collision was resolved. */
typedef enum {
    TIKU_TRK_CONFLICT_ASK = 0,   /* stop and report; the UI decides       */
    TIKU_TRK_CONFLICT_REPLACE,
    TIKU_TRK_CONFLICT_SKIP,
    TIKU_TRK_CONFLICT_RENAME,    /* invent a fresh name, as Duplicate does */
} tiku_trk_conflict_t;

/** @brief Live state of a running operation. */
typedef struct {
    tiku_trk_op_t op;
    int           total;          /* items to process                     */
    int           done;
    int           skipped;
    int           failed;
    char          current[TIKU_TRK_PATH_MAX];
    char          error[256];     /* the backend's own words, verbatim    */
    int           conflict;       /* 1 while stopped on a collision       */
    char          conflict_path[TIKU_TRK_PATH_MAX];
} tiku_trk_progress_t;

typedef struct tiku_trk_fs tiku_trk_fs_t;

/** @brief Create the operation engine for one backend. */
tiku_trk_fs_t *tiku_trk_fs_new(tiku_trk_backend_t *backend);

void tiku_trk_fs_free(tiku_trk_fs_t *fs);

/**
 * @brief Decide what a drop does: move within a volume, copy across one.
 *
 * Tracker's rule, and the reason dragging to another disk keeps the original.
 * @param force_copy  Non-zero when the user asked for a copy explicitly.
 * @return TIKU_TRK_OP_MOVE or TIKU_TRK_OP_COPY.
 */
tiku_trk_op_t tiku_trk_fs_drop_action(tiku_trk_fs_t *fs, const char *src,
                                      const char *dst_dir, int force_copy);

/**
 * @brief The name Duplicate would give a copy of @p path.
 *
 * "<name> copy", then "copy 2", "copy 3"; duplicating something already
 * called "<name> copy" gives "copy 2", never "copy copy" (FS-006).
 */
int tiku_trk_fs_duplicate_name(tiku_trk_fs_t *fs, const char *dir,
                               const char *name, char *out, size_t max);

/**
 * @brief The name Create Link would use: the plain name unless it is taken,
 *        then "<name> link", "<name> link 2" (FS-009).
 */
int tiku_trk_fs_link_name(tiku_trk_fs_t *fs, const char *dir,
                          const char *name, char *out, size_t max);

/** @brief Start copying @p paths into @p dst_dir.  @return 0 if accepted. */
int tiku_trk_fs_copy(tiku_trk_fs_t *fs, const char *const *paths, int n,
                     const char *dst_dir, tiku_trk_conflict_t policy);

/** @brief Start moving.  Items already in @p dst_dir are skipped (FS-005). */
int tiku_trk_fs_move(tiku_trk_fs_t *fs, const char *const *paths, int n,
                     const char *dst_dir, tiku_trk_conflict_t policy);

/** @brief Duplicate in place; each item lands in its OWN parent (FS-008). */
int tiku_trk_fs_duplicate(tiku_trk_fs_t *fs, const char *const *paths, int n);

/**
 * @brief Move to Trash, or delete where there is no Trash.
 *
 * The Trash itself is silently dropped from the selection first, so it is
 * never possible to trash the Trash.
 */
int tiku_trk_fs_trash(tiku_trk_fs_t *fs, const char *const *paths, int n);

/** @brief Rename one entry.  @return 0, or -1 with @p err filled. */
int tiku_trk_fs_rename(tiku_trk_fs_t *fs, const char *path,
                       const char *new_name, char *err, size_t errmax);

/**
 * @brief Whether a name is acceptable before anything is attempted.
 *
 * Empty, "." / "..", and names containing '/' are refused; the message is
 * what the UI shows.
 */
int tiku_trk_fs_name_ok(const char *name, char *err, size_t errmax);

/**
 * @brief The characters a name may not contain, as one string (MA-046).
 *
 * The editor refuses these at the keystroke rather than at commit, so the
 * set has to come from whoever owns the naming rules -- a namespace path and
 * a file name do not forbid the same things.
 */
const char *tiku_trk_fs_name_deny(void);

/** @brief Advance the current operation a little.  @return items processed. */
int tiku_trk_fs_step(tiku_trk_fs_t *fs, int budget);

/** @brief Snapshot of what is happening, for the progress window. */
const tiku_trk_progress_t *tiku_trk_fs_progress(const tiku_trk_fs_t *fs);

/** @brief Answer a conflict the engine stopped on and continue. */
void tiku_trk_fs_resolve(tiku_trk_fs_t *fs, tiku_trk_conflict_t how);

/** @brief Abandon the running operation; what is done stays done. */
void tiku_trk_fs_cancel(tiku_trk_fs_t *fs);

int tiku_trk_fs_busy(const tiku_trk_fs_t *fs);

/*---------------------------------------------------------------------------*/
/* Undo                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether the last operation can be undone, and its description.
 *
 * A move and a rename are reversible; a copy is undone by removing what it
 * created; a delete is NOT undoable and says so.
 */
int tiku_trk_fs_can_undo(const tiku_trk_fs_t *fs, char *what, size_t max);

/** @brief Reverse the last operation.  @return items reversed, or -1. */
int tiku_trk_fs_undo(tiku_trk_fs_t *fs, char *err, size_t errmax);

#endif /* TIKU_TRK_FS_H_ */
