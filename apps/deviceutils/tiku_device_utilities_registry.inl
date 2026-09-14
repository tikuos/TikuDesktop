/* Registry-style split view; explicit selection reads only one endpoint. */

/** Bounded rectangles shared by custom tree/table painters and input. */
static tiku_rect_t registry_tree_rect(struct device_utility *p)
{ return utility_rect(p, 16, 158, 248, U_TREE_HEIGHT); }
static tiku_rect_t registry_values_rect(struct device_utility *p)
{ return utility_rect(p, 280, 158, 560, 142); }

/** Point in rectangle, so a wheel turn moves the pane under the pointer. */
static int registry_holds(tiku_rect_t r, int x, int y)
{ return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/** Clip every cell so a long name or value cannot overwrite another column. */
static void registry_cell(tiku_surface_t *s, tiku_rect_t r, const char *text, tiku_rgb_t ink)
{ tiku_text_line_draw(s, r, TIKU_MEASURE_PLAIN, text, ink); }

/** Depth follows directory parents, not the length of their names. */
static int registry_depth(struct device_utility *p, int node)
{
    int depth = 0;
    while (p->tree[node].parent >= 0 && depth < 20) {
        node = p->tree[node].parent; depth++;
    }
    return depth;
}

/** A persistent directory tree with disclosure buttons and native selection. */
static void registry_tree_draw(struct device_utility *p, tiku_surface_t *s)
{
    tiku_rect_t body = registry_tree_rect(p);
    tiku_fill(s, body, TIKU_C_DOC);
    for (int i = p->tree_list.top; i < p->tree_count &&
         i < p->tree_list.top + tiku_list_visible_rows(&p->tree_list, body); i++) {
        int node = p->tree_rows[i], selected = tiku_list_selected(&p->tree_list, i);
        tiku_rect_t r = tiku_list_row_rect(&p->tree_list, body, i);
        int indent = registry_depth(p, node) * 14;
        if (indent > 154) indent = 154;
        if (selected) tiku_fill(s, r, TIKU_C_SELECT);
        tiku_rgb_t ink = selected ? TIKU_C_DOC : TIKU_C_TEXT;
        registry_cell(s, (tiku_rect_t){r.x + indent + 4, r.y, 20, r.h},
            p->tree[node].expanded ? "-" : "+", ink);
        const char *label = node ? strrchr(p->tree[node].path, '/') + 1 :
            p->target[0] ? p->target : "Device (not connected)";
        registry_cell(s, (tiku_rect_t){r.x + indent + 24, r.y, r.w - indent - 28, r.h}, label, ink);
    }
    tiku_frame(s, body, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

/** Show Name, Type, Access and Data; unqueried values are never invented. */
static void registry_values_draw(struct device_utility *p, tiku_surface_t *s)
{
    const int x[] = {0, 154, 240, 332, 560};
    const char *head[] = {"Name", "Type", "Access", "Data"};
    tiku_rect_t body = registry_values_rect(p);
    tiku_fill(s, body, TIKU_C_DOC);
    for (int k = 0; k < 4; k++)
        registry_cell(s, utility_rect(p, 284 + x[k], 132, x[k+1] - x[k] - 8, 26), head[k], TIKU_C_TEXT);
    for (int i = p->list.top; i < p->count &&
         i < p->list.top + tiku_list_visible_rows(&p->list, body); i++) {
        utility_entry_t *e = &p->entries[i];
        tiku_rect_t r = tiku_list_row_rect(&p->list, body, i);
        int selected = tiku_list_selected(&p->list, i);
        if (selected) tiku_fill(s, r, TIKU_C_SELECT);
        char path[384];
        snprintf(path, sizeof path, "%s%s%s", p->folder,
            strcmp(p->folder, "/") ? "/" : "", e->name);
        const char *pending = !e->readable ? "—" :
            utility_autoread(path) ? "Not read" : "Manual read";
        const char *values[] = {e->name, "Endpoint", e->writable ?
            e->readable ? "R/W" : "Write-only" : e->readable ? "Read-only" : "No access",
            e->sampled > 0 ? e->data : e->sampled < 0 ? "Unavailable" : pending};
        for (int k = 0; k < 4; k++)
            registry_cell(s, (tiku_rect_t){r.x + x[k] + 4, r.y, x[k+1] - x[k] - 8, r.h},
                values[k], selected ? TIKU_C_DOC : TIKU_C_TEXT);
    }
    if (!p->count) registry_cell(s, (tiku_rect_t){body.x+8, body.y+4, body.w-16, 26},
        p->busy ? "Loading…" : "No values loaded. Select a directory on the left.", TIKU_C_TEXT);
    tiku_frame(s, body, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

/** Directory navigation clears the old snapshot before publishing a new path. */
static void registry_go(struct device_utility *p, const char *path)
{
    char copy[256];
    if (!utility_path(path)) {
        snprintf(p->status, sizeof p->status, "Enter an absolute path without . or .. components."); return;
    }
    snprintf(copy, sizeof copy, "%s", path);
    size_t n = strlen(copy); while (n > 1 && copy[n-1] == '/') copy[--n] = 0;
    snprintf(p->path, sizeof p->path, "%s", copy);
    tiku_field_start(&p->address, p->path);
    p->armed = p->writable = p->count = 0; p->value[0] = 0; p->folder[0] = 0;
    tiku_list_set_count(&p->list, 0); tiku_field_start(&p->edit, "");
    utility_request(p, 0);
}

/** Move selection and invalidate the old editor before requesting another value. */
static int registry_select_value(struct device_utility *p)
{
    int row = p->list.cursor;
    if (p->busy || row < 0 || row >= p->count || !p->folder[0]) return 0;
    char path[256];
    int n = snprintf(path, sizeof path, "%s%s%s", p->folder,
        strcmp(p->folder, "/") ? "/" : "", p->entries[row].name);
    if (n >= (int)sizeof path || !utility_path(path)) return 0;
    snprintf(p->path, sizeof p->path, "%s", path);
    tiku_field_start(&p->address, path);
    p->armed = p->writable = 0; p->value[0] = 0;
    p->directory = 0;
    tiku_field_start(&p->edit, ""); return 1;
}

/** Reading one value keeps its siblings and the expanded branch in place. */
static void registry_read_value(struct device_utility *p)
{
    if (registry_select_value(p)) utility_request(p, 0);
}

/** Select or collapse one branch; expansion fetches only its direct children. */
static void registry_branch(struct device_utility *p, int node, int toggle)
{
    if (p->busy || node < 0 || node >= U_ROWS || !p->tree[node].path[0]) return;
    p->tree_selected = node;
    if (toggle && p->tree[node].expanded) {
        p->tree[node].expanded = 0; utility_tree_rows(p);
        if (strcmp(p->folder, p->tree[node].path)) {
            p->collapse = 1; registry_go(p, p->tree[node].path);
        }
        return;
    }
    p->tree[node].expanded = 1; utility_tree_rows(p);
    registry_go(p, p->tree[node].path);
}

/** Independent scrolling and familiar Left/Right tree navigation. */
static int registry_event(struct device_utility *p, const tiku_event_t *ev)
{
    tiku_rect_t tree = registry_tree_rect(p), values = registry_values_rect(p);
    if (ev->type == TIKU_EVENT_WHEEL) {
        if (registry_holds(tree, ev->x, ev->y))
            tiku_list_scroll_to(&p->tree_list, p->tree_list.top + ev->dy, tree);
        else if (registry_holds(values, ev->x, ev->y))
            tiku_list_scroll_to(&p->list, p->list.top + ev->dy, values);
        else return 0;
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int row = tiku_list_at(&p->tree_list, tree, ev->x, ev->y);
        if (row >= 0) {
            int node = p->tree_rows[row], indent = registry_depth(p, node) * 14;
            if (indent > 154) indent = 154;
            registry_branch(p, node, ev->x < tree.x + indent + 24); return 1;
        }
        row = tiku_list_at(&p->list, values, ev->x, ev->y);
        if (row >= 0) { tiku_list_select_only(&p->list, row); registry_read_value(p); return 1; }
    }
    if (ev->type != TIKU_EVENT_KEY_DOWN ||
        (ev->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION))) return 0;
    if (p->dialog.focus == U_LIST) {
        if (ev->key == TIKU_KEY_RETURN) registry_read_value(p);
        else {
            int before = p->list.cursor;
            tiku_list_key(&p->list, ev->key, ev->modifiers, values);
            if (p->list.cursor != before) {
                (void)registry_select_value(p);
                snprintf(p->status, sizeof p->status, "Press Enter to read the selected value before editing it.");
            }
        }
        return 1;
    }
    if (p->dialog.focus != U_TREE) return 0;
    int node = p->tree_rows[p->tree_list.cursor < 0 ? 0 : p->tree_list.cursor];
    if (ev->key == TIKU_KEY_LEFT) {
        if (p->tree[node].expanded) registry_branch(p, node, 1);
        else if (p->tree[node].parent >= 0) registry_branch(p, p->tree[node].parent, 0);
    } else if (ev->key == TIKU_KEY_RIGHT || ev->key == TIKU_KEY_RETURN) {
        int next = p->tree_list.cursor + 1;
        if (ev->key == TIKU_KEY_RIGHT && p->tree[node].expanded &&
            next < p->tree_count && p->tree[p->tree_rows[next]].parent == node)
            node = p->tree_rows[next];
        registry_branch(p, node, 0);
    }
    else {
        int before = p->tree_list.cursor;
        tiku_list_key(&p->tree_list, ev->key, ev->modifiers, tree);
        if (p->tree_list.cursor >= 0 && p->tree_list.cursor != before)
            registry_branch(p, p->tree_rows[p->tree_list.cursor], 0);
    }
    return 1;
}
