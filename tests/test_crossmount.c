/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_crossmount.c - copying between two stores in one namespace.
 *
 * The point of a namespace is that a file can be dragged from one device
 * to another, and the road between two stores is the backend contract --
 * which is narrow.  Its read takes no offset, and nothing in it removes
 * or creates anything.  So what this proves is as much about the
 * REFUSALS as about the copy: a limit that announces itself is a limit
 * somebody can work with, and one that truncates a file quietly is not.
 *
 * Two POSIX stores over one namespace rather than a board, because what
 * is under test is the crossing, and two backend objects over the same
 * filesystem cross exactly as two machines would as far as every
 * decision in tiku_fs is concerned.
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

static int fails;
static char work[256];

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
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

/** @brief Drive an operation to its end, or until it stops to ask. */
static void
run_to_end(tiku_fs_t *fs)
{
    int guard;

    /* Bounded: a step that stopped making progress would otherwise hang
     * the whole batch and report nothing at all. */
    for (guard = 0; guard < 4000; guard++) {
        if (tiku_fs_step(fs, 4) == 0) {
            return;
        }
    }
}

/**
 * @brief Take the refusal that stopped the last job, as the shell does.
 *
 * An operation that failed is WAITING to be told what to do about it,
 * and until it is told the next one cannot start.  A test that skipped
 * this would find its second operation refusing to begin and read that
 * as the second operation being broken.
 */
static void
acknowledge(tiku_fs_t *fs)
{
    if (tiku_fs_progress(fs)->error_ask) {
        tiku_fs_resolve_error(fs, 0);
    }
    run_to_end(fs);
}

static int
there(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static long
size_of(const char *path)
{
    struct stat st;

    return (stat(path, &st) == 0) ? (long)st.st_size : -1;
}

int
main(void)
{
    tiku_backend_t *root = tiku_backend_posix_open();
    tiku_backend_t *other = tiku_backend_posix_open();
    tiku_backend_t *ns = tiku_mount_open();
    tiku_fs_t *fs;
    const char *list[1];
    char from[512], to[512], landed[512];

    setvbuf(stdout, NULL, _IOLBF, 0);
    snprintf(work, sizeof work, "/tmp/trk-cross-%d", (int)getpid());
    sh("rm -rf %s && mkdir -p %s/here %s/away/sub", work, work, work);
    sh("printf 'hello' > %s/away/small.txt", work);
    sh("head -c 200000 /dev/zero > %s/away/big.bin", work);

    if (root == NULL || other == NULL || ns == NULL ||
        tiku_mount_add(ns, "/", root, 1) != 0 ||
        tiku_mount_add(ns, "/second", other, 1) != 0) {
        printf("[FAIL] no namespace to test\n");
        return 1;
    }
    fs = tiku_fs_new(ns);
    if (fs == NULL) {
        printf("[FAIL] no fs\n");
        return 1;
    }

    printf("a file crossing from one store to another (FS-XM)\n");
    snprintf(from, sizeof from, "/second%s/away/small.txt", work);
    snprintf(to, sizeof to, "%s/here", work);
    snprintf(landed, sizeof landed, "%s/here/small.txt", work);
    list[0] = from;
    check(tiku_fs_copy(fs, list, 1, to, TIKU_CONFLICT_ASK) == 0, "FS-XM",
          "a copy from one mount to another starts");
    run_to_end(fs);
    check(there(landed) && size_of(landed) == 5, "FS-XM",
          "and the file is on the other side, whole: the two stores never "
          "shared a filesystem call, only the backend contract");
    check(there(landed) &&
          size_of(landed) == size_of(strchr(from, '/') ? from + 7 : from),
          "FS-XM", "the same size the source is, byte for byte");

    printf("\nand what the contract cannot carry, it refuses (FS-XM)\n");
    sh("rm -f %s/here/big.bin", work);
    snprintf(from, sizeof from, "/second%s/away/big.bin", work);
    snprintf(landed, sizeof landed, "%s/here/big.bin", work);
    list[0] = from;
    (void)tiku_fs_copy(fs, list, 1, to, TIKU_CONFLICT_ASK);
    run_to_end(fs);
    check(!there(landed), "FS-XM",
          "a file too big to come back in one read is NOT copied: the "
          "read takes no offset, and half a file under the whole file's "
          "name is worse than no file");
    check(tiku_fs_progress(fs)->error[0] != '\0', "FS-XM",
          "and the refusal says so rather than failing quietly");
    acknowledge(fs);

    printf("\na folder is refused too, and for a reason it can state\n");
    sh("rm -rf %s/here/sub", work);
    snprintf(from, sizeof from, "/second%s/away/sub", work);
    snprintf(landed, sizeof landed, "%s/here/sub", work);
    list[0] = from;
    (void)tiku_fs_copy(fs, list, 1, to, TIKU_CONFLICT_ASK);
    run_to_end(fs);
    check(!there(landed), "FS-XM",
          "nothing in the backend contract makes a directory, so a folder "
          "does not half-cross");
    acknowledge(fs);

    printf("\nand a move within one store is untouched (FS-XM)\n");
    {
        char inside[512];

        sh("printf 'x' > %s/away/stays.txt", work);
        snprintf(from, sizeof from, "%s/away/stays.txt", work);
        snprintf(inside, sizeof inside, "%s/here/stays.txt", work);
        list[0] = from;
        check(tiku_fs_move(fs, list, 1, to, TIKU_CONFLICT_ASK) == 0,
              "FS-XM", "a move on one store still starts");
        run_to_end(fs);
        check(there(inside) && !there(from), "FS-XM",
              "and is still a rename rather than a copy: the crossing "
              "test must not have caught the ordinary case");
    }

    tiku_fs_free(fs);
    ns->ops->close(ns);
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "the stores keep their distance");
    return fails ? 1 : 0;
}
