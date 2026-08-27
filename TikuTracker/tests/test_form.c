/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_form.c - a panel that was described rather than programmed.
 *
 * The claims that matter are about what a description may SAY and what
 * it must not be able to do: rows in the order written, a place named
 * for each control, and a line this build has never heard of passed
 * over rather than refusing the whole panel -- because a device built
 * after this desktop will send one, and showing the rows we understand
 * beats showing none of them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_form.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] form    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

static const char *PANEL =
    "# a board says what it has\n"
    "title Board one\n"
    "text Sensors and switches\n"
    "gauge Temperature /devices/board1/sys/temp 0 120\n"
    "toggle Lamp /devices/board1/led\n"
    "field Name /devices/board1/name\n"
    "button Reboot /devices/board1/reboot 1\n";

int
main(void)
{
    tiku_form_t f;
    int unknown = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("a few lines of text are a panel\n");
    check(tiku_form_parse(&f, PANEL, &unknown) == 5 && unknown == 0,
          "five rows read, and nothing passed over");
    check(strcmp(f.title, "Board one") == 0,
          "the title is what it was called, not the file's name");
    check(f.row[0].kind == TIKU_FORM_TEXT &&
          strcmp(f.row[0].label, "Sensors and switches") == 0,
          "words with spaces in them survive: a label is the rest of "
          "its line, which is why it is last");
    check(f.row[1].kind == TIKU_FORM_GAUGE &&
          strcmp(f.row[1].bind, "/devices/board1/sys/temp") == 0 &&
          f.row[1].lo == 0 && f.row[1].hi == 120,
          "a gauge names the PLACE it shows and the ends it shows it "
          "between");
    check(f.row[2].kind == TIKU_FORM_TOGGLE &&
          strcmp(f.row[2].bind, "/devices/board1/led") == 0,
          "a toggle names the place it changes");
    check(f.row[4].kind == TIKU_FORM_BUTTON &&
          strcmp(f.row[4].value, "1") == 0,
          "and a button carries what pressing it writes");

    printf("\nrows this build has never heard of are passed over\n");
    {
        tiku_form_t g;
        int un = 0;

        check(tiku_form_parse(&g,
                  "toggle Lamp /led\n"
                  "hologram Face /dev/holo\n"
                  "button Go /run 1\n", &un) == 2 && un == 1,
              "the two it knows are kept and the one it does not is "
              "counted -- a panel from a newer device is short, not "
              "refused");
        check(g.row[1].kind == TIKU_FORM_BUTTON,
              "and the rows after the strange one still land in order");
    }

    printf("\nworking a control says what to write, and nothing else\n");
    {
        const char *w;

        (void)tiku_form_parse(&f, PANEL, &unknown);
        w = tiku_form_press(&f, 2);         /* the toggle */
        check(w != NULL && strcmp(w, "1") == 0,
              "a toggle whose place has not answered turns ON, which is "
              "what pressing an off-looking switch means");
        f.row[2].known = 1;
        snprintf(f.row[2].value, sizeof f.row[2].value, "1");
        w = tiku_form_press(&f, 2);
        check(w != NULL && strcmp(w, "0") == 0,
              "and one that is on turns off");
        w = tiku_form_press(&f, 4);
        check(w != NULL && strcmp(w, "1") == 0,
              "a button writes what it was described with");
        check(tiku_form_press(&f, 0) == NULL &&
              tiku_form_press(&f, 1) == NULL,
              "while words and a gauge write NOTHING -- a description "
              "cannot make a reading into a switch");
        check(tiku_form_press(&f, 99) == NULL,
              "and a row that is not there writes nothing either");
    }

    printf("\nthe rows are where the panel says they are\n");
    {
        tiku_rect_t body = { 10, 20, 300, 400 };
        tiku_rect_t r0 = tiku_form_row_rect(&f, body, 0);
        tiku_rect_t r1 = tiku_form_row_rect(&f, body, 1);

        check(r0.h > 0 && r1.y == r0.y + r0.h,
              "each row begins where the last ended");
        check(tiku_form_at(&f, body, r1.x + 5, r1.y + 2) == 1,
              "and a point in row one is found in row one");
        check(tiku_form_at(&f, body, r1.x + 5, body.y + 400) == -1,
              "while one past the last row belongs to none of them");
        check(tiku_form_height(&f) > 0 &&
              tiku_form_row_rect(&f, body, 99).h == 0,
              "a row the panel does not have is degenerate, not off the "
              "end");
    }

    printf("\n%s\n", fails ? "FAILURES"
                           : "a described panel is the panel described");
    return fails ? 1 : 0;
}
