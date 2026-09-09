/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watch.h - noticing that a turn has not ended.
 *
 * A shell with one thread freezes by not coming back from something.
 * Nothing inside it can notice that, so something outside it counts the
 * turns and writes down what the stuck one was doing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WATCH_H_
#define TIKU_WATCH_H_

/**
 * @brief Watch the caller's thread, and report when it stops turning.
 *
 * Call from the thread that runs the loop, after reports are armed.
 * @param after_ms how long a quiet turn is a stall; 0 for five seconds.
 */
void tiku_watch_install(int after_ms);

/** @brief The loop turned.  Cheap enough for every turn, and meant for it. */
void tiku_watch_beat(void);

/** @brief How many stalls have been written down. */
int tiku_watch_stalls(void);

#endif /* TIKU_WATCH_H_ */
