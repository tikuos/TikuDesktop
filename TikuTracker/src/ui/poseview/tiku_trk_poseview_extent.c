/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_extent.c - the viewport, the extent of what is laid out,
 * and the notices that move them
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/* Used before they are defined. */
int pv_notify_changed(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                      unsigned *flags, int *at, unsigned *out);
int pv_notify_moved(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                    unsigned *flags, int *at, unsigned *out);
int pv_notify_removed(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                      unsigned *flags, int *at, unsigned *out);
int pv_notify_created(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                      unsigned *flags, int *at, unsigned *out);
int pv_poll_ring(tiku_trk_poseview_t *v, int64_t now,
                 const tiku_model_change_t *ch, int nch, int complete,
                 unsigned *flags, unsigned *out);
/* End of the forward declarations. */

/** @brief Take one pose's rectangle into the running bounding box. */
void
pv_extent_add(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
{
    int px, py, pw, ph;

    if (p == NULL || !p->placed) {
        return;
    }
    tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
    if (!v->ext_valid) {
        v->ext_x0 = px;
        v->ext_y0 = py;
        v->ext_x1 = px + pw;
        v->ext_y1 = py + ph;
        v->ext_valid = 1;
        return;
    }
    if (px < v->ext_x0)           { v->ext_x0 = px; }
    if (py < v->ext_y0)           { v->ext_y0 = py; }
    if (px + pw > v->ext_x1)      { v->ext_x1 = px + pw; }
    if (py + ph > v->ext_y1)      { v->ext_y1 = py + ph; }
}

/** @brief Forget the box; the next pose to arrive defines it again. */
void
pv_extent_clear(tiku_trk_poseview_t *v)
{
    v->ext_valid = 0;
    v->ext_x0 = v->ext_y0 = v->ext_x1 = v->ext_y1 = 0;
}

/** @brief Walk every pose again.  Only for what cannot grow a box: removal. */
static void
extent_recalc(tiku_trk_poseview_t *v)
{
    int i;

    pv_extent_clear(v);
    for (i = 0; i < v->count; i++) {
        pv_extent_add(v, &v->poses[i]);
    }
}

void
tiku_trk_poseview_set_viewport(tiku_trk_poseview_t *v, int w, int h)
{
    if (v != NULL && (v->view_w != w || v->view_h != h)) {
        /* The hint is a point in the WINDOW, so a window of a different
         * size has a different top-left to start from (PVL-017). */
        tiku_trk_poseview_reset_hint(v);
    }
    if (v == NULL || w < 0 || h < 0) {
        return;
    }
    v->view_w = w;
    v->view_h = h;
    /* And the horizontal axis is pulled back like the vertical one: a
     * window grown past its scrolled-away content re-anchors the content
     * to the window instead of keeping a blank gulf (PVL-058). */
    tiku_trk_poseview_set_hscroll(v, v->hscroll);
    /* The row count follows from the height, so a caller that says how big
     * the window is has said everything: two numbers that could disagree
     * are two numbers that will. */
    tiku_trk_poseview_set_visible_rows(v,
        h / (tiku_trk_poseview_row_height(v) > 0
                 ? tiku_trk_poseview_row_height(v) : 1));
    {
        /* The CURRENT origin, put through the clamp again: a window that
         * grew can leave the view scrolled past content that now fits.
         * Passing zero here instead would reset the view to the top-left
         * on every resize, which is a different bug wearing this one's
         * clothes. */
        int x, y;

        tiku_trk_poseview_origin(v, &x, &y);
        tiku_trk_poseview_set_origin(v, x, y);
    }
}

void
tiku_trk_poseview_viewport(const tiku_trk_poseview_t *v, int *w, int *h)
{
    if (v == NULL) {
        return;
    }
    if (w != NULL) { *w = v->view_w; }
    if (h != NULL) { *h = v->view_h; }
}

void
tiku_trk_poseview_extent(tiku_trk_poseview_t *v, int *x, int *y, int *w,
                         int *h)
{
    int gx, gy, ox, oy, x0, y0, x1, y1;

    if (v == NULL) {
        return;
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* The columns by the rows.  A list has no origin of its own: it
         * scrolls by whole rows, which is what v->scroll counts. */
        int shown = v->filtering ? v->filtered_count : v->count;

        if (x != NULL) { *x = 0; }
        if (y != NULL) { *y = 0; }
        if (w != NULL) { *w = tiku_trk_poseview_columns_width(v); }
        if (h != NULL) { *h = shown * tiku_trk_poseview_row_height(v); }
        return;
    }
    if (!v->ext_valid) {
        extent_recalc(v);
    }
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    /* Outset by the layout offset, so an icon at the origin is not flush
     * against the window's edge -- the same margin auto-placement leaves. */
    x0 = v->ext_valid ? v->ext_x0 - ox : 0;
    y0 = v->ext_valid ? v->ext_y0 - oy : 0;
    x1 = v->ext_valid ? v->ext_x1 + ox : 0;
    y1 = v->ext_valid ? v->ext_y1 + oy : 0;
    /* The window's own corner is always part of it, so icons that all sit
     * low in the view do not drag the origin down after them: dropping at
     * the bottom of a window would scroll the window, putting what was
     * just dropped somewhere other than where it was let go.  The margin
     * is only outside content that is ITSELF outside the corner -- adding
     * it to an icon at 3 would scroll the view by three pixels. */
    if (v->ext_valid && v->ext_x0 >= 0) { x0 = 0; }
    if (v->ext_valid && v->ext_y0 >= 0) { y0 = 0; }
    if (x != NULL) { *x = x0; }
    if (y != NULL) { *y = y0; }
    if (w != NULL) { *w = v->ext_valid ? x1 - x0 : 0; }
    if (h != NULL) { *h = v->ext_valid ? y1 - y0 : 0; }
}

/** @brief The origin pair the current mode uses, or NULL in list mode. */
static void
origin_of(tiku_trk_poseview_t *v, int **x, int **y)
{
    if (v->mode == TIKU_TRK_MODE_ICON) {
        *x = &v->icon_x;
        *y = &v->icon_y;
    } else if (v->mode == TIKU_TRK_MODE_MINI) {
        *x = &v->mini_x;
        *y = &v->mini_y;
    } else {
        *x = NULL;
        *y = NULL;
    }
}

void
tiku_trk_poseview_set_origin(tiku_trk_poseview_t *v, int x, int y)
{
    int *ox, *oy, ex, ey, ew, eh, maxx, maxy;

    if (v == NULL) {
        return;
    }
    origin_of(v, &ox, &oy);
    if (ox == NULL) {
        return;
    }
    tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
    /* Clamped to what there IS to see: an origin past the end is rows of
     * blank space with nothing to scroll back to, and a saved one from a
     * bigger window would open the folder looking empty (PVL-053). */
    maxx = ew - v->view_w;
    maxy = eh - v->view_h;
    if (maxx < 0) { maxx = 0; }
    if (maxy < 0) { maxy = 0; }
    if (x < ex) { x = ex; }
    if (y < ey) { y = ey; }
    if (x > ex + maxx) { x = ex + maxx; }
    if (y > ey + maxy) { y = ey + maxy; }
    *ox = x;
    *oy = y;
}

void
tiku_trk_poseview_origin(const tiku_trk_poseview_t *v, int *x, int *y)
{
    if (v == NULL) {
        return;
    }
    if (x != NULL) {
        *x = (v->mode == TIKU_TRK_MODE_ICON) ? v->icon_x
           : (v->mode == TIKU_TRK_MODE_MINI) ? v->mini_x : 0;
    }
    if (y != NULL) {
        /* A list scrolls in ROWS, and its origin is that scroll in the
         * same pixels every other mode answers in -- a caller converting
         * screen to view coordinates must not have to know which mode it
         * is looking at. */
        *y = (v->mode == TIKU_TRK_MODE_ICON) ? v->icon_y
           : (v->mode == TIKU_TRK_MODE_MINI) ? v->mini_y
           : v->scroll * tiku_trk_poseview_row_height(v);
    }
}

int
tiku_trk_poseview_visible_range(tiku_trk_poseview_t *v, int *first, int *last)
{
    int i, n, ox, oy, lo = -1, hi = -1;

    if (first != NULL) { *first = -1; }
    if (last != NULL)  { *last = -1; }
    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    if (n == 0) {
        return 0;
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        lo = v->scroll;
        hi = v->scroll + ((v->visible_rows > 0) ? v->visible_rows : 1) - 1;
        if (lo < 0)      { lo = 0; }
        if (hi > n - 1)  { hi = n - 1; }
        if (lo > n - 1)  { return 0; }
        if (first != NULL) { *first = lo; }
        if (last != NULL)  { *last = hi; }
        return 1;
    }
    tiku_trk_poseview_origin(v, &ox, &oy);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        if (px + pw <= ox || py + ph <= oy ||
            (v->view_w > 0 && px >= ox + v->view_w) ||
            (v->view_h > 0 && py >= oy + v->view_h)) {
            continue;
        }
        if (lo < 0) { lo = i; }
        hi = i;
    }
    if (lo < 0) {
        return 0;
    }
    if (first != NULL) { *first = lo; }
    if (last != NULL)  { *last = hi; }
    return 1;
}

void
tiku_trk_poseview_set_visible_rows(tiku_trk_poseview_t *v, int rows)
{
    int shown, max;

    if (v == NULL || rows < 0) {
        return;
    }
    v->visible_rows = rows;
    /* A window that grew, or a listing that shrank, can leave the view
     * scrolled past its own content -- rows of blank space under real
     * content, with nothing to scroll to (CW-013, PVL-061).  Corrected
     * here because this is the one call that knows the height. */
    shown = v->filtering ? v->filtered_count : v->count;
    max = shown - rows;
    if (max < 0) {
        max = 0;
    }
    if (v->scroll > max) {
        v->scroll = max;
    }
}

int
tiku_trk_poseview_set_invisible(tiku_trk_poseview_t *v, int index,
                                int hidden)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(v, index);

    if (p == NULL || p->invisible == (hidden ? 1 : 0)) {
        return 0;
    }
    p->invisible = hidden ? 1 : 0;
    /* The bit lives in the saved record, so hiding something is a change to
     * PERSIST -- without this it would be forgotten at the next reload and
     * the entry would quietly come back. */
    p->placed = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_visible_rows(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->visible_rows : 0;
}

int
tiku_trk_poseview_parked(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? tiku_park_count(&v->park) : 0;
}

int
tiku_trk_poseview_zombies(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->zombie_count : 0;
}

/** @brief Whether @p path's saved record says it is not to be shown. */
int
pv_pose_is_hidden(tiku_trk_poseview_t *v, const char *path)
{
    tiku_store_t *store;
    char raw[sizeof(pose_info_t) + 4];
    pose_info_t info;

    if (v->backend == NULL || v->backend->ops->state_store == NULL) {
        return 0;
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL) {
        return 0;
    }
    if (tiku_state_read(store, path, TIKU_ATTR_POSE_INFO, raw,
                            sizeof raw) != (int)sizeof info) {
        return 0;
    }
    memcpy(&info, raw, sizeof info);
    return (info.flags & POSE_INFO_INVISIBLE) ? 1 : 0;
}

/** @brief Index of the pose holding @p path, or -1. */
int
pv_pose_index(const tiku_trk_poseview_t *v, const char *path)
{
    int i;

    for (i = 0; i < v->count; i++) {
        if (strcmp(v->poses[i].model.path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief Where @p p belongs in the current sort, without re-sorting. */
int
pv_sorted_slot(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
{
    int lo = 0, hi = v->count;

    pv_g_sorting = v;
    while (lo < hi) {
        int mid = (lo + hi) / 2;

        if (pv_pose_cmp(&v->poses[mid], p) <= 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    pv_g_sorting = NULL;
    return lo;
}

/**
 * @brief Insert @p p at its sorted slot, re-sorting first when the array
 *        has come unsorted, and move every index that pointed past the
 *        slot down by one.  @return the slot, or -1 when the array is
 *        full.
 */
int
pv_insert_sorted(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
{
    int at, i;

    if (v->count >= POSES_MAX) {
        return -1;
    }
    /* The binary search is only right when the array is ordered, and a value
     * refresh under a value sort can have unsorted it since.  Restoring the
     * precondition here costs a re-sort only when it was actually broken. */
    if (!pv_poses_sorted(v)) {
        pv_resort(v);
    }
    at = pv_sorted_slot(v, p);
    tiku_trk_repaint_insert(&v->repaint,
                            (tiku_trk_repaint_rect_t){0, 0, v->view_w, v->view_h},
                            v->row_h > 0 ? v->row_h : 1,
                            (at - v->scroll) * (v->row_h > 0 ? v->row_h : 1));
    for (i = v->count; i > at; i--) {
        v->poses[i] = v->poses[i - 1];
    }
    v->poses[at] = *p;
    v->count++;
    /* Indices that pointed past the insertion have all moved down one. */
    if (v->active >= at)         { v->active++; }
    if (v->pivot >= at)          { v->pivot++; }
    if (v->real_pivot >= at)     { v->real_pivot++; }
    if (v->last_added >= at)     { v->last_added++; }
    /* The armed rename holds a pose index too; left unadjusted it would
     * fire on whatever row slid into that slot. */
    if (v->edit_pose >= at)      { v->edit_pose++; }
    for (i = 0; i < v->count; i++) {
        v->poses[i].index = i;
    }
    return at;
}

/**
 * @brief Drop the row at @p at and everything that referred to it.
 *
 * A row removed while it is the selection pivot, the active pose or the
 * last-added leaves those pointing at whatever slid into its place, so they
 * are cleared here rather than at each call site.
 */
void
pv_remove_pose(tiku_trk_poseview_t *v, int at)
{
    int i;

    if (at < 0 || at >= v->count) {
        return;
    }
    tiku_trk_repaint_remove(&v->repaint,
                            (tiku_trk_repaint_rect_t){0, 0, v->view_w, v->view_h},
                            v->row_h > 0 ? v->row_h : 1,
                            (at - v->scroll) * (v->row_h > 0 ? v->row_h : 1));
    for (i = at; i < v->count - 1; i++) {
        v->poses[i] = v->poses[i + 1];
    }
    v->count--;
    if (v->active == at)          { v->active = -1; }
    else if (v->active > at)      { v->active--; }
    if (v->pivot == at)           { v->pivot = -1; }
    else if (v->pivot > at)       { v->pivot--; }
    if (v->real_pivot == at)      { v->real_pivot = -1; }
    else if (v->real_pivot > at)  { v->real_pivot--; }
    if (v->last_added == at)      { v->last_added = -1; }
    else if (v->last_added > at)  { v->last_added--; }
    /* A rename armed on the row that just went is disarmed, not re-aimed. */
    if (v->edit_pose == at)       { v->edit_pose = -1; v->edit_clicked_at = 0; }
    else if (v->edit_pose > at)   { v->edit_pose--; }
    for (i = 0; i < v->count; i++) {
        v->poses[i].index = i;
    }
    /* The one thing a running box cannot do is shrink, so a removal is the
     * one thing that pays for the walk (PVL-050). */
    extent_recalc(v);
    /*
     * Stated in terms of what is VISIBLE: if the row that went was the last
     * one on screen and the view is scrolled, coming up by one keeps the
     * window full instead of leaving blank space under real content.  With
     * no window height known, fall back to the weaker "do not scroll past
     * the end" rule rather than guessing a height.
     */
    {
        /* Scroll indexes the DISPLAYED list, so the correction counts that
         * list: comparing against the unfiltered total scrolls a filtered
         * view by the wrong amount, or not at all. */
        int shown = v->filtering ? v->filtered_count : v->count;

        if (v->visible_rows > 0) {
            if (v->scroll > 0 && v->scroll + v->visible_rows > shown) {
                v->scroll--;
            }
        } else if (v->scroll > 0 && v->scroll >= shown) {
            v->scroll = (shown > 0) ? shown - 1 : 0;
        }
    }
}

int
tiku_trk_poseview_on_device(const tiku_trk_poseview_t *v, uint64_t dev)
{
    int i;

    if (v == NULL || dev == 0u) {
        return 0;
    }
    /* The FOLDER's own device, taken from any row that is on it: the view
     * keeps no model of the directory it shows, and every row of a folder
     * on one volume carries that volume. */
    for (i = 0; i < v->count; i++) {
        if (v->poses[i].model.facts.dev == dev) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_poseview_device_gone(tiku_trk_poseview_t *v, uint64_t dev)
{
    int i, went = 0;

    if (v == NULL || dev == 0u) {
        return 0;
    }
    for (i = v->count - 1; i >= 0; i--) {
        if (v->poses[i].model.facts.dev == dev) {
            pv_remove_pose(v, i);
            went++;
        }
    }
    if (went > 0 && v->filtering) {
        pv_rebuild_filter(v);
    }
    return went;
}

void
tiku_trk_poseview_set_icons(tiku_trk_poseview_t *v, tiku_trk_nodeicon_t *c)
{
    if (v != NULL) {
        v->icons = c;
    }
}

uint64_t
tiku_trk_poseview_menu_generation(const tiku_trk_poseview_t *v)
{
    return v != NULL ? v->menu_generation : 0;
}

void
tiku_trk_poseview_schedule_broken_repair(tiku_trk_poseview_t *v, int64_t now)
{
    if (v != NULL) {
        v->broken_repair_due = now + 500000;
    }
}

int
tiku_trk_poseview_broken_repair_due(tiku_trk_poseview_t *v, int64_t now)
{
    if (v == NULL || v->broken_repair_due <= 0 ||
        now < v->broken_repair_due) {
        return 0;
    }
    v->broken_repair_due = 0;
    return 1;
}

/** @brief Which notice fields a column reads from (MA-072's routing). */
static unsigned
column_fields(int field)
{
    switch (field) {
    case TIKU_TRK_COL_SIZE:  return TIKU_EVF_SIZE;
    case TIKU_TRK_COL_DATE:  return TIKU_EVF_DATE;
    case TIKU_TRK_COL_KIND:  return TIKU_EVF_KIND;
    case TIKU_TRK_COL_VALUE: return TIKU_EVF_VALUE;
    case TIKU_TRK_COL_CAP:   return TIKU_EVF_CAP;
    case TIKU_TRK_COL_PERM:  return TIKU_EVF_PERM;
    default:
        /* The name changes only through MOVED; a named attribute column
         * reads the meta the VALUE bit stands for. */
        return (field >= TIKU_TRK_COL_ATTR_BASE) ? TIKU_EVF_VALUE : 0u;
    }
}

unsigned
tiku_trk_poseview_notify(tiku_trk_poseview_t *v, const tiku_node_event_t *e)
{
    unsigned flags = TIKU_TRK_NOTIFY_NOTHING;
    int at;
    unsigned out;

    if (v == NULL || e == NULL) {
        return flags;
    }
    if (v->icons != NULL) {
        /* Before anything else, and whether or not this view holds the row:
         * the cache is shared, so a notice that turns out to be about some
         * other window's entry still describes an icon this one may be
         * showing.  The MODEL's own hint needs no clearing here -- every
         * branch below overwrites it with the freshly listed one, which has
         * not been asked yet. */
        if (e->op == TIKU_EV_REMOVED) {
            (void)tiku_trk_nodeicon_deleted(v->icons, e->path);
        } else {
            (void)tiku_trk_nodeicon_changed(v->icons, e->path);
            if (e->op == TIKU_EV_MOVED && e->from[0] != '\0') {
                /* Keyed by path, so its former name is a key that describes
                 * nothing. */
                (void)tiku_trk_nodeicon_changed(v->icons, e->from);
            }
        }
    }
    /* A notice about the folder ITSELF is not about its contents.  A rename
     * names the folder by its OLD path -- the new one is where it has just
     * gone -- so matching only e->path would miss exactly the case the
     * retitle rule is about. */
    if (strcmp(e->path, v->path) == 0 ||
        (e->op == TIKU_EV_MOVED && e->from[0] != '\0' &&
         strcmp(e->from, v->path) == 0)) {
        if (e->op == TIKU_EV_REMOVED) {
            /* The root is exempt: it has nowhere to go, and closing it would
             * leave nothing to browse. */
            return (strcmp(v->path, "/") == 0)
                       ? TIKU_TRK_NOTIFY_NOTHING : TIKU_TRK_NOTIFY_GONE;
        }
        if (e->op == TIKU_EV_MOVED) {
            snprintf(v->path, sizeof v->path, "%s", e->path);
            return TIKU_TRK_NOTIFY_RETITLE | TIKU_TRK_NOTIFY_REDRAW;
        }
        return TIKU_TRK_NOTIFY_NOTHING;
    }

    /* PVN-057: an ANCESTOR moving takes this folder with it.  Nothing about
     * the contents changed and no row moved -- but the window is showing a
     * path that no longer exists, so the title has to follow. */
    if (e->op == TIKU_EV_MOVED && e->from[0] != '\0') {
        size_t n = strlen(e->from);

        /* At a component boundary, or "/a/b" would claim "/a/bc" as its
         * own child and rewrite an unrelated window's path. */
        if (n > 0u && strncmp(e->from, v->path, n) == 0 &&
            v->path[n] == '/') {
            char rebuilt[TIKU_PATH_MAX];

            /* Built into a scratch buffer and only then adopted: a path
             * that would not fit leaves the window pointing at the old one
             * rather than at a truncated name that resolves to nothing. */
            if ((int)snprintf(rebuilt, sizeof rebuilt, "%s%s", e->path,
                              v->path + n) < (int)sizeof rebuilt) {
                snprintf(v->path, sizeof v->path, "%s", rebuilt);
            }
            return TIKU_TRK_NOTIFY_RETITLE;
        }
    }

    switch (e->op) {
    case TIKU_EV_CREATED:
        if (pv_notify_created(v, e, &flags, &at, &out)) {
            return out;
        }
        break;
    case TIKU_EV_REMOVED:
        if (pv_notify_removed(v, e, &flags, &at, &out)) {
            return out;
        }
        break;
    case TIKU_EV_MOVED:
        if (pv_notify_moved(v, e, &flags, &at, &out)) {
            return out;
        }
        break;
    case TIKU_EV_CHANGED:
    case TIKU_EV_ATTR:
        if (pv_notify_changed(v, e, &flags, &at, &out)) {
            return out;
        }
        break;
    default:
        break;
    }
    if ((flags & TIKU_TRK_NOTIFY_REDRAW) != 0u && v->filtering) {
        pv_rebuild_filter(v);
    }
    return flags;
}

unsigned
tiku_trk_poseview_apply_results(tiku_trk_poseview_t *v,
                                const tiku_model_t *set, int n,
                                int64_t now)
{
    tiku_model_t *was;
    tiku_evlist_t evs;
    unsigned flags;
    int i, nwas;

    if (v == NULL) {
        return TIKU_TRK_NOTIFY_NOTHING;
    }
    /* The same diff a folder does, against the result set instead of a
     * directory read: a query window that rebuilt itself wholesale would
     * drop the selection every time any row moved, which a folder window
     * does not do -- and the row says both must update by one path. */
    was = calloc((size_t)(v->count > 0 ? v->count : 1), sizeof *was);
    if (was == NULL) {
        return TIKU_TRK_NOTIFY_NOTHING;
    }
    nwas = 0;
    for (i = 0; i < v->count; i++) {
        if (!v->poses[i].adopted) {
            was[nwas++] = v->poses[i].model;
        }
    }
    tiku_evlist_clear(&evs);
    (void)tiku_notify_diff(was, nwas, set, n, now, &evs);
    flags = tiku_trk_poseview_notify_all(v, &evs);
    free(was);
    return flags;
}

unsigned
tiku_trk_poseview_notify_all(tiku_trk_poseview_t *v,
                             const tiku_evlist_t *l)
{
    unsigned flags = TIKU_TRK_NOTIFY_NOTHING;
    int i;

    if (v == NULL || l == NULL) {
        return flags;
    }
    for (i = 0; i < l->count; i++) {
        flags |= tiku_trk_poseview_notify(v, &l->ev[i]);
    }
    return flags;
}

/**
 * @brief Take one entry of a re-listing into the diff, holding one the
 *        backend could not describe the way the first listing does.
 *        @return 1 to stop the walk when the list is full.
 */
static int
relist_one(const tiku_model_t *m, void *ctx)
{
    struct relist *r = ctx;

    if (r->n >= r->max) {
        return 1;
    }
    if (m->kind == TIKU_KIND_UNKNOWN) {
        /* The same hold the first listing applies.  Without it the live path
         * puts an entry the backend could not describe straight into the
         * diff, and it appears as a visible UNKNOWN row -- while still being
         * held, so it is both hidden and shown. */
        if (r->view != NULL &&
            r->view->zombie_count < (int)(sizeof r->view->zombie /
                                          sizeof r->view->zombie[0])) {
            int z;

            for (z = 0; z < r->view->zombie_count; z++) {
                if (strcmp(r->view->zombie[z].path, m->path) == 0) {
                    return 0;
                }
            }
            r->view->zombie[r->view->zombie_count++] = *m;
        }
        return 0;
    }
    if (r->view != NULL) {
        int z;

        for (z = 0; z < r->view->zombie_count; z++) {
            if (strcmp(r->view->zombie[z].path, m->path) == 0) {
                int k;

                for (k = z; k < r->view->zombie_count - 1; k++) {
                    r->view->zombie[k] = r->view->zombie[k + 1];
                }
                r->view->zombie_count--;
                break;
            }
        }
    }
    r->set[r->n++] = *m;
    return 0;
}

/** @brief What the node carrying @p id is currently called.  0 on success. */
static int
tiku_trk_poseview_path_of_id(tiku_trk_poseview_t *v, uint64_t id, char *out,
                             size_t max)
{
    if (v->backend->ops->path_of_id == NULL) {
        return -1;
    }
    return v->backend->ops->path_of_id(v->backend, id, out, max);
}

unsigned
tiku_trk_poseview_poll(tiku_trk_poseview_t *v, int64_t now,
                       const tiku_model_change_t *ch, int nch, int complete)
{
    unsigned out;
    tiku_model_t *was, *is;
    struct relist r;
    tiku_evlist_t evs;
    unsigned flags = TIKU_TRK_NOTIFY_NOTHING;
    int i, nwas;

    if (v == NULL || v->backend == NULL) {
        return flags;
    }
    /* The sweep runs before the query short-circuit: a parked notice ages on
     * the clock, not on what kind of view is holding it, and one parked in a
     * query view would otherwise sit for ever. */
    if (tiku_park_expire(&v->park, now) > 0) {
        flags |= TIKU_TRK_NOTIFY_REDRAW;
    }
    if (v->is_query) {
        /* A query's rows come from everywhere, so there is no directory to
         * re-list; the owner re-runs the query and hands the results to
         * tiku_trk_poseview_apply_results(), which goes through the same
         * handler this does. */
        return flags;
    }
    /*
     * Ask the backend what changed before re-reading anything.  One round
     * trip answers "did anything happen at all" whatever the size of the
     * namespace, so a quiet device costs one question per tick instead of a
     * directory listing per open window.  A backend that cannot tell
     * answers -1 and the re-listing follows, which is what a local
     * filesystem does.
     */
    if (pv_poll_ring(v, now, ch, nch, complete, &flags, &out)) {
        return out;
    }

    was = calloc(POSES_MAX, sizeof *was);
    is = calloc(POSES_MAX, sizeof *is);
    if (was == NULL || is == NULL) {
        free(was);
        free(is);
        return flags;
    }
    nwas = 0;
    for (i = 0; i < v->count; i++) {
        /* An adopted pose is left out of the comparison entirely: it does
         * not live in this directory, so a re-listing that cannot find it
         * is not evidence that it has gone. */
        if (!v->poses[i].adopted) {
            was[nwas++] = v->poses[i].model;
        }
    }
    r.set = is;
    r.n = 0;
    r.max = POSES_MAX;
    r.view = v;
    if (v->backend->ops->list(v->backend, v->path, relist_one, &r) < 0) {
        /* The listing failed: the folder may be gone.  Ask about the folder
         * rather than concluding it from a failed read. */
        tiku_model_t self;

        if (v->backend->ops->stat != NULL &&
            v->backend->ops->stat(v->backend, v->path, &self) != 0 &&
            strcmp(v->path, "/") != 0) {
            flags |= TIKU_TRK_NOTIFY_GONE;
        }
        free(was);
        free(is);
        return flags;
    }
    tiku_evlist_clear(&evs);
    (void)tiku_notify_diff(was, nwas, is, r.n, now, &evs);
    flags |= tiku_trk_poseview_notify_all(v, &evs);
    if (evs.overflowed) {
        /* More happened than the batch could carry.  Re-reading wholesale is
         * the honest response; pretending the batch was complete would leave
         * rows that no longer exist. */
        (void)tiku_trk_poseview_reload(v);
        flags |= TIKU_TRK_NOTIFY_REDRAW;
    }
    free(was);
    free(is);
    /* Rows that arrived ABOVE the viewport move the listing under the
     * reader.  The batch's worth of them is applied once here, so a folder
     * that gains ten files while someone reads the middle of it does not
     * walk down the screen ten times (PVL-046). */
    if (v->repaint.scroll_delta != 0 && v->mode == TIKU_TRK_MODE_LIST) {
        int rh = (v->row_h > 0) ? v->row_h : 1;
        int rows = v->repaint.scroll_delta / rh;

        v->repaint.scroll_delta -= rows * rh;
        if (rows != 0) {
            tiku_trk_poseview_scroll_to(v, v->scroll + rows);
            flags |= TIKU_TRK_NOTIFY_REDRAW;
        }
    }
    return flags;
}

/**
 * @brief A node changed, or an attribute did: its row re-read, its place kept.
 * @return 1 with the verdict in @p out when the notice is settled here, 0 to go
 * on
 */
int
pv_notify_changed(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                  unsigned *flags, int *at, unsigned *out)
{
    (void)out;
    /* The shells rebuild Open With/Add-ons from the focused view on
     * every menu open; this serial makes a MIME/type notice observable
     * even when the row remains in place. */
    v->menu_generation++;
    (*at) = pv_pose_index(v, e->path);
    if ((*at) < 0) {
        /* The row is not here yet.  Park it rather than lose it: a file
         * altered the instant after it was created would otherwise show
         * the state it was born with. */
        (void)tiku_park_add(&v->park, e);
        return 0;
    }
    {
        /* Only the affected columns refresh (MA-072): a notice that
         * NAMES its fields is compared column by column -- the value
         * AND the fitted text must both have moved before a repaint
         * is owed -- while one that cannot say re-reads everything,
         * which is what every notice meant before the mask. */
        unsigned f = e->fields;
        int repaint = (f == 0u) || (f & TIKU_EVF_KIND) != 0u;
        tiku_trk_pose_t before = v->poses[(*at)];

        v->poses[(*at)].model = e->model;
        (void)tiku_desc_parse(e->model.facts.meta,
                                  &v->poses[(*at)].desc);
        if (f == 0u || (f & TIKU_EVF_VALUE) != 0u) {
            v->poses[(*at)].value_valid = 0;
        }
        if (!repaint) {
            int c;

            for (c = 0; c < v->column_count; c++) {
                int field = v->columns[c].field;
                int width = v->columns[c].width;
                char oldbuf[96], newbuf[96];
                const char *was_text, *now_text;

                if ((column_fields(field) & f) == 0u) {
                    continue;
                }
                was_text = pv_cell_text_at(v, &before, field, width,
                                        oldbuf, sizeof oldbuf);
                now_text = pv_cell_text_at(v, &v->poses[(*at)], field, width,
                                        newbuf, sizeof newbuf);
                if (strcmp(was_text, now_text) != 0) {
                    repaint = 1;
                    break;
                }
            }
        }
        if (repaint) {
            (*flags) |= TIKU_TRK_NOTIFY_REDRAW;
            /* One row changed, so one row is what needs painting
             * again: the icon, the size and the date all live inside
             * it (PVL-075). */
            tiku_trk_poseview_damage_row(v, (*at));
        }
        /* The re-slot check is owed only when the changed FIELD is a
         * sort key (PVL-047): re-checking on every notice churned the
         * listing, and a notice that cannot say stays conservative. */
        if (f != 0u &&
            (column_fields(v->sort_field) & f) == 0u &&
            (v->sort2_field < 0 ||
             (column_fields(v->sort2_field) & f) == 0u)) {
            return 0;
        }
    }
    {
        /* The changed fact may be the one the listing is ORDERED by, and
         * a row that keeps its slot while its key moves leaves the
         * listing quietly unsorted (_CheckPoseSortOrder). */
        tiku_trk_pose_t moved = v->poses[(*at)];
        int slot = pv_sorted_slot(v, &moved);
        int scroll_was = v->scroll;

        if (slot != (*at) && slot != (*at) + 1) {
            int now_at;
            int was_active = (v->active == (*at));

            pv_remove_pose(v, (*at));
            v->scroll = scroll_was;
            now_at = pv_insert_sorted(v, &moved);
            if (now_at >= 0) {
                v->poses[now_at].selected = moved.selected;
                v->poses[now_at].sel_time = moved.sel_time;
                if (was_active) {
                    v->active = now_at;
                }
            }
        }
    }
    return 0;
}

/**
 * @brief A node moved: renamed in place, arrived from elsewhere, or gone
 * elsewhere.  @return 1 with the verdict in @p out when the notice is settled
 * here, 0 to go on
 */
int
pv_notify_moved(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                unsigned *flags, int *at, unsigned *out)
{
    v->menu_generation++;
    (*at) = (e->from[0] != '\0') ? pv_pose_index(v, e->from) : -1;
    if ((*at) < 0) {
        (*at) = pv_pose_index(v, e->path);
    }
    if ((*at) < 0) {
        /* Moved IN from elsewhere: that is a creation here. */
        tiku_node_event_t as_new = *e;

        as_new.op = TIKU_EV_CREATED;
        *out = tiku_trk_poseview_notify(v, &as_new); return 1;
    }
    /* Moved OUT of this folder is a removal -- unless this is a query or
     * a virtual listing, whose rows have no single parent, so every move
     * is a rename in place. */
    if (!v->is_query && strcmp(e->dir, v->path) != 0) {
        /* Moved out of this folder: the same thing as a removal, so it
         * must leave by the same door.  Returning here skipped the tail
         * that rebuilds the filtered list, and a filtered view was left
         * reporting a row it no longer had. */
        pv_remove_pose(v, (*at));
        (*flags) |= TIKU_TRK_NOTIFY_REDRAW;
        return 0;
    }
    {
        /* A rename re-reads EVERY column, not just the name: the notice
         * says the entry changed identity, and other notices for it may
         * have been missed while it had none. */
        tiku_trk_pose_t moved = v->poses[(*at)];
        int was_selected = moved.selected;
        int was_active = (v->active == (*at));
        int now_at;

        int scroll_was = v->scroll;

        moved.model = e->model;
        (void)tiku_desc_parse(e->model.facts.meta, &moved.desc);
        moved.value_valid = 0;
        pv_remove_pose(v, (*at));
        /* remove+insert is an implementation of RENAME, and a rename
         * changes nothing about what is visible -- the scroll correction
         * belongs to removal and must not leak in here. */
        v->scroll = scroll_was;
        /* Back to its sorted place, without re-sorting the folder. */
        now_at = pv_insert_sorted(v, &moved);
        if (now_at >= 0) {
            v->poses[now_at].selected = was_selected;
            if (was_active) {
                v->active = now_at;
            }
            if ((was_selected || was_active) &&
                v->mode == TIKU_TRK_MODE_LIST &&
                v->visible_rows > 0) {
                /* The APPLICATION moved the selection, so it is the
                 * application's job to keep it visible (NeXT UIG ch3:
                 * never move the selection out of view) -- a rename
                 * that re-sorts the row past the viewport follows it
                 * there.  A user's own scrolling is never chased. */
                tiku_trk_poseview_scroll_into_view(v, now_at,
                                                   v->visible_rows);
            }
        }
        (*flags) |= TIKU_TRK_NOTIFY_REDRAW;
        {
            /* A notice can be parked under the name the row has just
             * taken -- something changed it while it was called
             * something else -- so a rename into place replays as a
             * creation does. */
            tiku_node_event_t held[8];
            int n, k;

            do {
                n = tiku_park_take(&v->park, e->path, held,
                                       (int)(sizeof held /
                                             sizeof held[0]));
                for (k = 0; k < n; k++) {
                    (*flags) |= tiku_trk_poseview_notify(v, &held[k]);
                }
            } while (n > 0);
        }
    }
    return 0;
}

/**
 * @brief A node removed: its row taken out and the selection mended.  @return 1
 * with the verdict in @p out when the notice is settled here, 0 to go on
 */
int
pv_notify_removed(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                  unsigned *flags, int *at, unsigned *out)
{
    (void)out;
    v->menu_generation++;
    (*at) = pv_pose_index(v, e->path);
    if ((*at) < 0) {
        /* It may only ever have been a zombie; that still goes. */
        int z;

        for (z = 0; z < v->zombie_count; z++) {
            if (strcmp(v->zombie[z].path, e->path) == 0) {
                int m;

                for (m = z; m < v->zombie_count - 1; m++) {
                    v->zombie[m] = v->zombie[m + 1];
                }
                v->zombie_count--;
                (*flags) |= TIKU_TRK_NOTIFY_REDRAW;
                break;
            }
        }
        return 0;
    }
    pv_remove_pose(v, (*at));
    (*flags) |= TIKU_TRK_NOTIFY_REDRAW;
    return 0;
}

/**
 * @brief A node created: a row inserted where the sort puts it, or the listing
 * marked for a re-read.  @return 1 with the verdict in @p out when the notice
 * is settled here, 0 to go on
 */
int
pv_notify_created(tiku_trk_poseview_t *v, const tiku_node_event_t *e,
                  unsigned *flags, int *at, unsigned *out)
{
    v->menu_generation++;
    /* A file made in some other folder is not this folder's business,
     * and a duplicate notice must not produce a second row. */
    if (!v->is_query && strcmp(e->dir, v->path) != 0) {
        *out = TIKU_TRK_NOTIFY_NOTHING; return 1;
    }
    if (pv_pose_index(v, e->path) >= 0) {
        *out = TIKU_TRK_NOTIFY_NOTHING; return 1;
    }
    {
        /* The same node under another name is not a second entry: the
         * path is a name, the identity is the node, and a notice can
         * arrive under either.  The held tier is consulted for the same
         * reason -- an entry being held is not one waiting to be
         * discovered again. */
        int k;

        if (e->model.node_id != 0) {
            for (k = 0; k < v->count; k++) {
                if (v->poses[k].model.node_id == e->model.node_id) {
                    *out = TIKU_TRK_NOTIFY_NOTHING; return 1;
                }
            }
        }
        for (k = 0; k < v->zombie_count; k++) {
            if (strcmp(v->zombie[k].path, e->path) == 0) {
                *out = TIKU_TRK_NOTIFY_NOTHING; return 1;
            }
        }
    }
    {
        tiku_trk_pose_t p;
        tiku_node_event_t held[8];
        int n, k;

        memset(&p, 0, sizeof p);
        p.model = e->model;
        (void)tiku_desc_parse(e->model.facts.meta, &p.desc);
        {
            int lx, ly;

            if (v->mode != TIKU_TRK_MODE_LIST &&
                pv_take_landing(v, e->path, &lx, &ly)) {
                /* Dropped here: the position was worked out at the
                 * drop, from the shape the selection had when it was
                 * picked up (PVS-045, PVL-027). */
                float scale = pv_mode_scale(v->mode, v->icon_size);

                p.loc_x = (int)((float)lx / scale);
                p.loc_y = (int)((float)ly / scale);
                p.placed = 1;
                p.auto_placed = 0;
            }
        }
        if (v->drop_valid && v->mode != TIKU_TRK_MODE_LIST && !p.placed) {
            /* The position was asked for BEFORE the entry existed -- at
             * the click that made it -- and is consumed by the first
             * entry to appear.  Placing it afterwards would let
             * auto-placement put it somewhere else first and then jump
             * (PVL-025, PVL-098). */
            float scale = pv_mode_scale(v->mode, v->icon_size);

            p.loc_x = (int)((float)v->drop_x / scale);
            p.loc_y = (int)((float)v->drop_y / scale);
            p.placed = 1;
            p.auto_placed = 0;
            v->drop_valid = 0;
        }
        if (pv_pose_is_hidden(v, e->path)) {
            /* Marked invisible in its saved record: it exists and it is
             * deliberately not shown, so the notice is consumed rather
             * than producing a row. */
            *out = TIKU_TRK_NOTIFY_NOTHING; return 1;
        }
        if (pv_insert_sorted(v, &p) < 0) {
            *out = TIKU_TRK_NOTIFY_NOTHING; return 1;
        }
        /* PVL-090: the insertion set is shared with the loader.  A node
         * this notice brought in early must be recorded here, or the
         * loader reaching the same entry adds it a second time. */
        pv_note_inserted(v, e->model.node_id);
        (*flags) |= TIKU_TRK_NOTIFY_REDRAW;
        /* Anything that arrived for this node before it existed is
         * replayed now, in the order it came. */
        /* Drained in batches until empty: stopping at one bufferful
         * would strand the NEWEST notices, which are the ones that say
         * what the row looks like now. */
        do {
            n = tiku_park_take(&v->park, e->path, held,
                                   (int)(sizeof held / sizeof held[0]));
            for (k = 0; k < n; k++) {
                (*flags) |= tiku_trk_poseview_notify(v, &held[k]);
            }
        } while (n > 0);
    }
    return 0;
}

/**
 * @brief the change ring answered: each notice applied, or the listing marked
 * for a re-read.  @return 1 with the answer in @p out when this settled it
 */
int
pv_poll_ring(tiku_trk_poseview_t *v, int64_t now, const tiku_model_change_t *ch,
             int nch, int complete, unsigned *flags, unsigned *out)
{
    if (nch >= 0) {
        if (nch == 0 && complete) {
            /* Nothing happened anywhere, and the device is sure of it: one
             * question served every open window. */
            *out = (*flags); return 1;
        }
        {
            int i, k, relist = !complete;

            for (i = 0; i < nch; i++) {
                int held = 0;

                for (k = 0; k < v->count; k++) {
                    if (v->poses[k].model.node_id != 0 &&
                        v->poses[k].model.node_id == ch[i].id) {
                        held = 1;
                        if (ch[i].op == TIKU_CH_CHANGED) {
                            /* Through the SAME handler everything else uses:
                             * an inline poke here was a second update path,
                             * and a second path is how a query view and a
                             * folder view drift apart.  The model is
                             * re-stat'ed so the descriptor and facts move
                             * with the value. */
                            tiku_node_event_t ev;

                            memset(&ev, 0, sizeof ev);
                            ev.op = TIKU_EV_ATTR;
                            ev.at = now;
                            snprintf(ev.path, sizeof ev.path, "%s",
                                     v->poses[k].model.path);
                            snprintf(ev.dir, sizeof ev.dir, "%s", v->path);
                            if (v->backend->ops->stat == NULL ||
                                v->backend->ops->stat(v->backend, ev.path,
                                                      &ev.model) != 0) {
                                ev.model = v->poses[k].model;
                            }
                            (*flags) |= tiku_trk_poseview_notify(v, &ev);
                        } else {
                            /* Created, removed or renamed changes the SHAPE
                             * of the listing, and only a re-read can say
                             * what it looks like now.  Treating it as
                             * handled here is what leaves a deleted row on
                             * screen. */
                            relist = 1;
                        }
                    }
                }
                if (!held) {
                    /*
                     * A node this view does not hold.  It may be about to
                     * appear here, so the listing is re-read -- and the
                     * notice is turned into a real ATTR event first, keyed
                     * on the node's own identity, so it can be PARKED and
                     * replayed when the row arrives.  Without this the
                     * parking machinery is unreachable by anything except a
                     * test: a diff of two listings can only ever describe
                     * nodes the view already has.
                     */
                    if (ch[i].op == TIKU_CH_CHANGED &&
                        v->backend->ops->stat != NULL) {
                        tiku_model_t m;
                        char idpath[TIKU_PATH_MAX];

                        /* The record names an identity, not a path.  Only
                         * the backend can turn one into the other. */
                        if (tiku_trk_poseview_path_of_id(v, ch[i].id, idpath,
                                                         sizeof idpath) == 0 &&
                            v->backend->ops->stat(v->backend, idpath,
                                                  &m) == 0) {
                            tiku_node_event_t ev;

                            memset(&ev, 0, sizeof ev);
                            ev.op = TIKU_EV_ATTR;
                            ev.model = m;
                            ev.at = now;
                            snprintf(ev.path, sizeof ev.path, "%s", idpath);
                            {
                                /* The notice is only this view's business if
                                 * the node belongs to the folder it shows.
                                 * Parking a change to some other directory
                                 * would hold it here for ten seconds and
                                 * then drop it, which is neither use nor
                                 * ornament. */
                                const char *slash = strrchr(idpath, '/');
                                size_t dlen = (slash != NULL && slash != idpath)
                                                  ? (size_t)(slash - idpath)
                                                  : 1u;

                                if (dlen < sizeof ev.dir) {
                                    memcpy(ev.dir, idpath, dlen);
                                    ev.dir[dlen] = '\0';
                                    if (dlen == 1u) {
                                        ev.dir[0] = '/';
                                    }
                                }
                                if (strcmp(ev.dir, v->path) == 0) {
                                    (*flags) |=
                                        tiku_trk_poseview_notify(v, &ev);
                                }
                            }
                        }
                    }
                    relist = 1;
                }
            }
            if (!relist) {
                *out = (*flags); return 1;
            }
        }
    }
    return 0;
}
