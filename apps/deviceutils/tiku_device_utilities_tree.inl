/* tiku_device_utilities_tree.inl -- part of tiku_device_utilities.c: the
 * inspector's folder tree, built from the folders read so far, and how a
 * read, a cached listing or the preload fills the tree and value rows.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Add @p node's row, then, when it is open, its children's rows in
 *         path order; at most U_ROWS rows. */
static void utility_tree_walk(struct device_utility *p, int node)
{
    if (p->tree_count >= U_ROWS) return;
    p->tree_rows[p->tree_count++] = node;
    if (!p->tree[node].expanded) return;
    int children[U_ROWS], count = 0;
    for (int i = 1; i < U_ROWS; i++)
        if (p->tree[i].path[0] && p->tree[i].parent == node) children[count++] = i;
    for (int i = 1; i < count; i++) {
        int child = children[i], j = i;
        while (j && strcmp(p->tree[children[j-1]].path, p->tree[child].path) > 0) {
            children[j] = children[j-1]; j--;
        }
        children[j] = child;
    }
    for (int i = 0; i < count; i++) utility_tree_walk(p, children[i]);
}

/** @brief Rebuild the tree's rows; the selection moves to its nearest
 *         shown ancestor when a fold hid it. */
static void utility_tree_rows(struct device_utility *p)
{
    p->tree_count = 0; utility_tree_walk(p, 0);
    tiku_list_set_count(&p->tree_list, p->tree_count);
    int node = p->tree_selected, found = -1;
    while (node >= 0 && found < 0) {
        for (int i = 0; i < p->tree_count; i++)
            if (p->tree_rows[i] == node) { found = i; break; }
        if (found < 0) node = p->tree[node].parent;
    }
    p->tree_selected = node < 0 ? 0 : node;
    tiku_list_select_only(&p->tree_list, found < 0 ? 0 : found);
    tiku_list_reveal(&p->tree_list, (tiku_rect_t){0, 0, 248, U_TREE_HEIGHT});
}

/** @brief Empty the tree and the cache back to the root; the inspector's
 *         next read is a preload. */
static void utility_tree_reset(struct device_utility *p)
{
    utility_cache_free(p->cache); p->cache = NULL;
    p->preload = p->mode;
    memset(p->tree, 0, sizeof p->tree);
    strcpy(p->tree[0].path, "/"); p->tree[0].parent = -1;
    p->tree_selected = 0; p->tree_full = 0; p->collapse = 0;
    p->folder[0] = 0;
    tiku_list_init(&p->tree_list, 1);
    utility_tree_rows(p);
}

/** @brief The node of @p path, trailing slashes ignored, made along with
 *         any missing ancestors; each ancestor is opened, and a full tree
 *         sets tree_full.
 *  @return the node, 0 for the root, -1 for a bad path or a full tree. */
static int utility_tree_ensure(struct device_utility *p, const char *path)
{
    if (!utility_path(path)) return -1;
    char canonical[256];
    snprintf(canonical, sizeof canonical, "%s", path);
    size_t length = strlen(canonical);
    while (length > 1 && canonical[length-1] == '/') canonical[--length] = 0;
    path = canonical;
    if (!strcmp(path, "/")) return 0;
    char prefix[256]; int parent = 0;
    for (const char *end = path + 1; ; end++) {
        if (*end && *end != '/') continue;
        size_t n = (size_t)(end - path);
        if (n == 1) return 0;
        memcpy(prefix, path, n); prefix[n] = 0;
        int at = -1, empty = -1;
        for (int i = 1; i < U_ROWS; i++) {
            if (!strcmp(p->tree[i].path, prefix)) { at = i; break; }
            if (!p->tree[i].path[0] && empty < 0) empty = i;
        }
        if (at < 0) {
            if (empty < 0) { p->tree_full = 1; return -1; }
            at = empty; strcpy(p->tree[at].path, prefix);
            p->tree[at].parent = parent;
        }
        p->tree[parent].expanded = 1; parent = at;
        if (!*end) return at;
    }
}

/** @brief Remove @p node, its descendants and their cached listings; a
 *         selection among them moves to the parent. */
static void utility_tree_drop(struct device_utility *p, int node)
{
    for (int i = 1; i < U_ROWS; i++)
        if (p->tree[i].path[0] && p->tree[i].parent == node) utility_tree_drop(p, i);
    if (p->tree_selected == node) p->tree_selected = p->tree[node].parent;
    utility_cache_forget(p, p->tree[node].path);
    memset(&p->tree[node], 0, sizeof p->tree[node]);
}

/** @brief Merge job @p j's listing into the tree: a whole listing drops
 *         the folders gone from it, each folder in it gets a node, and the
 *         folder is selected and opens unless j->collapse. */
static void utility_tree_merge(struct device_utility *p, utility_job_t *j)
{
    p->tree_full = 0;
    int parent = utility_tree_ensure(p, j->path);
    if (parent < 0) return;
    if (!j->truncated) for (int i = 1; i < U_ROWS; i++) {
        if (!p->tree[i].path[0] || p->tree[i].parent != parent) continue;
        const char *name = strrchr(p->tree[i].path, '/') + 1; int present = 0;
        for (int k = 0; k < j->count; k++)
            if (j->entries[k].directory && !strcmp(name, j->entries[k].name)) present = 1;
        if (!present) utility_tree_drop(p, i);
    }
    for (int i = 0; i < j->count; i++) if (j->entries[i].directory) {
        char path[256];
        int n = snprintf(path, sizeof path, "%s%s%s", j->path,
            strcmp(j->path, "/") ? "/" : "", j->entries[i].name);
        if (n < (int)sizeof path) (void)utility_tree_ensure(p, path);
    }
    p->tree[parent].loaded = 1; p->tree[parent].expanded = !j->collapse;
    p->tree_selected = parent; utility_tree_rows(p);
}

/** @brief Apply a read to the inspector: a folder's listing fills the
 *         tree and the value rows; a value read updates its row and its
 *         cached listing. */
static void utility_registry_result(struct device_utility *p, utility_job_t *j)
{
    /* Cleared for each result: tree_full reports only this result's
     * merge. */
    p->tree_full = 0;
    if (j->directory && j->ok) {
        utility_tree_merge(p, j);
        snprintf(p->folder, sizeof p->folder, "%s", j->path);
        p->count = 0;
        for (int i = 0; i < j->count; i++)
            if (!j->entries[i].directory) p->entries[p->count++] = j->entries[i];
        tiku_list_set_count(&p->list, p->count); p->list.top = 0;
        tiku_list_select_only(&p->list, p->count ? 0 : -1);
    } else if (!j->directory && j->found) {
        /* Only a value the board found has its folder added to the
         * tree. */
        char parent[256]; snprintf(parent, sizeof parent, "%s", j->path);
        char *slash = strrchr(parent, '/');
        if (slash) *slash = 0;
        if (!parent[0]) strcpy(parent, "/");
        int node = utility_tree_ensure(p, parent);
        if (node >= 0) { p->tree_selected = node; utility_tree_rows(p); }
        utility_snapshot_t *s = utility_cache_find(p->cache, parent);
        const char *name = strrchr(j->path, '/');
        if (s && name) for (int i = 0; i < s->count; i++) {
            utility_entry_t *e = &s->entries[i];
            if (e->directory || strcmp(e->name, name + 1)) continue;
            e->readable = j->readable; e->writable = j->writable;
            e->sampled = j->ok ? 1 : -1;
            snprintf(e->data, sizeof e->data, "%.127s", j->value);
        }
        for (int i = 0; i < p->count; i++) {
            char path[256];
            int n = snprintf(path, sizeof path, "%s%s%s", p->folder,
                strcmp(p->folder, "/") ? "/" : "", p->entries[i].name);
            if (n >= (int)sizeof path || strcmp(path, j->path)) continue;
            p->entries[i].readable = j->readable;
            p->entries[i].writable = j->writable;
            p->entries[i].sampled = j->ok ? 1 : -1;
            snprintf(p->entries[i].data, sizeof p->entries[i].data, "%.127s", j->value);
            tiku_list_select_only(&p->list, i);
        }
    }
    if (p->tree_full) snprintf(p->status, sizeof p->status,
        "Tree cache full (128 branches). Use an exact path or reselect the device to clear it.");
}

/** @brief Show @p path's cached listing as the folder shown, with nothing
 *         to edit.
 *  @return 1, or 0 when the cache holds no successful listing of @p path. */
static int utility_cache_show(struct device_utility *p, const char *path)
{
    utility_snapshot_t *s = utility_cache_find(p->cache, path);
    if (!s || !s->ok) return 0;
    utility_job_t *j = calloc(1, sizeof *j); if (!j) return 0;
    strcpy(j->path, s->path); j->ok = j->directory = 1;
    j->count = s->count; j->truncated = s->truncated; j->collapse = p->collapse;
    p->collapse = 0;
    memcpy(j->entries, s->entries, sizeof j->entries);
    utility_registry_result(p, j); free(j);
    strcpy(p->path, s->path); tiku_field_start(&p->address, p->path);
    p->directory = p->readable = 1; p->writable = p->armed = 0;
    p->truncated = s->truncated;
    p->value[0] = 0; tiku_field_start(&p->edit, "");
    snprintf(p->status, sizeof p->status,
        "Cached device snapshot. Select a value to read it now; Refresh reloads the snapshot.");
    return 1;
}

/** @brief Take the preload's listings, build the tree from them, and show
 *         the folder shown before (/sys for the root), its parent, or the
 *         root. */
static void utility_preload_result(struct device_utility *p, utility_job_t *j)
{
    utility_cache_free(p->cache); p->cache = j->cache; j->cache = NULL;
    utility_job_t *entry = calloc(1, sizeof *entry);
    if (!entry) { snprintf(p->status, sizeof p->status, "Unable to display device snapshot."); return; }
    for (utility_snapshot_t *s = p->cache; s; s = s->next) {
        if (!s->ok) continue;
        strcpy(entry->path, s->path); entry->count = s->count;
        entry->truncated = s->truncated;
        memcpy(entry->entries, s->entries, sizeof entry->entries);
        utility_tree_merge(p, entry);
    }
    free(entry);
    char selected[256]; snprintf(selected, sizeof selected, "%s", p->path);
    if (!strcmp(selected, "/")) strcpy(selected, "/sys");
    if (!utility_cache_find(p->cache, selected)) {
        char *slash = strrchr(selected, '/'); if (slash) *slash = 0;
        if (!selected[0]) strcpy(selected, "/");
    }
    if (!utility_cache_show(p, selected)) (void)utility_cache_show(p, "/");
    snprintf(p->status, sizeof p->status, "%s", j->status);
}
