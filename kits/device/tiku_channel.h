/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_channel.h - a board's window session over its console.
 *
 * The desktop's window session speaks over a file descriptor.  A channel
 * gives it one: a socket pair, one end for the desktop's listener to
 * adopt, the other fed by the frames a session cuts out of the board's
 * console and drained into frames the board reads.  The shell keeps the
 * same cable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CHANNEL_H_
#define TIKU_CHANNEL_H_

#include "tiku_session.h"

typedef struct tiku_channel tiku_channel_t;

/** @brief The features bit that marks a WELCOME as an invitation. */
#define TIKU_CHANNEL_INVITE 0x80000000u

/** @brief Open a channel on @p s; frames now flow to it.  NULL when out
 *         of sockets. */
tiku_channel_t *tiku_channel_open(tiku_session_t *s);

/**
 * @brief Tell the board a desktop is listening: a WELCOME carrying the
 *        invitation bit (TIKU_CHANNEL_INVITE in its features), which a
 *        board answers by saying hello again and re-opening what it had
 *        drawn before anyone was there to see it.
 */
void tiku_channel_greet(tiku_channel_t *ch);

/** @brief The descriptor for the desktop's listener to adopt. */
int tiku_channel_fd(const tiku_channel_t *ch);

/**
 * @brief Send what the desktop wrote to its end: one whole message as
 *        one frame per call; pointer moves queued behind a newer message
 *        are dropped.  @return how many frames went out, 0 or 1.
 */
int tiku_channel_pump(tiku_channel_t *ch);

/** @brief Close both ends; the session stops routing frames. */
void tiku_channel_close(tiku_channel_t *ch);

#endif /* TIKU_CHANNEL_H_ */
