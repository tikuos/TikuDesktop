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

const char *
tiku_clipboard_status_text(int status)
{
    switch (status) {
    case TIKU_CLIPBOARD_OK: return "";
    case TIKU_CLIPBOARD_TOO_LARGE:
        return "Host clipboard exceeds the 4095-byte text limit.";
    case TIKU_CLIPBOARD_TIMEOUT:
        return "Host clipboard did not respond. Try copying again.";
    case TIKU_CLIPBOARD_INVALID:
        return "Host clipboard contains invalid text; nothing was pasted.";
    default: return "Host clipboard has no supported text to paste.";
    }
}

void
tiku_clipboard_set(const char *text)
{
    size_t n = (text != NULL) ? strlen(text) : 0u;

    if (n >= sizeof held) {
        n = sizeof held - 1u;
        /* A valid UTF-8 string must not end in the middle of a codepoint. */
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    }
    memmove(held, (text != NULL) ? text : "", n);
    held[n] = '\0';
    stamp++;
}

int
tiku_clipboard_import(const char *text, size_t length)
{
    if ((!text && length) || length >= sizeof held) return 0;
    for (size_t i = 0; i < length;) {
        unsigned char c = (unsigned char)text[i++];
        unsigned cp, minimum;
        int extra;
        if (!c) return 0;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; minimum = 0x10000; }
        else return 0;
        while (extra--) {
            if (i == length || ((unsigned char)text[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | ((unsigned char)text[i++] & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    if (length) memmove(held, text, length);
    held[length] = 0;
    stamp++;
    return 1;
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
