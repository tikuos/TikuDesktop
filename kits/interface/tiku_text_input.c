/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_text_input.c - a bounded UTF-8 commit, assembled from events.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_text_input.h"
#include <string.h>

static int valid(const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i++];
        unsigned cp, minimum;
        int extra;
        if (!c || (c < 32 && c != '\n' && c != '\t') || c == 127) return 0;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; minimum = 128; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; minimum = 2048; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; minimum = 65536; }
        else return 0;
        while (extra--) {
            if (i == n || ((unsigned char)s[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | ((unsigned char)s[i++] & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}
void tiku_text_input_reset(tiku_text_input_t *in)
{ in->text[0] = 0; in->length = in->offset = in->active = in->total = 0; }
int tiku_text_input_append(char *buffer, size_t capacity, const char *text)
{
    if (!buffer || !text || !capacity) return 0;
    char *end = memchr(buffer, 0, capacity);
    size_t n = strlen(text);
    if (!end || n >= TIKU_TEXT_INPUT_MAX || !valid(text, n) ||
        strchr(text, '\n') || strchr(text, '\t') ||
        n >= capacity - (size_t)(end - buffer)) return 0;
    memmove(end, text, n + 1);
    return 1;
}
void tiku_text_input_backspace(char *buffer, size_t capacity)
{
    if (!buffer || !capacity) return;
    char *end = memchr(buffer, 0, capacity);
    if (!end || end == buffer) return;
    char *at = end - 1;
    while (at > buffer && end - at < 4 && ((unsigned char)*at & 0xc0) == 0x80) at--;
    *at = 0;
}
int tiku_text_input_set(tiku_text_input_t *in, const char *text, size_t bytes)
{
    if (!text || bytes >= sizeof in->text || !valid(text, bytes)) {
        tiku_text_input_reset(in); return 0;
    }
    memmove(in->text, text, bytes); in->text[bytes] = 0;
    in->offset = 0;
    in->length = in->total = (int)bytes; in->active = bytes != 0;
    return 1;
}
int tiku_text_input_next(tiku_text_input_t *in, tiku_input_event_t *e)
{
    if (!in->active || in->offset >= in->length) return 0;
    int n = in->length - in->offset;
    if (n > (int)sizeof e->text - 1) {
        n = sizeof e->text - 1;
        while (n && ((unsigned char)in->text[in->offset + n] & 0xc0) == 0x80) n--;
    }
    memset(e, 0, sizeof *e);
    e->type = TIKU_EVENT_TEXT_INPUT;
    e->dx = in->offset; e->dy = in->length;
    if (!in->offset) e->button |= TIKU_TEXT_BEGIN;
    memcpy(e->text, in->text + in->offset, (size_t)n);
    in->offset += n;
    if (in->offset == in->length) { e->button |= TIKU_TEXT_END; in->active = 0; }
    return 1;
}
int tiku_text_input_event(tiku_text_input_t *in, const tiku_input_event_t *e)
{
    if (e->type != TIKU_EVENT_TEXT_INPUT) {
        if (e->type == TIKU_EVENT_KEY_DOWN || e->type == TIKU_EVENT_POINTER_DOWN ||
            e->type == TIKU_EVENT_DEACTIVATED || e->type == TIKU_EVENT_ACTIVATED)
            tiku_text_input_reset(in);
        return TIKU_TEXT_IGNORED;
    }
    const char *end = memchr(e->text, 0, sizeof e->text);
    if (e->button & TIKU_TEXT_BEGIN) {
        tiku_text_input_reset(in); in->active = 1; in->total = e->dy;
    }
    if (!end || !in->active || e->button & ~(TIKU_TEXT_BEGIN | TIKU_TEXT_END) ||
        e->key || e->modifiers || e->dx != in->length ||
        e->dy != in->total || e->dy <= 0 || e->dy >= TIKU_TEXT_INPUT_MAX)
        goto reject;
    size_t n = (size_t)(end - e->text);
    if (!n || !valid(e->text, n) || n > (size_t)(in->total - in->length)) goto reject;
    if (!!(e->button & TIKU_TEXT_END) != (in->length + (int)n == in->total)) goto reject;
    memcpy(in->text + in->length, e->text, n); in->length += (int)n;
    in->text[in->length] = 0;
    if (e->button & TIKU_TEXT_END) { in->active = 0; return TIKU_TEXT_COMPLETE; }
    return TIKU_TEXT_PENDING;
reject:
    tiku_text_input_reset(in); return TIKU_TEXT_REJECTED;
}
