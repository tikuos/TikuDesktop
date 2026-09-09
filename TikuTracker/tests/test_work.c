/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_work.c - workers for anything that waits.
 *
 * What is asked: that a job runs off the loop and its result is applied
 * ON the loop; that jobs under one key never overlap and keep their
 * order, while jobs under different keys do overlap; that a cancelled
 * job's done never runs and its free always does; that a job posted
 * from a done is legal and lands next turn; and that a job cannot beat
 * the watchdog.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_watch.h"
#include "tiku_work.h"

static int passes, fails;
static pthread_t loop_thread;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        fails++;
    }
}

/** @brief Wait on a clock, not a sleep, so a signal cannot cut it short. */
static void
hold_ms(long ms)
{
    struct timespec t0, t;

    (void)clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        long gone;

        (void)clock_gettime(CLOCK_MONOTONIC, &t);
        gone = (t.tv_sec - t0.tv_sec) * 1000L +
               (t.tv_nsec - t0.tv_nsec) / 1000000L;
        if (gone >= ms) {
            return;
        }
        usleep(2000);
    }
}

/** @brief Collect every turn until @p busy_left or @p ms elapse. */
static void
turn_until(int busy_left, long ms)
{
    long waited = 0;

    while (tiku_work_busy() > busy_left && waited < ms) {
        (void)tiku_work_collect();
        usleep(10000);
        waited += 10;
    }
    (void)tiku_work_collect();
}

/*---------------------------------------------------------------------------*/

typedef struct {
    int   id;
    long  hold;
    int   ran, done_ran, freed;
    int   done_on_loop, ran_on_loop;
    int   saw_cancel;
    long  began, ended;
} probe_t;

static volatile int inside;         /* jobs of one key running now */
static int overlapped;              /* ever two at once */
static int order[8], order_n;
static pthread_mutex_t order_lock = PTHREAD_MUTEX_INITIALIZER;

static long
ms_now(void)
{
    struct timespec t;

    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void
probe_run(void *ctx)
{
    probe_t *p = ctx;

    p->ran = 1;
    p->ran_on_loop = pthread_equal(pthread_self(), loop_thread);
    p->began = ms_now();
    if (++inside > 1) {
        overlapped = 1;
    }
    pthread_mutex_lock(&order_lock);
    if (order_n < 8) {
        order[order_n++] = p->id;
    }
    pthread_mutex_unlock(&order_lock);
    {
        long gone = 0;

        while (gone < p->hold) {
            if (tiku_work_cancelled(p)) {
                p->saw_cancel = 1;
                break;
            }
            hold_ms(5);
            gone += 5;
        }
    }
    inside--;
    p->ended = ms_now();
}

static void
probe_done(void *ctx)
{
    probe_t *p = ctx;

    p->done_ran = 1;
    p->done_on_loop = pthread_equal(pthread_self(), loop_thread);
}

static void
probe_free(void *ctx)
{
    ((probe_t *)ctx)->freed = 1;
}

static void
post(probe_t *p, const char *key)
{
    tiku_work_job_t job;

    memset(&job, 0, sizeof job);
    job.run = probe_run;
    job.done = probe_done;
    job.free = probe_free;
    job.ctx = p;
    snprintf(job.key, sizeof job.key, "%s", key);
    check(tiku_work_post(&job) == 0, "posted");
}

/* A done that posts: legal, and collected next turn. */
static probe_t chained;
static int chained_collected_same_turn;

static void
chain_done(void *ctx)
{
    (void)ctx;
    memset(&chained, 0, sizeof chained);
    chained.id = 99;
    chained.hold = 1;
    post(&chained, "");
    hold_ms(30);                    /* let it finish DURING this collect */
}

/* A job that tries to beat the watchdog. */
static void
beating_run(void *ctx)
{
    (void)ctx;
    tiku_watch_beat();
}

int
main(void)
{
    probe_t a, b, c, d, e, f;

    loop_thread = pthread_self();
    tiku_watch_install(60000);      /* far away: only its beat matters */

    printf("workers for anything that waits\n");

    /* 1. Off the loop, applied on it. */
    memset(&a, 0, sizeof a);
    a.id = 1; a.hold = 50;
    post(&a, "one");
    check(tiku_work_busy() == 1, "busy counts it");
    turn_until(0, 2000);
    check(a.ran && !a.ran_on_loop, "the job ran, and not on the loop");
    check(a.done_ran && a.done_on_loop, "its done ran, ON the loop");
    check(a.freed, "and it was freed");
    check(tiku_work_busy() == 0, "and busy is back to none");

    /* 2. One key: in order, never overlapping.  Different keys: overlapping. */
    memset(&b, 0, sizeof b); memset(&c, 0, sizeof c); memset(&d, 0, sizeof d);
    b.id = 2; b.hold = 80; c.id = 3; c.hold = 80; d.id = 4; d.hold = 80;
    inside = 0; overlapped = 0; order_n = 0;
    post(&b, "board");
    post(&c, "board");
    turn_until(0, 3000);
    check(!overlapped, "two jobs under one key never ran at once");
    check(order_n == 2 && order[0] == 2 && order[1] == 3,
          "and ran in the order posted");
    check(c.began >= b.ended, "the second began after the first ended");
    inside = 0; overlapped = 0;
    memset(&b, 0, sizeof b); memset(&c, 0, sizeof c);
    b.id = 2; b.hold = 80; c.id = 3; c.hold = 80;
    post(&b, "port-a");
    post(&c, "port-b");
    turn_until(0, 3000);
    check(overlapped, "two jobs under different keys ran at once");
    check(c.began < b.ended, "which is the point of having workers");

    /* 3. Cancel: queued never runs, running stops early; done never,
     *    free always. */
    memset(&d, 0, sizeof d); memset(&e, 0, sizeof e);
    d.id = 5; d.hold = 500; e.id = 6; e.hold = 500;
    post(&d, "slow");
    post(&e, "slow");
    hold_ms(40);                    /* d is running, e is behind it */
    check(tiku_work_cancel("slow") == 2, "cancel marks both");
    turn_until(0, 3000);
    check(d.ran && d.saw_cancel && d.ended - d.began < 300,
          "the running one saw it and stopped early");
    check(!e.ran, "the queued one never ran");
    check(!d.done_ran && !e.done_ran, "neither had its done called");
    check(d.freed && e.freed, "both were freed");

    /* 4. A job posted from a done is legal and lands next turn. */
    {
        tiku_work_job_t job;

        memset(&f, 0, sizeof f);
        f.id = 7; f.hold = 1;
        memset(&job, 0, sizeof job);
        job.run = probe_run; job.done = chain_done; job.free = probe_free;
        job.ctx = &f;
        snprintf(job.key, sizeof job.key, "chain");
        check(tiku_work_post(&job) == 0, "posted a job whose done posts");
        hold_ms(60);
        chained_collected_same_turn = 0;
        (void)tiku_work_collect();  /* runs chain_done, which posts */
        chained_collected_same_turn = chained.done_ran;
        check(!chained_collected_same_turn,
              "the job posted from a done is not collected in that turn");
        turn_until(0, 2000);
        check(chained.done_ran && chained.freed,
              "and is collected on the next");
    }

    /* 5. The hazard: a job cannot beat the watchdog. */
    {
        tiku_work_job_t job;
        int before = tiku_watch_refused();

        memset(&job, 0, sizeof job);
        job.run = beating_run;
        check(tiku_work_post(&job) == 0, "posted a job that beats");
        turn_until(0, 2000);
        check(tiku_watch_refused() == before + 1,
              "its beat was refused: a worker cannot hide a stall");
    }

    /* 6. The longest turn is measured on the loop. */
    tiku_watch_longest_reset();
    tiku_watch_beat();
    hold_ms(120);
    tiku_watch_beat();
    check(tiku_watch_longest_ms() >= 100 && tiku_watch_longest_ms() < 1000,
          "the longest gap between beats is what a proof reads");

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
