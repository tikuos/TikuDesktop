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

/** @brief Flush an open regular file, including the host's drive-cache barrier.
 * Returns 0 on success, -1 on failure. Call after syncing changed parent
 * directories too; visibility alone does not establish durable publication. */
int tiku_sys_sync_file(int fd);

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

/**
 * @brief Pin the process on the other end of the local socket @p fd.
 *
 * The point is identity, not permission.  A pid is a number the system
 * hands out again, so between a person choosing a row in a Force Quit
 * dialog and pressing the button, the process they picked can exit and
 * its number be given to something else.  What comes back here holds the
 * process itself, so a later kill either reaches the one that was seen
 * or reaches nothing.
 *
 * Only this user's own, never init and never the caller: a dialog that
 * could stop the desktop is a dialog that will.
 *
 * @param pid  filled with the peer's process id when it is pinned.
 * @return A descriptor for tiku_sys_process_kill(), close-on-exec and
 *         the caller's to close; -1 with errno set otherwise, ENOTSUP
 *         where the host cannot pin at all.
 */
int tiku_sys_process_pin(int fd, long *pid);

/**
 * @brief Kill what @p handle pinned.
 *
 * @return 0, or -1 with ESRCH when the pinned process has gone -- which
 *         includes a handle this seam did not issue, since a handle it
 *         cannot recognise names no process it promised anything about.
 */
int tiku_sys_process_kill(int handle);

/**
 * @brief Make sure every byte of @p fd up to @p bytes really exists.
 *
 * A shared mapping over a sparse file can be made, mapped, and then kill
 * the program with SIGBUS on the first write to a page the store never
 * had room for.  Asking for the room up front turns that into a refusal
 * a caller can answer -- here, by sending pixels instead of sharing
 * them.  Hosts that do not over-commit this way have nothing to do.
 *
 * @return 0 when the room is there, an errno value when it is not.
 */
int tiku_sys_reserve(int fd, long long bytes);

/**
 * @brief Run an open serial line at exactly @p baud.
 *
 * Every host names a handful of rates as constants and the ordinary
 * termios call sets those.  Above them the hosts part: Linux names
 * 460800 and 921600 like any other, and macOS names neither -- its
 * termios stops at 230400 and a faster line is set through the driver
 * instead, after the rest of the line's shape has been agreed.
 *
 * Asked only when the ordinary way did not take, so on a host where it
 * did this is never reached and the rate is already right.
 *
 * @param fd    An open terminal, already configured.
 * @param baud  The rate in bits per second.
 * @return 0 when the line is running at it, -1 when this host cannot.
 */
int tiku_sys_serial_speed(int fd, int baud);

/**
 * @brief The serial ports a board could be on the other end of.
 *
 * Every host has its own name for a USB serial line and its own place
 * to list them, and the two do not resemble each other: Linux has udev
 * write a directory of stable by-id names, macOS has the driver put a
 * node in /dev and nothing else.  What is the SAME is the question --
 * which of this machine's ports might have a board behind it -- and the
 * answer is a path that can be opened.
 *
 * Nothing here is opened.  A port whose device has not answered its
 * host blocks the open for half a minute on some hosts, so listing must
 * cost nothing; deciding is the caller's, off the turn.
 *
 * @param out  Filled with up to @p max paths.
 * @param max  How many will fit.
 * @return how many were written, or -1 when this host has no such list.
 */
int tiku_sys_serial_ports(char out[][256], int max);

#endif /* TIKU_SYS_H */
