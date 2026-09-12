/* Shared preferences kit, with no display server or physical device.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "tiku_preferences_layout.h"
#include "tiku_properties_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_dl.h"

static void text_test(void)
{
    char out[256];
    const int face = TIKU_MEASURE_PLAIN;
    const char *sample = "caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8c\x8d";
    int width = tiku_measure_width(face, sample);
    assert(!tiku_text_elide(face, sample, width, out, sizeof out));
    assert(!strcmp(out, sample));
    for (int w = 0; w < width; w++) {
        assert(tiku_text_elide(face, sample, w, out, sizeof out));
        assert(tiku_measure_width(face, out) <= w);
        assert(!*out || strstr(out, TIKU_TEXT_ELLIPSIS));
    }
    /* Every byte capacity is exercised, including the middle of a codepoint. */
    for (size_t cap = 1; cap < strlen(sample) + 1; cap++) {
        memset(out, '!', sizeof out);
        assert(tiku_text_elide(face, sample, 500, out, cap));
        assert(strlen(out) < cap && out[cap] == '!');
        assert(!*out || strstr(out, TIKU_TEXT_ELLIPSIS));
    }
    assert(tiku_text_elide(face, "x", 0, NULL, 0));
    assert(!tiku_text_elide(face, NULL, 0, out, sizeof out) && !*out);
    assert(!tiku_text_elide(face, "a\xff" "b", 100, out, sizeof out));
    assert(!strcmp(out, "a?b"));
    assert(!tiku_text_elide(face, "\xe2", 100, out, sizeof out));
    assert(!strcmp(out, "?"));
    const char *text = "alpha beta gamma";
    int fit = tiku_measure_width(face, "alpha be");
    assert(tiku_text_wrap_next(face, &text, fit, out, sizeof out, 0));
    assert(!strcmp(out, "alpha") && !strcmp(text, "beta gamma"));
    assert(tiku_text_wrap_next(face, &text, fit, out, sizeof out, 1));
    assert(strstr(out, TIKU_TEXT_ELLIPSIS) && !*text);
    assert(!tiku_text_wrap_next(face, &text, fit, out, sizeof out, 0));
    text = "\nA\r\n\nB";
    const char *want[] = {"", "A", "", "B"};
    for (int i = 0; i < 4; i++) {
        assert(tiku_text_wrap_next(face, &text, 200, out, sizeof out, 0));
        assert(!strcmp(out, want[i]));
    }
    assert(!*text);
    text = "\xc3\xa9\xc3\xa9\xc3\xa9";
    fit = tiku_measure_width(face, "\xc3\xa9");
    for (int i = 0; i < 3; i++) {
        assert(tiku_text_wrap_next(face, &text, fit, out, sizeof out, 0));
        assert(!strcmp(out, "\xc3\xa9"));
    }
    assert(!*text);
    /* Too-narrow rows make progress, without splitting or overflowing. */
    text = sample;
    int count = 0;
    while (tiku_text_wrap_next(face, &text, 0, out, sizeof out, 0)) {
        assert(!*out && ++count <= (int)strlen(sample));
    }
    puts("PASS: ellipsis, wrapping, blank lines, malformed input and UTF-8 boundaries");
}

static void layout_test(void)
{
    tiku_preferences_action_t utility = {"Open nothing", 112};
    tiku_preferences_action_t actions[] = {{"Cancel", 82}, {"Save changes", 82}};
    tiku_rect_t leading, out[2];
    int w = tiku_preferences_actions_width(&utility, actions, 2, 12);
    tiku_rect_t body = {17, 29, w + 90, tiku_ui_button_h()};
    assert(tiku_preferences_actions(body, &utility, actions, 2, 12, &leading, out) == 1);
    assert(leading.x == body.x && out[1].x + out[1].w == body.x + body.w);
    assert(leading.x + leading.w + 12 <= out[0].x);
    assert(out[0].x + out[0].w + 12 == out[1].x);
    assert(out[1].w == tiku_ui_button_w("Save changes", 82));
    body.w = w - 1;
    assert(!tiku_preferences_actions(body, &utility, actions, 2, 12, &leading, out));
    assert(!leading.w && !out[0].w && !out[1].w);
    body.w = w; body.h--;
    assert(!tiku_preferences_actions(body, &utility, actions, 2, 12, &leading, out));
    assert(tiku_preferences_actions_width(NULL, NULL, 0, 0) == 0);
    assert(tiku_preferences_actions_width(NULL, actions, -1, 0) == -1);
    assert(tiku_preferences_actions_width(NULL, actions, 9, 0) == -1);
    assert(tiku_preferences_actions_width(NULL, actions, 1, -1) == -1);
    actions[0].minimum = INT_MAX;
    assert(tiku_preferences_actions_width(NULL, actions, 2, 12) == -1);
    body = (tiku_rect_t){11, 23, 100, 100};
    tiku_rect_t inner = tiku_preferences_group_body(body);
    assert(inner.x > body.x && inner.y > body.y);
    assert(inner.x + inner.w < body.x + body.w && inner.y + inner.h < body.y + body.h);
    assert(!tiku_preferences_group_body((tiku_rect_t){0,0,5,5}).w);
    puts("PASS: font-measured action rows, minimum size, alignment and group interiors");
}

static void property_test(void)
{
    tiku_properties_t p;
    tiku_dialog_t d;
    tiku_properties_init(&p);
    assert(p.selected == -1);
    assert(tiku_properties_add(&p, "Processor", "", TIKU_PROPERTY_HEADING) == 0);
    assert(tiku_properties_add(&p, "CPU clock", "128 MHz", 0) == 1);
    assert(tiku_properties_add(&p, "Radio transmit power", "Unavailable",
                                TIKU_PROPERTY_UNAVAILABLE) == 2);
    assert(tiku_properties_add(&p, "A very long device setting label", "value", 0) == 3);
    int h = tiku_properties_row_height(&p);
    tiku_rect_t body = {10, 20, 210, 3 * h + 8};
    assert(tiku_properties_visible(&p, body) == 3);
    assert(tiku_properties_at(&p, body, body.x+5, body.y+5) == -1);
    assert(tiku_properties_at(&p, body, body.x+5, body.y+5+h) == 1);
    assert(tiku_properties_at(&p, body, body.x+1, body.y+5+h) == -1);
    assert(!tiku_properties_row(&p, body, 3).w);
    assert(!tiku_properties_select(&p, body, 0));
    assert(tiku_properties_select(&p, body, 3) && p.top == 1);
    assert(tiku_properties_row(&p, body, 3).h == h);
    assert(tiku_properties_label_width(&p, body) <= (body.w - 8) / 2);
    tiku_dialog_init(&d);
    tiku_dialog_begin(&d, -1, -1);
    assert(!tiku_properties_controls(&p, body, &d, 100, 1));
    tiku_dialog_end(&d);
    assert(d.count == 3 && d.focus == 101);
    tiku_event_t ev = {.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_TAB};
    tiku_dialog_event(&d, &ev);
    assert(d.focus == 102); /* Unavailable rows can explain why. */
    assert(tiku_properties_controls(&p, body, &d, 100, 1) == -1 && d.count == 3);
    assert(tiku_properties_controls(&p, body, &d, INT_MAX, 1) == -1);
    body.h = h + 7;
    assert(!tiku_properties_visible(&p, body));
    assert(!tiku_properties_row(&p, body, p.top).h);
    assert(tiku_properties_at(&p, body, body.x+5, body.y+5) == -1);
    for (int i = p.count; i < TIKU_PROPERTIES_MAX; i++)
        assert(tiku_properties_add(&p, "label", NULL, 0) == i);
    assert(tiku_properties_add(&p, "overflow", "value", 0) == -1);
    puts("PASS: property rows, headings, unavailable values, focus, scrolling and limits");
}

static void paint(tiku_surface_t *s)
{
    tiku_properties_t p;
    tiku_properties_init(&p);
    tiku_properties_add(&p, "Device settings", "", TIKU_PROPERTY_HEADING);
    tiku_properties_add(&p, "Device name", "Laboratory controller caf\xc3\xa9", 0);
    tiku_properties_add(&p, "CPU clock", "128 MHz now / 64 MHz saved", 0);
    tiku_properties_add(&p, "Radio transmit power", "Unavailable", TIKU_PROPERTY_UNAVAILABLE);
    p.selected = 2;
    tiku_properties_draw(&p, s, (tiku_rect_t){10,10,340,110}, 2);
    tiku_text_block_draw(s, (tiku_rect_t){15,130,230,32}, TIKU_MEASURE_PLAIN,
        "This long explanatory sentence should wrap and mark omitted text clearly.",
        TIKU_C_TEXT, 2);
}

static void paint_test(void)
{
    for (int dark = 0; dark < 2; dark++) {
        tiku_theme_set(dark ? tiku_theme_dark() : NULL);
        tiku_surface_t *a = tiku_surface_new(360,190,TIKU_C_PANEL);
        tiku_surface_t *b = tiku_surface_new(360,190,TIKU_C_PANEL);
        tiku_dl_t *dl = tiku_dl_new();
        a->record = dl;
        paint(a);
        a->record = NULL;
        unsigned char wire[16384]; size_t wrote;
        assert(!tiku_dl_misses(dl));
        assert(tiku_dl_flatten(dl, wire, sizeof wire, &wrote));
        tiku_dl_t *back = tiku_dl_unflatten(wire, wrote, NULL);
        assert(back);
        tiku_dl_play(back, b);
        assert(!memcmp(a->px, b->px, 360 * 190 * sizeof *a->px));
        assert(!tiku_surface_png(a, dark ? "/tmp/tiku-properties-dark.png" :
                                           "/tmp/tiku-properties-light.png"));
        /* A label must preserve its caller's clip and not touch outside it. */
        tiku_rect_t clip = {30,30,20,20};
        tiku_clip_set(a, clip);
        memcpy(b->px, a->px, 360 * 190 * sizeof *a->px);
        tiku_text_line_draw(a, (tiku_rect_t){0,0,200,60}, TIKU_MEASURE_PLAIN,
                            "Clipped text", TIKU_C_DANGER);
        assert(!memcmp(&a->clip, &clip, sizeof clip));
        for (int y = 0; y < 190; y++) for (int x = 0; x < 360; x++)
            if (x < 30 || x >= 50 || y < 30 || y >= 50)
                assert(a->px[y * 360 + x] == b->px[y * 360 + x]);
        tiku_dl_free(back); tiku_dl_free(dl);
        tiku_surface_free(a); tiku_surface_free(b);
    }
    tiku_theme_set(NULL);
    puts("PASS: light/dark rendering, clip preservation and byte-identical display-list replay");
}
int main(void)
{
    for (int size = 10; size <= 16; size += 2) {
        tiku_font_set_size(size);
        text_test(); layout_test(); property_test();
    }
    tiku_font_set_size(12);
    paint_test();
    return 0;
}
