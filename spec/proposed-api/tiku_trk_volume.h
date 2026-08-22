/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   Decision 4: a connected device is a volume — removable, non-boot, persistent, trash-less. Volume struct with its flags and the STALLED state that debounces unplug, the roster with its host-side scan and identity rebinding, the trash-less split that reuses Tracker's own partition, the device-path classifier that makes /sys and /dev virtual directories, the per-device worker with request priorities (the UI thread never touches a session), and the Desktop window and its iterator chain. */

/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_volume.h - a connected device is a volume, and nothing new.
 *
 * Specifically: a removable, non-boot, persistent, trash-less volume.  It
 * joins the same roster as a local mount, appears on the Desktop through the
 * same mounted/unmounted messages, and refuses the Trash through the path
 * Tracker already has for removable media without one -- split the selection,
 * warn once, delete what cannot be trashed.  Nothing about trash is invented.
 *
 * Two rules earn most of the behaviour.  Device volume ids are allocated in a
 * range that cannot alias a real store id, so the existing same-store test
 * gives the right copy-vs-move answer everywhere.  And every root child that
 * is not the writable store is a VIRTUAL DIRECTORY, which is a kind the
 * read-only tint, the greyed New folder and drag-out-creates-a-link already
 * understand.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VOLUME_H_
#define TIKU_VOLUME_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_backend.h"
#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_trk_task.h"

typedef enum {
    TIKU_TRK_VOL_LOCAL = 0,
    TIKU_TRK_VOL_TIKU
} tiku_trk_vol_kind_t;

/**
 * @brief Where a volume is in its life.
 *
 * STALLED has no Tracker analogue and is required rather than decorative: a
 * serial link glitches, and unmount's consequences (close the window, drop
 * the poses, break the links into it) are too destructive to fire on a
 * 200 ms hiccup.
 */
typedef enum {
    TIKU_TRK_VOLST_MOUNTING = 0,
    TIKU_TRK_VOLST_READY,
    TIKU_TRK_VOLST_STALLED, /**< link open, not answering; reconnecting   */
    TIKU_TRK_VOLST_GONE
} tiku_trk_vol_state_t;

#define TIKU_TRK_VF_PERSISTENT  0x01u /**< earns a Desktop icon           */
#define TIKU_TRK_VF_READONLY    0x02u
#define TIKU_TRK_VF_HAS_TRASH   0x04u
#define TIKU_TRK_VF_HAS_ATTRS   0x08u /**< state may live on the objects  */
#define TIKU_TRK_VF_KNOWS_QUERY 0x10u
#define TIKU_TRK_VF_REMOVABLE   0x20u /**< Unmount is offered             */
#define TIKU_TRK_VF_SHARED      0x40u
#define TIKU_TRK_VF_BOOT        0x80u /**< refuses to be unmounted        */

/** @brief A device volume is exactly these three, and no others. */
#define TIKU_TRK_VF_DEVICE                                                  \
    (TIKU_TRK_VF_PERSISTENT | TIKU_TRK_VF_KNOWS_QUERY                       \
                            | TIKU_TRK_VF_REMOVABLE)

typedef struct tiku_volume {
    tiku_trk_dev_t      dev;
    uint8_t             kind;      /**< tiku_trk_vol_kind_t               */
    uint8_t             state;     /**< tiku_trk_vol_state_t              */
    uint8_t             reconnect_tries;
    uint32_t            flags;     /**< TIKU_TRK_VF_*                     */
    char                name[64];       /**< the device's own name node   */
    char                identity[64];   /**< die uid, or the fs UUID      */
    char                endpoint[256];  /**< an address, not an identity  */
    char                mount_path[256];/**< local only                   */
    uint64_t            capacity;  /**< 0 = unknown: no bar, render "-"   */
    uint64_t            free_bytes;
    const char         *fs_kind;   /**< the store's backing medium        */
    int64_t             cap_polled_us;
    tiku_backend_t *backend;
    struct tiku_trk_worker *worker;
} tiku_volume_t;

#define TIKU_TRK_ROSTER_SCAN_US    1000000LL
#define TIKU_TRK_ROSTER_STALL_US   3000000LL
#define TIKU_TRK_ROSTER_RECONNECTS 3

void tiku_trk_roster_init(tiku_trk_loop_t *loop, tiku_trk_evq_t *q);
int  tiku_trk_roster_count(void);
tiku_volume_t *tiku_trk_roster_at(int i);
tiku_volume_t *tiku_trk_roster_find(tiku_trk_dev_t dev);

/** @brief Find by identity, which is how a saved window or query rebinds
 *  after a replug on a different port.  Never match on the volume id. */
tiku_volume_t *tiku_trk_roster_find_identity(const char *identity);

tiku_volume_t *tiku_trk_roster_boot(void);

/**
 * @brief Scan for endpoints; posts mounted and unmounted events.
 *
 * "Mounted" is a HOST fact -- does the endpoint answer a prompt -- so this
 * is a roster poll and deliberately not a watch on the device namespace: a
 * device cannot tell you it was unplugged.  The scan must never open an
 * endpoint it was not told about: on some rigs opening a port resets the
 * board, and an eager scanner would reboot what it is trying to show.
 */
void tiku_trk_roster_scan(void);

tiku_trk_dev_t tiku_trk_roster_mount_serial(const char *path, int baud);
tiku_trk_dev_t tiku_trk_roster_mount_tcp(const char *host, int port);

/** @brief Disconnect; @p eject is the drag-to-Trash gesture.  Pose
 *  positions are flushed before the views are torn down. */
int tiku_trk_roster_unmount(tiku_trk_dev_t dev, int eject);

/* ---------------------------------------------------------------------- *
 * Trash
 * ---------------------------------------------------------------------- */

#define TIKU_TRK_ERR_NO_TRASH (-3001)

/** @brief The Trash directory of @p dev, or TIKU_TRK_ERR_NO_TRASH.  A
 *  device volume has none and never gets one. */
int tiku_trk_vol_trash_dir(tiku_trk_dev_t dev, char *out, size_t max);

static inline int
tiku_trk_vol_has_trash(const tiku_volume_t *v)
{
    return v != NULL && (v->flags & TIKU_TRK_VF_HAS_TRASH) != 0;
}

/**
 * @brief Tracker's own partition, reused verbatim.
 *
 * A device selection simply lands entirely in @p to_delete, at which point
 * the existing "cannot be moved to the Trash, delete instead?" alert runs.
 * Deletion then reaches only what the store can actually unlink.
 */
void tiku_trk_split_trashable(tiku_model_t *const *sel, int n,
                              tiku_model_t **to_trash, int *n_trash,
                              tiku_model_t **to_delete, int *n_delete);

/** @brief Classify a device path: the writable store is a real directory,
 *  every other root child is a virtual directory. */
tiku_kind_t tiku_trk_classify_device_path(const tiku_volume_t *v,
                                              const char *path);

/* ---------------------------------------------------------------------- *
 * The device worker -- the UI thread never touches a session
 * ---------------------------------------------------------------------- */

typedef enum {
    TIKU_TRK_REQ_LIST = 0,
    TIKU_TRK_REQ_READ,
    TIKU_TRK_REQ_WRITE,
    TIKU_TRK_REQ_CREATE,
    TIKU_TRK_REQ_UNLINK,
    TIKU_TRK_REQ_COPY_IN,
    TIKU_TRK_REQ_COPY_OUT,
    TIKU_TRK_REQ_VOLINFO,
    TIKU_TRK_REQ_REFRESH_CAPS
} tiku_trk_req_t;

/** @brief Interactive work must overtake background work, or the window
 *  feels dead for the length of a bulk transfer. */
#define TIKU_TRK_PRIO_INTERACTIVE 0
#define TIKU_TRK_PRIO_BACKGROUND  1
#define TIKU_TRK_PRIO_BULK        2

typedef struct tiku_trk_worker tiku_trk_worker_t;

/** @brief Completion, delivered on the loop thread, never on the worker. */
typedef void (*tiku_trk_done_fn)(void *ctx, int32_t token, int status,
                                 const char *err);

tiku_trk_worker_t *tiku_trk_worker_start(tiku_backend_t *b,
                                         tiku_trk_loop_t *loop);
void tiku_trk_worker_stop(tiku_trk_worker_t *w);

/**
 * @brief Queue one request.  @return its token, or -1 when the queue is
 *        full.  @p arg is the value to write or the host path to transfer.
 */
int32_t tiku_trk_worker_submit(tiku_trk_worker_t *w, tiku_trk_req_t req,
                               const char *path, const char *arg, int prio,
                               tiku_trk_done_fn done, void *ctx);

/** @brief Abandon a request if it has not started (a window closing). */
void tiku_trk_worker_cancel(tiku_trk_worker_t *w, int32_t token);

/* ---------------------------------------------------------------------- *
 * The Desktop -- a real window, because the app owns the desktop
 * ---------------------------------------------------------------------- */

struct tiku_trk_poseview;
typedef struct tiku_trk_deskwindow tiku_trk_deskwindow_t;

/** @brief Create the full-screen, undecorated, bottom-of-stack window,
 *  before the app is ready to run. */
tiku_trk_deskwindow_t *tiku_trk_deskwindow_new(void);

int tiku_trk_is_desktop(const struct tiku_trk_poseview *v);

/**
 * @brief Build the Desktop's entry chain, in order: the local Desktop
 *        directory, then one pose per roster volume, then the Trash pose
 *        last so it takes the next free slot.
 */
struct tiku_trk_entry_iter *tiku_trk_desk_iter_new(
    struct tiku_trk_poseview *target);

/**
 * @brief The state-store key for @p m: the object itself on a local
 *        volume, "<identity>:<path>" for a device node.
 *
 * The Desktop's own arrangement is NEVER inside a device sidecar, so
 * wiping one board cannot disturb where the user put every other board.
 */
int tiku_trk_state_key(const tiku_model_t *m, char *out, size_t max);

#endif /* TIKU_VOLUME_H_ */
