/* tiku_desktop_volume_commands.c -- volume mount and unmount commands.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_desktop_private.h"

/**
 * @brief unmounting the folder's volume, refused out loud when it cannot be.
 * @return 1 when this was the command
 */
int
desktop_cmd_unmount(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_UNMOUNT) {
        /* The selection names the volume; with none, the volume ROOT the
         * window is showing is the one the gesture means (CW-034).
         * Everything open on it saves itself first (AW-096), and a
         * refusal or failure is said out loud. */
        char upaths[1][TIKU_PATH_MAX];
        const char *usel[1];
        const char *upath =
            (tiku_trk_folder_file_actions_selection(fl, upaths, usel, 1) > 0)
                ? usel[0] : fl->path;
        tiku_volumes_t uvols;
        char uwhy[200];

        uwhy[0] = '\0';
        if (tiku_volumes_scan(&uvols) > 0 &&
            tiku_volume_is_root(&uvols, upath)) {
                        char umount_at[TIKU_PATH_MAX], uname[128];

            /* Asked and saved here; the umount itself, which flushes
             * what is dirty, goes to a worker and reports back. */
            if (tiku_volume_unmount_prepare(&uvols, upath,
                                            save_before_unmount, app,
                                            umount_at, sizeof umount_at,
                                            uname, sizeof uname,
                                            uwhy, sizeof uwhy) == 0) {
                (void)desktop_wire_unmount(app, umount_at, uname);
            } else if (uwhy[0] != '\0') {
                tiku_sound_play(TIKU_SOUND_BEEP);
                tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                                    TIKU_TRK_ALERT_TAG_REFUSAL, uwhy,
                                    "Cancel");
            }
        } else {
            /* Nothing here IS a volume: said, not swallowed (NeXT UIG
             * ch6). */
            snprintf(app->status, sizeof app->status,
                     "Unmount is not available here");
        }
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief the mount settings and mounting everything.  @return 1 when this was
 * the command
 */
int
desktop_cmd_mount(desktop_t *app, folder_t *fl, int command)
{
    (void)fl;
    if (command == TIKU_TRK_CMD_MOUNT_SETTINGS) {
        /* Straight to the Disk mount page: the one caller the jump-to-page
         * request has (AW-101, CW-034). */
        app->prefs_window.page = TIKU_TRK_PAGE_MOUNT;
        tiku_trk_prefs_window_open(&app->prefs_window);
        desktop_damage_all(app);
        return 1;
    }
    if (command == TIKU_TRK_CMD_MOUNT_ALL ||
        (command >= TIKU_TRK_CMD_MOUNT_PART_BASE &&
         command < TIKU_TRK_CMD_MOUNT_PART_BASE +
                       TIKU_TRK_MOUNT_PART_MAX)) {
        /* Asked of the system, and the answer -- on this host, that
         * mounting needs its leave -- is said out loud (CW-034). */
        char why[200];
        int failed = 0, tried = 0;

        why[0] = '\0';
        if (command == TIKU_TRK_CMD_MOUNT_ALL) {
            tiku_partition_t part[TIKU_TRK_MOUNT_PART_MAX];
            int pn = tiku_partitions_scan(part,
                                              TIKU_TRK_MOUNT_PART_MAX);
            int k;

            for (k = 0; k < pn; k++) {
                if (part[k].mounted) {
                    continue;
                }
                tried++;
                if (tiku_volume_mount(part[k].name, why,
                                          sizeof why) != 0) {
                    failed++;
                    break;
                }
            }
        } else {
            int idx = command - TIKU_TRK_CMD_MOUNT_PART_BASE;

            if (idx < app->menus.mount_parts) {
                tried++;
                failed = tiku_volume_mount(app->menus.mount_part[idx],
                                               why, sizeof why) != 0;
            }
        }
        if (failed && why[0] != '\0') {
            tiku_sound_play(TIKU_SOUND_BEEP);
            tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                                TIKU_TRK_ALERT_TAG_REFUSAL, why, "Cancel");
        } else if (tried == 0) {
            snprintf(app->status, sizeof app->status,
                     "every partition is already mounted");
        }
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}
