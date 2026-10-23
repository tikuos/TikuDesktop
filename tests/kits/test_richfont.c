/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richfont.c - native glyph IDs, clipped drawing and layout lifetime.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richfont:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Locate an explicit test face without opening a display or device. */
static const char *
font_path(void)
{
    const char *override = getenv("TIKU_TEST_FONT");
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf"
    };
    size_t i;

    if (override && *override) return override;
    for (i = 0; i < sizeof paths / sizeof paths[0]; i++)
        if (!access(paths[i], R_OK)) return paths[i];
    fputs("Set TIKU_TEST_FONT to a TrueType face for native drawing tests.\n",
          stderr);
    exit(1);
}

/** @brief Drawing by glyph ID matches cmap drawing and refuses invalid IDs. */
static void
glyph_ids(const char *path)
{
    tiku_ttf_t *font = tiku_ttf_open(path);
    tiku_ttf_glyph_t codepoint, index;
    unsigned glyph;
    int size;

    CHECK(font);
    glyph = tiku_ttf_glyph(font, 'A');
    CHECK(glyph && glyph != 'A');
    for (size = 9; size <= 45; size += 9) {
        CHECK(tiku_ttf_render(font, 'A', size, &codepoint));
        CHECK(tiku_ttf_render_glyph(font, glyph, size, &index));
        CHECK(index.w == codepoint.w && index.h == codepoint.h &&
              index.ox == codepoint.ox && index.oy == codepoint.oy &&
              index.adv == codepoint.adv);
        CHECK(!memcmp(index.cover, codepoint.cover,
                      (size_t)index.w * index.h));
        tiku_ttf_free_glyph(&codepoint);
        tiku_ttf_free_glyph(&index);
    }
    CHECK(!tiku_ttf_render_glyph(font, tiku_ttf_glyphs(font), 12, &index));
    CHECK(!index.cover && !index.w);
    CHECK(!tiku_ttf_render_glyph(font, glyph, 513, &index));
    CHECK(tiku_ttf_render_glyph(font, 0, 20, &index));
    tiku_ttf_free_glyph(&index);
    tiku_ttf_close(font);
}

/** @brief Build a formatted layout and retain its fonts past caller release. */
static tiku_richlayout_t *
layout_for(tiku_richfont_t *fonts, const char *text)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_rich_paragraph_source_t p = {.text = text, .length = strlen(text),
        .chars = {.present = TIKU_RICH_SIZE | TIKU_RICH_FONT |
            TIKU_RICH_FOREGROUND | TIKU_RICH_UNDERLINE | TIKU_RICH_HIGHLIGHT,
            .size = 20, .font = "Fixture", .foreground = 0xff174d79,
            .underline = 1, .highlight = 0x4080ddff},
        .list = {TIKU_RICH_LIST_DECIMAL, 0, 12}};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richlayout_options_t options = {.width = 200,
        .provider = tiku_richfont_provider(fonts)};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    CHECK(tiku_richlayout_step(layout, 10000) == TIKU_RICH_OK);
    return layout;
}

/** @brief Render at several scales and verify clipping and repeatability. */
static void
drawing(const char *path)
{
    tiku_richfont_source_t source = {path, 0, "Fixture", 0, 0};
    tiku_richfont_t *fonts;
    tiku_richlayout_t *layout;
    tiku_surface_t *a, *b;
    tiku_rich_line_t line;
    int scale, x, y, ink;

    CHECK(tiku_richfont_open(&source, 1, &fonts) == TIKU_RICH_OK);
    layout = layout_for(fonts, "Abc native glyphs");
    CHECK(!tiku_richlayout_warnings(layout));
    tiku_richfont_release(fonts);
    CHECK(tiku_richlayout_line(layout, 0, &line) == TIKU_RICH_OK);
    CHECK(line.marker_count == 3 && !strcmp(line.marker, "12."));
    for (scale = 1; scale <= 3; scale++) {
        a = tiku_surface_new(260, 160, 0xffffff);
        b = tiku_surface_new(260, 160, 0xffffff);
        CHECK(a && b);
        CHECK(!tiku_surface_rescale(a, scale, 0xffffff));
        CHECK(!tiku_surface_rescale(b, scale, 0xffffff));
        tiku_clip_set(a, (tiku_rect_t){10, 12, 170, 100});
        tiku_clip_set(b, a->clip);
        CHECK(tiku_richfont_draw(fonts, layout, a, 4, 8, 1.25, 0,
              tiku_richlayout_lines(layout)) == TIKU_RICH_OK);
        CHECK(tiku_richfont_draw(fonts, layout, b, 4, 8, 1.25, 0,
              tiku_richlayout_lines(layout)) == TIKU_RICH_OK);
        CHECK(!memcmp(a->px, b->px,
              (size_t)260 * 160 * scale * scale * sizeof *a->px));
        ink = 0;
        for (y = 0; y < a->h * scale; y++)
            for (x = 0; x < a->w * scale; x++) {
                if (x < 10 * scale || y < 12 * scale ||
                    x >= 180 * scale || y >= 112 * scale)
                    CHECK(a->px[(size_t)y * a->w * scale + x] == 0xffffff);
                else if (a->px[(size_t)y * a->w * scale + x] != 0xffffff)
                    ink++;
            }
        CHECK(ink > 100);
        CHECK(tiku_richfont_draw(fonts, layout, a, NAN, 0, 1, 0, 1) ==
              TIKU_RICH_INVALID);
        CHECK(tiku_richfont_draw(fonts, layout, a, 0, 0, 1, 999, 1) ==
              TIKU_RICH_INVALID_RANGE);
        tiku_surface_free(a);
        tiku_surface_free(b);
    }
    tiku_richlayout_release(layout);
}

/** @brief Complex scripts and unavailable glyphs report explicit warnings. */
static void
diagnostics(const char *path)
{
    tiku_richfont_source_t source = {path, 0, "Fixture", 0, 0};
    tiku_richfont_t *fonts;
    tiku_richlayout_t *layout;

    CHECK(tiku_richfont_open(&source, 1, &fonts) == TIKU_RICH_OK);
    layout = layout_for(fonts, "a\xcc\x81 \xd8\xa7\xd8\xa8");
    CHECK(tiku_richlayout_warnings(layout) & TIKU_RICH_LAYOUT_SHAPING);
    tiku_richlayout_release(layout);
    layout = layout_for(fonts, "\xe2\x80\xae" "abc");
    CHECK(tiku_richlayout_warnings(layout) & TIKU_RICH_LAYOUT_SHAPING);
    tiku_richlayout_release(layout);
    layout = layout_for(fonts, "\xf4\x8f\xbf\xbd");
    CHECK(tiku_richlayout_warnings(layout) & TIKU_RICH_LAYOUT_FALLBACK);
    tiku_richlayout_release(layout);
    tiku_richfont_release(fonts);
    source.path = "/nonexistent/tiku-document-font.ttf";
    CHECK(tiku_richfont_open(&source, 1, &fonts) == TIKU_RICH_NOT_FOUND);
    CHECK(!fonts);
}

/** @brief Refuse another provider's glyph IDs without touching destination. */
static void
provider_identity(const char *path)
{
    tiku_richfont_source_t source = {path, 0, "Fixture", 0, 0};
    tiku_richfont_t *a, *b;
    tiku_richlayout_t *layout;
    tiku_surface_t *surface = tiku_surface_new(100, 100, 0xffffff);
    size_t i;

    CHECK(surface);
    CHECK(tiku_richfont_open(&source, 1, &a) == TIKU_RICH_OK);
    CHECK(tiku_richfont_open(&source, 1, &b) == TIKU_RICH_OK);
    layout = layout_for(a, "abc");
    CHECK(tiku_richfont_draw(b, layout, surface, 0, 0, 1, 0, 1) ==
          TIKU_RICH_INVALID);
    for (i = 0; i < 10000; i++) CHECK(surface->px[i] == 0xffffff);
    tiku_richfont_release(a);
    tiku_richfont_release(b);
    tiku_richlayout_release(layout);
    tiku_surface_free(surface);
}

/** @brief View painting transforms selection and caret, restoring outer clips. */
static void
view_drawing(const char *path)
{
    tiku_richfont_source_t source = {path, 0, "Fixture", 0, 0};
    tiku_richfont_t *fonts;
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 60};
    tiku_rich_paragraph_source_t paragraph = {.text = "abc def ghi jkl mno",
        .length = 19};
    tiku_rich_source_t input = {.paragraphs = &paragraph, .paragraph_count = 1};
    int scale;

    CHECK(tiku_richfont_open(&source, 1, &fonts) == TIKU_RICH_OK);
    options.provider = tiku_richfont_provider(fonts);
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &input, 0) == TIKU_RICH_OK);
    CHECK(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    for (scale = 1; scale <= 3; scale++) {
        tiku_surface_t *surface = tiku_surface_new(100, 100, 0xffffff);
        tiku_rect_t clip = {5, 5, 80, 80}, viewport = {10, 10, 60, 40};
        tiku_rich_pos_t a, b;
        tiku_rich_rect_t caret;
        int x, y, painted = 0;
        uint32_t *pixels;

        CHECK(surface && !tiku_surface_rescale(surface, scale, 0xffffff));
        CHECK(tiku_richview_viewport(view, 60, 40, 1.5) == TIKU_RICH_OK);
        CHECK(tiku_richview_reflow(view) == TIKU_RICH_OK);
        CHECK(tiku_richfont_draw_view(fonts, view, surface, viewport,
            0x703080ff, 0xff0000, 1) == TIKU_RICH_PENDING);
        for (x = 0; x < 10000 * scale * scale; x++)
            CHECK(surface->px[x] == 0xffffff);
        CHECK(tiku_richview_step(view, 10000) == TIKU_RICH_OK);
        CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_START, 0) <=
              TIKU_RICH_NO_CHANGE);
        CHECK(tiku_richsession_selection(tiku_richview_session(view),
                                          &a, &b) == TIKU_RICH_OK);
        CHECK(tiku_richlayout_caret(tiku_richview_layout(view), b, &caret) == 0);
        tiku_clip_set(surface, clip);
        CHECK(tiku_richfont_draw_view(fonts, view, surface, viewport,
            0x703080ff, 0xff0000, 1) == TIKU_RICH_OK);
        CHECK(!memcmp(&clip, &surface->clip, sizeof clip));
        x = (int)(10 + caret.x * 1.5) * scale;
        y = (int)(10 + caret.y * 1.5 + 2) * scale;
        CHECK(surface->px[(size_t)y * surface->w * scale + x] == 0xff0000);
        CHECK(tiku_richview_select_all(view) == TIKU_RICH_OK);
        CHECK(tiku_richsession_selection(tiku_richview_session(view),
                                          &a, &b) == 0);
        CHECK(tiku_richsession_select(tiku_richview_session(view), 0, b, a) == 0);
        CHECK(tiku_richview_scroll(view, 12) == 0);
        tiku_fill(surface, clip, 0xffffff);
        pixels = malloc((size_t)10000 * scale * scale * sizeof *pixels);
        CHECK(pixels);
        CHECK(tiku_richfont_draw_view(fonts, view, surface, viewport,
            0, 0xff0000, 1) == TIKU_RICH_OK);
        memcpy(pixels, surface->px,
               (size_t)10000 * scale * scale * sizeof *pixels);
        tiku_fill(surface, clip, 0xffffff);
        CHECK(tiku_richfont_draw_view(fonts, view, surface, viewport,
            0x703080ff, 0xff0000, 1) == TIKU_RICH_OK);
        for (y = 0; y < 100 * scale; y++)
            for (x = 0; x < 100 * scale; x++) {
                size_t index = (size_t)y * 100 * scale + x;

                if (x < 10 * scale || y < 10 * scale ||
                    x >= 70 * scale || y >= 50 * scale)
                    CHECK(surface->px[index] == 0xffffff);
                if (surface->px[index] != pixels[index]) painted++;
                CHECK(surface->px[index] != 0xff0000);
            }
        CHECK(painted > 100);
        CHECK(!memcmp(&clip, &surface->clip, sizeof clip));
        free(pixels);
        tiku_surface_free(surface);
    }
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
    tiku_richfont_release(fonts);
}

/** @brief Run native font tests without an X server or connected device. */
int
main(void)
{
    const char *path = font_path();

    glyph_ids(path);
    drawing(path);
    diagnostics(path);
    provider_identity(path);
    view_drawing(path);
    puts("PASS richfont (glyph IDs, 3 scales, clips, lifetime, diagnostics)");
    return 0;
}
