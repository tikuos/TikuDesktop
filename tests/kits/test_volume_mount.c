/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_volume_mount.c - which partitions the Mount menu offers, and where
 * and as what a partition is mounted.
 *
 * On Linux the test defines mount(), mkdir() and rmdir() itself, so the
 * volume code's calls reach these stand-ins and no device is touched.  The
 * stand-in mount() takes only the filesystem type it is told to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_volume.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

#ifndef __APPLE__
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

/* What the stand-ins were asked, and what mount() answers. */
static const char *accept_type;     /* the one type mount() takes */
static int         refuse_errno;    /* nonzero: mount() fails so */
static char        mounted_at[4][96];
static char        mounted_as[4][16];
static int         mounts;
static char        made[4][96];
static int         makes, removes;

int
mount(const char *source, const char *target, const char *type,
      unsigned long flags, const void *data)
{
    (void)source;
    (void)flags;
    (void)data;
    if (refuse_errno != 0) {
        errno = refuse_errno;
        return -1;
    }
    if (accept_type == NULL || strcmp(type, accept_type) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (mounts < 4) {
        snprintf(mounted_at[mounts], sizeof mounted_at[0], "%s", target);
        snprintf(mounted_as[mounts], sizeof mounted_as[0], "%s", type);
    }
    mounts++;
    return 0;
}

int
mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if (makes < 4) {
        snprintf(made[makes], sizeof made[0], "%s", path);
    }
    makes++;
    return 0;
}

int
rmdir(const char *path)
{
    (void)path;
    removes++;
    return 0;
}

static void
mounting(void)
{
    char why[128];

    printf("\nmounting a partition\n");
    accept_type = "vfat";
    check(tiku_volume_mount("sdb1", why, sizeof why) == 0 &&
          mounts == 1 && strcmp(mounted_as[0], "vfat") == 0,
          "D-133: a FAT stick mounts, as vfat");
    accept_type = "exfat";
    check(tiku_volume_mount("sdc1", why, sizeof why) == 0 &&
          mounts == 2 && strcmp(mounted_as[1], "exfat") == 0,
          "D-133: an exFAT stick mounts, as exfat");
    check(strcmp(mounted_at[0], "/mnt/sdb1") == 0 &&
          strcmp(mounted_at[1], "/mnt/sdc1") == 0 &&
          strcmp(made[0], "/mnt/sdb1") == 0,
          "D-134: each partition has its own directory under /mnt");

    accept_type = NULL;
    removes = 0;
    check(tiku_volume_mount("sdd1", why, sizeof why) == -1 &&
          strstr(why, "could not be mounted") != NULL && removes == 1,
          "a partition no type takes fails, and its directory goes");
    refuse_errno = EPERM;
    check(tiku_volume_mount("sdd1", why, sizeof why) == -1 &&
          strstr(why, "system's leave") != NULL,
          "an unprivileged mount is worded as needing the system's leave");
    refuse_errno = 0;
}
#endif

static void
carving(void)
{
    printf("\nwhich names are partitions of which disk\n");
    check(tiku_partition_carves("sda", "sda1") &&
          tiku_partition_carves("nvme0n1", "nvme0n1p2") &&
          tiku_partition_carves("mmcblk0", "mmcblk0p1") &&
          tiku_partition_carves("disk2", "disk2s5"),
          "sda1, nvme0n1p2, mmcblk0p1 and disk2s5 are partitions");
    check(!tiku_partition_carves("dm-1", "dm-10") &&
          !tiku_partition_carves("md1", "md10") &&
          !tiku_partition_carves("sda", "sdaa") &&
          !tiku_partition_carves("nbd1", "nbd10"),
          "D-135: dm-10, md10, sdaa and nbd10 are not partitions of "
          "dm-1, md1, sda and nbd1");
    check(!tiku_partition_carves("sda", "sda") &&
          !tiku_partition_carves("nvme0n1", "nvme0n1p"),
          "a name is not a partition of itself, nor a bare p");
}

int
main(void)
{
    carving();
#ifndef __APPLE__
    mounting();
#endif
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
