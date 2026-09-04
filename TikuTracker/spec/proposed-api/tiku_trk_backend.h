/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   The backend vtable both the POSIX and the TikuOS backends implement — the single seam between Tracker and a store. Capability bits that select fallbacks rather than signal failure; lifecycle, stepped listing, per-entry read/write/create/unlink/rename/bulk-transfer, the policy half of the refusal predicate plus capability generation, liveness arming and pumping, and the store's UI-state store. Also carries the AddPosesTask lister as a yielding state machine whose every cursor lives in the struct. */

/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_backend.h - the one seam between Tracker and a store.
 *
 * Two implementations and no third caller-visible distinction: a POSIX
 * backend over a real filesystem, and a TikuOS backend over a shell session.
 * Everything above this file -- poses, menus, alerts, queries, spatial state
 * -- is written once against these ops.
 *
 * Ops marked /@ worker @/ may block for the length of a link round trip and
 * are called on the backend's own worker; their results reach the view as
 * events, never as return values delivered to the UI thread.  Ops marked
 * /@ instant @/ answer from the mirror and may be called from anywhere,
 * which is what lets menu enablement be recomputed on every menu open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_BACKEND_H_
#define TIKU_TRK_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_trk_event.h"
#include "tiku_model.h"
#include "tiku_state.h"

/* What a backend can do.  Absent bits are not failures: they select the
 * fallback the design already specifies. */
#define TIKU_TRK_BK_PUSH      0x0001u /**< the store rings on change      */
#define TIKU_TRK_BK_DIRWATCH  0x0002u /**< a directory watch is accepted  */
#define TIKU_TRK_BK_EPOCH     0x0004u /**< cheap "nothing changed" oracle */
#define TIKU_TRK_BK_MOVE      0x0008u /**< can report a true rename       */
#define TIKU_TRK_BK_XATTR     0x0010u /**< state can live on the object   */
#define TIKU_TRK_BK_TRASH     0x0020u /**< has a Trash directory          */
#define TIKU_TRK_BK_QUERY_IDX 0x0040u /**< holds a complete typed index   */
#define TIKU_TRK_BK_BLOCKING  0x0080u /**< worker ops really do block     */

typedef struct tiku_backend tiku_backend_t;

/** @brief One entry as a listing hands it over. */
typedef struct {
    char     name[TIKU_NAME_MAX];
    uint8_t  kind;      /**< tiku_kind_t                             */
    uint32_t valhash;   /**< rendered value hash, or 0                   */
    uint8_t  truncated; /**< the value read was cut; treat as changed    */
} tiku_trk_dirent_t;

struct tiku_volume;

typedef struct tiku_trk_backend_ops {
    const char *name;

    /* -- lifecycle ------------------------------------------------- */
    /** @brief Learn what this store can do; sets caps.  /@ worker @/ */
    int  (*probe)(tiku_backend_t *b);
    void (*close)(tiku_backend_t *b);

    /** @brief Fill the roster entry: label, identity, capacity, flags.
     *  /@ worker @/ */
    int  (*volume_info)(tiku_backend_t *b, struct tiku_volume *out);

    /* -- listing, always stepped so a window never blocks ----------- */
    int  (*list_begin)(tiku_backend_t *b, const char *dir, uint32_t gen);
    /** @brief Deliver up to @p max entries.  @return count, 0 at the end,
     *  -1 on error.  /@ worker @/ */
    int  (*list_step)(tiku_backend_t *b, tiku_trk_dirent_t *out,
                      int max);
    void (*list_end)(tiku_backend_t *b);

    /* -- one entry -------------------------------------------------- */
    /** @brief Fill kind, size, times, perms, type facts.  /@ worker @/ */
    int  (*stat)(tiku_backend_t *b, const char *path,
                 tiku_model_t *m);
    /** @brief Read the object's rendered value.  /@ worker @/ */
    int  (*read_value)(tiku_backend_t *b, tiku_model_t *m,
                       char *err, size_t errmax);
    /** @brief Write it.  0 on success; -1 fills @p err with the store's
     *  own refusal text, reproduced verbatim in the alert.  /@ worker @/ */
    int  (*write_value)(tiku_backend_t *b, const tiku_model_t *m,
                        const char *val, size_t len,
                        char *err, size_t errmax);
    int  (*create)(tiku_backend_t *b, const char *dir, const char *name,
                   int is_dir, char *err, size_t errmax);
    int  (*unlink)(tiku_backend_t *b, const tiku_model_t *m,
                   char *err, size_t errmax);
    int  (*rename)(tiku_backend_t *b, const tiku_model_t *m,
                   const char *newname, char *err, size_t errmax);
    /** @brief Bulk transfer in and out of this store.  @p progress is
     *  called per chunk and returns non-zero to cancel.  /@ worker @/ */
    int  (*copy_in)(tiku_backend_t *b, const char *host_path,
                    const char *dst_path,
                    int (*progress)(void *ctx, uint64_t done, uint64_t all),
                    void *ctx, char *err, size_t errmax);
    int  (*copy_out)(tiku_backend_t *b, const tiku_model_t *m,
                     const char *host_path,
                     int (*progress)(void *ctx, uint64_t done, uint64_t all),
                     void *ctx, char *err, size_t errmax);

    /* -- policy ----------------------------------------------------- */
    /** @brief The single refusal predicate's backend half.  /@ instant @/ */
    tiku_trk_verdict_t (*permit)(tiku_backend_t *b,
                                 const tiku_model_t *m,
                                 tiku_trk_action_t a, uint8_t *missing_cap);
    /** @brief Capability this channel holds.  /@ instant @/ */
    unsigned (*caps_held)(tiku_backend_t *b);
    /** @brief Bumps whenever caps_held() changes, so every cached verdict
     *  can be invalidated wholesale rather than one at a time. */
    unsigned (*cap_generation)(tiku_backend_t *b);
    const char *(*cap_name)(tiku_backend_t *b, uint8_t bit);
    /** @brief Re-read the channel's capability now.  Called at connect, on
     *  a capability notification, and after any refusal that named one, so
     *  a mirror that went stale heals in one round trip.  /@ worker @/ */
    int  (*refresh_caps)(tiku_backend_t *b);

    /* -- liveness --------------------------------------------------- */
    int  (*watch_dir)(tiku_backend_t *b, const char *dir);
    /** @brief Ask for per-entry liveness.  Returns tiku_trk_watch_t: a
     *  full watch table demotes to polling, it never fails. */
    int  (*watch_node)(tiku_backend_t *b, const char *path, int prio);
    void (*unwatch_all)(tiku_backend_t *b);
    /** @brief Service the link, translate or synthesize events into the
     *  queue.  @return events posted, -1 on link loss.  /@ worker @/ */
    int  (*pump)(tiku_backend_t *b, int64_t now_us, int timeout_ms);
    int  (*liveness)(tiku_backend_t *b, tiku_trk_liveness_t *out);

    /* -- where UI state for this store's objects lives -------------- */
    /** @brief The store's state store: xattrs on a POSIX volume, a
     *  host-side sidecar for a device, memory when neither can persist. */
    tiku_store_t *(*state_store)(tiku_backend_t *b);
} tiku_backend_ops_t;

struct tiku_backend {
    const tiku_backend_ops_t *ops;
    void            *ctx;        /**< tiku_ns_t * for a device      */
    tiku_trk_dev_t   dev;
    uint8_t          kind;       /**< tiku_trk_backend_kind_t            */
    uint32_t         caps;       /**< TIKU_TRK_BK_*, set by probe()      */
    tiku_trk_evq_t  *q;          /**< where synthesized events go        */
    char             identity[64]; /**< fs UUID, or the device die uid   */
};

/** @brief A backend over the host filesystem, rooted at @p mount. */
tiku_backend_t *tiku_trk_backend_posix_new(const char *mount,
                                               tiku_trk_dev_t dev,
                                               tiku_trk_evq_t *q);

/**
 * @brief A backend over an established device session.
 *
 * @param ns  A tiku_ns_t mirror; the backend does not own it.  Every
 *            blocking op runs on this backend's worker, because the session
 *            is not reentrant and the UI thread must never touch it.
 */
tiku_backend_t *tiku_trk_backend_tiku_new(void *ns, tiku_trk_dev_t dev,
                                              tiku_trk_evq_t *q);

void tiku_trk_backend_free(tiku_backend_t *b);

/* ---------------------------------------------------------------------- *
 * The lister -- Tracker's AddPosesTask, as a yielding state machine
 * ---------------------------------------------------------------------- */

#define TIKU_TRK_CHUNK_MAX     50      /**< models per chunk              */
#define TIKU_TRK_CHUNK_US  300000LL    /**< ship a partial chunk after    */

/**
 * @brief Every cursor lives in the struct, deliberately.
 *
 * Nothing here may be a local across a yield; a skipped chunk on a device
 * looks exactly like a folder that legitimately has fewer entries.
 */
typedef struct {
    tiku_backend_t *b;
    uint32_t            gen;         /**< abandoned listings drop         */
    char                dir[TIKU_PATH_MAX];
    uint32_t            cursor;
    int                 done;
    int64_t             chunk_started_us;
    tiku_trk_dirsnap_t *snap;        /**< the differ's baseline           */
} tiku_trk_lister_t;

/**
 * @brief Begin a listing.
 *
 * The directory is watched and snapshotted BEFORE the first entry is read,
 * so a change during the load is queued rather than lost.  Events are not
 * suppressed while loading; single insertion is the view's business.
 */
int tiku_trk_lister_start(tiku_trk_lister_t *l, tiku_backend_t *b,
                          const char *dir, uint32_t gen);

/** @brief Deliver one chunk.  @return models produced, 0 when finished. */
int tiku_trk_lister_step(tiku_trk_lister_t *l, int64_t now_us,
                         tiku_model_t **out, int max);

/** @brief Whether this listing still belongs to the view's generation. */
int tiku_trk_lister_valid(const tiku_trk_lister_t *l, uint32_t view_gen);

void tiku_trk_lister_stop(tiku_trk_lister_t *l);

#endif /* TIKU_TRK_BACKEND_H_ */
