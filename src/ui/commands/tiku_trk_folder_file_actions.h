/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_file_actions.h - filesystem-facing folder commands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_FILE_ACTIONS_H_
#define TIKU_TRK_FOLDER_FILE_ACTIONS_H_

#include <stddef.h>

#include "tiku_trk_alert.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_prefs_window.h"

typedef void (*tiku_trk_folder_close_under_fn)(void *context,
                                                const char *path);

/** @brief Services shared by folder commands that mutate filesystem state. */
typedef struct {
    tiku_trk_fs_t                    *fs;
    tiku_trk_clip_t                  *clip;
    tiku_trk_drag_t                  *pending;
    char                             *drop_destination;
    tiku_trk_alert_t                 *alert;
    tiku_trk_prefs_window_t          *preferences;
    char                             *status;
    size_t                            status_size;
    tiku_trk_folder_close_under_fn    close_under;
    void                             *close_context;
} tiku_trk_folder_file_actions_t;

/** @brief Bind filesystem, clipboard, dialog, and shell-close services. */
void tiku_trk_folder_file_actions_init(
    tiku_trk_folder_file_actions_t *controller, tiku_trk_fs_t *fs,
    tiku_trk_clip_t *clip, tiku_trk_drag_t *pending,
    char *drop_destination, tiku_trk_alert_t *alert,
    tiku_trk_prefs_window_t *preferences, char *status, size_t status_size,
    tiku_trk_folder_close_under_fn close_under, void *close_context);

/** @brief Collect selected paths into stable caller-owned storage. */
int tiku_trk_folder_file_actions_selection(
    const tiku_trk_folder_window_t *folder,
    char paths[][TIKU_TRK_PATH_MAX], const char **selection, int maximum);

/** @brief Apply a filesystem-facing command, returning whether handled. */
int tiku_trk_folder_file_actions_invoke(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, int command);

#endif /* TIKU_TRK_FOLDER_FILE_ACTIONS_H_ */
