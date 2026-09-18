/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gfx.c - surface primitives and the PNG writer.
 *
 * Every primitive clips against the surface's rectangle, so callers draw in
 * widget coordinates and never test bounds themselves.  The PNG writer emits
 * stored (uncompressed) deflate blocks: a few lines of code instead of a zlib
 * dependency, and screenshots are not a size-critical path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dl.h"
#include "tiku_gfx.h"

/*
 * Recording.  A surface may carry a display list, and then every drawing
 * call writes itself down as well as painting -- one pass, two ways of
 * saying what it did, so a stream cannot drift from the picture it is
 * supposed to be of.
 *
 * Only the OUTERMOST call is written down.  A frame is four lines and a
 * line is a fill, so without the depth the list would carry the frame AND
 * everything the frame is made of, and the far end would draw it twice.
 */
int
tiku_gfx_rec_enter(tiku_surface_t *s)
{
    if (s == NULL || s->record == NULL || s->record_depth > 0) {
        return 0;
    }
    s->record_depth++;
    return 1;
}

void
tiku_gfx_rec_miss(tiku_surface_t *s)
{
    /*
     * The one call a painter of PICTURES owes the list.
     *
     * Everything in this kit reduces to commands that record; a picture
     * does not, and a list that quietly leaves one out is worse than no
     * list at all -- it is a window that arrives looking finished and is
     * not.  So whoever writes pixels the vocabulary cannot say says so
     * here, and whoever is about to put the list on a wire sends the
     * frame instead.
     *
     * It lives beside rec_enter because the depth rule is the same one:
     * a picture drawn INSIDE a command that is already being recorded is
     * part of that command, and the outer op carries it.
     */
    if (s != NULL && s->record != NULL && s->record_depth == 0) {
        tiku_dl_miss(s->record);
    }
}

void
tiku_gfx_rec_leave(tiku_surface_t *s, int on)
{
    if (on) {
        s->record_depth--;
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tiku_scale_pixels(tiku_rgb_t *destination,
                       int destination_width, int destination_height,
                       const tiku_rgb_t *source,
                       int source_width, int source_height)
{
    int scale, x, y, repeat;

    if (destination == NULL || source == NULL || destination_width <= 0 ||
        destination_height <= 0 || source_width <= 0 || source_height <= 0) {
        return;
    }
    scale = destination_width / source_width;
    if (scale > 1 && destination_width == source_width * scale &&
        destination_height == source_height * scale) {
        for (y = 0; y < source_height; y++) {
            tiku_rgb_t *row = destination +
                (size_t)y * scale * destination_width;

            for (x = 0; x < source_width; x++) {
                for (repeat = 0; repeat < scale; repeat++) {
                    row[x * scale + repeat] =
                        source[(size_t)y * source_width + x];
                }
            }
            for (repeat = 1; repeat < scale; repeat++) {
                memcpy(row + (size_t)repeat * destination_width, row,
                       (size_t)destination_width * sizeof *row);
            }
        }
        return;
    }
    if (destination_width == source_width &&
        destination_height == source_height) {
        memcpy(destination, source,
               (size_t)source_width * source_height * sizeof *source);
        return;
    }
    for (y = 0; y < destination_height; y++) {
        int source_y = (int)((int64_t)y * source_height /
                             destination_height);

        for (x = 0; x < destination_width; x++) {
            int source_x = (int)((int64_t)x * source_width /
                                 destination_width);

            destination[(size_t)y * destination_width + x] =
                source[(size_t)source_y * source_width + source_x];
        }
    }
}

/** @brief Native pixels per logical pixel; a zeroed surface reads as 1. */
static int
scale_of(const tiku_surface_t *s)
{
    return (s != NULL && s->scale > 1) ? s->scale : 1;
}

tiku_surface_t *
tiku_surface_new(int w, int h, tiku_rgb_t bg)
{
    tiku_surface_t *s;
    long i, n = (long)w * (long)h;   /* scale 1: native is logical */

    if (w <= 0 || h <= 0) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->px = malloc((size_t)n * sizeof *s->px);
    if (s->px == NULL) {
        free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    for (i = 0; i < n; i++) {
        s->px[i] = bg;
    }
    tiku_clip_reset(s);
    return s;
}

int
tiku_surface_resize(tiku_surface_t *s, int w, int h,
                         tiku_rgb_t bg)
{
    tiku_rgb_t *pixels;
    size_t i, count, sc;

    if (s == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    sc = (size_t)scale_of(s);
    if ((size_t)w * sc > SIZE_MAX / sizeof *pixels / ((size_t)h * sc)) {
        return -1;
    }
    count = (size_t)w * sc * (size_t)h * sc;
    pixels = malloc(count * sizeof *pixels);
    if (pixels == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        pixels[i] = bg;
    }
    free(s->px);
    s->px = pixels;
    s->w = w;
    s->h = h;
    s->damage_active = 0;
    tiku_clip_reset(s);
    return 0;
}

int
tiku_surface_rescale(tiku_surface_t *s, int scale,
                          tiku_rgb_t bg)
{
    if (s == NULL || scale < 1 || scale > 4) {
        return -1;
    }
    s->scale = scale;
    return tiku_surface_resize(s, s->w, s->h, bg);
}

tiku_rgb_t
tiku_peek(const tiku_surface_t *s, int x, int y)
{
    int sc = scale_of(s);

    if (s == NULL || s->px == NULL || x < 0 || y < 0 ||
        x >= s->w || y >= s->h) {
        return 0;
    }
    return s->px[(long)y * sc * ((long)s->w * sc) + (long)x * sc];
}

void
tiku_surface_free(tiku_surface_t *s)
{
    if (s != NULL) {
        free(s->px);
        free(s);
    }
}

tiku_rect_t tiku_rect_union(tiku_rect_t a, tiku_rect_t b)
{
    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    int right = a.x+a.w > b.x+b.w ? a.x+a.w : b.x+b.w;
    int bottom = a.y+a.h > b.y+b.h ? a.y+a.h : b.y+b.h;
    if (b.x < a.x) a.x = b.x;
    if (b.y < a.y) a.y = b.y;
    a.w = right-a.x; a.h = bottom-a.y; return a;
}

void
tiku_clip_reset(tiku_surface_t *s)
{
    if (s != NULL && s->record != NULL && s->record_depth == 0) {
        (void)tiku_dl_clip_reset(s->record);
    }
    if (s != NULL) {
        s->clip.x = 0;
        s->clip.y = 0;
        s->clip.w = s->w;
        s->clip.h = s->h;
        if (s->damage_active) tiku_clip_set(s, s->damage);
    }
}

void
tiku_clip_set(tiku_surface_t *s, tiku_rect_t r)
{
    int x0, y0, x1, y1;

    if (s == NULL) {
        return;
    }
    if (s->record != NULL && s->record_depth == 0) {
        (void)tiku_dl_clip_set(s->record, r);
    }
    x0 = (r.x > 0) ? r.x : 0;
    y0 = (r.y > 0) ? r.y : 0;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->w) { x1 = s->w; }
    if (y1 > s->h) { y1 = s->h; }
    if (s->damage_active) {
        if (x0 < s->damage.x) x0 = s->damage.x;
        if (y0 < s->damage.y) y0 = s->damage.y;
        if (x1 > s->damage.x+s->damage.w) x1 = s->damage.x+s->damage.w;
        if (y1 > s->damage.y+s->damage.h) y1 = s->damage.y+s->damage.h;
    }
    s->clip.x = x0;
    s->clip.y = y0;
    s->clip.w = (x1 > x0) ? x1 - x0 : 0;
    s->clip.h = (y1 > y0) ? y1 - y0 : 0;
}

void tiku_clip_damage(tiku_surface_t *s, const tiku_rect_t *region)
{
    if (!s) return;
    s->damage_active = 0;
    tiku_clip_reset(s);
    if (region) {
        tiku_clip_set(s, *region);
        s->damage = s->clip;
        s->damage_active = 1;
    }
}

tiku_rgb_t
tiku_tint(tiku_rgb_t c, float tint)
{
    int i, out[3];
    int ch[3];

    ch[0] = (int)((c >> 16) & 0xFFu);
    ch[1] = (int)((c >> 8) & 0xFFu);
    ch[2] = (int)(c & 0xFFu);
    for (i = 0; i < 3; i++) {
        float v;
        if (tint < 1.0f) {
            v = 255.0f - (255.0f - (float)ch[i]) * tint;
        } else {
            v = (float)ch[i] * (2.0f - tint);
        }
        if (v < 0.0f)   { v = 0.0f; }
        if (v > 255.0f) { v = 255.0f; }
        out[i] = (int)(v + 0.5f);
    }
    return TIKU_RGB(out[0], out[1], out[2]);
}

tiku_rgb_t
tiku_dim(tiku_rgb_t ink, tiku_rgb_t ground)
{
    return TIKU_RGB(
        (((ink >> 16) & 0xFFu) + ((ground >> 16) & 0xFFu)) / 2u,
        (((ink >> 8) & 0xFFu) + ((ground >> 8) & 0xFFu)) / 2u,
        ((ink & 0xFFu) + (ground & 0xFFu)) / 2u);
}

/*
 * The same pinning the board's shim makes (board/gui/tiku_paint_board.c):
 * an ink crosses to a board as a byte, so the numbering is the contract
 * and both ends say what it is.
 */
_Static_assert(TIKU_INK_PLAIN == 0 && TIKU_INK_FAINT == 1 &&
               TIKU_INK_WORD == 2 && TIKU_INK_NUM == 3 &&
               TIKU_INK_STR == 4 && TIKU_INK_REMARK == 5 &&
               TIKU_INK_WRONG == 6 && TIKU_INK_PATH == 7 &&
               TIKU_INK_LINK == 8,
               "the desktop's ink numbering is the board's");

tiku_rgb_t
tiku_ink(tiku_ink_t which)
{
    switch (which) {
    case TIKU_INK_FAINT:
        /* Derived, not named: ordinary ink standing back on the ground it
         * is read against.  A named grey would be right on one table and
         * invisible on the other. */
        return tiku_dim(TIKU_C_TEXT, TIKU_C_DOC);
    case TIKU_INK_WORD:   return TIKU_C_CODE_WORD;
    case TIKU_INK_NUM:    return TIKU_C_CODE_NUM;
    case TIKU_INK_STR:    return TIKU_C_CODE_STR;
    case TIKU_INK_REMARK: return TIKU_C_CODE_REM;
    case TIKU_INK_WRONG:  return TIKU_C_DANGER;
    case TIKU_INK_PATH:
        /* A string that is also an ADDRESS: the string's ink met
         * halfway with the shade the desk gives a thing pointed at, so
         * a path reads as a path on every table and names no fourth
         * colour of its own. */
        return tiku_dim(TIKU_C_CODE_STR, TIKU_C_SELECT);
    case TIKU_INK_LINK:
        /* The shade the desk already gives a thing pointed at.  A table
         * with no hue makes it nearly the document's own ink, which is
         * why a link is underlined as well as tinted. */
        return TIKU_C_SELECT;
    case TIKU_INK_PLAIN:
    default:
        /* An ink this build does not know reads as the document's own,
         * which is legible by construction -- never as nothing. */
        return TIKU_C_TEXT;
    }
}

void
tiku_pixel(tiku_surface_t *s, int x, int y, tiku_rgb_t c)
{
    /* Before the clip test, so a pixel clipped away still marks the list
     * not-whole: the only primitive that forgot the one call a painter
     * of pictures owes, and a marked menu row or a title tab's boxes
     * crossed the wire looking finished with their marks missing. */
    tiku_gfx_rec_miss(s);
    int sc, ry, rx;
    long stride;

    if (s == NULL || x < s->clip.x || y < s->clip.y ||
        x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h) {
        return;
    }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (ry = 0; ry < sc; ry++) {
        tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride +
                               (long)x * sc;
        for (rx = 0; rx < sc; rx++) {
            row[rx] = c;
        }
    }
}

void
tiku_picture(tiku_surface_t *s, int x, int y, int w, int h,
                  const uint32_t *px)
{
    int py, sx;

    if (s == NULL || px == NULL || w < 1 || h < 1) {
        return;
    }
    {
        /*
         * Recorded at the OUTERMOST call only, like every other command:
         * a picture drawn inside a control that is already recording is
         * part of that control.  Entering also puts the pixels below out
         * of the recorder's reach, so they neither double-record nor --
         * since tiku_pixel confesses what it cannot carry -- mark a
         * miss for a picture that WAS carried.
         */
        int rec = tiku_gfx_rec_enter(s);

        if (rec && !tiku_dl_picture(s->record, x, y, w, h, px)) {
            tiku_dl_miss(s->record);
        }
        for (py = 0; py < h; py++) {
            for (sx = 0; sx < w; sx++) {
                uint32_t v = px[(size_t)py * (size_t)w + sx];

                if ((v >> 24) != 0u) {
                    tiku_pixel(s, x + sx, y + py,
                        TIKU_RGB((v >> 16) & 0xffu, (v >> 8) & 0xffu,
                                      v & 0xffu));
                }
            }
        }
        tiku_gfx_rec_leave(s, rec);
    }
}

void
tiku_blit(tiku_surface_t *dst, int x, int y,
               const tiku_surface_t *src)
{
    tiku_gfx_rec_miss(dst);  /* a surface copied in IS a picture */

    int y0, y1, x0, x1, row;

    if (dst == NULL || src == NULL || dst->px == NULL || src->px == NULL) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = x + src->w;
    y1 = y + src->h;
    /* The clip alone: it is held inside the surface, so trimming to it
     * trims to the surface too. */
    if (x0 < dst->clip.x)               { x0 = dst->clip.x; }
    if (y0 < dst->clip.y)               { y0 = dst->clip.y; }
    if (x1 > dst->clip.x + dst->clip.w) { x1 = dst->clip.x + dst->clip.w; }
    if (y1 > dst->clip.y + dst->clip.h) { y1 = dst->clip.y + dst->clip.h; }
    if (x1 <= x0 || y1 <= y0) return;
    if (scale_of(dst) == scale_of(src)) {
        int sc = scale_of(dst), sub;
        long dstride = (long)dst->w * sc;
        long sstride = (long)src->w * sc;

        for (row = y0; row < y1; row++) {
            for (sub = 0; sub < sc; sub++) {
                memcpy(dst->px + ((long)row * sc + sub) * dstride +
                       (long)x0 * sc,
                       src->px + ((long)(row - y) * sc + sub) * sstride +
                       (long)(x0 - x) * sc,
                       (size_t)(x1 - x0) * sc * sizeof *dst->px);
            }
        }
    } else {
        /* Unequal scales: carry each source pixel over logically. */
        int col;
        int ss = scale_of(src);
        long sstride = (long)src->w * ss;

        for (row = y0; row < y1; row++) {
            for (col = x0; col < x1; col++) {
                tiku_pixel(dst, col, row,
                    src->px[(long)(row - y) * ss * sstride +
                            (long)(col - x) * ss]);
            }
        }
    }
}

void
tiku_copy_bits(tiku_surface_t *s, tiku_rect_t src, int dx,
                    int dy)
{
    tiku_gfx_rec_miss(s);   /* moving pixels about is not a drawing the list can say */

    int y, x0, y0, x1, y1, w;

    if (s == NULL || (dx == 0 && dy == 0) || src.w <= 0 || src.h <= 0) {
        return;
    }
    /* Clipped on BOTH ends: the source is read from the surface and the
     * destination is written into the clip, so a block that would run off
     * either is trimmed before a single pixel moves.  The destination
     * answers to the clip like every other way of drawing -- a list that
     * slides its rows must not carry them over its own border. */
    x0 = src.x;
    y0 = src.y;
    x1 = src.x + src.w;
    y1 = src.y + src.h;
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > s->w) { x1 = s->w; }
    if (y1 > s->h) { y1 = s->h; }
    if (x0 + dx < s->clip.x) { x0 = s->clip.x - dx; }
    if (y0 + dy < s->clip.y) { y0 = s->clip.y - dy; }
    if (x1 + dx > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w - dx; }
    if (y1 + dy > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h - dy; }
    w = x1 - x0;
    if (w <= 0 || y1 <= y0) {
        return;
    }
    {
        int sc = scale_of(s), sub;
        long stride = (long)s->w * sc;

        if (dy > 0) {
            /* Downwards: the bottom scanline first, or the copy overwrites
             * the rows it has not read yet. */
            for (y = y1 - 1; y >= y0; y--) {
                for (sub = sc - 1; sub >= 0; sub--) {
                    memmove(s->px + ((long)(y + dy) * sc + sub) * stride +
                            (long)(x0 + dx) * sc,
                            s->px + ((long)y * sc + sub) * stride +
                            (long)x0 * sc,
                            (size_t)w * sc * sizeof *s->px);
                }
            }
        } else {
            for (y = y0; y < y1; y++) {
                for (sub = 0; sub < sc; sub++) {
                    memmove(s->px + ((long)(y + dy) * sc + sub) * stride +
                            (long)(x0 + dx) * sc,
                            s->px + ((long)y * sc + sub) * stride +
                            (long)x0 * sc,
                            (size_t)w * sc * sizeof *s->px);
                }
            }
        }
    }
}

/** @brief One pixel, @p c laid over it at @p a of 255. */
static void
blend_px(tiku_rgb_t *px, tiku_rgb_t c, unsigned a)
{
    unsigned inv = 255u - a;
    tiku_rgb_t p = *px;
    unsigned r = (((c >> 16) & 0xFFu) * a + ((p >> 16) & 0xFFu) * inv) / 255u;
    unsigned g = (((c >> 8) & 0xFFu) * a + ((p >> 8) & 0xFFu) * inv) / 255u;
    unsigned b = ((c & 0xFFu) * a + (p & 0xFFu) * inv) / 255u;

    *px = (tiku_rgb_t)((r << 16) | (g << 8) | b);
}

/**
 * @brief How much of a native pixel a rounded corner covers, 0 to 255.
 *
 * The corner is the quarter disc of radius @p r whose centre is at
 * (@p cx, @p cy); the pixel is the unit square at (@p x, @p y), sampled
 * four by four.  Whole numbers in, a byte out, so the corner of every
 * window costs sixteen compares a pixel and no floating point.
 */
static unsigned
corner_coverage(int x, int y, int cx, int cy, int r)
{
    int i, j, inside = 0;
    long rr = (long)r * r * 64;         /* radius in eighths, squared */

    for (j = 0; j < 4; j++) {
        for (i = 0; i < 4; i++) {
            long dx = (long)(x * 8 + 1 + i * 2) - cx * 8;
            long dy = (long)(y * 8 + 1 + j * 2) - cy * 8;

            if (dx * dx + dy * dy <= rr) { inside++; }
        }
    }
    return (unsigned)(inside * 255 / 16);
}

/**
 * @brief The corners of @p r, each blended by how much the disc covers.
 *
 * With @p ring set, what is drawn is the hairline: the difference
 * between the corner at @p radius and the corner one pixel inside it.
 * Native pixels throughout, so a scaled surface gets a smoother curve
 * rather than a bigger jaggy one.
 */
static void
round_corners(tiku_surface_t *s, tiku_rect_t r, int radius, unsigned corners,
              tiku_rgb_t c, int ring)
{
    int sc = scale_of(s);
    long stride = (long)s->w * sc;
    int rad = radius * sc;
    int x0 = r.x * sc, y0 = r.y * sc;
    int x1 = (r.x + r.w) * sc, y1 = (r.y + r.h) * sc;
    int cx0 = s->clip.x * sc, cy0 = s->clip.y * sc;
    int cx1 = (s->clip.x + s->clip.w) * sc, cy1 = (s->clip.y + s->clip.h) * sc;
    int k, x, y;

    if (rad <= 0 || r.w < 2 * radius || r.h < 2 * radius) {
        return;
    }
    for (k = 0; k < 4; k++) {
        /* Each corner's square of pixels, and the disc's centre for it. */
        int left = (k & 1) ? x1 - rad : x0;

        if (!(corners & (1u << k))) { continue; }

        int top = (k & 2) ? y1 - rad : y0;
        int cx = (k & 1) ? x1 - rad : x0 + rad;
        int cy = (k & 2) ? y1 - rad : y0 + rad;

        for (y = top; y < top + rad; y++) {
            tiku_rgb_t *row;

            if (y < cy0 || y >= cy1) { continue; }
            row = s->px + (long)y * stride;
            for (x = left; x < left + rad; x++) {
                unsigned a;

                if (x < cx0 || x >= cx1) { continue; }
                a = corner_coverage(x, y, cx, cy, rad);
                if (ring) {
                    unsigned in = corner_coverage(x, y, cx, cy, rad - sc);

                    a = (a > in) ? a - in : 0u;
                }
                if (a >= 255u) { row[x] = c; }
                else if (a > 0u) { blend_px(&row[x], c, a); }
            }
        }
    }
}

void
tiku_fill_round(tiku_surface_t *s, tiku_rect_t r, int radius,
                unsigned corners, tiku_rgb_t c)
{
    int rec;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (radius <= 0 || r.w < 2 * radius || r.h < 2 * radius ||
        (corners & TIKU_CORNER_ALL) == 0u) {
        tiku_fill(s, r, c);
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_fill(s->record, r, c);   /* a fill, to the facts */
    }
    /* The cross the corners leave, opaque; a corner kept square is
     * filled whole; the rest are laid by cover. */
    tiku_fill(s, (tiku_rect_t){ r.x + radius, r.y, r.w - 2 * radius, r.h },
              c);
    tiku_fill(s, (tiku_rect_t){ r.x, r.y + radius, radius, r.h - 2 * radius },
              c);
    tiku_fill(s, (tiku_rect_t){ r.x + r.w - radius, r.y + radius, radius,
                                r.h - 2 * radius }, c);
    if (!(corners & TIKU_CORNER_TL)) {
        tiku_fill(s, (tiku_rect_t){ r.x, r.y, radius, radius }, c);
    }
    if (!(corners & TIKU_CORNER_TR)) {
        tiku_fill(s, (tiku_rect_t){ r.x + r.w - radius, r.y, radius, radius },
                  c);
    }
    if (!(corners & TIKU_CORNER_BL)) {
        tiku_fill(s, (tiku_rect_t){ r.x, r.y + r.h - radius, radius, radius },
                  c);
    }
    if (!(corners & TIKU_CORNER_BR)) {
        tiku_fill(s, (tiku_rect_t){ r.x + r.w - radius, r.y + r.h - radius,
                                    radius, radius }, c);
    }
    round_corners(s, r, radius, corners, c, 0);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_frame_round(tiku_surface_t *s, tiku_rect_t r, int radius,
                 unsigned corners, tiku_rgb_t c)
{
    int rec, tl, tr, bl, br;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (radius <= 0 || r.w < 2 * radius || r.h < 2 * radius ||
        (corners & TIKU_CORNER_ALL) == 0u) {
        tiku_frame(s, r, c);
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_frame(s->record, r, c);
    }
    /* Each straight edge runs to the corner, or stops short of a curve. */
    tl = (corners & TIKU_CORNER_TL) ? radius : 0;
    tr = (corners & TIKU_CORNER_TR) ? radius : 0;
    bl = (corners & TIKU_CORNER_BL) ? radius : 0;
    br = (corners & TIKU_CORNER_BR) ? radius : 0;
    tiku_hline(s, r.x + tl, r.y, r.w - tl - tr, c);
    tiku_hline(s, r.x + bl, r.y + r.h - 1, r.w - bl - br, c);
    tiku_vline(s, r.x, r.y + tl, r.h - tl - bl, c);
    tiku_vline(s, r.x + r.w - 1, r.y + tr, r.h - tr - br, c);
    round_corners(s, r, radius, corners, c, 1);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_shadow(tiku_surface_t *s, tiku_rect_t r, int radius, int corner,
            float strength)
{
    int sc, rad, drop, x0, y0, x1, y1, bx0, by0, bx1, by1, x, y, cr;
    long stride;
    unsigned edge;

    tiku_gfx_rec_miss(s);   /* a blend over what is there has no op */
    if (s == NULL || r.w <= 0 || r.h <= 0 || radius <= 0 ||
        strength <= 0.0f) {
        return;
    }
    if (strength > 1.0f) { strength = 1.0f; }
    edge = (unsigned)(strength * 255.0f + 0.5f);
    sc = scale_of(s);
    stride = (long)s->w * sc;
    rad = radius * sc;
    cr = (corner > 0) ? corner * sc : 0;
    drop = 2 * sc;                      /* the light is a little above */
    /* The box the shadow falls from, native, and the band around it. */
    bx0 = r.x * sc; by0 = r.y * sc + drop;
    bx1 = (r.x + r.w) * sc - 1; by1 = (r.y + r.h) * sc - 1 + drop;
    x0 = bx0 - rad; y0 = by0 - rad; x1 = bx1 + rad + 1; y1 = by1 + rad + 1;
    if (x0 < s->clip.x * sc) { x0 = s->clip.x * sc; }
    if (y0 < s->clip.y * sc) { y0 = s->clip.y * sc; }
    if (x1 > (s->clip.x + s->clip.w) * sc) {
        x1 = (s->clip.x + s->clip.w) * sc;
    }
    if (y1 > (s->clip.y + s->clip.h) * sc) {
        y1 = (s->clip.y + s->clip.h) * sc;
    }
    for (y = y0; y < y1; y++) {
        tiku_rgb_t *row = s->px + (long)y * stride;
        int dy = (y < by0) ? by0 - y : (y > by1) ? y - by1 : 0;

        for (x = x0; x < x1; x++) {
            int dx = (x < bx0) ? bx0 - x : (x > bx1) ? x - bx1 : 0;
            long d2, t;
            unsigned a;

            /* Under the window itself: nothing to lay, it is covered --
             * except in the cut of a rounded corner, where the desk
             * shows and the shadow must reach as it does past an edge. */
            if (x >= r.x * sc && x < (r.x + r.w) * sc &&
                y >= r.y * sc && y < (r.y + r.h) * sc) {
                int rx1 = (r.x + r.w) * sc, ry1 = (r.y + r.h) * sc;
                int ex = (x < r.x * sc + cr) ? r.x * sc + cr
                       : (x >= rx1 - cr) ? rx1 - cr - 1 : -1;
                int ey = (y < r.y * sc + cr) ? r.y * sc + cr
                       : (y >= ry1 - cr) ? ry1 - cr - 1 : -1;
                long ccx, ccy, out2;

                if (cr <= 0 || ex < 0 || ey < 0) {
                    continue;
                }
                ccx = x - ex; ccy = y - ey;
                out2 = ccx * ccx + ccy * ccy;
                if (out2 <= (long)cr * cr) {
                    continue;           /* under the curve's own paint */
                }
                /* Past the curve by a little: as dark as the edge is. */
                dx = 0; dy = 0;
            }
            d2 = (long)dx * dx + (long)dy * dy;
            if (d2 >= (long)rad * rad) { continue; }
            /* Falling off as the square of the distance left: dark at
             * the edge, gone at the radius, without a square root. */
            t = (long)rad * rad - d2;
            a = (unsigned)((long)edge * t / rad * t / rad / ((long)rad * rad));
            if (a > 0u) { blend_px(&row[x], 0u, a); }
        }
    }
}

void
tiku_veil(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c, float amount)
{
    tiku_gfx_rec_miss(s);   /* a blend over what is already there has no op */

    int x, y, x0, y0, x1, y1, sc;
    long stride;
    unsigned a, inv, cr, cg, cb;

    if (s == NULL || amount <= 0.0f) {
        return;
    }
    if (amount > 1.0f) {
        amount = 1.0f;
    }
    /*
     * Eight bits of it, so the arithmetic below is integer.  A veil is
     * laid over every pixel of a screen, and doing that in floating
     * point once per channel is the difference between a fade that
     * keeps up and one that stutters.
     */
    a = (unsigned)(amount * 255.0f + 0.5f);
    if (a == 0u) {
        return;
    }
    inv = 255u - a;
    cr = ((c >> 16) & 0xFFu) * a;
    cg = ((c >> 8) & 0xFFu) * a;
    cb = (c & 0xFFu) * a;

    x0 = (r.x > s->clip.x) ? r.x : s->clip.x;
    y0 = (r.y > s->clip.y) ? r.y : s->clip.y;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w; }
    if (y1 > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h; }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (y = y0; y < y1; y++) {
        int ry;

        for (ry = 0; ry < sc; ry++) {
            tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride;

            for (x = x0 * sc; x < x1 * sc; x++) {
                tiku_rgb_t p = row[x];
                unsigned pr = (cr + ((p >> 16) & 0xFFu) * inv) / 255u;
                unsigned pg = (cg + ((p >> 8) & 0xFFu) * inv) / 255u;
                unsigned pb = (cb + (p & 0xFFu) * inv) / 255u;

                row[x] = (tiku_rgb_t)((pr << 16) | (pg << 8) | pb);
            }
        }
    }
}

void
tiku_fill(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    int x, y, x0, y0, x1, y1;
    int rec;

    if (s == NULL) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_fill(s->record, r, c);
    }
    x0 = (r.x > s->clip.x) ? r.x : s->clip.x;
    y0 = (r.y > s->clip.y) ? r.y : s->clip.y;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w; }
    if (y1 > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h; }
    {
        int sc = scale_of(s);
        long stride = (long)s->w * sc;

        for (y = (long)y0 * sc; y < (long)y1 * sc; y++) {
            tiku_rgb_t *row = s->px + (long)y * stride;
            for (x = (long)x0 * sc; x < (long)x1 * sc; x++) {
                row[x] = c;
            }
        }
    }
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_hline(tiku_surface_t *s, int x, int y, int len,
                tiku_rgb_t c)
{
    tiku_rect_t r = { x, y, len, 1 };
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_hline(s->record, x, y, len, c);
    }
    tiku_fill(s, r, c);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_vline(tiku_surface_t *s, int x, int y, int len,
                tiku_rgb_t c)
{
    tiku_rect_t r = { x, y, 1, len };
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_vline(s->record, x, y, len, c);
    }
    tiku_fill(s, r, c);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_frame(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    int rec;

    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_frame(s->record, r, c);
    }
    tiku_hline(s, r.x, r.y, r.w, c);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, c);
    tiku_vline(s, r.x, r.y, r.h, c);
    tiku_vline(s, r.x + r.w - 1, r.y, r.h, c);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_bevel(tiku_surface_t *s, tiku_rect_t r,
                tiku_rgb_t light, tiku_rgb_t shadow)
{
    int rec;

    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_bevel(s->record, r, light, shadow);
    }
    tiku_hline(s, r.x, r.y, r.w - 1, light);
    tiku_vline(s, r.x, r.y, r.h - 1, light);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, shadow);
    tiku_vline(s, r.x + r.w - 1, r.y, r.h, shadow);
    tiku_gfx_rec_leave(s, rec);
}

tiku_rect_t
tiku_inset(tiku_rect_t r, int n)
{
    tiku_rect_t o;

    o.x = r.x + n;
    o.y = r.y + n;
    o.w = r.w - 2 * n;
    o.h = r.h - 2 * n;
    if (o.w < 0) { o.w = 0; }
    if (o.h < 0) { o.h = 0; }
    return o;
}

/*---------------------------------------------------------------------------*/
/* PNG                                                                       */
/*---------------------------------------------------------------------------*/

static unsigned long
png_crc(const unsigned char *b, size_t n, unsigned long crc)
{
    static unsigned long tab[256];
    static int built;
    size_t i;

    if (!built) {
        unsigned long c;
        int k, j;
        for (k = 0; k < 256; k++) {
            c = (unsigned long)k;
            for (j = 0; j < 8; j++) {
                c = (c & 1u) ? (0xEDB88320uL ^ (c >> 1)) : (c >> 1);
            }
            tab[k] = c;
        }
        built = 1;
    }
    for (i = 0; i < n; i++) {
        crc = tab[(crc ^ b[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static void
png_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int
png_chunk(FILE *f, const char *tag, const unsigned char *data, size_t n)
{
    unsigned char hdr[8];
    unsigned char crcbuf[4];
    unsigned long crc;

    png_be32(hdr, (unsigned long)n);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8u) {
        return -1;
    }
    if (n > 0u && fwrite(data, 1, n, f) != n) {
        return -1;
    }
    crc = png_crc((const unsigned char *)tag, 4u, 0xFFFFFFFFuL);
    crc = png_crc(data, n, crc) ^ 0xFFFFFFFFuL;
    png_be32(crcbuf, crc);
    return (fwrite(crcbuf, 1, 4, f) == 4u) ? 0 : -1;
}

int
tiku_surface_png(const tiku_surface_t *s, const char *path)
{
    FILE *f;
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    size_t rawlen, zlen, off, i;
    unsigned long a = 1, b = 0;
    int y, nw, nh, rc = -1;

    if (s == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    {
        int sc = scale_of(s);
        nw = s->w * sc;
        nh = s->h * sc;
    }
    rawlen = (size_t)nh * (1u + (size_t)nw * 3u);
    raw = malloc(rawlen);
    /* Stored deflate: 5 bytes of block header per 65535, plus zlib framing. */
    zlen = 2u + rawlen + 5u * (rawlen / 65535u + 1u) + 4u;
    z = malloc(zlen);
    if (raw == NULL || z == NULL) {
        goto done;
    }
    off = 0;
    for (y = 0; y < nh; y++) {
        int x;
        raw[off++] = 0;                       /* filter: none */
        for (x = 0; x < nw; x++) {
            tiku_rgb_t c = s->px[(long)y * nw + x];
            raw[off++] = (unsigned char)((c >> 16) & 0xFFu);
            raw[off++] = (unsigned char)((c >> 8) & 0xFFu);
            raw[off++] = (unsigned char)(c & 0xFFu);
        }
    }
    for (i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521uL;
        b = (b + a) % 65521uL;
    }
    off = 0;
    z[off++] = 0x78;                          /* zlib: deflate, 32K window */
    z[off++] = 0x01;
    for (i = 0; i < rawlen; ) {
        size_t n = rawlen - i;
        int last;
        if (n > 65535u) { n = 65535u; }
        last = (i + n >= rawlen);
        z[off++] = (unsigned char)(last ? 1 : 0);
        z[off++] = (unsigned char)(n & 0xFFu);
        z[off++] = (unsigned char)((n >> 8) & 0xFFu);
        z[off++] = (unsigned char)(~n & 0xFFu);
        z[off++] = (unsigned char)((~n >> 8) & 0xFFu);
        memcpy(z + off, raw + i, n);
        off += n;
        i += n;
    }
    png_be32(z + off, (b << 16) | a);
    off += 4u;

    if (fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) != 8u) {
        goto done;
    }
    png_be32(ihdr, (unsigned long)nw);
    png_be32(ihdr + 4, (unsigned long)nh);
    ihdr[8] = 8;                              /* bit depth  */
    ihdr[9] = 2;                              /* truecolour */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (png_chunk(f, "IHDR", ihdr, sizeof ihdr) != 0 ||
        png_chunk(f, "IDAT", z, off) != 0 ||
        png_chunk(f, "IEND", NULL, 0u) != 0) {
        goto done;
    }
    rc = 0;
done:
    free(raw);
    free(z);
    (void)fclose(f);
    return rc;
}

/** @brief One inverted pixel, honouring the clip. */
static void
invert_pixel(tiku_surface_t *s, int x, int y)
{
    int sc, ry, rx;
    long stride;

    if (s == NULL || x < s->clip.x || x >= s->clip.x + s->clip.w ||
        y < s->clip.y || y >= s->clip.y + s->clip.h ||
        x < 0 || x >= s->w || y < 0 || y >= s->h) {
        return;
    }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (ry = 0; ry < sc; ry++) {
        tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride +
                               (long)x * sc;
        for (rx = 0; rx < sc; rx++) {
            row[rx] = (tiku_rgb_t)(~row[rx] & 0x00FFFFFFu);
        }
    }
}

void
tiku_invert_frame(tiku_surface_t *s, tiku_rect_t r)
{
    tiku_gfx_rec_miss(s);   /* an inversion is not a colour a command can name */

    int i;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    for (i = 0; i < r.w; i++) {
        invert_pixel(s, r.x + i, r.y);
        invert_pixel(s, r.x + i, r.y + r.h - 1);
    }
    for (i = 1; i < r.h - 1; i++) {
        invert_pixel(s, r.x, r.y + i);
        invert_pixel(s, r.x + r.w - 1, r.y + i);
    }
}
