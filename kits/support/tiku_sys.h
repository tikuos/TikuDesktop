/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sys.h - what only the machine underneath can answer.
 *
 * One declaration here, one file per host behind it, and the Makefile
 * picks which one is compiled.  Everything else in this tree is written
 * to POSIX and says the same thing on both; what lands here is what
 * POSIX leaves to the implementation, where a source that asked the
 * question directly would have to name a platform to get an answer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SYS_H
#define TIKU_SYS_H

#include <stddef.h>

/**
 * @brief The instruction that faulted, out of a signal handler's context.
 *
 * @param context  The third argument an SA_SIGINFO handler was given.
 * @return Where the program was, or NULL when this host will not say.
 *
 * Worth asking because a backtrace cannot always answer it: a function
 * that only stores -- the commonest way to fault -- keeps no frame
 * pointer on arm64, so a walk of the frames steps from the signal
 * trampoline straight to its caller and the one frame a reader most
 * wants is the one missing.  The context was never walking anything; it
 * holds the register.
 */
void *tiku_sys_fault_pc(const void *context);

/** @brief One running program, as tiku_sys_processes() offers it. */
typedef struct {
    long        pid;
    const char *name;       /**< the short name a person would know it by */
    const char *command;    /**< the image it was started from */
} tiku_sys_process_t;

/**
 * @brief Offer every process this user owns to @p fn, with @p ctx.
 *
 * This user's own, because a roster is a list of things a person may
 * activate or quit; and only those with an image of their own, because a
 * kernel thread has no window and no name worth showing.  The strings
 * are the seam's and last only for the call.
 *
 * @return How many were offered, or -1 where the host will not say.
 */
int tiku_sys_processes(void (*fn)(const tiku_sys_process_t *process,
                                  void *ctx), void *ctx);

/**
 * @brief Total and available memory, in kilobytes.
 *
 * Available rather than free: what a program could still be given,
 * counting what the system would hand back under pressure.  Both are
 * left at zero where the host will not say.
 */
void tiku_sys_memory(long *total_kb, long *avail_kb);

/** @brief Seconds since the machine came up, or 0 where it will not say. */
long tiku_sys_uptime(void);

/**
 * @brief This program's own image, resolved.
 *
 * What a restart execs, what a symboliser is pointed at, and what a
 * program climbs from to find its tree.
 *
 * @return 0 when @p out holds a path.
 */
int tiku_sys_self_path(char *out, size_t max);

/**
 * @brief A backtrace frame as the report wrote it, made readable.
 *
 * Both halves of this are the host's: what backtrace_symbols_fd() wrote
 * into the report, and what can turn an offset in it back into a name.
 * @p out is always written, falling back to @p raw itself.
 */
void tiku_sys_frame_name(const char *raw, char *out, size_t max);

#endif /* TIKU_SYS_H */
