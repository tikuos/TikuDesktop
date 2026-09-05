/*
 * TikuTracker -- the file manager for TikuOS.
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
#include "tiku_fs.h"

typedef void (*tiku_trk_folder_close_under_fn)(void *context,
                                                const char *path);

/**
 * @brief Show the preferences, however the shell showing them does it.
 *
 * A callback rather than a window: what Preferences MEANS is the shell's
 * business, and the command layer is shared by a shell that has no window
 * stack at all.
 */
typedef void (*tiku_trk_folder_prefs_fn)(void *context);

/** @brief Services shared by folder commands that mutate filesystem state. */
typedef struct {
    tiku_fs_t                    *fs;
    tiku_backend_t               *backend;
    tiku_trk_clip_t                  *clip;
    tiku_trk_drag_t                  *pending;
    char                             *drop_destination;
    tiku_alert_t                 *alert;
    tiku_trk_folder_prefs_fn          open_prefs;
    void                             *prefs_context;
    char                             *status;
    size_t                            status_size;
    tiku_trk_folder_close_under_fn    close_under;
    void                             *close_context;
    /* A paste of a mixed clipboard is a move AND a copy, and the engine
     * takes one job at a time: the second waits here until it is idle
     * (FS-055). */
    char   deferred[64][TIKU_PATH_MAX];
    const char *deferred_paths[64];
    int    deferred_count;
    char   deferred_dest[TIKU_PATH_MAX];
    /* One named path standing in for a window's selection, so a command
     * can be carried out where there is no window to select in -- the
     * info window's icon (AW-076). */
    const char *only_path;
} tiku_trk_folder_file_actions_t;

/** @brief Bind filesystem, clipboard, dialog, and shell-close services. */
void tiku_trk_folder_file_actions_init(
    tiku_trk_folder_file_actions_t *controller, tiku_fs_t *fs,
    tiku_backend_t *backend,
    tiku_trk_clip_t *clip, tiku_trk_drag_t *pending,
    char *drop_destination, tiku_alert_t *alert,
    tiku_trk_folder_prefs_fn open_prefs, void *prefs_context,
    char *status, size_t status_size,
    tiku_trk_folder_close_under_fn close_under, void *close_context);

/** @brief Collect selected paths into stable caller-owned storage. */
int tiku_trk_folder_file_actions_selection(
    const tiku_trk_folder_window_t *folder,
    char paths[][TIKU_PATH_MAX], const char **selection, int maximum);

/** @brief What the three-way delete question can be answered with. */
typedef enum {
    TIKU_TRK_DELETE_ANSWER_CANCEL = 0,
    TIKU_TRK_DELETE_ANSWER_TRASH,
    TIKU_TRK_DELETE_ANSWER_DELETE
} tiku_trk_delete_answer_t;

/**
 * @brief Carry out the delete the question was asked about (FS-046).
 *
 * The selection is read again rather than copied at the point of asking:
 * the question is modal, so nothing can have changed it, and a copy would
 * be a second list to keep true.
 */
/** @brief The Empty Trash question said yes. */
void tiku_trk_folder_file_actions_answer_empty(
    tiku_trk_folder_file_actions_t *controller);

/** @brief The delete question answered: Trash, outright, or nothing. */
void tiku_trk_folder_file_actions_answer_delete(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, tiku_trk_delete_answer_t answer);

/**
 * @brief Start whatever waited for the engine to go idle (FS-055).
 *
 * Called from the shell's step, where the engine is pumped.  Does nothing
 * while it is busy and nothing when there is nothing waiting.
 *
 * @return 1 when a waiting job was started.
 */
int tiku_trk_folder_file_actions_pump(
    tiku_trk_folder_file_actions_t *controller);

/**
 * @brief Apply a command to ONE named path (AW-076).
 *
 * The same commands the menu carries out on a window's selection, for a
 * caller that has a path and no window: the selection every action reads
 * answers with this path while it runs.
 *
 * @return whether the command was handled.
 */
int tiku_trk_folder_file_actions_invoke_path(
    tiku_trk_folder_file_actions_t *controller, const char *path,
    int command);

/** @brief Apply a filesystem-facing command, returning whether handled. */
int tiku_trk_folder_file_actions_invoke(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, int command);

#endif /* TIKU_TRK_FOLDER_FILE_ACTIONS_H_ */
