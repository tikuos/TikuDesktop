/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_spatial.c - positions that ride on the objects: saved
 * and restored per pose
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/**
 * @brief Where this view's OWN record lives (MA-064's three slots).
 *
 * Distinct ATTRIBUTE names on real nodes: the Desktop's arrangement sits
 * on the Desktop directory under its own name, and the root listing --
 * this port's Disks window -- keeps its state on the Desktop directory
 * too, exactly as the original redirects it, because the root itself is
 * nowhere the store can write.
 */
const char *
pv_state_node(const tiku_trk_poseview_t *v, char *buf, size_t max)
{
    if (v->state_key[0] == '\0' && strcmp(v->path, "/") == 0) {
        return tiku_desktop_dir(buf, max);
    }
    return v->path;
}

/**
 * @brief The attribute the view's state is kept under: the Desktop's, the
 *        Disks root's, or the ordinary folder one.
 */
static const char *
state_attr(const tiku_trk_poseview_t *v)
{
    if (v->state_key[0] != '\0') {
        return TIKU_ATTR_DESK_VIEW_STATE;
    }
    if (strcmp(v->path, "/") == 0) {
        return TIKU_ATTR_DISKS_VIEW_STATE;
    }
    return TIKU_ATTR_VIEW_STATE;
}

void
tiku_trk_poseview_set_no_autoscroll(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->no_autoscroll = on;
    }
}

void
tiku_trk_poseview_set_state_slot(tiku_trk_poseview_t *v, const char *key)
{
    if (v != NULL) {
        snprintf(v->state_key, sizeof v->state_key, "%s",
                 key != NULL ? key : "");
    }
}

int
tiku_trk_poseview_last_icon_mode(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->last_icon : TIKU_TRK_MODE_ICON;
}

int
tiku_trk_poseview_restore_state(tiku_trk_poseview_t *v)
{
    tiku_store_t *store;
    int i, n = 0;

    if (v == NULL || v->backend->ops->state_store == NULL) {
        return 0;
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL) {
        return 0;
    }
    /* View state belongs to the FOLDER; pose info to each ENTRY. */
    {
        char rec[96];
        char node[TIKU_PATH_MAX];
        int len = tiku_state_read(store,
                                      pv_state_node(v, node, sizeof node),
                                      state_attr(v), rec, sizeof rec - 1);
        if (len > 0) {
            int px = 0;

            rec[len] = '\0';
            /* A mode nothing recognises takes the whole record with it
             * (MA-063).  The numbers beside it were written by whatever
             * wrote the name, so a size and a sort key out of a record
             * that garbled are no more trustworthy than the name was. */
            if (strncmp(rec, "icon", 4) == 0) { v->mode = TIKU_TRK_MODE_ICON; }
            else if (strncmp(rec, "mini", 4) == 0) { v->mode = TIKU_TRK_MODE_MINI; }
            else if (strncmp(rec, "list", 4) == 0) { v->mode = TIKU_TRK_MODE_LIST; }
            else { return 0; }
            /* IV-014: the size comes back with the mode.  A record written
             * before sizes were saved simply has no number, and the
             * constructor's size stands. */
            {
                int sc = 0, ix = 0, iy = 0, mx = 0, my = 0;
                int s1 = -1, s2 = -1, rev = 0, li = -1;
                int got = sscanf(rec, "%*s %d %d %d %d %d %d %d %d %d %d",
                                 &px, &sc, &ix, &iy, &mx, &my, &s1, &s2,
                                 &rev, &li);

                if (got >= 1) {
                    /* Pulled into the ladder rather than dropped for the
                     * default: a folder left as large as it could be asked
                     * for reopens as large as it can be, and a size below
                     * the bottom is a size, not an instruction to forget
                     * one (MA-063). */
                    v->icon_size = px < 16 ? 16 : (px > 128 ? 128 : px);
                }
                if (got >= 6) {
                    /* Clamped when the view is next given a viewport: a
                     * record written on a bigger screen would otherwise
                     * open the folder looking empty (PVL-053). */
                    /* A damaged state file must not strand a folder beyond
                     * its content.  List scroll is a non-negative row
                     * offset; spatial origins may be negative while a
                     * large icon is being centred, but are still bounded. */
                    v->scroll = pv_sane_scroll_origin(sc, 0);
                    v->icon_x = pv_sane_scroll_origin(ix, 1);
                    v->icon_y = pv_sane_scroll_origin(iy, 1);
                    v->mini_x = pv_sane_scroll_origin(mx, 1);
                    v->mini_y = pv_sane_scroll_origin(my, 1);
                }
                if (got >= 10) {
                    /* Which icon grid the stored positions belong to; a
                     * record from before the word simply leaves the
                     * default standing (MA-061). */
                    v->last_icon = (li == 1) ? TIKU_TRK_MODE_MINI
                                             : TIKU_TRK_MODE_ICON;
                }
                if (got >= 9 && s1 >= 0 && s1 < TIKU_TRK_COL_KINDS) {
                    /* A record from before the keys were written has no
                     * numbers here, and the constructor's own key stands. */
                    v->sort_field = s1;
                    v->sort2_field = (s2 >= 0 && s2 < TIKU_TRK_COL_KINDS)
                                         ? s2 : -1;
                    v->sort_reverse = rev ? 1 : 0;
                    pv_resort(v);
                }
            }
        }
    }
    for (i = 0; i < v->count; i++) {
        pose_info_t info;
        char raw[sizeof info + 4];
        int got;

        memset(&info, 0, sizeof info);
        got = tiku_state_read(store, v->poses[i].model.path,
                                  TIKU_ATTR_POSE_INFO, raw, sizeof raw);
        if (got == (int)sizeof info) {
            memcpy(&info, raw, sizeof info);
        } else if (got == (int)sizeof(pose_info_v1_t)) {
            /* Written before the flags existed: the position is good and
             * nothing was hidden. */
            pose_info_v1_t old_info;

            memcpy(&old_info, raw, sizeof old_info);
            info.x = old_info.x;
            info.y = old_info.y;
        } else {
            /* Any other length is not this record.  Reading it anyway would
             * take whatever the first bytes happened to be and call it a
             * position. */
            continue;
        }
        /* A nonsense position is ignored and the icon auto-placed instead
         * (PVL: |x|,|y| > 50000 is not a location). */
        if (info.x > TIKU_TRK_SANE_LOCATION || info.x < -TIKU_TRK_SANE_LOCATION ||
            info.y > TIKU_TRK_SANE_LOCATION || info.y < -TIKU_TRK_SANE_LOCATION) {
            continue;
        }
        v->poses[i].invisible = (info.flags & POSE_INFO_INVISIBLE) ? 1 : 0;
        v->poses[i].loc_x = info.x;
        v->poses[i].loc_y = info.y;
        v->poses[i].placed = 1;
        v->poses[i].auto_placed = 0;
        n++;
    }
    /* The invisible bit only becomes known once the saved records have been
     * read, so the listing is filtered here rather than at collect time. */
    pv_apply_visibility(v);
    v->state_dirty = 0;
    return n;
}

int
tiku_trk_poseview_save_state(tiku_trk_poseview_t *v)
{
    tiku_store_t *store;
    int i, n = 0;

    if (v == NULL || v->is_query || !v->state_dirty ||
        v->backend->ops->state_store == NULL) {
        return 0;               /* the dirty gate: no write, no wear */
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return 0;               /* read-only: degrade, do not fail */
    }
    {
        /* IV-014: the SIZE goes with the mode.  Written without it, a folder
         * arranged at 64 px reopens at 32 and every icon moves. */
        const char *mode = (v->mode == TIKU_TRK_MODE_ICON) ? "icon"
                         : (v->mode == TIKU_TRK_MODE_MINI) ? "mini" : "list";
        char rec[96];

        /* Both origins go with the mode and the size: a folder scrolled
         * somewhere and closed reopens where it was, and the list's row
         * and the icon view's pixels are different numbers that must not
         * be confused for one another (MA-061). */
        /* The sort keys go with the rest: a folder arranged by size and
         * closed came back by name, which is the arrangement lost in the
         * one place a user would look for it (MA-061, PVL-041). */
        snprintf(rec, sizeof rec, "%s %d %d %d %d %d %d %d %d %d %d", mode,
                 v->icon_size, v->scroll, v->icon_x, v->icon_y, v->mini_x,
                 v->mini_y, v->sort_field, v->sort2_field,
                 v->sort_reverse,
                 (v->last_icon == TIKU_TRK_MODE_MINI) ? 1 : 0);
        char node[TIKU_PATH_MAX];

        (void)tiku_state_write(store,
                                   pv_state_node(v, node, sizeof node),
                                   state_attr(v), rec, strlen(rec));
    }
    for (i = 0; i < v->count; i++) {
        pose_info_t info;

        /* An auto-placed icon is written too: it IS where the icon is, and
         * dropping it makes a folder re-place its contents on every open,
         * so nothing ever settles. */
        if (!v->poses[i].placed) {
            continue;
        }
        memset(&info, 0, sizeof info);
        info.x = (int32_t)v->poses[i].loc_x;
        info.y = (int32_t)v->poses[i].loc_y;
        if (v->poses[i].invisible) {
            info.flags |= POSE_INFO_INVISIBLE;
        }
        if (tiku_state_write(store, v->poses[i].model.path,
                                 TIKU_ATTR_POSE_INFO, &info,
                                 sizeof info) == 0) {
            n++;
        }
    }
    (void)tiku_store_flush(store);
    v->state_dirty = 0;
    return n;
}
