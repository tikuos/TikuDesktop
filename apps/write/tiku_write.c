/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write.c - independent rich document application and window lifecycle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_fontset.h"
#include "tiku_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Schedule toolbar/menu refresh after a session's endpoints change. */
static void
selection_changed(void *context, tiku_richsession_t *session,
                  const tiku_rich_selection_change_t *change)
{
    write_app_t *app = context;

    (void)session;
    (void)change;
    write_links_clear(app);
    app->bookmark_pending = 0;
    app->repaint = 1;
}

int
write_adopt(write_app_t *app, tiku_richdoc_t *doc)
{
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 600, .leading = 3};

    options.provider = tiku_richfont_provider(app->fonts);
    if (tiku_richview_create(doc, &options, &view) != TIKU_RICH_OK) return -1;
    write_search_cancel(app);
    write_outline_clear(app);
    write_links_clear(app);
    app->ruler_drag = 0;
    app->bookmark_pending = 0;
    app->bookmark_menu_generation = 0;
    memset(app->menu_bookmarks, 0, sizeof app->menu_bookmarks);
    tiku_richpages_release(app->pages);
    app->pages = NULL;
    app->pages_generation = 0;
    tiku_richview_destroy(app->view);
    tiku_richdoc_release(app->doc);
    app->doc = doc;
    app->view = view;
    tiku_richsession_observe_selection(tiku_richview_session(view),
                                        selection_changed, app);
    app->clip_action = 0;
    app->qfirst = app->qcount = 0;
    app->match.generation = app->recovered_generation = 0;
    app->reveal = app->repaint = 1;
    app->edited_at = app->now;
    app->prompt = app->pending = 0;
    app->panel = 0;
    app->preview = 0;
    if (*app->recovery) unlink(app->recovery);
    write_controls(app);
    tiku_dialog_focus(&app->dialog, W_BODY);
    return 0;
}

int
write_fonts(tiku_richfont_t **out)
{
    static const char *const names[] = {"sans-serif", "serif", "monospace"};
    tiku_fontset_face_t found[12];
    tiku_richfont_source_t source[12];
    size_t count = 0;

    for (unsigned family = 0; family < 3; family++)
        for (unsigned variant = 0; variant < 4; variant++) {
            if (tiku_fontset_find(names[family], variant & 1 ? 700 : 400,
                !!(variant & 2), &found[count])) continue;
            source[count] = (tiku_richfont_source_t){found[count].path,
                (unsigned)found[count].index, names[family],
                variant & 1, !!(variant & 2)};
            count++;
        }
    return tiku_richfont_open(source, count, out) == TIKU_RICH_OK ?
           0 : -1;
}

/** @brief Handle global shortcuts; text fields retain their editing keys. */
static int
shortcut(write_app_t *app, const tiku_event_t *event)
{
    unsigned key = event->key;
    int body = app->dialog.focus == W_BODY;
    int shift = !!(event->modifiers & TIKU_MOD_SHIFT);

    if (event->type != TIKU_EVENT_KEY_DOWN ||
        !(event->modifiers & TIKU_MOD_CMD)) return 0;
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    switch (key) {
    case 'n': return W_NEW;
    case 'o': return W_OPEN;
    case 's': return shift ? W_SAVEAS : W_SAVE;
    case 'q': case 'w': return W_CLOSE;
    case 'f': return W_FIND;
    case 'g': return W_NEXT;
    case '+': case '=': return W_ZOOM_IN;
    case '-': return W_ZOOM_OUT;
    case '0': return W_ZOOM_RESET;
    case 'c': return body ? W_COPY : 0;
    case 'x': return body ? W_CUT : 0;
    case 'v': return body ? (shift ? W_PASTE_TEXT : W_PASTE) : 0;
    default: return 0;
    }
}

/** @brief Accept replacement pixels before changing any view dimensions. */
static void
resize(write_app_t *app, int w, int h)
{
    tiku_surface_t *surface;

    if (w < 320 || h < 240 || w > 8192 || h > 8192 ||
        (uint64_t)w * h > 16000000) return;
    surface = tiku_surface_new(w, h, TIKU_C_PANEL);
    if (!surface) {
        snprintf(app->status, sizeof app->status,
                 "Resize failed: out of memory.");
        app->repaint = 1;
        return;
    }
    tiku_surface_free(app->surface);
    app->surface = surface;
    app->width = w;
    app->height = h;
    write_controls(app);
    app->repaint = 1;
}

/** @brief Handle scrollbar clicks and thumb dragging using document points. */
static int
scrollbar(write_app_t *app, const tiku_event_t *event)
{
    tiku_rect_t body = write_body(app);
    tiku_rect_t bar = {app->width - 19, body.y - 12, 16, body.h + 24};
    double height = tiku_richlayout_height(tiku_richview_layout(app->view));
    double visible = body.h / app->zoom, maximum = fmax(0, height - visible);
    double y = tiku_richview_scroll_y(app->view);
    tiku_rect_t thumb = tiku_ui_scrollbar_thumb(bar,
        maximum ? y / maximum : 0, height > visible ? visible / height : 1, 0);

    if (event->type == TIKU_EVENT_POINTER_UP && app->scroll_drag) {
        app->scroll_drag = 0;
        return 1;
    }
    if (event->type == TIKU_EVENT_POINTER_MOVE && app->scroll_drag) {
        int track = bar.h - 32 - thumb.h;
        if (track > 0)
            y = maximum * (event->y - app->scroll_grab - bar.y - 16) / track;
    } else if (event->type == TIKU_EVENT_POINTER_DOWN && event->button == 1 &&
        event->x >= bar.x && event->x < bar.x + bar.w &&
        event->y >= bar.y && event->y < bar.y + bar.h) {
        if (event->y < bar.y + 16) y -= 24;
        else if (event->y >= bar.y + bar.h - 16) y += 24;
        else if (event->y >= thumb.y && event->y < thumb.y + thumb.h) {
            app->scroll_drag = 1;
            app->scroll_grab = event->y - thumb.y;
        } else y += event->y < thumb.y ? -visible * 0.9 : visible * 0.9;
    } else return 0;
    tiku_richview_scroll(app->view, y);
    app->repaint = 1;
    return 1;
}

/** @brief Route input to questions, fields or the shared rich editor. */
static int
event(void *state, const tiku_event_t *ev)
{
    write_app_t *app = state;
    tiku_dialog_result_t result;
    int action = shortcut(app, ev), body_key;

    app->now = ev->time_us > app->now ? ev->time_us : app->now;
    if (!action && app->preview && ev->type == TIKU_EVENT_KEY_DOWN)
        action = ev->key == TIKU_KEY_ESCAPE ? W_PREVIEW :
            ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_PAGE_UP ?
            W_PREV_PAGE : ev->key == TIKU_KEY_RIGHT ||
            ev->key == TIKU_KEY_PAGE_DOWN ? W_NEXT_PAGE : 0;
    if (write_ruler_event(app, ev)) { }
    else if (ev->type == TIKU_EVENT_RESIZE) resize(app, ev->width, ev->height);
    else if (ev->type == TIKU_EVENT_CLOSE) write_action(app, W_CLOSE);
    else if (ev->type == TIKU_EVENT_EXPOSE) app->repaint = 1;
    else if (ev->type == TIKU_EVENT_ACTIVATED ||
             ev->type == TIKU_EVENT_DEACTIVATED) {
        app->active = ev->type == TIKU_EVENT_ACTIVATED;
        app->body_drag = app->scroll_drag = 0;
        write_input(app, ev);
        app->repaint = 1;
    } else if (action) write_action(app, action);
    else if (app->body_drag && !app->prompt && !app->preview &&
             (ev->type == TIKU_EVENT_POINTER_MOVE ||
              (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1))) {
        write_input(app, ev);
        if (ev->type == TIKU_EVENT_POINTER_UP) app->body_drag = 0;
    }
    else if (!app->prompt && !app->preview && write_outline_event(app, ev)) { }
    else if (!app->prompt && !app->preview && scrollbar(app, ev)) { }
    else {
        write_controls(app);
        body_key = !app->prompt && app->dialog.focus == W_BODY &&
            (ev->type == TIKU_EVENT_KEY_DOWN ||
             ev->type == TIKU_EVENT_TEXT_INPUT);
        result = body_key ? (tiku_dialog_result_t){0, -1} :
                           tiku_dialog_event(&app->dialog, ev);
        if (result.activated >= 0) write_action(app, result.activated);
        if (result.taken) app->repaint = 1;
        if (!app->prompt && !result.taken) {
            if (app->dialog.focus == W_FIELD ||
                app->dialog.focus == W_FIELD2) {
                int k = app->dialog.focus - W_FIELD;
                tiku_rect_t rect = {92, k ? 84 : 52,
                    app->width - (app->width < 660 ? 108 : 398), 24};
                char previous[TIKU_FIELD_MAX];

                strcpy(previous, app->fields[k].text);
                tiku_field_event(&app->fields[k], &app->field_input[k],
                                  ev, rect, 1);
                if (strcmp(previous, app->fields[k].text)) {
                    write_search_cancel(app);
                    app->match.generation = 0;
                }
                app->repaint = 1;
            } else if (app->dialog.focus == W_BODY) {
                if (ev->type == TIKU_EVENT_POINTER_DOWN)
                    app->body_drag = ev->button == 1;
                if (ev->type != TIKU_EVENT_POINTER_MOVE || app->body_drag)
                    write_input(app, ev);
                if (ev->type == TIKU_EVENT_POINTER_UP) app->body_drag = 0;
            }
        }
    }
    write_step(app);
    write_pages_step(app);
    write_pointer(app, ev);
    if (app->repaint) write_paint(app);
    return app->closing;
}

/** @brief Collect I/O, process bounded layout/search and schedule recovery. */
static void
tick(void *state, int64_t now)
{
    write_app_t *app = state;

    app->now = now;
    write_clip_tick(app);
    write_job_finish(app, 0);
    if (app->closing) return;
    write_step(app);
    if (app->active && !app->prompt && !app->preview && !app->panel &&
        !app->qcount && app->body_drag &&
        tiku_richview_tick(app->view, now) == TIKU_RICH_OK)
        app->repaint = 1;
    write_pages_step(app);
    write_search_step(app);
    write_outline_step(app);
    write_links_step(app);
    write_bookmarks_step(app);
    write_recovery_tick(app);
    if (app->repaint) write_paint(app);
}

/** @brief Keep unsaved text alive when the host closes its window directly. */
static void
closed(void *state, uint32_t id)
{
    write_app_t *app = state;

    if (id != app->id) return;
    app->id = 0;
    if (!app->closing && (write_dirty(app) || app->job || app->qcount)) {
        app->id = app->services->open(app->services->ctx,
            "TikuWrite — Unsaved changes", app->width, app->height);
        app->pending = W_CLOSE;
        app->prompt = W_PROMPT_UNSAVED;
        app->repaint = 1;
        write_paint(app);
    }
}

/** @brief Route menus, buttons and shortcuts to the same actions. */
static int
pick(void *state, uint32_t window, int command)
{
    write_app_t *app = state;

    if (window == app->id) {
        write_action(app, command);
        write_step(app);
        if (app->repaint) write_paint(app);
    }
    return app->closing;
}

/** @brief Finish accepted input and retain a checkpoint on forced shutdown. */
static void
stop(void *state)
{
    write_app_t *app = state;
    write_version_t version;
    int discard;

    while (app->qcount &&
        (tiku_richview_status(app->view) == TIKU_RICH_PENDING ||
         tiku_richview_status(app->view) == TIKU_RICH_OK))
        write_step(app);
    if (app->job) {
        if (app->job->kind == W_JOB_LOAD || app->job->kind == W_JOB_IMAGE)
            atomic_store(&app->job->cancel, 1);
        write_job_finish(app, 1);
    }
    discard = app->closing || !write_dirty(app);
    if (!discard && *app->recovery &&
        !write_version(app->recovery, &version) &&
        !write_job_start(app, W_JOB_RECOVERY, app->recovery, &version, 0))
        write_job_finish(app, 1);
    write_recovery_stop(app, discard);
    write_search_cancel(app);
    write_outline_clear(app);
    write_links_clear(app);
    tiku_richpages_release(app->pages);
    tiku_richview_destroy(app->view);
    tiku_richdoc_release(app->doc);
    tiku_richfont_release(app->fonts);
    tiku_surface_free(app->surface);
    free(app);
}

/** @brief Start empty, or asynchronously load the explicitly supplied file. */
static int
start(void **state, const tiku_app_services_t *services)
{
    write_app_t *app = calloc(1, sizeof *app);
    tiku_richdoc_t *doc = NULL;

    if (!app) return -1;
    app->services = services;
    app->width = 820;
    app->height = 600;
    app->zoom = 1.0;
    app->active = 1;
    app->recovery_lock = -1;
    tiku_dialog_init(&app->dialog);
    for (int i = 0; i < 2; i++) {
        tiku_field_start(&app->fields[i], "");
        tiku_field_input_init(&app->field_input[i]);
    }
    app->surface = tiku_surface_new(app->width, app->height, TIKU_C_PANEL);
    if (!app->surface || write_fonts(&app->fonts) ||
        tiku_richdoc_create(NULL, &doc) != TIKU_RICH_OK ||
        write_adopt(app, doc)) {
        tiku_richdoc_release(doc);
        tiku_richfont_release(app->fonts);
        tiku_surface_free(app->surface);
        free(app);
        return -1;
    }
    app->id = services->open(services->ctx, "Untitled — TikuWrite",
                             app->width, app->height);
    if (!app->id) { stop(app); return -1; }
    write_recovery_init(app);
    if (services->startup_document && *services->startup_document)
        write_job_start(app, W_JOB_LOAD, services->startup_document, NULL, 0);
    else if (*app->offered)
        snprintf(app->status, sizeof app->status,
                 "An earlier draft is available: File > Recover last draft.");
    if (services->help_page)
        services->help_page(services->ctx, "apps/write.html");
    write_step(app);
    write_paint(app);
    *state = app;
    write_clip_probe(app);
    return 0;
}

/** @brief Independent application descriptor using public desktop kits. */
const tiku_app_descriptor_t tiku_write_app = {
    .id = "org.tikuos.write", .name = "TikuWrite",
    .start = start, .stop = stop, .tick = tick, .event = event,
    .pick = pick, .closed = closed, .picked = write_picked,
    .message = write_clip_message
};
#ifdef TIKU_APP_SO
/** @brief Versioned descriptor entry loaded by the application runner. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_write_app
};
#endif
