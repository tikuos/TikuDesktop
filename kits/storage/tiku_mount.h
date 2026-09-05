/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mount.h - one namespace over several backends.
 *
 * A namespace over several backends, itself a backend: each is attached
 * at a path prefix and every operation goes to the longest prefix that
 * claims it, so a view asking its questions of tiku_backend_ops_t never
 * knows there is more than one store beneath.  Paths are rewritten both
 * ways, a path that is only the ancestor of a mount is listed as a
 * directory of the mounts beneath it, and ids leaving here carry the
 * mount in their top byte so two stores' inode 12345 stay apart.  The
 * reasoning, with the layout, is in doc/namespace.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MOUNT_H_
#define TIKU_MOUNT_H_

#include <stddef.h>

#include "tiku_model.h"

/**
 * @brief How many backends one namespace may hold.
 *
 * The local files and seven devices.  Small on purpose: the table is
 * searched per operation, and a shell showing eight devices at once has
 * a different problem from this one.
 */
#define TIKU_MOUNT_MAX 8

/** @brief An empty namespace.  NULL if there is no room for one. */
tiku_backend_t *tiku_mount_open(void);

/**
 * @brief Attach @p b at @p prefix.
 *
 * @param prefix Absolute, no trailing slash except for the root "/".
 * @param owned  Nonzero to close @p b when the namespace closes or the
 *               mount is removed.  A caller that keeps the backend for
 *               its own use passes zero and keeps the obligation.
 * @return 0, or -1 if the prefix is bad, taken, or the table is full.
 */
int tiku_mount_add(tiku_backend_t *router, const char *prefix,
                   tiku_backend_t *b, int owned);

/**
 * @brief Detach whatever is at @p prefix.
 *
 * The entry is emptied rather than moved down, so the mount indices that
 * are baked into every id already handed out do not shift under them.
 *
 * @return 0, or -1 when nothing is mounted there.
 */
int tiku_mount_remove(tiku_backend_t *router, const char *prefix);

/** @brief How many mounts there are, ignoring the gaps left by removal. */
int tiku_mount_count(const tiku_backend_t *router);

/**
 * @brief The prefix and backend of slot @p i, or NULL past the end.
 *
 * Walks slots, so a caller iterating to tiku_mount_slots() sees the gaps
 * as NULL rather than being handed a stale backend.
 */
int tiku_mount_slots(const tiku_backend_t *router);
const char *tiku_mount_prefix_at(const tiku_backend_t *router, int i);
tiku_backend_t *tiku_mount_at(const tiku_backend_t *router, int i);

/** @brief Whether @p b is a namespace at all, rather than a plain backend. */
int tiku_mount_is(const tiku_backend_t *b);

/**
 * @brief The backend that really answers for @p path, router or not.
 *
 * A plain backend answers for itself and @p local comes back as @p path;
 * a namespace routes and strips.  Ask this before anything that depends
 * on the kind of store a path is on, going past the ops table to the
 * local filesystem or naming the store in a window: with a namespace in
 * the way the outer backend is "mount" for everything, and asking it
 * whether it is local files gives the wrong answer for local files.
 *
 * @return the serving backend, or NULL when nothing serves @p path.
 */
tiku_backend_t *tiku_backend_serving(tiku_backend_t *b, const char *path,
                                     char *local, size_t max);

/**
 * @brief The state store of whatever serves @p path.
 *
 * state_store() on the ops table takes no path, so a namespace cannot
 * route it and answers for its ROOT -- which writes a device node's
 * pose position into the local filesystem's attributes, under a name no
 * local file has, and leaves the per-device sidecar the device backend
 * opens unreachable.  Anything that knows WHICH node it is keeping
 * state for asks here instead.
 *
 * @return the store, or NULL when nothing serves @p path or the store
 *         that does keeps no state.
 */
struct tiku_store *tiku_backend_store_for(tiku_backend_t *b,
                                          const char *path);

/**
 * @brief Who serves @p path, and what it is called there.
 *
 * @param local Receives the path as the answering backend knows it;
 *              may be NULL when only the backend is wanted.
 * @return the backend, or NULL when the path is a synthetic ancestor or
 *         belongs to nothing at all.
 */
tiku_backend_t *tiku_mount_route(const tiku_backend_t *router,
                                 const char *path, char *local,
                                 size_t max);

#endif /* TIKU_MOUNT_H_ */
