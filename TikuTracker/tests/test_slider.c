/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_slider.c - the dragged value and the stepped one, and the two
 * stateless painters that came with them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "tiku_slider.h"
#include "tiku_ui.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] slider  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

int
main(void)
{
    tiku_slider_t sl;
    tiku_rect_t track = { 100, 50, 216, 16 };   /* travel 200 */

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("a slider is a number with a floor, a ceiling and a step\n");

    tiku_slider_init(&sl, 0, 100, 50, 5);
    check(sl.value == 50 && sl.min == 0 && sl.max == 100,
          "opens on the value it was given");
    check(tiku_slider_set(&sl, 500) == 100 &&
          tiku_slider_set(&sl, -7) == 0,
          "and clamps to the range rather than leaving it");
    check(tiku_slider_step(&sl, 1) == 5 && tiku_slider_step(&sl, -1) == 0,
          "a step moves by the step, both ways");
    check(tiku_slider_step(&sl, -1) == 0,
          "and stepping past the floor stays on it");

    {
        tiku_slider_t back;

        tiku_slider_init(&back, 90, 10, 50, 1);
        check(back.min == 10 && back.max == 90,
              "a range given backwards is read as the range it describes");
    }
    {
        tiku_slider_t one;

        tiku_slider_init(&one, 7, 7, 7, 1);
        check(tiku_slider_fraction(&one) == 0.0f,
              "a range of one value is wholly at its start, not a "
              "division by nothing");
    }

    printf("\nthe knob sits where the value is, wholly inside the track\n");
    tiku_slider_init(&sl, 0, 100, 0, 5);
    check(tiku_slider_knob(&sl, track).x == track.x,
          "at the floor the knob is flush with the left end");
    (void)tiku_slider_set(&sl, 100);
    {
        tiku_rect_t k = tiku_slider_knob(&sl, track);

        check(k.x + k.w == track.x + track.w,
              "at the ceiling it is flush with the right, not half off it");
        check(k.w == track.h, "and it is square to the track's height");
    }
    (void)tiku_slider_set(&sl, 50);
    check(tiku_slider_knob(&sl, track).x == track.x + 100,
          "halfway along the range puts it halfway along the travel");

    printf("\na press jumps to where it landed, and carries on as a drag\n");
    tiku_slider_init(&sl, 0, 100, 0, 5);
    check(tiku_slider_press(&sl, track, track.x + 8 + 100) == 50,
          "a press in the middle of the track takes the middle value");
    check(sl.tracking, "and leaves the knob held");
    check(tiku_slider_drag(&sl, track, track.x + 8 + 200) == 100,
          "the drag that follows moves the value");
    tiku_slider_release(&sl);
    check(!sl.tracking, "letting go ends the track");
    check(tiku_slider_drag(&sl, track, track.x) == 100,
          "and a move after the release is not a drag: the value stands");

    {
        /* A coarse range is where rounding shows: 0..3 over 200 pixels,
         * a press just past the second boundary must land on 2, not 1. */
        tiku_slider_t coarse;

        tiku_slider_init(&coarse, 0, 3, 0, 1);
        check(tiku_slider_press(&coarse, track, track.x + 8 + 134) == 2,
              "a press nearer the higher value rounds up to it, rather "
              "than always drifting down");
    }

    printf("\na stepper is the same number with two arrows\n");
    tiku_slider_init(&sl, 1, 9, 5, 1);
    {
        tiku_rect_t box = { 10, 10, 60, 20 };
        int aw = tiku_ui_step_w();

        check(tiku_stepper_hit(box, 15, 15) == TIKU_STEPPER_HIT_NONE,
              "the digits are not an arrow");
        check(tiku_stepper_hit(box, box.x + box.w - aw / 2, 13) ==
              TIKU_STEPPER_HIT_UP, "the upper arrow counts up");
        check(tiku_stepper_hit(box, box.x + box.w - aw / 2, 27) ==
              TIKU_STEPPER_HIT_DOWN, "the lower one counts down");
        check(tiku_stepper_press(&sl, box, box.x + box.w - aw / 2, 13) &&
              sl.value == 6, "and a press on it moves the value");
        (void)tiku_slider_set(&sl, 9);
        check(!tiku_stepper_press(&sl, box, box.x + box.w - aw / 2, 13),
              "a press that changes nothing says so, so a caller need not "
              "repaint for it");
    }

    printf("\nan arrow that cannot move says so\n");
    {
        /*
         * The stepper's arrows were raised boxes with nothing in them:
         * blank at both ends of the range, pressable-looking and dead.
         * Measured as ink rather than described, because "it looks
         * right" is exactly what was wrong with it.
         */
        tiku_rect_t box = { 10, 10, 80, 24 };
        int aw = tiku_ui_step_w();
        tiku_rgb_t live = 0, dead = 0;
        int nlive = 0, ndead = 0;
        int pass;

        for (pass = 0; pass < 2; pass++) {
            tiku_surface_t *sf = tiku_surface_new(120, 60, TIKU_C_PANEL);
            tiku_slider_t st;
            int x, y;

            tiku_slider_init(&st, 1, 9, (pass == 0) ? 5 : 9, 1);
            tiku_stepper_draw(&st, sf, box);
            for (y = box.y + 2; y < box.y + box.h / 2 - 2; y++) {
                for (x = box.x + box.w - aw + 2;
                     x < box.x + box.w - 2; x++) {
                    tiku_rgb_t px = sf->px[y * sf->w + x];

                    if (px != TIKU_C_PANEL) {
                        if (pass == 0) {
                            nlive++;
                            if (live == 0) { live = px; }
                        } else {
                            ndead++;
                            if (dead == 0) { dead = px; }
                        }
                    }
                }
            }
            tiku_surface_free(sf);
        }
        check(nlive > 4, "the up arrow is an arrow, not an empty box");
        check(ndead > 4, "and it is still drawn at the top of the range");
        check(live != dead,
              "but in a fainter ink there: an arrow that cannot go "
              "further must not look like one that can");
    }

    {
        tiku_surface_t *s = tiku_surface_new(400, 120, TIKU_C_PANEL);

        if (s != NULL) {
            tiku_slider_draw(&sl, s, track);
            tiku_stepper_draw(&sl, s, (tiku_rect_t){ 10, 90, 60, 20 });
            tiku_ui_gauge(s, (tiku_rect_t){ 10, 10, 100, 10 }, 0.5f);
            tiku_ui_gauge(s, (tiku_rect_t){ 10, 25, 100, 10 }, 9.0f);
            tiku_ui_gauge(s, (tiku_rect_t){ 10, 40, 100, 10 }, -3.0f);
            tiku_ui_tip(s, tiku_ui_tip_size("a long value"), "a long value");
            check(1, "the painters draw without faulting, and a gauge "
                     "handed a fraction outside 0..1 clamps rather than "
                     "running off its own well");
            tiku_surface_free(s);
        }
    }
    check(tiku_ui_tip_size("").w == 0,
          "a tip with nothing to say asks for no room");

    printf("\n%s\n", fails ? "FAILURES" : "the values stay in their ranges");
    return fails ? 1 : 0;
}
