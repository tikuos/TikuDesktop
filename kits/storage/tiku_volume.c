/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_volume.c - reading the mount table and answering for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_volume.h"

#include <pthread.h>
#include <errno.h>
#include <sys/mount.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/ucred.h>
/* Darwin spells it unmount(2) and takes the flags this tree never passes. */
#define umount(path) unmount((path), 0)
#endif

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

#ifdef __APPLE__
/*
 * Darwin keeps no /proc: the mount table is asked for, not read, and it
 * arrives already parsed -- with the read-only and local bits the Linux
 * side has to pick out of an option string, and with the block counts
 * that would otherwise cost a statvfs per row.
 */
/** @brief What a mount attempt came back with, filled by the callback. */
struct mount_answer {
    int done;
    int refused;                /* the dissenter's status, 0 for none */
};

/** @brief The arbitration service's answer, on the run loop below. */
static void
mounted(DADiskRef disk, DADissenterRef refusal, void *ctx)
{
    struct mount_answer *answer = ctx;

    (void)disk;
    answer->refused = (refusal != NULL)
                          ? (int)DADissenterGetStatus(refusal) : 0;
    answer->done = 1;
}

/**
 * @brief Mount @p devname through the arbitration service.
 *
 * Which filesystem it holds and where it should go are the system's to
 * decide here -- the same decision it makes for a disk plugged in while
 * nobody is looking -- so nothing is guessed at.  The answer comes back
 * on a run loop, which is run for a bounded five seconds rather than
 * until it arrives: a disk that will not answer must not become a shell
 * that will not answer either.
 *
 * @return 0 when it mounted, -1 with errno otherwise.
 */
static int
arbitrate_mount(const char *devname)
{
    struct mount_answer answer = { 0, 0 };
    DASessionRef session;
    DADiskRef disk;
    int spin;

    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        errno = ENOMEM;
        return -1;
    }
    disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, devname);
    if (disk == NULL) {
        CFRelease(session);
        errno = ENOENT;
        return -1;
    }
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(),
                                 kCFRunLoopDefaultMode);
    DADiskMount(disk, NULL, kDADiskMountOptionDefault, mounted, &answer);
    for (spin = 0; spin < 50 && !answer.done; spin++) {
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    }
    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(),
                                   kCFRunLoopDefaultMode);
    CFRelease(disk);
    CFRelease(session);
    if (!answer.done) {
        errno = ETIMEDOUT;
        return -1;
    }
    if (answer.refused != 0) {
        /* The service's own reasons do not map onto errno, and the one
         * that matters to a person -- it would not let me -- is the one
         * worth keeping. */
        errno = EPERM;
        return -1;
    }
    return 0;
}

/**
 * @brief Whether the device behind @p from is one a person may take away.
 *
 * Asked of the arbitration service rather than guessed from the mount
 * point: "not the boot volume and local" calls a second internal volume
 * removable, and offering Eject on one is offering to break the machine.
 * A medium that says it is removable is; so is a device that says it is
 * not internal.  A device that says neither is left alone.
 *
 * @param from  the f_mntfromname of a mount, "/dev/disk2s5".
 */
static int
device_removable(const char *from)
{
    CFDictionaryRef about;
    DASessionRef session;
    DADiskRef disk;
    CFBooleanRef says;
    int removable = 0;

    if (strncmp(from, "/dev/", 5) != 0) {
        return 0;               /* not a device: nothing to take away */
    }
    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        return 0;
    }
    disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, from + 5);
    if (disk != NULL) {
        about = DADiskCopyDescription(disk);
        if (about != NULL) {
            says = CFDictionaryGetValue(about,
                                        kDADiskDescriptionMediaRemovableKey);
            if (says != NULL && CFBooleanGetValue(says)) {
                removable = 1;
            }
            says = CFDictionaryGetValue(about,
                                        kDADiskDescriptionDeviceInternalKey);
            if (says != NULL && !CFBooleanGetValue(says)) {
                removable = 1;
            }
            CFRelease(about);
        }
        CFRelease(disk);
    }
    CFRelease(session);
    return removable;
}

int
tiku_volumes_scan_now(tiku_volumes_t *vs)
{
    struct statfs *mounts = NULL;
    int count, i, data_rw = 0;

    if (vs == NULL) {
        return -1;
    }
    memset(vs, 0, sizeof *vs);
    count = getmntinfo(&mounts, MNT_NOWAIT);
    if (count <= 0) {
        return -1;
    }
    /*
     * The boot volume is two volumes: a sealed, read-only system and a
     * writable data volume, joined so that one namespace hangs off "/".
     * The system half is what "/" reports and it says read-only, which is
     * true of the seal and false of every path a person can reach -- and
     * both halves answer to the same st_dev, so the volume a path belongs
     * to cannot be told apart afterwards.  Ask the writable half once,
     * here, and let "/" wear its answer.
     */
    for (i = 0; i < count; i++) {
        if (strcmp(mounts[i].f_mntonname, "/System/Volumes/Data") == 0) {
            data_rw = (mounts[i].f_flags & MNT_RDONLY) == 0;
            break;
        }
    }
    for (i = 0; i < count && vs->n < TIKU_VOLUME_MAX; i++) {
        const struct statfs *m = &mounts[i];
        const char *type = m->f_fstypename;
        int is_root = strcmp(m->f_mntonname, "/") == 0;
        tiku_volume_t *v;
        struct stat st;

        /* Only volumes a person could browse.  MNT_DONTBROWSE is the
         * system's own word for that, set on every one of the boot
         * group's halves and on the recovery and preboot volumes: the
         * same bit the platform's own file manager reads, so what this
         * desktop shows is what the machine says to show. */
        if ((m->f_flags & MNT_DONTBROWSE) != 0 && !is_root) {
            continue;
        }
        if (strcmp(type, "devfs") == 0 || strcmp(type, "autofs") == 0 ||
            strcmp(type, "nullfs") == 0 || strcmp(type, "fdesc") == 0) {
            continue;
        }
        if (stat(m->f_mntonname, &st) != 0) {
            continue;
        }
        v = &vs->v[vs->n];
        memset(v, 0, sizeof *v);
        snprintf(v->mount, sizeof v->mount, "%s", m->f_mntonname);
        name_of(m->f_mntonname, v->name, sizeof v->name);
        v->dev = st.st_dev;
        snprintf(v->source, sizeof v->source, "%s", m->f_mntfromname);
        snprintf(v->fstype, sizeof v->fstype, "%.*s",
                 (int)(sizeof v->fstype - 1u), type);
        v->read_only = (m->f_flags & MNT_RDONLY) ? 1 : 0;
        if (is_root && data_rw) {
            v->read_only = 0;
        }
        v->shared = tiku_volume_shared_type(type) ||
                    !(m->f_flags & MNT_LOCAL);
        v->persistent = !(strcmp(type, "tmpfs") == 0 ||
                          strcmp(type, "devfs") == 0);
        v->is_boot = is_root;
        /* The system volume group's siblings -- /System/Volumes/Data and
         * the rest -- are parts of the boot disk wearing mount points,
         * and offering to eject one is offering to break the machine. */
        v->removable = !v->is_boot && device_removable(m->f_mntfromname);
        v->total = (uint64_t)m->f_blocks * (uint64_t)m->f_bsize;
        v->avail = (uint64_t)m->f_bavail * (uint64_t)m->f_bsize;
        vs->n++;
    }
    return vs->n;
}
#else
int
tiku_volumes_scan_now(tiku_volumes_t *vs)
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
           vs->n < TIKU_VOLUME_MAX) {
        char dev[256], mount[TIKU_PATH_MAX], type[64], opts[256];
        tiku_volume_t *v;
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
        snprintf(v->source, sizeof v->source, "%s", dev);
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
        snprintf(v->fstype, sizeof v->fstype, "%.*s",
                 (int)(sizeof v->fstype - 1u), type);
        /* Served over a network rather than by a disk.  It matters twice:
         * such a volume gets its own icon, and the setting that puts disks
         * on the desktop treats shares separately (IV-006, PVN-040). */
        v->shared = tiku_volume_shared_type(type);
        v->persistent = !(strcmp(type, "tmpfs") == 0 ||
                          strcmp(type, "devtmpfs") == 0 ||
                          strcmp(type, "ramfs") == 0 ||
                          strcmp(type, "overlay") == 0 ||
                          strcmp(type, "squashfs") == 0 ||
                          strncmp(type, "fuse.portal", 11) == 0);
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

#endif /* __APPLE__ */

int
tiku_volume_shared_type(const char *fstype)
{
    if (fstype == NULL) {
        return 0;
    }
    return (strcmp(fstype, "nfs") == 0 || strcmp(fstype, "nfs4") == 0 ||
            strcmp(fstype, "cifs") == 0 || strcmp(fstype, "smb3") == 0 ||
            strcmp(fstype, "smbfs") == 0 || strcmp(fstype, "9p") == 0 ||
            strncmp(fstype, "fuse.sshfs", 10) == 0);
}

unsigned
tiku_volume_flags(const tiku_volume_t *v)
{
    unsigned f = 0u;

    if (v == NULL) {
        return 0u;
    }
    if (v->read_only) { f |= TIKU_VOL_READ_ONLY; }
    if (v->is_boot)   { f |= TIKU_VOL_BOOT; }
    if (v->removable) { f |= TIKU_VOL_REMOVABLE; }
    if (v->shared)    { f |= TIKU_VOL_SHARED; }
    return f;
}

const tiku_volume_t *
tiku_volume_of(const tiku_volumes_t *vs, const char *path)
{
    const tiku_volume_t *best = NULL;
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
tiku_volume_is_root(const tiku_volumes_t *vs, const char *path)
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
tiku_volume_may_unmount(const tiku_volumes_t *vs, const char *path,
                            char *why, size_t max)
{
    const tiku_volume_t *v;

    if (!tiku_volume_is_root(vs, path)) {
        return 0;                   /* not a volume: not this gesture */
    }
    v = tiku_volume_of(vs, path);
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
tiku_volume_may_write(const tiku_volumes_t *vs, const char *path,
                          int write, char *why, size_t max)
{
    const tiku_volume_t *v;

    if (!write) {
        return 1;                   /* reading is always allowed */
    }
    v = tiku_volume_of(vs, path);
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

int
tiku_volume_shows_icon(const tiku_volumes_t *vs,
                           const tiku_volume_t *v)
{
    size_t n;
    int i;

    if (vs == NULL || v == NULL) {
        return 0;
    }
    if (v->is_boot) {
        return 1;                   /* everything is inside it */
    }
    n = strlen(v->mount);
    for (i = 0; i < vs->n; i++) {
        const char *o = vs->v[i].mount;
        size_t on = strlen(o);

        if (&vs->v[i] == v || vs->v[i].is_boot) {
            continue;
        }
        /* Mounted under another volume, on a whole path component: a
         * detail of that one rather than a second disk. */
        if (on < n && strncmp(v->mount, o, on) == 0 &&
            (o[on - 1u] == '/' || v->mount[on] == '/')) {
            return 0;
        }
    }
    return 1;
}

tiku_space_t
tiku_volume_space(const tiku_volume_t *v, int *used_pct)
{
    if (v == NULL) {
        if (used_pct != NULL) { *used_pct = 0; }
        return TIKU_SPACE_FREE;
    }
    return tiku_volume_space_facts(v->total, v->avail, used_pct);
}

tiku_space_t
tiku_volume_space_facts(uint64_t total, uint64_t avail, int *used_pct)
{
    struct { uint64_t total, avail; } vv;
    const void *vp = &vv;
    uint64_t used;
    int pct = 0;

    vv.total = total;
    vv.avail = avail;
    (void)vp;
    if (used_pct != NULL) {
        *used_pct = 0;
    }
    if (total == 0u) {
        /* A volume that will not say its size shows an empty bar rather
         * than a full one: guessing "full" would warn about every device
         * whose driver has no free-space call. */
        return TIKU_SPACE_FREE;
    }
    used = (total > avail) ? (total - avail) : 0u;
    pct = (int)((used * 100u + total / 2u) / total);
    if (pct > 100) { pct = 100; }
    if (used_pct != NULL) {
        *used_pct = pct;
    }
    /* The threshold is on what is LEFT, not on what is used: a rounded
     * used-share of 95 covers anything from 94.5 to 95.5, and the volume
     * that matters is the one with under a twentieth free. */
    if (avail * 100u < total * (uint64_t)TIKU_VOLUME_WARN_PCT) {
        return TIKU_SPACE_WARNING;
    }
    return TIKU_SPACE_USED;
}

/*---------------------------------------------------------------------------*/
/* Watching                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p a names the same mounted volume as @p b. */
static int
same_volume(const tiku_volume_t *a, const tiku_volume_t *b)
{
    /* Both, not either: a mount point reused by another device is one
     * volume leaving and another arriving, and a device remounted
     * somewhere else is too. */
    return strcmp(a->mount, b->mount) == 0 && a->dev == b->dev;
}

void
tiku_volwatch_init(tiku_volwatch_t *w)
{
    if (w == NULL) {
        return;
    }
    memset(w, 0, sizeof *w);
    if (tiku_volumes_scan(&w->was) >= 0) {
        w->primed = 1;
    }
}

int
tiku_volwatch_diff(const tiku_volumes_t *was,
                       const tiku_volumes_t *now,
                       tiku_volchange_t *out, int max)
{
    int i, j, n = 0;

    if (was == NULL || now == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < was->n && n < max; i++) {
        for (j = 0; j < now->n; j++) {
            if (same_volume(&was->v[i], &now->v[j])) {
                if (strcmp(was->v[i].name, now->v[j].name) != 0) {
                    /* The volume stayed and its label did not: a rename,
                     * not a departure (MA-021). */
                    memset(&out[n], 0, sizeof out[n]);
                    out[n].v = now->v[j];
                    out[n].renamed = 1;
                    n++;
                }
                break;
            }
        }
        if (j == now->n && n < max) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].v = was->v[i];
            out[n].mounted = 0;
            n++;
        }
    }
    for (j = 0; j < now->n && n < max; j++) {
        for (i = 0; i < was->n; i++) {
            if (same_volume(&was->v[i], &now->v[j])) {
                break;
            }
        }
        if (i == was->n) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].v = now->v[j];
            out[n].mounted = 1;
            n++;
        }
    }
    return n;
}

int
tiku_volwatch_poll(tiku_volwatch_t *w, tiku_volchange_t *out,
                       int max)
{
    tiku_volumes_t now;
    int n;

    if (w == NULL || out == NULL || max <= 0) {
        return 0;
    }
    if (!w->primed) {
        tiku_volwatch_init(w);
        return 0;               /* the first look is the baseline */
    }
    if (tiku_volumes_scan(&now) < 0) {
        return 0;
    }
    n = tiku_volwatch_diff(&w->was, &now, out, max);
    /* The baseline moves whether or not anything was reported, and whether
     * or not the caller had room: a change dropped for want of space must
     * not be reported for ever. */
    w->was = now;
    return n;
}

/**
 * @brief Drop any entry whose name another entry's name extends.
 *
 * A disk that is carved into partitions is not itself the mountable
 * thing, and both hosts name the parts by extending the whole's name --
 * sda and sda1, disk2 and disk2s5.
 *
 * @return how many are left.
 */
static int
drop_carved(tiku_partition_t *out, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            size_t li = strlen(out[i].name);

            if (i != j && strncmp(out[j].name, out[i].name, li) == 0 &&
                strlen(out[j].name) > li) {
                out[i] = out[--n];
                i--;
                break;
            }
        }
    }
    return n;
}

#ifdef __APPLE__
/** @brief A number out of a disk's description, 0 when it has none. */
static long long
disk_number(CFDictionaryRef about, CFStringRef key)
{
    CFNumberRef value = CFDictionaryGetValue(about, key);
    long long held = 0;

    if (value == NULL ||
        !CFNumberGetValue(value, kCFNumberLongLongType, &held)) {
        return 0;
    }
    return held;
}

/*
 * Darwin keeps no table of partitions to read.  The disks are asked
 * about one at a time through the arbitration service, and which ones
 * there are to ask about is what /dev has entries for -- the same two
 * facts the other host reads out of /proc/partitions, by the only route
 * this one offers.
 */
int
tiku_partitions_scan(tiku_partition_t *out, int max)
{
    DASessionRef session;
    struct dirent *entry;
    DIR *dev;
    int n = 0;

    if (out == NULL || max <= 0) {
        return 0;
    }
    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        return 0;
    }
    dev = opendir("/dev");
    while (dev != NULL && (entry = readdir(dev)) != NULL && n < max) {
        CFDictionaryRef about;
        DADiskRef disk;

        if (strncmp(entry->d_name, "disk", 4) != 0) {
            continue;
        }
        disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session,
                                       entry->d_name);
        if (disk == NULL) {
            continue;
        }
        about = DADiskCopyDescription(disk);
        if (about != NULL) {
            CFURLRef at = CFDictionaryGetValue(
                about, kDADiskDescriptionVolumePathKey);

            memset(&out[n], 0, sizeof out[n]);
            snprintf(out[n].name, sizeof out[n].name, "%s", entry->d_name);
            out[n].kb = (uint64_t)(disk_number(
                about, kDADiskDescriptionMediaSizeKey) / 1024);
            if (at != NULL &&
                CFURLGetFileSystemRepresentation(
                    at, true, (UInt8 *)out[n].mount,
                    (CFIndex)sizeof out[n].mount)) {
                out[n].mounted = 1;
            }
            n++;
            CFRelease(about);
        }
        CFRelease(disk);
    }
    if (dev != NULL) {
        (void)closedir(dev);
    }
    CFRelease(session);
    return drop_carved(out, n);
}
#else
int
tiku_partitions_scan(tiku_partition_t *out, int max)
{
    tiku_volumes_t vs;
    FILE *f;
    char line[256];
    int n = 0, have_vols, i;

    if (out == NULL || max <= 0) {
        return 0;
    }
    have_vols = tiku_volumes_scan(&vs) > 0;
    f = fopen("/proc/partitions", "r");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL && n < max) {
        unsigned major, minor;
        unsigned long long kb;
        char name[32];

        if (sscanf(line, "%u %u %llu %31s", &major, &minor, &kb,
                   name) != 4) {
            continue;
        }
        /* The kernel's own ram, loop and zram devices are not disks a
         * person mounts. */
        if (strncmp(name, "ram", 3) == 0 || strncmp(name, "loop", 4) == 0 ||
            strncmp(name, "zram", 4) == 0) {
            continue;
        }
        memset(&out[n], 0, sizeof out[n]);
        snprintf(out[n].name, sizeof out[n].name, "%s", name);
        out[n].kb = kb;
        if (have_vols) {
            char devpath[80];

            snprintf(devpath, sizeof devpath, "/dev/%s", name);
            for (i = 0; i < vs.n; i++) {
                if (strcmp(vs.v[i].source, devpath) == 0) {
                    out[n].mounted = 1;
                    snprintf(out[n].mount, sizeof out[n].mount, "%s",
                             vs.v[i].mount);
                    break;
                }
            }
        }
        n++;
    }
    (void)fclose(f);
    return drop_carved(out, n);
}
#endif /* __APPLE__ */


int
tiku_volume_mount(const char *devname, char *why, size_t max)
{
    char dev[80];

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (devname == NULL || devname[0] == '\0') {
        return -1;
    }
    snprintf(dev, sizeof dev, "/dev/%s", devname);
    /* The permission check comes before anything else the kernel would
     * object to, so an unprivileged browser learns the true reason. */
#ifndef __APPLE__
    if (mount(dev, "/mnt", "ext4", 0, NULL) == 0) {
        return 0;
    }
#else
    (void)dev;
    if (arbitrate_mount(devname) == 0) {
        return 0;
    }
#endif
    if (why != NULL) {
        snprintf(why, max, (errno == EPERM)
                     ? "Mounting \"%.40s\" needs the system's leave."
                     : "\"%.40s\" could not be mounted.", devname);
    }
    return -1;
}

tiku_unmount_t
tiku_volume_unmount(const tiku_volumes_t *vs, const char *path,
                        tiku_before_unmount_fn before, void *ctx,
                        char *why, size_t max)
{
    const tiku_volume_t *v;

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (!tiku_volume_may_unmount(vs, path, why, max)) {
        return TIKU_UNMOUNT_REFUSED;
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return TIKU_UNMOUNT_REFUSED;
    }
    if (before != NULL) {
        /* While the volume is still THERE: afterwards there is nowhere to
         * write, and the arrangement of everything open on that disk is
         * lost the moment it leaves (AW-096). */
        before(v->dev, ctx);
    }
    if (umount(v->mount) != 0) {
        if (why != NULL) {
            snprintf(why, max, (errno == EBUSY)
                         ? "Something is still using \"%.40s\"."
                         : "\"%.40s\" could not be unmounted.", v->name);
        }
        return (errno == EBUSY) ? TIKU_UNMOUNT_BUSY
                                : TIKU_UNMOUNT_FAILED;
    }
    return TIKU_UNMOUNT_OK;
}

/*
 * The cache.  A scan asks statvfs of every mount, and a mount that has
 * gone away on the network answers never; so a shell with workers has
 * one scan on a worker at a time and installs what it found here, and
 * every caller of tiku_volumes_scan() reads that.  With no cache ever
 * installed, a scan is a scan, as it always was.
 */
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
static tiku_volumes_t  cache;
static int             cache_count = -1;    /* -1: never installed */

void
tiku_volumes_cache_set(const tiku_volumes_t *vs, int count)
{
    pthread_mutex_lock(&cache_lock);
    if (vs != NULL) {
        cache = *vs;
    }
    cache_count = count;
    pthread_mutex_unlock(&cache_lock);
}

int
tiku_volumes_scan(tiku_volumes_t *vs)
{
    int count;

    if (vs == NULL) {
        return -1;
    }
    pthread_mutex_lock(&cache_lock);
    count = cache_count;
    if (count >= 0) {
        *vs = cache;
    }
    pthread_mutex_unlock(&cache_lock);
    if (count >= 0) {
        return count;
    }
    return tiku_volumes_scan_now(vs);
}

/*
 * An unmount in two halves, for a shell that will not wait on umount(2):
 * the asking and the saving on the loop, the call itself elsewhere.
 */
int
tiku_volume_unmount_prepare(const tiku_volumes_t *vs, const char *path,
                            tiku_before_unmount_fn before, void *ctx,
                            char *mount, size_t mmax, char *name,
                            size_t nmax, char *why, size_t max)
{
    const tiku_volume_t *v;

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (!tiku_volume_may_unmount(vs, path, why, max)) {
        return -1;
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return -1;
    }
    if (before != NULL) {
        before(v->dev, ctx);
    }
    snprintf(mount, mmax, "%s", v->mount);
    snprintf(name, nmax, "%s", v->name);
    return 0;
}

tiku_unmount_t
tiku_volume_umount_now(const char *mount, const char *name, char *why,
                       size_t max)
{
    if (umount(mount) != 0) {
        if (why != NULL) {
            snprintf(why, max, (errno == EBUSY)
                         ? "Something is still using \"%.40s\"."
                         : "\"%.40s\" could not be unmounted.", name);
        }
        return (errno == EBUSY) ? TIKU_UNMOUNT_BUSY
                                : TIKU_UNMOUNT_FAILED;
    }
    return TIKU_UNMOUNT_OK;
}

