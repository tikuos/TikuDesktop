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
 * Its own name where it has one, sanitised into a path component so a
 * board answering to "my/board" cannot mount somewhere nobody asked for.
 * The name is user-settable and so weak: a clash takes a number, and
 * durable state keys on the devid, not on this, so renaming a board
 * keeps the arrangement of its windows.
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
 * worth showing, and the shell is not a device tool.  A board named by
 * @p port goes at @p at when that is given, so a namespace a script
 * was written against can be laid over a real board.
 *
 * @return how many were mounted.
 */
int
mount_devices(desktop_t *app, const char *port, int baud, const char *at)
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
        if (at != NULL && at[0] == '/' && port != NULL && port[0] != '\0') {
            snprintf(prefix, sizeof prefix, "%s", at);
        } else {
            device_mount_name(dev, app->backend, name, sizeof name);
            snprintf(prefix, sizeof prefix, "/devices/%s", name);
        }
        if (tiku_mount_add(app->backend, prefix, dev, 1) != 0) {
            tiku_backend_close(dev);
            continue;
        }
        mounted++;
    }
    return mounted;
}

/**
 * @brief The notice pump, every 400 ms: what the stores report changed, and
 * which windows that reaches.
 */
void
desktop_notice_pump(desktop_t *app, desktop_loop_t *lp)
{
    int i;

    if (tiku_trk_now() - lp->last_pump > 400000) {
        int changed = 0;
        {
            /* What the leaf menu offers above the Deskbar folder.
             * Refreshed on the tick rather than read by the bar: they
             * are the same lists the Open panel keeps (DB-002). */
            static const tiku_trk_recent_t kWhich[] = {
                TIKU_TRK_RECENT_DOCS, TIKU_TRK_RECENT_FOLDERS,
                TIKU_TRK_RECENT_APPS
            };
            char paths[TIKU_TRK_DB_RECENT_MAX][TIKU_PATH_MAX];
            int k;

            for (k = 0; k < 3; k++) {
                int nr = tiku_trk_recent_list(app->store, kWhich[k],
                             TIKU_TRK_DB_RECENT_MAX, paths,
                             TIKU_TRK_DB_RECENT_MAX);

                tiku_trk_deskbar_set_recent(app->deskbar,
                    (tiku_trk_db_recent_t)k, paths, nr);
            }
        }
        /*
         * Drained ONCE for the whole desktop: reading the device's ring
         * takes the records out of it, so a per-window drain would let
         * the first window consume everyone else's and leave them told
         * that nothing happened.
         */
        tiku_model_change_t ch[32];
        int nch = -1, complete = 1;

        if (app->backend->ops->changes != NULL) {
            nch = app->backend->ops->changes(app->backend, ch, 32,
                                            &complete);
        }
        for (i = 0; i < app->folder_count; i++) {
            folder_t *fl = &app->folders[i];
            unsigned nf;

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            changed += tiku_trk_poseview_pump(fl->view, 5);
            /* Membership changes go through the notice handler rather
             * than a reload: a re-read would rebuild every row and take
             * the selection, the scroll position and the arrangement
             * with it. */
            nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(),
                                        ch, nch, complete);
            if ((nf & TIKU_TRK_NOTIFY_GONE) != 0u) {
                /* The folder is gone.  Its window goes with it, and its
                 * position is deliberately NOT saved -- there is nothing
                 * left for it to be the position of, and the teardown
                 * sweep must not write one either. */
                fl->gone = 1;
                tiku_workspace_close(app->desk, fl->win);
                changed++;
            } else if (nf != TIKU_TRK_NOTIFY_NOTHING) {
                if ((nf & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
                    tiku_trk_folder_title_for(prefs_model(app),
                        tiku_trk_poseview_path(fl->view),
                        fl->win->title, sizeof fl->win->title);
                    snprintf(fl->path, sizeof fl->path, "%s",
                             tiku_trk_poseview_path(fl->view));
                }
                changed++;
            }
        }
        changed += tiku_trk_desktop_view_poll(&app->desktop_view,
            tiku_trk_now(), ch, nch, complete);
        changed += tiku_trk_panel_window_poll(&app->file_panel,
            tiku_trk_now(), ch, nch, complete);
        changed += panels_poll(app, ch, nch);
        {   /* What has been mounted or unmounted since the last look.
             * Polled with everything else rather than on a timer of its
             * own: a volume arriving is a change like any other. */
            tiku_volchange_t vc[TIKU_VOLWATCH_MAX];
            int nv = tiku_volwatch_poll(&app->volwatch, vc,
                                            TIKU_VOLWATCH_MAX);
            int k;

            for (k = 0; k < nv; k++) {
                if (vc[k].renamed) {
                    /* Only the label moved: the union rebuild re-reads
                     * every volume's name, and the relist refreshes
                     * open listings (MA-021). */
                    tiku_trk_desktop_view_rebuild(&app->desktop_view);
                    changed++;
                    continue;
                }
                if (vc[k].mounted) {
                    /* A folder window showing the parent finds it by
                     * re-listing, which the poll above already did.
                     * The Desktop union gains the volume's Desktop
                     * folder as a source, live (PVN-040, PVN-051). */
                    tiku_trk_desktop_view_rebuild(&app->desktop_view);
                    /* And half a second on, broken links get another
                     * look: by then a link to the new volume resolves
                     * as a volume (PVN-042). */
                    (void)tiku_trk_run_later(app->loop,
                        refresh_after_mount, app, NULL, 500000);
                    changed++;
                    continue;
                }
                /* And loses it: a source that has gone must not be
                 * polled for. */
                tiku_trk_desktop_view_rebuild(&app->desktop_view);
                /* Unmounted.  A window ON the volume closes -- and its
                 * arrangement is deliberately NOT saved, because the
                 * disk it belongs to has gone; a window merely SHOWING
                 * rows from it drops those rows (PVN-043). */
                for (i = 0; i < app->folder_count; i++) {
                    folder_t *fl = &app->folders[i];

                    if (fl->win == NULL || !fl->win->open) {
                        continue;
                    }
                    if (tiku_trk_poseview_is_query(fl->view) &&
                        tiku_query_targets_device(
                            fl->run, (uint64_t)vc[k].v.dev)) {
                        /* The run is the query's subscription.  Stop it
                         * with the window so no watch survives a target
                         * volume disappearing (Q-046). */
                        tiku_query_stop(fl->run);
                        fl->run = NULL;
                        fl->live = 0;
                        fl->gone = 1;
                        tiku_workspace_close(app->desk, fl->win);
                        changed++;
                    } else if (strncmp(fl->path, vc[k].v.mount,
                                strlen(vc[k].v.mount)) == 0) {
                        fl->gone = 1;
                        tiku_workspace_close(app->desk, fl->win);
                        changed++;
                    } else if (tiku_trk_poseview_device_gone(fl->view,
                                   (uint64_t)vc[k].v.dev) > 0) {
                        changed++;
                    }
                }
                changed += tiku_trk_panel_window_device_gone(
                    &app->file_panel, vc[k].v.mount);
                changed += tiku_trk_info_windows_device_gone(
                    &app->info_windows, (uint64_t)vc[k].v.dev);
            }
        }
        changed += tiku_trk_info_windows_refresh(&app->info_windows,
                                                  tiku_trk_now());
        changed += tiku_trk_deskbar_refresh(app->deskbar);
        /* Live queries re-evaluate on a slower beat than values are
         * read: a re-walk costs a directory listing per subtree, and a
         * result set that moves every 400 ms is unreadable anyway. */
        if (tiku_trk_now() - lp->last_requery > 3000000) {
            for (i = 0; i < app->folder_count; i++) {
                if (app->folders[i].win != NULL &&
                    app->folders[i].win->open) {
                    changed += refresh_results(app, &app->folders[i]);
                    if (app->folders[i].trash_union) {
                        changed += trash_union_refresh(app,
                                                       &app->folders[i]);
                    }
                }
            }
            changed += tiku_trk_desktop_view_trash_check(
                &app->desktop_view);
            lp->last_requery = tiku_trk_now();
        }
        lp->last_pump = tiku_trk_now();
        if (changed > 0) {
            app->dirty = 1;
        }
    }
}

/**
 * @brief The minute sweep: every window re-lists, so a store that reports
 * nothing still catches up.
 */
void
desktop_sweep(desktop_t *app, desktop_loop_t *lp)
{
    int i;

    if (tiku_trk_now() - lp->last_sweep > 60000000) {
        /* The reaping of searches nobody named.  On a slow tick of its
         * own and skipped for the first half hour, because it is the
         * least urgent work there is (Q-037, AW-090). */
        const char *open[TIKU_WINDOW_MAX];
        int nopen = 0;

        for (i = 0; i < app->folder_count; i++) {
            if (app->folders[i].qname[0] != '\0' &&
                app->folders[i].win != NULL && app->folders[i].win->open) {
                open[nopen++] = app->folders[i].qname;
            }
        }
        if (!tiku_trk_task_system_busy()) {
            /* Deleting user data is idle-only work twice over: the
             * half-hour hold-off AND the machine's own load gate
             * (AW-090). */
            (void)tiku_query_sweep(app->store, (int64_t)time(NULL),
                                       (tiku_trk_now() - lp->started_at) /
                                           1000000, open, nopen);
        }
        lp->last_sweep = tiku_trk_now();
    }
}

/**
 * @brief One pump for every window and the tray: a device is read once per turn
 * however many views it has.
 */
void
desktop_device_pump(desktop_t *app)
{
    int i;

    /* One pump serves every window and the tray, so a device is read
     * once per cycle however many windows are showing it. */
    for (i = 0; i < app->folder_count; i++) {
        /* One batch per window per turn: a folder still arriving keeps
         * the desktop drawing rather than holding the loop. */
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            tiku_trk_poseview_loading(app->folders[i].view)) {
            (void)tiku_trk_poseview_load_step(app->folders[i].view,
                                              tiku_trk_now());
            app->dirty = 1;
        }
    }
}
