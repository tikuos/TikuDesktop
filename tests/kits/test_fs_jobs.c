/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fs_jobs.c - file jobs at their edges: a drop between two boards, a
 * cancelled move to another disk, the Trash found without a path, a large
 * tree deleted, a rename in a large folder, and a write that fails at close.
 *
 * Every fixture is made under a directory of its own from mkdtemp().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_fs.h"
#include "tiku_mount.h"
#include "tiku_notify.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-8s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

static void
sh(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (system(cmd) != 0) { /* the caller checks the effect */ }
}

static int
there(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0;
}

static void
put(const char *p, const char *text)
{
    FILE *f = fopen(p, "w");

    if (f != NULL) {
        (void)fputs(text, f);
        (void)fclose(f);
    }
}

/** @brief Cancel at this many cancel checks; 0 means never. */
static int cancel_after;
static int cancel_seen;

static int
cancel_hook(void *ctx)
{
    (void)ctx;
    return cancel_after != 0 && ++cancel_seen >= cancel_after;
}

/** @brief Step @p fs until it ends, answering each error with Continue.
 *  @return The errors answered. */
static int
run_to_end(tiku_fs_t *fs, char *first, size_t max)
{
    int guard, asks = 0;

    for (guard = 0; guard < 20000 && tiku_fs_busy(fs); guard++) {
        if (tiku_fs_step(fs, 8) != 0) {
            continue;
        }
        if (!tiku_fs_progress(fs)->error_ask) {
            break;
        }
        if (asks++ == 0 && first != NULL) {
            snprintf(first, max, "%s", tiku_fs_progress(fs)->error);
        }
        tiku_fs_resolve_error(fs, 1);
    }
    return asks;
}

/** @brief Two boards and the disk in one namespace (D-128). */
static void
drop_between_boards(const char *work)
{
    tiku_backend_t *ns = tiku_mount_open();
    tiku_backend_t *one = tiku_backend_posix_open();
    tiku_backend_t *two = tiku_backend_posix_open();
    tiku_fs_t *fs;
    char a[600], b[600], local[600];

    printf("a drop between two boards (D-128)\n");
    if (ns == NULL || one == NULL || two == NULL) {
        check(0, "D-128", "the namespace would not build");
        return;
    }
    snprintf(one->devid, sizeof one->devid, "%s", "uid-one");
    snprintf(two->devid, sizeof two->devid, "%s", "uid-two");
    if (tiku_mount_add(ns, "/", tiku_backend_posix_open(), 1) != 0 ||
        tiku_mount_add(ns, "/one", one, 1) != 0 ||
        tiku_mount_add(ns, "/two", two, 1) != 0 ||
        (fs = tiku_fs_new(ns)) == NULL) {
        check(0, "D-128", "the namespace would not build");
        return;
    }
    sh("mkdir -p %s/boards/from %s/boards/to", work, work);
    snprintf(a, sizeof a, "%s/boards/from/node.txt", work);
    put(a, "1");
    snprintf(a, sizeof a, "/one%s/boards/from/node.txt", work);
    snprintf(b, sizeof b, "/two%s/boards/to", work);
    check(tiku_fs_drop_action(fs, a, b, 0) == TIKU_OP_COPY, "D-128",
          "a drop from one board onto another copies");
    snprintf(b, sizeof b, "/one%s/boards/to", work);
    check(tiku_fs_drop_action(fs, a, b, 0) == TIKU_OP_MOVE, "D-128",
          "a drop within one board moves");
    snprintf(local, sizeof local, "%s/boards/to", work);
    check(tiku_fs_drop_action(fs, a, local, 0) == TIKU_OP_COPY, "D-128",
          "a drop from a board onto the disk copies");
    tiku_fs_free(fs);
    ns->ops->close(ns);
}

/** @brief A move to another disk cancelled inside the copy (D-129). */
static void
cancelled_move(const char *work)
{
    tiku_backend_t *disk = tiku_backend_posix_open();
    tiku_fs_t *fs = tiku_fs_new(disk);
    char far[] = "/dev/shm/trk_fs_jobs_XXXXXX";
    char src[700], dir[600];
    const char *sel[1];
    struct stat a, b;
    const tiku_progress_t *pr;

    printf("a move to another disk, cancelled part-way (D-129)\n");
    if (stat(work, &a) != 0 || mkdtemp(far) == NULL ||
        stat(far, &b) != 0 || a.st_dev == b.st_dev) {
        printf("  [SKIP] D-129    /dev/shm is not a second disk here\n");
        tiku_fs_free(fs);
        disk->ops->close(disk);
        return;
    }
    snprintf(dir, sizeof dir, "%s/movefrom", work);
    (void)mkdir(dir, 0755);
    snprintf(src, sizeof src, "%s/item.bin", dir);
    put(src, "0123456789");
    sel[0] = src;
    cancel_seen = 0;
    /* The first check is the one before the item, the second is inside
     * the copy of its bytes. */
    cancel_after = 2;
    tiku_fs_set_cancel_check(fs, cancel_hook, NULL);
    (void)tiku_fs_move(fs, sel, 1, far, TIKU_CONFLICT_ASK);
    (void)tiku_fs_step(fs, 4);
    pr = tiku_fs_progress(fs);
    check(pr->cancelled, "D-129", "the move says it was cancelled");
    check(!pr->error_ask, "D-129",
          "and puts up no error for the cancel");
    check(there(src), "D-129", "the source stays where it was");
    if (pr->error_ask) {
        printf("           error: %s\n", pr->error);
    }
    cancel_after = 0;
    tiku_fs_set_cancel_check(fs, NULL, NULL);
    tiku_fs_free(fs);
    disk->ops->close(disk);
    sh("rm -rf %s", far);
}

/** @brief The Trash asked for without a path is the home folder's
 *         (D-130). */
static void
trash_without_path(const char *work)
{
    tiku_backend_t *disk = tiku_backend_posix_open();
    tiku_fs_t *fs = tiku_fs_new(disk);
    char home[600], want[700], got[TIKU_PATH_MAX] = "";
    const char *was = getenv("HOME");
    char keep[600];

    printf("the Trash with no path given (D-130)\n");
    snprintf(keep, sizeof keep, "%s", was != NULL ? was : "");
    snprintf(home, sizeof home, "%s/home", work);
    (void)mkdir(home, 0755);
    (void)setenv("HOME", home, 1);
    snprintf(want, sizeof want, "%s/.Trash", home);
    check(tiku_fs_trash_dir(fs, NULL, got, sizeof got) == 0, "D-130",
          "a NULL path resolves a Trash");
    check(strcmp(got, want) == 0, "D-130", "and it is the home's .Trash");
    if (was != NULL) {
        (void)setenv("HOME", keep, 1);
    } else {
        (void)unsetenv("HOME");
    }
    tiku_fs_free(fs);
    disk->ops->close(disk);
}

/**
 * @brief A tree of 10 folders of 60 files, deleted whole (D-131).
 *
 * Its second level holds 600 entries, more than the queue's 512 slots.
 */
static void
large_tree_delete(const char *work)
{
    tiku_backend_t *disk = tiku_backend_posix_open();
    tiku_fs_t *fs = tiku_fs_new(disk);
    char tree[600], first[256] = "";
    const char *sel[1];
    int asks;

    printf("deleting a tree of 610 entries (D-131)\n");
    snprintf(tree, sizeof tree, "%s/bigtree", work);
    sh("mkdir -p %s && cd %s && for d in 0 1 2 3 4 5 6 7 8 9; do "
       "mkdir d$d; for f in $(seq 1 60); do : > d$d/f$f; done; done",
       tree, tree);
    sel[0] = tree;
    check(tiku_fs_delete(fs, sel, 1) == 0, "D-131", "the delete starts");
    asks = run_to_end(fs, first, sizeof first);
    check(asks == 0, "D-131", "it stops on no error");
    if (asks != 0) {
        printf("           %d errors, the first: %s\n", asks, first);
    }
    check(!there(tree), "D-131", "and the whole tree is gone");
    tiku_fs_free(fs);
    disk->ops->close(disk);
}

/**
 * @brief One folder of 1,300 files, one of 1,300 files and a folder that
 *        cannot be emptied, and one of 600 files that cannot be removed
 *        (D-131).
 *
 * The queue has 511 slots for children, so each folder takes rounds.
 */
static void
wide_folder_delete(const char *work)
{
    tiku_backend_t *disk = tiku_backend_posix_open();
    tiku_fs_t *fs = tiku_fs_new(disk);
    char dir[600], stuck[700], first[256] = "";
    const char *sel[1];
    int asks;

    printf("deleting folders wider than the queue (D-131)\n");
    snprintf(dir, sizeof dir, "%s/wide", work);
    sh("mkdir -p %s && cd %s && for f in $(seq 1 1300); do : > f$f; done",
       dir, dir);
    sel[0] = dir;
    (void)tiku_fs_delete(fs, sel, 1);
    asks = run_to_end(fs, first, sizeof first);
    check(asks == 0 && !there(dir), "D-131",
          "a folder of 1,300 files is deleted with no error");
    if (asks != 0) {
        printf("           %d errors, the first: %s\n", asks, first);
    }
    if (geteuid() == 0) {
        printf("  [SKIP] D-131    unremovable entries: running as root\n");
        tiku_fs_free(fs);
        disk->ops->close(disk);
        return;
    }

    snprintf(dir, sizeof dir, "%s/mixed", work);
    snprintf(stuck, sizeof stuck, "%s/stuck", dir);
    sh("mkdir -p %s && cd %s && for f in $(seq 1 1300); do : > f$f; done "
       "&& : > %s/kept && chmod 555 %s", stuck, dir, stuck, stuck);
    sel[0] = dir;
    (void)tiku_fs_delete(fs, sel, 1);
    asks = run_to_end(fs, NULL, 0);
    {
        char probe[800];

        snprintf(probe, sizeof probe, "%s/f1300", dir);
        check(!tiku_fs_busy(fs) && asks > 0, "D-131",
              "a folder with a child that cannot go ends with an error");
        check(there(stuck) && !there(probe), "D-131",
              "and everything else in it is gone");
    }
    sh("chmod 755 %s", stuck);

    snprintf(dir, sizeof dir, "%s/locked", work);
    sh("mkdir -p %s && cd %s && for f in $(seq 1 600); do : > f$f; done "
       "&& chmod 555 %s", dir, dir, dir);
    sel[0] = dir;
    (void)tiku_fs_delete(fs, sel, 1);
    asks = run_to_end(fs, first, sizeof first);
    check(!tiku_fs_busy(fs) && asks > 0 && there(dir), "D-131",
          "a folder of 600 files none of which can go ends with errors");
    printf("           %d errors, the first: %s\n", asks, first);
    sh("chmod 755 %s", dir);
    tiku_fs_free(fs);
    disk->ops->close(disk);
}

/** @brief A rename at the end of a 300-entry listing (D-136). */
static void
rename_in_large_folder(void)
{
    enum { N = 300 };
    tiku_model_t *was = calloc(N, sizeof *was);
    tiku_model_t *now = calloc(N, sizeof *now);
    static tiku_evlist_t out;
    int i, moved = 0, other = 0;

    printf("a rename in a folder of %d entries (D-136)\n", N);
    if (was == NULL || now == NULL) {
        check(0, "D-136", "no memory for the listings");
        free(was);
        free(now);
        return;
    }
    for (i = 0; i < N; i++) {
        snprintf(was[i].name, sizeof was[i].name, "f%03d", i);
        snprintf(was[i].path, sizeof was[i].path, "/dir/f%03d", i);
        snprintf(was[i].facts.meta, sizeof was[i].facts.meta, "-");
        was[i].kind = TIKU_KIND_FILE;
        was[i].node_id = (uint64_t)(i + 1);
        now[i] = was[i];
    }
    snprintf(now[N - 1].name, sizeof now[N - 1].name, "renamed");
    snprintf(now[N - 1].path, sizeof now[N - 1].path, "/dir/renamed");
    tiku_evlist_clear(&out);
    (void)tiku_notify_diff(was, N, now, N, 0, &out);
    for (i = 0; i < out.count; i++) {
        if (out.ev[i].op == TIKU_EV_MOVED &&
            strcmp(out.ev[i].from, was[N - 1].path) == 0 &&
            strcmp(out.ev[i].path, now[N - 1].path) == 0) {
            moved++;
        } else {
            other++;
        }
    }
    check(moved == 1 && other == 0, "D-136",
          "the rename is one MOVED notice, not a removal and a creation");
    free(was);
    free(now);
}

/** @brief A write whose bytes fail to reach the disk at close (D-137). */
static void
write_failing_at_close(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    char err[256] = "";

    printf("a write that fails at close (D-137)\n");
    if (access("/dev/full", W_OK) != 0) {
        printf("  [SKIP] D-137    this host has no /dev/full\n");
        b->ops->close(b);
        return;
    }
    check(b->ops->write(b, "/dev/full", "1", 1, err, sizeof err) == -1,
          "D-137", "the write fails");
    check(err[0] != '\0', "D-137", "and fills the error text");
    b->ops->close(b);
}

int
main(void)
{
    char work[] = "/tmp/trk_fs_jobs_XXXXXX";

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (mkdtemp(work) == NULL) {
        printf("[FAIL] no scratch directory\n");
        return 1;
    }
    drop_between_boards(work);
    cancelled_move(work);
    trash_without_path(work);
    large_tree_delete(work);
    wide_folder_delete(work);
    rename_in_large_folder();
    write_failing_at_close();
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "all file-job edges hold");
    return fails ? 1 : 0;
}
