/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_input.c - ordered rich-view input and bounded layout progress.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_host.h"
#include <stdio.h>
#include <stdlib.h>

void
write_pointer(write_app_t *app, const tiku_event_t *event)
{
    tiku_rich_divider_t divider;
    tiku_rect_t body = write_body(app);
    int shape = TIKU_CURSOR_ARROW;

    if (event->type >= TIKU_EVENT_POINTER_DOWN &&
        event->type <= TIKU_EVENT_POINTER_MOVE) {
        app->pointer_x = event->x;
        app->pointer_y = event->y;
    }
    if (app->active && !app->preview && !app->prompt &&
        (app->ruler_drag || write_ruler_hit(app, app->pointer_x, app->pointer_y) ||
         tiku_richview_divider(app->view, &divider) == TIKU_RICH_OK ||
         tiku_richview_divider_at(app->view, app->pointer_x - body.x,
            app->pointer_y - body.y, &divider) == TIKU_RICH_OK))
        shape = TIKU_CURSOR_RESIZE_H;
    if (shape != app->cursor &&
        tiku_app_cursor_set(app->services, app->id, shape)) app->cursor = shape;
}

/** @brief Deliver input and track document, selection and scroll changes. */
static void
deliver(write_app_t *app, const tiku_event_t *event)
{
    uint64_t before = tiku_richdoc_generation(app->doc);
    tiku_rich_status_t status;
    tiku_rich_divider_t divider;
    int resizing = event->type == TIKU_EVENT_POINTER_UP &&
        tiku_richview_divider(app->view, &divider) == TIKU_RICH_OK;
    int handled;
    tiku_rich_cells_t cells;

    status = tiku_richview_event(app->view, event, &handled);
    if (tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK)
        app->clicks = 0;
    if (status == TIKU_RICH_OK && event->type == TIKU_EVENT_POINTER_DOWN &&
        event->button == 1 &&
        tiku_richview_cells(app->view, &cells) != TIKU_RICH_OK) {
        if (!(event->modifiers & TIKU_MOD_SHIFT) && app->clicks &&
            event->time_us >= app->click_time &&
            event->time_us - app->click_time < 400000 &&
            abs(event->x - app->click_x) <= 4 &&
            abs(event->y - app->click_y) <= 4)
            app->clicks = app->clicks % 3 + 1;
        else app->clicks = 1;
        app->click_time = event->time_us;
        app->click_x = event->x;
        app->click_y = event->y;
        tiku_rich_rect_t picture;

        if (app->clicks > 1 && !event->modifiers &&
            tiku_richview_cells(app->view, &cells) != TIKU_RICH_OK &&
            tiku_richview_divider(app->view, &divider) != TIKU_RICH_OK &&
            tiku_richview_picture(app->view, &picture) != TIKU_RICH_OK)
            status = tiku_richview_select_at(app->view, event->x, event->y,
                                             app->clicks == 3);
    }
    write_changed(app, before);
    if (resizing && before != tiku_richdoc_generation(app->doc))
        app->reveal = 0;
    write_refused(app, status);
    if (status == TIKU_RICH_UNSUPPORTED &&
        tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK)
        snprintf(app->status, sizeof app->status,
            "Press Escape to edit text. Delete clears selected cell contents.");
    if (handled && status != TIKU_RICH_NO_CHANGE) app->repaint = 1;
}

void
write_step(write_app_t *app)
{
    tiku_rich_status_t before = tiku_richview_status(app->view);
    tiku_rich_status_t status = tiku_richview_step(app->view, 8192);
    unsigned count = 16;

    if (before != status) app->repaint = 1;
    while (status == TIKU_RICH_OK && app->qcount && count--) {
        tiku_event_t event = app->queue[app->qfirst];

        app->qfirst = (app->qfirst + 1) % WRITE_QUEUE;
        app->qcount--;
        deliver(app, &event);
        status = tiku_richview_status(app->view);
    }
    if (status != TIKU_RICH_OK && status != TIKU_RICH_PENDING) {
        if (app->qcount) app->cancel_input = 1;
        app->qcount = 0;
        write_refused(app, status);
    }
    if (!app->qcount && app->cancel_input) {
        tiku_event_t cancel = {.type = TIKU_EVENT_DEACTIVATED};
        int handled;

        tiku_richview_event(app->view, &cancel, &handled);
        app->cancel_input = 0;
    }
    if (status == TIKU_RICH_OK && app->reveal) {
        tiku_richview_reveal(app->view);
        app->reveal = 0;
        app->repaint = 1;
    }
}

void
write_input(write_app_t *app, const tiku_event_t *event)
{
    tiku_event_t local = *event;
    tiku_rect_t body = write_body(app);
    size_t at;

    if (event->type >= TIKU_EVENT_POINTER_DOWN &&
        event->type <= TIKU_EVENT_POINTER_MOVE) {
        local.x -= body.x;
        local.y -= body.y;
    }
    if (app->qcount == WRITE_QUEUE || app->cancel_input) {
        app->cancel_input = 1;
        app->repaint = 1;
        snprintf(app->status, sizeof app->status,
                 "Input queue full. New input was refused.");
        return;
    }
    at = (app->qfirst + app->qcount) % WRITE_QUEUE;
    app->queue[at] = local;
    app->qcount++;
    write_step(app);
    if (tiku_richview_status(app->view) == TIKU_RICH_PENDING) write_step(app);
}
