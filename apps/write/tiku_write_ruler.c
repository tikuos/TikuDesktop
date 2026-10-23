/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_ruler.c - paragraph ruler, captured previews and keyboard control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_text_layout_paint.h"
#include <math.h>
#include <stdio.h>

tiku_rect_t
write_ruler_bounds(const write_app_t *app)
{
    tiku_rect_t body = write_body(app);

    if (!app->show_ruler || app->preview) return (tiku_rect_t){0};
    return (tiku_rect_t){body.x - 7, body.y - 44, body.w + 14, 28};
}

/** @brief Check whether a logical pointer position falls inside a rectangle. */
static int
inside(tiku_rect_t box, int x, int y)
{
    return x >= box.x && x < box.x + box.w &&
           y >= box.y && y < box.y + box.h;
}

/** @brief Describe the selected handle in points relative to its text area. */
static void
describe(write_app_t *app, const tiku_rich_ruler_t *ruler)
{
    static const char *names[] = {"First line", "Left indent", "Right edge"};

    snprintf(app->status, sizeof app->status, "%s: %.1f pt from text-area left%s",
        names[app->ruler_part], ruler->position[app->ruler_part] - ruler->origin,
        ruler->mixed & (1u << app->ruler_part) ? " (mixed selection)" : "");
}

/** @brief Compare a capture with current selection and viewport identity. */
static int
current(write_app_t *app)
{
    tiku_rich_ruler_t ruler;
    const tiku_rich_ruler_t *source = &app->ruler_source;

    return tiku_richruler_read(app->view, &ruler) == TIKU_RICH_OK &&
        (ruler.enabled & (1u << app->ruler_part)) &&
        ruler.view == source->view && ruler.generation == source->generation &&
        ruler.session == source->session && ruler.anchor == source->anchor &&
        ruler.caret == source->caret && ruler.viewport == source->viewport &&
        ruler.zoom == source->zoom;
}

/** @brief Paint point measurements at a bounded number of visible ticks. */
static void
ticks(write_app_t *app, tiku_rect_t box, const tiku_rich_ruler_t *ruler)
{
    tiku_rect_t body = write_body(app);
    double origin = body.x + ruler->origin * app->zoom;
    int first = (int)ceil(fmax(0, (box.x - origin) / (12 * app->zoom)));
    int last = (int)floor(fmin(ruler->width / 12,
        (box.x + box.w - origin) / (12 * app->zoom)));
    char label[32];

    tiku_fill(app->surface, (tiku_rect_t){(int)origin, box.y + 1,
        (int)(ruler->width * app->zoom) + 1, box.h - 2}, 0xfaf9f4);
    for (int i = first; i <= last; i++) {
        int x = (int)lround(origin + i * 12 * app->zoom);

        tiku_vline(app->surface, x, box.y + box.h - (i % 6 ? 5 : 8),
            i % 6 ? 4 : 7, 0x92928c);
        if (!(i % 6)) {
            snprintf(label, sizeof label, "%d", i * 12);
            tiku_text_line_draw(app->surface,
                (tiku_rect_t){x + 3, box.y + 2, 48, 20},
                TIKU_MEASURE_PLAIN, label, 0x62625d);
        }
    }
}

/** @brief Paint a filled or mixed triangular handle with a focused outline. */
static void
marker(write_app_t *app, tiku_rect_t box, const tiku_rich_ruler_t *ruler,
    unsigned part)
{
    int x = (int)lround(write_body(app).x + ruler->position[part] * app->zoom);
    int selected = app->dialog.focus == W_RULER_CONTROL &&
                   app->ruler_part == (int)part;
    tiku_rgb_t color = !(ruler->enabled & (1u << part)) ? 0x999994 :
                       selected || app->ruler_drag ? 0x315f91 : 0x525750;
    int top = part == TIKU_RICH_RULER_FIRST;

    for (int i = 0; i < 7; i++) {
        int y = top ? box.y + 1 + i : box.y + box.h - 2 - i;
        int half = 6 - i;

        tiku_hline(app->surface, x - half, y, half * 2 + 1, color);
        if (half > 1 && i > 0)
            tiku_hline(app->surface, x - half + 1, y, half * 2 - 1,
                ruler->mixed & (1u << part) ? 0xfaf9f4 :
                selected ? 0xacc9e6 : 0xdce0d9);
    }
}

void
write_ruler_paint(write_app_t *app)
{
    tiku_rect_t box = write_ruler_bounds(app), body = write_body(app);
    tiku_rich_ruler_t ruler;
    tiku_rich_status_t status;

    if (!box.h) return;
    status = tiku_richruler_read(app->view, &ruler);
    if (app->ruler_drag && !current(app)) app->ruler_drag = 0;
    if (app->ruler_drag) ruler = app->ruler_preview;
    tiku_clip_set(app->surface, box);
    tiku_fill(app->surface, box, 0xe1e1db);
    if (status == TIKU_RICH_OK) {
        ticks(app, box, &ruler);
        for (unsigned i = 0; i < 3; i++) marker(app, box, &ruler, i);
    } else tiku_text_line_draw(app->surface,
        (tiku_rect_t){box.x + 8, box.y + 3, box.w - 16, 22},
        TIKU_MEASURE_PLAIN,
        status == TIKU_RICH_PENDING ? "Measuring text..." :
        status == TIKU_RICH_UNSUPPORTED ? "Select text in one body or cell" :
        status == TIKU_RICH_LIMIT ? "Ruler selection limit: 1024 paragraphs" :
                                  "Ruler unavailable", 0x62625d);
    tiku_frame(app->surface, box, app->dialog.focus == W_RULER_CONTROL ?
               0x527da5 : 0x969696);
    tiku_clip_reset(app->surface);
    tiku_text_line_draw(app->surface,
        (tiku_rect_t){body.x - 30, box.y + 4, 23, 20},
        TIKU_MEASURE_PLAIN, "pt", 0x62625d);
    if (app->ruler_drag) {
        int x = (int)lround(body.x + ruler.position[app->ruler_part] * app->zoom);

        tiku_clip_set(app->surface, body);
        for (int y = body.y; y < body.y + body.h; y += 6)
            tiku_vline(app->surface, x, y, 3, 0x527da5);
        tiku_clip_reset(app->surface);
    }
}

int
write_ruler_hit(write_app_t *app, int x, int y)
{
    tiku_rect_t box = write_ruler_bounds(app);
    tiku_rich_ruler_t ruler;
    int part;

    if (!inside(box, x, y) || app->prompt || app->qcount ||
        tiku_richruler_read(app->view, &ruler) != TIKU_RICH_OK) return 0;
    if (y < box.y + 11) part = TIKU_RICH_RULER_FIRST;
    else if (y >= box.y + box.h - 11) {
        double left = write_body(app).x + ruler.position[1] * app->zoom;
        double right = write_body(app).x + ruler.position[2] * app->zoom;

        part = fabs(x - left) <= fabs(x - right) ? 1 : 2;
    } else return 0;
    return ruler.enabled & (1u << part) &&
        fabs(x - write_body(app).x - ruler.position[part] * app->zoom) <= 7 ?
        part + 1 : 0;
}

/** @brief Apply a checked ruler edit and update ordinary recovery/dirty state. */
static void
commit(write_app_t *app, const tiku_rich_ruler_t *source, double x)
{
    uint64_t before = tiku_richdoc_generation(app->doc);
    tiku_rich_status_t status = tiku_richruler_apply(app->view, source,
        (tiku_rich_ruler_part_t)app->ruler_part, x);

    write_changed(app, before);
    write_refused(app, status);
    app->reveal = 0;
    app->repaint = 1;
}

/** @brief Move the focused marker or select another without mouse input. */
static int
keyboard(write_app_t *app, const tiku_event_t *event)
{
    tiku_rich_ruler_t ruler;
    double x;
    int key = event->key;

    if (event->type != TIKU_EVENT_KEY_DOWN ||
        app->dialog.focus != W_RULER_CONTROL ||
        (event->modifiers & ~TIKU_MOD_SHIFT)) return 0;
    if (key == TIKU_KEY_ESCAPE || key == TIKU_KEY_RETURN) {
        tiku_dialog_focus(&app->dialog, W_BODY);
        app->repaint = 1;
        return 1;
    }
    if (key != TIKU_KEY_UP && key != TIKU_KEY_DOWN &&
        key != TIKU_KEY_LEFT && key != TIKU_KEY_RIGHT &&
        key != TIKU_KEY_HOME && key != TIKU_KEY_END) return 0;
    if (tiku_richruler_read(app->view, &ruler) != TIKU_RICH_OK) return 1;
    if (key == TIKU_KEY_UP || key == TIKU_KEY_DOWN)
        app->ruler_part = (app->ruler_part + (key == TIKU_KEY_UP ? 2 : 1)) % 3;
    else {
        x = key == TIKU_KEY_HOME ? ruler.minimum[app->ruler_part] :
            key == TIKU_KEY_END ? ruler.maximum[app->ruler_part] :
            ruler.position[app->ruler_part] +
            (key == TIKU_KEY_LEFT ? -1 : 1) *
            (event->modifiers & TIKU_MOD_SHIFT ? 10 : 1);
        if (tiku_richruler_preview(&ruler, app->ruler_part, x,
            &app->ruler_preview) == TIKU_RICH_OK) {
            commit(app, &ruler, x);
            ruler = app->ruler_preview;
        }
    }
    describe(app, &ruler);
    app->repaint = 1;
    return 1;
}

/** @brief Move a captured guide and publish only the final primary release. */
static int
drag(write_app_t *app, const tiku_event_t *event)
{
    double x;

    if (event->type != TIKU_EVENT_POINTER_MOVE &&
        !(event->type == TIKU_EVENT_POINTER_UP && event->button == 1)) return 0;
    app->repaint = 1;
    if (!current(app)) {
        app->ruler_drag = 0;
        write_refused(app, TIKU_RICH_STALE);
        return 1;
    }
    x = app->ruler_source.position[app->ruler_part] +
        (event->x - write_body(app).x) / app->zoom - app->ruler_pointer;
    if (!(event->modifiers & TIKU_MOD_OPTION) &&
        fabs(x - app->ruler_source.position[app->ruler_part]) > 0.001)
        x = round(x);
    tiku_richruler_preview(&app->ruler_source, app->ruler_part, x,
                           &app->ruler_preview);
    describe(app, &app->ruler_preview);
    if (event->type == TIKU_EVENT_POINTER_UP) {
        app->ruler_drag = 0;
        if (fabs(app->ruler_preview.position[app->ruler_part] -
                 app->ruler_source.position[app->ruler_part]) > 0.001)
            commit(app, &app->ruler_source,
                   app->ruler_preview.position[app->ruler_part]);
    }
    return 1;
}

int
write_ruler_event(write_app_t *app, const tiku_event_t *event)
{
    int hit;

    if (app->ruler_drag) {
        if (event->type == TIKU_EVENT_KEY_DOWN ||
            event->type == TIKU_EVENT_TEXT_INPUT ||
            event->type == TIKU_EVENT_DEACTIVATED ||
            event->type == TIKU_EVENT_RESIZE ||
            event->type == TIKU_EVENT_WHEEL ||
            event->type == TIKU_EVENT_CLOSE || app->prompt || app->preview) {
            app->ruler_drag = 0;
            app->repaint = 1;
            if (event->type == TIKU_EVENT_KEY_DOWN &&
                event->key == TIKU_KEY_ESCAPE) return 1;
        } else if (drag(app, event)) return 1;
    }
    if (!app->show_ruler || app->preview || app->prompt || app->qcount) return 0;
    if (keyboard(app, event)) return 1;
    if (event->type != TIKU_EVENT_POINTER_DOWN || event->button != 1 ||
        !inside(write_ruler_bounds(app), event->x, event->y)) return 0;
    tiku_dialog_focus(&app->dialog, W_RULER_CONTROL);
    app->repaint = 1;
    hit = write_ruler_hit(app, event->x, event->y);
    if (!hit) return 1;
    app->ruler_part = hit - 1;
    tiku_richruler_read(app->view, &app->ruler_source);
    app->ruler_preview = app->ruler_source;
    app->ruler_pointer = (event->x - write_body(app).x) / app->zoom;
    app->ruler_drag = 1;
    app->body_drag = app->scroll_drag = 0;
    describe(app, &app->ruler_source);
    return 1;
}
