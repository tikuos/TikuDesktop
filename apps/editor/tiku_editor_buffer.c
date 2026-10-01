/* tiku_editor_buffer.c -- buffer.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_buffer.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int edit_utf8_valid(const char *s, size_t n);
static size_t edit_buffer_range(const edit_buffer_t *b, int y0, int x0,
                               int y1, int x1, char *out, size_t cap);
static int edit_buffer_splice(edit_buffer_t *b, int y0, int x0, int y1, int x1,
                              const char *text, int *end_y, int *end_x);
static void edit_buffer_drop(edit_buffer_t *b, int i);
static void edit_buffer_push(edit_buffer_t *b, edit_change_t *c);
static int edit_buffer_hits(edit_buffer_t *b, const edit_change_t *c, int undo);

/** Validate UTF-8 directly, without the small IME-event staging buffer. */
static int edit_utf8_valid(const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i++];
        if (!c || (c < 32 && c != '\n' && c != '\t') || c == 127) return 0;
        if (c < 128) continue;
        unsigned cp, minimum; int extra;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; minimum = 128; extra = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; minimum = 2048; extra = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; minimum = 65536; extra = 3; }
        else return 0;
        while (extra--) {
            if (i >= n || ((unsigned char)s[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | ((unsigned char)s[i++] & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

/** Release only owned lines and journal payloads. */
void tiku_editor_buffer_free(edit_buffer_t *b)
{
    for (int i = 0; i < b->nline; i++) free(b->line[i].text);
    free(b->line);
    for (int i = 0; i < b->undo_n; i++) {
        free(b->undo[i].before); free(b->undo[i].after); free(b->undo[i].hit);
    }
    memset(b, 0, sizeof *b);
}

/** Parse a complete document before replacing the old one; retain final LF. */
int tiku_editor_buffer_set(edit_buffer_t *b, const char *text)
{
    size_t size = strlen(text), count = 1;
    if (size > INT_MAX - 1u || !edit_utf8_valid(text, size)) return 0;
    for (size_t i = 0; i < size; i++) count += text[i] == '\n';
    if (count > INT_MAX || count > SIZE_MAX / sizeof(edit_line_t)) return 0;
    edit_line_t *lines = calloc(count, sizeof *lines);
    if (!lines) return 0;
    const char *p = text;
    for (size_t i = 0; i < count; i++) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        lines[i].text = malloc(n + 1);
        if (!lines[i].text) {
            for (size_t j = 0; j < i; j++) free(lines[j].text);
            free(lines); return 0;
        }
        memcpy(lines[i].text, p, n); lines[i].text[n] = 0;
        lines[i].len = (int)n; lines[i].width = -1;
        p += n + (nl != NULL);
    }
    tiku_editor_buffer_free(b); b->line = lines; b->nline = (int)count;
    return 1;
}

int tiku_editor_buffer_init(edit_buffer_t *b)
{ memset(b, 0, sizeof *b); return tiku_editor_buffer_set(b, ""); }
const char *tiku_editor_buffer_line(const edit_buffer_t *b, int y)
{ return y >= 0 && y < b->nline ? b->line[y].text : ""; }
int tiku_editor_buffer_modified(const edit_buffer_t *b)
{ return b->revision != b->saved_revision; }
void tiku_editor_buffer_saved(edit_buffer_t *b)
{ b->saved_revision = b->revision; }
int tiku_editor_buffer_can_undo(const edit_buffer_t *b)
{ return b->undo_at > 0; }
int tiku_editor_buffer_can_redo(const edit_buffer_t *b)
{ return b->undo_at < b->undo_n; }

/** Public coordinates are byte offsets snapped to UTF-8 boundaries. */
void tiku_editor_buffer_place(edit_buffer_t *b, int y, int x)
{
    if (!b->nline) return;
    if (y < 0) y = 0;
    if (y >= b->nline) y = b->nline - 1;
    if (x < 0) x = 0;
    if (x > b->line[y].len) x = b->line[y].len;
    while (x > 0 && x < b->line[y].len && ((unsigned char)b->line[y].text[x] & 0xc0) == 0x80) x--;
    b->cy = y; b->cx = x;
}
void tiku_editor_buffer_select_none(edit_buffer_t *b)
{ b->selecting = 0; }
void tiku_editor_buffer_select_begin(edit_buffer_t *b)
{ if (!b->selecting) { b->ay = b->cy; b->ax = b->cx; b->selecting = 1; } }
void tiku_editor_buffer_select_all(edit_buffer_t *b)
{
    if (!b->nline) return;
    b->ay = b->ax = 0; b->cy = b->nline - 1;
    b->cx = b->line[b->cy].len; b->selecting = 1;
}
int tiku_editor_buffer_selection(const edit_buffer_t *b, int *y0, int *x0, int *y1, int *x1)
{
    if (!b->selecting || (b->ay == b->cy && b->ax == b->cx)) return 0;
    int forward = b->ay < b->cy || (b->ay == b->cy && b->ax < b->cx);
    *y0 = forward ? b->ay : b->cy; *x0 = forward ? b->ax : b->cx;
    *y1 = forward ? b->cy : b->ay; *x1 = forward ? b->cx : b->ax;
    return 1;
}

/** Export a range exactly, reporting required space without partial copying. */
static size_t edit_buffer_range(const edit_buffer_t *b, int y0, int x0,
                               int y1, int x1, char *out, size_t cap)
{
    size_t n = (size_t)(y1 - y0);
    for (int y = y0; y <= y1; y++)
        n += (size_t)((y == y1 ? x1 : b->line[y].len) - (y == y0 ? x0 : 0));
    if (!out || cap <= n) { if (out && cap) out[0] = 0; return n; }
    size_t used = 0;
    for (int y = y0; y <= y1; y++) {
        int start = y == y0 ? x0 : 0, end = y == y1 ? x1 : b->line[y].len;
        if (y != y0) out[used++] = '\n';
        memcpy(out + used, b->line[y].text + start, (size_t)(end - start));
        used += (size_t)(end - start);
    }
    out[used] = 0; return used;
}
size_t tiku_editor_buffer_text(const edit_buffer_t *b, char *out, size_t cap)
{
    if (!b->nline) { if (out && cap) out[0] = 0; return 0; }
    return edit_buffer_range(b, 0, 0, b->nline - 1, b->line[b->nline - 1].len, out, cap);
}
size_t tiku_editor_buffer_selected(const edit_buffer_t *b, char *out, size_t cap)
{
    int y0, x0, y1, x1;
    if (!tiku_editor_buffer_selection(b, &y0, &x0, &y1, &x1)) {
        if (out && cap) out[0] = 0;
        return 0;
    }
    return edit_buffer_range(b, y0, x0, y1, x1, out, cap);
}

/** Splice complete affected lines, never temporary oversized joined lines.
 * Every allocation happens before the first document mutation. */
static int edit_buffer_splice(edit_buffer_t *b, int y0, int x0, int y1, int x1,
                              const char *text, int *end_y, int *end_x)
{
    size_t n = strlen(text), count = 1;
    for (size_t i = 0; i < n; i++) count += text[i] == '\n';
    size_t total = (size_t)b->nline - (size_t)(y1 - y0) + count - 1;
    if (total > INT_MAX || total > SIZE_MAX / sizeof(edit_line_t)) return 0;
    edit_line_t *added = calloc(count, sizeof *added);
    if (!added) return 0;
    const char *p = text;
    for (size_t i = 0; i < count; i++) {
        const char *nl = strchr(p, '\n');
        size_t take = nl ? (size_t)(nl - p) : strlen(p);
        size_t prefix = i ? 0 : (size_t)x0;
        size_t tail = i + 1 == count ? (size_t)(b->line[y1].len - x1) : 0;
        size_t len = prefix + take + tail;
        if (len > INT_MAX - 1u) goto fail;
        added[i].text = malloc(len + 1);
        if (!added[i].text) goto fail;
        memcpy(added[i].text, b->line[y0].text, prefix);
        memcpy(added[i].text + prefix, p, take);
        memcpy(added[i].text + prefix + take, b->line[y1].text + x1, tail);
        added[i].text[len] = 0; added[i].len = (int)len; added[i].width = -1;
        if (i + 1 == count) { *end_y = y0 + (int)i; *end_x = (int)(prefix + take); }
        p += take + (nl != NULL);
    }
    if (total > (size_t)b->nline) {
        edit_line_t *grown = realloc(b->line, total * sizeof *grown);
        if (!grown) goto fail;
        b->line = grown;
    }
    for (int y = y0; y <= y1; y++) free(b->line[y].text);
    memmove(b->line + y0 + count, b->line + y1 + 1,
            (size_t)(b->nline - y1 - 1) * sizeof *b->line);
    memcpy(b->line + y0, added, count * sizeof *added);
    free(added); b->nline = (int)total;
    b->cy = *end_y; b->cx = *end_x; b->selecting = 0;
    return 1;
fail:
    for (size_t i = 0; i < count; i++) free(added[i].text);
    free(added); return 0;
}

/** Retire an undo entry without retaining its potentially large payload. */
static void edit_buffer_drop(edit_buffer_t *b, int i)
{
    b->history_bytes -= b->undo[i].bytes;
    free(b->undo[i].before); free(b->undo[i].after); free(b->undo[i].hit);
}

/** Record @p c as the newest change.  The redo side goes first, then the
 *  oldest changes while 256 are held or the total would pass 32 MiB; the
 *  newest change is kept even when it alone is larger. */
static void edit_buffer_push(edit_buffer_t *b, edit_change_t *c)
{
    for (int i = b->undo_at; i < b->undo_n; i++) edit_buffer_drop(b, i);
    b->undo_n = b->undo_at;
    while (b->undo_n && (b->undo_n == 256 || b->history_bytes + c->bytes > 32u * 1024 * 1024)) {
        edit_buffer_drop(b, 0);
        memmove(b->undo, b->undo + 1, (size_t)(--b->undo_n) * sizeof *b->undo);
    }
    c->revision = ++b->serial; b->revision = c->revision;
    b->undo[b->undo_n++] = *c; b->undo_at = b->undo_n;
    b->history_bytes += c->bytes;
}

/** Swap the text at every hit of a Replace All record: the match for the
 *  replacement, or back when @p undo is set.  Each line it touches is built
 *  before any line changes, and the caret returns to where it was when the
 *  Replace All ran. */
static int edit_buffer_hits(edit_buffer_t *b, const edit_change_t *c, int undo)
{
    long long q = (long long)c->match, r = (long long)strlen(c->after);
    long long take = undo ? r : q, put = undo ? q : r;
    int lines = 0, l = 0;
    for (int k = 0; k < c->nhit; k++) lines += !k || c->hit[k].y != c->hit[k - 1].y;
    edit_line_t *made = calloc((size_t)lines, sizeof *made);
    if (!made) return 0;
    for (int k = 0; k < c->nhit; l++) {
        int y = c->hit[k].y, end = k;
        while (end < c->nhit && c->hit[end].y == y) end++;
        const char *line = b->line[y].text;
        long long len = b->line[y].len + (end - k) * (put - take), from = 0, at = 0;
        if (len < 0 || len > INT_MAX - 1 || !(made[l].text = malloc((size_t)len + 1))) goto fail;
        for (int j = k; j < end; j++) {
            long long x = c->hit[j].x + (undo ? (j - k) * (r - q) : 0);
            memcpy(made[l].text + at, line + from, (size_t)(x - from)); at += x - from;
            memcpy(made[l].text + at, undo ? c->before + (size_t)j * c->match : c->after,
                   (size_t)put);
            at += put; from = x + take;
        }
        memcpy(made[l].text + at, line + from, (size_t)(b->line[y].len - from));
        made[l].text[len] = 0; made[l].len = (int)len; made[l].width = -1;
        k = end;
    }
    l = 0;
    for (int k = 0; k < c->nhit; k++) {
        if (k && c->hit[k].y == c->hit[k - 1].y) continue;
        free(b->line[c->hit[k].y].text); b->line[c->hit[k].y] = made[l++];
    }
    free(made);
    b->selecting = 0; tiku_editor_buffer_place(b, c->y, c->x);
    return 1;
fail:
    for (int i = 0; i < lines; i++) free(made[i].text);
    free(made); return 0;
}

/** Commit one reversible range replacement, including an empty replacement. */
int tiku_editor_buffer_replace(edit_buffer_t *b, const char *text)
{
    size_t n = strlen(text);
    if (n > INT_MAX - 1u || !edit_utf8_valid(text, n)) return 0;
    if (!b->nline && !tiku_editor_buffer_set(b, "")) return 0;
    int y0, x0, y1, x1;
    if (!tiku_editor_buffer_selection(b, &y0, &x0, &y1, &x1)) {
        y0 = y1 = b->cy; x0 = x1 = b->cx;
    }
    size_t old = edit_buffer_range(b, y0, x0, y1, x1, NULL, 0);
    if (!old && !n) { b->selecting = 0; return 1; }
    edit_change_t c = {.y = y0, .x = x0, .old_y = y1, .old_x = x1,
        .bytes = old + n + 2, .previous = b->revision};
    c.before = malloc(old + 1); c.after = strdup(text);
    if (!c.before || !c.after) { free(c.before); free(c.after); return 0; }
    edit_buffer_range(b, y0, x0, y1, x1, c.before, old + 1);
    if (!edit_buffer_splice(b, y0, x0, y1, x1, text, &c.new_y, &c.new_x)) {
        free(c.before); free(c.after); return 0;
    }
    edit_buffer_push(b, &c);
    return 1;
}

/** Replace @p match bytes at each of @p n hits by @p with, as one record
 *  that holds the matches rather than the document.  Hits come in document
 *  order, each inside one line and none overlapping; on success the record
 *  keeps @p hit. */
int tiku_editor_buffer_replace_hits(edit_buffer_t *b, edit_hit_t *hit, int n,
                                    size_t match, const char *with)
{
    size_t r = strlen(with);
    if (n <= 0 || !match || match > INT_MAX || r > INT_MAX - 1u ||
        (size_t)n > (SIZE_MAX - r - 2) / (match + sizeof *hit) ||
        !edit_utf8_valid(with, r) || strchr(with, '\n')) return 0;
    for (int k = 0; k < n; k++) {
        int y = hit[k].y;
        if (y < 0 || y >= b->nline || hit[k].x < 0 ||
            (size_t)hit[k].x + match > (size_t)b->line[y].len ||
            (k && (y < hit[k - 1].y || (y == hit[k - 1].y &&
                   (size_t)hit[k - 1].x + match > (size_t)hit[k].x)))) return 0;
    }
    edit_change_t c = {.y = b->cy, .x = b->cx, .hit = hit, .nhit = n, .match = match,
        .bytes = (size_t)n * (match + sizeof *hit) + r + 2, .previous = b->revision};
    c.before = malloc((size_t)n * match + 1); c.after = strdup(with);
    if (!c.before || !c.after) goto fail;
    for (int k = 0; k < n; k++)
        memcpy(c.before + (size_t)k * match, b->line[hit[k].y].text + hit[k].x, match);
    c.before[(size_t)n * match] = 0;
    if (!edit_buffer_hits(b, &c, 0)) goto fail;
    edit_buffer_push(b, &c);
    return 1;
fail:
    free(c.before); free(c.after); return 0;
}

/** Undo and redo use the same atomic splice, never byte-by-byte replay;
 *  a Replace All record swaps its matches back line by line. */
int tiku_editor_buffer_undo(edit_buffer_t *b)
{
    if (!b->undo_at) return 0;
    edit_change_t *c = &b->undo[b->undo_at - 1]; int y, x;
    if (c->nhit ? !edit_buffer_hits(b, c, 1)
                : !edit_buffer_splice(b, c->y, c->x, c->new_y, c->new_x, c->before, &y, &x))
        return 0;
    b->undo_at--; b->revision = c->previous; return 1;
}
int tiku_editor_buffer_redo(edit_buffer_t *b)
{
    if (b->undo_at == b->undo_n) return 0;
    edit_change_t *c = &b->undo[b->undo_at]; int y, x;
    if (c->nhit ? !edit_buffer_hits(b, c, 0)
                : !edit_buffer_splice(b, c->y, c->x, c->old_y, c->old_x, c->after, &y, &x))
        return 0;
    b->undo_at++; b->revision = c->revision; return 1;
}

int tiku_editor_buffer_scroll_to(edit_buffer_t *b, int top, int rows)
{
    int last = b->nline > rows ? b->nline - rows : 0;
    if (top > last) top = last;
    if (top < 0) top = 0;
    b->top = top; return top;
}
void tiku_editor_buffer_reveal(edit_buffer_t *b, int rows)
{
    int top = b->top;
    if (b->cy < top) top = b->cy;
    if (b->cy >= top + rows) top = b->cy - rows + 1;
    tiku_editor_buffer_scroll_to(b, top, rows);
}
void tiku_editor_buffer_move(edit_buffer_t *b, tiku_textview_move_t move)
{
    if (!b->nline) return;
    int y = b->cy, x = b->cx;
    if (move == TIKU_TEXTVIEW_LEFT) {
        if (x) {
            x--; while (x && ((unsigned char)b->line[y].text[x] & 0xc0) == 0x80) x--;
        } else if (y) { y--; x = b->line[y].len; }
    } else if (move == TIKU_TEXTVIEW_RIGHT) {
        if (x < b->line[y].len) {
            x++; while (x < b->line[y].len && ((unsigned char)b->line[y].text[x] & 0xc0) == 0x80) x++;
        } else if (y + 1 < b->nline) { y++; x = 0; }
    } else if (move == TIKU_TEXTVIEW_UP) y--;
    else if (move == TIKU_TEXTVIEW_DOWN) y++;
    else if (move == TIKU_TEXTVIEW_HOME) x = 0;
    else if (move == TIKU_TEXTVIEW_END) x = INT_MAX;
    tiku_editor_buffer_place(b, y, x);
}
