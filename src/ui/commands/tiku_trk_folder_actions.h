/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_actions.h - direct folder-view command actions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_ACTIONS_H_
#define TIKU_TRK_FOLDER_ACTIONS_H_

#include "tiku_trk_folder_window.h"

/** @brief Apply a view-local command, returning whether it was handled. */
int tiku_trk_folder_actions_invoke(tiku_workspace_t *workspace,
    tiku_trk_folder_window_t *folder, int command);

#endif /* TIKU_TRK_FOLDER_ACTIONS_H_ */
