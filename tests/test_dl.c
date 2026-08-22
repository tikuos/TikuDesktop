/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_dl.c - what was drawn, against what it came out as.
 *
 * The claim the command stream has to make is not that it is small.  It
 * is that it is the SAME PICTURE: a window recorded, sent, and replayed
 * at the far end must come out pixel for pixel what drawing it directly
 * does, or the saving is just a different desktop that happens to be
 * cheaper to send.  So the test draws the Fonts window twice -- once
 * through the drawing calls and once through the stream -- and compares
 * every pixel of the two surfaces.
 *
 * Then, and only then, the size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_dl.h"
#include "tiku_gfx.h"
#include "tiku_ui.h"
#include "tiku_font.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-DL   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

#define W 372
#define H 302

/** @brief A window's worth of drawing, done straight at a surface. */
static void
draw_direct(tiku_surface_t *s)
{
    tiku_rect_t all = { 0, 0, W, H };
    tiku_rect_t list = { 16, 42, W - 32, 66 };
    int i;

    tiku_ui_panel(s, all);
    tiku_text(s, tiku_font_bold(), 16, 22, "Family", TIKU_C_TEXT);
    tiku_ui_sunken(s, list, TIKU_C_DOC);
    for (i = 0; i < 3; i++) {
        tiku_rect_t row = { list.x + 1, list.y + 1 + i * 22,
                                 list.w - 2, 22 };

        tiku_ui_list_row(s, row, i == 0 ? "Sans" : (i == 1 ? "Serif"
                                                          : "Mono"),
                              i == 0);
    }
    tiku_text(s, tiku_font_bold(), 16, 150, "Size", TIKU_C_TEXT);
    tiku_ui_button(s, (tiku_rect_t){ 250, 138, 26, 26 }, "-", 0u);
    tiku_ui_button(s, (tiku_rect_t){ 330, 138, 26, 26 }, "+", 0u);
    (void)tiku_text_centered(s, tiku_font_plain(),
                                  (tiku_rect_t){ 280, 138, 46, 26 },
                                  "12 px", TIKU_C_TEXT);
    tiku_ui_sunken(s, (tiku_rect_t){ 16, 172, W - 32, 52 }, TIKU_C_DOC);
    tiku_text(s, tiku_font_bold(), 26, 190, "TikuOS", TIKU_C_TEXT);
    tiku_text(s, tiku_font_plain(), 26, 210, "Handgloves 0123456789",
                   TIKU_C_TEXT);
    tiku_hline(s, 16, 230, W - 32, tiku_tint(TIKU_C_PANEL, 1.4f));
    tiku_ui_button(s, (tiku_rect_t){ 16, 240, 112, 26 }, "Defaults",
                        TIKU_S_DISABLED);
    tiku_ui_button(s, (tiku_rect_t){ 138, 240, 100, 26 }, "Revert", 0u);
    tiku_ui_checkbox(s, (tiku_rect_t){ 16, 274, 140, 16 }, "Ignore case",
                          TIKU_S_ON);
}

/** @brief The same, recorded.  Kept beside it on purpose: the two must
 *         be read together, and a stream that drifts from the drawing it
 *         mirrors is the failure this whole file exists to catch. */
static void
record(tiku_dl_t *dl)
{
    tiku_rect_t all = { 0, 0, W, H };
    tiku_rect_t list = { 16, 42, W - 32, 66 };
    int i;

    (void)tiku_dl_panel(dl, all);
    (void)tiku_dl_text(dl, TIKU_DL_BOLD, 16, 22, "Family", TIKU_C_TEXT);
    (void)tiku_dl_sunken(dl, list, TIKU_C_DOC);
    for (i = 0; i < 3; i++) {
        tiku_rect_t row = { list.x + 1, list.y + 1 + i * 22,
                                 list.w - 2, 22 };

        (void)tiku_dl_list_row(dl, row, i == 0 ? "Sans"
                                      : (i == 1 ? "Serif" : "Mono"),
                               i == 0);
    }
    (void)tiku_dl_text(dl, TIKU_DL_BOLD, 16, 150, "Size", TIKU_C_TEXT);
    (void)tiku_dl_button(dl, (tiku_rect_t){ 250, 138, 26, 26 }, "-", 0u);
    (void)tiku_dl_button(dl, (tiku_rect_t){ 330, 138, 26, 26 }, "+", 0u);
    (void)tiku_dl_text_centered(dl, TIKU_DL_PLAIN,
                                (tiku_rect_t){ 280, 138, 46, 26 },
                                "12 px", TIKU_C_TEXT);
    (void)tiku_dl_sunken(dl, (tiku_rect_t){ 16, 172, W - 32, 52 },
                         TIKU_C_DOC);
    (void)tiku_dl_text(dl, TIKU_DL_BOLD, 26, 190, "TikuOS", TIKU_C_TEXT);
    (void)tiku_dl_text(dl, TIKU_DL_PLAIN, 26, 210,
                       "Handgloves 0123456789", TIKU_C_TEXT);
    (void)tiku_dl_hline(dl, 16, 230, W - 32,
                        tiku_tint(TIKU_C_PANEL, 1.4f));
    (void)tiku_dl_button(dl, (tiku_rect_t){ 16, 240, 112, 26 }, "Defaults",
                         TIKU_S_DISABLED);
    (void)tiku_dl_button(dl, (tiku_rect_t){ 138, 240, 100, 26 }, "Revert",
                         0u);
    (void)tiku_dl_checkbox(dl, (tiku_rect_t){ 16, 274, 140, 16 },
                           "Ignore case", TIKU_S_ON);
}

static tiku_surface_t *
surface_new(void)
{
    tiku_surface_t *s = (tiku_surface_t *)calloc(1u, sizeof *s);

    s->px = (tiku_rgb_t *)calloc((size_t)W * H, sizeof *s->px);
    s->w = W;
    s->h = H;
    s->scale = 1;
    s->clip = (tiku_rect_t){ 0, 0, W, H };
    return s;
}

static void
surface_free(tiku_surface_t *s)
{
    free(s->px);
    free(s);
}

int
main(void)
{
    tiku_surface_t *direct = surface_new();
    tiku_surface_t *played = surface_new();
    tiku_dl_t *dl = tiku_dl_new();
    tiku_dl_t *back;
    unsigned char *wire;
    size_t n, wrote = 0, got = 0;
    int i, differing = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("what was drawn, against what it came out as (TS-DL)\n");

    draw_direct(direct);
    record(dl);
    check(tiku_dl_count(dl) == 17, "the window is seventeen commands");

    /* Out and back, as it would go down a line. */
    n = tiku_dl_flat_size(dl);
    wire = (unsigned char *)malloc(n);
    check(wire != NULL && tiku_dl_flatten(dl, wire, n, &wrote) &&
          wrote == n, "the stream flattens");
    back = tiku_dl_unflatten(wire, n, &got);
    check(back != NULL && got == n &&
          tiku_dl_count(back) == tiku_dl_count(dl),
          "and comes back with every command still in it");

    check(back != NULL && tiku_dl_play(back, played) == 17,
          "the far end carries out all seventeen");

    for (i = 0; i < W * H; i++) {
        if (direct->px[i] != played->px[i]) {
            differing++;
        }
    }
    check(differing == 0,
          "and the picture is the SAME PICTURE: not one pixel of "
          "112,344 differs from drawing it directly");

    /* Only now, the number the whole exercise is for. */
    {
        size_t frame = (size_t)W * H * sizeof(tiku_rgb_t);

        printf("         %lu bytes against a frame's %lu: %lux smaller\n",
               (unsigned long)n, (unsigned long)frame,
               (unsigned long)(frame / (n > 0u ? n : 1u)));
        check(n * 100u < frame,
              "and it is under a hundredth of the pixels it replaces, "
              "which is what makes a window fit down a serial line");
    }

    /* A stream from a newer end, carrying something this one has never
     * heard of: the length is what lets the rest of the window arrive. */
    {
        unsigned char *mixed = (unsigned char *)malloc(n + 12u);
        tiku_dl_t *tolerant;
        size_t at = 0u;

        mixed[0] = 0xEE; mixed[1] = 0x2A;       /* op 10990, not ours */
        mixed[2] = 0x08; mixed[3] = 0x00;       /* eight bytes of it  */
        memset(mixed + 4, 0x5A, 8u);
        at = 12u;
        memcpy(mixed + at, wire, n);
        tolerant = tiku_dl_unflatten(mixed, n + 12u, NULL);
        check(tolerant != NULL && tiku_dl_count(tolerant) == 18,
              "a command from a newer end is kept, not refused");
        {
            tiku_surface_t *third = surface_new();

            check(tolerant != NULL && tiku_dl_play(tolerant, third) == 17,
                  "stepped over when it is drawn, and the seventeen it "
                  "came with are drawn anyway");
            differing = 0;
            for (i = 0; i < W * H; i++) {
                if (direct->px[i] != third->px[i]) {
                    differing++;
                }
            }
            check(differing == 0, "for the same picture as before");
            surface_free(third);
        }
        tiku_dl_free(tolerant);
        free(mixed);
    }

    /* Bytes from a wire. */
    {
        size_t k;
        int refused = 0;

        for (k = 1u; k < n; k++) {
            tiku_dl_t *t = tiku_dl_unflatten(wire, k, NULL);

            if (t == NULL) {
                refused++;
            } else {
                tiku_surface_t *sc = surface_new();

                (void)tiku_dl_play(t, sc);      /* must not run off it */
                surface_free(sc);
                tiku_dl_free(t);
            }
        }
        check(refused > (int)(n / 2u),
              "a stream cut short is refused rather than half-drawn");
    }
    {
        unsigned char bad[] = { 0x06, 0x00, 0x0A, 0x00,   /* TEXT, 10 */
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 'x' };

        check(tiku_dl_unflatten(bad, sizeof bad, NULL) == NULL,
              "and text that does not end is refused, since the far end "
              "would read past it looking for the end");
        bad[13] = '\0';
        {
            tiku_dl_t *t = tiku_dl_unflatten(bad, sizeof bad, NULL);

            check(t != NULL, "while the same bytes ending are taken");
            tiku_dl_free(t);
        }
    }
    /*
     * Negative coordinates.  A window scrolled, or clipped against the
     * left edge, draws at a negative x every frame -- and i16 was chosen
     * for the SIZE, so the sign is the thing that choice could have
     * quietly cost.
     */
    {
        tiku_dl_t *neg = tiku_dl_new();
        tiku_surface_t *a = surface_new();
        tiku_surface_t *b = surface_new();
        unsigned char buf[128];
        size_t used = 0;
        tiku_dl_t *round;
        int k, off = 0;

        (void)tiku_dl_fill(neg, (tiku_rect_t){ -20, -8, 60, 40 },
                           TIKU_C_SELECT);
        (void)tiku_dl_text(neg, TIKU_DL_PLAIN, -6, 30, "edge",
                           TIKU_C_TEXT);
        tiku_fill(a, (tiku_rect_t){ -20, -8, 60, 40 }, TIKU_C_SELECT);
        tiku_text(a, tiku_font_plain(), -6, 30, "edge", TIKU_C_TEXT);

        check(tiku_dl_flatten(neg, buf, sizeof buf, &used) && used > 0,
              "a stream with negative coordinates flattens");
        round = tiku_dl_unflatten(buf, used, NULL);
        check(round != NULL && tiku_dl_play(round, b) == 2,
              "and plays at the far end");
        for (k = 0; k < W * H; k++) {
            if (a->px[k] != b->px[k]) {
                off++;
            }
        }
        check(off == 0,
              "with the signs intact: drawing off the left and top edges "
              "lands where it did, not 32768 pixels away");
        tiku_dl_free(neg);
        tiku_dl_free(round);
        surface_free(a);
        surface_free(b);
    }
    {
        unsigned char wrong[] = { 0x01, 0x00, 0x04, 0x00, 0, 0, 0, 0 };

        check(tiku_dl_unflatten(wrong, sizeof wrong, NULL) == NULL,
              "a fill claiming four bytes of payload is refused: the "
              "ops we DO know have the sizes we know they have");
    }

    /*
     * And the way an application will actually use it: draw ONCE, into a
     * surface that happens to be recording, and get both.  This is the
     * whole reason the recorder lives in the surface -- the two versions
     * above are written out twice in this file and could drift; a real
     * window has one painting path and cannot.
     */
    {
        tiku_surface_t *once = surface_new();
        tiku_surface_t *from_stream = surface_new();
        tiku_dl_t *caught = tiku_dl_new();
        int off = 0;

        once->record = caught;
        draw_direct(once);              /* the SAME function as at the top */
        once->record = NULL;

        check(tiku_dl_misses(caught) == 0,
              "drawing a window into a recording surface records all of it");
        check(tiku_dl_count(caught) == tiku_dl_count(dl),
              "and writes down exactly the commands recording it by hand "
              "did: no more, so a frame is not carried with the lines it "
              "is made of");
        (void)tiku_dl_play(caught, from_stream);
        for (i = 0; i < W * H; i++) {
            if (once->px[i] != from_stream->px[i]) {
                off++;
            }
        }
        check(off == 0,
              "and replaying what was caught gives back the very surface "
              "it was caught from");
        tiku_dl_free(caught);
        surface_free(once);
        surface_free(from_stream);
    }

    /*
     * A control with no command of its OWN.  It has no fewer for that:
     * a scrollbar is fills and bevels all the way down, and those record.
     * So it costs more bytes than a button does and is drawn exactly the
     * same at the far end -- which is the answer to "what about the
     * controls you did not give an op to".
     */
    {
        tiku_surface_t *drawn = surface_new();
        tiku_surface_t *replayed = surface_new();
        tiku_dl_t *bar = tiku_dl_new();
        int off = 0;

        drawn->record = bar;
        tiku_ui_scrollbar(drawn, (tiku_rect_t){ 4, 4, 12, 100 },
                               0.25f, 0.5f, 0);
        drawn->record = NULL;
        check(tiku_dl_count(bar) > 1 && tiku_dl_misses(bar) == 0,
              "a control with no command of its own records as the "
              "primitives it is made of, and is not counted as missing");
        (void)tiku_dl_play(bar, replayed);
        for (i = 0; i < W * H; i++) {
            if (drawn->px[i] != replayed->px[i]) {
                off++;
            }
        }
        check(off == 0, "and comes out the same scrollbar at the far end");
        tiku_dl_free(bar);
        surface_free(drawn);
        surface_free(replayed);
    }

    /*
     * A face off the four: the status line of a real application is set
     * at eleven, and a stream that could not say so would deliver it in
     * the body size and never mention it.
     */
    {
        tiku_surface_t *a = surface_new();
        tiku_surface_t *b = surface_new();
        tiku_dl_t *odd = tiku_dl_new();
        int off = 0;

        a->record = odd;
        tiku_text(a, tiku_font_at(11), 8, 40, "11 px status", TIKU_C_TEXT);
        tiku_text(a, tiku_font_plain(), 8, 70, "the body size",
                       TIKU_C_TEXT);
        a->record = NULL;
        check(tiku_dl_count(odd) == 2 && tiku_dl_misses(odd) == 0,
              "a face off the four is named by its SIZE, not guessed at");
        (void)tiku_dl_play(odd, b);
        for (i = 0; i < W * H; i++) {
            if (a->px[i] != b->px[i]) {
                off++;
            }
        }
        check(off == 0,
              "and comes back in the letters it was set in, not the "
              "body's");
        tiku_dl_free(odd);
        surface_free(a);
        surface_free(b);
    }

    free(wire);
    tiku_dl_free(dl);
    tiku_dl_free(back);
    surface_free(direct);
    surface_free(played);
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe stream is the same picture, and a fraction of it\n");
    return 0;
}
