/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_replicants.c - the replicants: added, synced, stored,
 * removed
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/**
 * @brief Write the tray's replicants to the session store, one label and
 *        path per line; a read-only or absent store is left alone.
 */
static void
save_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_PATH_MAX + 20)];
    size_t at = 0;
    int i;

    if (db == NULL || db->store == NULL ||
        !tiku_store_writable(db->store)) {
        return;
    }
    data[0] = '\0';
    for (i = 0; i < db->tray_count; i++) {
        int n = snprintf(data + at, sizeof data - at, "%s\t%s\n",
                         db->tray[i].label, db->tray[i].path);
        if (n < 0 || (size_t)n >= sizeof data - at) {
            break;
        }
        at += (size_t)n;
    }
    (void)tiku_state_write(db->store, TIKU_KEY_SESSION,
                               TIKU_ATTR_REPLICANTS, data, at);
    (void)tiku_store_flush(db->store);
}

int
tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                               const char *path)
{
    tiku_trk_replicant_t *r;

    if (db == NULL || label == NULL || path == NULL ||
        db->tray_count >= TIKU_TRK_DB_TRAY_MAX) {
        return -1;
    }
    for (int i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return db->tray[i].id;         /* already subscribed */
        }
        if (strncmp(db->tray[i].label, label,
                    sizeof db->tray[i].label - 1u) == 0) {
            /* A DIFFERENT node wearing a taken name is refused, as the
             * original refuses duplicate view names (DB-038). */
            return -1;
        }
    }
    r = &db->tray[db->tray_count++];
    r->id = ++db->next_replicant;
    snprintf(r->label, sizeof r->label, "%s", label);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->valid = 0;
    r->height = TIKU_TRK_DB_MAX_REPLICANT_H;
    /* Ask the node what its reading MEANS, once, so the tray can show
     * "16h 26m" rather than 59182. */
    if (db->backend != NULL && db->backend->ops->stat != NULL) {
        tiku_model_t m;

        if (db->backend->ops->stat(db->backend, path, &m) == 0) {
            r->desc_known = tiku_desc_parse(m.facts.meta, &r->desc);
            r->node_id = m.node_id;
        }
    }
    save_replicants(db);
    return r->id;
}

int
tiku_trk_deskbar_sync_replicants(tiku_trk_deskbar_t *db)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL || db->backend->ops->stat == NULL) {
        return 0;
    }
    for (i = db->tray_count - 1; i >= 0; i--) {
        tiku_trk_replicant_t *r = &db->tray[i];
        tiku_model_t m;
        char moved[TIKU_PATH_MAX];

        if (db->backend->ops->stat(db->backend, r->path, &m) == 0) {
            continue;                   /* still where it was */
        }
        if (r->node_id != 0 && db->backend->ops->path_of_id != NULL &&
            db->backend->ops->path_of_id(db->backend, r->node_id, moved,
                                         sizeof moved) == 0) {
            /* Moved, not gone: re-pointed, keeping its place and its
             * label (DB-040). */
            snprintf(r->path, sizeof r->path, "%s", moved);
            changed = 1;
            continue;
        }
        (void)tiku_trk_deskbar_drop_replicant(db, r->id);
        changed = 1;
    }
    if (changed) {
        save_replicants(db);
    }
    return changed;
}

int
tiku_trk_deskbar_replicant_by_id(const tiku_trk_deskbar_t *db, int id)
{
    int i;

    if (db == NULL) {
        return -1;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (db->tray[i].id == id) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_replicant_by_name(const tiku_trk_deskbar_t *db,
                                   const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].label, name) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_drop_replicant(tiku_trk_deskbar_t *db, int id)
{
    int at = tiku_trk_deskbar_replicant_by_id(db, id);
    int i;

    if (at < 0) {
        return 0;
    }
    for (i = at; i < db->tray_count - 1; i++) {
        db->tray[i] = db->tray[i + 1];
    }
    db->tray_count--;
    save_replicants(db);
    return 1;
}

int
tiku_trk_deskbar_set_replicant_height(tiku_trk_deskbar_t *db,
                                      const char *path, int height)
{
    int i;

    if (db == NULL || path == NULL || height <= 0 ||
        height > TIKU_TRK_DB_MAX_REPLICANT_H) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            if (db->tray[i].height == height) {
                return 1;
            }
            db->tray[i].height = height;
            /* The fixed row is the tray's layout unit; changing the
             * accepted size still invalidates the whole tray immediately. */
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_replicant_height(const tiku_trk_deskbar_t *db,
                                  const char *path)
{
    int i;

    for (i = 0; db != NULL && path != NULL && i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return db->tray[i].height;
        }
    }
    return 0;
}

void
tiku_trk_deskbar_set_show_replicants(tiku_trk_deskbar_t *db, int show)
{
    if (db != NULL) {
        db->show_replicants = !!show;
    }
}

int
tiku_trk_deskbar_show_replicants(const tiku_trk_deskbar_t *db)
{
    return db != NULL ? db->show_replicants : 0;
}

void
tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db, tiku_store_t *store)
{
    if (db != NULL) {
        db->store = store;
    }
}

int
tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_PATH_MAX + 20)];
    int n, loaded = 0;
    char *line;

    if (db == NULL || db->store == NULL) {
        return 0;
    }
    n = tiku_state_read(db->store, TIKU_KEY_SESSION,
                            TIKU_ATTR_REPLICANTS, data, sizeof data - 1u);
    if (n <= 0) {
        return 0;
    }
    data[n] = '\0';
    line = data;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        char *tab;

        if (next != NULL) { *next++ = '\0'; }
        tab = strchr(line, '\t');
        if (tab != NULL) {
            *tab++ = '\0';
            if (tab[0] != '\0' && db->backend->ops->stat(db->backend, tab,
                                                          &(tiku_model_t){0}) == 0 &&
                tiku_trk_deskbar_add_replicant(db, line, tab) >= 0) {
                loaded++;
            }
        }
        line = next;
    }
    save_replicants(db); /* prune stale entries and normalise the record */
    return loaded;
}

int
tiku_trk_deskbar_remove_replicant(tiku_trk_deskbar_t *db, const char *path)
{
    int i;

    if (db == NULL || path == NULL) { return 0; }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            memmove(&db->tray[i], &db->tray[i + 1],
                    (size_t)(db->tray_count - i - 1) * sizeof db->tray[0]);
            db->tray_count--;
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_repoint_replicant(tiku_trk_deskbar_t *db,
                                   const char *old_path,
                                   const char *new_path)
{
    int i;

    if (db == NULL || old_path == NULL || new_path == NULL) { return 0; }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, old_path) == 0) {
            snprintf(db->tray[i].path, sizeof db->tray[i].path, "%s", new_path);
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        tiku_trk_replicant_t *r = &db->tray[i];
        char buf[64];
        int n = db->backend->ops->read(db->backend, r->path, buf,
                                       sizeof buf - 1);
        char *nl;

        if (n < 0) {
            changed += tiku_trk_deskbar_remove_replicant(db, r->path);
            i--;
            continue;
        }
        buf[n] = '\0';
        nl = strchr(buf, '\n');
        if (nl != NULL) { *nl = '\0'; }
        buf[sizeof r->value - 1u] = '\0';
        if (strcmp(r->value, buf) != 0) {
            memcpy(r->value, buf, strlen(buf) + 1u);
            changed++;
        }
        r->valid = 1;
    }
    return changed;
}
