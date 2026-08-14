/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_file_actions.c - filesystem-facing folder commands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_shortcuts.h"

void
tiku_trk_folder_file_actions_init(
    tiku_trk_folder_file_actions_t *controller, tiku_trk_fs_t *fs,
    tiku_trk_clip_t *clip, tiku_trk_drag_t *pending,
    char *drop_destination, tiku_trk_alert_t *alert,
    tiku_trk_prefs_window_t *preferences, char *status, size_t status_size,
    tiku_trk_folder_close_under_fn close_under, void *close_context)
{
    if (controller == NULL) {
        return;
    }
    *controller = (tiku_trk_folder_file_actions_t){
        .fs = fs,
        .clip = clip,
        .pending = pending,
        .drop_destination = drop_destination,
        .alert = alert,
        .preferences = preferences,
        .status = status,
        .status_size = status_size,
        .close_under = close_under,
        .close_context = close_context
    };
}

int
tiku_trk_folder_file_actions_selection(
    const tiku_trk_folder_window_t *folder,
    char paths[][TIKU_TRK_PATH_MAX], const char **selection, int maximum)
{
    int i, count, selected = 0;

    if (folder == NULL || folder->view == NULL) {
        return 0;
    }
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count && selected < maximum; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose == NULL || !pose->selected) {
            continue;
        }
        snprintf(paths[selected], TIKU_TRK_PATH_MAX, "%s",
                 pose->model.path);
        selection[selected] = paths[selected];
        selected++;
    }
    return selected;
}

static void
close_selected(tiku_trk_folder_file_actions_t *controller,
               const char *const *selection, int count)
{
    int i;

    if (controller->close_under == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        controller->close_under(controller->close_context, selection[i]);
    }
}

static void
delete_selection(tiku_trk_folder_file_actions_t *controller,
                 tiku_trk_folder_window_t *folder, int command)
{
    char paths[64][TIKU_TRK_PATH_MAX];
    const char *selection[64];
    int count = tiku_trk_folder_file_actions_selection(folder, paths,
                                                        selection, 64);
    tiku_trk_delete_q_t question;

    if (count == 0) {
        return;
    }
    question = tiku_trk_fs_delete_question(controller->fs, selection[0],
                                            command == TIKU_TRK_CMD_DELETE);
    if (question == TIKU_TRK_DELETE_STRAIGHT) {
        snprintf(controller->status, controller->status_size,
                 "deleting %d item%s -- this cannot be undone", count,
                 count == 1 ? "" : "s");
        (void)tiku_trk_fs_delete(controller->fs, selection, count);
        close_selected(controller, selection, count);
        return;
    }
    snprintf(controller->status, controller->status_size,
             "moving %d item%s to the Trash", count,
             count == 1 ? "" : "s");
    {
        int i;

        for (i = 0; i < count; i++) {
            tiku_trk_confirm_t confirmation =
                tiku_trk_fs_confirm_move(selection[i]);

            if (confirmation == TIKU_TRK_CONFIRM_OVERRIDE &&
                command != TIKU_TRK_CMD_DELETE) {
                char message[300];

                snprintf(message, sizeof message,
                    "\"%.60s\" is a system folder.  Hold Shift and choose "
                    "Move to confirm.", selection[i]);
                tiku_trk_alert_open(controller->alert, TIKU_TRK_ALERT_STOP,
                    TIKU_TRK_ALERT_TAG_REFUSAL, message, "Cancel");
                return;
            }
            if (confirmation == TIKU_TRK_CONFIRM_ASK) {
                snprintf(controller->status, controller->status_size,
                    "'%.60s' holds settings -- moving it anyway",
                    selection[i]);
            }
        }
    }
    (void)tiku_trk_fs_trash(controller->fs, selection, count);
    close_selected(controller, selection, count);
}

static void
restore_selection(tiku_trk_folder_file_actions_t *controller,
                  tiku_trk_folder_window_t *folder)
{
    char paths[64][TIKU_TRK_PATH_MAX], error[200];
    const char *selection[64];
    int count = tiku_trk_folder_file_actions_selection(folder, paths,
                                                        selection, 64);
    int restored;

    error[0] = '\0';
    restored = tiku_trk_fs_restore(controller->fs, selection, count,
                                   error, sizeof error);
    if (restored > 0) {
        snprintf(controller->status, controller->status_size,
                 "put %d item%s back", restored,
                 restored == 1 ? "" : "s");
    } else if (error[0] != '\0') {
        snprintf(controller->status, controller->status_size, "%s", error);
    }
}

static void
copy_selection(tiku_trk_folder_file_actions_t *controller,
               tiku_trk_folder_window_t *folder, int command)
{
    tiku_trk_model_t selection[64];
    int i, selected = 0, count = tiku_trk_poseview_count(folder->view);
    int cut = command == TIKU_TRK_CMD_CUT ||
              command == TIKU_TRK_CMD_CUT_MORE;
    int more = command == TIKU_TRK_CMD_CUT_MORE ||
               command == TIKU_TRK_CMD_COPY_MORE;

    for (i = 0; i < count && selected < 64; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose != NULL && pose->selected) {
            selection[selected++] = pose->model;
        }
    }
    (void)tiku_trk_clip_add(controller->clip, selection, selected,
        cut ? TIKU_TRK_CLIP_MOVE : TIKU_TRK_CLIP_COPY, !more);
    snprintf(controller->status, controller->status_size,
             "%d item%s on the clipboard",
             tiku_trk_clip_count(controller->clip),
             tiku_trk_clip_count(controller->clip) == 1 ? "" : "s");
}

static void
finish_drop(tiku_trk_folder_file_actions_t *controller, int command)
{
    const char *selection[TIKU_TRK_DRAG_MAX];
    int i, count = 0;

    for (i = 0; i < controller->pending->count; i++) {
        selection[count++] = controller->pending->path[i];
    }
    if (count == 0 || controller->drop_destination[0] == '\0') {
        return;
    }
    if (command == TIKU_TRK_CMD_COPY_HERE) {
        (void)tiku_trk_fs_copy(controller->fs, selection, count,
            controller->drop_destination, TIKU_TRK_CONFLICT_ASK);
        snprintf(controller->status, controller->status_size,
                 "copying %d item%s", count, count == 1 ? "" : "s");
    } else if (command == TIKU_TRK_CMD_MOVE_HERE) {
        (void)tiku_trk_fs_move(controller->fs, selection, count,
            controller->drop_destination, TIKU_TRK_CONFLICT_ASK);
        snprintf(controller->status, controller->status_size,
                 "moving %d item%s", count, count == 1 ? "" : "s");
    } else {
        (void)tiku_trk_fs_link(controller->fs, selection, count,
            controller->drop_destination,
            command == TIKU_TRK_CMD_REL_LINK_HERE);
        snprintf(controller->status, controller->status_size,
                 "linking %d item%s", count, count == 1 ? "" : "s");
    }
    tiku_trk_drag_reset(controller->pending);
    controller->drop_destination[0] = '\0';
}

static void
new_folder(tiku_trk_folder_file_actions_t *controller,
           tiku_trk_folder_window_t *folder)
{
    char made[TIKU_TRK_PATH_MAX], error[200];

    error[0] = '\0';
    if (controller->fs != NULL &&
        tiku_trk_fs_new_folder(controller->fs, folder->path, made,
                               sizeof made, error, sizeof error) == 0) {
        const char *slash = strrchr(made, '/');

        snprintf(controller->status, controller->status_size,
                 "created '%.60s'", slash != NULL ? slash + 1 : made);
    } else {
        snprintf(controller->status, controller->status_size, "%.150s",
                 error[0] != '\0' ? error : "could not create a new folder");
    }
}

static void
paste(tiku_trk_folder_file_actions_t *controller,
      tiku_trk_folder_window_t *folder, int command)
{
    char paths[64][TIKU_TRK_PATH_MAX], here[64][TIKU_TRK_PATH_MAX];
    const char *selection[64], *same[64];
    int i, count = 0, same_count = 0, moving = 0;
    int links = command == TIKU_TRK_CMD_PASTE_LINKS;

    (void)tiku_trk_clip_prune(controller->clip);
    for (i = 0; i < tiku_trk_clip_count(controller->clip) && count < 64;
         i++) {
        const tiku_trk_clip_entry_t *entry =
            tiku_trk_clip_at(controller->clip, i);
        const char *slash = strrchr(entry->path, '/');
        size_t parent_length = slash != NULL
                             ? (size_t)(slash - entry->path) : 0u;

        if (!links && slash != NULL && strlen(folder->path) == parent_length &&
            strncmp(folder->path, entry->path, parent_length) == 0) {
            if (entry->mode != TIKU_TRK_CLIP_MOVE && same_count < 64) {
                snprintf(here[same_count], sizeof here[same_count], "%s",
                         entry->path);
                same[same_count] = here[same_count];
                same_count++;
            }
            continue;
        }
        snprintf(paths[count], sizeof paths[count], "%s", entry->path);
        selection[count] = paths[count];
        if (entry->mode == TIKU_TRK_CLIP_MOVE) {
            moving = 1;
        }
        count++;
    }
    if (same_count > 0) {
        (void)tiku_trk_fs_duplicate(controller->fs, same, same_count);
        snprintf(controller->status, controller->status_size,
                 "duplicating %d item%s already here", same_count,
                 same_count == 1 ? "" : "s");
        if (count == 0) {
            return;
        }
    }
    if (count == 0) {
        snprintf(controller->status, controller->status_size,
                 "the clipboard is empty");
        return;
    }
    if (tiku_trk_fs_in_trash(controller->fs, folder->path) &&
        (links || !moving)) {
        snprintf(controller->status, controller->status_size, "%s",
                 links ? "Sorry, you can't create links in the Trash."
                       : "Sorry, you can't copy items to the Trash.");
        return;
    }
    if (links) {
        (void)tiku_trk_fs_link(controller->fs, selection, count,
                               folder->path, 0);
        snprintf(controller->status, controller->status_size,
                 "linking %d item%s here", count,
                 count == 1 ? "" : "s");
        return;
    }
    if (moving) {
        (void)tiku_trk_fs_move(controller->fs, selection, count,
                               folder->path, TIKU_TRK_CONFLICT_ASK);
        tiku_trk_clip_clear(controller->clip);
    } else {
        (void)tiku_trk_fs_copy(controller->fs, selection, count,
                               folder->path, TIKU_TRK_CONFLICT_ASK);
    }
    snprintf(controller->status, controller->status_size,
             "%s %d item%s here", moving ? "moving" : "copying", count,
             count == 1 ? "" : "s");
}

int
tiku_trk_folder_file_actions_invoke(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, int command)
{
    if (controller == NULL || folder == NULL || folder->view == NULL) {
        return 0;
    }
    switch (command) {
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
    case TIKU_TRK_CMD_DELETE:
        delete_selection(controller, folder, command);
        return 1;
    case TIKU_TRK_CMD_RESTORE:
        restore_selection(controller, folder);
        return 1;
    case TIKU_TRK_CMD_EMPTY_TRASH: {
        int count = tiku_trk_fs_empty_trash(controller->fs);

        snprintf(controller->status, controller->status_size,
                 count > 0 ? "emptying the Trash: %d item%s"
                           : "the Trash is empty",
                 count, count == 1 ? "" : "s");
        return 1;
    }
    case TIKU_TRK_CMD_CUT:
    case TIKU_TRK_CMD_CUT_MORE:
    case TIKU_TRK_CMD_COPY:
    case TIKU_TRK_CMD_COPY_MORE:
        copy_selection(controller, folder, command);
        return 1;
    case TIKU_TRK_CMD_MOVE_HERE:
    case TIKU_TRK_CMD_COPY_HERE:
    case TIKU_TRK_CMD_LINK_HERE:
    case TIKU_TRK_CMD_REL_LINK_HERE:
        finish_drop(controller, command);
        return 1;
    case TIKU_TRK_CMD_PREFERENCES:
        tiku_trk_prefs_window_open(controller->preferences);
        return 1;
    case TIKU_TRK_CMD_NEW_FOLDER:
        new_folder(controller, folder);
        return 1;
    case TIKU_TRK_CMD_PASTE:
    case TIKU_TRK_CMD_PASTE_LINKS:
        paste(controller, folder, command);
        return 1;
    default:
        return 0;
    }
}
