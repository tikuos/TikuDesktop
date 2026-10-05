/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clipboard.h - the desk's one text clipboard.
 *
 * One clipboard for every field and every editor page in the process:
 * what was cut from a rename can be pasted into a program, and the
 * other way round.  The shell owns the desk, so this store is the desk's
 * clipboard; an application hosted out of process reaches the same store
 * through its services, and the shell keeps the two in step.
 *
 * Text only: Tracker's file clipboard is a separate store of file
 * references, so a rename's Cut does not cut the selected files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLIPBOARD_H_
#define TIKU_CLIPBOARD_H_

#include <stddef.h>

/** @brief The most text the clipboard holds, in bytes, its NUL apart;
 *         every remote session keeps a copy of it. */
#define TIKU_CLIPBOARD_LIMIT (16u * 1024u * 1024u)

/** @brief A host clipboard transfer's result, which a
 *         TIKU_EVENT_CLIPBOARD event carries in its key. */
enum { TIKU_CLIPBOARD_OK = 1, TIKU_CLIPBOARD_UNAVAILABLE,
       TIKU_CLIPBOARD_TOO_LARGE, TIKU_CLIPBOARD_INVALID, TIKU_CLIPBOARD_TIMEOUT };

/** @brief The message to show for transfer result @p status: empty for
 *         TIKU_CLIPBOARD_OK. */
const char *tiku_clipboard_status_text(int status);

/** @brief Put @p text on the clipboard; NULL clears it.  Text past
 *         TIKU_CLIPBOARD_LIMIT is cut at a character boundary, and text
 *         there is no memory for leaves the clipboard empty. */
void tiku_clipboard_set(const char *text);

/**
 * @brief Put @p length bytes of @p text on the clipboard whole, or not at
 *        all: it must be UTF-8 with no NUL, and nothing is cut short.
 * @return 1, or 0 for more than TIKU_CLIPBOARD_LIMIT bytes, an embedded
 *         NUL, malformed UTF-8 or no memory, leaving the contents and the
 *         stamp as they were.
 */
int tiku_clipboard_import(const char *text, size_t length);

/** @brief What the clipboard holds: never NULL, empty when nothing; good
 *         until the next set() or import(). */
const char *tiku_clipboard_text(void);

/** @brief How many bytes the clipboard holds, its NUL apart. */
size_t tiku_clipboard_length(void);

/**
 * @brief A counter that moves on every set() and every import() that
 *        succeeds.
 *
 * A party that mirrors the clipboard (the shell, to its remote sessions)
 * compares it to notice a change it did not make.
 */
unsigned tiku_clipboard_stamp(void);

#endif /* TIKU_CLIPBOARD_H_ */
