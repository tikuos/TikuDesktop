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
 * @brief A UTF-8 locale of the caller's own, for reading characters and
 *        ordering them, or NULL when the host has none; the process's
 *        and the thread's locales are left as they are.
 *
 * C.UTF-8 is taken where the host has it, ordering characters by code
 * point, and en_US.UTF-8 where it does not; there a bracket range in a
 * regular expression may follow that locale's order of letters instead.
 */
void *tiku_sys_locale_utf8(void);
/** @brief Make @p locale the calling thread's, or with NULL only ask, and
 *         return the thread's locale before. */
void *tiku_sys_locale_use(void *locale);
/** @brief Free a locale tiku_sys_locale_utf8() made; NULL is ignored.
 *  @note Put the thread's locale back first; a locale
 *        tiku_sys_locale_use() returned is not the caller's to free. */
void tiku_sys_locale_free(void *locale);

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
 * Identity, not permission.  A pid is a number the system hands out
 * again, so between a person choosing a row in a Force Quit dialog and
 * pressing the button that process can exit and its number go to
 * something else; what comes back here holds the process itself, so a
 * later kill reaches the one that was seen or reaches nothing.  Only
 * this user's own, never init and never the caller.
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
 * 460800 and 921600 like any other, while macOS stops at 230400 and
 * sets a faster line through the driver, after the rest of the line's
 * shape has been agreed.  Asked only when the ordinary way did not
 * take, so where it did this is never reached.
 *
 * @param fd    An open terminal, already configured.
 * @param baud  The rate in bits per second.
 * @return 0 when the line is running at it, -1 when this host cannot.
 */
int tiku_sys_serial_speed(int fd, int baud);

/**
 * @brief The serial ports a board could be on the other end of.
 *
 * Each host lists its USB serial lines its own way: udev writes a
 * directory of stable by-id names on Linux, while macOS has only the
 * node the driver put in /dev.  The question is the same on both, and
 * so is the answer: a path that can be opened.  Nothing here opens one.
 * A port whose device has not answered can block an open for half a
 * minute, so listing costs nothing and deciding is the caller's.
 *
 * @param out  Filled with up to @p max paths.
 * @param max  How many will fit.
 * @return how many were written, or -1 when this host has no such list.
 */
int tiku_sys_serial_ports(char out[][256], int max);

#endif /* TIKU_SYS_H */
