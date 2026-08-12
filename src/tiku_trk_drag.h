/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag.h - the drag gesture: arming, payload and drop validity.
 *
 * The file engine underneath a drop was built and tested long before any
 * gesture existed to reach it, so what lives here is only the part between
 * the press and the drop: when a press becomes a drag, what the drag is
 * carrying, and whether the thing under the pointer will take it.  Where the
 * bytes then go is tiku_trk_fs's business.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DRAG_H_
#define TIKU_TRK_DRAG_H_

#include "tiku_trk_model.h"

/** @brief How many items one drag can carry. */
#define TIKU_TRK_DRAG_MAX 64

/**
 * @brief How far the pointer must move before a press becomes a drag.
 *
 * Tracker's radiusThreshold, 4 px, compared squared so the test needs no
 * square root.  Small enough that a deliberate drag starts at once, large
 * enough that a shaky click stays a click.
 */
#define TIKU_TRK_DRAG_SLOP 4

/**
 * @brief Modifiers a drop reads, named rather than borrowed from X.
 *
 * Read at the DROP and not at the press: the user decides what the gesture
 * means while it is in the air, which is why Option-dropping into the same
 * folder duplicates but Option-pressing does nothing.
 */
#define TIKU_TRK_DRAG_MOD_COPY 0x1u    /* Option: copy even within a volume */
#define TIKU_TRK_DRAG_MOD_LINK 0x2u    /* Control: make a link instead      */

/** @brief What a drop should do, once something decides. */
typedef enum {
    TIKU_TRK_DROP_NOTHING = 0,
    TIKU_TRK_DROP_MOVE,
    TIKU_TRK_DROP_COPY,
    TIKU_TRK_DROP_LINK
} tiku_trk_drop_t;

/** @brief One drag in flight. */
typedef struct {
    int      armed;                 /* pressed on a pose, not yet moved  */
    int      active;                /* past the slop: a real drag        */
    int      x0, y0;                /* the PRESS point, not the current  */
    int      x, y;
    unsigned buttons;               /* what was held at the press        */
    unsigned mods;
    int      count;
    char     path[TIKU_TRK_DRAG_MAX][TIKU_TRK_PATH_MAX];
    char     src_dir[TIKU_TRK_PATH_MAX];
    void    *src_win;               /* opaque owner token                */
} tiku_trk_drag_t;

/** @brief Forget everything; a drag that is not happening. */
void tiku_trk_drag_reset(tiku_trk_drag_t *d);

/**
 * @brief Arm on a press over a pose.
 *
 * Armed, not started: the drag begins only if the pointer then moves far
 * enough, which is what stops every click being a one-pixel drag (PVS-032).
 * A press that is part of a double-click must not arm -- pass @p clicks.
 */
void tiku_trk_drag_arm(tiku_trk_drag_t *d, int x, int y, unsigned buttons,
                       unsigned mods, int clicks, void *src_win,
                       const char *src_dir);

/** @brief Add one item to what the drag carries (PVS-033). */
int tiku_trk_drag_add(tiku_trk_drag_t *d, const char *path);

/**
 * @brief Report pointer movement.
 *
 * @return 1 on the move that STARTS the drag, 0 otherwise.
 */
int tiku_trk_drag_moved(tiku_trk_drag_t *d, int x, int y);

/** @brief Whether a real drag is in flight. */
int tiku_trk_drag_active(const tiku_trk_drag_t *d);

/** @brief Whether @p path is one of the items being dragged (PVS-042). */
int tiku_trk_drag_carries(const tiku_trk_drag_t *d, const char *path);

/**
 * @brief Whether @p over can be a drop target for this drag (MA-016).
 *
 * A container takes items; anything else does not.  A pose that is itself
 * being dragged is never a target, which is what makes a drop onto the
 * selection behave as a drop on the window behind it.
 */
int tiku_trk_drag_can_drop(const tiku_trk_drag_t *d,
                           const tiku_trk_model_t *over);

/**
 * @brief What a drop on @p dst_dir means.
 *
 * The same-volume rule as a paste, plus the modifiers: Option forces a
 * copy even within one volume, and Control asks for a link.  A drop back
 * into the folder the items came from means nothing at all.
 */
tiku_trk_drop_t tiku_trk_drag_action(const tiku_trk_drag_t *d,
                                     const char *dst_dir, unsigned mods,
                                     int same_volume);

#endif /* TIKU_TRK_DRAG_H_ */
