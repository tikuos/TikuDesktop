/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_host.h - native display host behind a portable event boundary.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_HOST_H_
#define TIKU_HOST_H_

#include "tiku_event.h"
#include "tiku_gfx.h"

typedef struct tiku_host tiku_host_t;

/** @brief Create a native window, revealed by its first complete presentation.
 * @return NULL when no display is present.
 */
tiku_host_t *tiku_host_open(int width, int height,
                                      const char *title);

/** @brief Release the native window and every platform resource it owns. */
void tiku_host_close(tiku_host_t *host);

/** @brief Discard uncommitted composition when an internal text target changes. */
void tiku_host_cancel_composition(tiku_host_t *host);

/** @brief Native pixels per logical pixel of the display (1..4). */
int tiku_host_scale(const tiku_host_t *host);

/** @brief Copy the software surface into the native window. */
int tiku_host_present(tiku_host_t *host,
                           const tiku_surface_t *surface);
/** @brief Wait for native input or a deadline; never consumes queued input. */
void tiku_host_wait(tiku_host_t *host, int64_t timeout_us);
/** @brief Cumulative native presentation counters (bytes are pixel payload). */
void tiku_host_present_stats(tiku_host_t *host, uint64_t *frames,
                             uint64_t *bytes, uint64_t *rectangles);

/** @brief Change the native window title. */
void tiku_host_set_title(tiku_host_t *host, const char *title);

/**
 * @brief Make the window the whole screen, or a window again.
 *
 * Asked of the window manager in the standard way, so a Mac's quartz-wm
 * and a Linux desktop both answer it; where there is no manager at all
 * the window is sized to the screen itself.  The new size arrives as
 * TIKU_EVENT_RESIZE like any other, so a shell has nothing to do but
 * what it does for a person dragging a corner.
 */
void tiku_host_set_fullscreen(tiku_host_t *host, int on);

/** @brief Poll one normalized event without waiting for other applications.
 * The X11 host exports local text copies and asynchronously refreshes CLIPBOARD
 * on activation, paste, and menu clicks. Input waits behind an in-flight import
 * for at most one second. TIKU_EVENT_CLIPBOARD is delivered before that input;
 * shells must mirror the new text to remote apps before forwarding their keys.
 * event.key carries a TIKU_CLIPBOARD_* status from tiku_clipboard.h.
 */
int tiku_host_poll(tiku_host_t *host, tiku_event_t *event);

/** @brief Select the horizontal-resize pointer while a divider is hot. */
void tiku_host_set_resize_cursor(tiku_host_t *host, int enabled);

/** @brief Read the current pointer position and logical modifier state. */
int tiku_host_pointer(tiku_host_t *host, int *x, int *y,
                           unsigned *modifiers);

#endif /* TIKU_HOST_H_ */
