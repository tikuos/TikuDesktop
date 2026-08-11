/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   The TaskLoop — ALREADY EXISTS in the repo and needs no change; reproduced here so the unified API is complete. It already provides everything the six decisions schedule against: run-later, run-periodic (the 1 Hz roster scan, the graded query polls), run-when-idle, and run-accumulating with the can-accumulate / accumulate pair, a max fold count and a max window, which is exactly the 200 ms / 5 s device event accumulator the event queue configures. */

/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_task.h - delayed, periodic and accumulating tasks.
 *
 * Ported in shape from Tracker's TaskLoop (TaskLoop.h/.cpp): the flavours and
 * the accumulate discipline are the original's, which is what keeps a window
 * responsive while the world changes underneath it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TASK_H_
#define TIKU_TRK_TASK_H_

#include <stdint.h>

/** @brief Microseconds on the monotonic clock. */
int64_t tiku_trk_now(void);

typedef struct tiku_trk_loop tiku_trk_loop_t;
typedef struct tiku_trk_task tiku_trk_task_t;

/**
 * @brief A task's body.
 *
 * @return Non-zero when the task is finished and must not run again; a
 *         periodic task returns zero to stay scheduled.
 */
typedef int (*tiku_trk_task_fn)(void *ctx);

/**
 * @brief Decide whether a pending task can absorb new work.
 *
 * Tracker's AccumulatingFunctionObject::CanAccumulate: a burst of identical
 * requests must collapse into the one already queued rather than queue N
 * times.  @p pending is the queued task's context, @p incoming the new one.
 */
typedef int (*tiku_trk_can_accumulate_fn)(void *pending, void *incoming);

/** @brief Fold @p incoming into the already-queued @p pending context. */
typedef void (*tiku_trk_accumulate_fn)(void *pending, void *incoming);

/** @brief Release a task's context when the task is destroyed. */
typedef void (*tiku_trk_free_fn)(void *ctx);

tiku_trk_loop_t *tiku_trk_loop_new(void);

/** @brief Run every task whose time has come.  @return tasks run. */
int tiku_trk_loop_run(tiku_trk_loop_t *loop);

/**
 * @brief Microseconds until the next task is due.
 *
 * The event loop sleeps this long rather than spinning; -1 when nothing is
 * scheduled.
 */
int64_t tiku_trk_loop_next_due(const tiku_trk_loop_t *loop);

int tiku_trk_loop_count(const tiku_trk_loop_t *loop);

void tiku_trk_loop_free(tiku_trk_loop_t *loop);

/** @brief Run @p fn once, @p delay_us from now. */
tiku_trk_task_t *tiku_trk_run_later(tiku_trk_loop_t *loop,
                                    tiku_trk_task_fn fn, void *ctx,
                                    tiku_trk_free_fn freefn,
                                    int64_t delay_us);

/**
 * @brief Run @p fn every @p period_us until it returns non-zero.
 *
 * @param timeout_us  Give up after this long (0 = never).
 */
tiku_trk_task_t *tiku_trk_run_periodic(tiku_trk_loop_t *loop,
                                       tiku_trk_task_fn fn, void *ctx,
                                       tiku_trk_free_fn freefn,
                                       int64_t period_us,
                                       int64_t timeout_us);

/**
 * @brief Run @p fn once the loop has been quiet for @p idle_us.
 *
 * Tracker's RunWhenIdleTask: work that should happen, but never while the
 * user is busy.  Activity is reported by tiku_trk_loop_note_activity().
 */
tiku_trk_task_t *tiku_trk_run_when_idle(tiku_trk_loop_t *loop,
                                        tiku_trk_task_fn fn, void *ctx,
                                        tiku_trk_free_fn freefn,
                                        int64_t idle_us);

/** @brief Tell the loop the user did something (resets idle timers). */
void tiku_trk_loop_note_activity(tiku_trk_loop_t *loop);

/**
 * @brief Queue work that collapses with work already queued under @p key.
 *
 * This is the update-storm collapser: a hundred notifications for one folder
 * become one refresh.  When a pending task shares @p key and its
 * @p can_accumulate agrees, @p accumulate folds the new context in and the
 * deadline is pushed out by @p delay_us; otherwise a fresh task is queued.
 *
 * @param max_count  Stop accumulating after this many folds (0 = no limit),
 *                   so a continuous storm still gets serviced.
 * @param max_us     Stop accumulating this long after the first request
 *                   (0 = no limit).
 * @return The task holding the work, or NULL on failure.
 */
tiku_trk_task_t *tiku_trk_run_accumulating(tiku_trk_loop_t *loop,
                                           const char *key,
                                           tiku_trk_task_fn fn, void *ctx,
                                           tiku_trk_free_fn freefn,
                                           tiku_trk_can_accumulate_fn can,
                                           tiku_trk_accumulate_fn acc,
                                           int64_t delay_us,
                                           int64_t max_us,
                                           int32_t max_count);

/** @brief Cancel a task; its context is released. */
void tiku_trk_task_cancel(tiku_trk_loop_t *loop, tiku_trk_task_t *task);

/** @brief How many times this task absorbed extra work (tests read this). */
int32_t tiku_trk_task_accumulated(const tiku_trk_task_t *task);

#endif /* TIKU_TRK_TASK_H_ */
