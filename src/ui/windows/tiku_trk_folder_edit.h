/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_edit.h - folder value-edit and rename controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_EDIT_H_
#define TIKU_TRK_FOLDER_EDIT_H_

#include <stddef.h>

#include "tiku_attr.h"
#include "tiku_trk_edit.h"
#include "tiku_trk_folder_window.h"
#include "tiku_fs.h"

/** @brief State and dependencies for both kinds of inline folder editing. */
typedef struct {
    tiku_backend_t *backend;
    tiku_fs_t *fs;
    char *status;
    size_t status_size;
    int *dirty;
    int editing;
    tiku_trk_edit_t value_edit;
    char edit_path[TIKU_PATH_MAX];
    tiku_desc_t edit_desc;
    int renaming;
    tiku_trk_edit_t rename_edit;
    char rename_path[TIKU_PATH_MAX];
    tiku_trk_folder_window_t *rename_folder;
    int rename_row;
} tiku_trk_folder_edit_t;

void tiku_trk_folder_edit_init(tiku_trk_folder_edit_t *controller,
    tiku_backend_t *backend, tiku_fs_t *fs, char *status,
    size_t status_size, int *dirty);
void tiku_trk_folder_edit_begin_value(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder);
void tiku_trk_folder_edit_commit_value(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder);
void tiku_trk_folder_edit_cancel_value(tiku_trk_folder_edit_t *controller);
void tiku_trk_folder_edit_begin_rename(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, int row);
void tiku_trk_folder_edit_finish_rename(tiku_trk_folder_edit_t *controller,
    int commit);
int tiku_trk_folder_edit_key(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, unsigned key, unsigned modifiers,
    char character);

#endif /* TIKU_TRK_FOLDER_EDIT_H_ */
