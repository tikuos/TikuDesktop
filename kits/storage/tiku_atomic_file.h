/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_atomic_file.h - a local file replaced whole, or left as it was.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ATOMIC_FILE_H_
#define TIKU_ATOMIC_FILE_H_
#include <stdio.h>

/**
 * @brief Replace @p path with what @p produce writes, whole.
 *
 * The content goes to a private temporary file beside @p path, which is
 * synced and renamed over it; then the directory is synced.  An existing
 * @p path keeps its mode, and a new one gets 0666 under the caller's umask.
 *
 * @note Call it from a worker: producing, syncing and replacing can block.
 *       An existing @p path must be a regular file, not a symbolic link.
 * @return 0, or -1.  When @p produce or a write fails, the destination is
 *         untouched; a failed directory sync after the rename is -1 too.
 */
int tiku_atomic_file(const char *path, int (*produce)(FILE *, void *),
                     void *ctx);
#endif
