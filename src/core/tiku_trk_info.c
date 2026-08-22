/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_info.c - what the Get Info window knows and how it stays true.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_info.h"
#include "tiku_strings.h"
#include "tiku_attr.h"
#include "tiku_thumb.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
tiku_trk_info_open(tiku_trk_info_t *in, tiku_backend_t *b,
                   const char *path)
{
    if (in == NULL || b == NULL || b->ops == NULL || b->ops->stat == NULL ||
        path == NULL) {
        return -1;
    }
    memset(in, 0, sizeof *in);
    if (b->ops->stat(b, path, &in->model) != 0) {
        return -1;
    }
    snprintf(in->path, sizeof in->path, "%s", path);
    in->used = 1;
    in->perm = in->model.facts.perm;
    /* Editable only where the store can be told.  A backend with no way to
     * set permissions shows the grid as it is rather than offering an edit
     * that fails after the fact (AW-078). */
    in->perm_editable = (b->ops->setperm != NULL);
    if (tiku_model_is_container(&in->model)) {
        tiku_trk_info_calc_start(in);
    }
    return 0;
}

void
tiku_trk_info_title(const tiku_trk_info_t *in, char *out, size_t max)
{
    if (out == NULL || max == 0u) {
        return;
    }
    if (in == NULL || !in->used) {
        out[0] = '\0';
        return;
    }
    snprintf(out, max, "%s info", tiku_model_display_name(&in->model));
}

const char *
tiku_trk_info_tab_name(tiku_trk_info_tab_t tab)
{
    switch (tab) {
    case TIKU_TRK_INFO_TAB_PERMISSIONS: return tiku_str("info.permissions");
    case TIKU_TRK_INFO_TAB_ATTRIBUTES:  return tiku_str("menu.attributes");
    default:                            return tiku_str("info.information");
    }
}

/** @brief Measure for the formatters: characters, not pixels. */
static int
chars(const char *s, void *ctx)
{
    (void)ctx;
    return (s != NULL) ? (int)strlen(s) : 0;
}

/** @brief The rows this entry has, in order. */
static int
field_list(const tiku_trk_info_t *in, tiku_store_t *store,
           tiku_trk_info_field_t *out, int max)
{
    int n = 0;
    uint32_t dim[2];

    if (n < max) {
        out[n++] = tiku_model_is_container(&in->model) &&
                   (in->model.kind == TIKU_KIND_VOLUME ||
                    in->model.kind == TIKU_KIND_ROOT)
                       ? TIKU_TRK_INFO_F_CAPACITY : TIKU_TRK_INFO_F_SIZE;
    }
    if (n < max) { out[n++] = TIKU_TRK_INFO_F_CREATED; }
    if (n < max) { out[n++] = TIKU_TRK_INFO_F_MODIFIED; }
    if (n < max) { out[n++] = TIKU_TRK_INFO_F_KIND; }
    if (n < max) { out[n++] = TIKU_TRK_INFO_F_LOCATION; }
    if (in->model.kind == TIKU_KIND_SYMLINK && n < max) {
        out[n++] = TIKU_TRK_INFO_F_LINK;
    }
    if (in->opens_with[0] != '\0' && n < max &&
        !tiku_model_is_container(&in->model) &&
        (in->model.facts.perm & TIKU_P_EXEC) == 0u) {
        /* Not for executables, directories or volumes: an application
         * does not open THROUGH another application (AW-071). */
        out[n++] = TIKU_TRK_INFO_F_OPENS_WITH;
    }
    if ((in->model.kind == TIKU_KIND_VOLUME ||
         in->model.kind == TIKU_KIND_ROOT) && n < max) {
        out[n++] = TIKU_TRK_INFO_F_FILESYSTEM;
    }
    /* Only when the picture has actually been measured.  A row that said
     * "unknown" for every image until one was made would be worse than no
     * row at all (IV-035). */
    if (store != NULL && n < max &&
        tiku_state_read(store, in->path, TIKU_ATTR_THUMB_DIM, dim,
                            sizeof dim) == (int)sizeof dim) {
        out[n++] = TIKU_TRK_INFO_F_DIMENSIONS;
    }
    return n;
}

tiku_trk_info_field_t
tiku_trk_info_field(const tiku_trk_info_t *in, tiku_store_t *store,
                    int i, const char **label, char *value, size_t max)
{
    tiku_trk_info_field_t list[TIKU_TRK_INFO_FIELDS];
    tiku_trk_info_field_t f;
    int n;

    if (value != NULL && max > 0u) {
        value[0] = '\0';
    }
    if (label != NULL) {
        *label = "";
    }
    if (in == NULL || !in->used) {
        return TIKU_TRK_INFO_F_NONE;
    }
    n = field_list(in, store, list, TIKU_TRK_INFO_FIELDS);
    if (i < 0 || i >= n) {
        return TIKU_TRK_INFO_F_NONE;
    }
    f = list[i];
    switch (f) {
    case TIKU_TRK_INFO_F_SIZE:
        if (label != NULL) { *label = tiku_str("label.size"); }
        if (tiku_model_is_container(&in->model)) {
            if (in->calc == TIKU_TRK_INFO_CALC_RUNNING) {
                /* Not "0 bytes" and not blank: the window says what it is
                 * doing, because the answer takes as long as the tree is
                 * deep (AW-068). */
                snprintf(value, max, "calculating" "\xe2\x80\xa6");
            } else if (in->calc == TIKU_TRK_INFO_CALC_DONE) {
                char sz[32];

                (void)tiku_attr_size_fit(in->calc_bytes, 32, chars, NULL,
                                             sz, sizeof sz);
                snprintf(value, max, "%s for %d file%s in %d folder%s%s", sz,
                         in->calc_files, (in->calc_files == 1) ? "" : "s",
                         in->calc_dirs, (in->calc_dirs == 1) ? "" : "s",
                         in->calc_overflow ? " (deeper than counted)" : "");
            } else {
                snprintf(value, max, "-");
            }
        } else {
            (void)tiku_attr_size_fit(in->model.facts.size, 32, chars,
                                         NULL, value, max);
        }
        break;
    case TIKU_TRK_INFO_F_CAPACITY: {
        char total[32], avail[32];

        if (label != NULL) { *label = tiku_str("info.capacity"); }
        (void)tiku_attr_size_fit((int64_t)in->model.facts.total, 32,
                                     chars, NULL, total, sizeof total);
        (void)tiku_attr_size_fit((int64_t)in->model.facts.avail, 32,
                                     chars, NULL, avail, sizeof avail);
        snprintf(value, max, "%s (%s free)", total, avail);
        break;
    }
    case TIKU_TRK_INFO_F_CREATED:
    case TIKU_TRK_INFO_F_MODIFIED:
        if (label != NULL) {
            *label = (f == TIKU_TRK_INFO_F_CREATED) ? tiku_str("info.created")
                                           : tiku_str("label.modified");
        }
        /* Through the columns' own formatter, so a date reads the same
         * here as it does in the list (MA-041). */
        (void)tiku_attr_time_fit(in->model.facts.mtime, 40, chars, NULL,
                                     value, max);
        break;
    case TIKU_TRK_INFO_F_KIND:
        if (label != NULL) { *label = tiku_str("label.kind"); }
        snprintf(value, max, "%s", tiku_model_kind_string(&in->model));
        break;
    case TIKU_TRK_INFO_F_OPENS_WITH:
        if (label != NULL) { *label = tiku_str("info.opens_with"); }
        snprintf(value, max, "%s", in->opens_with);
        break;
    case TIKU_TRK_INFO_F_LOCATION: {
        char dir[TIKU_PATH_MAX];

        if (label != NULL) { *label = tiku_str("label.location"); }
        if (tiku_trk_info_location(in, dir, sizeof dir, NULL, 0)) {
            snprintf(value, max, "%s", dir);
        }
        break;
    }
    case TIKU_TRK_INFO_F_LINK:
        if (label != NULL) { *label = tiku_str("info.link_to"); }
        {
            char tgt[TIKU_PATH_MAX];
            ssize_t got = readlink(in->path, tgt, sizeof tgt - 1u);

            if (got > 0) {
                tgt[got] = '\0';
                snprintf(value, max, "%s%s", tgt,
                         in->model.facts.link_broken ? " (broken)" : "");
            } else {
                snprintf(value, max, "-");
            }
        }
        break;
    case TIKU_TRK_INFO_F_DIMENSIONS: {
        uint32_t dim[2] = { 0u, 0u };

        if (label != NULL) { *label = tiku_str("info.dimensions"); }
        (void)tiku_state_read(store, in->path, TIKU_ATTR_THUMB_DIM,
                                  dim, sizeof dim);
        snprintf(value, max, "%u x %u", (unsigned)dim[0], (unsigned)dim[1]);
        break;
    }
    case TIKU_TRK_INFO_F_FILESYSTEM:
        if (label != NULL) { *label = tiku_str("info.filesystem"); }
        snprintf(value, max, "%s",
                 (in->model.backend != NULL &&
                  in->model.backend->ops != NULL)
                     ? in->model.backend->ops->name : "-");
        break;
    default:
        break;
    }
    return f;
}

/*---------------------------------------------------------------------------*/
/* The size walk                                                             */
/*---------------------------------------------------------------------------*/

void
tiku_trk_info_calc_start(tiku_trk_info_t *in)
{
    if (in == NULL || !tiku_model_is_container(&in->model)) {
        return;
    }
    in->nqueue = 0;
    in->calc_bytes = 0;
    in->calc_files = 0;
    in->calc_dirs = 0;
    in->calc_overflow = 0;
    snprintf(in->queue[in->nqueue++], TIKU_PATH_MAX, "%s", in->path);
    in->calc = TIKU_TRK_INFO_CALC_RUNNING;
}

void
tiku_trk_info_calc_stop(tiku_trk_info_t *in)
{
    if (in == NULL) {
        return;
    }
    /* Back to idle rather than to done: a half-counted total presented as
     * the answer would be a smaller number than the truth, stated with the
     * same confidence (AW-068). */
    in->calc = TIKU_TRK_INFO_CALC_IDLE;
    in->nqueue = 0;
}

/** @brief What one directory's listing adds to the running total. */
static int
calc_entry(const tiku_model_t *m, void *ctx)
{
    tiku_trk_info_t *in = ctx;

    if (tiku_model_is_container(m)) {
        in->calc_dirs++;
        if (in->nqueue < TIKU_TRK_INFO_CALC_QUEUE) {
            snprintf(in->queue[in->nqueue++], TIKU_PATH_MAX, "%s",
                     m->path);
        } else {
            /* Said out loud rather than silently under-counted: a total
             * that quietly stopped descending is a wrong number wearing a
             * right number's face. */
            in->calc_overflow = 1;
        }
    } else {
        in->calc_files++;
        if (m->facts.size > 0) {
            in->calc_bytes += m->facts.size;
        }
    }
    return 0;
}

int
tiku_trk_info_calc_step(tiku_trk_info_t *in, tiku_backend_t *b)
{
    char dir[TIKU_PATH_MAX];

    if (in == NULL || b == NULL || b->ops == NULL || b->ops->list == NULL ||
        in->calc != TIKU_TRK_INFO_CALC_RUNNING) {
        return 0;
    }
    if (in->nqueue <= 0) {
        in->calc = TIKU_TRK_INFO_CALC_DONE;
        return 0;
    }
    snprintf(dir, sizeof dir, "%s", in->queue[--in->nqueue]);
    (void)b->ops->list(b, dir, calc_entry, in);
    if (in->nqueue <= 0) {
        in->calc = TIKU_TRK_INFO_CALC_DONE;
        return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* Liveness                                                                  */
/*---------------------------------------------------------------------------*/

int
tiku_trk_info_refresh(tiku_trk_info_t *in, tiku_backend_t *b, int64_t now)
{
    tiku_model_t fresh;

    if (in == NULL || !in->used || b == NULL || b->ops == NULL ||
        b->ops->stat == NULL) {
        return 0;
    }
    /* Only what changes without anyone saying so.  A folder's contents
     * arrive as notices; free space is nobody's node, and a file being
     * written is not reported until it is closed (AW-069). */
    if (in->model.kind != TIKU_KIND_VOLUME &&
        in->model.kind != TIKU_KIND_ROOT &&
        in->model.kind != TIKU_KIND_FILE) {
        return 0;
    }
    if (now - in->last_refresh < TIKU_TRK_INFO_REFRESH_US) {
        return 0;
    }
    in->last_refresh = now;
    if (b->ops->stat(b, in->path, &fresh) != 0) {
        return 0;
    }
    if (fresh.facts.size == in->model.facts.size &&
        fresh.facts.avail == in->model.facts.avail &&
        fresh.facts.mtime == in->model.facts.mtime) {
        return 0;
    }
    in->model = fresh;
    return 1;
}

int
tiku_trk_info_moved(tiku_trk_info_t *in, tiku_backend_t *b,
                    const char *from, const char *to)
{
    size_t n;
    char rebuilt[TIKU_PATH_MAX];

    if (in == NULL || !in->used || from == NULL || to == NULL ||
        from[0] == '\0') {
        return 0;
    }
    if (strcmp(in->path, from) == 0) {
        snprintf(in->path, sizeof in->path, "%s", to);
    } else {
        /* An ancestor moving takes the entry with it, at a component
         * boundary so "/a/b" does not claim "/a/bc". */
        n = strlen(from);
        if (n == 0u || strncmp(from, in->path, n) != 0 ||
            in->path[n] != '/') {
            return 0;
        }
        if ((int)snprintf(rebuilt, sizeof rebuilt, "%s%s", to,
                          in->path + n) >= (int)sizeof rebuilt) {
            return 0;
        }
        snprintf(in->path, sizeof in->path, "%s", rebuilt);
    }
    /* Re-read rather than patched: the title, the Location field and the
     * kind all come off the model, and a model still describing the old
     * path would leave two of the three quietly wrong. */
    if (b != NULL && b->ops != NULL && b->ops->stat != NULL) {
        (void)b->ops->stat(b, in->path, &in->model);
    }
    return 1;
}

/** @brief The entry in a listing carrying the identity we are looking for. */
static int
same_node(const tiku_model_t *m, void *ctx)
{
    tiku_trk_info_t *in = ctx;

    if (m->node_id != 0 && m->node_id == in->model.node_id) {
        snprintf(in->path, sizeof in->path, "%s", m->path);
        return 1;
    }
    return 0;
}

int
tiku_trk_info_refind(tiku_trk_info_t *in, tiku_backend_t *b)
{
    char dir[TIKU_PATH_MAX], was[TIKU_PATH_MAX];

    if (in == NULL || !in->used || b == NULL || b->ops == NULL ||
        b->ops->list == NULL || b->ops->stat == NULL ||
        in->model.node_id == 0) {
        return 0;
    }
    if (!tiku_trk_info_location(in, dir, sizeof dir, NULL, 0)) {
        return 0;
    }
    snprintf(was, sizeof was, "%s", in->path);
    (void)b->ops->list(b, dir, same_node, in);
    if (strcmp(was, in->path) == 0) {
        return 0;               /* nothing there answers to it */
    }
    /* Re-read rather than trusting the listing's copy: the title, the
     * Location field and the kind all come off the model. */
    return (b->ops->stat(b, in->path, &in->model) == 0);
}

int
tiku_trk_info_gone(const tiku_trk_info_t *in, const char *removed)
{
    size_t n;

    if (in == NULL || !in->used || removed == NULL || removed[0] == '\0') {
        return 0;
    }
    if (strcmp(in->path, removed) == 0) {
        return 1;
    }
    /* A folder above it going takes it too: the entry is gone whether or
     * not anyone said its name. */
    n = strlen(removed);
    return (n > 0u && strncmp(removed, in->path, n) == 0 &&
            in->path[n] == '/');
}

/*---------------------------------------------------------------------------*/
/* Permissions                                                               */
/*---------------------------------------------------------------------------*/

/** @brief The bit one column of the grid stands for. */
static unsigned
perm_bit(int which)
{
    switch (which) {
    case TIKU_TRK_INFO_PERM_WRITE: return TIKU_P_WRITE;
    case TIKU_TRK_INFO_PERM_EXEC:  return TIKU_P_EXEC;
    default:                       return TIKU_P_READ;
    }
}

int
tiku_trk_info_perm(const tiku_trk_info_t *in, int which)
{
    if (in == NULL) {
        return 0;
    }
    return (in->perm & perm_bit(which)) != 0u;
}

int
tiku_trk_info_perm_toggle(tiku_trk_info_t *in, tiku_backend_t *b,
                          int which, char *err, size_t errmax)
{
    unsigned want;

    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    if (in == NULL || !in->used) {
        return -1;
    }
    if (!in->perm_editable || b == NULL || b->ops == NULL ||
        b->ops->setperm == NULL) {
        snprintf(err, errmax, "%s",
                 tiku_str("info.msg.perms_cannot_change"));
        return -1;
    }
    want = in->perm ^ perm_bit(which);
    if (b->ops->setperm(b, in->path, want, err, errmax) != 0) {
        /* The box goes back rather than staying where the click put it:
         * with no OK button the tick IS the claim, and a tick the store
         * refused would be a lie the window keeps telling (AW-078). */
        return -1;
    }
    in->perm = want;
    in->model.facts.perm = want;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Attributes                                                                */
/*---------------------------------------------------------------------------*/

/** @brief Whether a byte run is text a person could read. */
static int
printable(const unsigned char *v, size_t len)
{
    size_t i;

    if (len == 0u) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (v[i] == '\0') {
            return (i + 1u == len);     /* a trailing terminator is fine */
        }
        if (v[i] < 0x20u || v[i] > 0x7eu) {
            return 0;
        }
    }
    return 1;
}

static int
attr_row(const char *name, const void *val, size_t len, void *ctx)
{
    tiku_trk_info_t *in = ctx;
    tiku_trk_info_attr_t *a;

    if (in->nattr >= TIKU_TRK_INFO_ATTRS) {
        return 1;
    }
    a = &in->attr[in->nattr++];
    snprintf(a->name, sizeof a->name, "%s", name);
    if (printable(val, len)) {
        snprintf(a->type, sizeof a->type, "text");
        snprintf(a->value, sizeof a->value, "%.*s", (int)len,
                 (const char *)val);
    } else if (len == 4u || len == 8u) {
        int64_t n = 0;

        memcpy(&n, val, len);
        snprintf(a->type, sizeof a->type, (len == 4u) ? "int32" : "int64");
        snprintf(a->value, sizeof a->value, "%lld", (long long)n);
    } else {
        /* A blob is described rather than spelled: printing a thumbnail as
         * mojibake tells the reader nothing and costs a screenful. */
        snprintf(a->type, sizeof a->type, "raw");
        snprintf(a->value, sizeof a->value, "%u bytes", (unsigned)len);
    }
    return 0;
}

int
tiku_trk_info_attrs(tiku_trk_info_t *in, tiku_store_t *store)
{
    if (in == NULL) {
        return 0;
    }
    in->nattr = 0;
    if (store == NULL) {
        return 0;
    }
    (void)tiku_state_each(store, in->path, attr_row, in);
    return in->nattr;
}

/*---------------------------------------------------------------------------*/
/* Where the clickable fields lead                                           */
/*---------------------------------------------------------------------------*/

int
tiku_trk_info_location(const tiku_trk_info_t *in, char *dir, size_t dmax,
                       char *select, size_t smax)
{
    const char *slash;

    if (select != NULL && smax > 0u) {
        select[0] = '\0';
    }
    if (in == NULL || !in->used || dir == NULL || dmax == 0u) {
        return 0;
    }
    dir[0] = '\0';
    slash = strrchr(in->path, '/');
    if (slash == NULL || slash == in->path) {
        if (strcmp(in->path, "/") == 0) {
            return 0;           /* the root is in nothing */
        }
        snprintf(dir, dmax, "/");
    } else {
        int n = (int)(slash - in->path);

        if (n >= (int)dmax) {
            n = (int)dmax - 1;
        }
        memcpy(dir, in->path, (size_t)n);
        dir[n] = '\0';
    }
    if (select != NULL) {
        snprintf(select, smax, "%s", in->model.name);
    }
    return 1;
}

int
tiku_trk_info_link_target(const tiku_trk_info_t *in, tiku_backend_t *b,
                          char *dir, size_t dmax, char *select, size_t smax)
{
    char tgt[TIKU_PATH_MAX];
    tiku_model_t m;
    ssize_t got;
    const char *slash;

    if (select != NULL && smax > 0u) {
        select[0] = '\0';
    }
    if (in == NULL || !in->used || dir == NULL || dmax == 0u ||
        in->model.kind != TIKU_KIND_SYMLINK) {
        return 0;
    }
    dir[0] = '\0';
    got = readlink(in->path, tgt, sizeof tgt - 1u);
    if (got <= 0) {
        return 0;
    }
    tgt[got] = '\0';
    if (b == NULL || b->ops == NULL || b->ops->stat == NULL ||
        b->ops->stat(b, tgt, &m) != 0) {
        return 0;               /* broken: the caller offers a picker */
    }
    slash = strrchr(tgt, '/');
    if (slash == NULL || slash == tgt) {
        snprintf(dir, dmax, "/");
    } else {
        int n = (int)(slash - tgt);

        if (n >= (int)dmax) {
            n = (int)dmax - 1;
        }
        memcpy(dir, tgt, (size_t)n);
        dir[n] = '\0';
    }
    if (select != NULL) {
        snprintf(select, smax, "%s", m.name);
    }
    return 1;
}

/** @brief One attribute kept aside while a link is replaced. */
typedef struct {
    char          name[40];
    unsigned char val[256];
    size_t        len;
} kept_attr_t;

typedef struct {
    kept_attr_t a[TIKU_TRK_INFO_ATTRS];
    int         n;
} kept_t;

static int
keep_attr(const char *name, const void *val, size_t len, void *ctx)
{
    kept_t *k = ctx;

    if (k->n >= TIKU_TRK_INFO_ATTRS || len > sizeof k->a[0].val) {
        /* Anything that will not fit is dropped LOUDLY rather than
         * truncated: half a thumbnail written back is a valid-looking
         * attribute holding nonsense. */
        return 0;
    }
    snprintf(k->a[k->n].name, sizeof k->a[k->n].name, "%s", name);
    memcpy(k->a[k->n].val, val, len);
    k->a[k->n].len = len;
    k->n++;
    return 0;
}

int
tiku_trk_info_relink(tiku_trk_info_t *in, tiku_backend_t *b,
                     tiku_store_t *store, const char *target, char *err,
                     size_t errmax)
{
    kept_t kept;
    int i;

    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    if (in == NULL || !in->used || target == NULL || target[0] == '\0') {
        snprintf(err, errmax, "%s", tiku_str("info.msg.nothing_to_point"));
        return -1;
    }
    if (in->model.kind != TIKU_KIND_SYMLINK) {
        snprintf(err, errmax, "%s", tiku_str("info.msg.not_a_link"));
        return -1;
    }
    /* Read BEFORE the link is replaced, and read RAW: a store that keeps
     * state on the file itself loses it with the old link, and writing back
     * the displayed text would turn a blob into a description of itself
     * (AW-074). */
    kept.n = 0;
    if (store != NULL) {
        (void)tiku_state_each(store, in->path, keep_attr, &kept);
    }
    if (unlink(in->path) != 0 || symlink(target, in->path) != 0) {
        snprintf(err, errmax, "%s", tiku_str("info.msg.repoint_failed"));
        return -1;
    }
    for (i = 0; i < kept.n; i++) {
        (void)tiku_state_write(store, in->path, kept.a[i].name,
                                   kept.a[i].val, kept.a[i].len);
    }
    if (b != NULL && b->ops != NULL && b->ops->stat != NULL) {
        (void)b->ops->stat(b, in->path, &in->model);
    }
    return 0;
}
