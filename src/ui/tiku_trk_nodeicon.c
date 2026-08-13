/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_nodeicon.c - the lookup order and the cache behind it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_nodeicon.h"
#include "tiku_trk_types.h"
#include "tiku_trk_thumb.h"

#include <stdio.h>
#include <string.h>

void
tiku_trk_nodeicon_init(tiku_trk_nodeicon_t *c)
{
    if (c != NULL) {
        memset(c, 0, sizeof *c);
    }
}

int
tiku_trk_nodeicon_hint(tiku_trk_store_t *store, const tiku_trk_thumb_q_t *q,
                       tiku_trk_model_t *m)
{
    char art[TIKU_TRK_NAME_MAX];

    if (m == NULL) {
        return TIKU_TRK_ICONHINT_NONE;
    }
    if (m->facts.icon_hint != TIKU_TRK_ICONHINT_UNASKED) {
        return m->facts.icon_hint;
    }
    /* A thumbnail is a node icon, and it counts from the moment the file is
     * one we WOULD make a thumbnail of -- before it exists.  Waiting for the
     * attribute would mean the first draw of every image took the type icon
     * and nothing ever asked again (IV-037). */
    if (q != NULL && tiku_trk_thumb_wanted(q, m->path)) {
        m->facts.icon_hint = TIKU_TRK_ICONHINT_THUMB;
    } else if (store != NULL &&
               tiku_trk_state_read(store, m->path, TIKU_TRK_ATTR_ICON, art,
                                   sizeof art) > 0) {
        m->facts.icon_hint = TIKU_TRK_ICONHINT_ATTR;
    } else {
        m->facts.icon_hint = TIKU_TRK_ICONHINT_NONE;
    }
    return m->facts.icon_hint;
}

int
tiku_trk_nodeicon_possible(const tiku_trk_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    /* From the hint alone.  An entry that has not been asked yet answers no
     * rather than probing here: this is called from the draw path, and one
     * read hidden inside it is the read the whole tier exists to avoid. */
    return (m->facts.icon_hint == TIKU_TRK_ICONHINT_ATTR ||
            m->facts.icon_hint == TIKU_TRK_ICONHINT_THUMB);
}

/** @brief The cache entry for @p path, or NULL. */
static tiku_trk_nodeicon_ent_t *
find(tiku_trk_nodeicon_t *c, const char *path)
{
    int i;

    for (i = 0; i < c->n; i++) {
        if (c->e[i].used && strcmp(c->e[i].path, path) == 0) {
            return &c->e[i];
        }
    }
    return NULL;
}

/** @brief A free slot, evicting the oldest non-permanent one if need be. */
static tiku_trk_nodeicon_ent_t *
slot(tiku_trk_nodeicon_t *c)
{
    int i;

    for (i = 0; i < c->n; i++) {
        if (!c->e[i].used) {
            return &c->e[i];
        }
    }
    if (c->n < TIKU_TRK_NODEICON_MAX) {
        return &c->e[c->n++];
    }
    /* Full: the first entry nobody asked to keep goes.  A preloaded one
     * is never evicted, which is what "permanent" means. */
    for (i = 0; i < c->n; i++) {
        if (!c->e[i].permanent) {
            return &c->e[i];
        }
    }
    return NULL;
}

tiku_trk_icon_from_t
tiku_trk_nodeicon_resolve(tiku_trk_nodeicon_t *c, tiku_trk_store_t *store,
                          const tiku_trk_model_t *m, char *out, size_t max)
{
    tiku_trk_nodeicon_ent_t *e;
    char art[TIKU_TRK_NAME_MAX];

    if (out != NULL && max > 0u) {
        out[0] = '\0';
    }
    if (c == NULL || m == NULL) {
        return TIKU_TRK_ICON_FROM_UNKNOWN;
    }
    if (m->facts.icon_hint == TIKU_TRK_ICONHINT_THUMB) {
        /* Nothing to read: the caller loads the picture itself.  It is
         * still given a name, because a thumbnail that has not been made
         * yet has to draw as SOMETHING and the type icon is what the row
         * shows meanwhile (IV-031). */
        if (out != NULL) {
            snprintf(out, max, "%s", tiku_trk_model_icon_name(m));
        }
        return TIKU_TRK_ICON_FROM_THUMB;
    }
    e = find(c, m->path);
    if (e != NULL) {
        if (out != NULL) {
            snprintf(out, max, "%s", e->name);
        }
        return TIKU_TRK_ICON_FROM_NODE;
    }
    if (tiku_trk_nodeicon_possible(m) && store != NULL) {
        c->reads++;
        if (tiku_trk_state_read(store, m->path, TIKU_TRK_ATTR_ICON, art,
                                sizeof art) > 0) {
            /* The entry's OWN icon wins over anything its type would give
             * it: a file that carries its own picture must not be drawn as
             * a generic anything (IV-001). */
            art[sizeof art - 1u] = '\0';
            e = slot(c);
            if (e != NULL) {
                memset(e, 0, sizeof *e);
                snprintf(e->path, sizeof e->path, "%s", m->path);
                snprintf(e->name, sizeof e->name, "%s", art);
                e->used = 1;
            }
            if (out != NULL) {
                snprintf(out, max, "%s", art);
            }
            return TIKU_TRK_ICON_FROM_NODE;
        }
    }
    if (m->type[0] != '\0') {
        int k;

        for (k = 0; k < c->miss_n; k++) {
            if (strcmp(c->miss[k], m->type) == 0) {
                /* Known to have none: the generic answer, without the
                 * database walk and the comparison that proved it the
                 * first time (IV-011). */
                if (out != NULL) {
                    snprintf(out, max, "%s", tiku_trk_model_icon_name(m));
                }
                return TIKU_TRK_ICON_FROM_GENERIC;
            }
        }
    }
    if (m->type[0] != '\0' && c->types != NULL) {
        const tiku_trk_typerec_t *tr = tiku_trk_types_find(c->types,
                                                           m->type);

        if (tr != NULL && tr->icon[0] != '\0') {
            /* Declared for the type rather than compiled in: this is the
             * one an installation can change. */
            if (out != NULL) {
                snprintf(out, max, "%s", tr->icon);
            }
            return TIKU_TRK_ICON_FROM_TYPE;
        }
    }
    if (out != NULL) {
        snprintf(out, max, "%s", tiku_trk_model_icon_name(m));
    }
    if (m->type[0] != '\0') {
        /* The type only counts as the SOURCE when it actually supplies
         * something.  A type nothing is registered for lands on the same
         * document icon an untyped file gets, and calling that a type
         * answer would let a caller share one cached icon across every
         * unregistered type in the listing. */
        tiku_trk_model_t bare = *m;

        bare.type[0] = '\0';
        if (strcmp(tiku_trk_model_icon_name(m),
                   tiku_trk_model_icon_name(&bare)) != 0) {
            return TIKU_TRK_ICON_FROM_TYPE;
        }
        if (c->miss_n < TIKU_TRK_NODEICON_MISS_MAX) {
            /* Nothing anywhere has art for this type.  Remembered so the
             * walk is paid once rather than per row of a folder full of
             * them (IV-011). */
            snprintf(c->miss[c->miss_n], TIKU_TRK_TYPE_MAX, "%s", m->type);
            c->miss_n++;
        }
    }
    return TIKU_TRK_ICON_FROM_GENERIC;
}

int
tiku_trk_nodeicon_misses(const tiku_trk_nodeicon_t *c)
{
    return (c != NULL) ? c->miss_n : 0;
}

void
tiku_trk_nodeicon_set_types(tiku_trk_nodeicon_t *c,
                            const struct tiku_trk_types *db)
{
    if (c != NULL) {
        c->types = db;
        /* What is missing is a question about the database, so a new one
         * makes every remembered miss an unasked question again. */
        c->miss_n = 0;
    }
}

void
tiku_trk_nodeicon_preload(tiku_trk_nodeicon_t *c, const char *path,
                          const char *name)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL || name == NULL) {
        return;
    }
    e = find(c, path);
    if (e == NULL) {
        e = slot(c);
    }
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", path);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->permanent = 1;
    e->used = 1;
}

int
tiku_trk_nodeicon_changed(tiku_trk_nodeicon_t *c, const char *path)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL) {
        return 0;
    }
    e = find(c, path);
    if (e == NULL) {
        return 0;
    }
    /* Dropped whether or not it is permanent: the attribute CHANGED, so
     * what was remembered is now wrong, and being wanted often is no
     * reason to keep showing the wrong picture (IV-038). */
    memset(e, 0, sizeof *e);
    return 1;
}

int
tiku_trk_nodeicon_deleted(tiku_trk_nodeicon_t *c, const char *path)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL) {
        return 0;
    }
    e = find(c, path);
    if (e == NULL || e->permanent) {
        /* Preloaded entries survive: they were cached because they are
         * wanted often, and the file going does not make the next ask
         * less likely (IV-041). */
        return 0;
    }
    memset(e, 0, sizeof *e);
    return 1;
}

int
tiku_trk_nodeicon_removing(tiku_trk_nodeicon_t *c, const char *path)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL) {
        return 0;
    }
    e = find(c, path);
    if (e == NULL) {
        return 0;
    }
    /* Permanent or not.  The flag says "do not evict this to make room",
     * not "keep it after the file it describes has gone". */
    memset(e, 0, sizeof *e);
    return 1;
}

void
tiku_trk_nodeicon_unhint(tiku_trk_model_t *m)
{
    if (m != NULL) {
        m->facts.icon_hint = TIKU_TRK_ICONHINT_UNASKED;
    }
}

int
tiku_trk_nodeicon_attr_matters(const char *attr)
{
    if (attr == NULL) {
        return 0;
    }
    /* The icon itself and the thumbnail, since a thumbnail IS the node's
     * icon for an image; the thumbnail's stamp too, because that is what
     * changes when one is regenerated. */
    return (strcmp(attr, TIKU_TRK_ATTR_ICON) == 0 ||
            strcmp(attr, TIKU_TRK_ATTR_THUMB) == 0 ||
            strcmp(attr, TIKU_TRK_ATTR_THUMB_TIME) == 0);
}
