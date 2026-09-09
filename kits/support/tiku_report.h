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

/** @brief Where a report of a stall would be written. */
const char *tiku_report_stall_path(void);

/**
 * @brief Write a report now, as a fault would, and carry on.
 *
 * For a program that has decided something is wrong before anything has
 * killed it; the caller says @p why.
 */
void tiku_report_now(const char *why);

#endif /* TIKU_REPORT_H_ */
