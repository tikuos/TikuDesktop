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

#endif /* TIKU_SYS_H */
