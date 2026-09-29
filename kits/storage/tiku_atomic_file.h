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

/* Write into a private temporary file beside path, close it once, then
 * rename it over path. A producer failure leaves the destination intact.
 * Existing destinations must be regular files (not symbolic links).
 * Call from a worker: producing, syncing and replacing can block.
 * Returns 0 on success, -1 on failure. */
int tiku_atomic_file(const char *path, int (*produce)(FILE *, void *),
                     void *ctx);
#endif
