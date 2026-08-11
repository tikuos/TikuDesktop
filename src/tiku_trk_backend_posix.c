/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_backend_posix.c - local files, at full Tracker fidelity.
 *
 * Real permissions, real inodes, real extended attributes: everything Tracker
 * expects of a filesystem is here, so the compromises the device backend must
 * make never leak into the local experience.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    tiku_trk_store_t *store;
} posix_impl_t;

/** @brief Fill a Model from a stat buffer. */
static void
from_stat(tiku_trk_model_t *m, const char *path, const struct stat *st,
          int is_link)
{
    const char *slash = strrchr(path, '/');

    memset(m, 0, sizeof *m);
    snprintf(m->path, sizeof m->path, "%s", path);
    snprintf(m->name, sizeof m->name, "%s",
             (slash != NULL && slash[1] != '\0') ? slash + 1 : path);
    m->node_id = (uint64_t)st->st_ino;
    m->facts.size = (int64_t)st->st_size;
    m->facts.mtime = (int64_t)st->st_mtime;
    m->facts.cap_known = 1;
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "-");
    snprintf(m->facts.meta, sizeof m->facts.meta, "-");

    if (is_link) {
        m->kind = TIKU_TRK_KIND_SYMLINK;
    } else if (S_ISDIR(st->st_mode)) {
        m->kind = TIKU_TRK_KIND_DIRECTORY;
    } else {
        m->kind = TIKU_TRK_KIND_FILE;
    }
    /* Permission as the current user sees it: what the UI must reflect is
     * whether THIS user can act, not the raw mode bits. */
    if (access(path, R_OK) == 0) { m->facts.perm |= TIKU_TRK_P_READ; }
    if (access(path, W_OK) == 0) { m->facts.perm |= TIKU_TRK_P_WRITE; }
    if (access(path, X_OK) == 0) { m->facts.perm |= TIKU_TRK_P_EXEC; }
    snprintf(m->type, sizeof m->type, "%s",
             S_ISDIR(st->st_mode) ? "application/x-vnd.Be-directory"
                                  : "application/octet-stream");
}

static int
px_stat(tiku_trk_backend_t *b, const char *path, tiku_trk_model_t *out)
{
    struct stat st, lst;

    if (path == NULL || out == NULL) {
        return -1;
    }
    if (lstat(path, &lst) != 0) {
        return -1;
    }
    if (S_ISLNK(lst.st_mode)) {
        /* Report the link, but size/kind of the target when it resolves --
         * Tracker shows a broken link differently from a live one. */
        if (stat(path, &st) == 0) {
            from_stat(out, path, &st, 1);
        } else {
            /* The target has gone.  The LINK has not, so it keeps its row
             * and carries the fact -- a broken link is a thing to see, not
             * an entry to remove. */
            from_stat(out, path, &lst, 1);
            out->facts.link_broken = 1;
        }
    } else {
        from_stat(out, path, &lst, 0);
    }
    out->backend = b;
    return 0;
}

static int
px_list(tiku_trk_backend_t *b, const char *path, tiku_trk_entry_fn fn,
        void *ctx)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_TRK_PATH_MAX];
        tiku_trk_model_t m;
        size_t pl = strlen(path);

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (pl + strlen(e->d_name) + 2u > sizeof child) {
            continue;
        }
        memcpy(child, path, pl);
        if (pl > 0u && path[pl - 1u] != '/') { child[pl++] = '/'; }
        memcpy(child + pl, e->d_name, strlen(e->d_name) + 1u);

        if (px_stat(b, child, &m) != 0) {
            /* Unreadable right now -- busy, mid-write, a transient error.
             * It is still an ENTRY, so it is reported with what is known and
             * marked unreadable; dropping it makes a file disappear from a
             * listing because it was briefly locked, and reappear later as
             * though it had just been created. */
            memset(&m, 0, sizeof m);
            (void)snprintf(m.path, sizeof m.path, "%s", child);
            (void)snprintf(m.name, sizeof m.name, "%s", e->d_name);
            (void)snprintf(m.facts.meta, sizeof m.facts.meta, "-");
            (void)snprintf(m.facts.req_cap, sizeof m.facts.req_cap, "?");
            m.kind = TIKU_TRK_KIND_UNKNOWN;
            m.facts.cap_known = 0;
            m.backend = b;
        }
        n++;
        if (fn != NULL && fn(&m, ctx) != 0) {
            break;
        }
    }
    (void)closedir(d);
    return n;
}

static int
px_read(tiku_trk_backend_t *b, const char *path, void *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    (void)b;
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, max, f);
    (void)fclose(f);
    return (int)n;
}

static int
px_write(tiku_trk_backend_t *b, const char *path, const void *buf, size_t len,
         char *err, size_t errmax)
{
    FILE *f;

    (void)b;
    f = fopen(path, "wb");
    if (f == NULL) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "cannot write '%s' (%s)", path,
                     strerror(errno));
        }
        return -1;
    }
    if (len > 0u && fwrite(buf, 1, len, f) != len) {
        (void)fclose(f);
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "short write to '%s'", path);
        }
        return -1;
    }
    return (fclose(f) == 0) ? 0 : -1;
}

static int
px_pump(tiku_trk_backend_t *b, int timeout_ms)
{
    /* inotify lands with the liveness layer; local files are quiet until
     * then, which is honest rather than a pretend zero. */
    (void)b;
    (void)timeout_ms;
    return 0;
}

static tiku_trk_store_t *
px_store(tiku_trk_backend_t *b)
{
    posix_impl_t *impl = b->impl;

    if (impl->store == NULL) {
        impl->store = tiku_trk_store_xattr_open();
    }
    return impl->store;
}

static void
px_close(tiku_trk_backend_t *b)
{
    posix_impl_t *impl = b->impl;

    if (impl != NULL) {
        tiku_trk_store_free(impl->store);
        free(impl);
    }
    free(b);
}

static const tiku_trk_backend_ops_t POSIX_OPS = {
    /* A local filesystem has no change ring to ask, so it says so and
     * the view polls -- which is what NULL here means. */
    /* No change ring, so no identity to resolve either. */
    "posix", px_stat, px_list, px_read, px_write, px_pump, NULL, NULL,
    px_store, px_close
};

tiku_trk_backend_t *
tiku_trk_backend_posix_open(void)
{
    tiku_trk_backend_t *b = calloc(1, sizeof *b);
    posix_impl_t *impl = calloc(1, sizeof *impl);

    if (b == NULL || impl == NULL) {
        free(b);
        free(impl);
        return NULL;
    }
    b->ops = &POSIX_OPS;
    b->impl = impl;
    return b;
}
