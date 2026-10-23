/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_outline.c - semantic heading navigation and a virtual sidebar.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_ui.h"
#include "tiku_text_layout_paint.h"
#include <stdio.h>
#include <string.h>

int
write_outline_width(const write_app_t *app)
{
    if (!app->show_outline || app->preview) return 0;
    return app->width / 3 < 200 ? app->width / 3 : 200;
}

/** @brief Sidebar bounds aligned with the document frame. */
static tiku_rect_t
bounds(const write_app_t *app)
{
    tiku_rect_t body = write_body(app);

    return (tiku_rect_t){8, body.y - 12, write_outline_width(app), body.h + 24};
}

/** @brief Number of complete heading rows in the sidebar. */
static size_t
visible(const write_app_t *app)
{
    int count = (bounds(app).h - 48) / 24;

    return count > 0 ? (size_t)count : 0;
}

/** @brief Keep scrolling and the keyboard selection within discovered entries. */
static void
clamp(write_app_t *app, int reveal)
{
    size_t count = tiku_richoutline_count(app->outline), rows = visible(app);
    size_t maximum = count > rows ? count - rows : 0;

    if (app->outline_selected >= count)
        app->outline_selected = count ? count - 1 : 0;
    if (reveal && rows) {
        if (app->outline_selected < app->outline_top)
            app->outline_top = app->outline_selected;
        if (app->outline_selected - app->outline_top >= rows)
            app->outline_top = app->outline_selected - rows + 1;
    }
    if (app->outline_top > maximum) app->outline_top = maximum;
}

void
write_outline_clear(write_app_t *app)
{
    tiku_richoutline_release(app->outline);
    app->outline = NULL;
    app->outline_generation = 0;
    app->outline_top = app->outline_selected = 0;
    app->outline_pending = 0;
    app->outline_status = TIKU_RICH_PENDING;
}

/** @brief Move the caret to a current-generation heading without editing text. */
static void
go_to(write_app_t *app, size_t index)
{
    tiku_rich_heading_t heading;
    tiku_rich_status_t status;

    if (app->qcount || app->preview || app->prompt ||
        app->outline_generation != tiku_richdoc_generation(app->doc)) return;
    status = tiku_richoutline_get(app->outline, index, &heading);
    if (status != TIKU_RICH_OK) return;
    status = tiku_richsession_select(tiku_richview_session(app->view),
        app->outline_generation, heading.position, heading.position);
    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    tiku_richview_select_cells(app->view, app->outline_generation, NULL);
    write_search_cancel(app);
    app->outline_selected = index;
    clamp(app, 1);
    app->reveal = app->repaint = 1;
}

/** @brief Locate the neighboring heading relative to the live caret. */
static void
neighbor(write_app_t *app, int action)
{
    tiku_rich_pos_t anchor, caret;
    size_t count = tiku_richoutline_count(app->outline), found = SIZE_MAX;
    tiku_rich_status_t status;

    tiku_richsession_selection(tiku_richview_session(app->view),
                                &anchor, &caret);
    status = tiku_richoutline_neighbor(app->outline, caret,
                                        action == W_OUTLINE_NEXT, &found);
    if (status == TIKU_RICH_OK) {
        go_to(app, found);
        tiku_dialog_focus(&app->dialog, W_BODY);
    } else if (status == TIKU_RICH_NOT_FOUND) {
        snprintf(app->status, sizeof app->status, "%s",
            !count ? "No headings. Choose Heading 1–6 in Paragraph." :
            action == W_OUTLINE_NEXT ? "No next heading." :
                                      "No previous heading.");
        app->repaint = 1;
    } else write_refused(app, status);
}

void
write_outline_step(write_app_t *app)
{
    uint64_t generation = tiku_richdoc_generation(app->doc);
    size_t before;
    tiku_rich_status_t previous;

    if ((!app->show_outline && !app->outline_pending) || app->preview) return;
    if (app->outline_generation != generation) {
        tiku_richsnapshot_t *snapshot = NULL;
        int pending = app->outline_pending;

        write_outline_clear(app);
        app->outline_pending = pending;
        app->outline_generation = generation;
        app->outline_status = tiku_richdoc_snapshot(app->doc, &snapshot);
        if (app->outline_status == TIKU_RICH_OK)
            app->outline_status = tiku_richoutline_create(snapshot,
                                                           &app->outline);
        tiku_richsnapshot_release(snapshot);
        app->repaint = 1;
    }
    previous = app->outline_status;
    before = tiku_richoutline_count(app->outline);
    if (app->outline)
        app->outline_status = tiku_richoutline_step(app->outline, 256);
    if (previous != app->outline_status ||
        before != tiku_richoutline_count(app->outline)) app->repaint = 1;
    clamp(app, 0);
    if (app->outline_pending && !app->qcount && !app->prompt &&
        app->outline_status != TIKU_RICH_PENDING) {
        int action = app->outline_pending;

        app->outline_pending = 0;
        if (app->outline_status == TIKU_RICH_OK) neighbor(app, action);
        else write_refused(app, app->outline_status);
    }
}

void
write_outline_action(write_app_t *app, int action)
{
    if (app->preview) return;
    if (action == W_OUTLINE) {
        app->show_outline = !app->show_outline;
        write_outline_clear(app);
        write_controls(app);
        tiku_dialog_focus(&app->dialog,
                          app->show_outline ? W_OUTLINE_LIST : W_BODY);
    } else app->outline_pending = action;
    write_outline_step(app);
    app->repaint = 1;
}

/** @brief Produce a bounded UTF-8 heading label without splitting a scalar. */
static void
heading_label(const tiku_rich_heading_t *heading, char *text, size_t capacity)
{
    size_t length = heading->length < capacity - 1 ?
                    heading->length : capacity - 1;

    while (length < heading->length && length &&
           ((unsigned char)heading->text[length] & 0xc0) == 0x80) length--;
    memcpy(text, heading->text, length);
    for (size_t i = 0; i < length; i++)
        if (text[i] == '\n' || text[i] == '\t') text[i] = ' ';
    text[length] = 0;
    if (!length) snprintf(text, capacity, "(Empty heading)");
}

void
write_outline_paint(write_app_t *app)
{
    tiku_rect_t box = bounds(app);
    size_t rows = visible(app), count = tiku_richoutline_count(app->outline);
    int current = app->outline_generation == tiku_richdoc_generation(app->doc);
    char text[256];

    if (!box.w) return;
    clamp(app, 0);
    tiku_fill(app->surface, box, TIKU_C_DOC);
    tiku_frame(app->surface, box, 0x969696);
    tiku_text_line_draw(app->surface,
        (tiku_rect_t){box.x + 8, box.y + 3, box.w - 16, 24},
        TIKU_MEASURE_PLAIN, "Headings", TIKU_C_TEXT);
    for (size_t i = 0; current && i < rows && app->outline_top + i < count; i++) {
        tiku_rich_heading_t heading;
        tiku_rect_t row = {box.x + 1, box.y + 26 + (int)i * 24, box.w - 2, 24};
        size_t index = app->outline_top + i;

        tiku_richoutline_get(app->outline, index, &heading);
        if (index == app->outline_selected) {
            tiku_fill(app->surface, row, 0xd9e7f4);
            if (app->dialog.focus == W_OUTLINE_LIST)
                tiku_frame(app->surface, row, 0x527da5);
        }
        row.x += 8 + (int)(heading.level - 1) * 8;
        row.w -= 16 + (int)(heading.level - 1) * 8;
        heading_label(&heading, text, sizeof text);
        tiku_text_line_draw(app->surface, row, TIKU_MEASURE_PLAIN,
                            text, TIKU_C_TEXT);
    }
    if (!current || app->outline_status == TIKU_RICH_PENDING)
        snprintf(text, sizeof text, "Indexing headings...");
    else if (app->outline_status != TIKU_RICH_OK)
        snprintf(text, sizeof text, "Index unavailable");
    else if (!count) snprintf(text, sizeof text, "No headings");
    else snprintf(text, sizeof text, "%zu of %zu",
                   app->outline_selected + 1, count);
    tiku_text_line_draw(app->surface,
        (tiku_rect_t){box.x + 8, box.y + box.h - 23, box.w - 16, 22},
        TIKU_MEASURE_PLAIN, text, TIKU_C_TEXT);
}

int
write_outline_event(write_app_t *app, const tiku_event_t *event)
{
    tiku_rect_t box = bounds(app);
    size_t count = tiku_richoutline_count(app->outline), rows = visible(app);
    int inside = box.w && event->x >= box.x && event->x < box.x + box.w &&
                 event->y >= box.y && event->y < box.y + box.h;

    if (!box.w || app->qcount || app->outline_generation !=
        tiku_richdoc_generation(app->doc)) return 0;
    if (inside && event->type == TIKU_EVENT_WHEEL) {
        if (event->dy > 0) app->outline_top += 3;
        if (event->dy < 0)
            app->outline_top = app->outline_top > 3 ? app->outline_top - 3 : 0;
        clamp(app, 0);
        app->repaint = 1;
        return 1;
    }
    if (inside && event->type == TIKU_EVENT_POINTER_DOWN &&
        event->button == 1) {
        int offset = event->y - box.y - 26;

        tiku_dialog_focus(&app->dialog, W_OUTLINE_LIST);
        if (offset >= 0 && (size_t)(offset / 24) < rows)
            go_to(app, app->outline_top + (size_t)offset / 24);
        app->repaint = 1;
        return 1;
    }
    if (app->dialog.focus != W_OUTLINE_LIST ||
        event->type != TIKU_EVENT_KEY_DOWN) return 0;
    if (event->key == TIKU_KEY_ESCAPE || event->key == TIKU_KEY_RETURN) {
        if (event->key == TIKU_KEY_RETURN && count)
            go_to(app, app->outline_selected);
        tiku_dialog_focus(&app->dialog, W_BODY);
    } else if (event->key == TIKU_KEY_UP || event->key == TIKU_KEY_PAGE_UP) {
        size_t step = event->key == TIKU_KEY_UP ? 1 : rows ? rows : 1;

        app->outline_selected = app->outline_selected > step ?
                                 app->outline_selected - step : 0;
    } else if (event->key == TIKU_KEY_DOWN || event->key == TIKU_KEY_PAGE_DOWN)
        app->outline_selected += event->key == TIKU_KEY_DOWN ?
                                  1 : rows ? rows : 1;
    else if (event->key == TIKU_KEY_HOME) app->outline_selected = 0;
    else if (event->key == TIKU_KEY_END)
        app->outline_selected = count ? count - 1 : 0;
    else return 0;
    clamp(app, 1);
    app->repaint = 1;
    return 1;
}
