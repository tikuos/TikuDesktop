/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_atomic_file.c - write beside the destination, then rename over it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_atomic_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** @brief fsync() the directory holding @p path, which makes a rename in it
 *         durable.  @return 0, or -1. */
static int sync_parent(const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path) : 0;
    char *dir = malloc(n + 2);
    int fd, rc;

    if (!dir) return -1;
    if (n) memcpy(dir, path, n);
    else dir[n++] = slash ? '/' : '.';
    dir[n] = 0;
    fd = open(dir, O_RDONLY | O_DIRECTORY);
    free(dir);
    if (fd < 0) return -1;
    rc = fsync(fd);
    close(fd);
    return rc;
}

int tiku_atomic_file(const char *path, int (*produce)(FILE *, void *),
                     void *ctx)
{
    struct stat st;
    FILE *out;
    char *tmp;
    int fd, bad, exists;
    size_t n;

    if (!path || !*path || !produce) return -1;
    exists = lstat(path, &st) == 0;
    if ((exists && !S_ISREG(st.st_mode)) || (!exists && errno != ENOENT))
        return -1;
    n = strlen(path);
    tmp = malloc(n + sizeof ".tmp-XXXXXX");
    if (!tmp) return -1;
    memcpy(tmp, path, n);
    memcpy(tmp + n, ".tmp-XXXXXX", sizeof ".tmp-XXXXXX");
    fd = mkstemp(tmp);
    if (fd < 0) { free(tmp); return -1; }
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    out = fdopen(fd, "wb");
    if (!out) { close(fd); unlink(tmp); free(tmp); return -1; }
    bad = produce(out, ctx) != 0 || ferror(out);
    if (!bad) {
        /* mkstemp opens 0600: keep the old mode, or give a new file the
         * mode an ordinary create would under the caller's umask. */
        mode_t mode = st.st_mode & 0777;
        if (!exists) { mode = umask(0); umask(mode); mode = 0666 & ~mode; }
        if (fchmod(fd, mode) != 0) bad = 1;
    }
    if (!bad && fflush(out) != 0) bad = 1;
    if (!bad && fsync(fd) != 0) bad = 1;
    if (fclose(out) != 0) bad = 1;
    if (!bad && rename(tmp, path) != 0) bad = 1;
    if (bad) unlink(tmp);
    else bad = sync_parent(path) != 0;   /* the rename itself made durable */
    free(tmp);
    return bad ? -1 : 0;
}
