/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trail.h - what this program was doing, kept in a ring.
 *
 * Always on and never written down: a fixed ring in memory, so the last
 * thing a program did is there to be read when it stops doing anything.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_TRAIL_H_
#define TIKU_TRAIL_H_

#include <stddef.h>

/* Enough to hold the run-up to a failure without being a log: a turn of
 * the desk's loop writes a handful of lines, and 512 is minutes of them. */
#define TIKU_TRAIL_LINES 512
#define TIKU_TRAIL_LINE  112

/**
 * @brief Write one line into the ring, as printf would.
 *
 * Formatted HERE rather than when it is read, so that reading it needs
 * nothing but write() -- which is what a signal handler has.
 */
void tiku_trail(const char *fmt, ...);

/**
 * @brief The most recent lines that fit in @p out, oldest of them first.
 * @return the bytes written.
 */
int tiku_trail_read(char *out, size_t max);

/**
 * @brief The same, but only lines holding @p needle (NULL for all).
 *
 * A ring is longer than any one reader's room, so a reader that knows
 * what it is asking about says so rather than being handed the end.
 */
int tiku_trail_find(const char *needle, char *out, size_t max);

/**
 * @brief Every line still in the ring, to @p fd, oldest first.
 *
 * Async-signal-safe: it formats nothing and allocates nothing.
 */
void tiku_trail_write(int fd);

/** @brief How many lines have been written since the program began. */
unsigned long tiku_trail_written(void);

#endif /* TIKU_TRAIL_H_ */
