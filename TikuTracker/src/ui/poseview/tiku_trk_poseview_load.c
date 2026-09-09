/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_load.c - filling the list from the backend, and
 * refreshing it
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/* Every view that exists, so a listing fetched elsewhere can be told
 * whether the view it was for is still there to take it. */
#define VIEWS_MAX 256
static tiku_trk_poseview_t *views[VIEWS_MAX];
static tiku_trk_poseview_lister_fn lister;
static void *lister_ctx;

/** @brief Note a view that now exists. */
static void
views_add(tiku_trk_poseview_t *v)
{
    int i;

    for (i = 0; i < VIEWS_MAX; i++) {
        if (views[i] == NULL) {
            views[i] = v;
            return;
        }
    }
}

/** @brief Forget a view that is going. */
static void
views_drop(const tiku_trk_poseview_t *v)
{
    int i;

    for (i = 0; i < VIEWS_MAX; i++) {
        if (views[i] == v) {
            views[i] = NULL;
        }
    }
}

int
tiku_trk_poseview_alive(const tiku_trk_poseview_t *v)
{
    int i;

    for (i = 0; v != NULL && i < VIEWS_MAX; i++) {
        if (views[i] == v) {
            return 1;
        }
    }
    return 0;
}

void
tiku_trk_poseview_set_lister(tiku_trk_poseview_lister_fn fn, void *ctx)
{
    lister = fn;
    lister_ctx = ctx;
}

int
tiku_trk_poseview_lister_takes(tiku_trk_poseview_t *v, int kind)
{
    return lister != NULL && lister(lister_ctx, v, v->load_gen, kind);
}

/**
 * @brief Take one listed entry as a pose, hiding dotfiles when asked and
 *        holding an entry the backend could not describe.  @return 1 to
 *        stop the walk when the view is full.
 */
int
pv_collect(const tiku_model_t *m, void *ctx)
{
    tiku_trk_poseview_t *v = ctx;

    if (v->hide_dotfiles && m != NULL && m->name[0] == '.') {
        return 0;
    }
    if (v->count >= POSES_MAX) {
        return 1;
    }
    if (m->kind == TIKU_KIND_UNKNOWN) {
        /*
         * The backend could not describe it -- busy, mid-write, a transient
         * error.  It is HELD rather than shown: a row that says "unknown"
         * is a promise the view cannot keep, and dropping it would make the
         * entry vanish and later reappear as though newly created.  It is
         * promoted the moment any listing describes it.
         */
        if (v->zombie_count < (int)(sizeof v->zombie / sizeof v->zombie[0])) {
            int z;

            for (z = 0; z < v->zombie_count; z++) {
                if (strcmp(v->zombie[z].path, m->path) == 0) {
                    return 0;
                }
            }
            v->zombie[v->zombie_count++] = *m;
        }
        return 0;
    }
    {
        /* Describable now: if it was being held, it stops being held. */
        int z;

        for (z = 0; z < v->zombie_count; z++) {
            if (strcmp(v->zombie[z].path, m->path) == 0) {
                int k;

                for (k = z; k < v->zombie_count - 1; k++) {
                    v->zombie[k] = v->zombie[k + 1];
                }
                v->zombie_count--;
                break;
            }
        }
    }
    memset(&v->poses[v->count], 0, sizeof v->poses[0]);
    v->poses[v->count].model = *m;
    /* Resolve the type once here rather than on every draw: it is a property
     * of the node, and it decides formatting, editability and sort order. */
    (void)tiku_desc_parse(m->facts.meta, &v->poses[v->count].desc);
    v->poses[v->count].index = v->count;
    v->count++;
    return 0;
}

/** @brief Drop poses marked invisible, after their state has been read. */
void
pv_apply_visibility(tiku_trk_poseview_t *v)
{
    int i, w = 0;

    for (i = 0; i < v->count; i++) {
        if (v->poses[i].invisible) {
            continue;
        }
        v->poses[w++] = v->poses[i];
    }
    for (i = 0; i < w; i++) {
        v->poses[i].index = i;
    }
    v->count = w;
}

/** @brief Reject a saved scroll origin that could place the view nowhere. */
int
pv_sane_scroll_origin(int value, int allow_negative)
{
    if (value < -TIKU_TRK_SANE_LOCATION ||
        value > TIKU_TRK_SANE_LOCATION) {
        return 0;
    }
    if (!allow_negative && value < 0) {
        return 0;
    }
    return value;
}

/** @brief Natural order: digits inside names compare by value, not by digit. */
static int
natural_cmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            long x = 0, y = 0;
            while (*a >= '0' && *a <= '9') { x = x * 10 + (*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { y = y * 10 + (*b++ - '0'); }
            if (x != y) {
                return (x < y) ? -1 : 1;
            }
            continue;
        }
        {
            int ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') { ca += 32; }
            if (cb >= 'A' && cb <= 'Z') { cb += 32; }
            if (ca != cb) {
                return (ca < cb) ? -1 : 1;
            }
        }
        a++;
        b++;
    }
    return (*a == '\0') ? ((*b == '\0') ? 0 : -1) : 1;
}

tiku_trk_poseview_t *pv_g_sorting;

/** @brief Order two poses on one field, without the tie-breaks. */
static int
field_cmp(const tiku_trk_pose_t *a, const tiku_trk_pose_t *b, int field)
{
    switch (field) {
    case TIKU_TRK_COL_KIND:
        return strcmp(tiku_model_kind_string(&a->model),
                      tiku_model_kind_string(&b->model));
    case TIKU_TRK_COL_SIZE:
        return (a->model.facts.size < b->model.facts.size) ? -1
               : (a->model.facts.size > b->model.facts.size) ? 1 : 0;
    case TIKU_TRK_COL_VALUE:
        /* Through the descriptor, so a byte column orders 9 B before 10 KiB
         * rather than ordering the text it happens to be shown as. */
        return tiku_attr_compare(&a->desc, a->value, b->value);
    case TIKU_TRK_COL_CAP:
        return strcmp(a->model.facts.req_cap, b->model.facts.req_cap);
    case TIKU_TRK_COL_DATE:
        return (a->model.facts.mtime < b->model.facts.mtime) ? -1
               : (a->model.facts.mtime > b->model.facts.mtime) ? 1 : 0;
    case TIKU_TRK_COL_NAME:
        return natural_cmp(tiku_model_display_name(&a->model),
                           tiku_model_display_name(&b->model));
    default:
        return 0;
    }
}

/**
 * @brief The sort order: containers first when that setting is on, then
 *        the sort field, then the secondary; reads the view in pv_g_sorting.
 */
int
pv_pose_cmp(const void *pa, const void *pb)
{
    const tiku_trk_pose_t *a = pa, *b = pb;
    int r;

    /* Containers first, then by the sort field: a folder is not just a big
     * file, and mixing them makes a listing unreadable.  It is a SETTING,
     * though -- the user can ask for one list sorted by the column alone
     * (MA-039). */
    if (pv_g_sorting->folders_first) {
        /* The full ladder, not one bit: the Disks root beats everything,
         * real volumes beat folders, folders beat files -- and a volume
         * whose capacity reads zero is DEMOTED to the folder bucket, the
         * way the source sorts a volume that cannot say its size
         * (MA-009). */
        int ba = (a->model.kind == TIKU_KIND_ROOT) ? 3
               : (a->model.kind == TIKU_KIND_VOLUME &&
                  a->model.facts.total > 0u)           ? 2
               : tiku_model_is_container(&a->model) ? 1 : 0;
        int bb = (b->model.kind == TIKU_KIND_ROOT) ? 3
               : (b->model.kind == TIKU_KIND_VOLUME &&
                  b->model.facts.total > 0u)           ? 2
               : tiku_model_is_container(&b->model) ? 1 : 0;

        if (ba != bb) {
            return bb - ba;
        }
    }
    /* An entry with no value for the sort field sorts LAST whichever way
     * the column is ordered.  Decided here rather than inside the field
     * comparison, because the reverse below negates everything it returns
     * and "last" is not a value to be turned around. */
    if (pv_g_sorting->sort_field == TIKU_TRK_COL_VALUE) {
        int ea = (a->value[0] == '\0'), eb = (b->value[0] == '\0');

        if (ea != eb) {
            return ea ? 1 : -1;
        }
    }
    r = field_cmp(a, b, pv_g_sorting->sort_field);
    if (r == 0 && pv_g_sorting->sort2_field >= 0 &&
        pv_g_sorting->sort2_field != pv_g_sorting->sort_field) {
        r = field_cmp(a, b, pv_g_sorting->sort2_field);
    }
    if (r == 0) {
        r = natural_cmp(tiku_model_display_name(&a->model),
                        tiku_model_display_name(&b->model));
    }
    return pv_g_sorting->sort_reverse ? -r : r;
}

/** @brief Sort the poses and renumber their indexes. */
void
pv_resort(tiku_trk_poseview_t *v)
{
    int i;

    pv_g_sorting = v;
    qsort(v->poses, (size_t)v->count, sizeof v->poses[0], pv_pose_cmp);
    for (i = 0; i < v->count; i++) {
        v->poses[i].index = i;
    }
    pv_g_sorting = NULL;
}

/**
 * @brief Stage one entry from the enumeration.
 *
 * Zero CONTINUES the walk and non-zero stops it, which is the backend's
 * contract and the opposite of what a "did it work" return would read as.
 */
static int
stage_one(const tiku_model_t *m, void *ctx)
{
    tiku_trk_poseview_t *v = ctx;

    if (v->stage_count >= POSES_MAX) {
        return 1;                       /* full: stop the walk            */
    }
    if (v->hide_dotfiles && m != NULL && m->name[0] == '.') {
        return 0;
    }
    v->stage[v->stage_count++] = *m;
    return 0;
}

/** @brief Whether this node has already been published in this load. */
static int
already_inserted(const tiku_trk_poseview_t *v, uint64_t id)
{
    int i;

    if (id == 0) {
        return 0;                       /* no identity, no claim           */
    }
    for (i = 0; i < v->inserted_count; i++) {
        if (v->inserted[i] == id) {
            return 1;
        }
    }
    return 0;
}

/** @brief Remember @p id as inserted this load, once. */
void
pv_note_inserted(tiku_trk_poseview_t *v, uint64_t id)
{
    if (id != 0 && v->inserted_count < POSES_MAX &&
        !already_inserted(v, id)) {
        v->inserted[v->inserted_count++] = id;
    }
}

int
tiku_trk_poseview_generation(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->load_gen : 0;
}

int
tiku_trk_poseview_loading(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->loading : 0;
}

int
tiku_trk_poseview_pole(const tiku_trk_poseview_t *v, int64_t now)
{
    if (v == NULL || !v->loading) {
        return 0;
    }
    /* Armed, not shown: a folder that loads in a blink must not flash a
     * pole at the user, so the arming time has to pass first. */
    return (now >= v->pole_at) ? 1 : 0;
}

int
tiku_trk_poseview_load_begin(tiku_trk_poseview_t *v, int64_t now)
{
    if (v == NULL || v->backend == NULL) {
        return -1;
    }
    /* The generation moves FIRST.  Anything still in flight from the last
     * load is stamped with the old one from here on and will be refused. */
    v->load_gen++;
    v->count = 0;
    v->pivot = -1;
    v->active = -1;
    v->last_added = -1;
    v->inserted_count = 0;
    v->stage_count = 0;
    v->stage_at = 0;
    if (v->is_query) {
        v->loading = 0;
        return v->count;
    }
    v->awaiting = 0;
    if (tiku_trk_poseview_lister_takes(v, TIKU_TRK_LIST_LOAD)) {
        /* Fetched elsewhere: the window shows its pole and publishes
         * when the entries land. */
        v->awaiting = 1;
        v->loading = 1;
        v->pole_at = now + TIKU_TRK_POLE_AFTER_US;
        v->next_chunk = 0;
        return 0;
    }
    if (v->backend->ops->list(v->backend, v->path, stage_one, v) < 0) {
        v->loading = 0;
        return -1;
    }
    v->loading = 1;
    v->pole_at = now + TIKU_TRK_POLE_AFTER_US;
    /* Zero, so the first row is published on its own and the window shows
     * something at once; the batching starts from the second. */
    v->next_chunk = 0;
    return v->stage_count;
}

int
tiku_trk_poseview_load_step(tiku_trk_poseview_t *v, int64_t now)
{
    int published = 0;

    if (v == NULL || !v->loading) {
        return 0;
    }
    if (v->awaiting) {
        return 1;                       /* still loading; nothing to publish */
    }
    while (v->stage_at < v->stage_count) {
        tiku_model_t *m = &v->stage[v->stage_at];

        /* By identity where the store has one, and by path where it does
         * not: a backend without node ids still must not list a row twice. */
        if (already_inserted(v, m->node_id) || pv_pose_index(v, m->path) >= 0 ||
            pv_pose_is_hidden(v, m->path)) {
            /* A node the change notices already brought in, or one marked
             * not to be shown: consumed, not published. */
            v->stage_at++;
            continue;
        }
        {   /* collect() answers the WALK, not the caller: whether a row
             * appeared is the count moving, not its return value. */
            int before = v->count;

            (void)pv_collect(m, v);
            v->stage_at++;
            if (v->count == before) {
                continue;               /* held as a zombie, or no room   */
            }
        }
        pv_note_inserted(v, m->node_id);
        published++;
        if (published >= TIKU_TRK_LOAD_CHUNK ||
            (v->next_chunk != 0 && now > v->next_chunk)) {
            break;
        }
        if (v->next_chunk == 0) {
            break;                      /* the first row goes alone        */
        }
    }
    v->next_chunk = now + TIKU_TRK_LOAD_CHUNK_US;
    pv_resort(v);
    if (v->filtering) {
        pv_rebuild_filter(v);
    }
    if (v->stage_at >= v->stage_count) {
        v->loading = 0;                 /* AddPosesCompleted               */
        v->stage_count = 0;
        v->stage_at = 0;
        return 0;
    }
    return 1;
}

int
tiku_trk_poseview_reload(tiku_trk_poseview_t *v)
{
    /* A reload is a fresh set of poses, so the placement hint starts over
     * with them: kept, it would place the first new icon under whatever
     * the last listing left behind (PVL-017). */
    tiku_trk_poseview_reset_hint(v);
    /* And the armed rename goes with them: the arm names a pose INDEX,
     * and over a fresh listing that index is whatever happens to land
     * there -- an arm kept across a reload opened an editor on the row
     * that slid into the armed slot (MA-043). */
    tiku_trk_poseview_cancel_edit_wait(v);
    if (v == NULL || v->backend == NULL) {
        return -1;
    }
    if (v->is_query) {
        return v->count;      /* results do not come from a directory    */
    }
    /* The same path the batched loader takes, drained in one go: two ways
     * of building a listing is two places for it to differ. */
    if (tiku_trk_poseview_load_begin(v, 0) < 0) {
        return -1;
    }
    if (v->awaiting) {
        return v->count;        /* the rest lands, and is stepped, later */
    }
    while (tiku_trk_poseview_load_step(v, TIKU_TRK_LOAD_CHUNK_US * 2)) {
    }
    pv_resort(v);
    {
        /* The invisible bit rides on the saved records, and this path does
         * not read them wholesale -- so each entry is asked individually.
         * Without this, poll()'s overflow reload resurrects every hidden
         * row until the next restore. */
        int i2, w = 0;

        for (i2 = 0; i2 < v->count; i2++) {
            if (pv_pose_is_hidden(v, v->poses[i2].model.path)) {
                continue;
            }
            v->poses[w++] = v->poses[i2];
        }
        for (i2 = 0; i2 < w; i2++) {
            v->poses[i2].index = i2;
        }
        v->count = w;
    }
    /* The filter indexes INTO the pose list, so a listing replaced under it
     * leaves those indices pointing at rows that are gone -- which shows as
     * the previous folder's entries listed inside this one. */
    if (v->filtering) {
        pv_rebuild_filter(v);
    } else {
        v->filtered_count = 0;
    }
    (void)tiku_trk_poseview_refresh_values(v, 0, 64);
    return v->count;
}

/** @brief Fill from a model set; a query view also says it is one. */
static void
query_attr_column(tiku_trk_poseview_t *v, const char *attr,
                  const char *title, int width)
{
    int field = tiku_trk_poseview_attr_column(v, attr, title, width);

    if (field >= 0 && v->column_count < TIKU_TRK_COLUMNS_MAX) {
        v->columns[v->column_count++] = (tiku_trk_column_t){ title, width,
            field, 0, TIKU_TRK_ALIGN_LEFT };
    }
}

/** @brief Built-in layout used only when this type has no saved template. */
static void
query_default_columns(tiku_trk_poseview_t *v)
{
    int bookmark, person, mail;

    bookmark = strcasecmp(v->query_type,
                          "application/x-vnd.Be-bookmark") == 0;
    person = strcasecmp(v->query_type, "application/x-person") == 0;
    mail = strcasecmp(v->query_type, "text/x-email") == 0 ||
           strcasecmp(v->query_type, "application/x-vnd.Be-mail") == 0;
    v->columns[0] = (tiku_trk_column_t){ tiku_str("label.name"), 145,
        TIKU_TRK_COL_NAME, 0, TIKU_TRK_ALIGN_LEFT };
    v->columns[1] = (tiku_trk_column_t){ tiku_str("label.location"), 225,
        TIKU_TRK_COL_PATH, 0, TIKU_TRK_ALIGN_LEFT };
    v->column_count = 2;
    if (bookmark) {
        query_attr_column(v, "META:title", tiku_str("col.title"), 180);
        query_attr_column(v, "META:url", tiku_str("col.url"), 260);
    } else if (person) {
        query_attr_column(v, "META:name", tiku_str("label.name"), 160);
        query_attr_column(v, "META:nickname", tiku_str("col.nickname"), 110);
        query_attr_column(v, "META:email", tiku_str("col.email"), 190);
        query_attr_column(v, "META:phone", tiku_str("col.phone"), 120);
    } else if (mail) {
        query_attr_column(v, "MAIL:subject", tiku_str("col.subject"), 220);
        query_attr_column(v, "MAIL:from", tiku_str("col.from"), 170);
        query_attr_column(v, "MAIL:when", tiku_str("col.when"), 135);
        query_attr_column(v, "MAIL:status", tiku_str("col.status"), 90);
    } else {
        v->columns[2] = (tiku_trk_column_t){ tiku_str("label.size"), 80,
            TIKU_TRK_COL_SIZE, 0, TIKU_TRK_ALIGN_RIGHT };
        v->columns[3] = (tiku_trk_column_t){ tiku_str("label.modified"), 150,
            TIKU_TRK_COL_DATE, 0, TIKU_TRK_ALIGN_LEFT };
        v->column_count = 4;
    }
    tiku_trk_poseview_reflow_columns(v);
}

/**
 * @brief Replace the poses with @p n models, as a query's results when
 *        @p as_query says so.  @return -1 for a view or models that are
 *        missing, else 0.
 */
static int
fill_from(tiku_trk_poseview_t *v, const tiku_model_t *m, int n,
          int as_query)
{
    int first_query;
    int i;

    if (v == NULL || (m == NULL && n > 0)) {
        return -1;
    }
    first_query = as_query && !v->is_query;
    v->count = 0;
    v->pivot = -1;
    v->active = -1;
    v->last_added = -1;
    v->is_query = as_query ? 1 : 0;
    if (as_query) {
        /* Results are a report, not a place whose icons can be arranged.
         * Force their stable list shape before any result is inserted. */
        v->mode = TIKU_TRK_MODE_LIST;
        if (first_query) {
            query_default_columns(v);
            if (v->query_layout[0] != '\0') {
                (void)tiku_trk_poseview_restore_columns(v);
            }
        }
    }
    for (i = 0; i < n && v->count < POSES_MAX; i++) {
        memset(&v->poses[v->count], 0, sizeof v->poses[0]);
        v->poses[v->count].model = m[i];
        (void)tiku_desc_parse(m[i].facts.meta, &v->poses[v->count].desc);
        v->poses[v->count].index = v->count;
        v->count++;
    }
    pv_resort(v);
    if (v->filtering) {
        pv_rebuild_filter(v);
    } else {
        v->filtered_count = 0;
    }
    (void)tiku_trk_poseview_refresh_values(v, 0, 64);
    return v->count;
}

int
tiku_trk_poseview_set_results(tiku_trk_poseview_t *v,
                              const tiku_model_t *m, int n)
{
    return fill_from(v, m, n, 1);
}

int
tiku_trk_poseview_set_models(tiku_trk_poseview_t *v,
                             const tiku_model_t *m, int n)
{
    return fill_from(v, m, n, 0);
}

int
tiku_trk_poseview_adopt(tiku_trk_poseview_t *v, const tiku_model_t *m)
{
    tiku_trk_pose_t p;

    if (v == NULL || m == NULL) {
        return -1;
    }
    if (pv_pose_index(v, m->path) >= 0) {
        return -1;              /* already here: adopting twice is a copy */
    }
    memset(&p, 0, sizeof p);
    p.model = *m;
    (void)tiku_desc_parse(m->facts.meta, &p.desc);
    p.adopted = 1;
    if (pv_insert_sorted(v, &p) < 0) {
        return -1;
    }
    pv_note_inserted(v, m->node_id);
    return 0;
}

int
tiku_trk_poseview_is_query(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->is_query : 0;
}

void
tiku_trk_poseview_set_query_type(tiku_trk_poseview_t *v, const char *type)
{
    char *p;

    if (v == NULL) {
        return;
    }
    snprintf(v->query_type, sizeof v->query_type, "%s",
             (type != NULL) ? type : "");
    snprintf(v->query_layout, sizeof v->query_layout,
             "_trk/query-layout/%s",
             (type != NULL && type[0] != '\0') ? type : "all");
    for (p = v->query_layout + strlen("_trk/query-layout/"); *p != '\0'; p++) {
        if (*p == '/' || *p == '\\' || *p == ' ') {
            *p = '_';
        }
    }
}

void
tiku_trk_poseview_set_layout_store(tiku_trk_poseview_t *v,
                                   tiku_store_t *store)
{
    if (v != NULL) {
        v->layout_store = store;
    }
}

const char *
tiku_trk_poseview_layout_path(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return "";
    }
    return (v->query_layout[0] != '\0') ? v->query_layout : v->path;
}

tiku_trk_poseview_t *
tiku_trk_poseview_new(tiku_backend_t *backend, const char *path)
{
    tiku_trk_poseview_t *v = calloc(1, sizeof *v);

    if (v == NULL) {
        return NULL;
    }
    v->folders_first = 1;
    v->poses = calloc(POSES_MAX, sizeof *v->poses);
    v->filtered = calloc(POSES_MAX, sizeof *v->filtered);
    v->band_was = calloc(POSES_MAX, 1u);
    v->stage = calloc(POSES_MAX, sizeof *v->stage);
    v->inserted = calloc(POSES_MAX, sizeof *v->inserted);
    if (v->poses == NULL || v->filtered == NULL || v->band_was == NULL ||
        v->stage == NULL || v->inserted == NULL) {
        free(v->poses);
        free(v->filtered);
        free(v->band_was);
        free(v->stage);
        free(v->inserted);
        free(v);
        return NULL;
    }
        v->backend = backend;
    views_add(v);
    snprintf(v->path, sizeof v->path, "%s", path ? path : "/");
    v->last_icon = TIKU_TRK_MODE_ICON;
    v->pivot = -1;
    v->active = -1;
    v->last_added = -1;
    v->sort_field = TIKU_TRK_COL_NAME;
    v->sort2_field = -1;
    v->mode = TIKU_TRK_MODE_LIST;
    v->icon_size = 32;
    v->filter_count = 1;            /* always one, possibly empty, term    */
    v->selection_visible = 1;
    v->real_pivot = -1;
    v->edit_pose = -1;
    v->allow_editing = 1;
    v->font_px = TIKU_TRK_FONT_REF;
    tiku_trk_repaint_init(&v->repaint);

    v->columns[0] = (tiku_trk_column_t){ tiku_str("label.name"),  240, TIKU_TRK_COL_NAME,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->columns[1] = (tiku_trk_column_t){ tiku_str("label.kind"),  140, TIKU_TRK_COL_KIND,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->columns[2] = (tiku_trk_column_t){ tiku_str("label.value"), 190, TIKU_TRK_COL_VALUE,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->columns[3] = (tiku_trk_column_t){ tiku_str("label.cap"),    60, TIKU_TRK_COL_CAP,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->column_count = 4;
    tiku_trk_poseview_reflow_columns(v);

    (void)tiku_trk_poseview_reload(v);
    return v;
}

void
tiku_trk_poseview_free(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        views_drop(v);
        free(v->poses);
        free(v->filtered);
        free(v->band_was);
        free(v->stage);
        free(v->inserted);
        free(v);
    }
}

int
tiku_trk_poseview_switch_dir(tiku_trk_poseview_t *v, const char *path)
{
    if (v == NULL || path == NULL) {
        return -1;
    }
    tiku_trk_poseview_type_clear(v);
    tiku_trk_poseview_reset_hint(v);
    /* Everything the old folder was still waiting on goes with it: a notice
     * parked for a node in the folder just left would otherwise be replayed
     * into this one if a name happened to match, and would sit in the park
     * until it aged out either way. */
    tiku_park_clear(&v->park);
    v->zombie_count = 0;
    snprintf(v->path, sizeof v->path, "%s", path);
    v->scroll = 0;
    return tiku_trk_poseview_reload(v);
}

int
tiku_trk_poseview_go_up(tiku_trk_poseview_t *v)
{
    char parent[TIKU_PATH_MAX];
    char *slash;

    if (v == NULL || strcmp(v->path, "/") == 0) {
        return -1;
    }
    snprintf(parent, sizeof parent, "%s", v->path);
    slash = strrchr(parent, '/');
    if (slash == NULL) {
        return -1;
    }
    if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }
    return tiku_trk_poseview_switch_dir(v, parent);
}

int
tiku_trk_poseview_load_landed(tiku_trk_poseview_t *v, int gen,
                              const tiku_model_t *entries, int n)
{
    int i;

    if (v == NULL || !v->awaiting || gen != v->load_gen) {
        return -1;              /* a listing the window has left */
    }
    v->awaiting = 0;
    if (n < 0) {
        v->loading = 0;
        v->stage_count = 0;
        v->stage_at = 0;
        return -1;
    }
    v->stage_count = 0;
    v->stage_at = 0;
    for (i = 0; i < n; i++) {
        if (stage_one(&entries[i], v)) {
            break;
        }
    }
    v->next_chunk = 0;
    return 0;
}

