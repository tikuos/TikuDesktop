/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * are built from.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clipboard.h - the desk's one TEXT clipboard.
 *
 * One clipboard for every field and every editor page in the process:
 * what was cut from a rename can be pasted into a program, and the
 * other way round.  The SHELL is the process that owns the desk, so
 * this store IS the desk's clipboard; an application hosted out of
 * process reaches the same store through its services, and the shell
 * keeps the two in step over the wire.
 *
 * Text only, by design: the file clipboard (Tracker's) holds names and
 * cuts nothing, and the two must never be one -- a rename's Cut must
 * not cut the selected files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLIPBOARD_H_
#define TIKU_CLIPBOARD_H_

#include <stddef.h>

#define TIKU_CLIPBOARD_MAX 4096

/** @brief Put @p text on the clipboard; NULL clears it. */
void tiku_clipboard_set(const char *text);

/** @brief What the clipboard holds -- never NULL, empty when nothing. */
const char *tiku_clipboard_text(void);

/**
 * @brief A counter that moves on every set().
 *
 * For the one party that must NOTICE a change it did not make -- the
 * shell, which mirrors the clipboard to its remote sessions -- without
 * this store having to know that party exists.
 */
unsigned tiku_clipboard_stamp(void);

#endif /* TIKU_CLIPBOARD_H_ */
