/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskicons.h - the rules the Desktop's own icons obey.
 *
 * The Desktop is a pose view with three things no folder window has: it is
 * never a list, its icons must stay somewhere the user can reach them, and
 * it shows the union of every volume's Desktop folder rather than one
 * directory.  The last of those is a virtual directory, so it is one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKICONS_H_
#define TIKU_TRK_DESKICONS_H_

#include "tiku_vdir.h"
#include "tiku_volume.h"

/** @brief A rectangle, in screen coordinates. */
typedef struct {
    int x, y, w, h;
} tiku_trk_rect_t;

/**
 * @brief Coerce a saved view mode for the Desktop (PVL-008).
 *
 * The Desktop is never a list: a saved state that says otherwise is from a
 * window, or from a build that allowed it, and honouring it would leave the
 * user with a Desktop they cannot put icons on.
 *
 * @return the mode the Desktop should actually use.
 */
int tiku_trk_deskicons_mode(int saved);

/** @brief How far an icon must stay clear of the Deskbar and of others. */
#define TIKU_TRK_DESK_BAR_MARGIN   10
#define TIKU_TRK_DESK_OTHER_MARGIN  5

/**
 * @brief Whether @p r is somewhere an icon may sit (PVL-018).
 *
 * Wholly on screen, clear of the Deskbar by a margin, and clear of any
 * replicant or other reserved rectangle by a smaller one.
 */
int tiku_trk_deskicons_valid(tiku_trk_rect_t r, tiku_trk_rect_t screen,
                             tiku_trk_rect_t deskbar,
                             const tiku_trk_rect_t *others, int nothers);

/**
 * @brief Pull @p r back onto @p screen, keeping its size.
 *
 * @return 1 when it had to move.
 */
int tiku_trk_deskicons_pull_back(tiku_trk_rect_t *r, tiku_trk_rect_t screen);

/** @brief Preserve a rectangle's relative position across a screen resize. */
void tiku_trk_deskicons_reflow(tiku_trk_rect_t *r,
                               tiku_trk_rect_t old_area,
                               tiku_trk_rect_t new_area);

/** @brief Move a rectangle to the nearest unoccupied grid slot. */
int tiku_trk_deskicons_find_free(tiku_trk_rect_t *r,
    tiku_trk_rect_t area, const tiku_trk_rect_t *others, int nothers,
    int step_x, int step_y);

/** @brief One remembered position, for one screen size (PVL-030). */
typedef struct {
    int sw, sh;                     /* the screen it was recorded on     */
    int x, y;
} tiku_trk_deskpos_t;

#define TIKU_TRK_DESK_POS_MAX 8

/** @brief Every position remembered for one icon. */
typedef struct {
    tiku_trk_deskpos_t at[TIKU_TRK_DESK_POS_MAX];
    int                n;
} tiku_trk_deskpos_set_t;

/** @brief Record where an icon sits on the current screen. */
void tiku_trk_deskicons_remember(tiku_trk_deskpos_set_t *s, int sw, int sh,
                                 int x, int y);

/**
 * @brief Where an icon should sit on a screen of @p sw by @p sh.
 *
 * @return 1 when a position was recorded for that size.
 */
int tiku_trk_deskicons_recall(const tiku_trk_deskpos_set_t *s, int sw,
                              int sh, int *x, int *y);

/**
 * @brief Build the Desktop's source: every volume's Desktop folder.
 *
 * A union rather than one directory, so a second disk's Desktop items
 * appear beside the boot volume's (PVN-051).
 *
 * @param home The boot volume's Desktop folder, which on a system with a
 *             home directory is the user's own rather than one at the
 *             mount point.  It contributes FIRST, so a name it holds is
 *             the one that shows.  NULL or "" leaves it out.
 */
void tiku_trk_deskicons_sources(const tiku_volumes_t *vs,
                                tiku_backend_t *b, const char *home,
                                tiku_vdir_t *out);

/**
 * @brief Whether the "/" icon should be shown, given the two settings.
 *
 * They are mutually exclusive in the original's preferences; here the
 * question is only whether the icon appears, so both are read (PVN-058).
 */
int tiku_trk_deskicons_show_disks(int show_disks_icon, int volumes_on_desktop);

/**
 * @brief Whether @p v earns its own Desktop pose (PVN-040).
 *
 * The boot volume is already the Disks pose; a shared volume follows its
 * own setting; and only a persistent volume shows at all -- an icon for a
 * RAM filesystem would be an icon for a thing that forgets.
 */
int tiku_trk_deskicons_volume_pose(const tiku_volumes_t *vs,
                                   const tiku_volume_t *v,
                                   int volumes_on_desktop, int shared_too);

#endif /* TIKU_TRK_DESKICONS_H_ */
