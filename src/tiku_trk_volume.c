/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_volume.c - reading the mount table and answering for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_volume.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/** @brief The leaf of a mount point, which is the name a volume shows. */
static void
name_of(const char *mount, char *out, size_t max)
{
    const char *slash = strrchr(mount, '/');

    if (strcmp(mount, "/") == 0) {
        snprintf(out, max, "Boot");
        return;
    }
    snprintf(out, max, "%.*s", (int)max - 1,
             (slash != NULL && slash[1] != '\0') ? slash + 1 : mount);
}

int
tiku_trk_volumes_scan(tiku_trk_volumes_t *vs)
{
    FILE *f;
    char line[1024];

    if (vs == NULL) {
        return -1;
    }
    memset(vs, 0, sizeof *vs);
    /* The kernel's own table rather than /etc/mtab: what is mounted NOW is
     * the question, and the two can disagree. */
    f = fopen("/proc/self/mounts", "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL &&
           vs->n < TIKU_TRK_VOLUME_MAX) {
        char dev[256], mount[TIKU_TRK_PATH_MAX], type[64], opts[256];
        tiku_trk_volume_t *v;
        struct stat st;
        struct statvfs sv;

        if (sscanf(line, "%255s %511s %63s %255s", dev, mount, type,
                   opts) != 4) {
            continue;
        }
        /* Only volumes a person could browse: the kernel's own
         * bookkeeping filesystems are not places files live. */
        if (strcmp(type, "proc") == 0 || strcmp(type, "sysfs") == 0 ||
            strcmp(type, "cgroup") == 0 || strcmp(type, "cgroup2") == 0 ||
            strcmp(type, "devpts") == 0 || strcmp(type, "securityfs") == 0 ||
            strcmp(type, "debugfs") == 0 || strcmp(type, "tracefs") == 0 ||
            strcmp(type, "bpf") == 0 || strcmp(type, "pstore") == 0 ||
            strcmp(type, "mqueue") == 0 || strcmp(type, "hugetlbfs") == 0 ||
            strcmp(type, "configfs") == 0 || strcmp(type, "fusectl") == 0 ||
            strcmp(type, "binfmt_misc") == 0 ||
            strcmp(type, "autofs") == 0 || strcmp(type, "nsfs") == 0) {
            continue;
        }
        if (stat(mount, &st) != 0) {
            continue;
        }
        v = &vs->v[vs->n];
        memset(v, 0, sizeof *v);
        snprintf(v->mount, sizeof v->mount, "%s", mount);
        name_of(mount, v->name, sizeof v->name);
        v->dev = st.st_dev;
        /* "ro" as a whole option, not a substring: "rootcontext=" and
         * "errors=remount-ro" both contain it. */
        {
            const char *o = opts;

            while (o != NULL && *o != '\0') {
                if (strncmp(o, "ro", 2) == 0 &&
                    (o[2] == ',' || o[2] == '\0')) {
                    v->read_only = 1;
                    break;
                }
                o = strchr(o, ',');
                if (o != NULL) { o++; }
            }
        }
        v->is_boot = (strcmp(mount, "/") == 0);
        v->removable = !v->is_boot;
        if (statvfs(mount, &sv) == 0) {
            v->total = (uint64_t)sv.f_blocks * (uint64_t)sv.f_frsize;
            v->avail = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
        }
        vs->n++;
    }
    (void)fclose(f);
    return vs->n;
}

const tiku_trk_volume_t *
tiku_trk_volume_of(const tiku_trk_volumes_t *vs, const char *path)
{
    const tiku_trk_volume_t *best = NULL;
    struct stat st;
    int i;

    if (vs == NULL || path == NULL || stat(path, &st) != 0) {
        return NULL;
    }
    /* By device rather than by longest prefix: a bind mount and a symlink
     * both make the prefix lie, and the device does not. */
    for (i = 0; i < vs->n; i++) {
        if (vs->v[i].dev == st.st_dev) {
            /* The shortest mount point on that device is the volume; a
             * deeper one is a mount of the same device elsewhere. */
            if (best == NULL ||
                strlen(vs->v[i].mount) < strlen(best->mount)) {
                best = &vs->v[i];
            }
        }
    }
    return best;
}

int
tiku_trk_volume_is_root(const tiku_trk_volumes_t *vs, const char *path)
{
    int i;

    if (vs == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < vs->n; i++) {
        if (strcmp(vs->v[i].mount, path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_volume_may_unmount(const tiku_trk_volumes_t *vs, const char *path,
                            char *why, size_t max)
{
    const tiku_trk_volume_t *v;

    if (!tiku_trk_volume_is_root(vs, path)) {
        return 0;                   /* not a volume: not this gesture */
    }
    v = tiku_trk_volume_of(vs, path);
    if (v == NULL) {
        return 0;
    }
    if (v->is_boot) {
        if (why != NULL) {
            snprintf(why, max, "You cannot unmount the boot volume "
                               "\"%.40s\".", v->name);
        }
        return 0;
    }
    return 1;
}

int
tiku_trk_volume_may_write(const tiku_trk_volumes_t *vs, const char *path,
                          int write, char *why, size_t max)
{
    const tiku_trk_volume_t *v;

    if (!write) {
        return 1;                   /* reading is always allowed */
    }
    v = tiku_trk_volume_of(vs, path);
    if (v == NULL || !v->read_only) {
        return 1;
    }
    if (why != NULL) {
        /* The original's words: it names the volume, because a user with
         * several mounted needs to know which one refused. */
        snprintf(why, max, "Files cannot be moved or deleted from a "
                           "read-only volume (\"%.40s\").", v->name);
    }
    return 0;
}

tiku_trk_space_t
tiku_trk_volume_space(const tiku_trk_volume_t *v, int *used_pct)
{
    uint64_t used;
    int pct = 0;

    if (used_pct != NULL) {
        *used_pct = 0;
    }
    if (v == NULL || v->total == 0u) {
        /* A volume that will not say its size shows an empty bar rather
         * than a full one: guessing "full" would warn about every device
         * whose driver has no free-space call. */
        return TIKU_TRK_SPACE_FREE;
    }
    used = (v->total > v->avail) ? (v->total - v->avail) : 0u;
    pct = (int)((used * 100u + v->total / 2u) / v->total);
    if (pct > 100) { pct = 100; }
    if (used_pct != NULL) {
        *used_pct = pct;
    }
    /* The threshold is on what is LEFT, not on what is used: a rounded
     * used-share of 95 covers anything from 94.5 to 95.5, and the volume
     * that matters is the one with under a twentieth free. */
    if (v->avail * 100u < v->total * (uint64_t)TIKU_TRK_VOLUME_WARN_PCT) {
        return TIKU_TRK_SPACE_WARNING;
    }
    return TIKU_TRK_SPACE_USED;
}
