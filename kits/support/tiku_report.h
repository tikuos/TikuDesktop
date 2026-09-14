/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_report.h - what a program was doing when it was killed.
 *
 * A fault leaves a file naming the signal, where it happened, and the
 * trail up to it; then the program dies as it would have, so a core
 * still drops for whoever wants one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_REPORT_H_
#define TIKU_REPORT_H_

#include <stddef.h>
#include <stdio.h>

/**
 * @brief Take the fatal signals, and say where reports go.
 *
 * @param program what to call this program in the report and its name.
 * @param dir     the folder for reports, or NULL for the one under the
 *                user's own configuration.
 */
void tiku_report_install(const char *program, const char *dir);

/** @brief Where the report of a fault would be written. */
const char *tiku_report_path(void);

/**
 * @brief The file that always holds the NEWEST report, whoever wrote it.
 *
 * One name, so a program that did not crash can still say where the
 * one that did put its report.
 */
const char *tiku_report_latest(void);

/**
 * @brief Write a report of a turn that has not ended, and carry on.
 *
 * @param ms         how long it has been quiet.
 * @param with_stack nonzero when the caller is the thread that is
 *                   stuck, so a backtrace of it means something.
 */
void tiku_report_stall(long ms, int with_stack);

/**
 * @brief Where reports go: @p dir, else $TIKU_TRK_REPORTS, else
 *        $HOME/.config/tracker/reports.  The one answer every program
 *        that writes or reads a report agrees on.
 */
void tiku_report_folder(const char *dir, char *out, size_t max);

/** @brief Where a report of a stall would be written. */
const char *tiku_report_stall_path(void);

/*
 * A report on demand.  SIGUSR2 only ASKS: a handler cannot format a
 * snapshot of anything, so the asking is a flag and the writing is the
 * loop's, where everything may be read and printed.
 */

/** @brief Arm SIGUSR2 to ask for a report. */
void tiku_report_ask_install(void);

/** @brief Whether a report was asked for since last asked; clears it. */
int tiku_report_asked(void);

/**
 * @brief Write report-<time>.txt: the header, @p why, what @p snapshot
 *        prints, the trail.  Loop context: formatting is allowed here.
 *
 * @param out where it was written, or "" when it could not be.
 * @return 0 when written.
 */
int tiku_report_write(const char *why, void (*snapshot)(FILE *, void *),
                      void *ctx, char *out, size_t max);

/**
 * @brief The newest report of any kind in the folder, or "" for none.
 *        The row in About this system that opens it asks this.
 */
int tiku_report_newest(char *out, size_t max);

/**
 * @brief Every report in the folder, newest first, as full paths.
 * @return how many were written to @p out.
 */
int tiku_report_list(char out[][512], int max);

/**
 * @brief Write a report now, as a fault would, and carry on.
 *
 * For a program that has decided something is wrong before anything has
 * killed it; the caller says @p why.
 */
void tiku_report_now(const char *why);

/** @brief Persist a parent's observation of a failed child, even SIGKILL.
 * No child stack is invented; any matching child crash report is referenced.
 * Returns 0 and its unique path on success. Normal/intentional exits are
 * filtered by the caller. Does not overwrite latest-crash.txt.
 */
int tiku_report_child(const char *program, long pid, int sig, int status,
                      char *out, size_t max);

#endif /* TIKU_REPORT_H_ */
