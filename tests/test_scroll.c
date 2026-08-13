/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_scroll.c - the scrollbar as a thing that can be held and detached.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "tiku_trk_scroll.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_trk_scroll_t sb;
    int pos, thick;

    printf("range, thumb and steps (PVL-051)\n");
    tiku_trk_scroll_init(&sb, 20);
    (void)tiku_trk_scroll_set_range(&sb, 100, 25);
    check(tiku_trk_scroll_max(&sb) == 75, "PVL-051",
          "the last value that still shows content is the content less "
          "the window");
    check(tiku_trk_scroll_thumb(&sb, 200, &pos, &thick) && thick == 50 &&
          pos == 0, "PVL-051",
          "the thumb is the visible fraction of the track, at the top when "
          "nothing is scrolled");
    (void)tiku_trk_scroll_to(&sb, 75);
    (void)tiku_trk_scroll_thumb(&sb, 200, &pos, &thick);
    check(pos == 150, "PVL-051",
          "and at the bottom of its travel when the content is");
    (void)tiku_trk_scroll_set_range(&sb, 10000, 25);
    (void)tiku_trk_scroll_thumb(&sb, 200, &pos, &thick);
    check(thick == TIKU_TRK_SCROLL_MIN_THUMB, "PVL-051",
          "a thumb proportional to ten thousand rows is one nobody can "
          "catch, so the proportion has a floor");
    check(tiku_trk_scroll_line(&sb) == 10, "PVL-051",
          "a stepper click is half a line");
    tiku_trk_scroll_init(&sb, 1);
    check(tiku_trk_scroll_line(&sb) == 1, "PVL-051",
          "rounded UP, because half of one line is not none -- a stepper "
          "that moved nothing would look broken");
    tiku_trk_scroll_init(&sb, 20);
    (void)tiku_trk_scroll_set_range(&sb, 100, 25);
    check(tiku_trk_scroll_page(&sb) == 5, "PVL-051",
          "and a page keeps one line of what was on screen, so the eye "
          "has something to land on across the jump");
    check(tiku_trk_scroll_thumb(&sb, 200, &pos, &thick) == 1 &&
          (tiku_trk_scroll_set_range(&sb, 10, 25),
           tiku_trk_scroll_thumb(&sb, 200, &pos, &thick)) == 0, "PVL-051",
          "content that all fits has no thumb at all");

    printf("\nbeing dragged (PVL-051)\n");
    tiku_trk_scroll_init(&sb, 20);
    (void)tiku_trk_scroll_set_range(&sb, 100, 25);
    (void)tiku_trk_scroll_thumb(&sb, 200, &pos, &thick);
    check(tiku_trk_scroll_hit(&sb, 200, pos + 5) ==
          TIKU_TRK_SCROLL_HIT_THUMB &&
          tiku_trk_scroll_hit(&sb, 200, 199) == TIKU_TRK_SCROLL_HIT_DOWN,
          "PVL-051",
          "a press lands on the thumb or on the track beside it, which are "
          "different asks");
    (void)tiku_trk_scroll_press(&sb, 200, 199);
    check(sb.value == 5, "PVL-051",
          "the track pages, rather than jumping to where it was clicked");
    (void)tiku_trk_scroll_to(&sb, 0);
    (void)tiku_trk_scroll_thumb(&sb, 200, &pos, &thick);
    (void)tiku_trk_scroll_press(&sb, 200, pos + 10);
    check(sb.value == 0, "PVL-051",
          "taking hold of the thumb moves nothing by itself");
    check(tiku_trk_scroll_drag(&sb, 200, pos + 10 + 75) == 37, "PVL-051",
          "and the drag moves the content by where the thumb went, keeping "
          "the grip: the content does not jump to put the thumb's top "
          "under the pointer");

    printf("\nwhat a held bar does with a new range (PVL-052)\n");
    check(tiku_trk_scroll_busy(&sb), "PVL-052", "a held bar says so");
    check(tiku_trk_scroll_set_range(&sb, 500, 25) == 0 && sb.total == 100,
          "PVL-052",
          "a range that arrives while the button is down is not taken: it "
          "would make the thumb jump out from under the pointer");
    tiku_trk_scroll_release(&sb);
    check(sb.total == 500, "PVL-052",
          "it is remembered and applied on release -- the content really "
          "did change, and forgetting it would leave the bar describing a "
          "listing that is no longer there");

    printf("\nwhat a bulk re-layout does (PVL-060)\n");
    tiku_trk_scroll_init(&sb, 20);
    (void)tiku_trk_scroll_set_range(&sb, 100, 25);
    tiku_trk_scroll_detach(&sb);
    check(tiku_trk_scroll_set_range(&sb, 40, 25) == 0 &&
          tiku_trk_scroll_set_range(&sb, 900, 25) == 0 && sb.total == 100,
          "PVL-060",
          "every intermediate shape a re-layout passes through is refused, "
          "so none of them feeds back into a view that is halfway rebuilt");
    tiku_trk_scroll_attach(&sb);
    check(sb.total == 900, "PVL-060",
          "and attaching takes the last one, which is the shape the view "
          "actually ended up in");
    (void)tiku_trk_scroll_to(&sb, 800);
    tiku_trk_scroll_detach(&sb);
    (void)tiku_trk_scroll_set_range(&sb, 30, 25);
    tiku_trk_scroll_attach(&sb);
    check(sb.value == 5, "PVL-060",
          "a position past the end of the new content is pulled back when "
          "the range lands, not left pointing past it");

    printf("\n%s\n", fails ? "FAILURES" : "the scrollbar holds");
    return fails ? 1 : 0;
}
