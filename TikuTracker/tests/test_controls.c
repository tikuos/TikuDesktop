/* Shared controls: behavior is tested without a display or hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tiku_dialog.h"
#include "tiku_choice_paint.h"
#include "tiku_field_event.h"
#include "tiku_clipboard.h"
#include "tiku_measure.h"
#include "tiku_ui.h"

static tiku_event_t key(unsigned k, unsigned mods)
{ return (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=k, .modifiers=mods}; }
static tiku_event_t pointer(tiku_event_type_t type, int x, int y)
{ return (tiku_event_t){.type=type, .button=1, .x=x, .y=y}; }

static void dialog_test(void)
{
    tiku_dialog_t d;
    tiku_event_t ev;
    tiku_dialog_init(&d);
    tiku_dialog_begin(&d, 3, 4);
    assert(!tiku_dialog_add(&d, 1, (tiku_rect_t){0,0,80,20}, TIKU_DIALOG_TOGGLE, 1));
    assert(!tiku_dialog_add(&d, 2, (tiku_rect_t){0,30,80,20}, TIKU_DIALOG_BUTTON, 0));
    assert(!tiku_dialog_add(&d, 3, (tiku_rect_t){0,60,80,20}, TIKU_DIALOG_BUTTON, 1));
    assert(!tiku_dialog_add(&d, 4, (tiku_rect_t){0,90,80,20}, TIKU_DIALOG_BUTTON, 1));
    assert(!tiku_dialog_add(&d, 5, (tiku_rect_t){0,120,80,20}, TIKU_DIALOG_INPUT, 1));
    assert(tiku_dialog_add(&d, 1, (tiku_rect_t){0,0,1,1}, TIKU_DIALOG_BUTTON, 1) < 0);
    tiku_dialog_end(&d);
    assert(d.focus == 1 && !tiku_dialog_focused(&d, 1));
    ev = key(TIKU_KEY_TAB, 0);
    assert(tiku_dialog_event(&d, &ev).taken && d.focus == 3);
    ev.modifiers = TIKU_MOD_SHIFT;
    tiku_dialog_event(&d, &ev); assert(d.focus == 1);
    tiku_dialog_event(&d, &ev); assert(d.focus == 5);
    ev = key(' ', 0); assert(!tiku_dialog_event(&d, &ev).taken);
    ev = key(TIKU_KEY_RETURN, 0);
    assert(tiku_dialog_event(&d, &ev).activated == 3);
    ev = key(TIKU_KEY_ESCAPE, 0);
    assert(tiku_dialog_event(&d, &ev).activated == 4);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, 5, 65);
    assert(tiku_dialog_event(&d, &ev).activated == -1);
    assert(tiku_dialog_pressed(&d, 3));
    ev = pointer(TIKU_EVENT_POINTER_MOVE, 99, 65);
    tiku_dialog_event(&d, &ev); assert(!tiku_dialog_pressed(&d, 3));
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_dialog_event(&d, &ev).activated == -1);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, 5, 65); tiku_dialog_event(&d, &ev);
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_dialog_event(&d, &ev).activated == 3);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, 5, 65); tiku_dialog_event(&d, &ev);
    ev.type = TIKU_EVENT_DEACTIVATED;
    tiku_dialog_event(&d, &ev); assert(d.capture == -1);
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_dialog_event(&d, &ev).activated == -1);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, 5, 35); tiku_dialog_event(&d, &ev);
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_dialog_event(&d, &ev).activated == -1);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, 5, 65); tiku_dialog_event(&d, &ev);
    tiku_dialog_begin(&d, 3, 4); /* Captured control disappears. */
    tiku_dialog_add(&d, 4, (tiku_rect_t){0,90,80,20}, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&d); assert(d.capture == -1 && d.focus == 4);
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_dialog_event(&d, &ev).activated == -1);
    tiku_dialog_begin(&d, -1, -1); tiku_dialog_end(&d);
    ev = key(TIKU_KEY_TAB, 0); assert(tiku_dialog_event(&d, &ev).taken);
    assert(d.focus == -1);
    for (int i = 0; i < TIKU_DIALOG_MAX; i++)
        assert(!tiku_dialog_add(&d, i, (tiku_rect_t){0,0,1,1}, TIKU_DIALOG_BUTTON, 1));
    assert(tiku_dialog_add(&d, 100, (tiku_rect_t){0,0,1,1}, TIKU_DIALOG_BUTTON, 1) < 0);
    puts("PASS: dialog focus, skipping, capture, release, default/cancel and limits");
}

static void choice_test(void)
{
    tiku_choice_t c;
    tiku_rect_t a = {20,210,180,26}, screen = {0,0,250,250}, popup;
    tiku_event_t ev;
    tiku_choice_init(&c);
    assert(tiku_choice_add(&c, "64 MHz", 1) == 0);
    assert(tiku_choice_add(&c, "unavailable", 0) == 1);
    assert(tiku_choice_add(&c, "128 MHz", 1) == 2);
    ev = key(' ', 0);
    assert(tiku_choice_event(&c, &ev, a, screen, 1) == TIKU_CHOICE_HANDLED);
    popup = tiku_choice_popup(&c, a, screen);
    assert(c.open && popup.y < a.y && popup.y >= 0);
    ev = key(TIKU_KEY_DOWN, 0); tiku_choice_event(&c, &ev, a, screen, 1);
    assert(c.hot == 2 && c.selected == 0);
    ev = key(TIKU_KEY_ESCAPE, 0); tiku_choice_event(&c, &ev, a, screen, 1);
    assert(!c.open && c.selected == 0);
    ev = key(TIKU_KEY_DOWN, 0); tiku_choice_event(&c, &ev, a, screen, 1);
    ev = key(TIKU_KEY_RETURN, 0);
    assert(tiku_choice_event(&c, &ev, a, screen, 1) == TIKU_CHOICE_CHANGED);
    assert(c.selected == 2 && !c.open);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, a.x+5, a.y+5);
    tiku_choice_event(&c, &ev, a, screen, 0); assert(c.open);
    ev.type = TIKU_EVENT_POINTER_UP;
    tiku_choice_event(&c, &ev, a, screen, 0); assert(c.open);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, popup.x+5, popup.y+5);
    tiku_choice_event(&c, &ev, a, screen, 0); assert(c.selected == 2);
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_choice_event(&c, &ev, a, screen, 0) == TIKU_CHOICE_CHANGED);
    assert(c.selected == 0);
    ev = key(' ', 0); tiku_choice_event(&c, &ev, a, screen, 1);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, 245, 5);
    assert(tiku_choice_event(&c, &ev, a, screen, 0) == TIKU_CHOICE_HANDLED);
    assert(!c.open && c.selected == 0);
    for (int i = c.count; i < TIKU_CHOICE_MAX; i++) {
        char label[24]; snprintf(label, sizeof label, "Option %d", i);
        assert(tiku_choice_add(&c, label, 1) == i);
    }
    assert(tiku_choice_add(&c, "overflow", 1) < 0);
    ev = key(TIKU_KEY_END, 0); tiku_choice_event(&c, &ev, a, screen, 1);
    assert(c.hot == TIKU_CHOICE_MAX-1 && c.top > 0);
    popup = tiku_choice_popup(&c, (tiku_rect_t){240,5,300,26}, screen);
    assert(popup.x >= 0 && popup.x+popup.w <= screen.w &&
           popup.y >= 0 && popup.y+popup.h <= screen.h);
    for (int dark = 0; dark < 2; dark++) {
        tiku_theme_set(dark ? tiku_theme_dark() : NULL);
        tiku_surface_t *s = tiku_surface_new(250,250,TIKU_C_PANEL);
        tiku_choice_draw(&c, s, a, NULL, TIKU_S_FOCUS);
        tiku_choice_draw_popup(&c, s, a, screen);
        assert(tiku_surface_png(s, dark ? "/tmp/tiku-choice-dark.png" :
                                         "/tmp/tiku-choice-light.png") == 0);
        tiku_surface_free(s);
    }
    tiku_theme_set(NULL);
    ev = key(TIKU_KEY_TAB, 0);
    assert(tiku_choice_event(&c, &ev, a, screen, 1) == TIKU_CHOICE_IGNORED);
    assert(!c.open && c.selected == 0);
    c.enabled = 0;
    ev = key(' ', 0);
    assert(tiku_choice_event(&c, &ev, a, screen, 1) == TIKU_CHOICE_IGNORED);
    assert(!c.open);
    c.enabled = 1;
    ev = key(' ', 0); tiku_choice_event(&c, &ev, a, screen, 1);
    assert(c.open);
    ev.type = TIKU_EVENT_DEACTIVATED;
    tiku_choice_event(&c, &ev, a, screen, 1);
    assert(!c.open && c.selected == 0);
    puts("PASS: dropdown keyboard, cancellation, pointer commit, limits and placement");
}

static void field_test(void)
{
    tiku_field_t e;
    tiku_field_input_t input;
    tiku_rect_t r = {10,10,160,26};
    tiku_event_t ev;
    tiku_field_start(&e, "old"); tiku_field_input_init(&input);
    ev = key('v', TIKU_MOD_CMD); tiku_clipboard_set("caf\xc3\xa9");
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_HANDLED);
    assert(!strcmp(e.text, "caf\xc3\xa9"));
    ev = key(TIKU_KEY_BACKSPACE, 0); tiku_field_event(&e, &input, &ev, r, 1);
    assert(!strcmp(e.text, "caf"));
    ev = key('z', TIKU_MOD_CMD); tiku_field_event(&e, &input, &ev, r, 1);
    assert(!strcmp(e.text, "caf\xc3\xa9") && e.caret == 5);
    ev.modifiers |= TIKU_MOD_SHIFT; tiku_field_event(&e, &input, &ev, r, 1);
    assert(!strcmp(e.text, "caf"));
    ev = key('a', TIKU_MOD_CMD); tiku_field_event(&e, &input, &ev, r, 1);
    char big[300]; memset(big, 'a', sizeof big-1); big[sizeof big-1] = 0;
    tiku_clipboard_set(big); ev = key('v', TIKU_MOD_CMD);
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_REJECTED);
    assert(!strcmp(e.text, "caf") && e.sel_a == 0 && e.sel_b == 3);
    tiku_clipboard_set("bad\xff");
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_REJECTED);
    tiku_field_deny(&e, "/"); tiku_clipboard_set("bad/name");
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_REJECTED);
    tiku_clipboard_set("good"); tiku_field_event(&e, &input, &ev, r, 1);
    assert(!strcmp(e.text, "good"));
    ev = pointer(TIKU_EVENT_POINTER_DOWN, r.x+6, r.y+4);
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_HANDLED);
    assert(e.caret == 0);
    /* The caret lands where the PAINTER puts the glyph: a click just past
     * the middle of the first character is caret 1, which a hit test
     * measuring from a different inset than tiku_ui_textfield_scroll
     * draws from cannot get right. */
    {
        int first = tiku_measure_width(TIKU_MEASURE_PLAIN, "g");

        ev = pointer(TIKU_EVENT_POINTER_DOWN,
                     r.x + 3 + first / 2 + 1, r.y + 4);
        assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_HANDLED);
        assert(e.caret == 1);
        ev = pointer(TIKU_EVENT_POINTER_DOWN, r.x + 3, r.y + 4);
        assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_HANDLED);
        assert(e.caret == 0);
    }
    ev = pointer(TIKU_EVENT_POINTER_MOVE, r.x+r.w+30, r.y+4);
    tiku_field_event(&e, &input, &ev, r, 1);
    assert(e.sel_a == 0 && e.sel_b == 4);
    ev.type = TIKU_EVENT_POINTER_UP; tiku_field_event(&e, &input, &ev, r, 1);
    assert(!input.dragging);
    ev = pointer(TIKU_EVENT_POINTER_DOWN, r.x+6, r.y+4);
    tiku_field_event(&e, &input, &ev, r, 1);
    assert(input.dragging);
    ev.type = TIKU_EVENT_DEACTIVATED;
    tiku_field_event(&e, &input, &ev, r, 0);
    assert(!input.dragging);
    ev = key('a', TIKU_MOD_CMD); tiku_field_event(&e, &input, &ev, r, 1);
    ev = key('c', TIKU_MOD_CMD); tiku_field_event(&e, &input, &ev, r, 1);
    assert(!strcmp(tiku_clipboard_text(), "good"));
    tiku_field_set_editable(&e, 0);
    ev = key('x', TIKU_MOD_CMD); tiku_field_event(&e, &input, &ev, r, 1);
    assert(!strcmp(e.text, "good"));
    ev = key(TIKU_KEY_RETURN, 0);
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_COMMITTED && e.active);
    ev = key(TIKU_KEY_ESCAPE, 0);
    assert(tiku_field_event(&e, &input, &ev, r, 1) == TIKU_FIELD_CANCELLED && e.active);
    puts("PASS: field pointer selection, clipboard, undo/redo, UTF-8 and atomic rejection");
}
int main(void)
{
    dialog_test(); choice_test(); field_test();
    return 0;
}
