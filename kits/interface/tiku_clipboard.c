/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * are built from.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clipboard.c - the desk's one TEXT clipboard.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_clipboard.h"

static char     held[TIKU_CLIPBOARD_MAX];
static unsigned stamp;

void
tiku_clipboard_set(const char *text)
{
    size_t n = (text != NULL) ? strlen(text) : 0u;

    if (n >= sizeof held) {
        n = sizeof held - 1u;
    }
    memcpy(held, (text != NULL) ? text : "", n);
    held[n] = '\0';
    stamp++;
}

const char *
tiku_clipboard_text(void)
{
    return held;
}

unsigned
tiku_clipboard_stamp(void)
{
    return stamp;
}
