/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview.c - the engine.
 *
 * Rules here are Tracker's, read from the source and recorded in FEATURES.md;
 * each block cites the rows it implements so a behavior can be traced back to
 * the code that specified it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

tiku_trk_layout_clip_t pv_g_layout_clip;
int pv_g_layout_clip_valid;

/*---------------------------------------------------------------------------*/
/* list access -- everything iterates through CurrentPoseList()              */
/*---------------------------------------------------------------------------*/

int
tiku_trk_poseview_count(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return 0;
    }
    return v->filtering ? v->filtered_count : v->count;
}

tiku_trk_pose_t *
tiku_trk_poseview_at(tiku_trk_poseview_t *v, int i)
{
    if (v == NULL || i < 0 || i >= tiku_trk_poseview_count(v)) {
        return NULL;
    }
    return v->filtering ? &v->poses[v->filtered[i]] : &v->poses[i];
}

int
tiku_trk_poseview_selection_count(const tiku_trk_poseview_t *v)
{
    int i, n = 0;

    for (i = 0; v != NULL && i < v->count; i++) {
        if (v->poses[i].selected) {
            n++;
        }
    }
    return n;
}

tiku_trk_pose_t *
tiku_trk_poseview_active(tiku_trk_poseview_t *v)
{
    if (v == NULL || v->active < 0 || v->active >= v->count) {
        return NULL;
    }
    return &v->poses[v->active];
}

const char *
tiku_trk_poseview_path(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->path : "";
}

/** @brief The width the column showing @p field has, or a default. */
static int
column_width_of(const tiku_trk_poseview_t *v, int field)
{
    int i;

    for (i = 0; i < v->column_count; i++) {
        if (v->columns[i].field == field) {
            return v->columns[i].width;
        }
    }
    return 140;
}

/** @brief Whether @p path is inside a real filesystem Trash directory. */
static int
path_in_trash(const char *path)
{
    const char *p;

    if (path == NULL) {
        return 0;
    }
    p = path;
    while ((p = strstr(p, "/.Trash")) != NULL) {
        char c = p[7];

        if (c == '\0' || c == '/') {
            return c == '/';
        }
        p += 7;
    }
    return 0;
}

/** @brief Read a Trash origin, walking ancestors for folder-level records. */
static int
original_path_text(tiku_trk_poseview_t *v, const char *path, char *out,
                   size_t max)
{
    tiku_store_t *store;
    char probe[TIKU_PATH_MAX];

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (v == NULL || path == NULL || !path_in_trash(path) ||
        v->backend == NULL || v->backend->ops->state_store == NULL) {
        return 0;
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL) {
        return 0;
    }
    snprintf(probe, sizeof probe, "%s", path);
    for (;;) {
        int n = tiku_state_read(store, probe, TIKU_ATTR_ORIGINAL,
                                    out, max - 1u);

        if (n > 0) {
            out[n] = '\0';
            return 1;
        }
        if (strstr(probe, "/.Trash/") == NULL) {
            break;
        }
        {
            char *slash = strrchr(probe, '/');

            if (slash == NULL || slash == probe) {
                break;
            }
            *slash = '\0';
        }
    }
    return 0;
}

/**
 * @brief The text a column shows, formatted for a given width.
 *
 * The width matters: an interval reads "2h 37m 5s" where there is room and
 * "2h 37m" where there is not, so "what does this cell say" and "how wide
 * would this cell like to be" are two different questions asked of the same
 * function with two different widths.
 */
const char *
pv_cell_text_at(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p, int field,
             int width, char *buf, size_t max)
{
    if (v == NULL || p == NULL || buf == NULL || max == 0u) {
        return "";
    }
    buf[0] = '\0';
    switch (field) {
    case TIKU_TRK_COL_NAME:
        if (v->is_query) {
            /* Results come from EVERYWHERE, so two rows can share a name;
             * the one that does gains the minimum distinguishing path
             * (NeXT UIG ch8) -- computed against the rows shown. */
            const char *others[POSES_MAX];
            int k;

            for (k = 0; k < v->count; k++) {
                others[k] = v->poses[k].model.path;
            }
            (void)tiku_attr_disambiguate(p->model.path, others,
                                             v->count, buf, max);
            return buf;
        }
        return tiku_model_display_name(&p->model);
    case TIKU_TRK_COL_KIND:
        return tiku_model_kind_string(&p->model);
    case TIKU_TRK_COL_VALUE:
        if (tiku_model_is_container(&p->model)) {
            return "";
        }
        (void)tiku_attr_format(&p->desc, p->value_valid ? p->value : "",
                                   buf, max);
        return buf;
    case TIKU_TRK_COL_CAP:
        return (p->model.facts.req_cap[0] != '\0') ? p->model.facts.req_cap
                                                   : "-";
    case TIKU_TRK_COL_SIZE:
        /* A volume reports its CAPACITY, which is the size it has; every
         * other container has none of its own, and neither does a link --
         * a link's own bytes are not what anyone reading the column is
         * asking about.  -1 is the unknown that reads as "-" (MA-024). */
        if (p->model.kind == TIKU_KIND_VOLUME ||
            p->model.kind == TIKU_KIND_ROOT) {
            (void)tiku_attr_size_fit(
                (p->model.facts.total > 0u)
                    ? (int64_t)p->model.facts.total : -1,
                width, v->measure, v->measure_ctx, buf, max);
            return buf;
        }
        (void)tiku_attr_size_fit(
            (tiku_model_is_container(&p->model) ||
             p->model.kind == TIKU_KIND_SYMLINK) ? -1
                                                     : p->model.facts.size,
            width, v->measure, v->measure_ctx, buf, max);
        return buf;
    case TIKU_TRK_COL_DATE:
        (void)tiku_attr_time_fit(p->model.facts.mtime, width, v->measure,
                                     v->measure_ctx, buf, max);
        return buf;
    case TIKU_TRK_COL_PATH: {
        /* The folder it is IN, not its own path: the row already says its
         * name, and what a Location column answers is "where". */
        const char *slash = strrchr(p->model.path, '/');

        if (slash == NULL) {
            return p->model.path;
        }
        if (slash == p->model.path) {
            return "/";
        }
        snprintf(buf, max, "%.*s", (int)(slash - p->model.path),
                 p->model.path);
        return buf;
    }
    case TIKU_TRK_COL_ORIGINAL:
        return original_path_text(v, p->model.path, buf, max) ? buf : "-";
    default:
        if (field >= TIKU_TRK_COL_ATTR_BASE) {
            int at = field - TIKU_TRK_COL_ATTR_BASE;
            tiku_store_t *store = NULL;
            int n = -1;

            if (at < 0 || at >= v->attrcol_count ||
                v->backend->ops->state_store == NULL) {
                return "";
            }
            store = v->backend->ops->state_store(v->backend);
            if (store != NULL) {
                n = tiku_state_read(store, p->model.path,
                                        v->attrcol[at].name, buf, max - 1u);
            }
            if (n <= 0) {
                /* Nothing written for this entry.  A dash rather than an
                 * empty cell: the column exists for the whole listing, and
                 * a blank reads as "not measured yet" rather than "this
                 * one has none". */
                return "-";
            }
            buf[n] = '\0';
            return buf;
        }
        return "";
    case TIKU_TRK_COL_PERM: {
        unsigned perm = p->model.facts.perm;

        if (p->model.facts.mode_known && max >= 11u) {
            /* The classic ten characters (MA-031), walked from 00400 down
             * exactly as the source walks them.  The leading character
             * comes from what the MODEL is -- a link statted through
             * carries its target's bits, but it is still a link. */
            unsigned mode = p->model.facts.mode;
            unsigned mask = 0400u;
            int i;

            buf[0] = (p->model.kind == TIKU_KIND_SYMLINK) ? 'l'
                   : tiku_model_is_container(&p->model) ? 'd'
                   : ((mode & 0170000u) == 0060000u) ? 'b'
                   : ((mode & 0170000u) == 0020000u) ? 'c' : '-';
            for (i = 0; i < 9; i++, mask >>= 1) {
                buf[i + 1] = (mode & mask) ? "rwx"[i % 3] : '-';
            }
            buf[10] = '\0';
            return buf;
        }
        /* No mode word in this store: what THIS user may do, which is
         * what the access fact carries. */
        snprintf(buf, max, "%c%c%c",
                 (perm & TIKU_P_READ) ? 'r' : '-',
                 (perm & TIKU_P_WRITE) ? 'w' : '-',
                 (perm & TIKU_P_EXEC) ? 'x' : '-');
        return buf;
    }
    }
}

const char *
tiku_trk_poseview_cell_text(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p,
                            int field, char *buf, size_t max)
{
    tiku_trk_pose_t *mutable_pose = (tiku_trk_pose_t *)p;
    uint64_t key = UINT64_C(1469598103934665603);
    int width, i;

    if (v == NULL || p == NULL || buf == NULL || max == 0u) {
        return "";
    }
    /* The fingerprint contains every value that can affect a formatted
     * cell.  Width is kept separately because fitting is intentionally a
     * cache key too: resizing a column never reuses text fitted for its old
     * width. */
#define MIX_CELL(value) do { \
        uint64_t _x = (uint64_t)(value); \
        key ^= _x; key *= UINT64_C(1099511628211); \
    } while (0)
    MIX_CELL(field);
    MIX_CELL(p->model.node_id);
    if (v->is_query && field == TIKU_TRK_COL_NAME) {
        /* A result's NAME can depend on its neighbours (the
         * disambiguating suffix), so membership is part of the key. */
        MIX_CELL(v->count);
        MIX_CELL(v->load_gen);
    }
    MIX_CELL(p->model.kind);
    MIX_CELL(p->model.facts.size);
    MIX_CELL(p->model.facts.mtime);
    MIX_CELL(p->model.facts.perm);
    MIX_CELL(p->model.facts.total);
    MIX_CELL(p->model.facts.avail);
    MIX_CELL(p->value_valid);
    for (i = 0; i < (int)sizeof p->value && p->value[i] != '\0'; i++) {
        MIX_CELL((unsigned char)p->value[i]);
    }
    for (i = 0; i < (int)sizeof p->model.path && p->model.path[i] != '\0'; i++) {
        MIX_CELL((unsigned char)p->model.path[i]);
    }
    for (i = 0; i < (int)sizeof p->model.name && p->model.name[i] != '\0'; i++) {
        MIX_CELL((unsigned char)p->model.name[i]);
    }
    for (i = 0; i < (int)sizeof p->model.type && p->model.type[i] != '\0'; i++) {
        MIX_CELL((unsigned char)p->model.type[i]);
    }
#undef MIX_CELL
    width = column_width_of(v, field);
    for (i = 0; i < TIKU_TRK_COLUMNS_MAX; i++) {
        if (mutable_pose->cell_cache[i].valid &&
            mutable_pose->cell_cache[i].field == field &&
            mutable_pose->cell_cache[i].width == width &&
            mutable_pose->cell_cache[i].key == key) {
            v->cell_cache_hits++;
            snprintf(buf, max, "%s", mutable_pose->cell_cache[i].text);
            return buf;
        }
    }
    v->cell_cache_misses++;
    i = 0;
    while (i < TIKU_TRK_COLUMNS_MAX && mutable_pose->cell_cache[i].valid) {
        i++;
    }
    if (i == TIKU_TRK_COLUMNS_MAX) {
        i = field % TIKU_TRK_COLUMNS_MAX;
        if (i < 0) { i = 0; }
    }
    {
        const char *text = pv_cell_text_at(v, p, field, width,
                                        mutable_pose->cell_cache[i].text,
                                        sizeof mutable_pose->cell_cache[i].text);

        if (text != mutable_pose->cell_cache[i].text) {
            snprintf(mutable_pose->cell_cache[i].text,
                     sizeof mutable_pose->cell_cache[i].text, "%s", text);
        }
    }
    mutable_pose->cell_cache[i].field = field;
    mutable_pose->cell_cache[i].width = width;
    mutable_pose->cell_cache[i].key = key;
    mutable_pose->cell_cache[i].valid = 1;
    snprintf(buf, max, "%s", mutable_pose->cell_cache[i].text);
    return buf;
}

void
tiku_trk_poseview_cell_cache_stats(const tiku_trk_poseview_t *v,
                                   uint64_t *hits, uint64_t *misses)
{
    if (hits != NULL) { *hits = v != NULL ? v->cell_cache_hits : 0; }
    if (misses != NULL) { *misses = v != NULL ? v->cell_cache_misses : 0; }
}
