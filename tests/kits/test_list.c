/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_list.c - the list widget: what a click and a key do to a
 * selection, and what the ANCHOR does, which is the part nobody notices
 * until it is wrong.
 *
 * A list that gets selection wrong is not obviously broken -- it picks
 * rows, it draws them lit -- it is just subtly not what the hands
 * expect: a second shift-click that grows the wrong way, a range that
 * clears what it was reaching out to add, an anchor left pointing at a
 * row that no longer exists.  So most of what is pinned here is the
 * pivot, and the rule that an empty selection has no anchors at all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_dl.h"
#include "tiku_list.h"
#include "tiku_list_paint.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] list    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief The selection as a string of dots and stars, for one look. */
static const char *
shape(const tiku_list_t *l)
{
    static char s[TIKU_LIST_MAX + 1];
    int i;

    for (i = 0; i < l->count && i < TIKU_LIST_MAX; i++) {
        s[i] = tiku_list_selected(l, i) ? '*' : '.';
    }
    s[i] = '\0';
    return s;
}

static void
same(const tiku_list_t *l, const char *want, const char *what)
{
    const char *got = shape(l);

    check(strcmp(got, want) == 0, what);
    if (strcmp(got, want) != 0) {
        printf("         want: %s\n         got:  %s\n", want, got);
    }
}

static const char *const NAMES[] = {
    "alpha", "beta", "Gamma", "delta", "epsilon",
    "zeta", "eta", "theta", "iota", "kappa"
};

static const char *
name_of(void *ctx, int row)
{
    (void)ctx;
    return (row >= 0 && row < 10) ? NAMES[row] : NULL;
}

int
main(void)
{
    tiku_list_t l;
    tiku_rect_t body = { 100, 50, 200, 80 };   /* 4 rows at 20 px */

    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("a list starts empty, and says so\n");
    tiku_list_init(&l, 0);
    tiku_list_set_row_h(&l, 20);
    check(tiku_list_count(&l) == 0 && tiku_list_chosen(&l) == -1,
          "no rows, nothing chosen");
    check(l.pivot == -1 && l.last_added == -1 && l.cursor == -1,
          "and no anchors: there is nothing for a range to reach from");
    check(tiku_list_set_count(&l, 10) == 10 && tiku_list_count(&l) == 10,
          "it takes the rows it is given");
    check(tiku_list_set_count(&l, TIKU_LIST_MAX + 5) == TIKU_LIST_MAX,
          "and says how many it kept when asked for more than it holds, "
          "rather than ending early and looking whole");
    (void)tiku_list_set_count(&l, 10);

    printf("\nwhere the rows are, and which one a click lands on\n");
    check(tiku_list_visible_rows(&l, body) == 4,
          "a body four rows tall shows four");
    {
        tiku_rect_t r0 = tiku_list_row_rect(&l, body, 0);
        tiku_rect_t r1 = tiku_list_row_rect(&l, body, 1);

        check(r0.y == 50 && r0.h == 20 && r0.x == 100 && r0.w == 200,
              "the first row fills the top of the body");
        check(r1.y == r0.y + r0.h,
              "and the next begins exactly where it ends, with no gap a "
              "click could fall into");
        check(tiku_list_row_rect(&l, body, 99).h == 0,
              "a row the list does not have is degenerate, not off the end");
    }
    check(tiku_list_at(&l, body, 150, 55) == 0 &&
          tiku_list_at(&l, body, 150, 75) == 1,
          "a point in a row lands on that row");
    check(tiku_list_at(&l, body, 150, 45) == -1 &&
          tiku_list_at(&l, body, 350, 55) == -1,
          "and a point outside the body lands on none");
    {
        /* A body that is not a whole number of rows tall leaves a strip
         * at its foot that the painter never fills. */
        tiku_rect_t ragged = {100, 50, 200, 90};

        check(tiku_list_visible_rows(&l, ragged) == 4,
              "a body four and a half rows tall shows four");
        check(tiku_list_at(&l, ragged, 150, 125) == 3,
              "the last row drawn takes a click on it");
        check(tiku_list_at(&l, ragged, 150, 135) == -1,
              "and the empty strip below it takes none, rather than the "
              "row nothing was drawn for");
    }

    printf("\nplain clicking picks one row and anchors on it\n");
    tiku_list_click(&l, 2, 0u);
    same(&l, "..*.......", "a click selects the row it landed on");
    check(l.pivot == 2 && l.cursor == 2,
          "and anchors there, because that is where a range would reach "
          "from next");
    tiku_list_click(&l, 5, 0u);
    same(&l, ".....*....", "another click starts over");
    check(l.pivot == 5, "and moves the anchor with it");

    printf("\nreaching from the anchor, and reaching again\n");
    tiku_list_click(&l, 2, 0u);
    tiku_list_click(&l, 5, TIKU_MOD_SHIFT);
    same(&l, "..****....", "shift reaches from the anchor to the click");
    check(l.pivot == 2,
          "and the anchor does NOT move -- which is what lets the next "
          "reach start from the same place");
    tiku_list_click(&l, 4, TIKU_MOD_SHIFT);
    same(&l, "..***.....",
         "so a second reach re-reaches from the anchor rather than "
         "growing what the first one left");
    tiku_list_click(&l, 0, TIKU_MOD_SHIFT);
    same(&l, "***.......", "and it reaches backwards just as well");

    printf("\nthe modifier that toggles one row\n");
    tiku_list_click(&l, 3, 0u);
    tiku_list_click(&l, 6, TIKU_MOD_CMD);
    same(&l, "...*..*...", "cmd adds a row without disturbing the rest");
    check(l.pivot == 6, "and anchors on what it added");
    tiku_list_click(&l, 6, TIKU_MOD_CMD);
    same(&l, "...*......", "and takes it away again");
    tiku_list_click(&l, 3, TIKU_MOD_CMD);
    same(&l, "..........", "down to nothing");
    check(l.pivot == -1 && l.last_added == -1,
          "and an empty selection keeps NO anchor: the invariant "
          "everything else leans on");

    printf("\nshift and cmd together add without clearing\n");
    tiku_list_click(&l, 1, 0u);
    tiku_list_click(&l, 2, TIKU_MOD_CMD);
    same(&l, ".**.......", "two rows, picked one at a time");
    tiku_list_click(&l, 5, TIKU_MOD_SHIFT | TIKU_MOD_CMD);
    same(&l, ".*****....",
         "reaching out to an unselected row ADDS the range -- it takes "
         "the state the clicked row did not have");
    tiku_list_click(&l, 3, TIKU_MOD_SHIFT | TIKU_MOD_CMD);
    same(&l, ".*..**....",
         "and reaching out to a selected one clears that stretch, which "
         "is the same rule read the other way");

    printf("\nthe click that deliberately does nothing\n");
    tiku_list_click(&l, 0, 0u);
    tiku_list_click(&l, 3, TIKU_MOD_SHIFT);
    same(&l, "****......", "a range of four");
    tiku_list_click(&l, 2, 0u);
    same(&l, "****......",
         "a plain click INSIDE a selection changes nothing -- a gesture "
         "that starts on a selected row carries them all");
    tiku_list_click(&l, 7, 0u);
    same(&l, ".......*..", "while one outside it starts over");

    printf("\nall, none, and the other way up\n");
    tiku_list_select_all(&l);
    same(&l, "**********", "all of them");
    check(l.pivot == 0 && l.last_added == 9,
          "with both anchors re-taken, not left over from the last click");
    tiku_list_invert(&l);
    same(&l, "..........", "inverted, all of them is none");
    check(l.pivot == -1,
          "and an inverted selection that is empty has no anchor either");
    tiku_list_click(&l, 4, 0u);
    tiku_list_invert(&l);
    same(&l, "****.*****", "inverting keeps what was not picked");
    check(l.pivot == 0 && l.last_added == 9,
          "and anchors at the top, because an inverted selection has no "
          "click behind it to anchor on");
    tiku_list_select_none(&l);
    same(&l, "..........", "and none is none");

    printf("\nthe keyboard moves the same selection\n");
    tiku_list_select_only(&l, 0);
    check(tiku_list_key(&l, TIKU_KEY_DOWN, 0u, body) == 1, "down is taken");
    same(&l, ".*........", "and moves the selection by one");
    (void)tiku_list_key(&l, TIKU_KEY_DOWN, TIKU_MOD_SHIFT, body);
    same(&l, ".**.......", "with shift it reaches instead of moving");
    (void)tiku_list_key(&l, TIKU_KEY_END, 0u, body);
    same(&l, ".........*", "End goes to the last row");
    check(l.top == 6,
          "and the top follows, so the row it lands on is one that shows");
    (void)tiku_list_key(&l, TIKU_KEY_HOME, 0u, body);
    same(&l, "*.........", "Home to the first");
    check(l.top == 0, "and the top comes back with it");
    (void)tiku_list_key(&l, TIKU_KEY_PAGE_DOWN, 0u, body);
    same(&l, "....*.....", "a page is a bodyful of rows");
    check(tiku_list_key(&l, TIKU_KEY_RETURN, 0u, body) == 0,
          "Return is NOT taken: what it means to open a row is the "
          "caller's, and a list that swallowed it would be worked around "
          "by every one of them");
    (void)tiku_list_key(&l, 'a', TIKU_MOD_CMD, body);
    same(&l, "**********", "and cmd-A takes the lot");

    printf("\nthe first arrow lands rather than moves\n");
    {
        tiku_list_t f;

        tiku_list_init(&f, 1);
        tiku_list_set_row_h(&f, 20);
        (void)tiku_list_set_count(&f, 10);
        (void)tiku_list_key(&f, TIKU_KEY_DOWN, 0u, body);
        same(&f, "*.........",
             "with nothing picked yet, Down lands ON the first row -- "
             "moving from nowhere one row in is how a list silently "
             "skips its first entry");
        (void)tiku_list_key(&f, TIKU_KEY_DOWN, 0u, body);
        same(&f, ".*........", "and only then does it move");
    }

    printf("\nholding one row at a time is enforced here, not asked for\n");
    {
        tiku_list_t one;

        tiku_list_init(&one, 1);
        tiku_list_set_row_h(&one, 20);
        (void)tiku_list_set_count(&one, 10);
        tiku_list_click(&one, 2, 0u);
        tiku_list_click(&one, 5, TIKU_MOD_SHIFT);
        same(&one, ".....*....",
             "a reach lands on the row it reached, and does not extend");
        tiku_list_click(&one, 7, TIKU_MOD_CMD);
        same(&one, ".......*..", "and neither does the toggle");
        tiku_list_select_all(&one);
        same(&one, "*.........", "nor does select-all");
        (void)tiku_list_key(&one, TIKU_KEY_DOWN, TIKU_MOD_SHIFT, body);
        same(&one, ".*........", "nor a shifted arrow");
        check(tiku_list_selection_count(&one) == 1,
              "one row at a time, whatever the gesture");
    }

    printf("\nthe rows can move under the selection\n");
    tiku_list_select_all(&l);
    (void)tiku_list_set_count(&l, 4);
    same(&l, "****", "a list that shrinks keeps the rows it still has");
    check(l.pivot >= 0 && l.pivot < 4 && l.last_added < 4,
          "and no anchor is left pointing past the end");
    tiku_list_select_none(&l);
    (void)tiku_list_set_count(&l, 10);
    check(tiku_list_selection_count(&l) == 0,
          "rows that come back come back unselected, rather than "
          "wearing a flag left behind by a row that used to be there");

    printf("\nspelling a name jumps to it\n");
    {
        int64_t t = 1000000;

        (void)tiku_list_set_count(&l, 10);
        tiku_list_select_none(&l);
        check(tiku_list_typeahead(&l, 'd', t, body, name_of, NULL) == 3,
              "a letter goes to the first row that begins with it");
        check(tiku_list_typeahead(&l, 'e', t + 1000, body, name_of,
                                  NULL) == 3,
              "and a letter typed straight after it goes on spelling the "
              "same name rather than starting a new one");
        check(tiku_list_typeahead(&l, 'x', t + 2000, body, name_of,
                                  NULL) == -1,
              "a spelling that matches nothing moves nothing -- it is a "
              "name still being typed");
        same(&l, "...*......", "so the jump before it still stands");
        check(tiku_list_typeahead(&l, 'g', t + 3000000, body, name_of,
                                  NULL) == 2,
              "after a second of quiet the next letter starts a new "
              "spelling, and case is not part of it");
        check(tiku_list_typeahead(&l, 'e', t + 3001000, body, name_of,
                                  NULL) == -1 &&
              tiku_list_chosen(&l) == 2,
              "and the spelling goes on from there");
    }

    printf("\nthe simple look draws the rows it holds, and says so\n");
    {
        /* The one drawing convenience the widget offers: plain named
         * rows through the kit's row painter -- so each crosses the
         * wire as the row it is, and a reader is told its name and
         * whether it is picked.  Proven from the wire, because that is
         * the claim. */
        tiku_surface_t *s = tiku_surface_new(300, 200, TIKU_C_DOC);
        tiku_dl_t *dl = tiku_dl_new();
        char lines[1024];

        tiku_list_init(&l, 0);
        tiku_list_set_row_h(&l, 20);
        (void)tiku_list_set_count(&l, 10);
        tiku_list_select_only(&l, 1);
        (void)tiku_list_scroll_to(&l, 0, body);
        s->record = dl;
        tiku_list_draw(&l, s, (tiku_rect_t){ 10, 10, 280, 80 },
                       name_of, NULL);
        s->record = NULL;
        (void)tiku_dl_say(dl, lines, sizeof lines);
        check(strstr(lines, "row \"alpha\"") != NULL &&
              strstr(lines, "row \"beta\", selected") != NULL,
              "a drawn list narrates its rows by name, with the picked "
              "one saying so");
        check(strstr(lines, "row \"epsilon\"") == NULL,
              "and only the rows that SHOW are drawn -- four fit, so the "
              "fifth is not on the wire pretending to be visible");
        check(tiku_dl_misses(dl) == 0,
              "whole on the wire: nothing about it was pixels");
        tiku_dl_free(dl);
        tiku_surface_free(s);
    }

    /*
     * Headings: furniture among the rows.  A project window names its
     * groups this way, and the whole of what a heading is lives here --
     * it cannot be picked by any road, travel steps over it, spelling
     * passes it by, and it crosses the wire as words rather than as a
     * row a reader would think it could open.
     */
    {
        tiku_list_t l;
        tiku_rect_t body = { 0, 0, 200, 200 };

        tiku_list_init(&l, 0);
        tiku_list_set_row_h(&l, 20);
        (void)tiku_list_set_count(&l, 6);
        tiku_list_set_heading(&l, 0, 1);        /* alpha names 1..2  */
        tiku_list_set_heading(&l, 3, 1);        /* delta names 4..5  */
        check(tiku_list_is_heading(&l, 0) && tiku_list_is_heading(&l, 3) &&
              !tiku_list_is_heading(&l, 1),
              "a row can be made a heading, and its neighbours are not");
        tiku_list_select_only(&l, 0);
        same(&l, "......",
             "a heading cannot be selected, even asked for by name");
        tiku_list_click(&l, 1, 0u);
        same(&l, ".*....", "the row under it can");
        tiku_list_click(&l, 3, 0u);
        same(&l, ".*....",
             "and clicking the heading takes nothing and lets go of "
             "nothing: pressing a word is not choosing a file");
        tiku_list_click(&l, 5, TIKU_MOD_SHIFT);
        same(&l, ".**.**",
             "a range reaches OVER the heading in its way rather than "
             "taking it");
        tiku_list_select_all(&l);
        same(&l, ".**.**", "and neither does taking everything");
        {
            tiku_list_t one;

            tiku_list_init(&one, 1);
            tiku_list_set_row_h(&one, 20);
            (void)tiku_list_set_count(&one, 6);
            tiku_list_set_heading(&one, 0, 1);
            tiku_list_set_heading(&one, 3, 1);
            tiku_list_select_only(&one, 4);
            tiku_list_select_all(&one);
            same(&one, ".*....",
                 "select-all holding one row takes the first row under "
                 "a leading heading");
        }
        tiku_list_select_none(&l);
        tiku_list_invert(&l);
        same(&l, ".**.**", "nor does turning the selection inside out");
        /* Travel. */
        tiku_list_select_only(&l, 2);
        (void)tiku_list_key(&l, TIKU_KEY_DOWN, 0u, body);
        same(&l, "....*.",
             "travelling DOWN steps over the heading onto the row "
             "beneath it");
        (void)tiku_list_key(&l, TIKU_KEY_UP, 0u, body);
        same(&l, "..*...", "and back UP over it the other way");
        (void)tiku_list_key(&l, TIKU_KEY_HOME, 0u, body);
        same(&l, ".*....",
             "HOME onto a heading means the first row UNDER it, not the "
             "nothing that lies before it");
        (void)tiku_list_key(&l, TIKU_KEY_END, 0u, body);
        same(&l, ".....*", "and END the last row there is");
        /* Spelling. */
        tiku_list_select_none(&l);
        check(tiku_list_typeahead(&l, 'd', 1000, body, name_of,
                                  NULL) == -1,
              "spelling a heading's own name finds nothing: it is not "
              "something to open");
        check(tiku_list_typeahead(&l, 'z', 5000000, body, name_of,
                                  NULL) == 5,
              "while a row's name still finds its row");
        /* What crosses the wire. */
        {
            tiku_surface_t *sf = tiku_surface_new(200, 200,
                                                            TIKU_C_DOC);
            tiku_dl_t *dl = tiku_dl_new();
            char lines[4096];

            sf->record = dl;
            tiku_list_draw(&l, sf, body, name_of, NULL);
            sf->record = NULL;
            (void)tiku_dl_say(dl, lines, sizeof lines);
            check(strstr(lines, "row \"beta\"") != NULL,
                  "a drawn list still narrates its rows");
            check(strstr(lines, "row \"alpha\"") == NULL &&
                  strstr(lines, "row \"delta\"") == NULL,
                  "but a heading is not among them: furniture told as a "
                  "row is a row a reader would try to open");
            check(strstr(lines, "alpha") != NULL,
                  "it is there as WORDS, because it still names the "
                  "group underneath");
            tiku_dl_free(dl);
            tiku_surface_free(sf);
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "the rows hold their places");
    return fails ? 1 : 0;
}
