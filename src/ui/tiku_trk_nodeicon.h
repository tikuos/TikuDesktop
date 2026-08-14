/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_nodeicon.h - where an entry's icon comes from, and its cache.
 *
 * Three sources in order: the NODE's own picture, then its type's, then the
 * generic one for its kind.  The order is the whole rule -- a file that
 * carries its own picture must not be drawn as a generic anything.
 *
 * The other half is the shortcut past it.  Whether a node has a picture is
 * asked ONCE, when the entry is taken in, and remembered on the model; the
 * draw path then reads the store only for the entries that said yes.  The
 * cost of getting that wrong is not theoretical -- before this existed, every
 * row of every frame sniffed the file's first bytes and read an attribute to
 * ask a question whose answer had not changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NODEICON_H_
#define TIKU_TRK_NODEICON_H_

#include "tiku_trk_model.h"
#include "tiku_trk_state.h"
#include "tiku_trk_thumb.h"

/** @brief Where the icon being drawn came from. */
typedef enum {
    TIKU_TRK_ICON_FROM_UNKNOWN = 0,
    TIKU_TRK_ICON_FROM_NODE,        /* the entry's own icon attribute     */
    /* Also the node's, and Tracker files it under the same source; it is
     * separate here because the caller has to DRAW it differently -- a
     * thumbnail is a bitmap to load, not a name in the art set. */
    TIKU_TRK_ICON_FROM_THUMB,
    TIKU_TRK_ICON_FROM_TYPE,        /* its type's                         */
    TIKU_TRK_ICON_FROM_GENERIC      /* the fallback for its kind          */
} tiku_trk_icon_from_t;

/** @brief The attribute an entry's own icon lives in. */
#define TIKU_TRK_ATTR_ICON "_trk/icon"

/** @brief How many node icons are remembered at once. */
#define TIKU_TRK_NODEICON_MISS_MAX 8

#define TIKU_TRK_NODEICON_MAX 64

/** @brief One remembered node icon. */
struct tiku_trk_types;

typedef struct {
    char     path[TIKU_TRK_PATH_MAX];
    char     name[TIKU_TRK_NAME_MAX];   /* the art it resolved to         */
    int      permanent;                 /* preloaded; survives a delete   */
    int      used;
} tiku_trk_nodeicon_ent_t;

/** @brief The node-icon cache. */
typedef struct {
    tiku_trk_nodeicon_ent_t e[TIKU_TRK_NODEICON_MAX];
    int                     n;
    int                     reads;      /* store probes actually made     */
    uint64_t                generation; /* changed art invalidates draws  */
    const struct tiku_trk_types *types;   /* where a type's art is
                                          * declared, or NULL       */
    /* Types known to have no art of their own.  A miss costs a database
     * walk and a comparison against the bare model; remembered, it costs
     * a string compare, which is the whole of IV-011. */
    char                    miss[TIKU_TRK_NODEICON_MISS_MAX]
                                [TIKU_TRK_TYPE_MAX];
    int                     miss_n;
} tiku_trk_nodeicon_t;

/** @brief How many types the negative cache remembers. */
int tiku_trk_nodeicon_misses(const tiku_trk_nodeicon_t *c);

void tiku_trk_nodeicon_init(tiku_trk_nodeicon_t *c);

/**
 * @brief Ask, once, whether @p m has a picture of its own (IV-005, IV-037).
 *
 * The one probe: a thumbnail it already has or would be given, else an icon
 * attribute.  A model that has been asked is never asked again, so calling
 * this from a listing pass costs one probe per entry and calling it from a
 * draw costs nothing after the first.
 *
 * @param q  The thumbnail queue, for the "generate thumbnails" setting;
 *           NULL means no thumbnails, and only the attribute is looked for.
 * @return the resulting TIKU_TRK_ICONHINT_*.
 */
int tiku_trk_nodeicon_hint(tiku_trk_store_t *store,
                           const tiku_trk_thumb_q_t *q, tiku_trk_model_t *m);

/**
 * @brief Whether @p m could have an icon of its own at all (IV-005).
 *
 * Answered from the hint the model already carries, never by probing, which
 * is what lets the draw path ask it per row per frame.
 */
int tiku_trk_nodeicon_possible(const tiku_trk_model_t *m);

/**
 * @brief The icon to draw for @p m, and where it came from.
 *
 * @param out  Receives the art's name.  A thumbnail gets one too -- the
 *             fallback its row draws until the picture has been made.
 * @return the source, so a caller can tell a node icon from a fallback.
 */
tiku_trk_icon_from_t tiku_trk_nodeicon_resolve(tiku_trk_nodeicon_t *c,
                                               tiku_trk_store_t *store,
                                               const tiku_trk_model_t *m,
                                               char *out, size_t max);

/**
 * @brief Let the cache consult the type database for a type's icon.
 *
 * The database is where a type's art is DECLARED and can be edited; the
 * compiled table is only what the port ships knowing.  Consulted ahead of
 * it, so a type given an icon by the user wears it (AW-013, IV-011).
 */
void tiku_trk_nodeicon_set_types(tiku_trk_nodeicon_t *c,
                                 const struct tiku_trk_types *db);

/** @brief Remember @p path's icon, and refuse to forget it (IV-041). */
void tiku_trk_nodeicon_preload(tiku_trk_nodeicon_t *c, const char *path,
                               const char *name);

/**
 * @brief Forget @p path's icon because the entry changed (IV-038).
 *
 * @return 1 when something was dropped.
 */
int tiku_trk_nodeicon_changed(tiku_trk_nodeicon_t *c, const char *path);
/** @brief Generation incremented whenever cached art is invalidated. */
uint64_t tiku_trk_nodeicon_generation(const tiku_trk_nodeicon_t *c);

/**
 * @brief Forget @p path's icon because the entry went (IV-041).
 *
 * A preloaded entry is KEPT: it was cached because it is wanted often, and
 * the file being deleted does not make it less wanted next time.
 *
 * @return 1 when something was dropped.
 */
int tiku_trk_nodeicon_deleted(tiku_trk_nodeicon_t *c, const char *path);

/**
 * @brief Forget @p path's icon because the entry it was preloaded FROM has
 * gone (IV-041).
 *
 * The stronger removal, and the only one that takes a permanent entry: it is
 * for whoever preloaded it to say the file behind it no longer exists.  An
 * ordinary deletion goes through _deleted and leaves it alone.
 *
 * @return 1 when something was dropped.
 */
int tiku_trk_nodeicon_removing(tiku_trk_nodeicon_t *c, const char *path);

/** @brief Whether an attribute name is one that changes an icon (IV-038). */
int tiku_trk_nodeicon_attr_matters(const char *attr);

/**
 * @brief Take back the hint on @p m as well, after such a change (IV-038).
 *
 * Dropping the cached art alone would leave a model that was told it has no
 * picture still believing it, so a newly given icon would never be looked
 * for.
 */
void tiku_trk_nodeicon_unhint(tiku_trk_model_t *m);

#endif /* TIKU_TRK_NODEICON_H_ */
