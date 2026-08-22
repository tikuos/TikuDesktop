/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   Decision 1: the per-node UI state store. The AttributeStreamNode seam ported to C — one vtable, three implementations (xattr, per-device sidecar, memory), two reserved node keys reproducing Tracker's three-slot split, the in-memory and the naturally-padded on-disk pose-info forms, the scored device-identity match with the uid veto, the sidecar file format with its gate-last torn-write discipline, and the explicit-only export/import/publish path. */

/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state.h - where per-node UI state lives, for every kind of node.
 *
 * Tracker already abstracts where attributes come from -- a file node and a
 * memory node behind one stream interface -- so that abstraction is the
 * seam, not a second one invented beside it.  Pose positions, view state,
 * column sets and window frames are read and written ONCE against
 * tiku_store_t; three implementations sit behind it: xattrs on a local
 * file, a host-side sidecar for a device, and memory when neither can
 * persist.
 *
 * A device stores nothing.  Tracker already relocates state to the viewer's
 * storage whenever the object cannot hold it (root and Trash pose info go to
 * the Desktop directory); a device namespace is permanently in that
 * condition, so the fallback becomes the default rather than the exception.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STATE_H_
#define TIKU_STATE_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tiku_store tiku_store_t;

#define TIKU_TRK_STORE_WRITABLE   0x01u /**< writes can succeed            */
#define TIKU_TRK_STORE_PERSISTENT 0x02u /**< survives app exit             */
#define TIKU_TRK_STORE_SIDECAR    0x04u /**< keyed by node_key, not object */
#define TIKU_TRK_STORE_FOREIGN_RD 0x08u /**< honours the foreign-endian
                                         *   twin on read                  */

typedef struct tiku_trk_store_ops {
    /** @brief Read one named record.  @p foreign_name is the byte-swapped
     *  twin tried on a miss; NULL where the store has one byte order. */
    long (*read)(tiku_store_t *st, const char *node_key,
                 const char *name, const char *foreign_name,
                 uint32_t type, void *buf, size_t len);
    long (*write)(tiku_store_t *st, const char *node_key,
                  const char *name, const char *foreign_name,
                  uint32_t type, const void *buf, size_t len);
    /** @brief Size of a record, or -1 when absent. */
    long (*contains)(tiku_store_t *st, const char *node_key,
                     const char *name, uint32_t type);
    int  (*remove)(tiku_store_t *st, const char *node_key,
                   const char *name);
    /** @brief Make everything durable.  Called at the dirty-gate moments
     *  only -- never per drag frame. */
    int  (*flush)(tiku_store_t *st);
    void (*close)(tiku_store_t *st);
} tiku_trk_store_ops_t;

struct tiku_store {
    const tiku_trk_store_ops_t *ops;
    uint32_t flags;
    void    *impl;
};

/* Reserved node keys.  Tracker keeps three non-colliding slots (a folder's
 * own state, the Disks/root slot, the Desktop slot) by redirecting the
 * latter two onto the Desktop directory node; the sidecar reproduces them
 * as key prefixes instead.  The xattr store ignores the key: the file IS
 * the node. */
#define TIKU_KEY_DEV_ROOT  "\x01root"
#define TIKU_KEY_DESKTOP   "\x01desk"

/** @brief Local store: real extended attributes, full fidelity. */
tiku_store_t *tiku_store_xattr_open(void);

/** @brief Device store: one sidecar file per device, keyed by die uid. */
tiku_store_t *tiku_store_sidecar_open(const char *devid);

/** @brief Nothing can persist here (portable mode, read-only home): the
 *  layout is live for the session and dropped on close, with no error.
 *  A third implementation, so no caller carries a special case. */
tiku_store_t *tiku_store_memory_open(void);

/* The two wrappers every view and window actually call. */
long tiku_state_read(tiku_store_t *st, const char *node_key,
                         const char *name, void *buf, size_t len);
long tiku_state_write(tiku_store_t *st, const char *node_key,
                          const char *name, const void *buf, size_t len);

/** @brief Non-zero when this store may hold state at all; the caller then
 *  skips the write silently, exactly as Tracker does on a read-only
 *  volume, rather than raising an error the user cannot act on. */
static inline int
tiku_trk_state_can_persist(const tiku_store_t *st)
{
    return st != NULL
        && (st->flags & TIKU_TRK_STORE_PERSISTENT) != 0
        && (st->flags & TIKU_TRK_STORE_WRITABLE) != 0;
}

/* ---------------------------------------------------------------------- *
 * Pose info -- the per-entry icon position
 * ---------------------------------------------------------------------- */

#define TIKU_TRK_SANE_POSE 50000.0f  /**< the sanity bound, unchanged      */
#define TIKU_TRK_PARENT_INVALID ((uint64_t)0)

/** @brief In-memory form, shared by both stores. */
typedef struct {
    uint8_t  invisible;
    uint8_t  pad[3];
    uint64_t inited_parent; /**< local: the parent's inode.  device: the
                             *   FNV-1a of the parent's path.  A record
                             *   whose parent no longer matches is ignored
                             *   and the icon auto-placed -- the same
                             *   validation either way. */
    float    x, y;
} tiku_trk_poseinfo_t;

/**
 * @brief On-disk form for the LOCAL store only.
 *
 * Byte-identical to the original: bool, seven bytes of padding, a 64-bit
 * inode, two floats.  Do NOT pack it -- a packed struct misreads positions
 * authored by real Tracker on a shared volume, and the failure is silent
 * (the coordinates trip the sanity bound and the icons quietly auto-place).
 */
typedef struct {
    uint8_t invisible;
    uint8_t pad[7];
    int64_t inited_directory;
    float   x, y;
} tiku_trk_poseinfo_disk_t;

int tiku_trk_pose_read(tiku_store_t *st, const char *node_key,
                       const char *parent_key, tiku_trk_poseinfo_t *out);
int tiku_trk_pose_write(tiku_store_t *st, const char *node_key,
                        const char *parent_key,
                        const tiku_trk_poseinfo_t *in);

/** @brief FNV-1a 32 over a path; never returns 0, which is the sentinel. */
uint32_t tiku_trk_path_hash(const char *path);

/* ---------------------------------------------------------------------- *
 * Device identity -- a scored match, so a rename cannot steal a layout
 * ---------------------------------------------------------------------- */

typedef struct {
    char uid[64];       /**< /sys/device/uid; "" when the part has none   */
    char id[32];        /**< /sys/device/id                               */
    char mcu[32];       /**< /sys/device/mcu                              */
    char name[40];      /**< /sys/device/name -- durable, user-editable   */
    char endpoint[128]; /**< an ADDRESS, never the identity               */
} tiku_trk_ident_t;

#define TIKU_TRK_IDW_UID   8
#define TIKU_TRK_IDW_ID    4
#define TIKU_TRK_IDW_MCU   2
#define TIKU_TRK_IDW_NAME  1
#define TIKU_TRK_IDW_ACCEPT 7   /**< uid alone binds; id+mcu+name also    */
#define TIKU_TRK_IDW_VETO (-1)  /**< both uids present and different      */

/** @brief Probe the four identity nodes.  @p ns is a device mirror. */
int tiku_trk_ident_probe(void *ns, tiku_trk_ident_t *out);

/**
 * @brief Score a probe against a stored binding.
 *
 * A uid mismatch is an absolute veto whatever else matches -- that is the
 * one thing a die identifier buys, and without it a renamed board steals
 * another board's arrangement.
 */
int tiku_ident_score(const tiku_trk_ident_t *probed,
                         const tiku_trk_ident_t *stored);

/**
 * @brief Derive the sidecar key: "uid-<hex>", or "prov-<hex>" for a part
 *        with no unique id.
 *
 * @param provisional_out  Set when the binding is not uid-backed.  Two
 *                         identical boards with the same name genuinely are
 *                         ambiguous, and the UI must say so rather than
 *                         picking the best match and hiding the doubt.
 */
int tiku_ident_key(const tiku_trk_ident_t *id, char *buf, size_t max,
                       int *provisional_out);

/* ---------------------------------------------------------------------- *
 * The sidecar file
 * ---------------------------------------------------------------------- */

#define TIKU_TRK_SIDECAR_MAGIC   0x54524B53u /**< 'TRKS'                  */
#define TIKU_TRK_SIDECAR_GATE    0x54524B52u /**< 'TRKR', written last    */
#define TIKU_TRK_SIDECAR_VERSION 1u
#define TIKU_TRK_SIDECAR_MAX_REC 10000u
#define TIKU_TRK_PUBLISH_MAX     4096u       /**< refuse above, never
                                              *   truncate               */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t record_count;
    uint32_t bytes_used;
    uint8_t  provisional;
    uint8_t  reserved[3];
    char     dev_uid[64];
    char     dev_id[32];
    char     dev_mcu[32];
    char     dev_name[40];   /**< last seen, for display only            */
    uint64_t last_seen;
    uint32_t manifest_hash;  /**< lets the host notice the shape changed */
    uint32_t persist_primed; /**< re-primed cells mean a wipe or a layout
                              *   change: re-validate, warn, do not
                              *   silently apply stale positions         */
} tiku_trk_sidecar_hdr_t;

typedef struct {
    uint32_t gate;      /**< written last, so a torn record reads as
                         *   "no saved state" and never as garbage       */
    uint16_t key_len;
    uint16_t name_len;
    uint32_t type;
    uint32_t data_len;
    uint32_t crc32;     /**< over key + name + data                      */
} tiku_trk_sidecar_rec_t;

/** @brief Fold duplicate records away: write a temp, sync, rename. */
int tiku_trk_sidecar_compact(tiku_store_t *st);

/* Cross-host transfer is explicit, always.  Never on window close, never a
 * write per drag: a device's store is carved, shared with user data, and on
 * some parts a chatty writer amplifies into flush traffic. */
int tiku_trk_state_export(const char *devid, const char *out_path);
int tiku_trk_state_import(const char *devid, const char *in_path);

/** @brief Whether the manifest shows a writable dynamic store to publish
 *  into; the menu items are disabled when it does not. */
int tiku_trk_state_can_publish(void *ns);
int tiku_trk_state_publish(void *ns, const char *devid);
int tiku_trk_state_adopt(void *ns, const char *devid);

#endif /* TIKU_STATE_H_ */
