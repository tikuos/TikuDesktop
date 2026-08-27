/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   Decision 3: the one notification vocabulary that carries liveness into the view. Opcodes (including the query and link-stall ones, so there is no second vocabulary), the explicit field mask that replaces the attr-name-present discriminator, the coalescing queue with the accumulator and the honest lost flag, the directory differ that synthesizes device events with its fixed REMOVED/CREATED/CHANGED order and the value-hash gate, the liveness report the count view renders, the pending cache, and the single pose-view door. */

/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_event.h - the one notification vocabulary, and who manufactures it.
 *
 * Tracker's liveness rests on typed events with an ordering: created,
 * removed, moved, stat, attr.  TikuOS `watch` says only "touched, re-read",
 * with no opcode and eight shared slots, so the missing events are
 * SYNTHESIZED by the backend and never by the view.  inotify translates 1:1;
 * the device backend diffs directory snapshots.  Both arrive at one entry
 * point, so the pose view contains zero device-awareness.
 *
 * Coalescing happens at queue insert rather than in the view, because the
 * queue is the only place that sees every backend and can drop a change
 * whose rendered value did not move -- which is what makes a live folder
 * affordable on a 9600-baud link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_EVENT_H_
#define TIKU_TRK_EVENT_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_model.h"

#define TIKU_TRK_EV_NAME_MAX 96

typedef enum {
    TIKU_EV_CREATED = 1,   /**< B_ENTRY_CREATED                       */
    TIKU_EV_REMOVED,       /**< B_ENTRY_REMOVED                       */
    TIKU_EV_MOVED,         /**< B_ENTRY_MOVED; local backend only     */
    TIKU_TRK_EV_STAT_CHANGED,  /**< B_STAT_CHANGED                        */
    TIKU_TRK_EV_ATTR_CHANGED,  /**< B_ATTR_CHANGED                        */
    TIKU_TRK_EV_VOL_MOUNTED,   /**< B_DEVICE_MOUNTED; a session attached  */
    TIKU_TRK_EV_VOL_UNMOUNTED, /**< B_DEVICE_UNMOUNTED; link lost         */
    TIKU_TRK_EV_QUERY_UPDATE,  /**< B_QUERY_UPDATE; kept distinct so an
                                *   unmatch never closes the window       */
    TIKU_TRK_EV_SCAN_BEGIN,    /**< a listing or query walk started       */
    TIKU_TRK_EV_SCAN_END,
    TIKU_TRK_EV_LINK_STALLED,  /**< store still bound, not answering      */
    TIKU_TRK_EV_LINK_RESUMED,
    TIKU_TRK_EV_LIVENESS       /**< tier or watch budget changed          */
} tiku_trk_ev_op_t;

/* The explicit field selector.  It replaces BOTH the "is the attr string
 * present" discriminator and the aliasing attribute hash: a caller must
 * never have to guess which column moved. */
#define TIKU_TRK_F_NAME   0x0001u
#define TIKU_TRK_F_SIZE   0x0002u
#define TIKU_TRK_F_MTIME  0x0004u
#define TIKU_TRK_F_MODE   0x0008u
#define TIKU_TRK_F_VALUE  0x0010u  /**< device: the rendered value         */
#define TIKU_TRK_F_DESC   0x0020u  /**< device: the descriptor             */
#define TIKU_TRK_F_CAP    0x0040u  /**< device: required capability        */
#define TIKU_TRK_F_ICON   0x0080u
#define TIKU_TRK_F_TYPE   0x0100u
#define TIKU_TRK_F_ALL    0xFFFFu  /**< 0 also means "unknown, re-read"    */

typedef struct {
    uint8_t        op;         /**< tiku_trk_ev_op_t                      */
    uint8_t        backend;    /**< == ref.dev's backend slot             */
    uint16_t       fields;     /**< TIKU_TRK_F_*                          */
    uint32_t       gen;        /**< listing generation; stale ones drop    */
    uint64_t       seq;        /**< per-backend monotonic; replay order    */
    tiku_trk_ref_t ref;
    tiku_trk_ref_t dir;        /**< parent at the destination             */
    tiku_trk_ref_t from_dir;   /**< MOVED only                            */
    int64_t        when_us;
    const char    *attr;       /**< xattr name, or NULL = re-read all     */
    char           name[TIKU_TRK_EV_NAME_MAX];
    char           from_name[TIKU_TRK_EV_NAME_MAX];
} tiku_node_event_t;

/* ---------------------------------------------------------------------- *
 * The queue: coalescing, accumulation, and honest overflow
 * ---------------------------------------------------------------------- */

typedef struct tiku_trk_evq tiku_trk_evq_t;

tiku_trk_evq_t *tiku_trk_evq_new(size_t capacity_pow2);
void tiku_trk_evq_free(tiku_trk_evq_t *q);

/**
 * @brief Queue an event, merging where merging cannot lose information.
 *
 * A CHANGED merges into a pending CHANGED for the same ref (CHANGED means
 * "re-read", which is exactly what the device bus already means) and is
 * dropped when a CREATED for that ref is still pending.  CREATED, REMOVED
 * and MOVED never coalesce away.
 *
 * @return 1 inserted, 0 merged, -1 overflow (which sets the lost flag).
 */
int tiku_trk_evq_post(tiku_trk_evq_t *q, const tiku_node_event_t *ev);

/**
 * @brief Damp a backend's traffic: hold events @p delay_us after the last
 *        one, but never longer than @p cap_us after the first.
 *
 * Tracker's own accumulator constants are 200 ms and 5 s.  Local events
 * bypass this entirely -- they are free -- so a local folder stays as
 * responsive as Tracker's while a device folder is deliberately damped.
 */
void tiku_trk_evq_set_accum(tiku_trk_evq_t *q, uint8_t backend,
                            int64_t delay_us, int64_t cap_us);

/** @brief Take up to @p max events whose hold expired.  @return count. */
int tiku_trk_evq_drain(tiku_trk_evq_t *q, int64_t now_us,
                       tiku_node_event_t *out, int max);

/** @brief Liveness is never silently lost: overflow raises this, and the
 *  view answers with a whole-folder Refresh, rate-limited per backend. */
int tiku_trk_evq_lost(const tiku_trk_evq_t *q, uint8_t backend);
void tiku_trk_evq_clear_lost(tiku_trk_evq_t *q, uint8_t backend);

/* ---------------------------------------------------------------------- *
 * The differ: where synthesized events come from
 * ---------------------------------------------------------------------- */

typedef struct tiku_trk_dirsnap tiku_trk_dirsnap_t;

tiku_trk_dirsnap_t *tiku_trk_dirsnap_new(tiku_trk_dev_t dev,
                                         const char *dir);
void tiku_trk_dirsnap_free(tiku_trk_dirsnap_t *s);

/** @brief Start a fresh observation of the directory. */
void tiku_trk_dirsnap_begin(tiku_trk_dirsnap_t *s, uint32_t gen);

/**
 * @brief Offer one observed entry.
 *
 * @param valhash   FNV-1a of the RENDERED value text, or 0 when unread.
 * @param truncated Non-zero when the read was cut by the device's buffer;
 *                  such an entry is treated as always-changed, because the
 *                  hash of a truncated string is a lie.
 */
int tiku_trk_dirsnap_ingest(tiku_trk_dirsnap_t *s, const char *name,
                            int is_dir, uint32_t valhash, int truncated);

/**
 * @brief Emit the delta, in a fixed order that IS the ordering guarantee:
 *        every REMOVED, then every CREATED, then every CHANGED.
 *
 * Remove-before-create makes a replacement a single visible step;
 * change-after-create means a value moving on a brand-new entry never needs
 * the pending cache.  A CHANGED whose value hash did not move is not posted
 * at all -- the gate applied one layer below where Tracker applies it.
 *
 * @return Events emitted.
 */
int tiku_trk_dirsnap_commit(tiku_trk_dirsnap_t *s, tiku_trk_evq_t *q);

/** @brief Force the next commit to re-emit everything. */
void tiku_trk_dirsnap_invalidate(tiku_trk_dirsnap_t *s);

/* ---------------------------------------------------------------------- *
 * Liveness, as the view must be able to display it
 * ---------------------------------------------------------------------- */

typedef enum {
    TIKU_TRK_TIER_PUSH = 0, /**< the device rings us                      */
    TIKU_TRK_TIER_EPOCH,    /**< two cheap counters answer "nothing moved" */
    TIKU_TRK_TIER_POLL      /**< re-list on an interval; zero firmware     */
} tiku_trk_tier_t;

/** @brief What a watch request got.  A refusal demotes, never fails. */
typedef enum {
    TIKU_TRK_WATCH_PUSHED = 0,
    TIKU_TRK_WATCH_POLLED = 1,
    TIKU_TRK_WATCH_ERR    = -1
} tiku_trk_watch_t;

/** @brief The honesty instrument: what the count view renders. */
typedef struct {
    uint16_t rows;
    uint16_t pushed;
    uint16_t polled;
    uint16_t static_only;   /**< fresh=static: live for free forever       */
    uint16_t poll_ms;
    uint8_t  tier;          /**< tiku_trk_tier_t                           */
    uint8_t  slots_used;
    uint8_t  slots_free;    /**< of the eight the whole device shares      */
    uint8_t  index_partial; /**< the manifest arrived truncated            */
    uint32_t value_reads;   /**< reads spent this session -- the energy    */
} tiku_trk_liveness_t;

/* ---------------------------------------------------------------------- *
 * The pending cache, and the view's single door
 * ---------------------------------------------------------------------- */

#define TIKU_TRK_PENDING_LIFETIME_US 10000000LL

typedef struct tiku_trk_pending tiku_trk_pending_t;

tiku_trk_pending_t *tiku_trk_pending_new(void);
void tiku_trk_pending_free(tiku_trk_pending_t *c);

/** @brief Park an event for a node the view does not have yet.  The ref,
 *  the accumulated field mask and the name are kept -- not the payload, so
 *  replay re-reads current state rather than replaying a stale one. */
void tiku_trk_pending_add(tiku_trk_pending_t *c, const tiku_node_event_t *ev,
                          int64_t now_us);

/** @brief Replay everything parked for @p ref, in seq order. */
int tiku_trk_pending_replay(tiku_trk_pending_t *c,
                            const tiku_trk_ref_t *ref,
                            tiku_node_event_t *out, int max);

/** @brief Expire parked entries.  Called on every accumulator flush as well
 *  as on pose creation, so an entry for a node that never arrives cannot
 *  leak until some unrelated pose happens to be created. */
void tiku_trk_pending_sweep(tiku_trk_pending_t *c, int64_t now_us);

struct tiku_trk_poseview;

/**
 * @brief The single entry point for every notification, from either store.
 *
 * @return Non-zero when the event was consumed; zero means "park it", which
 *         is the caller's cue to hand it to the pending cache.
 */
int tiku_trk_poseview_notify(struct tiku_trk_poseview *v,
                             const tiku_node_event_t *ev);

#endif /* TIKU_TRK_EVENT_H_ */
