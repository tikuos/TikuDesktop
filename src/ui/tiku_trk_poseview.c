/*
 * The new Tracker for TikuOS.
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

#include "tiku_trk_poseview.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_state.h"
#include "tiku_trk_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define POSES_MAX 4096

struct tiku_trk_poseview {
    tiku_trk_backend_t *backend;
    char                path[TIKU_TRK_PATH_MAX];

    tiku_trk_pose_t    *poses;          /* fPoseList                        */
    int                 count;
    int                *filtered;       /* fFilteredPoseList: indices       */
    int                 filtered_count;
    int                 filtering;
    uint64_t            menu_generation;
    int64_t             broken_repair_due;
    tiku_trk_repaint_t  repaint;

    int                 pivot;          /* fSelectionPivotPose (-1 = none)  */
    int                 last_added;     /* for shift+arrow walk-back        */
    int                 active;         /* fActivePose                      */

    char                typed[TIKU_TRK_TYPEAHEAD_MAX];
    int                 typed_len;
    int64_t             typed_at;

    /* Filtering keeps its OWN strings, because it is a different mechanism
     * from jump-to-name: the terms accumulate, they are shown, and they are
     * only cleared deliberately, where a jump string lapses on its own. */
    char                filter[TIKU_TRK_FILTER_TERMS][TIKU_TRK_TYPEAHEAD_MAX];
    int                 filter_count;
    char                filter_shown[TIKU_TRK_FILTER_TERMS *
                                     TIKU_TRK_TYPEAHEAD_MAX];

    int                 sort_field;
    int                 sort2_field;    /* the tie-break, or -1             */
    int                 sort_reverse;
    int                 scroll;

    /* Selection MEMBERSHIP is the pose flag; whether it is PAINTED is this.
     * Tracker keeps two structures and clears the flag on deactivation; one
     * flag plus this bit says the same thing without a second list that can
     * drift out of step with the first. */
    int                 selection_visible;
    int                 show_sel_inactive;
    int                 real_pivot;     /* the pivot, parked while hidden   */
    int                 band;           /* a rubber band is being swept     */
    tiku_trk_repaint_rect_t band_rect;  /* where it was when last swept     */
    int                 band_seen;      /* whether band_rect means anything */
    unsigned char      *band_was;       /* selection when the band began    */

    /* The armed inline rename: which pose, and when its click landed. */
    int                 edit_pose;
    int64_t             edit_clicked_at;
    int                 allow_editing;

    tiku_trk_measure_fn measure;
    void               *measure_ctx;
    int                 drop_x, drop_y; /* where the next new entry lands   */
    int                 drop_valid;
    /* Where NAMED entries about to arrive should land.  A drop is decided
     * before the entries exist here, and an arrangement that is applied
     * after they appear is one the user watches assemble itself. */
    struct {
        char            name[TIKU_TRK_NAME_MAX];
        int             x, y;
    }                   land[TIKU_TRK_LAND_MAX];
    int                 land_count;

    int                 mode;           /* TIKU_TRK_MODE_*                  */
    /* Which ICON mode was last live, kept beside the current mode the way
     * the original keeps fLastIconMode: the stored pose positions are in
     * ITS geometry, and a folder leaving list mode needs to know whose
     * grid it is reading (MA-061). */
    int                 last_icon;
    /* A named state slot overriding the path: the Desktop and the Disks
     * window must never collide with a folder window that happens to show
     * the same directory (MA-064). */
    char                state_key[16];
    int                 icon_size;
    int                 hint_x, hint_y; /* fHintLocation: where to place next */
    int                 state_dirty;
    /* Set only by the column mutators, so a release that merely
     * re-sorted does not rewrite the layout record (TS-036). */
    int                 columns_dirty;    /* fStateNeedsSaving                */
    int                 is_query;       /* results, not a directory         */
    char                query_layout[TIKU_TRK_PATH_MAX];
    char                query_type[TIKU_TRK_TYPE_MAX];
    tiku_trk_store_t    *layout_store;  /* synthetic query layout records   */
    int                 visible_rows;   /* what the window can show         */
    int                 view_w, view_h; /* the window, in PIXELS            */
    /* The font the labels are drawn in.  Zero until the owner says, and
     * every icon-mode height falls back to the art size until it does. */
    int                 font_h, font_a;
    int                 row_h;      /* what the owner draws, 0 = derive  */
    int                 folders_first;  /* the setting, on by default    */
    int                 no_autoscroll;  /* the Desktop's exemption       */
    int                 hide_dotfiles;
    /* Columns that show a named ATTRIBUTE rather than a fixed fact.  The
     * slot is the field id; the name is what a saved layout carries. */
    struct {
        char name[TIKU_TRK_ATTR_NAME_MAX];
        char title[24];
        int  width;
    }                   attrcol[TIKU_TRK_COL_ATTR_MAX];
    int                 attrcol_count;
    tiku_trk_hit_fn     icon_hit;
    void               *icon_hit_ctx;
    /* The pixel origin of each icon mode, kept apart from the list's row
     * scroll so a mode round-trip returns to where it was (PVL-009). */
    int                 icon_x, icon_y;
    int                 mini_x, mini_y;
    /* The bounding box of everything placed, maintained as poses arrive and
     * move rather than walked on every ask (PVL-050).  Inverted while empty,
     * so the first pose DEFINES it instead of the origin doing so. */
    int                 ext_x0, ext_y0, ext_x1, ext_y1;
    int                 ext_valid;
    /* Notices that arrived before the row they concern, and entries seen but
     * not readable -- neither is dropped, because a change lost is a row
     * that lies and an unreadable entry is not an absent one. */
    tiku_trk_park_t     park;
    tiku_trk_model_t    zombie[16];
    int                 zombie_count;

    tiku_trk_column_t   columns[TIKU_TRK_COLUMNS_MAX];
    int                 column_count;
    int                 hscroll;        /* titles and rows share it        */
    int                 font_px;        /* what widths are scaled against  */
    uint64_t            cell_cache_hits;
    uint64_t            cell_cache_misses;

    /* The batched load.  The read is synchronous; what is staged here is
     * PUBLISHED a batch at a time, so the window is never blank and never
     * blocked (PVL-088, PVN-047). */
    tiku_trk_model_t   *stage;
    int                 stage_count;
    int                 stage_at;
    int                 loading;
    int64_t             next_chunk;
    int64_t             pole_at;        /* when the pole becomes visible   */
    /* Bumped whenever the listing this view shows changes identity.  A
     * batch or a notice carrying an older one belongs to a folder the
     * window has left (PVL-089). */
    int                 load_gen;
    /* Node ids published this load.  The loader and the change notices are
     * two mouths feeding one listing, and without this a node reported by
     * both arrives twice (PVL-090). */
    uint64_t           *inserted;
    int                 inserted_count;
    /* The shared icon cache, or NULL.  Kept in step from the notice
     * handler: see tiku_trk_poseview_set_icons. */
    tiku_trk_nodeicon_t *icons;
    /* Whether several poses may be selected at once.  Stored inverted so a
     * zeroed view is a normal multi-select one, which every existing
     * caller expects. */
    int single_only;
};

/* Process-wide typed layout channel.  Unlike the file clipboard this is a
 * value clipboard: it has no node lifetime to watch and is replaced as one
 * immutable snapshot, so every open view sees the same paste immediately. */
static tiku_trk_layout_clip_t g_layout_clip;
static int g_layout_clip_valid;

static void column_def_v(const tiku_trk_poseview_t *v, int field,
                         const char **title, int *width);

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
    tiku_trk_store_t *store;
    char probe[TIKU_TRK_PATH_MAX];

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
        int n = tiku_trk_state_read(store, probe, TIKU_TRK_ATTR_ORIGINAL,
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
static const char *
cell_text_at(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p, int field,
             int width, char *buf, size_t max)
{
    if (v == NULL || p == NULL || buf == NULL || max == 0u) {
        return "";
    }
    buf[0] = '\0';
    switch (field) {
    case TIKU_TRK_COL_NAME:
        return tiku_trk_model_display_name(&p->model);
    case TIKU_TRK_COL_KIND:
        return tiku_trk_model_kind_string(&p->model);
    case TIKU_TRK_COL_VALUE:
        if (tiku_trk_model_is_container(&p->model)) {
            return "";
        }
        (void)tiku_trk_attr_format(&p->desc, p->value_valid ? p->value : "",
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
        if (p->model.kind == TIKU_TRK_KIND_VOLUME ||
            p->model.kind == TIKU_TRK_KIND_ROOT) {
            (void)tiku_trk_attr_size_fit(
                (p->model.facts.total > 0u)
                    ? (int64_t)p->model.facts.total : -1,
                width, v->measure, v->measure_ctx, buf, max);
            return buf;
        }
        (void)tiku_trk_attr_size_fit(
            (tiku_trk_model_is_container(&p->model) ||
             p->model.kind == TIKU_TRK_KIND_SYMLINK) ? -1
                                                     : p->model.facts.size,
            width, v->measure, v->measure_ctx, buf, max);
        return buf;
    case TIKU_TRK_COL_DATE:
        (void)tiku_trk_attr_time_fit(p->model.facts.mtime, width, v->measure,
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
            tiku_trk_store_t *store = NULL;
            int n = -1;

            if (at < 0 || at >= v->attrcol_count ||
                v->backend->ops->state_store == NULL) {
                return "";
            }
            store = v->backend->ops->state_store(v->backend);
            if (store != NULL) {
                n = tiku_trk_state_read(store, p->model.path,
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

            buf[0] = (p->model.kind == TIKU_TRK_KIND_SYMLINK) ? 'l'
                   : tiku_trk_model_is_container(&p->model) ? 'd'
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
                 (perm & TIKU_TRK_P_READ) ? 'r' : '-',
                 (perm & TIKU_TRK_P_WRITE) ? 'w' : '-',
                 (perm & TIKU_TRK_P_EXEC) ? 'x' : '-');
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
        const char *text = cell_text_at(v, p, field, width,
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

/*---------------------------------------------------------------------------*/
/* loading                                                                   */
/*---------------------------------------------------------------------------*/

static int
collect(const tiku_trk_model_t *m, void *ctx)
{
    tiku_trk_poseview_t *v = ctx;

    if (v->hide_dotfiles && m != NULL && m->name[0] == '.') {
        return 0;
    }
    if (v->count >= POSES_MAX) {
        return 1;
    }
    if (m->kind == TIKU_TRK_KIND_UNKNOWN) {
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
    (void)tiku_trk_desc_parse(m->facts.meta, &v->poses[v->count].desc);
    v->poses[v->count].index = v->count;
    v->count++;
    return 0;
}

/** @brief Drop poses marked invisible, after their state has been read. */
static void
apply_visibility(tiku_trk_poseview_t *v)
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
static int
sane_scroll_origin(int value, int allow_negative)
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

static void rebuild_filter(tiku_trk_poseview_t *v);
static void note_inserted(tiku_trk_poseview_t *v, uint64_t id);
static int pose_index(const tiku_trk_poseview_t *v, const char *path);
static int insert_sorted(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p);
static int pose_is_hidden(tiku_trk_poseview_t *v, const char *path);
static int poses_sorted(tiku_trk_poseview_t *v);

static tiku_trk_poseview_t *g_sorting;

/** @brief Order two poses on one field, without the tie-breaks. */
static int
field_cmp(const tiku_trk_pose_t *a, const tiku_trk_pose_t *b, int field)
{
    switch (field) {
    case TIKU_TRK_COL_KIND:
        return strcmp(tiku_trk_model_kind_string(&a->model),
                      tiku_trk_model_kind_string(&b->model));
    case TIKU_TRK_COL_SIZE:
        return (a->model.facts.size < b->model.facts.size) ? -1
               : (a->model.facts.size > b->model.facts.size) ? 1 : 0;
    case TIKU_TRK_COL_VALUE:
        /* Through the descriptor, so a byte column orders 9 B before 10 KiB
         * rather than ordering the text it happens to be shown as. */
        return tiku_trk_attr_compare(&a->desc, a->value, b->value);
    case TIKU_TRK_COL_CAP:
        return strcmp(a->model.facts.req_cap, b->model.facts.req_cap);
    case TIKU_TRK_COL_DATE:
        return (a->model.facts.mtime < b->model.facts.mtime) ? -1
               : (a->model.facts.mtime > b->model.facts.mtime) ? 1 : 0;
    case TIKU_TRK_COL_NAME:
        return natural_cmp(tiku_trk_model_display_name(&a->model),
                           tiku_trk_model_display_name(&b->model));
    default:
        return 0;
    }
}

static int
pose_cmp(const void *pa, const void *pb)
{
    const tiku_trk_pose_t *a = pa, *b = pb;
    int r;

    /* Containers first, then by the sort field: a folder is not just a big
     * file, and mixing them makes a listing unreadable.  It is a SETTING,
     * though -- the user can ask for one list sorted by the column alone
     * (MA-039). */
    if (g_sorting->folders_first) {
        /* The full ladder, not one bit: the Disks root beats everything,
         * real volumes beat folders, folders beat files -- and a volume
         * whose capacity reads zero is DEMOTED to the folder bucket, the
         * way the source sorts a volume that cannot say its size
         * (MA-009). */
        int ba = (a->model.kind == TIKU_TRK_KIND_ROOT) ? 3
               : (a->model.kind == TIKU_TRK_KIND_VOLUME &&
                  a->model.facts.total > 0u)           ? 2
               : tiku_trk_model_is_container(&a->model) ? 1 : 0;
        int bb = (b->model.kind == TIKU_TRK_KIND_ROOT) ? 3
               : (b->model.kind == TIKU_TRK_KIND_VOLUME &&
                  b->model.facts.total > 0u)           ? 2
               : tiku_trk_model_is_container(&b->model) ? 1 : 0;

        if (ba != bb) {
            return bb - ba;
        }
    }
    /* An entry with no value for the sort field sorts LAST whichever way
     * the column is ordered.  Decided here rather than inside the field
     * comparison, because the reverse below negates everything it returns
     * and "last" is not a value to be turned around. */
    if (g_sorting->sort_field == TIKU_TRK_COL_VALUE) {
        int ea = (a->value[0] == '\0'), eb = (b->value[0] == '\0');

        if (ea != eb) {
            return ea ? 1 : -1;
        }
    }
    r = field_cmp(a, b, g_sorting->sort_field);
    if (r == 0 && g_sorting->sort2_field >= 0 &&
        g_sorting->sort2_field != g_sorting->sort_field) {
        r = field_cmp(a, b, g_sorting->sort2_field);
    }
    if (r == 0) {
        r = natural_cmp(tiku_trk_model_display_name(&a->model),
                        tiku_trk_model_display_name(&b->model));
    }
    return g_sorting->sort_reverse ? -r : r;
}

static void
resort(tiku_trk_poseview_t *v)
{
    int i;

    g_sorting = v;
    qsort(v->poses, (size_t)v->count, sizeof v->poses[0], pose_cmp);
    for (i = 0; i < v->count; i++) {
        v->poses[i].index = i;
    }
    g_sorting = NULL;
}


/*---------------------------------------------------------------------------*/
/* loading in batches (PVL-088, PVN-047, PVL-089, PVL-090, TS-038)           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stage one entry from the enumeration.
 *
 * Zero CONTINUES the walk and non-zero stops it, which is the backend's
 * contract and the opposite of what a "did it work" return would read as.
 */
static int
stage_one(const tiku_trk_model_t *m, void *ctx)
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

static void
note_inserted(tiku_trk_poseview_t *v, uint64_t id)
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
    while (v->stage_at < v->stage_count) {
        tiku_trk_model_t *m = &v->stage[v->stage_at];

        /* By identity where the store has one, and by path where it does
         * not: a backend without node ids still must not list a row twice. */
        if (already_inserted(v, m->node_id) || pose_index(v, m->path) >= 0 ||
            pose_is_hidden(v, m->path)) {
            /* A node the change notices already brought in, or one marked
             * not to be shown: consumed, not published. */
            v->stage_at++;
            continue;
        }
        {   /* collect() answers the WALK, not the caller: whether a row
             * appeared is the count moving, not its return value. */
            int before = v->count;

            (void)collect(m, v);
            v->stage_at++;
            if (v->count == before) {
                continue;               /* held as a zombie, or no room   */
            }
        }
        note_inserted(v, m->node_id);
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
    resort(v);
    if (v->filtering) {
        rebuild_filter(v);
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
    while (tiku_trk_poseview_load_step(v, TIKU_TRK_LOAD_CHUNK_US * 2)) {
    }
    resort(v);
    {
        /* The invisible bit rides on the saved records, and this path does
         * not read them wholesale -- so each entry is asked individually.
         * Without this, poll()'s overflow reload resurrects every hidden
         * row until the next restore. */
        int i2, w = 0;

        for (i2 = 0; i2 < v->count; i2++) {
            if (pose_is_hidden(v, v->poses[i2].model.path)) {
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
        rebuild_filter(v);
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
    v->columns[0] = (tiku_trk_column_t){ "Name", 145,
        TIKU_TRK_COL_NAME, 0, TIKU_TRK_ALIGN_LEFT };
    v->columns[1] = (tiku_trk_column_t){ "Location", 225,
        TIKU_TRK_COL_PATH, 0, TIKU_TRK_ALIGN_LEFT };
    v->column_count = 2;
    if (bookmark) {
        query_attr_column(v, "META:title", "Title", 180);
        query_attr_column(v, "META:url", "URL", 260);
    } else if (person) {
        query_attr_column(v, "META:name", "Name", 160);
        query_attr_column(v, "META:nickname", "Nickname", 110);
        query_attr_column(v, "META:email", "E-mail", 190);
        query_attr_column(v, "META:phone", "Phone", 120);
    } else if (mail) {
        query_attr_column(v, "MAIL:subject", "Subject", 220);
        query_attr_column(v, "MAIL:from", "From", 170);
        query_attr_column(v, "MAIL:when", "When", 135);
        query_attr_column(v, "MAIL:status", "Status", 90);
    } else {
        v->columns[2] = (tiku_trk_column_t){ "Size", 80,
            TIKU_TRK_COL_SIZE, 0, TIKU_TRK_ALIGN_RIGHT };
        v->columns[3] = (tiku_trk_column_t){ "Modified", 150,
            TIKU_TRK_COL_DATE, 0, TIKU_TRK_ALIGN_LEFT };
        v->column_count = 4;
    }
    tiku_trk_poseview_reflow_columns(v);
}

static int
fill_from(tiku_trk_poseview_t *v, const tiku_trk_model_t *m, int n,
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
        (void)tiku_trk_desc_parse(m[i].facts.meta, &v->poses[v->count].desc);
        v->poses[v->count].index = v->count;
        v->count++;
    }
    resort(v);
    if (v->filtering) {
        rebuild_filter(v);
    } else {
        v->filtered_count = 0;
    }
    (void)tiku_trk_poseview_refresh_values(v, 0, 64);
    return v->count;
}

int
tiku_trk_poseview_set_results(tiku_trk_poseview_t *v,
                              const tiku_trk_model_t *m, int n)
{
    return fill_from(v, m, n, 1);
}

int
tiku_trk_poseview_set_models(tiku_trk_poseview_t *v,
                             const tiku_trk_model_t *m, int n)
{
    return fill_from(v, m, n, 0);
}

int
tiku_trk_poseview_adopt(tiku_trk_poseview_t *v, const tiku_trk_model_t *m)
{
    tiku_trk_pose_t p;

    if (v == NULL || m == NULL) {
        return -1;
    }
    if (pose_index(v, m->path) >= 0) {
        return -1;              /* already here: adopting twice is a copy */
    }
    memset(&p, 0, sizeof p);
    p.model = *m;
    (void)tiku_trk_desc_parse(m->facts.meta, &p.desc);
    p.adopted = 1;
    if (insert_sorted(v, &p) < 0) {
        return -1;
    }
    note_inserted(v, m->node_id);
    return 0;
}

int
tiku_trk_poseview_is_query(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->is_query : 0;
}

uint32_t
tiku_trk_poseview_background(const tiku_trk_poseview_t *v)
{
    /* A tint below 1 lightens, and white cannot get lighter; use the
     * concrete read-only shade so the cue is visible on the document white. */
    return (v != NULL && v->is_query) ? UINT32_C(0x00f0f0f0)
                                      : UINT32_C(0x00ffffff);
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
                                   tiku_trk_store_t *store)
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
tiku_trk_poseview_new(tiku_trk_backend_t *backend, const char *path)
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

    v->columns[0] = (tiku_trk_column_t){ "Name",  240, TIKU_TRK_COL_NAME,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->columns[1] = (tiku_trk_column_t){ "Kind",  140, TIKU_TRK_COL_KIND,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->columns[2] = (tiku_trk_column_t){ "Value", 190, TIKU_TRK_COL_VALUE,
                                         0, TIKU_TRK_ALIGN_LEFT };
    v->columns[3] = (tiku_trk_column_t){ "Cap",    60, TIKU_TRK_COL_CAP,
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
    tiku_trk_park_clear(&v->park);
    v->zombie_count = 0;
    snprintf(v->path, sizeof v->path, "%s", path);
    v->scroll = 0;
    return tiku_trk_poseview_reload(v);
}

int
tiku_trk_poseview_go_up(tiku_trk_poseview_t *v)
{
    char parent[TIKU_TRK_PATH_MAX];
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

/*---------------------------------------------------------------------------*/
/* selection -- PVS-001..010                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Select or deselect one pose, stamping when it joined.
 *
 * The stamp is what the slow-second-click rename reads: the click that
 * selects a pose must not also arm its rename, and only the moment
 * selection happened can tell those two clicks apart.
 */
static void
set_selected(tiku_trk_pose_t *p, int on)
{
    if (p == NULL) {
        return;
    }
    if (on && !p->selected) {
        p->sel_time = tiku_trk_now();
    }
    p->selected = on ? 1 : 0;
}

static void
clear_selection(tiku_trk_poseview_t *v)
{
    int i;

    for (i = 0; i < v->count; i++) {
        v->poses[i].selected = 0;
    }
}

void
tiku_trk_poseview_select_none(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        clear_selection(v);
        v->pivot = -1;              /* PVS-008: empty selection, no pivot */
        v->real_pivot = -1;
        v->last_added = -1;
    }
}

int
tiku_trk_poseview_select_after_removal(tiku_trk_poseview_t *v, int index)
{
    int n;

    if (v == NULL || tiku_trk_poseview_mode(v) != TIKU_TRK_MODE_LIST) {
        /* Icon modes have no "next one down": the poses are where the user
         * put them, and picking a neighbour by list order would select
         * something on the other side of the window. */
        return -1;
    }
    n = tiku_trk_poseview_count(v);
    if (n == 0) {
        tiku_trk_poseview_select_none(v);
        return -1;
    }
    /* The row that has TAKEN the removed one's index -- the next one down
     * before the removal.  Past the end means the last item went, and the
     * new last row is the one that moved up into the gap. */
    if (index < 0) {
        index = 0;
    }
    if (index >= n) {
        index = n - 1;
    }
    tiku_trk_poseview_click(v, index, 0u);
    return index;
}

void
tiku_trk_poseview_select_all(tiku_trk_poseview_t *v)
{
    int i, n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    if (v->single_only) {
        /* Nothing to select ALL of.  The command stays reachable and does
         * the only thing it can mean here (PVS-009). */
        n = (n > 0) ? 1 : 0;
    }
    for (i = 0; i < n; i++) {
        set_selected(tiku_trk_poseview_at(v, i), 1);
    }
    if (n > 0) {
        /* Always the first row, not only when there is no anchor: a pivot
         * left over from an earlier click would make the next shift-click
         * extend from a pose that has nothing to do with this selection. */
        v->pivot = tiku_trk_poseview_at(v, 0)->index;
        v->last_added = tiku_trk_poseview_at(v, n - 1)->index;
    }
}

/** @brief Where pose @p pi appears in the list being shown, or -1. */
static int
display_of(const tiku_trk_poseview_t *v, int pi)
{
    int i;

    if (pi < 0) {
        return -1;
    }
    if (!v->filtering) {
        return (pi < v->count) ? pi : -1;
    }
    for (i = 0; i < v->filtered_count; i++) {
        if (v->filtered[i] == pi) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Select the range between two POSE indices.
 *
 * The anchors are pose-array indices, because that is the one space the view
 * keeps: a display index means nothing once a filter changes or a row is
 * removed, and mixing the two is why a pivot could end up pointing at
 * whatever slid into the gap.  The walk is over the DISPLAYED list, so a
 * range never selects a row the user cannot see.
 */
static void
select_range(tiku_trk_poseview_t *v, int from_pose, int to_pose, int on)
{
    int a = display_of(v, from_pose), b = display_of(v, to_pose);
    int i, lo, hi;

    if (a < 0 || b < 0) {
        return;
    }
    lo = (a < b) ? a : b;
    hi = (a < b) ? b : a;
    for (i = lo; i <= hi; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        set_selected(p, on);
    }
}

/** @brief Select an icon-mode rectangle from a point to a pose's location. */
static void
select_icon_range(tiku_trk_poseview_t *v, int from_pose, int x, int y, int on)
{
    tiku_trk_pose_t *anchor;
    int ax = 0, ay = 0, i, n;
    int lox, loy, hix, hiy;

    if (from_pose < 0 || from_pose >= v->count) {
        return;
    }
    anchor = &v->poses[from_pose];
    tiku_trk_poseview_pose_location(v, anchor, &ax, &ay);
    lox = (x < ax) ? x : ax;
    loy = (y < ay) ? y : ay;
    hix = (x < ax) ? ax : x;
    hiy = (y < ay) ? ay : y;
    /* A zero-width or zero-height BRect still catches the poses it touches.
     * Express that explicitly because the integer intersection below uses a
     * half-open far edge (PVS-005). */
    if (hix - lox < 1) { hix = lox + 1; }
    if (hiy - loy < 1) { hiy = loy + 1; }

    n = tiku_trk_poseview_count(v);
    for (i = n - 1; i >= 0; i--) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        if (px + pw > lox && px < hix && py + ph > loy && py < hiy) {
            set_selected(p, on);
        }
    }
}

/** @brief Restore the selection/pivot invariant after every click branch. */
static void
repair_click_pivot(tiku_trk_poseview_t *v, const tiku_trk_pose_t *clicked)
{
    if (tiku_trk_poseview_selection_count(v) == 0) {
        v->pivot = -1;
        v->real_pivot = -1;
        v->last_added = -1;
    } else if (v->pivot < 0 && clicked != NULL) {
        /* Tracker re-anchors on the pose that caused the operation, including
         * when a Shift+Command range removed that pose but left others. */
        v->pivot = clicked->index;
    }
}

void
tiku_trk_poseview_set_multiple(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->single_only = !on;
        if (!on) {
            /* Whatever was already gathered goes down to one: the view is
             * about to be shown as a picker, and leaving three rows lit
             * would offer a choice it will not honour. */
            int i, n = tiku_trk_poseview_count(v), kept = 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

                if (p != NULL && p->selected) {
                    if (kept) {
                        p->selected = 0;
                    } else {
                        kept = 1;
                    }
                }
            }
        }
    }
}

int
tiku_trk_poseview_multiple(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? !v->single_only : 1;
}

void
tiku_trk_poseview_click(tiku_trk_poseview_t *v, int index, unsigned mods)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(v, index);
    int x = 0, y = 0;

    if (p != NULL) {
        int w = 0, h = 0;

        /* Put a coordinate-free click just inside what is actually hit.  An
         * icon-mode pose location is the cell's top-left, which may be empty
         * margin to the left of centred art and would select nothing. */
        tiku_trk_poseview_pose_rect(v, p, &x, &y, &w, &h);
        x++;
        y++;
    }
    tiku_trk_poseview_click_at(v, index, x, y, mods);
}

void
tiku_trk_poseview_click_at(tiku_trk_poseview_t *v, int index, int x, int y,
                           unsigned mods)
{
    tiku_trk_pose_t *p;

    if (v == NULL) {
        return;
    }
    if (v->single_only) {
        /* Every extending modifier is dropped rather than handled, so a
         * modifier click is a plain click (PVS-009). */
        mods &= ~(TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
    }
    if (index < 0) {
        /* PVS-001: clicking empty space clears the whole selection, and with
         * it any rename waiting to start (PVS-015). */
        tiku_trk_poseview_cancel_edit_wait(v);
        tiku_trk_poseview_select_none(v);
        v->active = -1;
        return;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL) {
        return;
    }
    /* PVS-015: a click on a DIFFERENT pose disarms a pending rename; the
     * pose it was armed on is no longer the one being worked with. */
    if (v->edit_clicked_at != 0 && v->edit_pose != p->index) {
        tiku_trk_poseview_cancel_edit_wait(v);
    }
    v->active = p->index;

    if ((mods & TIKU_TRK_MOD_CMD) && (mods & TIKU_TRK_MOD_SHIFT)) {
        /* PVS-007: the range takes the state the CLICKED pose does not
         * already have.  Deselecting unconditionally would make a
         * shift+cmd click on an unselected pose clear a range the user was
         * reaching out to add. */
        int on = !p->selected;

        if (v->pivot >= 0) {
            if (v->mode == TIKU_TRK_MODE_LIST) {
                select_range(v, v->pivot, p->index, on);
            } else {
                select_icon_range(v, v->pivot, x, y, on);
            }
        } else {
            set_selected(p, on);
        }
        if (!on && v->pivot >= 0 && !v->poses[v->pivot].selected) {
            v->pivot = -1;
        }
        if (!on && v->real_pivot >= 0 &&
            !v->poses[v->real_pivot].selected) {
            v->real_pivot = -1;
        }
        repair_click_pivot(v, p);
        return;
    }
    if (mods & TIKU_TRK_MOD_CMD) {
        /* PVS-004: toggle one, leave the rest alone. */
        set_selected(p, !p->selected);
        if (p->selected) {
            v->pivot = p->index;
            v->last_added = p->index;
        } else if (tiku_trk_poseview_selection_count(v) == 0) {
            v->pivot = -1;          /* PVS-008 */
        }
        repair_click_pivot(v, p);
        return;
    }
    if (mods & TIKU_TRK_MOD_SHIFT) {
        /* PVS-005/006: clear first but KEEP the pivot, so successive
         * shift-clicks re-anchor rather than growing monotonically. */
        int pivot = (v->pivot >= 0) ? v->pivot : p->index;
        clear_selection(v);
        v->pivot = pivot;
        if (v->mode == TIKU_TRK_MODE_LIST) {
            select_range(v, pivot, p->index, 1);
        } else {
            select_icon_range(v, pivot, x, y, 1);
        }
        v->last_added = p->index;
        repair_click_pivot(v, p);
        return;
    }
    /* PVS-003/PVS-014: a selected pose stays selected without being stamped
     * again.  For a group that preserves the drag payload; for a single pose
     * it preserves the age that distinguishes a slow second click from the
     * click that originally selected it and lets inline rename arm. */
    if (p->selected && (tiku_trk_poseview_selection_count(v) > 1 ||
                        v->pivot == p->index)) {
        return;
    }
    /* PVS-002: otherwise select just this one and make it the pivot. */
    clear_selection(v);
    set_selected(p, 1);
    v->pivot = p->index;
    v->last_added = p->index;
    repair_click_pivot(v, p);
}

void
tiku_trk_poseview_invert_selection(tiku_trk_poseview_t *v)
{
    int i, n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    /* Both anchors go first.  An inverted selection has no click behind it,
     * so a pivot left over from before would anchor the next shift-click on
     * a row the user never touched. */
    v->pivot = -1;
    v->real_pivot = -1;
    v->last_added = -1;
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

        if (p == NULL) {
            continue;
        }
        set_selected(p, !p->selected);
        if (i == 0 && p->selected) {
            v->pivot = p->index;
        }
    }
}

int
tiku_trk_poseview_select_matching(tiku_trk_poseview_t *v, const char *expr,
                                  tiku_trk_match_t how, int ignore_case,
                                  int invert, char *err, size_t errmax)
{
    int i, n, hits = 0;

    if (v == NULL || expr == NULL) {
        return -1;
    }
    /* Check the expression BEFORE clearing anything: a typo that matched
     * nothing and a typo that was rejected look identical on screen once the
     * selection is already gone. */
    if (how == TIKU_TRK_MATCH_REGEX &&
        !tiku_trk_regex_check(expr, err, errmax)) {
        return -1;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    tiku_trk_poseview_select_none(v);
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int m;

        if (p == NULL) {
            continue;
        }
        m = tiku_trk_match(p->model.name, expr, !ignore_case, how);
        if (m ^ (invert ? 1 : 0)) {
            set_selected(p, 1);
            if (hits == 0) {
                v->pivot = p->index;
                v->active = p->index;
            }
            v->last_added = p->index;
            hits++;
        }
    }
    if (hits > 0) {
        int i, n = tiku_trk_poseview_count(v);

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

            if (p != NULL && p->selected) {
                /* The first thing it found, for the same reason: a window
                 * that selected six rows off screen looks like a window
                 * that did nothing (PVL-054). */
                tiku_trk_poseview_scroll_into_view(v, i, v->visible_rows);
                break;
            }
        }
    }
    return hits;
}

int
tiku_trk_poseview_context_click(tiku_trk_poseview_t *v, int index)
{
    tiku_trk_pose_t *p;

    if (v == NULL) {
        return 0;
    }
    if (index < 0) {
        /* Empty space: the menu is about the FOLDER, so a selection left
         * standing would make its items apply to something off-screen. */
        if (tiku_trk_poseview_selection_count(v) == 0) {
            return 0;
        }
        tiku_trk_poseview_select_none(v);
        v->active = -1;
        return 1;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL) {
        return 0;
    }
    v->active = p->index;
    if (p->selected) {
        return 0;               /* a group keeps its group                */
    }
    clear_selection(v);
    set_selected(p, 1);
    v->pivot = p->index;
    v->last_added = p->index;
    return 1;
}

int
tiku_trk_poseview_draws_selected(const tiku_trk_poseview_t *v,
                                 const tiku_trk_pose_t *p)
{
    if (v == NULL || p == NULL || !p->selected) {
        return 0;
    }
    /* A band in progress paints its sweep even in an inactive window: it is
     * the only feedback the sweep has (PVS-031). */
    return v->selection_visible || v->show_sel_inactive || v->band;
}

void
tiku_trk_poseview_window_activated(tiku_trk_poseview_t *v, int active)
{
    if (v == NULL) {
        return;
    }
    if (v->show_sel_inactive) {
        v->selection_visible = 1;
        return;
    }
    if (active == v->selection_visible) {
        return;
    }
    v->selection_visible = active ? 1 : 0;
    if (!active) {
        /* Parked, not dropped: coming back to a window and shift-clicking
         * must extend from the anchor the user left, not from scratch. */
        v->real_pivot = v->pivot;
        v->pivot = -1;
    } else if (v->real_pivot >= 0) {
        v->pivot = v->real_pivot;
        v->real_pivot = -1;
    }
}

void
tiku_trk_poseview_show_selection_when_inactive(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->show_sel_inactive = on ? 1 : 0;
        if (on) {
            v->selection_visible = 1;
            if (v->real_pivot >= 0) {
                v->pivot = v->real_pivot;
                v->real_pivot = -1;
            }
        }
    }
}

void
tiku_trk_poseview_set_band(tiku_trk_poseview_t *v, int on)
{
    if (v == NULL) {
        return;
    }
    if (!on) {
        if (v->band_seen) {
            /* The sweep is over and the band is not drawn again, so where
             * it was lying is what needs painting.  The frame that ends a
             * sweep usually has nothing else to go on and paints the whole
             * listing regardless; this is for the one that does -- a value
             * arriving mid-sweep leaves that frame partial, and the band
             * would stay on the ground it was over.  No test can force
             * that race, which is why it is written down here. */
            tiku_trk_repaint_add(&v->repaint, v->band_rect);
        }
        v->band_seen = 0;       /* the next sweep is a new one */
    }
    if (on && v->band_was != NULL) {
        int i;

        /* The band toggles against the selection as it was when the sweep
         * began, so the snapshot has to be taken here and not recomputed
         * from the live flags the sweep is busy changing. */
        for (i = 0; i < v->count; i++) {
            v->band_was[i] = (unsigned char)(v->poses[i].selected ? 1 : 0);
        }
    }
    if (!on && v->pivot >= 0 && v->pivot < v->count &&
        !v->poses[v->pivot].selected) {
        /* A pivot the sweep left UNSELECTED is dropped: the next
         * shift-click would otherwise extend from a pose that is no longer
         * part of anything (PVS-029). */
        v->pivot = -1;
    }
    v->band = on ? 1 : 0;
}

int
tiku_trk_poseview_band(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->band : 0;
}

void
tiku_trk_poseview_damage_row(tiku_trk_poseview_t *v, int index)
{
    int rh;

    if (v == NULL || index < 0 || index >= v->count) {
        return;
    }
    rh = (v->row_h > 0) ? v->row_h : 1;
    tiku_trk_repaint_add(&v->repaint,
        (tiku_trk_repaint_rect_t){ 0, (index - v->scroll) * rh,
                                   (v->view_w > 0) ? v->view_w : 1, rh });
}

int
tiku_trk_poseview_repaint_take(tiku_trk_poseview_t *v,
                               tiku_trk_repaint_rect_t *out, int max)
{
    return v != NULL ? tiku_trk_repaint_take(&v->repaint, out, max) : 0;
}

int
tiku_trk_poseview_repaint_take_slide(tiku_trk_poseview_t *v, int *y)
{
    return v != NULL ? tiku_trk_repaint_take_slide(&v->repaint, y) : 0;
}

/*---------------------------------------------------------------------------*/
/* keyboard -- PVK-001..010                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Row of the current list holding the active pose, or -1. */
static int
active_row(tiku_trk_poseview_t *v)
{
    int i, n = tiku_trk_poseview_count(v);

    for (i = 0; i < n; i++) {
        if (tiku_trk_poseview_at(v, i)->index == v->active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief The pose an arrow from @p row lands on in an icon mode (PVK-007).
 *
 * Tracker's own rule: among the poses in the half-plane the key points at,
 * the nearest by distance, with movement ALONG the key's axis counting for
 * more than movement across it -- otherwise "down" from a lone icon picks
 * whatever happens to be nearest, which is usually beside it.
 *
 * @return a display index, or -1 when there is nothing that way.
 */
static int
nearest_in_direction(tiku_trk_poseview_t *v, int row, int key)
{
    int i, n = tiku_trk_poseview_count(v), best = -1;
    long best_score = 0;
    int cx = 0, cy = 0;

    if (n == 0) {
        return -1;
    }
    if (row < 0) {
        /* PVK-008: with nothing selected the first arrow picks the
         * upper-left-most pose, whichever key it was. */
        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
            int px, py, pw, ph;

            if (p == NULL || !p->placed) {
                continue;
            }
            tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
            if (best < 0 || py < cy || (py == cy && px < cx)) {
                best = i;
                cx = px;
                cy = py;
            }
        }
        return best;
    }
    {
        tiku_trk_pose_t *from = tiku_trk_poseview_at(v, row);
        int px, py, pw, ph;

        if (from == NULL) {
            return -1;
        }
        tiku_trk_poseview_pose_rect(v, from, &px, &py, &pw, &ph);
        cx = px + pw / 2;
        cy = py + ph / 2;
    }
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph, mx, my;
        long along, across, score;

        if (p == NULL || !p->placed || i == row) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        mx = px + pw / 2;
        my = py + ph / 2;
        switch (key) {
        case TIKU_TRK_KEY_UP:
            if (my >= cy) { continue; }
            along = cy - my;  across = (mx > cx) ? mx - cx : cx - mx;
            break;
        case TIKU_TRK_KEY_DOWN:
            if (my <= cy) { continue; }
            along = my - cy;  across = (mx > cx) ? mx - cx : cx - mx;
            break;
        case TIKU_TRK_KEY_LEFT:
            if (mx >= cx) { continue; }
            along = cx - mx;  across = (my > cy) ? my - cy : cy - my;
            break;
        default:
            if (mx <= cx) { continue; }
            along = mx - cx;  across = (my > cy) ? my - cy : cy - my;
            break;
        }
        /* Across counts double, which is what keeps a step down inside its
         * own column when the next column's icon is marginally closer. */
        score = along + 2 * across;
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

int
tiku_trk_poseview_key_nav(tiku_trk_poseview_t *v, int key, unsigned mods)
{
    int n, row, target;

    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    if (n == 0) {
        return 1;
    }
    /* PVK-003 keys on the SELECTION being empty, not on whether some pose
     * was active earlier: after Select None an arrow must seed, even though
     * the last-clicked pose is still remembered. */
    row = (tiku_trk_poseview_selection_count(v) == 0) ? -1 : active_row(v);

    if (v->mode != TIKU_TRK_MODE_LIST &&
        (key == TIKU_TRK_KEY_UP || key == TIKU_TRK_KEY_DOWN ||
         key == TIKU_TRK_KEY_LEFT || key == TIKU_TRK_KEY_RIGHT)) {
        /* Geometrically, not by index: icons are laid out in two
         * dimensions and walking the sort order would jump across the
         * window (PVK-007). */
        target = nearest_in_direction(v, row, key);
        if (target < 0) {
            return 1;           /* nothing that way; the selection stays */
        }
    } else {
    switch (key) {
    case TIKU_TRK_KEY_UP:
    case TIKU_TRK_KEY_LEFT:
        if (row < 0) {
            target = n - 1;         /* PVK-003: seed at the last pose */
        } else if (row == 0) {
            /* PVK-002: Left wraps, Up does not. */
            target = (key == TIKU_TRK_KEY_LEFT) ? n - 1 : 0;
        } else {
            target = row - 1;
        }
        break;
    case TIKU_TRK_KEY_DOWN:
    case TIKU_TRK_KEY_RIGHT:
        if (row < 0) {
            target = 0;             /* PVK-003: seed at the first pose */
        } else if (row == n - 1) {
            target = (key == TIKU_TRK_KEY_RIGHT) ? 0 : n - 1;
        } else {
            target = row + 1;
        }
        break;
    case TIKU_TRK_KEY_HOME:  target = 0;     break;
    case TIKU_TRK_KEY_END:   target = n - 1; break;
    case TIKU_TRK_KEY_PGUP:
    case TIKU_TRK_KEY_PGDN: {
        /* A page is what the window can SHOW, not a constant: paging by
         * twenty rows in a window that holds eight skips twelve of them
         * unseen, which is the one thing paging must not do. */
        int page = (v->visible_rows > 1) ? v->visible_rows - 1 : 1;
        /* From the first VISIBLE row, not the selected one: a page is a
         * movement of the window, and stepping it from a selection six
         * screens away pages somewhere the user is not looking
         * (PVK-011). */
        int base = (row >= 0 && row >= v->scroll &&
                    row < v->scroll + v->visible_rows) ? row : v->scroll;

        if (n == 0) {
            target = -1;
        } else if (key == TIKU_TRK_KEY_PGUP) {
            target = (base > page) ? base - page : 0;
        } else {
            target = (base + page < n) ? base + page : n - 1;
        }
        break;
    }
    default:
        return 0;
    }

    /* PVK-010/PVL-055: in an icon mode Home/End/Page move the VIEW and leave
     * the selection alone -- there is no "first row" to select when the
     * poses are arranged in two dimensions, and jumping the selection to a
     * corner of the layout is not what the key means there. */
    if (v->no_autoscroll &&
        (key == TIKU_TRK_KEY_HOME || key == TIKU_TRK_KEY_END ||
         key == TIKU_TRK_KEY_PGUP || key == TIKU_TRK_KEY_PGDN)) {
        /* The Desktop is exempt: it does not scroll for anyone
         * (PVK-013). */
        return 1;
    }
    if (v->mode != TIKU_TRK_MODE_LIST &&
        (key == TIKU_TRK_KEY_HOME || key == TIKU_TRK_KEY_END ||
         key == TIKU_TRK_KEY_PGUP || key == TIKU_TRK_KEY_PGDN)) {
        /* In pixels, against the extent: an icon layout has no rows to
         * count, and the row index these keys used to write was read by
         * nothing that draws icons (PVK-010). */
        int ex, ey, ew, eh, ox, oy;
        int page = (v->view_h > 1) ? v->view_h : 1;

        tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
        tiku_trk_poseview_origin(v, &ox, &oy);
        switch (key) {
        case TIKU_TRK_KEY_HOME: oy = ey; break;
        case TIKU_TRK_KEY_END:  oy = ey + eh; break;
        case TIKU_TRK_KEY_PGUP: oy -= page; break;
        default:                oy += page; break;
        }
        /* The SELECTION is deliberately untouched: in an icon mode these
         * keys move the view, and moving the selection as well would lose
         * what the user gathered before scrolling to look at something. */
        tiku_trk_poseview_set_origin(v, ox, oy);
        return 1;
    }

    }
    if ((mods & TIKU_TRK_MOD_SHIFT) && !v->single_only &&
        (key == TIKU_TRK_KEY_HOME || key == TIKU_TRK_KEY_END ||
         key == TIKU_TRK_KEY_PGUP || key == TIKU_TRK_KEY_PGDN)) {
        /* A jump key with Shift selects EVERYTHING between where the
         * selection stood and where the jump lands -- Shift+End extends
         * to the end, not by one row (PVK-012). */
        int from = (row >= 0) ? row : target;
        int lo = (from < target) ? from : target;
        int hi = (from > target) ? from : target;
        int r2;

        for (r2 = lo; r2 <= hi; r2++) {
            tiku_trk_pose_t *t2 = tiku_trk_poseview_at(v, r2);

            if (t2 != NULL) {
                set_selected(t2, 1);
            }
        }
        {
            tiku_trk_pose_t *t2 = tiku_trk_poseview_at(v, target);

            if (t2 != NULL) {
                v->last_added = t2->index;
                if (v->pivot < 0) {
                    v->pivot = (row >= 0)
                                   ? tiku_trk_poseview_at(v, row)->index
                                   : t2->index;
                }
            }
        }
    } else if ((mods & TIKU_TRK_MOD_SHIFT) && !v->single_only) {
        /* PVK-005: grow by one, or walk back when the target is already in
         * the selection.  In a single-selection view Shift adds nothing --
         * it is a plain arrow (PVK-006). */
        tiku_trk_pose_t *t = tiku_trk_poseview_at(v, target);

        if (t != NULL && t->selected && v->last_added >= 0) {
            tiku_trk_pose_t *last = tiku_trk_poseview_at(v,
                                        display_of(v, v->last_added));

            if (last != NULL) {
                last->selected = 0;
            }
            /* The walk-back has to MOVE the mark, or the second step drops
             * the pose the first one already dropped and the selection
             * stops shrinking.  It becomes the target, which is the pose
             * the walk is now standing on. */
            v->last_added = t->index;
        } else if (t != NULL) {
            set_selected(t, 1);
            v->last_added = t->index;
        }
        if (v->pivot < 0 && t != NULL) {
            v->pivot = t->index;
        }
    } else {
        /* PVK-004: a plain arrow replaces the whole selection. */
        clear_selection(v);
        set_selected(tiku_trk_poseview_at(v, target), 1);
        v->pivot = tiku_trk_poseview_at(v, target)->index;
        v->last_added = v->pivot;
    }
    v->active = tiku_trk_poseview_at(v, target)->index;
    /* PVL-054: a selection that moved is a selection to be looked at. */
    tiku_trk_poseview_scroll_into_view(v, target, v->visible_rows);
    return 1;
}

/*---------------------------------------------------------------------------*/
/* type-ahead                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Tracker's scoring: starts-with always wins, else 1/offset.
 *
 * @return Score, higher is better; 0 means not a candidate.
 */
static float
type_score(const char *name, const char *typed)
{
    const char *hit;

    if (typed[0] == '\0') {
        return 0.0f;
    }
    if (strncasecmp(name, typed, strlen(typed)) == 0) {
        return 1000.0f;
    }
    hit = strcasestr(name, typed);
    if (hit == NULL) {
        return 0.0f;
    }
    return 1.0f / (float)(hit - name + 1);
}

int
tiku_trk_poseview_filter_pose(tiku_trk_poseview_t *v,
                              const tiku_trk_pose_t *p)
{
    unsigned char found[TIKU_TRK_FILTER_TERMS];
    int left, i, j;

    if (v == NULL || p == NULL) {
        return 0;
    }
    left = v->filter_count;
    memset(found, 0, sizeof found);
    /* An empty term matches everything, and is counted straight away: it is
     * the state the filter is in before a character is typed, and treating
     * it as unmatched would blank the listing on entry. */
    for (j = 0; j < v->filter_count; j++) {
        if (v->filter[j][0] == '\0') {
            found[j] = 1;
            left--;
        }
    }
    if (left == 0) {
        return 1;
    }
    /* Every COLUMN is searched, not the name: each term may land in a
     * different one, which is what makes two short terms selective. */
    for (i = 0; i < v->column_count; i++) {
        char buf[160];
        const char *text = tiku_trk_poseview_cell_text(v, p,
                                                       v->columns[i].field,
                                                       buf, sizeof buf);
        if (text == NULL) {
            continue;
        }
        for (j = 0; j < v->filter_count; j++) {
            if (found[j]) {
                continue;
            }
            if (strcasestr(text, v->filter[j]) != NULL) {
                if (--left == 0) {
                    return 1;
                }
                found[j] = 1;
            }
        }
    }
    return 0;
}

static void
rebuild_filter(tiku_trk_poseview_t *v)
{
    int i;

    v->filtered_count = 0;
    for (i = 0; i < v->count; i++) {
        if (tiku_trk_poseview_filter_pose(v, &v->poses[i])) {
            v->filtered[v->filtered_count++] = i;
        } else {
            /* A row that has left the listing cannot stay in the selection:
             * a later command would act on something the user cannot see. */
            v->poses[i].selected = 0;
        }
    }
}

int
tiku_trk_poseview_filter_terms(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->filter_count : 0;
}

/** @brief The last term, which is the one being typed into. */
static char *
last_term(tiku_trk_poseview_t *v)
{
    return v->filter[(v->filter_count > 0) ? v->filter_count - 1 : 0];
}

int
tiku_trk_poseview_filter_term(tiku_trk_poseview_t *v)
{
    if (v == NULL || !v->filtering) {
        return 0;
    }
    if (last_term(v)[0] == '\0') {
        return 0;                   /* refuse: an empty term widens it     */
    }
    if (v->filter_count >= TIKU_TRK_FILTER_TERMS) {
        return 0;
    }
    v->filter[v->filter_count++][0] = '\0';
    return 1;
}

/**
 * @brief Re-apply the filter after a term changed (TypeAheadFilteringChanged).
 *
 * A term that GREW can only take rows away, so the filtered list is walked
 * and thinned in place.  A term that shrank, or a term that went away, can
 * bring rows back, and nothing but a full rebuild can find them.
 */
static void
filter_changed(tiku_trk_poseview_t *v, int grew)
{
    if (!v->filtering) {
        return;
    }
    if (grew) {
        int i, w = 0;

        for (i = 0; i < v->filtered_count; i++) {
            if (tiku_trk_poseview_filter_pose(v, &v->poses[v->filtered[i]])) {
                v->filtered[w++] = v->filtered[i];
            } else {
                v->poses[v->filtered[i]].selected = 0;
            }
        }
        v->filtered_count = w;
    } else {
        rebuild_filter(v);
    }
    /* PVL-061: a shrinking list must not leave the window scrolled past its
     * own last row. */
    if (v->visible_rows > 0 && v->scroll > 0 &&
        v->scroll + v->visible_rows > v->filtered_count) {
        v->scroll = v->filtered_count - v->visible_rows;
        if (v->scroll < 0) {
            v->scroll = 0;
        }
    }
}

/** @brief Jump the selection to the best match for the typed string. */
static void
type_select_best(tiku_trk_poseview_t *v)
{
    int i, n = tiku_trk_poseview_count(v), best = -1;
    float bestscore = 0.0f;

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        float s = type_score(p->model.name, v->typed);

        if (v->mode == TIKU_TRK_MODE_LIST) {
            /* Every COLUMN participates, as the filter's walk does: a
             * kind or a value typed at the list should land on the row
             * that shows it (PVK-029). */
            int c;

            for (c = 0; c < v->column_count; c++) {
                char buf[160];
                const char *text = tiku_trk_poseview_cell_text(v, p,
                    v->columns[c].field, buf, sizeof buf);
                float cs = (text != NULL) ? type_score(text, v->typed)
                                          : 0.0f;

                if (cs > s) {
                    s = cs;
                }
            }
        }
        if (s > bestscore) {
            bestscore = s;
            best = i;
        }
    }
    if (best >= 0) {
        clear_selection(v);
        set_selected(tiku_trk_poseview_at(v, best), 1);
        v->pivot = best;
        v->last_added = best;
        v->active = tiku_trk_poseview_at(v, best)->index;
        /* Typing a name that is off screen must SHOW it: a selection the
         * user cannot see is one they cannot tell happened (PVL-054). */
        tiku_trk_poseview_scroll_into_view(v, best, v->visible_rows);
    }
}

void
tiku_trk_poseview_type_ahead(tiku_trk_poseview_t *v, char ch)
{
    if (v == NULL) {
        return;
    }
    if (v->filtering) {
        char *t = last_term(v);
        size_t n = strlen(t);

        if (n + 1u >= TIKU_TRK_TYPEAHEAD_MAX) {
            return;
        }
        t[n] = ch;
        t[n + 1u] = '\0';
        filter_changed(v, 1);
        return;
    }
    if (v->typed_len + 1 >= TIKU_TRK_TYPEAHEAD_MAX) {
        return;
    }
    v->typed[v->typed_len++] = ch;
    v->typed[v->typed_len] = '\0';
    v->typed_at = tiku_trk_now();
    type_select_best(v);
}

void
tiku_trk_poseview_type_backspace(tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return;
    }
    if (v->filtering) {
        char *t = last_term(v);
        size_t n = strlen(t);

        if (n > 0u) {
            t[n - 1u] = '\0';
        } else if (v->filter_count > 1) {
            /* Backspacing off the front of a term removes the term, which is
             * how the '|' separators come back off one at a time. */
            v->filter_count--;
        }
        filter_changed(v, 0);
        return;
    }
    if (v->typed_len == 0) {
        return;
    }
    v->typed[--v->typed_len] = '\0';
    v->typed_at = tiku_trk_now();   /* backspace restarts the timeout */
    if (v->typed_len > 0) {
        type_select_best(v);
    }
}

void
tiku_trk_poseview_type_clear(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->typed[0] = '\0';
        v->typed_len = 0;
        v->typed_at = 0;
        v->filter_count = 1;
        v->filter[0][0] = '\0';
        if (v->filtering) {
            rebuild_filter(v);
        }
    }
}

const char *
tiku_trk_poseview_type_string(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return "";
    }
    if (v->filtering) {
        /* Terms are shown joined by '|', as the count view shows them: the
         * separator is the only thing on screen that says two terms are in
         * force rather than one long one. */
        tiku_trk_poseview_t *m = (tiku_trk_poseview_t *)v;
        size_t at = 0;
        int i;

        m->filter_shown[0] = '\0';
        for (i = 0; i < v->filter_count; i++) {
            int n = snprintf(m->filter_shown + at,
                             sizeof m->filter_shown - at, "%s%s",
                             (i > 0) ? "|" : "", v->filter[i]);
            if (n < 0 || (size_t)n >= sizeof m->filter_shown - at) {
                break;
            }
            at += (size_t)n;
        }
        return m->filter_shown;
    }
    return v->typed;
}

void
tiku_trk_poseview_type_tick(tiku_trk_poseview_t *v)
{
    if (v == NULL || v->typed_len == 0 || v->typed_at == 0) {
        return;
    }
    if (tiku_trk_now() - v->typed_at > TIKU_TRK_TYPEAHEAD_US) {
        tiku_trk_poseview_type_clear(v);
    }
}

void
tiku_trk_poseview_set_filtering(tiku_trk_poseview_t *v, int on)
{
    if (v == NULL) {
        return;
    }
    if (!on) {
        /* Leaving the mode drops the terms: they are not a setting, they are
         * a query, and a stale one would silently hide rows next time. */
        v->filter_count = 1;
        v->filter[0][0] = '\0';
    }
    v->filtering = on ? 1 : 0;
    rebuild_filter(v);
    if (!on) {
        v->scroll = 0;
    }
}

int
tiku_trk_poseview_is_filtering(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->filtering : 0;
}

int
tiku_trk_poseview_return_pressed(tiku_trk_poseview_t *v, int shift)
{
    if (v == NULL) {
        return 0;
    }
    if (v->filtering && tiku_trk_poseview_selection_count(v) == 0 &&
        tiku_trk_poseview_count(v) > 0) {
        /* PVK-015: the filter has already picked the row out; asking the
         * user to arrow onto it first would make the filter half a tool. */
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, 0);

        clear_selection(v);
        set_selected(p, 1);
        v->pivot = p->index;
        v->last_added = p->index;
        v->active = p->index;
    }
    /* PVK-016: Shift says "and put the listing back", so the window is not
     * left filtered around a folder the user has just left. */
    return (shift && v->filtering) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* sorting                                                                   */
/*---------------------------------------------------------------------------*/

void
tiku_trk_poseview_sort_by(tiku_trk_poseview_t *v, int field)
{
    if (v == NULL) {
        return;
    }
    /* The same column again reverses; a different column sorts ascending. */
    if (v->sort_field == field) {
        v->sort_reverse = !v->sort_reverse;
    } else {
        v->sort_field = field;
        v->sort_reverse = 0;
    }
    resort(v);
    if (v->filtering) {
        rebuild_filter(v);
    }
}

int
tiku_trk_poseview_sort_field(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->sort_field : 0;
}

int
tiku_trk_poseview_sort_reverse(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->sort_reverse : 0;
}

int
tiku_trk_poseview_sort_secondary(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->sort2_field : -1;
}

void
tiku_trk_poseview_set_secondary(tiku_trk_poseview_t *v, int field)
{
    if (v == NULL) {
        return;
    }
    /* A secondary that no longer has a column is no secondary at all: it
     * would order the listing by something nothing on screen shows. */
    if (field >= 0 && !tiku_trk_poseview_has_column(v, field)) {
        field = -1;
    }
    v->sort2_field = (field == v->sort_field) ? -1 : field;
    resort(v);
    if (v->filtering) {
        rebuild_filter(v);
    }
}

void
tiku_trk_poseview_sort_click(tiku_trk_poseview_t *v, int field, int shift)
{
    if (v == NULL) {
        return;
    }
    if (field == v->sort_field) {
        if (shift && v->sort2_field >= 0) {
            /* TS-003: swap the two, so the tie-break becomes the key and
             * the key becomes the tie-break. */
            int old = v->sort_field;

            v->sort_field = v->sort2_field;
            v->sort2_field = old;
        } else {
            /* With no secondary there is nothing to swap, so Shift falls
             * through to the plain reversal rather than doing nothing. */
            v->sort_reverse = !v->sort_reverse;
        }
    } else if (shift) {
        /* TS-004: Shift on another column sets the tie-break, or clears it
         * when that column is already the tie-break. */
        v->sort2_field = (field == v->sort2_field) ? -1 : field;
    } else {
        v->sort_field = field;
        v->sort_reverse = 0;
    }
    if (v->sort2_field == v->sort_field) {
        v->sort2_field = -1;
    }
    resort(v);
    if (v->filtering) {
        rebuild_filter(v);
    }
    v->state_dirty = 1;
}

/*---------------------------------------------------------------------------*/
/* icon mode, layout, spatial memory (PVL-012..021)                          */
/*---------------------------------------------------------------------------*/

int
tiku_trk_poseview_mode(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->mode : TIKU_TRK_MODE_LIST;
}

int
tiku_trk_poseview_icon_size(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->icon_size : 32;
}

void
tiku_trk_poseview_set_icon_size(tiku_trk_poseview_t *v, int px)
{
    if (v != NULL && px >= 16 && px <= 128) {
        v->icon_size = px;
        v->state_dirty = 1;
    }
}

/** @brief The size ladder, in the order Cmd-+ walks it (PVL-003). */
static const int icon_ladder[] = { 32, 40, 48, 64, 96, 128 };

static void extent_add(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p);
static void extent_clear(tiku_trk_poseview_t *v);
static void extent_recalc(tiku_trk_poseview_t *v);

static float mode_scale(int mode, int size);

int
tiku_trk_poseview_step_icon_size(tiku_trk_poseview_t *v, int up)
{
    int i, n = (int)(sizeof icon_ladder / sizeof icon_ladder[0]);

    if (v == NULL || v->mode != TIKU_TRK_MODE_ICON) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (icon_ladder[i] == v->icon_size) {
            break;
        }
    }
    if (i >= n) {
        /* Not on the ladder at all: step onto the nearest rung rather than
         * refusing, so a restored odd size is not a dead end. */
        for (i = 0; i < n - 1 && icon_ladder[i] < v->icon_size; i++) {
        }
    }
    /* The ends are a no-op, not a wrap: 128 stepping to 32 would look like
     * the display broke. */
    {
        /* What the middle of the window is looking at, in the UNSCALED
         * space positions are stored in -- the one thing that does not
         * change when the size does. */
        float was = mode_scale(v->mode, v->icon_size);
        float cx = ((float)v->icon_x + (float)v->view_w / 2.0f) / was;
        float cy = ((float)v->icon_y + (float)v->view_h / 2.0f) / was;

        if (up && i + 1 < n) {
            v->icon_size = icon_ladder[i + 1];
        } else if (!up && i > 0) {
            v->icon_size = icon_ladder[i - 1];
        } else {
            return 0;
        }
        /* Put that same point back in the middle.  Without this the view
         * jumps to the top-left on every size change, because the origin
         * is in pixels and the pixels all moved (PVL-010). */
        extent_clear(v);
        {
            float now = mode_scale(v->mode, v->icon_size);

            tiku_trk_poseview_set_origin(v,
                (int)(cx * now - (float)v->view_w / 2.0f),
                (int)(cy * now - (float)v->view_h / 2.0f));
        }
    }
    v->state_dirty = 1;
    return 1;
}

/** @brief The scale locations are drawn at in @p mode at @p size. */
static float
mode_scale(int mode, int size)
{
    return (mode == TIKU_TRK_MODE_ICON) ? (float)size / 32.0f : 1.0f;
}

/**
 * @brief Carry existing positions from one icon grid into another (PVL-011).
 *
 * The whole-cell count and the offset WITHIN the cell are converted
 * separately: converting the raw coordinate would rescale a deliberate
 * nudge into a different cell, and converting only the cell count would
 * lose it.  Together they keep the arrangement recognisable across a mode
 * whose cell is a different shape.
 */
static void
map_icons(tiku_trk_poseview_t *v, int old_mode, int old_size)
{
    int ogx, ogy, oox, ooy, ngx, ngy, nox, noy, i;
    float oscale = mode_scale(old_mode, old_size);
    float nscale = mode_scale(v->mode, v->icon_size);
    int saved_mode = v->mode, saved_size = v->icon_size;

    v->mode = old_mode;
    v->icon_size = old_size;
    tiku_trk_poseview_layout_info(v, &ogx, &ogy, &oox, &ooy);
    v->mode = saved_mode;
    v->icon_size = saved_size;
    tiku_trk_poseview_layout_info(v, &ngx, &ngy, &nox, &noy);
    if (ogx <= 0 || ogy <= 0 || ngx <= 0 || ngy <= 0) {
        return;
    }
    for (i = 0; i < v->count; i++) {
        tiku_trk_pose_t *p = &v->poses[i];
        int x, y, px, py, dx, dy;

        if (!p->placed) {
            continue;
        }
        x = (int)((float)p->loc_x * oscale);
        y = (int)((float)p->loc_y * oscale);
        px = x;
        py = y;
        {   /* Pin to the OLD grid to find the cell and the nudge. */
            float fx = (float)(px - oox) / (float)ogx;
            float fy = (float)(py - ooy) / (float)ogy;
            px = (int)((fx >= 0.0f) ? (fx + 0.5f) : (fx - 0.5f)) * ogx + oox;
            py = (int)((fy >= 0.0f) ? (fy + 0.5f) : (fy - 0.5f)) * ogy + ooy;
        }
        dx = x - px;
        dy = y - py;
        px -= oox;
        py -= ooy;
        px = (px >= 0) ? (px / ogx) * ngx : -(((-px) + ogx - 1) / ogx) * ngx;
        py = (py >= 0) ? (py / ogy) * ngy : -(((-py) + ogy - 1) / ogy) * ngy;
        if (dx != 0 || dy != 0) {
            dx = (dx >= 0) ? ngx * (dx / ogx) : -ngx * (((-dx) + ogx - 1) / ogx);
            dy = (dy >= 0) ? ngy * (dy / ogy) : -ngy * (((-dy) + ogy - 1) / ogy);
            px += dx;
            py += dy;
        }
        px += nox;
        py += noy;
        p->loc_x = (int)((float)px / nscale);
        p->loc_y = (int)((float)py / nscale);
    }
    v->state_dirty = 1;
}

void
tiku_trk_poseview_hint(const tiku_trk_poseview_t *v, int *x, int *y)
{
    if (v == NULL) {
        return;
    }
    if (x != NULL) { *x = v->hint_x; }
    if (y != NULL) { *y = v->hint_y; }
}

void
tiku_trk_poseview_reset_hint(tiku_trk_poseview_t *v)
{
    int gx, gy, ox, oy;

    if (v == NULL) {
        return;
    }
    /* PVL-017: back to the top-left of what is on screen.  Left where it
     * was, the next folder's auto-placement carries on from the last
     * folder's last icon and the first row lands halfway down. */
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    v->hint_x = ox;
    v->hint_y = oy;
}

void
tiku_trk_poseview_set_mode(tiku_trk_poseview_t *v, int mode)
{
    int old_mode;

    if (v == NULL || v->mode == mode ||
        (v->is_query && mode != TIKU_TRK_MODE_LIST)) {
        return;
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* Leaving list mode cancels type-ahead filtering, which only makes
         * sense against column text (PVL-005). */
        tiku_trk_poseview_type_clear(v);
        tiku_trk_poseview_set_filtering(v, 0);
    }
    old_mode = v->mode;
    v->mode = mode;
    v->state_dirty = 1;
    /* Between the two icon modes the cell changes shape, so what was laid
     * out has to be carried across; list mode has no grid to carry into.
     * Leaving LIST, the stored positions are in whatever icon grid was
     * last live -- the fLastIconMode the record kept -- so they are
     * mapped from THAT, not taken at face value (MA-061). */
    if (mode != TIKU_TRK_MODE_LIST) {
        int from = (old_mode != TIKU_TRK_MODE_LIST) ? old_mode
                                                    : v->last_icon;

        if (from != mode) {
            map_icons(v, from, v->icon_size);
        }
        v->last_icon = mode;
    }
    /* Every rect changed shape, so the box built from the old ones is a
     * box around nothing; and the origin this mode kept is re-clamped
     * against it, since the mode it was saved under may have been bigger. */
    extent_clear(v);
    {
        int x, y;

        tiku_trk_poseview_origin(v, &x, &y);
        tiku_trk_poseview_set_origin(v, x, y);
    }
    tiku_trk_poseview_reset_hint(v);
}

void
tiku_trk_poseview_layout_info(const tiku_trk_poseview_t *v, int *gx, int *gy,
                              int *ox, int *oy)
{
    int s;

    if (v == NULL) {
        return;
    }
    s = tiku_trk_poseview_art_size(v);
    switch (v->mode) {
    case TIKU_TRK_MODE_MINI:
        /* PVL-013: a wide, short cell, because a mini-icon row is a label.
         * Its HEIGHT is the taller of the art and the label, since the two
         * sit side by side (PVL-007). */
        *gx = s * 6;
        *gy = (v->font_h > 0 && v->font_h + 1 > s) ? v->font_h + 1 : s;
        *ox = (int)(s * 0.6f + 0.999f);
        *oy = (int)(s * 0.3f + 0.999f);
        break;
    case TIKU_TRK_MODE_ICON:
        *gx = s + (int)(s * 0.875f + 0.999f);
        /* The label sits UNDER the icon, so the cell is as tall as both:
         * a height derived from the art alone clips the name at one font
         * and leaves it floating at another (PVL-007). */
        *gy = (v->font_h > 0) ? s + v->font_h + 1 : *gx;
        *ox = (int)(s * 0.625f + 0.999f);
        *oy = *ox;
        break;
    default:
        *gx = 0;                  /* no grid in list mode */
        *gy = 0;
        *ox = 3;
        *oy = 3;
        break;
    }
}

void
tiku_trk_poseview_pin_to_grid(const tiku_trk_poseview_t *v, int *x, int *y)
{
    int gx, gy, ox, oy;

    if (v == NULL) {
        return;
    }
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    if (gx == 0 || gy == 0) {
        return;                   /* PVL-014: a no-op with no grid */
    }
    /* Round to the NEAREST cell, in both directions. */
    {
        float fx = (float)(*x - ox) / (float)gx;
        float fy = (float)(*y - oy) / (float)gy;
        *x = (int)((fx >= 0.0f) ? (fx + 0.5f) : (fx - 0.5f)) * gx + ox;
        *y = (int)((fy >= 0.0f) ? (fy + 0.5f) : (fy - 0.5f)) * gy + oy;
    }
}

int
tiku_trk_poseview_open_anim_rect(const tiku_trk_poseview_t *v, int row,
                                 int row_h, int *x, int *y, int *w, int *h)
{
    const tiku_trk_pose_t *p;

    if (v == NULL || row < 0 || row >= tiku_trk_poseview_count(v)) {
        return 0;
    }
    p = v->filtering ? &v->poses[v->filtered[row]] : &v->poses[row];
    if (v->mode != TIKU_TRK_MODE_LIST) {
        int cell = (v->mode == TIKU_TRK_MODE_MINI) ? 16 : 32;
        int px = 0;
        int py = 0;

        tiku_trk_poseview_pose_location(v, p, &px, &py);
        *x = px;
        *y = py;
        *w = cell;
        *h = cell;
        return 1;
    }
    *x = 2;
    *y = 2 + (row - v->scroll) * row_h;
    /* The MEASURED first column, not a guess (PVS-013). */
    *w = (v->column_count > 0) ? v->columns[0].width : 200;
    *h = row_h;
    return 1;
}

void
tiku_trk_poseview_pose_location(const tiku_trk_poseview_t *v,
                                const tiku_trk_pose_t *p, int *x, int *y)
{
    float scale;

    if (v == NULL || p == NULL) {
        return;
    }
    /* Stored unscaled at 32 px and scaled on the way out, so one arrangement
     * serves every icon size (PVL-012).  Mini and list mode do not scale. */
    scale = (v->mode == TIKU_TRK_MODE_ICON)
            ? (float)v->icon_size / 32.0f : 1.0f;
    *x = (int)((float)p->loc_x * scale);
    *y = (int)((float)p->loc_y * scale);
}

void
tiku_trk_poseview_move_pose(tiku_trk_poseview_t *v, tiku_trk_pose_t *p,
                            int x, int y, int snap)
{
    float scale;

    if (v == NULL || p == NULL) {
        return;
    }
    if (snap) {
        tiku_trk_poseview_pin_to_grid(v, &x, &y);
    }
    scale = (v->mode == TIKU_TRK_MODE_ICON)
            ? (float)v->icon_size / 32.0f : 1.0f;
    p->loc_x = (int)((float)x / scale);
    p->loc_y = (int)((float)y / scale);
    p->placed = 1;
    p->auto_placed = 0;           /* the user put it there */
    /* Grown in place.  A pose can only ever move the box OUTWARDS from
     * here; shrinking it needs the walk, and that is what removal does. */
    extent_add(v, p);
    v->state_dirty = 1;
}

/** @brief Cell footprint of one pose on screen. */
static void
pose_extent(const tiku_trk_poseview_t *v, int *w, int *h)
{
    int gx, gy, ox, oy;

    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    *w = (gx > 0) ? gx : v->icon_size;
    *h = (gy > 0) ? gy : v->icon_size;
}

/**
 * @brief Is the candidate slot taken?
 *
 * Only poses vertically near the candidate are inspected, so placement stays
 * cheap in a crowded folder (PVL-016).
 */
static int
slot_occupied(tiku_trk_poseview_t *v, int x, int y, const tiku_trk_pose_t *self)
{
    int i, w, h;

    pose_extent(v, &w, &h);
    for (i = 0; i < v->count; i++) {
        tiku_trk_pose_t *p = &v->poses[i];
        int px, py;

        if (p == self || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_location(v, p, &px, &py);
        if (py + h <= y || py >= y + h) {
            continue;             /* not in this band */
        }
        /* The footprint is GROWN horizontally, not shrunk: the margin exists
         * so two icons never quite touch, and insetting the other way lets
         * them overlap by exactly the margin meant to prevent it (PVL-015). */
        if (px + w + TIKU_TRK_PLACE_GAP > x &&
            px - TIKU_TRK_PLACE_GAP < x + w) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_poseview_art_size(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return TIKU_TRK_MINI_ICON;
    }
    /* Mini-icon mode is defined by its icon size, not merely by its grid:
     * drawing the icon-mode size in it produces full-size art in a short
     * cell, which is not a third mode at all. */
    return (v->mode == TIKU_TRK_MODE_ICON) ? v->icon_size
                                           : TIKU_TRK_MINI_ICON;
}

/** @brief Advance the hint to the next slot, wrapping at the right edge. */
static void
next_slot(tiku_trk_poseview_t *v, int view_w)
{
    int gx, gy, ox, oy, w, h;

    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    pose_extent(v, &w, &h);
    v->hint_x += (gx > 0) ? gx : w;
    if (v->hint_x + w > view_w) {
        v->hint_x = ox;
        v->hint_y += (gy > 0) ? gy : h;
        tiku_trk_poseview_pin_to_grid(v, &v->hint_x, &v->hint_y);
    }
}

static int take_landing(tiku_trk_poseview_t *v, const char *path, int *x,
                        int *y);

/** @brief Place one pose at the first free slot from the hint. */
static void
place_pose(tiku_trk_poseview_t *v, tiku_trk_pose_t *p, int view_w)
{
    int tries = 0;
    float scale = (v->mode == TIKU_TRK_MODE_ICON)
                  ? (float)v->icon_size / 32.0f : 1.0f;

    while (slot_occupied(v, v->hint_x, v->hint_y, p) && tries < 4096) {
        next_slot(v, view_w);
        tries++;
    }
    if (tries >= 4096) {
        /* PVL-019: nowhere free.  The icon goes to the ORIGIN rather than
         * to wherever the search happened to stop, which would be a
         * position far outside anything the user can see. */
        tiku_trk_poseview_reset_hint(v);
    }
    p->loc_x = (int)((float)v->hint_x / scale);
    p->loc_y = (int)((float)v->hint_y / scale);
    p->placed = 1;
    p->auto_placed = 1;
    next_slot(v, view_w);
}

int
tiku_trk_poseview_place_unplaced(tiku_trk_poseview_t *v, int view_w)
{
    int i, gx, gy, ox, oy, n = 0;

    if (v == NULL) {
        return 0;
    }
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    (void)gx;
    (void)gy;
    /* The hint restarts at the top-left of the VIEW, not the document
     * (PVL-017); this view does not scroll in icon mode yet, so they
     * coincide.  It is reset by the events that invalidate it -- a mode
     * change, a directory switch, a clear -- rather than guessed at from
     * being (0,0), which a legitimately-placed hint can also be. */
    if (v->hint_x == 0 && v->hint_y == 0) {
        v->hint_x = ox;
        v->hint_y = oy;
    }
    for (i = 0; i < v->count; i++) {
        int lx, ly;

        if (v->poses[i].placed) {
            continue;
        }
        /* A position asked for before the entry existed -- the point a drop
         * landed on, or the click that made a folder -- is honoured here
         * as well as on the notice, so a listing rebuilt from disk does not
         * put it back in the next free slot (PVL-025, PVL-098). */
        if (v->mode != TIKU_TRK_MODE_LIST &&
            take_landing(v, v->poses[i].model.path, &lx, &ly)) {
            float scale = mode_scale(v->mode, v->icon_size);

            v->poses[i].loc_x = (int)((float)lx / scale);
            v->poses[i].loc_y = (int)((float)ly / scale);
            v->poses[i].placed = 1;
            v->poses[i].auto_placed = 0;
            n++;
            continue;
        }
        place_pose(v, &v->poses[i], view_w);
        n++;
    }
    return n;
}

void
tiku_trk_poseview_cleanup(tiku_trk_poseview_t *v, int view_w)
{
    int i, gx, gy, ox, oy;

    if (v == NULL) {
        return;
    }
    /* List mode has no grid to clean up onto: laying out against gx=gy=0
     * would clear every saved position and stack the lot on the origin, so
     * an arrangement made in icon mode would not survive a stray Cmd+K in
     * list mode. */
    if (v->mode == TIKU_TRK_MODE_LIST) {
        return;
    }
    resort(v);
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    for (i = 0; i < v->count; i++) {
        v->poses[i].placed = 0;
    }
    v->hint_x = ox;
    v->hint_y = oy;
    (void)tiku_trk_poseview_place_unplaced(v, view_w);
    v->state_dirty = 1;
}

int
tiku_trk_poseview_snap_to_grid(tiku_trk_poseview_t *v)
{
    int i, moved = 0;

    if (v == NULL || v->mode == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    {
        /* Snapped in the space positions are STORED in, not in screen
         * pixels: a screen point converted to storage and back does not
         * always return itself, so a snap done on screen would move the
         * same icon again every time it was asked. */
        int gx, gy, ox, oy;
        float sc = mode_scale(v->mode, v->icon_size);
        float ugx, ugy, uox, uoy;

        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        if (gx <= 0 || gy <= 0 || sc <= 0.0f) {
            return 0;
        }
        ugx = (float)gx / sc;
        ugy = (float)gy / sc;
        uox = (float)ox / sc;
        uoy = (float)oy / sc;
        for (i = 0; i < v->count; i++) {
            tiku_trk_pose_t *p = &v->poses[i];
            int nx, ny;

            if (!p->placed) {
                continue;
            }
            nx = (int)(((float)p->loc_x - uox) / ugx + 0.5f) *
                 (int)(ugx + 0.5f) + (int)(uox + 0.5f);
            ny = (int)(((float)p->loc_y - uoy) / ugy + 0.5f) *
                 (int)(ugy + 0.5f) + (int)(uoy + 0.5f);
            if (nx == p->loc_x && ny == p->loc_y) {
                continue;
            }
            p->loc_x = nx;
            p->loc_y = ny;
            extent_add(v, p);
            moved++;
        }
    }
    if (moved > 0) {
        v->state_dirty = 1;
    }
    return moved;
}

void
tiku_trk_poseview_set_measure(tiku_trk_poseview_t *v, tiku_trk_measure_fn fn,
                              void *ctx)
{
    if (v != NULL) {
        v->measure = fn;
        v->measure_ctx = ctx;
    }
}

void
tiku_trk_poseview_set_font_metrics(tiku_trk_poseview_t *v, int height,
                                   int ascent)
{
    if (v != NULL && height > 0) {
        v->font_h = height;
        v->font_a = ascent;
    }
}

int
tiku_trk_poseview_row_height(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return 20;              /* the reference face's row */
    }
    if (v->row_h > 0) {
        /* What the owner actually DRAWS with.  Deriving a second answer
         * here is how a band came to sweep rows 27 px apart over a list
         * drawn 20 px apart, catching the row above the one under the
         * pointer. */
        return v->row_h;
    }
    if (v->font_h <= 0) {
        return 20;
    }
    /* Tracker's own: ceil(font height * 1.65), which is what leaves a mini
     * icon room in the row rather than touching both edges. */
    return (int)((float)v->font_h * 1.65f + 0.999f);
}

void
tiku_trk_poseview_set_folders_first(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL && v->folders_first != (on ? 1 : 0)) {
        v->folders_first = on ? 1 : 0;
        resort(v);
    }
}

void
tiku_trk_poseview_set_hide_dotfiles(tiku_trk_poseview_t *v, int on)
{
    int value;

    if (v == NULL) {
        return;
    }
    value = on ? 1 : 0;
    if (v->hide_dotfiles == value) {
        return;
    }
    v->hide_dotfiles = value;
    (void)tiku_trk_poseview_reload(v);
}

int
tiku_trk_poseview_tab_next(tiku_trk_poseview_t *v, int backwards)
{
    tiku_trk_pose_t *active;
    const char *needle = "";
    int best = -1, i, n;
    const char *best_name = NULL;

    if (v == NULL || v->filtering || v->count <= 0) {
        return 0;
    }
    active = tiku_trk_poseview_active(v);
    if (active != NULL) {
        needle = active->model.name;
    }
    n = v->count;
    for (i = 0; i < n; i++) {
        const char *name = v->poses[i].model.name;
        int cmp = strcasecmp(name, needle);

        if ((!backwards && cmp > 0) || (backwards && cmp < 0)) {
            if (best < 0 || (!backwards && strcasecmp(name, best_name) < 0) ||
                (backwards && strcasecmp(name, best_name) > 0)) {
                best = i;
                best_name = name;
            }
        }
    }
    if (best < 0) {
        for (i = 0; i < n; i++) {
            const char *name = v->poses[i].model.name;

            if (best < 0 || (!backwards && strcasecmp(name, best_name) < 0) ||
                (backwards && strcasecmp(name, best_name) > 0)) {
                best = i;
                best_name = name;
            }
        }
    }
    if (best < 0) {
        return 0;
    }
    tiku_trk_poseview_click(v, best, 0u);
    return 1;
}

void
tiku_trk_poseview_set_row_height(tiku_trk_poseview_t *v, int h)
{
    if (v != NULL) {
        v->row_h = (h > 0) ? h : 0;
    }
}

void
tiku_trk_poseview_set_icon_hit(tiku_trk_poseview_t *v, tiku_trk_hit_fn fn,
                               void *ctx)
{
    if (v != NULL) {
        v->icon_hit = fn;
        v->icon_hit_ctx = ctx;
    }
}

int
tiku_trk_poseview_fit_label(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p,
                            char *out, size_t max)
{
    const char *name;

    if (v == NULL || p == NULL || out == NULL || max == 0u) {
        return 0;
    }
    name = tiku_trk_model_display_name(&p->model);
    if (v->measure == NULL) {
        snprintf(out, max, "%s", name);
        return (int)strlen(out);
    }
    return tiku_trk_text_fit(name, TIKU_TRK_LABEL_CHARS * v->measure("M",
                                 v->measure_ctx),
                             TIKU_TRK_TRUNC_MIDDLE, v->measure,
                             v->measure_ctx, out, max);
}

void
tiku_trk_poseview_pose_parts(tiku_trk_poseview_t *v,
                             const tiku_trk_pose_t *p, int *ix, int *iy,
                             int *iw, int *ih, int *lx, int *ly, int *lw,
                             int *lh)
{
    int px, py, cw, ch, art, tw = 0;

    if (v == NULL || p == NULL) {
        return;
    }
    pose_extent(v, &cw, &ch);
    art = tiku_trk_poseview_art_size(v);
    tiku_trk_poseview_pose_location(v, p, &px, &py);
    if (v->measure != NULL) {
        /* Measured from the label as it will be DRAWN, elided and all: the
         * width taken from the whole name is a box that runs off across
         * the icons beside it (IV-047). */
        char fit[TIKU_TRK_NAME_MAX * 2];

        if (v->mode != TIKU_TRK_MODE_LIST &&
            tiku_trk_poseview_fit_label(v, p, fit, sizeof fit) > 0) {
            tw = v->measure(fit, v->measure_ctx);
        } else {
            tw = v->measure(tiku_trk_model_display_name(&p->model),
                            v->measure_ctx);
        }
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* A row, not a saved location: in a list the pose's place is its
         * INDEX, and the icon-mode location it also carries belongs to a
         * different arrangement entirely.  Answering with that one is how
         * a band swept over rows would catch the wrong ones. */
        int rh = tiku_trk_poseview_row_height(v);

        py = p->index * rh;
        if (ix != NULL) { *ix = 2; *iy = py; *iw = art; *ih = rh; }
        if (lx != NULL) {
            *lx = 2 + art + 4;
            *ly = py;
            *lw = tw;
            *lh = rh;
        }
        return;
    }
    if (v->mode == TIKU_TRK_MODE_MINI) {
        if (ix != NULL) { *ix = px; *iy = py; *iw = art; *ih = art; }
        if (lx != NULL) {
            *lx = px + art + 4;
            *ly = py;
            *lw = tw;
            *lh = (v->font_h > 0) ? v->font_h : art;
        }
        return;
    }
    /* Icon mode: the art centred in the cell, the label under it. */
    if (ix != NULL) {
        *ix = px + ((cw > art) ? (cw - art) / 2 : 0);
        *iy = py;
        *iw = art;
        *ih = art;
    }
    if (lx != NULL) {
        int h = (v->font_h > 0) ? v->font_h : 0;

        *lx = px + ((cw - tw) / 2);
        *ly = py + art + 1;
        *lw = tw;
        *lh = h;
    }
}

void
tiku_trk_poseview_pose_rect(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p,
                            int *x, int *y, int *w, int *h)
{
    int ix, iy, iw, ih, lx, ly, lw, lh, x0, y0, x1, y1;

    if (v == NULL || p == NULL) {
        return;
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* The whole ROW, not the union of the icon and the name: a list is
         * judged by row -- a click anywhere across it hits the pose -- so
         * a band that only caught the rows whose NAME it crossed would
         * sweep a different box from the one a click is judged against. */
        int rh = tiku_trk_poseview_row_height(v);

        *x = 0;
        *y = p->index * rh;
        *w = (v->view_w > 0) ? v->view_w : 1;
        *h = rh;
        return;
    }
    /* The UNION of the parts, derived rather than computed a second time:
     * two pieces of arithmetic for one rectangle is how a band comes to
     * sweep a different box from the one a click is judged against. */
    tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih, &lx, &ly, &lw,
                                 &lh);
    x0 = (lw > 0 && lx < ix) ? lx : ix;
    y0 = (lh > 0 && ly < iy) ? ly : iy;
    x1 = (lw > 0 && lx + lw > ix + iw) ? lx + lw : ix + iw;
    y1 = (lh > 0 && ly + lh > iy + ih) ? ly + lh : iy + ih;
    {
        /* Never narrower than the cell in mini mode, where the row IS the
         * cell and a short name would leave a gap nothing can be clicked
         * in. */
        int cw, ch;

        pose_extent(v, &cw, &ch);
        if (v->mode == TIKU_TRK_MODE_MINI && x1 - x0 < cw) {
            x1 = x0 + cw;
        }
    }
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
}

/** @brief Whether a point is inside a rectangle. */
static int
in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

tiku_trk_pose_t *
tiku_trk_poseview_pose_at(tiku_trk_poseview_t *v, int x, int y)
{
    int i, n;

    if (v == NULL) {
        return NULL;
    }
    n = tiku_trk_poseview_count(v);
    /* Last drawn wins, so the topmost icon under the point is the one hit. */
    for (i = n - 1; i >= 0; i--) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int ix, iy, iw, ih, lx, ly, lw, lh;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih, &lx, &ly,
                                     &lw, &lh);
        if (in_rect(x, y, lx, ly, lw, lh)) {
            return p;           /* the name is hit where it is drawn */
        }
        if (!in_rect(x, y, ix, iy, iw, ih)) {
            /* The gap between an icon and its name, and the space beside a
             * short label, belong to neither: in an icon mode the cell is
             * mostly empty, and treating the cell as the pose makes the
             * space BETWEEN icons undraggable (PVS-019). */
            continue;
        }
        if (v->icon_hit != NULL) {
            /* Opaque pixels only.  An icon is not a rectangle, and arming
             * from its transparent corner is what makes the gap between two
             * adjacent icons draggable at all (IV-026). */
            if (v->icon_hit(&p->model, iw, x - ix, y - iy,
                            v->icon_hit_ctx)) {
                return p;
            }
            continue;
        }
        return p;               /* no hook: the square, as before */
    }
    return NULL;
}

int
tiku_trk_poseview_select_rect(tiku_trk_poseview_t *v, int x0, int y0, int x1,
                              int y1, unsigned mods)
{
    int i, n, hits = 0;
    int lox = (x0 < x1) ? x0 : x1, hix = (x0 < x1) ? x1 : x0;
    int loy = (y0 < y1) ? y0 : y1, hiy = (y0 < y1) ? y1 : y0;

    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    /* The sliver between where the band WAS and where it is now, rather
     * than the whole view: a band that grows by a pixel repaints a pixel's
     * worth of rows (PVL-070). */
    {
        /* Full width, whatever the band's own shape: what gets painted
         * again is a ROW, and a band swept straight down is a rectangle
         * with no width at all. */
        tiku_trk_repaint_rect_t now = { 0, loy,
                                        (v->view_w > 0) ? v->view_w : 1,
                                        (hiy > loy) ? hiy - loy : 1 };

        (void)lox; (void)hix;

        if (v->band_seen) {
            tiku_trk_repaint_band(&v->repaint, v->band_rect, now);
        } else {
            tiku_trk_repaint_add(&v->repaint, now);
        }
        v->band_rect = now;
        v->band_seen = 1;
    }
    if (v->single_only) {
        /* A band cannot gather in a single-selection view: the additive
         * modifiers mean nothing, and the sweep stops at the first pose it
         * touches rather than lighting the row under every one (PVS-009). */
        mods &= ~(TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
    }
    /* Outside a tracked band the rectangle simply selects: with a modifier it
     * adds to what is there, without one it replaces. */
    if (!v->band && !(mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD))) {
        clear_selection(v);
    }
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph, inside;

        if (p == NULL ||
            (!p->placed && v->mode != TIKU_TRK_MODE_LIST)) {
            /* An unplaced pose has no rectangle to sweep -- except in a
             * list, where the row IS the place and nothing was ever
             * placed. */
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        inside = (px + pw > lox && px < hix && py + ph > loy && py < hiy);
        if (v->band) {
            /* PVS-027: within a band each pose is toggled against what it
             * was when the band STARTED, so sweeping back over a pose puts
             * it back rather than leaving it stuck selected. */
            int was = v->band_was[p->index];

            set_selected(p, inside ? !was : was);
            if (inside) {
                hits++;
                if (v->pivot < 0 && !was) {
                    v->pivot = p->index;
                }
            }
        } else if (inside) {
            set_selected(p, 1);
            hits++;
            if (v->single_only) {
                break;
            }
            if (v->pivot < 0) {
                v->pivot = p->index;
            }
        }
    }
    return hits;
}

void
tiku_trk_poseview_set_drop_point(tiku_trk_poseview_t *v, int x, int y)
{
    if (v != NULL) {
        v->drop_x = x;
        v->drop_y = y;
        v->drop_valid = 1;
    }
}

void
tiku_trk_poseview_clear_drop_point(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->drop_valid = 0;
    }
}

void
tiku_trk_poseview_drop_land(tiku_trk_poseview_t *v, const char *name, int x,
                            int y)
{
    int i;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return;
    }
    for (i = 0; i < v->land_count; i++) {
        if (strcmp(v->land[i].name, name) == 0) {
            break;              /* a second drop of the same name wins */
        }
    }
    if (i == v->land_count) {
        if (v->land_count >= TIKU_TRK_LAND_MAX) {
            /* More names than the table holds: the rest are auto-placed,
             * which is what a view does for an entry it was told nothing
             * about.  Dropping the OLDEST instead would scatter the items
             * the user is watching. */
            return;
        }
        v->land_count++;
        snprintf(v->land[i].name, sizeof v->land[i].name, "%s", name);
    }
    v->land[i].x = x;
    v->land[i].y = y;
}

void
tiku_trk_poseview_clear_landings(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->land_count = 0;
    }
}

/**
 * @brief Take the landing reserved for @p path, if there is one.
 *
 * Consumed rather than read: a name that arrives twice is a new entry the
 * second time, and giving it the old drop point would put it under the
 * thing already there.
 */
static int
take_landing(tiku_trk_poseview_t *v, const char *path, int *x, int *y)
{
    const char *name;
    int i;

    if (v == NULL || v->land_count <= 0 || path == NULL) {
        return 0;
    }
    name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;
    for (i = 0; i < v->land_count; i++) {
        if (strcmp(v->land[i].name, name) == 0) {
            *x = v->land[i].x;
            *y = v->land[i].y;
            v->land[i] = v->land[v->land_count - 1];
            v->land_count--;
            return 1;
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* inline rename, armed on the slow second click (PVS-014, PVS-015, MA-043)  */
/*---------------------------------------------------------------------------*/

void
tiku_trk_poseview_allow_editing(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->allow_editing = on ? 1 : 0;
        if (!on) {
            v->edit_pose = -1;
            v->edit_clicked_at = 0;
        }
    }
}

int
tiku_trk_poseview_name_clicked(tiku_trk_poseview_t *v, int index, int64_t now)
{
    tiku_trk_pose_t *p;

    if (v == NULL || !v->allow_editing) {
        return 0;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL || !p->selected) {
        /* Clicking a name that was not already selected only selects it. */
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
        return 0;
    }
    if (v->edit_clicked_at != 0 && v->edit_pose == p->index) {
        /* The second of a pair: this is a double-click, which opens.  Arming
         * here as well would race the open. */
        v->edit_clicked_at = 0;
        v->edit_pose = -1;
        return 0;
    }
    /* The click that SELECTED the pose does not count -- otherwise selecting
     * anything and pausing would silently start renaming it. */
    if (now - p->sel_time < TIKU_TRK_CLICK_SPEED_US) {
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
        return 0;
    }
    v->edit_pose = p->index;
    v->edit_clicked_at = now;
    return 1;
}

void
tiku_trk_poseview_cancel_edit_wait(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
    }
}

int
tiku_trk_poseview_edit_armed(const tiku_trk_poseview_t *v)
{
    return (v != NULL && v->edit_clicked_at != 0) ? 1 : 0;
}

int
tiku_trk_poseview_edit_due(tiku_trk_poseview_t *v, int64_t now)
{
    int idx;

    if (v == NULL || v->edit_clicked_at == 0 || v->edit_pose < 0) {
        return -1;
    }
    /* Disarm when the conditions that armed it stopped holding: a pose that
     * has since been deselected, or has gone, must not start an editor. */
    if (v->edit_pose >= v->count || !v->poses[v->edit_pose].selected ||
        !v->allow_editing) {
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
        return -1;
    }
    if (now - v->edit_clicked_at <= TIKU_TRK_CLICK_SPEED_US) {
        return -1;                  /* still inside the click window       */
    }
    /* Handed back as a ROW of the list being shown, which is the space
     * every other call takes; the pose index it was held in is the one that
     * survives a filter change while the timer runs. */
    idx = display_of(v, v->edit_pose);
    v->edit_pose = -1;
    v->edit_clicked_at = 0;
    return idx;
}

/*---------------------------------------------------------------------------*/
/* spatial memory: positions ride on the objects                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Pose info as stored: two unscaled coordinates and a flag word.
 *
 * The flags carry the one bit a listing cannot derive -- whether the entry is
 * meant to be shown at all.  A record of any other length is REFUSED: reading
 * a longer one into this struct would silently take its first eight bytes and
 * call the result a position.
 */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t flags;
} pose_info_t;

/** @brief Stored, but not to be shown. */
#define POSE_INFO_INVISIBLE 0x1u

/** @brief The pre-flags record, still readable. */
typedef struct {
    int32_t x;
    int32_t y;
} pose_info_v1_t;

/**
 * @brief Where this view's OWN record lives (MA-064's three slots).
 *
 * Distinct ATTRIBUTE names on real nodes: the Desktop's arrangement sits
 * on the Desktop directory under its own name, and the root listing --
 * this port's Disks window -- keeps its state on the Desktop directory
 * too, exactly as the original redirects it, because the root itself is
 * nowhere the store can write.
 */
static const char *
state_node(const tiku_trk_poseview_t *v, char *buf, size_t max)
{
    if (v->state_key[0] == '\0' && strcmp(v->path, "/") == 0) {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(buf, max, "%s/Desktop", home);
            return buf;
        }
    }
    return v->path;
}

static const char *
state_attr(const tiku_trk_poseview_t *v)
{
    if (v->state_key[0] != '\0') {
        return TIKU_TRK_ATTR_DESK_VIEW_STATE;
    }
    if (strcmp(v->path, "/") == 0) {
        return TIKU_TRK_ATTR_DISKS_VIEW_STATE;
    }
    return TIKU_TRK_ATTR_VIEW_STATE;
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
    tiku_trk_store_t *store;
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
        char node[TIKU_TRK_PATH_MAX];
        int len = tiku_trk_state_read(store,
                                      state_node(v, node, sizeof node),
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
                    v->scroll = sane_scroll_origin(sc, 0);
                    v->icon_x = sane_scroll_origin(ix, 1);
                    v->icon_y = sane_scroll_origin(iy, 1);
                    v->mini_x = sane_scroll_origin(mx, 1);
                    v->mini_y = sane_scroll_origin(my, 1);
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
                    resort(v);
                }
            }
        }
    }
    for (i = 0; i < v->count; i++) {
        pose_info_t info;
        char raw[sizeof info + 4];
        int got;

        memset(&info, 0, sizeof info);
        got = tiku_trk_state_read(store, v->poses[i].model.path,
                                  TIKU_TRK_ATTR_POSE_INFO, raw, sizeof raw);
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
    apply_visibility(v);
    v->state_dirty = 0;
    return n;
}

int
tiku_trk_poseview_save_state(tiku_trk_poseview_t *v)
{
    tiku_trk_store_t *store;
    int i, n = 0;

    if (v == NULL || v->is_query || !v->state_dirty ||
        v->backend->ops->state_store == NULL) {
        return 0;               /* the dirty gate: no write, no wear */
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL || !tiku_trk_store_writable(store)) {
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
        char node[TIKU_TRK_PATH_MAX];

        (void)tiku_trk_state_write(store,
                                   state_node(v, node, sizeof node),
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
        if (tiku_trk_state_write(store, v->poses[i].model.path,
                                 TIKU_TRK_ATTR_POSE_INFO, &info,
                                 sizeof info) == 0) {
            n++;
        }
    }
    (void)tiku_trk_store_flush(store);
    v->state_dirty = 0;
    return n;
}

/*---------------------------------------------------------------------------*/
/* Change notices                                                            */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* The viewport and the extent                                               */
/*---------------------------------------------------------------------------*/

/** @brief Take one pose's rectangle into the running bounding box. */
static void
extent_add(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
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
static void
extent_clear(tiku_trk_poseview_t *v)
{
    v->ext_valid = 0;
    v->ext_x0 = v->ext_y0 = v->ext_x1 = v->ext_y1 = 0;
}

/** @brief Walk every pose again.  Only for what cannot grow a box: removal. */
static void
extent_recalc(tiku_trk_poseview_t *v)
{
    int i;

    extent_clear(v);
    for (i = 0; i < v->count; i++) {
        extent_add(v, &v->poses[i]);
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
    return (v != NULL) ? tiku_trk_park_count(&v->park) : 0;
}

int
tiku_trk_poseview_zombies(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->zombie_count : 0;
}

/** @brief Whether @p path's saved record says it is not to be shown. */
static int
pose_is_hidden(tiku_trk_poseview_t *v, const char *path)
{
    tiku_trk_store_t *store;
    char raw[sizeof(pose_info_t) + 4];
    pose_info_t info;

    if (v->backend == NULL || v->backend->ops->state_store == NULL) {
        return 0;
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL) {
        return 0;
    }
    if (tiku_trk_state_read(store, path, TIKU_TRK_ATTR_POSE_INFO, raw,
                            sizeof raw) != (int)sizeof info) {
        return 0;
    }
    memcpy(&info, raw, sizeof info);
    return (info.flags & POSE_INFO_INVISIBLE) ? 1 : 0;
}

/** @brief Index of the pose holding @p path, or -1. */
static int
pose_index(const tiku_trk_poseview_t *v, const char *path)
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
static int
sorted_slot(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
{
    int lo = 0, hi = v->count;

    g_sorting = v;
    while (lo < hi) {
        int mid = (lo + hi) / 2;

        if (pose_cmp(&v->poses[mid], p) <= 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    g_sorting = NULL;
    return lo;
}

/** @brief Put @p p at its sorted place, shifting the rest down. */
static int
insert_sorted(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
{
    int at, i;

    if (v->count >= POSES_MAX) {
        return -1;
    }
    /* The binary search is only right when the array is ordered, and a value
     * refresh under a value sort can have unsorted it since.  Restoring the
     * precondition here costs a re-sort only when it was actually broken. */
    if (!poses_sorted(v)) {
        resort(v);
    }
    at = sorted_slot(v, p);
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
static void
remove_pose(tiku_trk_poseview_t *v, int at)
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
            remove_pose(v, i);
            went++;
        }
    }
    if (went > 0 && v->filtering) {
        rebuild_filter(v);
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
    case TIKU_TRK_COL_SIZE:  return TIKU_TRK_EVF_SIZE;
    case TIKU_TRK_COL_DATE:  return TIKU_TRK_EVF_DATE;
    case TIKU_TRK_COL_KIND:  return TIKU_TRK_EVF_KIND;
    case TIKU_TRK_COL_VALUE: return TIKU_TRK_EVF_VALUE;
    case TIKU_TRK_COL_CAP:   return TIKU_TRK_EVF_CAP;
    case TIKU_TRK_COL_PERM:  return TIKU_TRK_EVF_PERM;
    default:
        /* The name changes only through MOVED; a named attribute column
         * reads the meta the VALUE bit stands for. */
        return (field >= TIKU_TRK_COL_ATTR_BASE) ? TIKU_TRK_EVF_VALUE : 0u;
    }
}

unsigned
tiku_trk_poseview_notify(tiku_trk_poseview_t *v, const tiku_trk_event_t *e)
{
    unsigned flags = TIKU_TRK_NOTIFY_NOTHING;
    int at;

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
        if (e->op == TIKU_TRK_EV_REMOVED) {
            (void)tiku_trk_nodeicon_deleted(v->icons, e->path);
        } else {
            (void)tiku_trk_nodeicon_changed(v->icons, e->path);
            if (e->op == TIKU_TRK_EV_MOVED && e->from[0] != '\0') {
                /* Keyed by path, so the name it USED to have is a key that
                 * now describes nothing. */
                (void)tiku_trk_nodeicon_changed(v->icons, e->from);
            }
        }
    }
    /* A notice about the folder ITSELF is not about its contents.  A rename
     * names the folder by its OLD path -- the new one is where it has just
     * gone -- so matching only e->path would miss exactly the case the
     * retitle rule is about. */
    if (strcmp(e->path, v->path) == 0 ||
        (e->op == TIKU_TRK_EV_MOVED && e->from[0] != '\0' &&
         strcmp(e->from, v->path) == 0)) {
        if (e->op == TIKU_TRK_EV_REMOVED) {
            /* The root is exempt: it has nowhere to go, and closing it would
             * leave nothing to browse. */
            return (strcmp(v->path, "/") == 0)
                       ? TIKU_TRK_NOTIFY_NOTHING : TIKU_TRK_NOTIFY_GONE;
        }
        if (e->op == TIKU_TRK_EV_MOVED) {
            snprintf(v->path, sizeof v->path, "%s", e->path);
            return TIKU_TRK_NOTIFY_RETITLE | TIKU_TRK_NOTIFY_REDRAW;
        }
        return TIKU_TRK_NOTIFY_NOTHING;
    }

    /* PVN-057: an ANCESTOR moving takes this folder with it.  Nothing about
     * the contents changed and no row moved -- but the window is showing a
     * path that no longer exists, so the title has to follow. */
    if (e->op == TIKU_TRK_EV_MOVED && e->from[0] != '\0') {
        size_t n = strlen(e->from);

        /* At a component boundary, or "/a/b" would claim "/a/bc" as its
         * own child and rewrite an unrelated window's path. */
        if (n > 0u && strncmp(e->from, v->path, n) == 0 &&
            v->path[n] == '/') {
            char rebuilt[TIKU_TRK_PATH_MAX];

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
    case TIKU_TRK_EV_CREATED:
        v->menu_generation++;
        /* A file made in some other folder is not this folder's business,
         * and a duplicate notice must not produce a second row. */
        if (!v->is_query && strcmp(e->dir, v->path) != 0) {
            return TIKU_TRK_NOTIFY_NOTHING;
        }
        if (pose_index(v, e->path) >= 0) {
            return TIKU_TRK_NOTIFY_NOTHING;
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
                        return TIKU_TRK_NOTIFY_NOTHING;
                    }
                }
            }
            for (k = 0; k < v->zombie_count; k++) {
                if (strcmp(v->zombie[k].path, e->path) == 0) {
                    return TIKU_TRK_NOTIFY_NOTHING;
                }
            }
        }
        {
            tiku_trk_pose_t p;
            tiku_trk_event_t held[8];
            int n, k;

            memset(&p, 0, sizeof p);
            p.model = e->model;
            (void)tiku_trk_desc_parse(e->model.facts.meta, &p.desc);
            {
                int lx, ly;

                if (v->mode != TIKU_TRK_MODE_LIST &&
                    take_landing(v, e->path, &lx, &ly)) {
                    /* Dropped here: the position was worked out at the
                     * drop, from the shape the selection had when it was
                     * picked up (PVS-045, PVL-027). */
                    float scale = mode_scale(v->mode, v->icon_size);

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
                float scale = mode_scale(v->mode, v->icon_size);

                p.loc_x = (int)((float)v->drop_x / scale);
                p.loc_y = (int)((float)v->drop_y / scale);
                p.placed = 1;
                p.auto_placed = 0;
                v->drop_valid = 0;
            }
            if (pose_is_hidden(v, e->path)) {
                /* Marked invisible in its saved record: it exists and it is
                 * deliberately not shown, so the notice is consumed rather
                 * than producing a row. */
                return TIKU_TRK_NOTIFY_NOTHING;
            }
            if (insert_sorted(v, &p) < 0) {
                return TIKU_TRK_NOTIFY_NOTHING;
            }
            /* PVL-090: the insertion set is shared with the loader.  A node
             * this notice brought in early must be recorded here, or the
             * loader reaching the same entry adds it a second time. */
            note_inserted(v, e->model.node_id);
            flags |= TIKU_TRK_NOTIFY_REDRAW;
            /* Anything that arrived for this node before it existed is
             * replayed now, in the order it came. */
            /* Drained in batches until empty: stopping at one bufferful
             * would strand the NEWEST notices, which are the ones that say
             * what the row looks like now. */
            do {
                n = tiku_trk_park_take(&v->park, e->path, held,
                                       (int)(sizeof held / sizeof held[0]));
                for (k = 0; k < n; k++) {
                    flags |= tiku_trk_poseview_notify(v, &held[k]);
                }
            } while (n > 0);
        }
        break;

    case TIKU_TRK_EV_REMOVED:
        v->menu_generation++;
        at = pose_index(v, e->path);
        if (at < 0) {
            /* It may only ever have been a zombie; that still goes. */
            int z;

            for (z = 0; z < v->zombie_count; z++) {
                if (strcmp(v->zombie[z].path, e->path) == 0) {
                    int m;

                    for (m = z; m < v->zombie_count - 1; m++) {
                        v->zombie[m] = v->zombie[m + 1];
                    }
                    v->zombie_count--;
                    flags |= TIKU_TRK_NOTIFY_REDRAW;
                    break;
                }
            }
            break;
        }
        remove_pose(v, at);
        flags |= TIKU_TRK_NOTIFY_REDRAW;
        break;

    case TIKU_TRK_EV_MOVED:
        v->menu_generation++;
        at = (e->from[0] != '\0') ? pose_index(v, e->from) : -1;
        if (at < 0) {
            at = pose_index(v, e->path);
        }
        if (at < 0) {
            /* Moved IN from elsewhere: that is a creation here. */
            tiku_trk_event_t as_new = *e;

            as_new.op = TIKU_TRK_EV_CREATED;
            return tiku_trk_poseview_notify(v, &as_new);
        }
        /* Moved OUT of this folder is a removal -- unless this is a query or
         * a virtual listing, whose rows have no single parent, so every move
         * is a rename in place. */
        if (!v->is_query && strcmp(e->dir, v->path) != 0) {
            /* Moved out of this folder: the same thing as a removal, so it
             * must leave by the same door.  Returning here skipped the tail
             * that rebuilds the filtered list, and a filtered view was left
             * reporting a row it no longer had. */
            remove_pose(v, at);
            flags |= TIKU_TRK_NOTIFY_REDRAW;
            break;
        }
        {
            /* A rename re-reads EVERY column, not just the name: the notice
             * says the entry changed identity, and other notices for it may
             * have been missed while it had none. */
            tiku_trk_pose_t moved = v->poses[at];
            int was_selected = moved.selected;
            int was_active = (v->active == at);
            int now_at;

            int scroll_was = v->scroll;

            moved.model = e->model;
            (void)tiku_trk_desc_parse(e->model.facts.meta, &moved.desc);
            moved.value_valid = 0;
            remove_pose(v, at);
            /* remove+insert is an implementation of RENAME, and a rename
             * changes nothing about what is visible -- the scroll correction
             * belongs to removal and must not leak in here. */
            v->scroll = scroll_was;
            /* Back to its sorted place, without re-sorting the folder. */
            now_at = insert_sorted(v, &moved);
            if (now_at >= 0) {
                v->poses[now_at].selected = was_selected;
                if (was_active) {
                    v->active = now_at;
                }
            }
            flags |= TIKU_TRK_NOTIFY_REDRAW;
            {
                /* A notice can be parked under the name the row has just
                 * taken -- something changed it while it was called
                 * something else -- so a rename into place replays as a
                 * creation does. */
                tiku_trk_event_t held[8];
                int n, k;

                do {
                    n = tiku_trk_park_take(&v->park, e->path, held,
                                           (int)(sizeof held /
                                                 sizeof held[0]));
                    for (k = 0; k < n; k++) {
                        flags |= tiku_trk_poseview_notify(v, &held[k]);
                    }
                } while (n > 0);
            }
        }
        break;

    case TIKU_TRK_EV_CHANGED:
    case TIKU_TRK_EV_ATTR:
        /* The shells rebuild Open With/Add-ons from the focused view on
         * every menu open; this serial makes a MIME/type notice observable
         * even when the row remains in place. */
        v->menu_generation++;
        at = pose_index(v, e->path);
        if (at < 0) {
            /* The row is not here yet.  Park it rather than lose it: a file
             * altered the instant after it was created would otherwise show
             * the state it was born with. */
            (void)tiku_trk_park_add(&v->park, e);
            break;
        }
        {
            /* Only the affected columns refresh (MA-072): a notice that
             * NAMES its fields is compared column by column -- the value
             * AND the fitted text must both have moved before a repaint
             * is owed -- while one that cannot say re-reads everything,
             * which is what every notice meant before the mask. */
            unsigned f = e->fields;
            int repaint = (f == 0u) || (f & TIKU_TRK_EVF_KIND) != 0u;
            tiku_trk_pose_t before = v->poses[at];

            v->poses[at].model = e->model;
            (void)tiku_trk_desc_parse(e->model.facts.meta,
                                      &v->poses[at].desc);
            if (f == 0u || (f & TIKU_TRK_EVF_VALUE) != 0u) {
                v->poses[at].value_valid = 0;
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
                    was_text = cell_text_at(v, &before, field, width,
                                            oldbuf, sizeof oldbuf);
                    now_text = cell_text_at(v, &v->poses[at], field, width,
                                            newbuf, sizeof newbuf);
                    if (strcmp(was_text, now_text) != 0) {
                        repaint = 1;
                        break;
                    }
                }
            }
            if (repaint) {
                flags |= TIKU_TRK_NOTIFY_REDRAW;
                /* One row changed, so one row is what needs painting
                 * again: the icon, the size and the date all live inside
                 * it (PVL-075). */
                tiku_trk_poseview_damage_row(v, at);
            }
            /* The re-slot check is owed only when the changed FIELD is a
             * sort key (PVL-047): re-checking on every notice churned the
             * listing, and a notice that cannot say stays conservative. */
            if (f != 0u &&
                (column_fields(v->sort_field) & f) == 0u &&
                (v->sort2_field < 0 ||
                 (column_fields(v->sort2_field) & f) == 0u)) {
                break;
            }
        }
        {
            /* The changed fact may be the one the listing is ORDERED by, and
             * a row that keeps its slot while its key moves leaves the
             * listing quietly unsorted (_CheckPoseSortOrder). */
            tiku_trk_pose_t moved = v->poses[at];
            int slot = sorted_slot(v, &moved);
            int scroll_was = v->scroll;

            if (slot != at && slot != at + 1) {
                int now_at;
                int was_active = (v->active == at);

                remove_pose(v, at);
                v->scroll = scroll_was;
                now_at = insert_sorted(v, &moved);
                if (now_at >= 0) {
                    v->poses[now_at].selected = moved.selected;
                    v->poses[now_at].sel_time = moved.sel_time;
                    if (was_active) {
                        v->active = now_at;
                    }
                }
            }
        }
        break;

    default:
        break;
    }
    if ((flags & TIKU_TRK_NOTIFY_REDRAW) != 0u && v->filtering) {
        rebuild_filter(v);
    }
    return flags;
}

unsigned
tiku_trk_poseview_apply_results(tiku_trk_poseview_t *v,
                                const tiku_trk_model_t *set, int n,
                                int64_t now)
{
    tiku_trk_model_t *was;
    tiku_trk_evlist_t evs;
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
    tiku_trk_evlist_clear(&evs);
    (void)tiku_trk_notify_diff(was, nwas, set, n, now, &evs);
    flags = tiku_trk_poseview_notify_all(v, &evs);
    free(was);
    return flags;
}

unsigned
tiku_trk_poseview_notify_all(tiku_trk_poseview_t *v,
                             const tiku_trk_evlist_t *l)
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

/** @brief Collects a re-listing for the differ. */
struct relist {
    tiku_trk_model_t    *set;
    int                  n;
    int                  max;
    tiku_trk_poseview_t *view;   /* for the zombie hold                   */
};

static int
relist_one(const tiku_trk_model_t *m, void *ctx)
{
    struct relist *r = ctx;

    if (r->n >= r->max) {
        return 1;
    }
    if (m->kind == TIKU_TRK_KIND_UNKNOWN) {
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
                       const tiku_trk_change_t *ch, int nch, int complete)
{
    tiku_trk_model_t *was, *is;
    struct relist r;
    tiku_trk_evlist_t evs;
    unsigned flags = TIKU_TRK_NOTIFY_NOTHING;
    int i, nwas;

    if (v == NULL || v->backend == NULL) {
        return flags;
    }
    /* The sweep runs before the query short-circuit: a parked notice ages on
     * the clock, not on what kind of view is holding it, and one parked in a
     * query view would otherwise sit for ever. */
    if (tiku_trk_park_expire(&v->park, now) > 0) {
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
     * answers -1 and we fall through to the re-listing, which is what a
     * local filesystem does.
     */
    if (nch >= 0) {
        if (nch == 0 && complete) {
            /* Nothing happened anywhere, and the device is sure of it: one
             * question served every open window. */
            return flags;
        }
        {
            int i, k, relist = !complete;

            for (i = 0; i < nch; i++) {
                int held = 0;

                for (k = 0; k < v->count; k++) {
                    if (v->poses[k].model.node_id != 0 &&
                        v->poses[k].model.node_id == ch[i].id) {
                        held = 1;
                        if (ch[i].op == TIKU_TRK_CH_CHANGED) {
                            /* Through the SAME handler everything else uses:
                             * an inline poke here was a second update path,
                             * and a second path is how a query view and a
                             * folder view drift apart.  The model is
                             * re-stat'ed so the descriptor and facts move
                             * with the value. */
                            tiku_trk_event_t ev;

                            memset(&ev, 0, sizeof ev);
                            ev.op = TIKU_TRK_EV_ATTR;
                            ev.at = now;
                            snprintf(ev.path, sizeof ev.path, "%s",
                                     v->poses[k].model.path);
                            snprintf(ev.dir, sizeof ev.dir, "%s", v->path);
                            if (v->backend->ops->stat == NULL ||
                                v->backend->ops->stat(v->backend, ev.path,
                                                      &ev.model) != 0) {
                                ev.model = v->poses[k].model;
                            }
                            flags |= tiku_trk_poseview_notify(v, &ev);
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
                    if (ch[i].op == TIKU_TRK_CH_CHANGED &&
                        v->backend->ops->stat != NULL) {
                        tiku_trk_model_t m;
                        char idpath[TIKU_TRK_PATH_MAX];

                        /* The record names an identity, not a path.  Only
                         * the backend can turn one into the other. */
                        if (tiku_trk_poseview_path_of_id(v, ch[i].id, idpath,
                                                         sizeof idpath) == 0 &&
                            v->backend->ops->stat(v->backend, idpath,
                                                  &m) == 0) {
                            tiku_trk_event_t ev;

                            memset(&ev, 0, sizeof ev);
                            ev.op = TIKU_TRK_EV_ATTR;
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
                                    flags |= tiku_trk_poseview_notify(v, &ev);
                                }
                            }
                        }
                    }
                    relist = 1;
                }
            }
            if (!relist) {
                return flags;
            }
        }
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
        tiku_trk_model_t self;

        if (v->backend->ops->stat != NULL &&
            v->backend->ops->stat(v->backend, v->path, &self) != 0 &&
            strcmp(v->path, "/") != 0) {
            flags |= TIKU_TRK_NOTIFY_GONE;
        }
        free(was);
        free(is);
        return flags;
    }
    tiku_trk_evlist_clear(&evs);
    (void)tiku_trk_notify_diff(was, nwas, is, r.n, now, &evs);
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

/*---------------------------------------------------------------------------*/
/* liveness + drawing support                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether the pose array is ordered under the current sort.
 *
 * The incremental insert is a binary search whose precondition is exactly
 * this; a value refresh under a value sort silently breaks it.
 */
static int
poses_sorted(tiku_trk_poseview_t *v)
{
    int i;

    g_sorting = v;
    for (i = 1; i < v->count; i++) {
        if (pose_cmp(&v->poses[i - 1], &v->poses[i]) > 0) {
            g_sorting = NULL;
            return 0;
        }
    }
    g_sorting = NULL;
    return 1;
}

/**
 * @brief Put pose @p at back in sorted order if its key has moved (MA-055).
 *
 * A rename already goes through remove+insert; an edited VALUE did not, so
 * under a value sort the row kept its old slot until some later insert
 * happened to notice the array was unsorted.
 *
 * @return Where the pose ended up.
 */
int
tiku_trk_poseview_recheck_order(tiku_trk_poseview_t *v, int at)
{
    tiku_trk_pose_t moved;
    int slot, now_at, was_active, scroll_was;

    if (v == NULL || at < 0 || at >= v->count) {
        return -1;
    }
    moved = v->poses[at];
    slot = sorted_slot(v, &moved);
    if (slot == at || slot == at + 1) {
        return at;                      /* already where it belongs        */
    }
    was_active = (v->active == at);
    scroll_was = v->scroll;
    remove_pose(v, at);
    v->scroll = scroll_was;             /* re-placing is not a removal     */
    now_at = insert_sorted(v, &moved);
    if (now_at >= 0) {
        v->poses[now_at].selected = moved.selected;
        v->poses[now_at].sel_time = moved.sel_time;
        if (was_active) {
            v->active = now_at;
        }
    }
    if (v->filtering) {
        /* filtered[] holds indices INTO the pose array, and remove+insert
         * has just renumbered it.  Left alone the filtered listing points
         * at whatever slid into those slots -- the notify path rebuilds for
         * exactly this reason and this path must too. */
        rebuild_filter(v);
    }
    return now_at;
}

int
tiku_trk_poseview_refresh_values(tiku_trk_poseview_t *v, int first, int count)
{
    int i, n, changed = 0;

    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    for (i = first; i < first + count && i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        char buf[256];
        int len;

        if (p == NULL || tiku_trk_model_is_container(&p->model)) {
            continue;
        }
        if (!(p->model.facts.perm & TIKU_TRK_P_READ)) {
            continue;
        }
        len = v->backend->ops->read(v->backend, p->model.path, buf,
                                    sizeof buf - 1);
        if (len < 0) {
            continue;
        }
        buf[len] = '\0';
        {
            char *nl = strchr(buf, '\n');
            if (nl != NULL) { *nl = '\0'; }
        }
        if (strcmp(p->value, buf) != 0) {
            snprintf(p->value, sizeof p->value, "%s", buf);
            p->value_valid = 1;
            changed++;
            /* The row whose reading moved, and no other: a value refresh
             * that redrew the listing would undo the saving the notice
             * path just made (PVL-075). */
            tiku_trk_poseview_damage_row(v, i);
            if (v->sort_field == TIKU_TRK_COL_VALUE ||
                v->sort2_field == TIKU_TRK_COL_VALUE) {
                /* MA-055: the key this listing is ordered by has moved, so
                 * the row moves with it now rather than at whatever later
                 * insert happens to notice the array is unsorted.  The
                 * walk carries on from the same index, which may re-read a
                 * row that shifted into it -- harmless, since every value
                 * is re-read on the next tick anyway. */
                (void)tiku_trk_poseview_recheck_order(v, p->index);
            }
            continue;
        }
        if (!p->value_valid) {
            /* The reading is the same one as before, but the row has been
             * showing a dash since the notice invalidated it: coming back
             * is a change to what the row says, and a row nobody reports
             * keeps the dash until something else repaints it. */
            p->value_valid = 1;
            changed++;
            tiku_trk_poseview_damage_row(v, i);
        }
    }
    return changed;
}

int
tiku_trk_poseview_pump(tiku_trk_poseview_t *v, int timeout_ms)
{
    if (v == NULL || v->backend->ops->pump == NULL) {
        return 0;
    }
    (void)v->backend->ops->pump(v->backend, timeout_ms);
    /* Values are re-read for the rows on screen; a folder of thousands does
     * not pay for what nobody is looking at. */
    return tiku_trk_poseview_refresh_values(v, v->scroll, 40);
}

/** @brief Title and default width for a field. */
static void
column_def_v(const tiku_trk_poseview_t *v, int field, const char **title,
             int *width)
{
    if (v != NULL && field >= TIKU_TRK_COL_ATTR_BASE) {
        int at = field - TIKU_TRK_COL_ATTR_BASE;

        if (at >= 0 && at < v->attrcol_count) {
            *title = v->attrcol[at].title;
            *width = v->attrcol[at].width;
            return;
        }
    }
    switch (field) {
    case TIKU_TRK_COL_KIND:  *title = "Kind";  *width = 140; break;
    case TIKU_TRK_COL_VALUE: *title = "Value"; *width = 190; break;
    case TIKU_TRK_COL_SIZE:  *title = "Size";  *width =  90; break;
    case TIKU_TRK_COL_CAP:   *title = "Cap";   *width =  60; break;
    case TIKU_TRK_COL_DATE:  *title = "Modified"; *width = 150; break;
    case TIKU_TRK_COL_PATH:  *title = "Location"; *width = 200; break;
    case TIKU_TRK_COL_PERM:  *title = "Permissions"; *width = 90; break;
    case TIKU_TRK_COL_ORIGINAL: *title = "Original path"; *width = 220; break;
    default:                 *title = "Name";  *width = 240; break;
    }
}

/**
 * @brief The sort key a view falls back to when its own column goes.
 *
 * The column that LEADS the strip, which is the one the reader is looking
 * at -- not whichever column happens to be called Name (MA-067).
 */
static int
sort_fallback(const tiku_trk_poseview_t *v)
{
    return (v->column_count > 0) ? v->columns[0].field : TIKU_TRK_COL_NAME;
}

/** @brief A size column is a number, and numbers read right-aligned. */
static int
column_alignment(int field)
{
    return (field == TIKU_TRK_COL_SIZE) ? TIKU_TRK_ALIGN_RIGHT
                                        : TIKU_TRK_ALIGN_LEFT;
}

int
tiku_trk_poseview_types_present(const tiku_trk_poseview_t *v,
                                const char **out, int max)
{
    int i, n = 0;

    if (v == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < v->count && n < max; i++) {
        const char *ty = v->poses[i].model.type;
        int k, seen = 0;

        if (ty[0] == '\0' || tiku_trk_model_is_container(&v->poses[i].model)) {
            /* A folder's type is the folder type, which says nothing about
             * what the listing HOLDS. */
            continue;
        }
        for (k = 0; k < n; k++) {
            if (strcmp(out[k], ty) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[n++] = ty;
        }
    }
    return n;
}

int
tiku_trk_poseview_has_column(const tiku_trk_poseview_t *v, int field)
{
    int i;

    for (i = 0; v != NULL && i < v->column_count; i++) {
        if (v->columns[i].field == field) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_poseview_copy_layout(const tiku_trk_poseview_t *v)
{
    int i;

    if (v == NULL || v->column_count <= 0) {
        return 0;
    }
    memset(&g_layout_clip, 0, sizeof g_layout_clip);
    for (i = 0; i < v->column_count && i < TIKU_TRK_COLUMNS_MAX; i++) {
        const tiku_trk_column_t *src = &v->columns[i];

        g_layout_clip.column[i].field = src->field;
        g_layout_clip.column[i].width = src->width;
        g_layout_clip.column[i].alignment = src->alignment;
        snprintf(g_layout_clip.column[i].title,
                 sizeof g_layout_clip.column[i].title, "%s",
                 src->title != NULL ? src->title : "");
        g_layout_clip.count++;
    }
    g_layout_clip.sort_field = v->sort_field;
    g_layout_clip.sort_secondary = v->sort2_field;
    g_layout_clip.reverse = v->sort_reverse;
    g_layout_clip.serial++;
    g_layout_clip_valid = 1;
    return g_layout_clip.count;
}

int
tiku_trk_poseview_paste_layout(tiku_trk_poseview_t *v)
{
    tiku_trk_column_t next[TIKU_TRK_COLUMNS_MAX];
    int i, n = 0, name = 0;

    if (v == NULL || !g_layout_clip_valid || g_layout_clip.count <= 0) {
        return 0;
    }
    memset(next, 0, sizeof next);
    for (i = 0; i < g_layout_clip.count && n < TIKU_TRK_COLUMNS_MAX; i++) {
        const tiku_trk_layout_column_t *src = &g_layout_clip.column[i];

        /* Name is mandatory; other built-ins are always known.  A custom
         * field is only accepted when this target registered it already. */
        if (src->field >= TIKU_TRK_COL_ATTR_BASE &&
            !tiku_trk_poseview_attr_of(v, src->field)) {
            continue;
        }
        if (src->field == TIKU_TRK_COL_NAME) {
            name = 1;
        }
        next[n].field = src->field;
        next[n].width = src->width < TIKU_TRK_COL_MIN_WIDTH
                            ? TIKU_TRK_COL_MIN_WIDTH : src->width;
        next[n].alignment = src->alignment;
        {
            const char *title = NULL;
            int ignored_width = next[n].width;

            column_def_v(v, src->field, &title, &ignored_width);
            /* Built-in titles are static and custom titles are owned by the
             * target view, so this pointer remains valid for the view. */
            next[n].title = title;
        }
        n++;
    }
    if (!name) {
        return 0;
    }
    memcpy(v->columns, next, (size_t)n * sizeof next[0]);
    v->column_count = n;
    tiku_trk_poseview_reflow_columns(v);
    v->sort_field = tiku_trk_poseview_has_column(v, g_layout_clip.sort_field)
                        ? g_layout_clip.sort_field : TIKU_TRK_COL_NAME;
    v->sort2_field = tiku_trk_poseview_has_column(v,
                                                   g_layout_clip.sort_secondary)
                         ? g_layout_clip.sort_secondary : -1;
    if (v->sort2_field == v->sort_field) {
        v->sort2_field = -1;
    }
    v->sort_reverse = g_layout_clip.reverse != 0;
    resort(v);
    if (v->filtering) {
        rebuild_filter(v);
    }
    v->state_dirty = 1;
    return n;
}

int
tiku_trk_poseview_has_layout_clipboard(void)
{
    return g_layout_clip_valid;
}

const tiku_trk_layout_clip_t *
tiku_trk_poseview_layout_clipboard(void)
{
    return g_layout_clip_valid ? &g_layout_clip : NULL;
}

int
tiku_trk_poseview_attr_column(tiku_trk_poseview_t *v, const char *attr,
                              const char *title, int width)
{
    int i;

    if (v == NULL || attr == NULL || attr[0] == '\0') {
        return -1;
    }
    for (i = 0; i < v->attrcol_count; i++) {
        if (strcmp(v->attrcol[i].name, attr) == 0) {
            return TIKU_TRK_COL_ATTR_BASE + i;   /* already has an id */
        }
    }
    if (v->attrcol_count >= TIKU_TRK_COL_ATTR_MAX) {
        return -1;
    }
    i = v->attrcol_count++;
    snprintf(v->attrcol[i].name, sizeof v->attrcol[i].name, "%s", attr);
    snprintf(v->attrcol[i].title, sizeof v->attrcol[i].title, "%s",
             (title != NULL && title[0] != '\0') ? title : attr);
    v->attrcol[i].width = (width > TIKU_TRK_COL_MIN_WIDTH)
                              ? width : TIKU_TRK_COL_MIN_WIDTH;
    return TIKU_TRK_COL_ATTR_BASE + i;
}

const char *
tiku_trk_poseview_attr_of(const tiku_trk_poseview_t *v, int field)
{
    int at = field - TIKU_TRK_COL_ATTR_BASE;

    if (v == NULL || field < TIKU_TRK_COL_ATTR_BASE ||
        at >= v->attrcol_count) {
        return NULL;
    }
    return v->attrcol[at].name;
}

int
tiku_trk_poseview_last_column(const tiku_trk_poseview_t *v, int field)
{
    /* The name column is the one that cannot go, so it is also the one
     * whose removal would leave nothing (TS-023). */
    (void)v;
    return (field == TIKU_TRK_COL_NAME) ? 1 : 0;
}

int
tiku_trk_poseview_toggle_column(tiku_trk_poseview_t *v, int field)
{
    int i;

    if (v == NULL || field == TIKU_TRK_COL_NAME) {
        return 0;              /* the name column is not optional          */
    }
    for (i = 0; i < v->column_count; i++) {
        if (v->columns[i].field != field) {
            continue;
        }
        for (; i < v->column_count - 1; i++) {
            v->columns[i] = v->columns[i + 1];
        }
        v->column_count--;
        tiku_trk_poseview_reflow_columns(v);
        /* Sorting on a column nobody can see is sorting by nothing the
         * user can check. */
        if (v->sort_field == field) {
            v->sort_field = sort_fallback(v);
            v->sort_reverse = 0;
            resort(v);
        }
        if (v->sort2_field == field) {
            v->sort2_field = -1;    /* TS-004: no column, no tie-break     */
        }
        /* PVL-081: the filter matches column TEXT, so a column that has
         * gone can only take rows away -- thin the list in place. */
        filter_changed(v, 1);
        v->columns_dirty = 1;
    v->state_dirty = 1;
        return 1;
    }
    if (v->column_count >= TIKU_TRK_COLUMNS_MAX) {
        return 0;
    }
    column_def_v(v, field, &v->columns[v->column_count].title,
                 &v->columns[v->column_count].width);
    v->columns[v->column_count].field = field;
    v->columns[v->column_count].alignment = column_alignment(field);
    v->column_count++;
    tiku_trk_poseview_reflow_columns(v);
    /* PVL-081: a new column can only bring rows BACK, and nothing short of
     * a rebuild can find a row that is not in the filtered list. */
    filter_changed(v, 0);
    v->state_dirty = 1;
    return 1;
}


/*---------------------------------------------------------------------------*/
/* the saved column layout (PVL-033, TS-030, TS-031, TS-032)                 */
/*---------------------------------------------------------------------------*/

void
tiku_trk_poseview_set_font_size(tiku_trk_poseview_t *v, int px)
{
    if (v != NULL && px > 0) {
        v->font_px = px;
    }
}

/**
 * @brief One column, as stored.
 *
 * The key is written in HOST order and is the only thing that says which
 * order the rest of the record is in.  Tracker gets that from the name it
 * stored the attribute under -- two names, one per endianness -- which needs
 * a namespace that can hold both; a stamp inside the record needs nothing,
 * and answers the same question.
 */
typedef struct {
    uint32_t key;
    int32_t  version;
    int32_t  field;
    int32_t  offset;          /* order only; the position is re-derived    */
    int32_t  width;
    int32_t  alignment;
    /* An attribute column is identified by its NAME: the field id is a
     * slot in this session's table, and a slot number means nothing to the
     * next one.  Empty for the fixed columns. */
    char     attr[TIKU_TRK_ATTR_NAME_MAX];
} column_rec_t;

#define COLUMN_REC_KEY     0x54434f4cu      /* "TCOL"                      */
#define COLUMN_REC_VERSION 2

static int column_index(const tiku_trk_poseview_t *v, int field);

static uint32_t
swap32u(uint32_t v)
{
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}

static int32_t
swap32(int32_t v)
{
    return (int32_t)swap32u((uint32_t)v);
}

/** @brief Round UP into device pixels; a column must not lose a pixel. */
static int
scale_in(const tiku_trk_poseview_t *v, int32_t stored)
{
    long n = (long)stored * (long)v->font_px;

    return (int)((n + TIKU_TRK_FONT_REF - 1) / TIKU_TRK_FONT_REF);
}

/** @brief Round DOWN into stored units, so a round trip cannot creep. */
static int32_t
scale_out(const tiku_trk_poseview_t *v, int px)
{
    return (int32_t)(((long)px * TIKU_TRK_FONT_REF) / (long)v->font_px);
}

int
tiku_trk_poseview_save_columns(tiku_trk_poseview_t *v)
{
    tiku_trk_store_t *store;
    column_rec_t rec[TIKU_TRK_COLUMNS_MAX];
    int i;

    if (v == NULL) {
        return 0;
    }
    if (!v->columns_dirty) {
        /* Nothing about the columns changed, so nothing is written: a
         * click that only re-sorted must not rewrite the layout record
         * (TS-036). */
        return 0;
    }
    store = (v->query_layout[0] != '\0' && v->layout_store != NULL)
          ? v->layout_store
          : ((v->backend->ops->state_store != NULL)
             ? v->backend->ops->state_store(v->backend) : NULL);
    if (store == NULL || !tiku_trk_store_writable(store)) {
        return 0;
    }
    for (i = 0; i < v->column_count; i++) {
        memset(&rec[i], 0, sizeof rec[i]);
        rec[i].key = COLUMN_REC_KEY;
        rec[i].version = COLUMN_REC_VERSION;
        rec[i].field = (int32_t)v->columns[i].field;
        rec[i].offset = scale_out(v, v->columns[i].offset);
        rec[i].width = scale_out(v, v->columns[i].width);
        rec[i].alignment = (int32_t)v->columns[i].alignment;
        {
            const char *a = tiku_trk_poseview_attr_of(v,
                                                      v->columns[i].field);

            if (a != NULL) {
                snprintf(rec[i].attr, sizeof rec[i].attr, "%s", a);
            }
        }
    }
    {
        char node[TIKU_TRK_PATH_MAX];
        const char *at = (v->query_layout[0] == '\0' &&
                          strcmp(v->path, "/") == 0)
                             ? TIKU_TRK_ATTR_DISKS_COLUMNS
                             : TIKU_TRK_ATTR_COLUMNS;
        const char *nd = (v->query_layout[0] != '\0')
                             ? v->query_layout
                             : state_node(v, node, sizeof node);

        if (tiku_trk_state_write(store, nd, at, rec,
                sizeof rec[0] * (size_t)v->column_count) != 0) {
            return 0;
        }
    }
    v->columns_dirty = 0;
    return v->column_count;
}

/** @brief Order restored columns by their saved offset (CompareColumns). */
static int
rec_cmp(const void *a, const void *b)
{
    const column_rec_t *x = a, *y = b;

    return (x->offset < y->offset) ? -1 : ((x->offset > y->offset) ? 1 : 0);
}

void
tiku_trk_poseview_copy_columns(tiku_trk_poseview_t *v,
                               const tiku_trk_poseview_t *from)
{
    int i;

    if (v == NULL || from == NULL || from->column_count == 0) {
        return;
    }
    v->column_count = from->column_count;
    for (i = 0; i < from->column_count; i++) {
        v->columns[i] = from->columns[i];
    }
    tiku_trk_poseview_reflow_columns(v);
}

int
tiku_trk_poseview_restore_columns(tiku_trk_poseview_t *v)
{
    tiku_trk_store_t *store;
    column_rec_t rec[TIKU_TRK_COLUMNS_MAX];
    int n, kept = 0, i, j, order = -1;
    size_t got = 0;

    if (v == NULL) {
        return 0;
    }
    store = (v->query_layout[0] != '\0' && v->layout_store != NULL)
          ? v->layout_store
          : ((v->backend->ops->state_store != NULL)
             ? v->backend->ops->state_store(v->backend) : NULL);
    if (store == NULL) {
        return 0;
    }
    {
        char node[TIKU_TRK_PATH_MAX];
        const char *at = (v->query_layout[0] == '\0' &&
                          strcmp(v->path, "/") == 0)
                             ? TIKU_TRK_ATTR_DISKS_COLUMNS
                             : TIKU_TRK_ATTR_COLUMNS;
        const char *nd = (v->query_layout[0] != '\0')
                             ? v->query_layout
                             : state_node(v, node, sizeof node);
        int r = tiku_trk_state_read(store, nd, at, rec, sizeof rec);
        if (r <= 0) {
            return 0;
        }
        got = (size_t)r;
    }
    /* A record that is not a whole number of columns is not a layout: it is
     * a truncated write or something else's bytes under our name. */
    if (got % sizeof rec[0] != 0u) {
        return 0;
    }
    n = (int)(got / sizeof rec[0]);
    if (n > TIKU_TRK_COLUMNS_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        int swapped = 0;

        if (rec[i].key == swap32u(COLUMN_REC_KEY)) {
            swapped = 1;
            /* Written by a host of the other byte order.  Every scalar in
             * the record is turned, including the key that said so. */
            rec[i].key = swap32u(rec[i].key);
            rec[i].version = swap32(rec[i].version);
            rec[i].field = swap32(rec[i].field);
            rec[i].offset = swap32(rec[i].offset);
            rec[i].width = swap32(rec[i].width);
            rec[i].alignment = swap32(rec[i].alignment);
        }
        if (rec[i].attr[0] != '\0') {
            /* Registered again under this session's numbering, so a layout
             * saved with three attribute columns comes back with the same
             * three whatever order they are registered in now. */
            const char *ttl = NULL;
            int w = 0;

            column_def_v(v, rec[i].field, &ttl, &w);
            rec[i].field = (int32_t)tiku_trk_poseview_attr_column(v,
                               rec[i].attr, rec[i].attr,
                               (int)rec[i].width);
        }
        if (order < 0) {
            order = swapped;
        } else if (order != swapped) {
            /* Records disagreeing about their own byte order are not one
             * layout written by one host; nothing legitimate produces that. */
            return 0;
        }
        if (rec[i].key != COLUMN_REC_KEY ||
            rec[i].version != COLUMN_REC_VERSION) {
            return 0;               /* not ours, or not this format        */
        }
        /* TS-032: a value out of range is REJECTED, not clamped.  Clamping
         * a corrupt width to something legal produces a layout the user
         * never chose and cannot tell from one they did.
         *
         * Checked on the SCALED value, because that is the number that
         * becomes a column: bounding the stored one instead lets the
         * ceiling grow with the font size, so the same record is legal at
         * one font and absurd at another. */
        if (scale_in(v, rec[i].width) < 0 ||
            scale_in(v, rec[i].width) > 10000 ||
            scale_in(v, rec[i].offset) < 0 ||
            scale_in(v, rec[i].offset) > 10000 ||
            rec[i].alignment < TIKU_TRK_ALIGN_LEFT ||
            rec[i].alignment > TIKU_TRK_ALIGN_CENTER ||
            rec[i].field < 0 ||
            (rec[i].field > TIKU_TRK_COL_ORIGINAL &&
             (rec[i].field < TIKU_TRK_COL_ATTR_BASE ||
              rec[i].field >= TIKU_TRK_COL_ATTR_BASE +
                              TIKU_TRK_COL_ATTR_MAX))) {
            /* A field outside the fixed set is legal only when it is an
             * attribute column's slot -- and one of THIS session's, since
             * the name it carries has already been registered above. */
            return 0;
        }
        for (j = 0; j < i; j++) {
            /* Two records for one field would produce a listing with the
             * same column twice, and the name column cannot be removed --
             * so a layout like that could never be undone. */
            if (rec[j].field == rec[i].field) {
                return 0;
            }
        }
        if (rec[i].field == TIKU_TRK_COL_NAME) {
            kept = 1;               /* the name column must be in there    */
        }
    }
    if (!kept) {
        return 0;
    }
    /* PVL-033: the saved offsets are used ONCE, to sort, and then thrown
     * away; positions are re-derived.  Keeping them would let a layout
     * saved at another font size place a column where nothing draws. */
    qsort(rec, (size_t)n, sizeof rec[0], rec_cmp);
    for (i = 0; i < n; i++) {
        const char *title;
        int def_w;

        column_def_v(v, (int)rec[i].field, &title, &def_w);
        v->columns[i].title = title;
        v->columns[i].field = (int)rec[i].field;
        v->columns[i].width = scale_in(v, rec[i].width);
        if (v->columns[i].width < TIKU_TRK_COL_MIN_WIDTH) {
            v->columns[i].width = TIKU_TRK_COL_MIN_WIDTH;
        }
        v->columns[i].alignment = (int)rec[i].alignment;
    }
    v->column_count = n;
    tiku_trk_poseview_reflow_columns(v);
    /* A sort key whose column did not come back sorts by nothing visible.
     * It falls to the column that LEADS the strip, which is the one the
     * reader is looking at, rather than to whichever column happens to be
     * called Name (MA-067). */
    if (column_index(v, v->sort_field) < 0) {
        v->sort_field = sort_fallback(v);
        v->sort_reverse = 0;
    }
    if (v->sort2_field >= 0 && column_index(v, v->sort2_field) < 0) {
        v->sort2_field = -1;
    }
    if (v->sort2_field == v->sort_field) {
        /* The repair above can collapse the two onto one column; a listing
         * whose tie-break is its own key has no tie-break. */
        v->sort2_field = -1;
    }
    resort(v);
    return n;
}

int
tiku_trk_poseview_install_query_defaults(tiku_trk_backend_t *backend,
                                          tiku_trk_store_t *store)
{
    static const char *const kType[] = {
        "", "application/x-vnd.Be-bookmark", "application/x-person",
        "text/x-email"
    };
    tiku_trk_frame_t def = { 40, 40, 695, 350 };
    int i, installed = 0;

    if (backend == NULL || store == NULL || !tiku_trk_store_writable(store)) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof kType / sizeof kType[0]); i++) {
        tiku_trk_poseview_t *v = tiku_trk_poseview_new(backend,
                                                       "/query-template");
        tiku_trk_frame_t frame;
        unsigned char byte;
        int has_columns, has_frame;

        if (v == NULL) {
            continue;
        }
        tiku_trk_poseview_set_layout_store(v, store);
        tiku_trk_poseview_set_query_type(v, kType[i]);
        has_columns = tiku_trk_state_read(store,
                            tiku_trk_poseview_layout_path(v),
                            TIKU_TRK_ATTR_COLUMNS, &byte, sizeof byte) > 0;
        has_frame = tiku_trk_state_read(store,
                          tiku_trk_poseview_layout_path(v),
                          TIKU_TRK_ATTR_FRAME, &frame, sizeof frame) ==
                    (int)sizeof frame &&
                    frame.w >= TIKU_TRK_FRAME_MIN_W &&
                    frame.h >= TIKU_TRK_FRAME_MIN_H;
        if (!has_columns) {
            (void)tiku_trk_poseview_set_results(v, NULL, 0);
            /* The install IS the change being recorded: the gate that
             * spares a sort-only release must not spare it (TS-036). */
            v->columns_dirty = 1;
            (void)tiku_trk_poseview_save_columns(v);
        }
        if (!has_frame) {
            (void)tiku_trk_state_write(store,
                                       tiku_trk_poseview_layout_path(v),
                                       TIKU_TRK_ATTR_FRAME, &def,
                                       sizeof def);
        }
        if (!has_columns || !has_frame) {
            installed++;
        }
        tiku_trk_poseview_free(v);
    }
    return installed;
}

int
tiku_trk_poseview_start_offset(const tiku_trk_poseview_t *v)
{
    /* The row's icon owns the strip to the left of the first column, so the
     * first column starts past it: icon, the mini-icon separator, and one
     * pixel of air. */
    (void)v;
    return TIKU_TRK_MINI_ICON + TIKU_TRK_PLACE_GAP + 1;
}

void
tiku_trk_poseview_reflow_columns(tiku_trk_poseview_t *v)
{
    int i, at;

    if (v == NULL) {
        return;
    }
    at = tiku_trk_poseview_start_offset(v);
    for (i = 0; i < v->column_count; i++) {
        v->columns[i].offset = at;
        at += v->columns[i].width + TIKU_TRK_COL_MARGIN;
    }
}

/** @brief Index of the column showing @p field, or -1. */
static int
column_index(const tiku_trk_poseview_t *v, int field)
{
    int i;

    for (i = 0; i < v->column_count; i++) {
        if (v->columns[i].field == field) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_poseview_resize_column(tiku_trk_poseview_t *v, int field, int width)
{
    int i;

    if (v == NULL) {
        return 0;
    }
    i = column_index(v, field);
    if (i < 0) {
        return 0;
    }
    if (width < TIKU_TRK_COL_MIN_WIDTH) {
        width = TIKU_TRK_COL_MIN_WIDTH;
    }
    if (v->columns[i].width == width) {
        return 0;
    }
    v->columns[i].width = width;
    tiku_trk_poseview_reflow_columns(v);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_move_column_to(tiku_trk_poseview_t *v, int field, int to)
{
    int from;
    tiku_trk_column_t moved;

    if (v == NULL) {
        return 0;
    }
    from = column_index(v, field);
    if (to >= v->column_count) {
        to = v->column_count - 1;
    }
    if (from < 0 || to < 0 || from == to) {
        return 0;
    }
    moved = v->columns[from];
    if (from < to) {
        int k;

        for (k = from; k < to; k++) {
            v->columns[k] = v->columns[k + 1];
        }
    } else {
        int k;

        for (k = from; k > to; k--) {
            v->columns[k] = v->columns[k - 1];
        }
    }
    v->columns[to] = moved;
    tiku_trk_poseview_reflow_columns(v);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_column_after(tiku_trk_poseview_t *v, int field, int after)
{
    int at;

    if (v == NULL) {
        return 0;
    }
    at = column_index(v, after);
    if (at < 0) {
        return 0;
    }
    /* One PAST the target, which is what "insert after" means and what a
     * column dragged back into the strip has to land on. */
    return tiku_trk_poseview_move_column_to(v, field, at + 1);
}

int
tiku_trk_poseview_move_column(tiku_trk_poseview_t *v, int field, int before)
{
    int from, to;
    tiku_trk_column_t moved;

    if (v == NULL) {
        return 0;
    }
    from = column_index(v, field);
    to = column_index(v, before);
    /* The destination index is read BEFORE the source is lifted out: taken
     * afterwards, a rightward move would land one short of where the user
     * dropped it and the column would never pass its neighbour. */
    if (from < 0 || to < 0 || from == to) {
        return 0;
    }
    moved = v->columns[from];
    if (from < to) {
        int k;

        for (k = from; k < to; k++) {
            v->columns[k] = v->columns[k + 1];
        }
    } else {
        int k;

        for (k = from; k > to; k--) {
            v->columns[k] = v->columns[k - 1];
        }
    }
    v->columns[to] = moved;
    tiku_trk_poseview_reflow_columns(v);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_column_widest(tiku_trk_poseview_t *v, int field)
{
    int i, n, best = TIKU_TRK_COL_MIN_WIDTH;

    if (v == NULL || v->measure == NULL) {
        return 0;
    }
    /* Measured over the list being SHOWN: fitting a filtered listing to a
     * row that is filtered out would widen the column for nothing. */
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        char buf[160];
        const char *text;
        int w;

        if (p == NULL) {
            continue;
        }
        /* Measured at an effectively unbounded width, as PreferredWidth is:
         * asking the cell how wide it wants to be at its CURRENT width can
         * only ever ratify the width it already has. */
        text = cell_text_at(v, p, field, 100000, buf, sizeof buf);
        if (text == NULL || text[0] == '\0') {
            continue;
        }
        w = v->measure(text, v->measure_ctx);
        if (w > best) {
            best = w;
        }
    }
    return best;
}

int
tiku_trk_poseview_fit_column(tiku_trk_poseview_t *v, int field)
{
    int want, i;

    if (v == NULL) {
        return 0;
    }
    i = column_index(v, field);
    if (i < 0) {
        return 0;
    }
    want = tiku_trk_poseview_column_widest(v, field);
    if (want <= 0) {
        return 0;
    }
    /* Nothing to fit: the widest value is already at the floor and the
     * column is no wider than that.  Saying so is what lets the caller fall
     * through to a drag instead of swallowing the click. */
    if (!(want > TIKU_TRK_COL_MIN_WIDTH || want < v->columns[i].width)) {
        return 0;
    }
    /* Note the answer is about the GESTURE, not about the pixels: fitting a
     * column that is already the right width still consumes the
     * double-click, or the second one would arm a resize on the column the
     * user has just finished fitting. */
    (void)tiku_trk_poseview_resize_column(v, field, want);
    return 1;
}

int
tiku_trk_poseview_hscroll(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->hscroll : 0;
}

void
tiku_trk_poseview_set_hscroll(tiku_trk_poseview_t *v, int px)
{
    int max;

    if (v == NULL) {
        return;
    }
    max = tiku_trk_poseview_columns_width(v);
    if (v->view_w > 0) {
        /* The clamp is against what CANNOT be shown, not against the
         * content's whole width: scrolling ends where the last column
         * meets the window's right edge (PVL-058). */
        max -= v->view_w;
        if (max < 0) {
            max = 0;
        }
    }
    if (px > max) { px = max; }
    if (px < 0)   { px = 0; }
    v->hscroll = px;
}

int
tiku_trk_poseview_columns_width(const tiku_trk_poseview_t *v)
{
    int i, w;

    if (v == NULL || v->column_count == 0) {
        return 0;
    }
    w = tiku_trk_poseview_start_offset(v);
    for (i = 0; i < v->column_count; i++) {
        w += v->columns[i].width + TIKU_TRK_COL_MARGIN;
    }
    return w;
}

int
tiku_trk_poseview_columns(const tiku_trk_poseview_t *v,
                          const tiku_trk_column_t **out)
{
    if (v == NULL) {
        return 0;
    }
    if (out != NULL) {
        /* Asking only HOW MANY is a fair question, and answering it with a
         * write through a null pointer is not an answer. */
        *out = v->columns;
    }
    return v->column_count;
}

int
tiku_trk_poseview_scroll(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->scroll : 0;
}

void
tiku_trk_poseview_scroll_to(tiku_trk_poseview_t *v, int row)
{
    int n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    if (row < 0) { row = 0; }
    if (row > n - 1) { row = (n > 0) ? n - 1 : 0; }
    v->scroll = row;
}

void
tiku_trk_poseview_scroll_into_view(tiku_trk_poseview_t *v, int index,
                                   int visible_rows)
{
    if (v == NULL || visible_rows <= 0 || v->no_autoscroll) {
        return;
    }
    if (index < v->scroll) {
        v->scroll = index;
    } else if (index >= v->scroll + visible_rows) {
        v->scroll = index - visible_rows + 1;
    }
    if (v->scroll < 0) {
        v->scroll = 0;
    }
}
