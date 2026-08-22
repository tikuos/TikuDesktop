/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_roster.h - Deskbar process and window publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_ROSTER_H_
#define TIKU_TRK_DESKBAR_ROSTER_H_

#include "tiku_window.h"
#include "tiku_trk_deskbar.h"

/** @brief Add this user's foreground host processes to the Deskbar. */
/** @brief Sync the roster with the live process set.  @return changed. */
int tiku_trk_deskbar_scan_processes(tiku_trk_deskbar_t *deskbar);

/** @brief Publish a workspace's open windows beneath one Deskbar team. */
void tiku_trk_deskbar_publish_workspace(tiku_trk_deskbar_t *deskbar,
    int team, tiku_workspace_t *workspace);

#endif /* TIKU_TRK_DESKBAR_ROSTER_H_ */
