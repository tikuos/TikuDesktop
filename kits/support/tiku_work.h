/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_work.h - workers for anything that waits.
 *
 * The loop is the one owner of the desk's state.  A job is what it
 * hands to a worker: self-contained, touching nothing it was not given,
 * returning a result the loop applies on its next turn.  The kit is two
 * rings -- requests in, results out -- and a few threads that take from
 * the one and put on the other.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WORK_H_
#define TIKU_WORK_H_

#include <stddef.h>

#define TIKU_WORK_KEY 48                  /* room for a key, with its end */

/**
 * @brief One piece of work.
 *
 * @c run is called on a worker with @c ctx, and may take as long as it
 * likes; it must touch nothing but what @c ctx holds.  @c done is called
 * on the loop, from tiku_work_collect(), once @c run has returned; it is
 * not called for a job cancelled first.  @c free is called on the loop
 * after @c done, or instead of it, and may be NULL.  Jobs sharing a
 * @c key run one at a time, in the order posted; an empty key runs
 * beside anything.
 */
typedef struct tiku_work_job {
    void (*run)(void *ctx);
    void (*done)(void *ctx);
    void (*free)(void *ctx);
    void *ctx;
    char  key[TIKU_WORK_KEY];
} tiku_work_job_t;

/**
 * @brief Hand @p job to a worker.  Loop thread only.
 * @return 0 when queued; -1 when the ring is full or the job has no run.
 */
int tiku_work_post(const tiku_work_job_t *job);

/**
 * @brief Run @c done, then @c free, for every job finished since last
 *        time.  Loop thread only; called once a turn.
 *
 * Takes what is finished at entry: a job posted from a @c done here is
 * collected next turn, not looped on.
 * @return how many were collected.
 */
int tiku_work_collect(void);

/**
 * @brief Cancel every job under @p key.  A job not yet started never
 *        runs; one running sees tiku_work_cancelled() say so and may
 *        stop early.  Neither has its @c done called.
 * @return how many were marked.
 */
int tiku_work_cancel(const char *key);

/** @brief For a running job: whether it was cancelled.  Any thread. */
int tiku_work_cancelled(const void *ctx);

/** @brief Jobs posted and not yet collected. */
int tiku_work_busy(void);

#endif /* TIKU_WORK_H_ */
