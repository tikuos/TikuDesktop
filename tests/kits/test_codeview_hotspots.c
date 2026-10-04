/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_hotspots.c - runs of text a click follows: which run is
 * under the pointer, the keys that make it live, a press and release on
 * it told to the host while the selection stays, the line under the one
 * shown, and what ends it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

#define W 320
#define H 140

static const tiku_rect_t box = { 10, 10, 300, 110 };
/* Numbers at 9..12 and 15..17 on the first line, one at 26..28 on the
 * second. */
static const char *const program = "10 PRINT 123 + 45\n20 PRINT 99";

static int x0, y0, cell, step;

/* What the host was told. */
static int clicks;
static tiku_edit_range_t clicked;
static unsigned clicked_mods;

static void
hotspot(void *ctx, tiku_codeview_t *view, tiku_edit_range_t range,
        unsigned modifiers)
{
    (void)ctx;
    (void)view;
    clicks++;
    clicked = range;
    clicked_mods = modifiers;
}

static const tiku_codeview_host_t host = { NULL, NULL, NULL, NULL, hotspot };

static void
metrics(void)
{
    x0 = box.x + 5;
    y0 = box.y + 3;
    cell = tiku_font_mono_cell(0);
    step = tiku_font_mono(0)->height + 2;
}

/** @brief A view of @p s coloured as BASIC, numbers a hotspot. */
static tiku_codeview_t *
view_of(const char *s)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_language(v, TIKU_SYNTAX_BASIC);
    tiku_codeview_set_host(v, &host);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_NUM, TIKU_CODEVIEW_HOTSPOT);
    return v;
}

/** @brief A pointer event at column @p col of row @p row. */
static int
pointer(tiku_codeview_t *v, tiku_event_type_t type, int row, int col,
        unsigned mods)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.button = type == TIKU_EVENT_POINTER_MOVE ? 0 : 1;
    e.x = x0 + col * cell + cell / 2;
    e.y = y0 + row * step + step / 2;
    e.modifiers = mods;
    e.time_us = 1000000;
    return tiku_codeview_event(v, &e);
}

/** @brief A key going down or up. */
static int
key(tiku_codeview_t *v, tiku_event_type_t type, unsigned code, unsigned mods)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.key = code;
    e.modifiers = mods;
    return tiku_codeview_event(v, &e);
}

/** @brief Whether the hotspot shown is @p start to @p end, or with
 *         @p start -1 that none is. */
static int
shown(tiku_codeview_t *v, tiku_edit_pos_t start, tiku_edit_pos_t end)
{
    tiku_edit_range_t r = { -1, -1 };
    int on = tiku_codeview_hotspot(v, &r);

    return start < 0 ? !on : on && r.start == start && r.end == end;
}

/** @brief Whether the selection is the caret @p at. */
static int
caret_is(tiku_codeview_t *v, tiku_edit_pos_t at)
{
    tiku_edit_pos_t anchor, caret;

    return tiku_codeview_selection(v, &anchor, &caret) == TIKU_EDIT_OK &&
           anchor == at && caret == at;
}

/** @brief The run under the pointer is the hotspot when its ink's style
 *         makes one, the whole run, and nothing else is. */
static void
under_pointer(void)
{
    tiku_codeview_t *v = view_of(program);

    check(shown(v, -1, 0), "HOT-001", "a new view shows no hotspot");
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0) == 1 &&
          shown(v, 9, 12), "HOT-001",
          "over a number its whole run is the hotspot, and the view "
          "takes the move");
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 9, 0) == 0 &&
          shown(v, 9, 12), "HOT-001",
          "a move within the same hotspot changes nothing");
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 16, 0) == 1 &&
          shown(v, 15, 17), "HOT-001", "another run is another hotspot");
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 13, 0) == 1 &&
          shown(v, -1, 0), "HOT-001",
          "over a blank, a sign or a word there is none");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 4, 0);
    check(shown(v, -1, 0), "HOT-001", "nor over a reserved word");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 30, 0);
    check(shown(v, -1, 0), "HOT-001", "nor past a line's end");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 3, 10, 0);
    check(shown(v, -1, 0), "HOT-001", "nor below the last line");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 1, 9, 0);
    check(shown(v, 18 + 9, 18 + 11), "HOT-001",
          "and the second line's number is one");
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_NUM, 0);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    check(shown(v, -1, 0), "HOT-001",
          "without the hotspot style a run is plain text");
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_NUM, TIKU_CODEVIEW_HOTSPOT);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    (void)tiku_editdoc_annotation_add(tiku_codeview_doc(v), 0,
                                      "a note long enough to reach", 0);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 1, 10, 0);
    check(shown(v, -1, 0), "HOT-001",
          "nor over an annotation's row, under the line's number");
    tiku_codeview_destroy(v);

    v = view_of("10 PRINT 1 1234567890");
    tiku_codeview_set_bounds(v, (tiku_rect_t){ 10, 10, 5 + 14 * cell, 110 });
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 12, 0);
    check(shown(v, -1, 0), "HOT-001",
          "nor past the end of a row the line wraps after");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 1, 2, 0);
    check(shown(v, 11, 21), "HOT-001",
          "and the number the next row holds is the hotspot there");
    tiku_codeview_destroy(v);
}

/** @brief Uncoloured text is the plain ink's: with that a hotspot, the
 *         line is one. */
static void
plain_text(void)
{
    tiku_codeview_t *v = view_of("just words\nmore");

    tiku_codeview_set_language(v, TIKU_SYNTAX_NONE);
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_PLAIN,
                                      TIKU_CODEVIEW_HOTSPOT);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 5, 0);
    check(shown(v, 0, 10), "HOT-001",
          "a line no language colours is one plain run");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, -1, 0);
    check(shown(v, -1, 0), "HOT-001", "nor is there one over the gutter");
    tiku_codeview_destroy(v);
}

/** @brief Hotspots are live only while exactly the keys set are held,
 *         Caps Lock aside, and the keys going down and up are followed
 *         where the pointer stands. */
static void
keys(void)
{
    tiku_codeview_t *v = view_of(program);

    tiku_codeview_set_hotspot_keys(v, TIKU_MOD_CMD);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    check(shown(v, -1, 0), "HOT-002",
          "with Command asked for, a move without it shows none");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, TIKU_MOD_CMD);
    check(shown(v, 9, 12), "HOT-002", "with it, the hotspot is live");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10,
                  TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    check(shown(v, -1, 0), "HOT-002", "with another key too, it is not");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10,
                  TIKU_MOD_CMD | TIKU_MOD_CAPS);
    check(shown(v, 9, 12), "HOT-002", "Caps Lock makes no difference");
    tiku_codeview_set_hotspot_keys(v, TIKU_MOD_CMD | TIKU_MOD_CAPS);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, TIKU_MOD_CMD);
    check(shown(v, 9, 12), "HOT-002", "not even in the keys asked for");
    tiku_codeview_set_hotspot_keys(v, TIKU_MOD_CMD);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    check(key(v, TIKU_EVENT_KEY_DOWN, TIKU_KEY_CMD, 0) == 1 &&
          shown(v, 9, 12), "HOT-002",
          "Command going down over a hotspot makes it live, and the view "
          "takes the key");
    check(key(v, TIKU_EVENT_KEY_UP, TIKU_KEY_CMD, TIKU_MOD_CMD) == 1 &&
          shown(v, -1, 0), "HOT-002", "and going up, not");
    check(key(v, TIKU_EVENT_KEY_UP, TIKU_KEY_CMD, 0) == 0 &&
          key(v, TIKU_EVENT_KEY_DOWN, TIKU_KEY_SHIFT, 0) == 0 &&
          shown(v, -1, 0), "HOT-002",
          "a modifier that changes nothing is left to the application");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 13, 0);
    check(key(v, TIKU_EVENT_KEY_DOWN, TIKU_KEY_CMD, 0) == 0 &&
          shown(v, -1, 0), "HOT-002", "away from a hotspot, Command is too");
    tiku_codeview_destroy(v);
}

/** @brief A press on a live hotspot is its own: the selection stays, and
 *         the host hears of a release over the same hotspot only. */
static void
clicking(void)
{
    tiku_codeview_t *v = view_of(program);

    tiku_codeview_set_hotspot_keys(v, TIKU_MOD_CMD);
    (void)tiku_codeview_select(v, 2, 2);
    clicks = 0;
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, 0, 10, TIKU_MOD_CMD) == 1 &&
          caret_is(v, 2) && clicks == 0, "HOT-003",
          "a press on a live hotspot leaves the caret, and tells nothing "
          "yet");
    check(pointer(v, TIKU_EVENT_POINTER_UP, 0, 11, TIKU_MOD_CMD) == 1 &&
          clicks == 1 && clicked.start == 9 && clicked.end == 12 &&
          clicked_mods == TIKU_MOD_CMD && caret_is(v, 2), "HOT-003",
          "released over it, the host hears the hotspot and the keys, "
          "and the caret still stays");
    (void)pointer(v, TIKU_EVENT_POINTER_DOWN, 0, 10, TIKU_MOD_CMD);
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 13, TIKU_MOD_CMD) == 1 &&
          shown(v, -1, 0) && caret_is(v, 2), "HOT-003",
          "dragged off it, it is no longer shown and nothing is selected");
    (void)pointer(v, TIKU_EVENT_POINTER_UP, 0, 13, TIKU_MOD_CMD);
    check(clicks == 1 && caret_is(v, 2), "HOT-003",
          "and released there, the host hears nothing");
    (void)pointer(v, TIKU_EVENT_POINTER_DOWN, 0, 10, TIKU_MOD_CMD);
    (void)pointer(v, TIKU_EVENT_POINTER_UP, 0, 16, TIKU_MOD_CMD);
    check(clicks == 1, "HOT-003",
          "released over another hotspot, nothing either");
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, 0, 10, 0) == 1 &&
          !caret_is(v, 2), "HOT-003",
          "a press without the keys puts the caret, as any press does");
    (void)pointer(v, TIKU_EVENT_POINTER_UP, 0, 10, 0);
    check(clicks == 1, "HOT-003", "and the host hears nothing of it");
    tiku_codeview_set_hotspot_keys(v, 0);
    (void)pointer(v, TIKU_EVENT_POINTER_DOWN, 0, 4, 0);
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    check(!caret_is(v, 4) && !caret_is(v, 5) && shown(v, -1, 0), "HOT-003",
          "a drag from the text over a live hotspot still selects as it "
          "goes, the hotspot not shown");
    (void)pointer(v, TIKU_EVENT_POINTER_UP, 0, 10, 0);
    check(clicks == 1 && shown(v, 9, 12), "HOT-003",
          "and once the button is up the hotspot shows");
    tiku_codeview_destroy(v);
}

/** @brief The hotspot shown is underlined in its text's colour; with
 *         none shown, the text is as it was. */
static void
painting(void)
{
    tiku_codeview_t *v = view_of(program);
    tiku_surface_t *s = tiku_surface_new(W, H, TIKU_RGB(255, 0, 255));
    int line_y = y0 + tiku_font_mono(0)->ascent + 1;
    tiku_rgb_t num = tiku_ink(TIKU_INK_NUM) & 0xffffffu;

    if (s == NULL)
        exit(1);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, s);
    check((tiku_peek(s, x0 + 9 * cell + 1, line_y) & 0xffffffu) != num &&
          (tiku_peek(s, x0 + 11 * cell + 6, line_y) & 0xffffffu) != num,
          "HOT-004", "with none shown, nothing is underlined");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    tiku_codeview_paint(v, s);
    check((tiku_peek(s, x0 + 9 * cell, line_y) & 0xffffffu) == num &&
          (tiku_peek(s, x0 + 12 * cell - 1, line_y) & 0xffffffu) == num &&
          (tiku_peek(s, x0 + 12 * cell, line_y) & 0xffffffu) != num &&
          (tiku_peek(s, x0 + 15 * cell + 1, line_y) & 0xffffffu) != num,
          "HOT-004", "the one shown is underlined end to end in its ink, "
          "and only it");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief A change to the document, a new style or deactivation ends the
 *         hotspot shown; a scroll finds the one under the pointer anew. */
static void
ending(void)
{
    tiku_codeview_t *v = view_of(program);
    tiku_event_t e;

    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    (void)tiku_codeview_select(v, 0, 0);
    (void)tiku_codeview_insert(v, "x", 1, 0);
    check(shown(v, -1, 0), "HOT-005", "an edit ends the hotspot shown");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 11, 0);
    check(shown(v, 10, 13), "HOT-005", "and the next move finds it again");
    (void)tiku_codeview_set_ink_style(v, TIKU_INK_WORD, TIKU_CODEVIEW_BOLD);
    check(shown(v, -1, 0), "HOT-005", "so does a style set");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 11, 0);
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_DEACTIVATED;
    (void)tiku_codeview_event(v, &e);
    check(shown(v, -1, 0), "HOT-005", "and so does deactivation");
    tiku_codeview_destroy(v);

    v = view_of("10 PRINT 1\n20 PRINT 22\n30 PRINT 333\n40 PRINT 4444\n"
                "50 PRINT 5\n60 PRINT 6\n70 PRINT 7\n80 PRINT 8\n");
    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 9, 0);
    check(shown(v, 9, 10), "HOT-005", "the first line's number is shown");
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_WHEEL;
    e.dy = 1;
    e.x = x0 + 9 * cell + cell / 2;
    e.y = y0 + step / 2;
    (void)tiku_codeview_event(v, &e);
    check(tiku_codeview_top(v) == 1 && shown(v, 11 + 9, 11 + 11),
          "HOT-005", "scrolled a line, the pointer is over the second "
          "line's number");
    tiku_codeview_destroy(v);
}

/** @brief A change of language ends the hotspot shown, and setting the
 *         keys ends a press, even set back before the release. */
static void
configuration_ends_press(void)
{
    tiku_codeview_t *v = view_of(program);
    int before = clicks;

    (void)pointer(v, TIKU_EVENT_POINTER_MOVE, 0, 10, 0);
    tiku_codeview_set_language(v, TIKU_SYNTAX_NONE);
    check(shown(v, -1, 0), "HOT-007",
          "changing language immediately clears the old hotspot");
    tiku_codeview_set_language(v, TIKU_SYNTAX_BASIC);
    (void)pointer(v, TIKU_EVENT_POINTER_DOWN, 0, 10, 0);
    tiku_codeview_set_hotspot_keys(v, TIKU_MOD_CMD);
    tiku_codeview_set_hotspot_keys(v, 0);
    (void)pointer(v, TIKU_EVENT_POINTER_UP, 0, 10, 0);
    check(clicks == before, "HOT-007",
          "changing hotspot keys cancels an in-progress activation");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    metrics();
    under_pointer();
    plain_text();
    keys();
    clicking();
    painting();
    ending();
    configuration_ends_press();
    printf("%s\n", fails ? "FAIL: code view hotspots" :
           "PASS: code view hotspots -- runs under the pointer, the keys "
           "that make them live, clicks told to the host, underlines, "
           "what ends them");
    return fails != 0;
}
