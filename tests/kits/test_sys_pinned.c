/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_sys_pinned.c - the table of pinned processes, by handle.
 *
 * Darwin's tiku_sys_process_pin() records each kqueue it issues in this
 * table; here a descriptor on /dev/null stands in for the kqueue, since
 * the table sees only descriptor numbers.  A handle the caller closed must
 * not answer for a new handle given the same number, and must not hold
 * its row.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "tiku_sys_pinned.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-PIN  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief A fresh descriptor, the lowest number free. */
static int
handle_new(void)
{
    return open("/dev/null", O_RDONLY | O_CLOEXEC);
}

int
main(void)
{
    static tiku_sys_pinned_t table[TIKU_SYS_PINNED_MAX];
    tiku_sys_pinned_t *row;
    int a, b, a2, b2, i, held[TIKU_SYS_PINNED_MAX], added = 0;

    printf("the table of pinned processes (TS-PIN)\n");

    /* Two applications pinned, then both handles closed, as Force Quit
     * does on each refresh, and pinned again in the other order. */
    a = handle_new();
    b = handle_new();
    check(a > 0 && b > 0 && tiku_sys_pinned_add(table, a, 101, 1) == 0 &&
          tiku_sys_pinned_add(table, b, 202, 2) == 0,
          "two processes are pinned");
    (void)close(a);
    (void)close(b);
    b2 = handle_new();
    a2 = handle_new();
    check(b2 == a && a2 == b, "their numbers come back, swapped");
    check(tiku_sys_pinned_add(table, b2, 202, 2) == 0 &&
          tiku_sys_pinned_add(table, a2, 101, 1) == 0,
          "and both are pinned again");
    row = tiku_sys_pinned_find(table, b2);
    check(row != NULL && row->pid == 202,
          "a new handle finds its own process, not the closed one's");
    row = tiku_sys_pinned_find(table, a2);
    check(row != NULL && row->pid == 101,
          "and so does the other");
    (void)close(a2);
    (void)close(b2);

    /* Many pins whose handles are closed leave the table with room. */
    for (i = 0; i < 4 * TIKU_SYS_PINNED_MAX; i++) {
        int h = handle_new();

        if (h > 0 && tiku_sys_pinned_add(table, h, 300 + i, 3) == 0) {
            added++;
        }
        (void)close(h);
    }
    check(added == 4 * TIKU_SYS_PINNED_MAX,
          "a closed handle frees its row for the next pin");

    /* An open handle keeps its row: the table fills, and a close makes
     * room again. */
    for (i = 0; i < TIKU_SYS_PINNED_MAX; i++) {
        held[i] = handle_new();
        (void)tiku_sys_pinned_add(table, held[i], 400 + i, 4);
    }
    a = handle_new();
    check(tiku_sys_pinned_add(table, a, 500, 5) != 0 && errno == ENOMEM,
          "every row held by an open handle: the next pin is refused");
    (void)close(a);
    row = tiku_sys_pinned_find(table, held[5]);
    check(row != NULL && row->pid == 405,
          "and each open handle still finds its process");
    (void)close(held[0]);
    a = handle_new();
    check(tiku_sys_pinned_add(table, a, 600, 6) == 0 &&
          tiku_sys_pinned_find(table, a)->pid == 600,
          "a closed one makes room, and its number is the new pin's");
    check(tiku_sys_pinned_find(table, -1) == NULL &&
          tiku_sys_pinned_find(table, 0) == NULL,
          "no row answers for a handle that was never issued");
    (void)close(a);
    for (i = 1; i < TIKU_SYS_PINNED_MAX; i++) {
        (void)close(held[i]);
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe pinned table holds\n");
    return 0;
}
