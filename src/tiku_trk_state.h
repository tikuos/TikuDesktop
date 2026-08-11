/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_state.h - where a node's UI state lives.
 *
 * Tracker's AttributeStreamNode seam, ported: every caller that reads or
 * writes pose info, column sets or view state goes through this one interface,
 * so "the object holds its own state" (local files, xattr) and "the viewer
 * holds it for the object" (device nodes, sidecar) are one code path.  The
 * fallback is Tracker's own -- it already relocates state for volumes that
 * cannot carry attributes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_STATE_H_
#define TIKU_TRK_STATE_H_

#include <stddef.h>
#include <stdint.h>

/* Attribute names, kept as Tracker spells them so the meaning is greppable
 * across both trees.  Local files really do carry these (prefixed for the
 * xattr namespace); the sidecar stores them under the same names. */
#define TIKU_TRK_ATTR_POSE_INFO   "_trk/pinfo"
#define TIKU_TRK_ATTR_COLUMNS     "_trk/columns"
#define TIKU_TRK_ATTR_VIEW_STATE  "_trk/viewstate"

/* Reserved node keys reproducing Tracker's three-slot split: state for
 * things that cannot hold it goes to a well-known slot rather than to the
 * object (PoseView's FSGetDeskDir branch does exactly this for root and
 * Trash). */
#define TIKU_TRK_KEY_DEV_ROOT  "\x01root"
#define TIKU_TRK_KEY_DESKTOP   "\x01desk"

typedef struct tiku_trk_store tiku_trk_store_t;

/**
 * @brief Open the store for local files (extended attributes).
 *
 * State lives on the file itself, which is what makes a folder remember when
 * it is moved or copied to another host.
 */
tiku_trk_store_t *tiku_trk_store_xattr_open(void);

/**
 * @brief Open the per-device sidecar store.
 *
 * One file per device under $XDG_DATA_HOME/tracker/devices/<devid>.state, so
 * discarding a device touches exactly one file and cannot disturb where the
 * user put every other device.
 *
 * @param devid  From tiku_trk_ident_key().
 */
tiku_trk_store_t *tiku_trk_store_sidecar_open(const char *devid);

/** @brief Open a volatile store (tests, and nodes whose state must not persist). */
tiku_trk_store_t *tiku_trk_store_memory_open(void);

void tiku_trk_store_free(tiku_trk_store_t *s);

/**
 * @brief Read one attribute of one node.
 *
 * @param node  Absolute path, or a TIKU_TRK_KEY_* slot.
 * @return Bytes read, or -1 when absent.
 */
int tiku_trk_state_read(tiku_trk_store_t *s, const char *node,
                        const char *attr, void *buf, size_t max);

/** @brief Write one attribute.  @return 0, or -1 when the store refused. */
int tiku_trk_state_write(tiku_trk_store_t *s, const char *node,
                         const char *attr, const void *buf, size_t len);

/**
 * @brief Whether this store can persist at all.
 *
 * A read-only volume answers 0, and callers must degrade rather than fail:
 * Tracker keeps working on a CD, it just forgets.
 */
int tiku_trk_store_writable(const tiku_trk_store_t *s);

/**
 * @brief Persist pending changes.
 *
 * The sidecar batches: state is touched on directory switch, window close and
 * link loss, never once per drag.
 */
int tiku_trk_store_flush(tiku_trk_store_t *s);

/**
 * @brief Forget one node's state.
 *
 * ONLY for a user-invoked "forget arrangements this device no longer has".
 * INVARIANT: nothing on the pose-teardown path may call this -- a device
 * unplugged or a directory switched must leave its arrangement intact, or
 * reconnecting loses the user's desktop.
 */
int tiku_trk_state_forget(tiku_trk_store_t *s, const char *node);

/** @brief Records held (tests and the store's own reporting). */
int tiku_trk_store_count(const tiku_trk_store_t *s);

#endif /* TIKU_TRK_STATE_H_ */
