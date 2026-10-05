/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_input.h - the interface's input event record and its types,
 * apart from the kernel's byte-sized event ids.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_INPUT_H_
#define TIKU_INPUT_H_
#include <stdint.h>

/** @brief What an input event is. */
typedef enum {
    TIKU_EVENT_NONE = 0,
    TIKU_EVENT_EXPOSE,
    TIKU_EVENT_CLOSE,
    TIKU_EVENT_RESIZE,
    TIKU_EVENT_KEY_DOWN,
    TIKU_EVENT_KEY_UP,
    TIKU_EVENT_POINTER_DOWN,
    TIKU_EVENT_POINTER_UP,
    TIKU_EVENT_POINTER_MOVE,
    TIKU_EVENT_WHEEL,
    TIKU_EVENT_ACTIVATED,
    TIKU_EVENT_DEACTIVATED,
    TIKU_EVENT_TICK,
    TIKU_EVENT_CLIPBOARD,
    TIKU_EVENT_TEXT_INPUT
} tiku_event_type_t;

/**
 * @brief One input event.
 *
 * @c key is a TIKU_KEY_* value or a character, and for TIKU_EVENT_CLIPBOARD
 * a TIKU_CLIPBOARD_* status.  @c button is a pointer button, numbered as X
 * numbers them, and for TIKU_EVENT_TEXT_INPUT the TIKU_TEXT_* flags; @c dx
 * and @c dy are a wheel's travel, or a text commit's offset and length.
 * @c x and @c y are logical; @c width and @c height a resize's new size.
 */
typedef struct {
    tiku_event_type_t type;
    int64_t                time_us;
    unsigned               modifiers;
    unsigned               key;
    unsigned               button;
    unsigned               repeat;
    char                   text[8];
    int                    x, y;
    int                    dx, dy;
    int                    width, height;
} tiku_input_event_t;
#endif
