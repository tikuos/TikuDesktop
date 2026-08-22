/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_select_window.c - Select by pattern window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_select_window.h"

#define SELECT_WIDTH  360

/*
 * A field, a tick and a button are each one line of text tall, so each is
 * asked for.  Every one answers what was written here at 12 px, and the
 * window's own height falls out of them rather than being a number the
 * controls then have to fit inside.
 */
static int field_h(void) { return tiku_font_plain()->height + 5; }
static int tick_h(void)  { return tiku_font_plain()->height + 1; }
static int step(void)    { return field_h() + 8; }
static int ticks_y(void) { return 2 * step() + step(); }

/** @brief The whole window, title bar and all. */
static int select_height(void)
{
    return ticks_y() + tick_h() + 1 +
           tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER;
}

typedef enum {
    SELECT_HIT_NONE = 0,
    SELECT_HIT_EXPRESSION,
    SELECT_HIT_RULE,
    SELECT_HIT_IGNORE,
    SELECT_HIT_INVERT,
    SELECT_HIT_APPLY
} select_hit_t;

static void
layout(tiku_rect_t content, tiku_rect_t *expression,
       tiku_rect_t *rule, tiku_rect_t *ignore,
       tiku_rect_t *invert, tiku_rect_t *apply)
{
    int w = tiku_ui_button_w("Select", 70);

    *expression = (tiku_rect_t){ content.x + 10, content.y + step(),
                                      content.w - 20, field_h() };
    *rule = (tiku_rect_t){ content.x + 10, content.y + 2 * step(),
                                content.w - 20, field_h() };
    *ignore = (tiku_rect_t){ content.x + 10, content.y + ticks_y(),
                                  tiku_ui_button_w("Ignore case", 110),
                                  tick_h() };
    *invert = (tiku_rect_t){ content.x + 130, content.y + ticks_y(),
                                  tiku_ui_button_w("Invert selection", 120),
                                  tick_h() };
    *apply = (tiku_rect_t){ content.x + content.w - w - 10,
                                 content.y + content.h - tiku_ui_row_h() - 6,
                                 w, tiku_ui_row_h() };
}

static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_select_window_t *controller = context;
    const tiku_font_t *font = tiku_font_plain();
    tiku_rect_t expression, rule, ignore, invert, apply;
    char label[96];

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    layout(content, &expression, &rule, &ignore, &invert, &apply);
    tiku_text(surface, font, content.x + 10, content.y + font->ascent + 6,
                   "Select items whose name", TIKU_C_TEXT);
    tiku_ui_textfield_sel(surface, expression,
        controller->model.expr.text,
        controller->field == SELECT_HIT_EXPRESSION
            ? controller->model.expr.caret : -1,
        controller->model.expr.sel_a, controller->model.expr.sel_b,
        controller->field == SELECT_HIT_EXPRESSION ? TIKU_S_FOCUS : 0u);
    snprintf(label, sizeof label, "%s",
             tiku_trk_select_rule_label(controller->model.rule));
    tiku_ui_menufield(surface, rule, label, 0u);
    tiku_ui_checkbox(surface, ignore, "Ignore case",
        controller->model.ignore_case ? TIKU_S_ON : 0u);
    tiku_ui_checkbox(surface, invert, "Invert selection",
        controller->model.invert ? TIKU_S_ON : 0u);
    tiku_ui_button(surface, apply, "Select", 0u);
}

static select_hit_t
hit(tiku_trk_select_window_t *controller, int x, int y)
{
    tiku_rect_t content, expression, rule, ignore, invert, apply;

    if (controller->window == NULL) {
        return SELECT_HIT_NONE;
    }
    content = tiku_window_content(controller->window);
    layout(content, &expression, &rule, &ignore, &invert, &apply);
#define INSIDE(rect) (x >= (rect).x && x < (rect).x + (rect).w && \
                      y >= (rect).y && y < (rect).y + (rect).h)
    if (INSIDE(expression)) { return SELECT_HIT_EXPRESSION; }
    if (INSIDE(rule))       { return SELECT_HIT_RULE; }
    if (INSIDE(ignore))     { return SELECT_HIT_IGNORE; }
    if (INSIDE(invert))     { return SELECT_HIT_INVERT; }
    if (INSIDE(apply))      { return SELECT_HIT_APPLY; }
#undef INSIDE
    return SELECT_HIT_NONE;
}

static void
mark_dirty(tiku_trk_select_window_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
apply(tiku_trk_select_window_t *controller)
{
    tiku_window_t *target;

    if (controller->apply == NULL) {
        return;
    }
    target = controller->apply(controller->apply_context, controller->target,
                               &controller->model);
    if (target == NULL) {
        return;
    }
    tiku_trk_select_hide(&controller->model);
    if (controller->window != NULL) {
        tiku_workspace_close(controller->workspace, controller->window);
        controller->window = NULL;
    }
    tiku_workspace_activate(controller->workspace, target);
}

static int
event(tiku_window_t *window, const tiku_event_t *event,
      void *context)
{
    tiku_trk_select_window_t *controller = context;

    (void)window;
    if (event->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    switch (hit(controller, event->x, event->y)) {
    case SELECT_HIT_EXPRESSION:
        controller->field = SELECT_HIT_EXPRESSION;
        break;
    case SELECT_HIT_RULE:
        tiku_trk_select_set_rule(&controller->model,
            (controller->model.rule + 1) % TIKU_TRK_SELECT_RULES);
        break;
    case SELECT_HIT_IGNORE:
        controller->model.ignore_case = !controller->model.ignore_case;
        break;
    case SELECT_HIT_INVERT:
        controller->model.invert = !controller->model.invert;
        break;
    case SELECT_HIT_APPLY:
        apply(controller);
        break;
    default:
        return 0;
    }
    mark_dirty(controller);
    return 1;
}

void
tiku_trk_select_window_init(tiku_trk_select_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_select_apply_fn apply_callback,
    void *apply_context)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->apply = apply_callback;
    controller->apply_context = apply_context;
    tiku_trk_select_init(&controller->model);
}

tiku_trk_select_t *
tiku_trk_select_window_model(tiku_trk_select_window_t *controller)
{
    return &controller->model;
}

tiku_window_t *
tiku_trk_select_window_handle(tiku_trk_select_window_t *controller)
{
    return controller->window;
}

void
tiku_trk_select_window_open(tiku_trk_select_window_t *controller,
                            void *target, int x, int y)
{
    tiku_rect_t frame;

    if (target == NULL) {
        return;
    }
    controller->target = target;
    tiku_trk_select_show(&controller->model, x, y, SELECT_WIDTH,
                         select_height(), controller->screen_w,
                         controller->screen_h);
    frame = (tiku_rect_t){ controller->model.x, controller->model.y,
                                SELECT_WIDTH, select_height() };
    if (controller->window != NULL && controller->window->open) {
        controller->window->frame = frame;
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
    } else {
        controller->window = tiku_workspace_open(controller->workspace,
            "Select", frame, draw, controller, &controller->model);
        if (controller->window != NULL) {
            tiku_window_set_handlers(controller->window, event,
                                           NULL, NULL);
        }
    }
    controller->field = SELECT_HIT_EXPRESSION;
    controller->model.expr.active = 1;
    mark_dirty(controller);
}

int
tiku_trk_select_window_key(tiku_trk_select_window_t *controller,
    int key, unsigned modifiers, char character)
{
    tiku_trk_edit_key_t edit_key = TIKU_TRK_EDIT_CHAR;
    int shift = (modifiers & TIKU_MOD_SHIFT) != 0u;

    if (controller->window == NULL || !controller->window->open ||
        tiku_workspace_focused(controller->workspace) !=
            controller->window) {
        return 0;
    }
    switch (key) {
    case TIKU_KEY_RETURN:
        apply(controller);
        mark_dirty(controller);
        return 1;
    case TIKU_KEY_ESCAPE:
        tiku_trk_select_hide(&controller->model);
        tiku_workspace_close(controller->workspace, controller->window);
        controller->window = NULL;
        mark_dirty(controller);
        return 1;
    case TIKU_KEY_BACKSPACE: edit_key = TIKU_TRK_EDIT_BACKSPACE; break;
    case TIKU_KEY_DELETE:    edit_key = TIKU_TRK_EDIT_DELETE;    break;
    case TIKU_KEY_LEFT:      edit_key = TIKU_TRK_EDIT_LEFT;      break;
    case TIKU_KEY_RIGHT:     edit_key = TIKU_TRK_EDIT_RIGHT;     break;
    case TIKU_KEY_HOME:      edit_key = TIKU_TRK_EDIT_HOME;      break;
    case TIKU_KEY_END:       edit_key = TIKU_TRK_EDIT_END;       break;
    case TIKU_KEY_TAB:
        tiku_trk_select_set_rule(&controller->model,
            (controller->model.rule + 1) % TIKU_TRK_SELECT_RULES);
        mark_dirty(controller);
        return 1;
    default:
        if (character == '\0') {
            return 1;
        }
        break;
    }
    (void)tiku_trk_edit_key(&controller->model.expr, edit_key, shift,
                            character);
    mark_dirty(controller);
    return 1;
}
