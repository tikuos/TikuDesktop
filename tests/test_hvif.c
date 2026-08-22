/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_hvif.c - every embedded icon, parsed and rasterised at 16/32/64.
 *
 * The parser already refuses a blob it does not consume to the final byte,
 * so a successful parse is a real structural check rather than a smoke
 * test.  What this file adds is the rasteriser's side: ink has to exist,
 * cover a plausible slice of the canvas, and reach across it -- an icon
 * that collapsed into one corner or flooded the whole square passes a
 * "some pixels are set" test and fails these.
 *
 * Writes build/icons.png, all 34 icons at 64 px over a light grey checker
 * so the alpha edges are visible.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_gfx.h"
#include "tiku_hvif.h"
#include "tiku_iconpaint.h"
#include "tiku_icons.h"

/* An icon must put ink on at least this share of its square, and must not
 * flood it: the Tracker set measures 0.20 to 0.68 at 64 px. */
#define INK_MIN 0.08f
#define INK_MAX 0.95f

/* ...and the ink has to span the canvas, not huddle in a corner. */
#define SPAN_MIN 0.40f

#define SHEET_COLS 8
#define SHEET_CELL 80
#define SHEET_PAD  8

static int fails;
static const int sizes[3] = { 16, 32, 64 };

static void
check(int cond, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %s\n", what);
        fails++;
    }
}

/** @brief Ink share, and the fraction of each axis the ink spans. */
typedef struct {
    float ink;
    float span_x, span_y;
    int   lit;
} stat_t;

static stat_t
measure(const uint32_t *bmp, int size)
{
    int x, y, x0 = size, y0 = size, x1 = -1, y1 = -1;
    stat_t s;

    memset(&s, 0, sizeof s);
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            /* Count a pixel as ink only once it would actually read on the
             * panel; a stray 1/255 does not make an icon. */
            if (((bmp[(size_t)y * (size_t)size + x] >> 24) & 0xFFu) < 8u) {
                continue;
            }
            s.lit++;
            if (x < x0) { x0 = x; }
            if (y < y0) { y0 = y; }
            if (x > x1) { x1 = x; }
            if (y > y1) { y1 = y; }
        }
    }
    s.ink = (float)s.lit / (float)(size * size);
    if (x1 >= 0) {
        s.span_x = (float)(x1 - x0 + 1) / (float)size;
        s.span_y = (float)(y1 - y0 + 1) / (float)size;
    }
    return s;
}

/** @brief A light grey checker, so a transparent pixel is unmistakable. */
static void
checker(tiku_surface_t *s)
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            s->px[(size_t)y * (size_t)s->w + x] =
                (((x >> 3) + (y >> 3)) & 1) ? TIKU_RGB(216, 216, 216)
                                            : TIKU_RGB(200, 200, 200);
        }
    }
}

int
main(void)
{
    tiku_surface_t *sheet;
    uint32_t *bmp;
    int n = tiku_icons_count();
    int parsed = 0, rendered = 0, rows, i, k;
    float ink_lo = 1.0f, ink_hi = 0.0f, ink_sum = 0.0f;

    printf("HVIF: %d embedded icons\n\n", n);
    printf("  %-18s %6s %6s %6s %6s\n", "icon", "bytes", "ink16", "ink32",
           "ink64");

    rows = (n + SHEET_COLS - 1) / SHEET_COLS;
    sheet = tiku_surface_new(SHEET_COLS * SHEET_CELL + 2 * SHEET_PAD,
                                  rows * SHEET_CELL + 2 * SHEET_PAD,
                                  TIKU_C_PANEL);
    if (sheet == NULL) {
        printf("no surface\n");
        return 1;
    }
    checker(sheet);

    bmp = malloc(64u * 64u * sizeof *bmp);
    if (bmp == NULL) {
        printf("no bitmap\n");
        return 1;
    }

    for (i = 0; i < n; i++) {
        const tiku_icon_t *ic = tiku_icons_at(i);
        char err[TIKU_HVIF_ERRLEN];
        tiku_hvif_t *icon;
        float ink[3];
        char what[128];

        icon = tiku_hvif_parse(ic->data, ic->len, err, sizeof err);
        snprintf(what, sizeof what, "%s parses (%s)", ic->name,
                 icon != NULL ? "ok" : err);
        check(icon != NULL, what);
        if (icon == NULL) {
            continue;
        }
        parsed++;

        for (k = 0; k < 3; k++) {
            int size = sizes[k];
            stat_t st;

            check(tiku_hvif_render(icon, bmp, size, size) == 0,
                  "render returns 0");
            st = measure(bmp, size);
            ink[k] = st.ink;

            snprintf(what, sizeof what, "%s at %d: some pixel has alpha",
                     ic->name, size);
            check(st.lit > 0, what);
            snprintf(what, sizeof what,
                     "%s at %d: ink %.3f is inside [%.2f, %.2f]",
                     ic->name, size, (double)st.ink, (double)INK_MIN,
                     (double)INK_MAX);
            check(st.ink >= INK_MIN && st.ink <= INK_MAX, what);
            snprintf(what, sizeof what,
                     "%s at %d: ink spans %.2f x %.2f of the square",
                     ic->name, size, (double)st.span_x, (double)st.span_y);
            check(st.span_x >= SPAN_MIN && st.span_y >= SPAN_MIN, what);

            if (size == 64) {
                int cx = SHEET_PAD + (i % SHEET_COLS) * SHEET_CELL +
                         (SHEET_CELL - 64) / 2;
                int cy = SHEET_PAD + (i / SHEET_COLS) * SHEET_CELL +
                         (SHEET_CELL - 64) / 2;

                tiku_hvif_blit(sheet, cx, cy, bmp, 64, 64);
                if (st.ink < ink_lo) { ink_lo = st.ink; }
                if (st.ink > ink_hi) { ink_hi = st.ink; }
                ink_sum += st.ink;
            }
            rendered++;
        }
        printf("  %-18s %6zu %6.3f %6.3f %6.3f\n", ic->name, ic->len,
               (double)ink[0], (double)ink[1], (double)ink[2]);
        tiku_hvif_free(icon);
    }

    /* The cache has to hand back the same picture, and hand back the same
     * pointer the second time. */
    {
        const uint32_t *a = tiku_icons_bitmap("folder", 32);
        const uint32_t *b = tiku_icons_bitmap("folder", 32);

        check(a != NULL && a == b, "the cache renders once per size");
        check(tiku_icons_bitmap("no_such_icon", 32) == NULL,
              "an unknown name has no bitmap");
        check(tiku_icons_find("trash_full") != NULL,
              "find() reaches the whole table");
        tiku_icons_flush();
    }

    /* A cache with a slot count and no byte budget is not bounded: one
     * 1024 px icon is four megabytes, and the slots hold four of them per
     * icon in a table of thirty-four (IV-012). */
    {
        static const char *const names[] = { "folder", "file", "trash_full",
                                             "query", "volume" };
        size_t i;

        tiku_icons_flush();
        check(tiku_icons_cached_bytes() == 0,
              "a flushed cache holds nothing");
        for (i = 0; i < sizeof names / sizeof names[0]; i++) {
            (void)tiku_icons_bitmap(names[i], 256);
        }
        check(tiku_icons_cached_bytes() <= TIKU_ICONS_BUDGET,
              "five icons at 256 px is a megabyte and a quarter, and the "
              "cache gives up its oldest renders rather than growing to "
              "hold them all");
        check(tiku_icons_cached_bytes() > 0,
              "it does not empty itself either: the newest renders are the "
              "ones a window is drawing");
        check(!tiku_icons_cached(names[0], 256) &&
              tiku_icons_cached(names[4], 256),
              "and what it gave up is the OLDEST render, not the one just "
              "asked for: dropping the newest would throw away the art the "
              "window is drawing this frame");
        check(tiku_icons_bitmap("folder", 32) != NULL,
              "an eviction does not break the next render");
        {
            /* The pointer is borrowed until the next flush, so one render
             * larger than the whole budget still has to live somewhere. */
            const uint32_t *huge = tiku_icons_bitmap("folder", 1024);

            check(huge != NULL, "an icon larger than the budget renders");
            check(tiku_icons_cached_bytes() <=
                  TIKU_ICONS_BUDGET + 1024u * 1024u * 4u,
                  "and it is the only thing over the budget: everything "
                  "else was given up to make room for it");
        }
        tiku_icons_flush();
        check(tiku_icons_cached_bytes() == 0, "and a flush empties it");
    }

    /* Nonzero winding, on hand-built geometry.  An inner subpath wound the
     * other way is the only way HVIF cuts a hole, and the ink measurements
     * above cannot see it: filling the cutout solid moves coverage by less
     * than a thousandth across the whole set, while the gear, the counter
     * of the "A" and the centre of the CD all silently close up. */
    {
        static const uint8_t holed[] = {
            0x6E, 0x63, 0x69, 0x66,
            1, 3, 0, 0, 0,                       /* one solid black style */
            2,
            0x0A, 4, 32, 32, 96, 32, 96, 96, 32, 96,      /* outer, one way */
            0x0A, 4, 48, 48, 48, 80, 80, 80, 80, 48,      /* inner, other   */
            1, 10, 0, 2, 0, 1, 0x00              /* one shape, both paths */
        };
        tiku_hvif_t *icon = tiku_hvif_parse(holed, sizeof holed,
                                                    NULL, 0);

        check(icon != NULL, "a hand-built blob parses");
        if (icon != NULL) {
            check(tiku_hvif_render(icon, bmp, 64, 64) == 0,
                  "the hand-built blob renders");
            check(((bmp[32 * 64 + 32] >> 24) & 0xFFu) == 0u,
                  "a counter-wound subpath cuts a hole");
            check(((bmp[36 * 64 + 4] >> 24) & 0xFFu) == 255u,
                  "the outer subpath is still solid");
            /* Coordinates 32 and 96 are 0 and 64 in canvas units, so the
             * outer square is the whole square: the decode is checked too. */
            check(((bmp[0] >> 24) & 0xFFu) == 255u,
                  "coordinate 32 decodes to canvas 0");
            tiku_hvif_free(icon);
        }
    }

    /* A blob that is not HVIF must be refused, not half-drawn. */
    {
        static const uint8_t junk[8] = { 'n', 'c', 'i', 'f', 9, 9, 9, 9 };
        char err[TIKU_HVIF_ERRLEN];

        check(tiku_hvif_parse(junk, sizeof junk, err, sizeof err) == NULL,
              "a truncated blob is refused");
        check(tiku_hvif_parse("xxxx", 4, err, sizeof err) == NULL,
              "a bad magic is refused");
    }

    /* Every proper prefix of a real icon is a truncated icon and must be
     * refused.  Cutting exactly at a section's count byte is the case that
     * looks valid to a parser which only checks that it consumed everything:
     * the count reads as zero, the section is empty, and nothing is left
     * over.  Such a blob renders blank rather than reporting a problem. */
    {
        int bad_accepted = 0, prefixes = 0, i;

        for (i = 0; i < tiku_icons_count(); i++) {
            const tiku_icon_t *ic = tiku_icons_at(i);
            size_t cut;

            for (cut = 1; cut < ic->len; cut++) {
                char err[TIKU_HVIF_ERRLEN];
                struct tiku_hvif *t;

                t = tiku_hvif_parse(ic->data, cut, err, sizeof err);
                prefixes++;
                if (t != NULL) {
                    if (bad_accepted == 0) {
                        printf("  first accepted truncation: %s cut to %zu of "
                               "%zu bytes\n", ic->name, cut, ic->len);
                    }
                    bad_accepted++;
                    tiku_hvif_free(t);
                }
            }
        }
        printf("  %d truncated blobs tried, %d accepted\n", prefixes,
               bad_accepted);
        check(bad_accepted == 0, "no truncation of a real icon is accepted");
    }

    if (tiku_surface_png(sheet, "build/icons.png") != 0) {
        printf("\ncould not write build/icons.png\n");
        fails++;
    }
    tiku_surface_free(sheet);
    free(bmp);

    printf("\n%d/%d parsed, %d renders, ink at 64px: min %.3f mean %.3f "
           "max %.3f\n", parsed, n, rendered, (double)ink_lo,
           (double)(parsed ? ink_sum / (float)parsed : 0.0f), (double)ink_hi);
    printf("contact sheet: build/icons.png\n");
    {   /* An icon is not a rectangle: a drag must start only from a pixel
         * that is part of the picture, or the gap between two icons
         * becomes draggable (IV-057). */
        int x, y, opaque = 0, clear = 0;

        for (y = 0; y < 32; y++) {
            for (x = 0; x < 32; x++) {
                if (tiku_icon_hit("folder", 32, x, y)) {
                    opaque++;
                } else {
                    clear++;
                }
            }
        }
        printf("\nicon hit testing (IV-057)\n");
        if (opaque == 0 || clear == 0) {
            printf("  [FAIL] IV-057    a folder icon is neither wholly "
                   "opaque nor wholly empty (%d/%d)\n", opaque, clear);
            fails++;
        } else {
            printf("  [PASS] IV-057    a folder icon has both opaque and "
                   "transparent pixels (%d opaque, %d clear)\n", opaque,
                   clear);
        }
        if (tiku_icon_hit("folder", 32, 40, 40)) {
            printf("  [FAIL] IV-057    a point outside the icon hits it\n");
            fails++;
        } else {
            printf("  [PASS] IV-057    a point outside the icon does not "
                   "hit it\n");
        }
        if (!tiku_icon_hit("no-such-icon", 32, 3, 3)) {
            printf("  [FAIL] IV-057    an icon with no art cannot be "
                   "picked up at all\n");
            fails++;
        } else {
            printf("  [PASS] IV-057    an icon with no art answers over its "
                   "whole square, rather than being unpickable\n");
        }
    }


    {   /* IV-019: the selected look is derived once and CACHED. */
        size_t before, after_one, after_two;
        const uint32_t *w1, *w2, *plain;

        plain = tiku_icons_bitmap("folder", 32);
        check(plain != NULL, "IV-019: the plain render exists");
        before = tiku_icons_cached_bytes();
        w1 = tiku_icons_bitmap_washed("folder", 32, 0x336698u, 89u);
        after_one = tiku_icons_cached_bytes();
        w2 = tiku_icons_bitmap_washed("folder", 32, 0x336698u, 89u);
        after_two = tiku_icons_cached_bytes();
        check(w1 != NULL && w1 == w2, "IV-019: the second ask is the same "
              "bitmap, not a second derivation");
        check(tiku_icons_washed_builds() == 1u,
              "IV-019: derived ONCE -- the second ask cost nothing");
        check(after_one > before && after_two == after_one,
              "IV-019: it is stored once and accounted once");
        {   /* An opaque pixel is the wash formula exactly. */
            size_t k, n = 32u * 32u;
            int held = 1;

            for (k = 0; k < n; k++) {
                uint32_t src = plain[k];

                if (((src >> 24) & 0xffu) == 0xffu) {
                    unsigned r = (((src >> 16) & 0xffu) * (255u - 89u) +
                                  0x33u * 89u) / 255u;

                    if (((w1[k] >> 16) & 0xffu) != r) { held = 0; }
                }
            }
            check(held, "IV-019: an opaque pixel is the wash formula, so "
                  "the cached look equals the recomputed one");
        }
        {   /* IV-018: the SELECTED look is the source's exact transform --
             * every channel at 66%% brightness (168/255), alpha intact.  A
             * black wash at 87/255 must therefore equal the multiply. */
            const uint32_t *sel =
                tiku_icons_bitmap_washed("folder", 32, 0x000000u, 87u);
            size_t k, n = 32u * 32u;
            int held = (sel != NULL);

            for (k = 0; sel != NULL && k < n; k++) {
                uint32_t src = plain[k];

                if (((src >> 24) & 0xffu) == 0xffu) {
                    unsigned r = ((src >> 16) & 0xffu) * 168u / 255u;

                    if (((sel[k] >> 16) & 0xffu) != r) { held = 0; }
                }
                if (((sel[k] >> 24) & 0xffu) !=
                    ((src >> 24) & 0xffu)) { held = 0; }
            }
            check(held, "IV-018: selected = 66%% brightness with the "
                  "transparency intact, the source's own transform");
        }
        {   /* And THROUGH the selected paint itself, not just the wash
             * primitive: an opaque pixel painted selected must land at
             * exactly 66%% of the plain paint. */
            tiku_surface_t *ps = tiku_surface_new(80, 40, TIKU_C_PANEL);
            size_t k, n = 32u * 32u;
            int held = (ps != NULL);
            int probed = 0;

            if (ps != NULL) {
                (void)tiku_icon_paint(ps, "folder", 0, 0, 32);
                (void)tiku_icon_paint_selected(ps, "folder", 40, 0, 32);
                for (k = 0; k < n; k++) {
                    if (((plain[k] >> 24) & 0xffu) == 0xffu) {
                        int px = (int)(k % 32u), py = (int)(k / 32u);
                        uint32_t a = ps->px[(size_t)py * (size_t)ps->w +
                                            (size_t)px];
                        uint32_t b = ps->px[(size_t)py * (size_t)ps->w +
                                            (size_t)px + 40u];

                        probed = 1;
                        if (((b >> 16) & 0xffu) !=
                                ((a >> 16) & 0xffu) * 168u / 255u ||
                            ((b >> 8) & 0xffu) !=
                                ((a >> 8) & 0xffu) * 168u / 255u ||
                            (b & 0xffu) != (a & 0xffu) * 168u / 255u) {
                            held = 0;
                        }
                    }
                }
                tiku_surface_free(ps);
            }
            check(held && probed, "IV-018: the selected PAINT is the 66%% "
                  "multiply of the plain paint, pixel for pixel");
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "every icon parses and renders");
    return fails ? 1 : 0;
}
