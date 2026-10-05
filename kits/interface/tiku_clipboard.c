/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clipboard.c - the desk's one text clipboard, held at the size of
 * what it holds, up to TIKU_CLIPBOARD_LIMIT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_clipboard.h"

static char    *held;              /* NULL when empty                   */
static size_t   held_len;          /* its bytes, the NUL apart          */
static unsigned stamp;

const char *
tiku_clipboard_status_text(int status)
{
    static char too_large[96];

    switch (status) {
    case TIKU_CLIPBOARD_OK: return "";
    case TIKU_CLIPBOARD_TOO_LARGE:
        snprintf(too_large, sizeof too_large, "Host clipboard exceeds the "
                 "%u MB text limit.", TIKU_CLIPBOARD_LIMIT / (1024u * 1024u));
        return too_large;
    case TIKU_CLIPBOARD_TIMEOUT:
        return "Host clipboard did not respond. Try copying again.";
    case TIKU_CLIPBOARD_INVALID:
        return "Host clipboard contains invalid text; nothing was pasted.";
    default: return "Host clipboard has no supported text to paste.";
    }
}

/** @brief Hold @p n bytes of @p text in place of what was held, and move
 *         the stamp; empty when @p n is 0 or there is no memory for them.
 *  @return whether it holds them. */
static int
hold(const char *text, size_t n)
{
    char *copy = n != 0u ? malloc(n + 1u) : NULL;

    if (copy != NULL) {
        memcpy(copy, text, n);
        copy[n] = '\0';
    }
    free(held);
    held = copy;
    held_len = copy != NULL ? n : 0u;
    stamp++;
    return n == 0u || copy != NULL;
}

void
tiku_clipboard_set(const char *text)
{
    size_t n = (text != NULL) ? strlen(text) : 0u;

    if (n > TIKU_CLIPBOARD_LIMIT) {
        n = TIKU_CLIPBOARD_LIMIT;
        /* Cut before a character, not inside one. */
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    }
    (void)hold(text, n);
}

int
tiku_clipboard_import(const char *text, size_t length)
{
    char *copy;

    if ((!text && length) || length > TIKU_CLIPBOARD_LIMIT) return 0;
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
    if (length == 0u) return hold("", 0u);
    if ((copy = malloc(length + 1u)) == NULL) return 0;  /* keep what is held */
    memcpy(copy, text, length);
    copy[length] = '\0';
    free(held);
    held = copy;
    held_len = length;
    stamp++;
    return 1;
}

const char *
tiku_clipboard_text(void)
{
    return held != NULL ? held : "";
}

size_t
tiku_clipboard_length(void)
{
    return held_len;
}

unsigned
tiku_clipboard_stamp(void)
{
    return stamp;
}
