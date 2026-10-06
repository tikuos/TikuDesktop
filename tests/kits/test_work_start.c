/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_work_start.c - the worker pool when thread creation fails.
 *
 * pthread_create() is replaced, through the linker's --wrap, by one that
 * fails while the test says so.  A job posted while no worker can start
 * is queued; a later post that can start workers runs it and the new
 * job.  Off Linux, where the link has no --wrap, the checks are skipped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tiku_work.h"

static int fails;
static volatile int refuse;         /* nonzero: thread creation fails */

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

#ifdef __linux__
int __real_pthread_create(pthread_t *t, const pthread_attr_t *a,
                          void *(*fn)(void *), void *arg);

/** @brief pthread_create(), failing with EAGAIN while @c refuse is set. */
int
__wrap_pthread_create(pthread_t *t, const pthread_attr_t *a,
                      void *(*fn)(void *), void *arg)
{
    if (refuse) {
        return 11;                  /* EAGAIN */
    }
    return __real_pthread_create(t, a, fn, arg);
}
#endif

/** @brief A job's run: count it. */
static void
count_run(void *ctx)
{
    __atomic_add_fetch((int *)ctx, 1, __ATOMIC_SEQ_CST);
}

/** @brief Post a job that adds one to @p ran. */
static int
post(int *ran)
{
    tiku_work_job_t job;

    memset(&job, 0, sizeof job);
    job.run = count_run;
    job.ctx = ran;
    return tiku_work_post(&job);
}

/** @brief Collect until nothing is busy or two seconds pass. */
static void
settle(void)
{
    int waited = 0;

    while (tiku_work_busy() > 0 && waited < 2000) {
        (void)tiku_work_collect();
        usleep(10000);
        waited += 10;
    }
}

int
main(void)
{
    int first = 0, second = 0;

#ifndef __linux__
    printf("skipped: thread creation is replaced through GNU ld's --wrap\n");
    return 0;
#endif
    printf("a pool whose first start fails\n");
    refuse = 1;
    check(post(&first) == 0, "a job posted while no worker starts queues");
    refuse = 0;
    check(post(&second) == 0, "a job posted after creation recovers");
    settle();
    check(__atomic_load_n(&first, __ATOMIC_SEQ_CST) == 1 &&
          __atomic_load_n(&second, __ATOMIC_SEQ_CST) == 1,
          "the later post starts workers, and both jobs run");
    check(tiku_work_busy() == 0, "and both are collected");
    printf("%s\n", fails ? "FAIL: work start" : "PASS: work start");
    return fails != 0;
}
