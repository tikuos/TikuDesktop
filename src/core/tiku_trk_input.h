/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_input.h - platform-neutral input vocabulary.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_INPUT_H_
#define TIKU_TRK_INPUT_H_

#include "tiku_event.h"

/* Compatibility names while Tracker controllers migrate to the desktop app
 * API.  Their values and platform mapping are owned by the runtime. */
#define TIKU_TRK_MOD_SHIFT TIKU_MOD_SHIFT
#define TIKU_TRK_MOD_CMD   TIKU_MOD_CMD

#define TIKU_TRK_KEY_UP    TIKU_KEY_UP
#define TIKU_TRK_KEY_DOWN  TIKU_KEY_DOWN
#define TIKU_TRK_KEY_LEFT  TIKU_KEY_LEFT
#define TIKU_TRK_KEY_RIGHT TIKU_KEY_RIGHT
#define TIKU_TRK_KEY_HOME  TIKU_KEY_HOME
#define TIKU_TRK_KEY_END   TIKU_KEY_END
#define TIKU_TRK_KEY_PGUP  TIKU_KEY_PAGE_UP
#define TIKU_TRK_KEY_PGDN  TIKU_KEY_PAGE_DOWN

#endif /* TIKU_TRK_INPUT_H_ */
