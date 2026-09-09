/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_work.c - workers for anything that waits.
 *
 * One table of slots under one lock.  A worker takes the oldest QUEUED
 * slot whose key has nothing RUNNING, runs it unlocked, and marks it
 * FINISHED; the loop takes the FINISHED slots at collect and runs their
 * done unlocked.  Nothing here allocates: the slots are a fixed table,
 * and what a job needs it brought in its own context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_work.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* THE TABLE                                                                 */
/*---------------------------------------------------------------------------*/

#define SLOTS    64
#define WORKERS  4

enum { FREE = 0, QUEUED, RUNNING, FINISHED };

typedef struct {
    tiku_work_job_t job;
    int             state;
    int             cancelled;
    unsigned long   order;          /* posted when; oldest runs first */
    long            posted_ms, began_ms, ended_ms;
} slot_t;

static slot_t          slots[SLOTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  more = PTHREAD_COND_INITIALIZER;
static unsigned long   posted;
static int             started;
static int             busy;        /* posted, not yet collected */

/** @brief Milliseconds on a clock that does not step. */
static long
now_ms(void)
{
    struct timespec t;

    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/** @brief Whether a job with @p key is running now.  Lock held. */
static int
key_running(const char *key)
{
    int i;

    if (key[0] == '\0') {
        return 0;
    }
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == RUNNING &&
            strcmp(slots[i].job.key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief The oldest QUEUED slot a worker may take, or -1.  Lock held.
 *
 * Oldest by order, not by index: a slot freed early and refilled would
 * otherwise jump the queue.
 */
static int
next_runnable(void)
{
    int i, best = -1;

    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state != QUEUED) {
            continue;
        }
        if (best >= 0 && slots[i].order > slots[best].order) {
            continue;
        }
        if (slots[i].cancelled || !key_running(slots[i].job.key)) {
            best = i;
        }
    }
    return best;
}

/*---------------------------------------------------------------------------*/
/* THE WORKERS                                                               */
/*---------------------------------------------------------------------------*/

static void *
working(void *ignored)
{
    (void)ignored;
    for (;;) {
        int i;

        pthread_mutex_lock(&lock);
        while ((i = next_runnable()) < 0) {
            pthread_cond_wait(&more, &lock);
        }
        if (slots[i].cancelled) {
            /* Never started: finished without running, so the loop
             * frees it without calling done. */
            slots[i].state = FINISHED;
            pthread_mutex_unlock(&lock);
            continue;
        }
        slots[i].state = RUNNING;
        slots[i].began_ms = now_ms();
        pthread_mutex_unlock(&lock);

        slots[i].job.run(slots[i].job.ctx);

        pthread_mutex_lock(&lock);
        slots[i].ended_ms = now_ms();
        slots[i].state = FINISHED;
        /* A key just freed may unblock a job behind it. */
        pthread_cond_broadcast(&more);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

/** @brief Start the pool, once.  Lock held. */
static void
start_workers(void)
{
    int i;

    for (i = 0; i < WORKERS; i++) {
        pthread_t t;

        if (pthread_create(&t, NULL, working, NULL) == 0) {
            (void)pthread_detach(t);
        }
    }
    started = 1;
}

/*---------------------------------------------------------------------------*/
/* THE LOOP'S SIDE                                                           */
/*---------------------------------------------------------------------------*/

int
tiku_work_post(const tiku_work_job_t *job)
{
    int i;

    if (job == NULL || job->run == NULL) {
        return -1;
    }
    pthread_mutex_lock(&lock);
    if (!started) {
        start_workers();
    }
    for (i = 0; i < SLOTS && slots[i].state != FREE; i++) {
    }
    if (i == SLOTS) {
        pthread_mutex_unlock(&lock);
        tiku_trail("work: full, %s refused", job->key);
        return -1;
    }
    slots[i].job = *job;
    slots[i].job.key[TIKU_WORK_KEY - 1] = '\0';
    slots[i].state = QUEUED;
    slots[i].cancelled = 0;
    slots[i].order = ++posted;
    slots[i].posted_ms = now_ms();
    busy++;
    pthread_cond_broadcast(&more);
    pthread_mutex_unlock(&lock);
    tiku_trail("work: post %s", job->key);
    return 0;
}

int
tiku_work_collect(void)
{
    int taken[SLOTS];
    int i, n = 0, k;

    /* What is finished at entry, and nothing that finishes during. */
    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == FINISHED) {
            taken[n++] = i;
        }
    }
    pthread_mutex_unlock(&lock);

    for (k = 0; k < n; k++) {
        slot_t *s = &slots[taken[k]];
        tiku_work_job_t job = s->job;
        int cancelled = s->cancelled;

        if (!cancelled) {
            tiku_trail("work: done %s (%ld ms, waited %ld)", job.key,
                       s->ended_ms - s->began_ms,
                       s->began_ms - s->posted_ms);
            if (job.done != NULL) {
                job.done(job.ctx);
            }
        } else {
            tiku_trail("work: dropped %s", job.key);
        }
        if (job.free != NULL) {
            job.free(job.ctx);
        }
        pthread_mutex_lock(&lock);
        s->state = FREE;
        busy--;
        pthread_mutex_unlock(&lock);
    }
    return n;
}

int
tiku_work_cancel(const char *key)
{
    int i, n = 0;

    if (key == NULL) {
        return 0;
    }
    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        if ((slots[i].state == QUEUED || slots[i].state == RUNNING) &&
            strcmp(slots[i].job.key, key) == 0 && !slots[i].cancelled) {
            slots[i].cancelled = 1;
            n++;
        }
    }
    pthread_cond_broadcast(&more);
    pthread_mutex_unlock(&lock);
    return n;
}

int
tiku_work_cancelled(const void *ctx)
{
    int i, yes = 0;

    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == RUNNING && slots[i].job.ctx == ctx) {
            yes = slots[i].cancelled;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return yes;
}

int
tiku_work_busy(void)
{
    int n;

    pthread_mutex_lock(&lock);
    n = busy;
    pthread_mutex_unlock(&lock);
    return n;
}
