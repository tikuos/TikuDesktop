/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcells_navigation.c - directed selection and timed drag scrolling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Shape fixed ten-point text for independent scroll calculations. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
    size_t size, const uint8_t *bounds, tiku_rich_shape_t *out)
{
    (void)ctx; (void)chars; (void)text; (void)size; (void)bounds;
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 6,
        .ascent = 8, .descent = 2, .caret_count = 2,
        .carets = {{0,0}, {1,6}}};
    return TIKU_RICH_OK;
}

/** @brief Complete layout under a finite iteration bound. */
static void
settle(tiku_richview_t *view)
{
    unsigned attempts = 0;

    while (tiku_richview_step(view, 128) == TIKU_RICH_PENDING)
        assert(++attempts < 10000);
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
}

/** @brief Create a table bracketed by one-line body paragraphs. */
static tiku_richview_t *
fixture(unsigned rows, unsigned columns)
{
    size_t count = (size_t)rows * columns;
    tiku_rich_paragraph_source_t *p = calloc(count + 2, sizeof *p);
    tiku_rich_block_source_t *b = calloc(2 * count + 3, sizeof *b);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = count + 2,
        .blocks = b, .block_count = 2 * count + 3};
    tiku_richlayout_options_t options = {.width = columns * 60,
        .provider = {.shape = shape}};
    tiku_richdoc_t *doc;
    tiku_richview_t *view;

    assert(p && b);
    for (size_t i = 0; i < count + 2; i++)
        p[i] = (tiku_rich_paragraph_source_t){.text = "x", .length = 1};
    b[0].kind = TIKU_RICH_BLOCK_PARAGRAPH;
    b[1] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_TABLE,
        .table = {.rows = rows, .columns = columns, .width = columns * 60,
                  .padding = 2, .border = 1}};
    for (size_t i = 0; i < count; i++) {
        b[2 + 2 * i] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_CELL, .parent = 2};
        b[3 + 2 * i] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 3 + 2 * i,
            .paragraph = i + 1};
    }
    b[2 * count + 2] = (tiku_rich_block_source_t){
        .kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = count + 1};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(p); free(b);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    settle(view);
    return view;
}

/** @brief Deliver recognized input, returning the operation's result. */
static tiku_rich_status_t
send(tiku_richview_t *view, int type, int x, int y, unsigned key, unsigned mods)
{
    tiku_event_t event = {.type = type, .button = 1, .x = x, .y = y,
        .key = key, .modifiers = mods};
    int handled = 0;
    tiku_rich_status_t status = tiku_richview_event(view, &event, &handled);

    assert(handled);
    return status;
}

/** @brief Deliver a pointer gesture at a direct cell's center. */
static void
point(tiku_richview_t *view, unsigned ordinal, int type, unsigned mods)
{
    tiku_rich_grid_box_t box;
    tiku_rich_status_t status;

    assert(tiku_richlayout_grid(view->layout, 1 + ordinal, &box) == TIKU_RICH_OK);
    status = send(view, type,
        (int)((box.box.x + box.box.width / 2) * view->zoom),
        (int)((box.box.y + box.box.height / 2 - view->scroll) * view->zoom),
        0, mods);
    assert(status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE);
}

/** @brief Check the normalized range against independent row/column endpoints. */
static void
range(tiku_richview_t *view, unsigned a, unsigned b, unsigned columns)
{
    tiku_rich_cells_t cells;
    unsigned r1 = a / columns, r2 = b / columns;
    unsigned c1 = a % columns, c2 = b % columns;

    assert(tiku_richview_cells(view, &cells) == TIKU_RICH_OK);
    assert(cells.row == (r1 < r2 ? r1 : r2));
    assert(cells.column == (c1 < c2 ? c1 : c2));
    assert(cells.rows == (r1 < r2 ? r2 - r1 : r1 - r2) + 1);
    assert(cells.columns == (c1 < c2 ? c2 - c1 : c1 - c2) + 1);
}

/** @brief Exhaust endpoint directions and key motions, including table edges. */
static void
keyboard(void)
{
    tiku_richview_t *view = fixture(3, 4);
    const unsigned keys[] = {TIKU_KEY_LEFT, TIKU_KEY_RIGHT, TIKU_KEY_UP,
        TIKU_KEY_DOWN, TIKU_KEY_HOME, TIKU_KEY_END, TIKU_KEY_HOME, TIKU_KEY_END};
    uint64_t generation = view->doc->generation;
    size_t memory = tiku_richdoc_memory(view->doc);

    for (unsigned a = 0; a < 12; a++) for (unsigned b = 0; b < 12; b++) {
        unsigned targets[] = {b % 4 ? b - 1 : b, b % 4 < 3 ? b + 1 : b,
            b >= 4 ? b - 4 : b, b < 8 ? b + 4 : b,
            b / 4 * 4, b / 4 * 4 + 3, 0, 11};

        for (unsigned k = 0; k < 8; k++) {
            unsigned mods = TIKU_MOD_SHIFT | (k >= 6 ? TIKU_MOD_CMD : 0);
            tiku_rich_status_t status;

            point(view, a, TIKU_EVENT_POINTER_DOWN, TIKU_MOD_OPTION);
            point(view, b, TIKU_EVENT_POINTER_MOVE, 0);
            point(view, b, TIKU_EVENT_POINTER_UP, 0);
            status = send(view, TIKU_EVENT_KEY_DOWN, 0, 0, keys[k], mods);
            assert(status == (targets[k] == b ? TIKU_RICH_NO_CHANGE : TIKU_RICH_OK));
            range(view, a, targets[k], 4);
            assert(view->cells_origin == view->doc->root->blocks->nodes[2 + 2 * a].id);
            assert(view->cells_focus == view->doc->root->blocks->nodes[2 + 2 * targets[k]].id);
            assert(!view->cells_drag && view->doc->generation == generation);
            assert(tiku_richdoc_memory(view->doc) == memory);
        }
    }
    tiku_richview_destroy(view);
    puts("Cell navigation: 1152 directional keyboard oracles PASS");
}

/** @brief Preserve directed endpoints after shading and reject stale geometry. */
static void
guards(void)
{
    tiku_richview_t *view = fixture(3, 4);
    tiku_rich_cells_t cells = {view->doc->root->blocks->nodes[1].id, 0,0,2,2};
    tiku_rich_pos_t first, last;

    assert(tiku_richview_cells_extend(NULL, TIKU_RICH_MOVE_LEFT) == TIKU_RICH_INVALID);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_LEFT) == TIKU_RICH_NOT_FOUND);
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_LEFT) == TIKU_RICH_OK);
    range(view, 0, 4, 4);
    assert(tiku_richview_cells_extend(view, (tiku_rich_motion_t)999) == TIKU_RICH_INVALID);
    range(view, 0, 4, 4);
    point(view, 11, TIKU_EVENT_POINTER_DOWN, TIKU_MOD_OPTION);
    point(view, 0, TIKU_EVENT_POINTER_UP, 0);
    assert(tiku_richview_cells_shade(view, 0xffabcdef) == TIKU_RICH_OK);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_RIGHT) == TIKU_RICH_PENDING);
    range(view, 11, 0, 4);
    settle(view);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_RIGHT) == TIKU_RICH_OK);
    range(view, 11, 1, 4);
    tiku_richsession_set_read_only(view->session, 1);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_DOWN) == TIKU_RICH_OK);
    range(view, 11, 5, 4);
    view->doc->initiator = view->session;
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_DOWN) == TIKU_RICH_BUSY);
    assert(tiku_richview_tick(view, 10) == TIKU_RICH_BUSY);
    range(view, 11, 5, 4);
    view->doc->initiator = NULL;
    tiku_richsession_selection(view->session, &first, &last);
    first.byte = last.byte = 1;
    assert(tiku_richsession_select(view->session, 0, first, last) == TIKU_RICH_OK);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_DOWN) == TIKU_RICH_NOT_FOUND);
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    assert(send(view, TIKU_EVENT_KEY_DOWN, 0, 0, TIKU_KEY_HOME, 0) == TIKU_RICH_OK);
    assert(tiku_richview_cells(view, &cells) == TIKU_RICH_NOT_FOUND);
    tiku_richview_destroy(view);
}

/** @brief Keep outer and nested keyboard ranges in their immediate tables. */
static void
nested(void)
{
    tiku_richview_t *view = fixture(3, 4);
    uint64_t outer = view->doc->root->blocks->nodes[1].id;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = view->doc->root->paragraphs[1]->value.id,
        .table = {.rows = 2, .columns = 2, .width = 48}};
    tiku_rich_transaction_t tx = {view->doc->generation, &edit, 1};
    tiku_rich_cells_t cells = {outer, 0,0,1,1};
    uint64_t inner;

    assert(tiku_richdoc_apply(view->doc, &tx) == TIKU_RICH_OK);
    settle(view);
    inner = view->doc->root->blocks->nodes[4].id;
    assert(view->doc->root->blocks->nodes[4].kind == TIKU_RICH_BLOCK_TABLE);
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_RIGHT) == TIKU_RICH_OK);
    range(view, 0, 1, 4);
    assert(tiku_richview_cells(view, &cells) == TIKU_RICH_OK && cells.table == outer);
    cells = (tiku_rich_cells_t){inner, 0,0,1,1};
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_FINISH) == TIKU_RICH_OK);
    range(view, 0, 3, 2);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_RIGHT) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_cells(view, &cells) == TIKU_RICH_OK && cells.table == inner);
    assert(send(view, TIKU_EVENT_POINTER_DOWN, INT_MAX, INT_MAX, 0, TIKU_MOD_SHIFT) == TIKU_RICH_NO_CHANGE);
    assert(send(view, TIKU_EVENT_POINTER_UP, INT_MIN, INT_MIN, 0, 0) == TIKU_RICH_OK);
    range(view, 0, 0, 2);
    assert(tiku_richview_cells(view, &cells) == TIKU_RICH_OK && cells.table == inner);
    tiku_richview_destroy(view);
}

/** @brief Verify bounded time steps, stationary-pointer progress and table clamps. */
static void
scrolling(double zoom)
{
    tiku_richview_t *view = fixture(40, 3);
    tiku_rich_cells_t cells;
    double before, maximum = 10 + 40 * 16 - 48;
    uint64_t generation = view->doc->generation;

    assert(tiku_richview_viewport(view, 180 * zoom, 48 * zoom, zoom) == TIKU_RICH_OK);
    settle(view);
    assert(tiku_richview_tick(view, -1) == TIKU_RICH_INVALID);
    assert(tiku_richview_tick(NULL, 0) == TIKU_RICH_INVALID);
    assert(tiku_richview_tick(view, 0) == TIKU_RICH_NO_CHANGE);
    tiku_richsession_set_read_only(view->session, 1);
    point(view, 0, TIKU_EVENT_POINTER_DOWN, TIKU_MOD_OPTION);
    assert(tiku_richview_tick(view, 0) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_tick(view, 100000) == TIKU_RICH_NO_CHANGE);
    assert(view->scroll == 0);
    assert(send(view, TIKU_EVENT_POINTER_MOVE, INT_MAX, INT_MAX, 0, 0) == TIKU_RICH_OK);
    assert(tiku_richview_tick(view, 0) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_tick(view, 50000) == TIKU_RICH_OK);
    assert(fabs(view->scroll - 54 / zoom) < 0.0001);
    assert(tiku_richview_tick(view, 50000) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_tick(view, 1) == TIKU_RICH_NO_CHANGE);
    before = view->scroll;
    assert(tiku_richview_tick(view, INT64_MAX) == TIKU_RICH_OK);
    assert(fabs(view->scroll - before - 108 / zoom) < 0.0001);
    assert(tiku_richview_tick(view, 0) == TIKU_RICH_NO_CHANGE);
    for (int64_t t = 100000; t <= 2000000; t += 100000)
        (void)tiku_richview_tick(view, t);
    assert(fabs(view->scroll - maximum) < 0.0001);
    range(view, 0, 119, 3);
    assert(tiku_richview_tick(view, 2100000) == TIKU_RICH_NO_CHANGE);
    assert(send(view, TIKU_EVENT_POINTER_MOVE, INT_MIN, INT_MIN, 0, 0) == TIKU_RICH_OK);
    for (int64_t t = 2200000; t <= 4200000; t += 100000)
        (void)tiku_richview_tick(view, t);
    assert(fabs(view->scroll - 10) < 0.0001);
    range(view, 0, 0, 3);
    assert(send(view, TIKU_EVENT_POINTER_UP, INT_MIN, INT_MIN, 0, 0) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_tick(view, 4300000) == TIKU_RICH_NO_CHANGE);
    before = view->scroll;
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_FINISH) == TIKU_RICH_OK);
    assert(view->scroll > before && fabs(view->scroll - maximum) < 0.0001);
    assert(tiku_richview_cells(view, &cells) == TIKU_RICH_OK && cells.rows == 40);
    assert(tiku_richview_cells_extend(view, TIKU_RICH_MOVE_START) == TIKU_RICH_OK);
    assert(fabs(view->scroll - 10) < 0.0001);
    assert(view->doc->generation == generation);
    tiku_richview_destroy(view);
}

/** @brief Cancel timer capture on release, focus loss, wheel and geometry changes. */
static void
cancellation(void)
{
    tiku_richview_t *view = fixture(40, 3);
    tiku_rich_cells_t cells;
    tiku_rich_pos_t a, b;
    double before;

    tiku_richview_viewport(view, 180, 48, 1);
    for (unsigned mode = 0; mode < 7; mode++) {
        tiku_richview_scroll(view, 0);
        settle(view);
        point(view, 0, TIKU_EVENT_POINTER_DOWN, TIKU_MOD_OPTION);
        assert(send(view, TIKU_EVENT_POINTER_MOVE, 70, 90, 0, 0) == TIKU_RICH_OK);
        assert(tiku_richview_tick(view, 0) == TIKU_RICH_NO_CHANGE);
        if (mode == 0) send(view, TIKU_EVENT_DEACTIVATED, 0, 0, 0, 0);
        if (mode == 1) send(view, TIKU_EVENT_WHEEL, 0, 0, 0, 0);
        if (mode == 2) tiku_richview_reflow(view);
        if (mode == 3) tiku_richview_viewport(view, 180, 49, 1);
        if (mode == 4) send(view, TIKU_EVENT_KEY_DOWN, 0, 0, TIKU_KEY_ESCAPE, 0);
        if (mode == 5) {
            tiku_richsession_selection(view->session, &a, &b);
            a.byte = b.byte = 1;
            tiku_richsession_select(view->session, 0, a, b);
            assert(tiku_richview_tick(view, 50000) == TIKU_RICH_STALE);
        }
        if (mode == 6) assert(tiku_richview_cells_shade(view, 0xffaabbcc) == TIKU_RICH_OK);
        before = view->scroll;
        assert(tiku_richview_tick(view, 100000) == TIKU_RICH_NO_CHANGE);
        assert(view->scroll == before && !view->cells_drag);
        if (mode != 4 && mode != 5)
            assert(tiku_richview_cells(view, &cells) == TIKU_RICH_OK);
    }
    tiku_richview_destroy(view);
}

/** @brief Run independent selection and time/geometry checks. */
int
main(void)
{
    keyboard();
    guards();
    nested();
    scrolling(1);
    scrolling(2);
    cancellation();
    puts("Cell navigation: clocks, zoom, reveal, capture and guards PASS");
    return 0;
}
