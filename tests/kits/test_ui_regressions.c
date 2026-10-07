/*
 * TikuOS kits -- desktop interface tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_ui_regressions.c - bounded text, display-list metadata and controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_dl.h"
#include "tiku_menu.h"
#include "tiku_textview.h"
#include "tiku_ui.h"

static int fails;

/** @brief Record a failed contract check. */
static void
check(int ok, const char *id, const char *what)
{
    if (!ok) {
        printf("FAIL %s: %s\n", id, what);
        fails++;
    }
}

/** @brief Narration lengths count stored bytes, including a truncated footer. */
static void
bounded_narration(void)
{
    tiku_dl_t *dl = tiku_dl_new();
    char buf[128];
    size_t cap, n;
    int facts;

    tiku_dl_miss(dl);
    for (facts = 0; facts < 2; facts++) {
        for (cap = 0; cap <= sizeof buf; cap++) {
            memset(buf, 'X', sizeof buf);
            n = facts ? tiku_dl_facts(dl, buf, cap)
                      : tiku_dl_say(dl, buf, cap);
            check(cap ? n < cap && buf[n] == '\0' && strlen(buf) == n
                      : n == 0, "D-115", "bounded footer and returned length");
            check(cap == sizeof buf || buf[cap] == 'X', "D-115",
                  "no write past capacity");
        }
    }
    tiku_dl_free(dl);
}

/** @brief Appending preserves feature counts and the shared icon-art budget. */
static void
append_metadata(void)
{
    tiku_dl_t *dst = tiku_dl_new(), *src = tiku_dl_new();
    uint32_t pixel = 0xff123456;
    unsigned char art = 0;
    size_t before;
    int i;

    (void)tiku_dl_icon(src, &art, 1, 0, 0, 16, 0, 0);
    (void)tiku_dl_picture(src, 0, 0, 1, 1, &pixel);
    tiku_dl_miss(src);
    check(tiku_dl_append(dst, src) && tiku_dl_icons(dst) == 1 &&
          tiku_dl_pictures(dst) == 1 && tiku_dl_misses(dst) == 1,
          "D-116", "appended features and misses are counted");
    check(tiku_dl_append(dst, src) && tiku_dl_icons(dst) == 2 &&
          tiku_dl_pictures(dst) == 2 && tiku_dl_misses(dst) == 2,
          "D-116", "overlapping art is shared across repeated appends");
    before = tiku_dl_flat_size(dst);
    (void)tiku_dl_icon(dst, &art, 1, 0, 0, 16, 0, 0);
    check(tiku_dl_flat_size(dst) == before + 20, "D-116",
          "appended art is reused without a second definition");
    for (i = 1; i < TIKU_DL_ART_MAX; i++) {
        art = (unsigned char)i;
        check(tiku_dl_icon(dst, &art, 1, 0, 0, 16, 0, 0), "D-116",
              "remaining art slots accept distinct icons");
    }
    art = TIKU_DL_ART_MAX;
    check(!tiku_dl_icon(dst, &art, 1, 0, 0, 16, 0, 0), "D-116",
          "appended definitions count toward the art limit");
    tiku_dl_clear(src);
    (void)tiku_dl_icon(src, &art, 1, 0, 0, 16, 0, 0);
    before = tiku_dl_flat_size(dst);
    check(!tiku_dl_append(dst, src) && tiku_dl_flat_size(dst) == before,
          "D-116", "an over-budget append leaves the destination unchanged");
    tiku_dl_clear(src);
    tiku_dl_miss(src);
    i = tiku_dl_misses(dst);
    check(tiku_dl_append(dst, src) && tiku_dl_misses(dst) == i + 1,
          "D-116", "an empty list still contributes its misses");
    tiku_dl_free(src);
    tiku_dl_free(dst);
}

/** @brief Selected text uses the theme's selection foreground in both themes. */
static void
selected_text(void)
{
    const tiku_font_t *font = tiku_font_plain();
    int dark;

    for (dark = 0; dark < 2; dark++) {
        tiku_surface_t *s = tiku_surface_new(160, 40, 0);
        tiku_surface_t *expected = tiku_surface_new(160, 40, 0);
        tiku_rect_t box = {5, 5, 145, 30};
        tiku_rect_t hl = {8, 7, tiku_text_width(font, "Selected"), 26};

        tiku_theme_set(dark ? tiku_theme_dark() : NULL);
        tiku_ui_textfield_scroll(s, box, "Selected rest", -1, 8, 0, 0, 0);
        tiku_ui_textfield_scroll(expected, box, "Selected rest", -1,
                                  0, 0, 0, 0);
        tiku_fill(expected, hl, TIKU_C_SELECT);
        tiku_clip_set(expected, hl);
        tiku_text(expected, font, 8, 5 + (30 - font->height) / 2 + font->ascent,
                  "Selected rest", TIKU_C_SELTEXT);
        check(!memcmp(s->px, expected->px, 160 * 40 * sizeof *s->px),
              "D-118", "selected glyphs use reverse-video colors");
        tiku_surface_free(expected);
        tiku_surface_free(s);
    }
    tiku_theme_set(NULL);
}

/** @brief Non-finite gauge values are clamped before recording or drawing. */
static void
gauge_values(void)
{
    const float values[] = {NAN, -INFINITY, INFINITY, -1, 2, 0.5f};
    const int expected[] = {0, 0, 1000, 0, 1000, 500};
    tiku_surface_t *s = tiku_surface_new(100, 20, 0);
    tiku_dl_t *dl = tiku_dl_new();
    size_t i;

    s->record = dl;
    for (i = 0; i < sizeof values / sizeof values[0]; i++) {
        unsigned char buf[128];
        size_t at = 0;
        tiku_dl_fact_t f;

        tiku_dl_clear(dl);
        tiku_ui_gauge(s, (tiku_rect_t){0, 0, 100, 20}, values[i]);
        check(tiku_dl_flatten(dl, buf, sizeof buf, NULL) &&
              tiku_dl_read(buf, tiku_dl_flat_size(dl), &at, &f) &&
              f.kind == TIKU_DL_FACT_GAUGE && f.v1 == expected[i],
              "D-119", "gauge records a clamped finite value");
    }
    tiku_surface_free(s);
    tiku_dl_free(dl);
}

/** @brief Every output capacity produces a terminated prefix of the document. */
static void
short_text(void)
{
    tiku_textview_t *tv = calloc(1, sizeof *tv);
    const char *all = "alpha\nbeta\n";
    char buf[16];
    size_t cap;

    tiku_textview_init(tv);
    tiku_textview_set(tv, "alpha\nbeta");
    for (cap = 0; cap <= sizeof buf; cap++) {
        size_t used = cap ? (cap - 1 < 11 ? cap - 1 : 11) : 0;

        memset(buf, 'X', sizeof buf);
        check(tiku_textview_text(tv, buf, cap) == 11, "D-120",
              "required length is independent of capacity");
        check(!cap || (!memcmp(buf, all, used) && buf[used] == '\0'),
              "D-120", "short output contains no uninitialized gaps");
        check(cap == sizeof buf || buf[cap] == 'X', "D-120",
              "text export respects the buffer boundary");
    }
    tiku_textview_free(tv);
    free(tv);
}

/** @brief Removing a menu preserves the open menu's identity or closes it. */
static void
menu_removal(void)
{
    tiku_menu_t menus[3];
    int open, removed, i;

    memset(menus, 0, sizeof menus);
    for (open = -1; open < 3; open++) {
        for (removed = 0; removed < 3; removed++) {
            tiku_menubar_t bar;
            int wanted = open == removed ? -1 : open - (removed < open);

            tiku_menubar_init(&bar);
            for (i = 0; i < 3; i++)
                (void)tiku_menubar_add(&bar, &menus[i]);
            bar.open = open;
            tiku_menubar_remove(&bar, &menus[removed]);
            check(bar.count == 2 && bar.open == wanted, "D-122",
                  "removal before, at and after the open menu");
        }
    }
}

/** @brief Run the six interface regression groups. */
int
main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    bounded_narration();
    append_metadata();
    selected_text();
    gauge_values();
    short_text();
    menu_removal();
    printf("%s: interface regressions\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
