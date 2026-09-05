/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_frame.h - the frame on a board's console: one codec for the desktop.
 *
 * A frame is END, a marker byte naming its channel, the payload escaped the
 * SLIP way, END.  The decoder cuts frames of one marker out of a byte
 * stream and calls everything else text; the encoder makes one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FRAME_H_
#define TIKU_FRAME_H_

#include <stddef.h>

#define TIKU_FRAME_END      0xC0u
#define TIKU_FRAME_ESC      0xDBu
#define TIKU_FRAME_ESC_END  0xDCu
#define TIKU_FRAME_ESC_ESC  0xDDu
#define TIKU_FRAME_ESC_NUL  0xDEu

/** @brief The marker a board's window session rides. */
#define TIKU_FRAME_MARK_SESSION 0xF1u

/** @brief What one fed byte was. */
#define TIKU_FRAME_TEXT  0     /**< outside any frame: the caller's */
#define TIKU_FRAME_TAKEN 1     /**< part of a frame, or a delimiter */
#define TIKU_FRAME_WHOLE 2     /**< a frame is complete in the buffer */

/** @brief A decoder for one marker's frames.  Fields are the codec's. */
typedef struct {
    unsigned char *buf;
    size_t         cap;
    size_t         len;        /**< of
