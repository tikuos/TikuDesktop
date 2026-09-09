/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * slowstatvfs.c - a statvfs that dawdles, for the tests.
 *
 * Preloaded into the desk, every statvfs sleeps TRK_SLOW_STATVFS_MS
 * before answering as the real one does -- the shape of a mount gone
 * away on the network, without one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <time.h>

int
statvfs(const char *path, struct statvfs *buf)
{
    static int (*real)(const char *, struct statvfs *);
    const char *ms = getenv("TRK_SLOW_STATVFS_MS");

    if (real == NULL) {
        real = dlsym(RTLD_NEXT, "statvfs");
    }
    if (ms != NULL && atoi(ms) > 0) {
        struct timespec t;

        t.tv_sec = atoi(ms) / 1000;
        t.tv_nsec = (atoi(ms) % 1000) * 1000000L;
        (void)nanosleep(&t, NULL);
    }
    return real(path, buf);
}
