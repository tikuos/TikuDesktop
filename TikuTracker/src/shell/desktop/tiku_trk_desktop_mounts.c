/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_mounts.c - devices as mounts: naming a board's namespace,
 * mounting it beside the local files, and what is saved and refreshed around
 * it
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief Re-resolve what a mount may have fixed, half a second on (PVN-042).
 *
 * Deliberately deferred so a link to the arriving volume resolves as a
 * volume rather than a plain directory.
 */
int
refresh_after_mount(void *ctx)
{
    desktop_t *app = ctx;
    int i;

    for (i = 0; i < app->folder_count; i++) {
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            app->folders[i].view != NULL) {
            (void)tiku_trk_poseview_reload(app->folders[i].view);
        }
    }
    tiku_trk_desktop_view_rebuild(&app->desktop_view);
    app->dirty = 1;
    return 1;
}

/**
 * @brief Save what is open on the leaving volume, first (AW-096).
 *
 * While the volume is still there to save to: positions flushed for every
 * window on it, and an active query holding the device open is closed so
 * it cannot block the unmount.
 */
void
save_before_unmount(dev_t dev, void *context)
{
    desktop_t *app = context;
    int i;

    for (i = 0; i < app->folder_count; i++) {
        folder_t *fl = &app->folders[i];

        if (fl->win == NULL || !fl->win->open || fl->view == NULL) {
            continue;
        }
        if (tiku_trk_poseview_is_query(fl->view) &&
            tiku_query_targets_device(fl->run, (uint64_t)dev)) {
            close_window(app, fl->win);
            continue;
        }
        if (tiku_trk_poseview_on_device(fl->view, (uint64_t)dev)) {
            (void)tiku_trk_poseview_save_state(fl->view);
        }
    }
    {
        folder_t *desk = desktop_folder(app);

        if (desk != NULL && desk->view != NULL &&
            tiku_trk_poseview_on_device(desk->view, (uint64_t)dev)) {
            (void)tiku_trk_poseview_save_state(desk->view);
        }
    }
}

/**
 * @brief What to call a board where the namespace can see it.
 *
 * Its own name where it has one, because that is what somebody set and
 * what they will look for.  Sanitised first: this becomes a PATH
 * COMPONENT, and a board answering to "my/board" would otherwise mount
 * somewhere nobody asked for.
 *
 * The name is user-settable and therefore weak -- two boards may answer
 * to the same one -- so a clash takes a number.  What durable state keys
 * on stays the devid, not this: renaming a board must not lose the
 * arrangement of its windows.
 */
static void
device_mount_name(const tiku_backend_t *dev, const tiku_backend_t *ns,
                  char *out, size_t max)
{
    const tiku_identity_t *id = tiku_backend_tiku_identity(dev);
    char base[64];
    size_t i, n = 0;
    const char *from;
    int tries;

    from = (id != NULL && id->name[0] != '\0') ? id->name : dev->devid;
    for (i = 0; from[i] != '\0' && n + 1u < sizeof base; i++) {
        unsigned char c = (unsigned char)from[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            base[n++] = (char)c;
        } else if (n > 0u && base[n - 1u] != '-') {
            base[n++] = '-';
        }
    }
    while (n > 0u && base[n - 1u] == '-') {
        n--;
    }
    base[n] = '\0';
    if (base[0] == '\0') {
        snprintf(base, sizeof base, "%s", "device");
    }
    for (tries = 1; tries < 100; tries++) {
        char prefix[TIKU_PATH_MAX];
        int slot, taken = 0;

        if (tries == 1) {
            snprintf(out, max, "%s", base);
        } else {
            snprintf(out, max, "%s-%d", base, tries);
        }
        snprintf(prefix, sizeof prefix, "/devices/%s", out);
        for (slot = 0; slot < tiku_mount_slots(ns); slot++) {
            const char *have = tiku_mount_prefix_at(ns, slot);

            if (have != NULL && strcmp(have, prefix) == 0) {
                taken = 1;
            }
        }
        if (!taken) {
            return;
        }
    }
}

/**
 * @brief Put every board that answers into the namespace under /devices.
 *
 * One backend per board, each at a name of its own, so two of them are
 * two places in one namespace rather than two desktops.  A board that
 * will not open is skipped rather than fatal: the others are still
 * worth showing, and the shell is not a device tool.
 *
 * @return how many were mounted.
 */
int
mount_devices(desktop_t *app, const char *port, int baud)
{
    char scan[TIKU_MOUNT_MAX][256];
    int found, i, mounted = 0;

    if (port != NULL && port[0] != '\0') {
        snprintf(scan[0], sizeof scan[0], "%s", port);
        found = 1;
    } else {
        found = tiku_tx_scan_serial(scan, TIKU_MOUNT_MAX);
    }
    for (i = 0; i < found && i < TIKU_MOUNT_MAX; i++) {
        tiku_backend_t *dev = tiku_backend_tiku_open(scan[i], baud);
        char name[64], prefix[TIKU_PATH_MAX];

        if (dev == NULL) {
            continue;
        }
        device_mount_name(dev, app->backend, name, sizeof name);
        snprintf(prefix, sizeof prefix, "/devices/%s", name);
        if (tiku_mount_add(app->backend, prefix, dev, 1) != 0) {
            tiku_backend_close(dev);
            continue;
        }
        mounted++;
    }
    return mounted;
}
