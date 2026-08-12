/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_volume.h - what a volume is, and what that means for an operation.
 *
 * Until now the port knew filesystems only as st_dev numbers, which is
 * enough to tell a move from a copy and nothing else.  Several rules need
 * more than that: whether a volume will take a write at all, which one the
 * system booted from, and what a whole volume dragged to the Trash means.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_VOLUME_H_
#define TIKU_TRK_VOLUME_H_

#include "tiku_trk_model.h"

#include <stdint.h>
#include <sys/types.h>

/** @brief How many mounted volumes are tracked. */
#define TIKU_TRK_VOLUME_MAX 32

/** @brief One mounted volume. */
typedef struct {
    char     mount[TIKU_TRK_PATH_MAX];
    char     name[TIKU_TRK_NAME_MAX];
    dev_t    dev;
    int      read_only;
    int      is_boot;               /* the volume "/" lives on           */
    int      removable;             /* can be unmounted at all           */
    uint64_t total, avail;
} tiku_trk_volume_t;

/** @brief Every volume the system has mounted. */
typedef struct {
    tiku_trk_volume_t v[TIKU_TRK_VOLUME_MAX];
    int               n;
} tiku_trk_volumes_t;

/**
 * @brief Read the mount table.
 *
 * @return how many volumes were found, or -1.
 */
int tiku_trk_volumes_scan(tiku_trk_volumes_t *vs);

/** @brief The volume @p path is on, or NULL. */
const tiku_trk_volume_t *tiku_trk_volume_of(const tiku_trk_volumes_t *vs,
                                            const char *path);

/** @brief Whether @p path is the mount point of a volume. */
int tiku_trk_volume_is_root(const tiku_trk_volumes_t *vs, const char *path);

/**
 * @brief Whether a whole volume dragged to the Trash may be unmounted.
 *
 * The boot volume never can: the system is running from it, and "delete"
 * was never what the gesture meant anyway (FS-041).
 *
 * @param why Receives the refusal, when there is one.
 * @return 1 when the unmount may proceed.
 */
int tiku_trk_volume_may_unmount(const tiku_trk_volumes_t *vs,
                                const char *path, char *why, size_t max);

/**
 * @brief Whether an operation on @p path is allowed by its volume.
 *
 * @param write Non-zero when the operation changes the item (a move, a
 *              delete, a rename); zero for a read (a copy FROM it).
 * @param why   Receives the refusal, when there is one.
 * @return 1 when it may proceed.
 */
int tiku_trk_volume_may_write(const tiku_trk_volumes_t *vs, const char *path,
                              int write, char *why, size_t max);

/** @brief Below this share free, the bar warns (TS-054). */
#define TIKU_TRK_VOLUME_WARN_PCT 5

/** @brief Which of the three space-bar colours a volume calls for. */
typedef enum {
    TIKU_TRK_SPACE_USED = 0,
    TIKU_TRK_SPACE_FREE,
    TIKU_TRK_SPACE_WARNING
} tiku_trk_space_t;

/**
 * @brief How full @p v is, and whether it is low enough to warn.
 *
 * @param used_pct Receives the used share, 0..100.
 * @return the colour the USED part of the bar takes: the warning colour
 *         replaces the used one rather than being drawn beside it, which
 *         is what makes a nearly-full volume read at a glance.
 */
tiku_trk_space_t tiku_trk_volume_space(const tiku_trk_volume_t *v,
                                       int *used_pct);

#endif /* TIKU_TRK_VOLUME_H_ */
