/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * draw_demo.c - a window of the board's own, built from the desktop's
 * kit: tabs, a list, a slider, a field and a button, the same state
 * objects and the same paint calls a desktop program uses; a menu bar
 * written as text, a window the person may resize, and the desk's file
 * panel asked for a path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw.h"

#include "tiku_event.h"
#include "tiku_field.h"
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_list.h"
#include "tiku_list_paint.h"
#include "tiku_slider.h"
#include "tiku_slider_paint.h"
#include "tiku_tabs.h"
#include "tiku_tabs_paint.h"
#include "tiku_ui.h"
#include "tiku_layout.h"

#include <stdio.h>
#include <string.h>

TIKU_PROCESS(draw_demo_process, "draw-demo");

static tiku_draw_window_t win;
static unsigned char list_buf[TIKU_DRAW_LIST_MAX];
static tiku_surface_t surface;
static tiku_tabs_t tabs;
static tiku_list_t rows;
static tiku_slider_t level;
static tiku_field_t name;
static int presses, editing;

static const char *const row_names[] = {
    "Temperature", "Humidity", "Pressure", "Light", "Sound"
};

static const char *const menus_text =
    "menu File\n"
    "item 1 o Choose a file…\n"
    "sep\n"
    "item 9 q Reset\n"
    "menu Level\n"
    "item 2 - Up\n"
    "item 3 - Down\n"
    "menu Edit\n"
    "item 4 c Copy the name\n";

/**
 * @brief The layout: a stack of rows down the window, said as weights
 *        and least heights rather than pixels, so a resize lays it out
 *        again and the board and the desktop divide it the same way.
 */
static tiku_rect_t
place(int which)
{
    tiku_rect_t body = { 0, 0, win.w, win.h };
    tiku_layout_t stack;
    tiku_rect_t row, r;

    tiku_layout_init(&stack, 0, 8, 12);
    (void)tiku_layout_add(&stack, 22, 0, 22);           /* the tabs   */
    (void)tiku_layout_add(&stack, 0, 1, tiku_ui_row_h()); /* the list */
    (void)tiku_layout_add(&stack, 24, 0, 24);           /* the slider */
    (void)tiku_layout_add(&stack, 24, 0, 24);           /* the field  */
    (void)tiku_layout_add(&stack, tiku_ui_button_h(), 0,
                          tiku_ui_button_h());          /* the button */
    row = tiku_layout_slot(&stack, body, which);
    if (which == 4) {
        /* The button is only as wide as its word; the rest of its row
         * is the space the count is written in. */
        r = row;
        r.w = tiku_ui_button_w("Count", 80);
        return r;
    }
    return row;
}

/** @brief The button's row, for the count written beside it. */
static tiku_rect_t
button_row(void)
{
    tiku_rect_t body = { 0, 0, win.w, win.h };
    tiku_layout_t stack;

    tiku_layout_init(&stack, 0, 8, 12);
    (void)tiku_layout_add(&stack, 22, 0, 22);
    (void)tiku_layout_add(&stack, 0, 1, tiku_ui_row_h());
    (void)tiku_layout_add(&stack, 24, 0, 24);
    (void)tiku_layout_add(&stack, 24, 0, 24);
    (void)tiku_layout_add(&stack, tiku_ui_button_h(), 0, tiku_ui_button_h());
    return tiku_layout_slot(&stack, body, 4);
}

/** @brief A row's name, asked for by the list. */
static const char *
row_name(void *ctx, int row)
{
    (void)ctx;
    return (row >= 0 && row < 5) ? row_names[row] : "";
}

/** @brief The whole window, from its state. */
static void
paint(void)
{
    char line[40];

    tiku_draw_begin(&win);
    tiku_ui_panel(&surface, (tiku_rect_t){ 0, 0, win.w, win.h });
    tiku_tabs_draw(&tabs, &surface, place(0));
    tiku_ui_sunken(&surface, place(1), TIKU_C_DOC);
    tiku_list_draw(&rows, &surface, tiku_inset(place(1), 1), row_name, NULL);
    tiku_slider_draw(&level, &surface, place(2));
    tiku_ui_textfield(&surface, place(3), name.text, editing ? name.caret : -1,
                      editing ? TIKU_S_FOCUS : 0u);
    tiku_ui_button(&surface, place(4), "Count", 0u);
    snprintf(line, sizeof line, "Pressed %d time%s, level %d", presses,
             presses == 1 ? "" : "s", level.value);
    {
        tiku_rect_t brow = button_row();

        tiku_text(&surface, tiku_font_plain(),
                  place(4).x + place(4).w + 10, brow.y + 17, line,
                  TIKU_C_TEXT);
    }
    (void)tiku_draw_present(&win);
}

/** @brief Whether (@p x, @p y) lies in @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief One key for the field, in the field's own vocabulary. */
static void
key(const tiku_draw_event_t *e)
{
    tiku_field_key_t k = TIKU_FIELD_CHAR;
    char ch = e->text[0];

    switch (e->key) {
    case TIKU_KEY_LEFT:      k = TIKU_FIELD_LEFT; break;
    case TIKU_KEY_RIGHT:     k = TIKU_FIELD_RIGHT; break;
    case TIKU_KEY_HOME:      k = TIKU_FIELD_HOME; break;
    case TIKU_KEY_END:       k = TIKU_FIELD_END; break;
    case TIKU_KEY_BACKSPACE: k = TIKU_FIELD_BACKSPACE; break;
    case TIKU_KEY_DELETE:    k = TIKU_FIELD_DELETE; break;
    case TIKU_KEY_RETURN:    k = TIKU_FIELD_COMMIT; break;
    case TIKU_KEY_ESCAPE:    k = TIKU_FIELD_CANCEL; break;
    default:
        if (ch < ' ') {
            return;
        }
        break;
    }
    if (tiku_field_key(&name, k, 0, ch) != TIKU_FIELD_HANDLED) {
        editing = 0;
    }
}

/** @brief A press: whichever control is under it takes it. */
static void
press(const tiku_draw_event_t *e)
{
    int hit = tiku_tabs_hit(&tabs, place(0), e->x, e->y);

    if (hit >= 0) {
        (void)tiku_tabs_select(&tabs, hit);
    } else if (inside(place(1), e->x, e->y)) {
        int row = tiku_list_at(&rows, tiku_inset(place(1), 1), e->x, e->y);

        if (row >= 0) {
            tiku_list_click(&rows, row, e->modifiers);
        }
    } else if (inside(place(2), e->x, e->y)) {
        (void)tiku_slider_press(&level, place(2), e->x);
    } else if (inside(place(3), e->x, e->y)) {
        if (!editing) {
            tiku_field_start(&name, name.text);
            editing = 1;
        }
    } else if (inside(place(4), e->x, e->y)) {
        presses++;
    }
}

TIKU_PROCESS_THREAD(draw_demo_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    (void)tiku_process_register("draw-demo", &draw_demo_process);
    tiku_tabs_init(&tabs);
    (void)tiku_tabs_add(&tabs, "Sensors");
    (void)tiku_tabs_add(&tabs, "Switches");
    tiku_list_init(&rows, 1);
    (void)tiku_list_set_count(&rows, 5);
    tiku_slider_init(&level, 0, 100, 40, 5);
    memset(&name, 0, sizeof name);
    snprintf(name.text, sizeof name.text, "workshop");
    name.len = (int)strlen(name.text);
    (void)tiku_draw_open(&win, "Board window", 260, 262, &draw_demo_process,
                         list_buf, sizeof list_buf);
    tiku_draw_menus(&win, menus_text);
    tiku_draw_sizeable(&win);
    tiku_surface_on_window(&surface, &win);
    paint();

    while (1) {
        tiku_draw_window_t *w;
        int changed = 0;

        TIKU_PROCESS_WAIT_EVENT();
        while ((w = tiku_draw_event(ev, data)) != NULL) {
            const tiku_draw_event_t *e = &w->event;

            switch (e->kind) {
            case TIKU_DRAW_POINTER_DOWN:
                press(e);
                changed = 1;
                break;
            case TIKU_DRAW_POINTER_MOVE:
                if (level.tracking) {
                    (void)tiku_slider_drag(&level, place(2), e->x);
                    changed = 1;
                }
                break;
            case TIKU_DRAW_POINTER_UP:
                tiku_slider_release(&level);
                break;
            case TIKU_DRAW_KEY_DOWN:
                if (editing) {
                    key(e);
                    changed = 1;
                }
                break;
            case TIKU_DRAW_RESIZE:
                /* The size is in the window already; the layout follows. */
                changed = 1;
                break;
            case TIKU_DRAW_PICK:
                switch (e->command) {
                case 1: tiku_draw_pick(w, 0, "/tmp", NULL); break;
                case 2: (void)tiku_slider_step(&level, 1); break;
                case 3: (void)tiku_slider_step(&level, -1); break;
                case 4: tiku_draw_clipboard(name.text); break;
                case 9: presses = 0; break;
                default: break;
                }
                changed = 1;
                break;
            case TIKU_DRAW_PICKED:
                /* The path chosen lands in the field. */
                memset(&name, 0, sizeof name);
                snprintf(name.text, sizeof name.text, "%s", e->path);
                name.len = (int)strlen(name.text);
                editing = 0;
                changed = 1;
                break;
            case TIKU_DRAW_SESSION:
                /* A desktop arrived: the window starts as it did at boot,
                 * so what it shows is this session's doing. */
                presses = 0;
                editing = 0;
                (void)tiku_tabs_select(&tabs, 0);
                tiku_list_select_none(&rows);
                (void)tiku_slider_set(&level, 40);
                memset(&name, 0, sizeof name);
                snprintf(name.text, sizeof name.text, "workshop");
                name.len = (int)strlen(name.text);
                changed = 1;
                break;
            default:
                break;
            }
        }
        if (changed) {
            paint();
        }
    }

    TIKU_PROCESS_END();
}
