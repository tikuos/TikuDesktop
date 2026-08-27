/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.h - the About box, in the R5 manner.
 *
 * One modeless window that says what this system IS: the wordmark, the
 * release, how many processors, and the memory with its gauge.  Closed by
 * its own close box or Escape; nothing in it is a control.
 *
 * It is an application DESCRIPTOR now, not a window controller: the shell
 * hosts it through the same services every embedded application gets, so
 * the flagship panel and a third-party app arrive through one door -- and
 * whatever the About needs of that door (a placement role, a refit) had
 * to be added to the door rather than kept as a private privilege.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ABOUT_WINDOW_H_
#define TIKU_TRK_ABOUT_WINDOW_H_

#include "tiku_app.h"

extern const tiku_app_descriptor_t tiku_trk_about_app;

#endif /* TIKU_TRK_ABOUT_WINDOW_H_ */
