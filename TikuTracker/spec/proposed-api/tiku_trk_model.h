/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   The Model, its kind and state enums, and the one refusal predicate (decisions 2, 4 and 5 meet here). Kind says what shape a thing is; a separate backend byte says who answers the read; a new state axis says how true the row still is, which is what a stallable link forces. Refusal is a typed verdict with two macros that encode the predict-to-disable / attempt-to-be-sure asymmetry. Also holds ref identity and the path-interning table that stands in for inodes. */

/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.h - what a Tracker row is, whichever store answers for it.
 *
 * One Model serves a local file and a device namespace node.  Kind says what
 * SHAPE a thing is -- Tracker asks a kind exactly four questions (icon, sort
 * bucket, which window opens it, may it be renamed) -- and backend says WHO
 * answers the read.  The two are orthogonal: "local vs device" is not a kind.
 * State says how much of what is on screen is still true, which is the only
 * new axis a device forces, because a link can stall.
 *
 * A refusal is a typed verdict rather than a bool.  The device tells us why
 * it said no, and the alert has to say so; a bool throws that away.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MODEL_H_
#define TIKU_MODEL_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_trk_type.h"

#define TIKU_PATH_MAX   512
#define TIKU_NAME_MAX    96
#define TIKU_TRK_VALUE_MAX  512
#define TIKU_TRK_ERR_MAX    160

struct tiku_backend;

/* ---------------------------------------------------------------------- *
 * Identity
 * ---------------------------------------------------------------------- */

/** @brief Store id.  Local volumes carry their real st_dev. */
typedef int32_t tiku_trk_dev_t;

#define TIKU_TRK_DEV_NONE   ((tiku_trk_dev_t)-1)
/** @brief Device volumes are numbered from here so they never alias st_dev,
 *  which is what makes Tracker's same-store drag test (copy vs move) right
 *  with no new rule. */
#define TIKU_TRK_DEV_BASE   ((tiku_trk_dev_t)0x40000000)

/**
 * @brief Tracker's node_ref: which store, which object.
 *
 * TikuOS has no inodes, so a device node's @c node is an interned absolute
 * path (tiku_trk_ino_intern()).  Interned, not hashed: the tree is static and
 * node pointers are stable, so path IS identity and interning costs no
 * collisions.
 */
typedef struct {
    tiku_trk_dev_t dev;
    uint64_t       node;
} tiku_trk_ref_t;

/** @brief Intern an absolute path to a stable id for @p dev. */
uint64_t tiku_trk_ino_intern(tiku_trk_dev_t dev, const char *abs_path);

/** @brief Recover the path an id was interned from.  0 on success. */
int tiku_trk_ino_path(tiku_trk_dev_t dev, uint64_t node, char *out,
                      size_t max);

/** @brief Release one id (call from DeletePose, or the table leaks). */
void tiku_trk_ino_forget(tiku_trk_dev_t dev, uint64_t node);

/** @brief Release every id of a store (call on unmount). */
void tiku_trk_ino_drop_dev(tiku_trk_dev_t dev);

static inline int
tiku_trk_ref_eq(const tiku_trk_ref_t *a, const tiku_trk_ref_t *b)
{
    return a->dev == b->dev && a->node == b->node;
}

/* ---------------------------------------------------------------------- *
 * Kind and state
 * ---------------------------------------------------------------------- */

/**
 * @brief What shape a thing is.
 *
 * Exactly one kind is new: TIKU_TRK_K_DEVICE_NODE, a value-bearing leaf.
 * Device directories stay TIKU_TRK_K_DIR and a device's root stays
 * TIKU_TRK_K_VOLUME, so container-ness, sort buckets and window choice work
 * with no new cases.  /sys, /dev and /proc are TIKU_TRK_K_VIRTUAL_DIR, which
 * is what makes the read-only tint, the greyed New folder and
 * drag-out-creates-a-link fall out of code Tracker already has.
 */
typedef enum {
    TIKU_TRK_K_PLAIN = 0,
    TIKU_TRK_K_DIR,
    TIKU_TRK_K_SYMLINK,
    TIKU_TRK_K_VOLUME,          /**< a mount root / a device's root       */
    TIKU_TRK_K_ROOT,            /**< the "Disks" pseudo-directory         */
    TIKU_TRK_K_TRASH,
    TIKU_TRK_K_DESKTOP,
    TIKU_TRK_K_QUERY,
    TIKU_TRK_K_QUERY_TEMPLATE,
    TIKU_TRK_K_VIRTUAL_DIR,     /**< /sys, /dev, /proc on a device        */
    TIKU_TRK_K_DEVICE_NODE,     /**< value-bearing leaf                   */
    TIKU_TRK_K_UNKNOWN
} tiku_kind_t;

/**
 * @brief How true the row is right now.
 *
 * Tracker has no analogue of STALE or ZOMBIE-with-retry because a local
 * filesystem does not stop answering.  A serial link does, so the view needs
 * a way to draw "this was true a moment ago" without lying.
 */
typedef enum {
    TIKU_TRK_ST_UNRESOLVED = 0, /**< listed, not yet read                 */
    TIKU_TRK_ST_READY,          /**< current                              */
    TIKU_TRK_ST_STALE,          /**< link stalled; drawn under a veil     */
    TIKU_TRK_ST_ZOMBIE,         /**< transient read failure, retry armed  */
    TIKU_TRK_ST_GONE,           /**< store vanished or entry removed      */
    TIKU_TRK_ST_ERROR           /**< permanent failure; reason in err     */
} tiku_trk_state_t;

/** @brief Which store answers.  Never a kind. */
typedef enum {
    TIKU_TRK_BE_POSIX = 0,
    TIKU_TRK_BE_TIKU
} tiku_trk_backend_kind_t;

/* Model flags. */
#define TIKU_TRK_MF_NO_SIZE     0x0001u /**< size is undefined; render "-"  */
#define TIKU_TRK_MF_SECRET      0x0002u /**< DF_SECRET: never render value  */
#define TIKU_TRK_MF_TRUNCATED   0x0004u /**< came from the ls-walked tail   */
#define TIKU_TRK_MF_INVISIBLE   0x0008u /**< PoseInfo::fInvisible           */
#define TIKU_TRK_MF_BROKEN_LINK 0x0010u
#define TIKU_TRK_MF_POSE_VALID  0x0020u /**< pose_x/pose_y were restored    */

/* ---------------------------------------------------------------------- *
 * Capabilities and refusal
 * ---------------------------------------------------------------------- */

/** @brief Device capability bits; mirrors tiku_vfs_cap_t exactly. */
#define TIKU_TRK_DCAP_NONE  0x00u
#define TIKU_TRK_DCAP_HW    0x01u
#define TIKU_TRK_DCAP_SYS   0x02u
#define TIKU_TRK_DCAP_FS    0x04u
#define TIKU_TRK_DCAP_NET   0x08u
#define TIKU_TRK_DCAP_ALL   0xFFu

/** @brief The operations enablement and refusal are asked about. */
typedef enum {
    TIKU_TRK_ACT_OPEN = 0,
    TIKU_TRK_ACT_WRITE,          /**< edit a value, paste one, drop one    */
    TIKU_TRK_ACT_RENAME,
    TIKU_TRK_ACT_CREATE,         /**< New folder, Paste, drop-in           */
    TIKU_TRK_ACT_DELETE,
    TIKU_TRK_ACT_TRASH,
    TIKU_TRK_ACT_MOVE_OUT,       /**< Cut / Move to, source side           */
    TIKU_TRK_ACT_LINK,
    TIKU_TRK_ACT_SAVE_VIEWSTATE, /**< write view state beside the object   */
    TIKU_TRK_ACT_COUNT
} tiku_trk_action_t;

typedef enum {
    TIKU_TRK_ALLOW = 0,
    TIKU_TRK_DENY_STRUCTURAL, /**< no such operation exists on this object */
    TIKU_TRK_DENY_READONLY,   /**< writable object, read-only container    */
    TIKU_TRK_DENY_KIND,       /**< root / Trash / Desktop / query / virtual */
    TIKU_TRK_DENY_CAP,        /**< device policy: channel lacks the cap    */
    TIKU_TRK_DENY_UNKNOWN     /**< metadata absent (truncated manifest)    */
} tiku_trk_verdict_t;

/** @brief Grey the menu item -- but never for UNKNOWN, or the ls-walked
 *  tail of a big namespace silently becomes unusable. */
#define TIKU_TRK_DENIED(v)                                                  \
    ((v) != TIKU_TRK_ALLOW && (v) != TIKU_TRK_DENY_UNKNOWN)

/** @brief May still be tried against the device: only the policy axis can
 *  go stale, so prediction is allowed to be wrong only permissively. */
#define TIKU_TRK_ATTEMPTABLE(v)                                             \
    ((v) == TIKU_TRK_ALLOW || (v) == TIKU_TRK_DENY_UNKNOWN                  \
                           || (v) == TIKU_TRK_DENY_CAP)

typedef struct {
    tiku_trk_verdict_t verdict;
    uint8_t            missing_cap;   /**< TIKU_TRK_DCAP_* not held; 0 n/a */
    const char        *alert;         /**< Tracker-shaped sentence         */
    char               device_text[TIKU_TRK_ERR_MAX]; /**< verbatim, or "" */
} tiku_trk_permit_t;

/* ---------------------------------------------------------------------- *
 * The Model
 * ---------------------------------------------------------------------- */

typedef struct tiku_model {
    tiku_trk_ref_t  ref;
    tiku_trk_ref_t  parent;
    char            path[TIKU_PATH_MAX];
    char            name[TIKU_NAME_MAX];

    uint8_t         kind;        /**< tiku_kind_t                     */
    uint8_t         state;       /**< tiku_trk_state_t                    */
    uint8_t         backend_kind;/**< tiku_trk_backend_kind_t             */
    uint8_t         icon_from;   /**< tiku_trk_icon_src_t                 */
    uint32_t        flags;       /**< TIKU_TRK_MF_*                       */

    /* One type field, one namespace.  A string starting "tiku-" is
     * answered by the node-type table; anything else by the MIME database.
     * That strncmp is the only place two type systems are visible. */
    char            type[TIKU_TRK_TYPE_LEN];
    tiku_facts_t facts;      /**< device nodes only; from the manifest */

    uint64_t        size;
    int64_t         mtime_us;

    /* Access, as the two backends express it. */
    unsigned        perm;        /**< TIKU_P_READ | TIKU_P_WRITE  */
    uint8_t         req_cap;     /**< device: capability a writer needs   */
    uint8_t         cap_known;   /**< 0 when the manifest was truncated   */

    /* The rendered value of a device node -- the device's "content". */
    char            value[TIKU_TRK_VALUE_MAX];
    uint8_t         value_valid;
    uint32_t        value_hash;  /**< FNV-1a; the change gate             */
    uint32_t        generation;

    /* Spatial state, restored BEFORE the pose is first drawn. */
    float           pose_x, pose_y;

    /* Predicted verdicts, invalidated wholesale when the channel's
     * capability generation moves. */
    uint8_t         cached[TIKU_TRK_ACT_COUNT];
    unsigned        permit_gen;

    struct tiku_backend *backend;
    struct tiku_model   *link_to;  /**< resolved symlink target       */
    char            err[TIKU_TRK_ERR_MAX];
    int             refs;
} tiku_model_t;

#define TIKU_P_READ   0x1u
#define TIKU_P_WRITE  0x2u

/** @brief Allocate a model owned by @p b, for @p path.  NULL on failure. */
tiku_model_t *tiku_trk_model_new(struct tiku_backend *b,
                                     const char *path);

tiku_model_t *tiku_trk_model_acquire(tiku_model_t *m);

/** @brief Drop a reference; frees at zero. */
void tiku_trk_model_release(tiku_model_t *m);

/* The four questions a kind exists to answer, plus the one a device adds. */
int tiku_model_is_container(const tiku_model_t *m);
int tiku_trk_model_is_directory(const tiku_model_t *m);
int tiku_trk_model_is_file(const tiku_model_t *m); /**< true for a node */
int tiku_trk_model_is_device(const tiku_model_t *m);
int tiku_trk_model_is_volume_root(const tiku_model_t *m);

/**
 * @brief Re-derive the type string from freshly loaded manifest facts.
 *
 * Runs at manifest load and manifest reload ONLY.  A value ringing does not
 * re-type a node, or a busy sensor becomes a reclassify storm.
 *
 * @return Non-zero when the type changed and the caller must re-icon.
 */
int tiku_trk_model_reclassify(tiku_model_t *m,
                              const tiku_facts_t *f);

/** @brief Note a newly rendered value.  @return non-zero if it changed. */
int tiku_trk_model_set_value(tiku_model_t *m, const char *text,
                             int truncated);

/* ---------------------------------------------------------------------- *
 * The one refusal predicate
 * ---------------------------------------------------------------------- */

/** @brief Predict a verdict for @p m.  Cheap; cached per capability gen. */
void tiku_trk_permit_of(const tiku_model_t *m, tiku_trk_action_t a,
                        tiku_trk_permit_t *out);

/** @brief As above for a destination container (New folder, Paste, drop). */
void tiku_trk_permit_of_target(const tiku_model_t *dir,
                               tiku_trk_action_t a, tiku_trk_permit_t *out);

/**
 * @brief Split a selection instead of refusing it wholesale.
 *
 * Tracker tests only the first selected item's volume, which a merged
 * local+device selection trips constantly.  We perform the permitted subset
 * and report the rest once.
 *
 * @return Count that may proceed; @p refused receives the pruned models.
 */
int tiku_trk_permit_selection(tiku_model_t *const *sel, int n,
                              tiku_trk_action_t a,
                              const tiku_model_t **refused,
                              int max_refused, int *n_refused);

/**
 * @brief Execution-time gate: alerts and returns 0 when the act must not run.
 *
 * The analogue of PoseView's CheckVolumeReadOnly(), called at exactly the
 * same points.  STRUCTURAL, READONLY and KIND abort here; CAP and UNKNOWN
 * are attempted anyway, because the device is the authority on policy.
 */
int tiku_trk_permit_check(const tiku_model_t *m, tiku_trk_action_t a);

/** @brief Ten-character access string for the Permissions column: a Unix
 *  mode for local files, kind/rw/gate/cap for a device node. */
void tiku_trk_access_string(const tiku_model_t *m, char out[11]);

#endif /* TIKU_MODEL_H_ */
