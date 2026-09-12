/* Clipboard import boundaries and empty-paste safety.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "tiku_clipboard.h"
#include "tiku_field_event.h"

int main(void)
{
    tiku_clipboard_set("previous");
    unsigned before = tiku_clipboard_stamp();
    assert(!tiku_clipboard_import("bad\xff", 4));
    assert(!tiku_clipboard_import("\xe2\x82", 2));
    assert(!tiku_clipboard_import("\xc0\xaf", 2));
    assert(!tiku_clipboard_import("\xed\xa0\x80", 3));
    assert(!tiku_clipboard_import("\xf4\x90\x80\x80", 4));
    assert(!tiku_clipboard_import("a\0b", 3));
    assert(!strcmp(tiku_clipboard_text(), "previous"));
    assert(tiku_clipboard_stamp() == before);
    assert(tiku_clipboard_import("caf\xc3\xa9\n\xf0\x9f\x8c\x8d", 10));
    assert(!strcmp(tiku_clipboard_text(), "caf\xc3\xa9\n\xf0\x9f\x8c\x8d"));
    char big[TIKU_CLIPBOARD_MAX + 4];
    memset(big, 'a', sizeof big); big[sizeof big - 1] = 0;
    assert(!tiku_clipboard_import(big, TIKU_CLIPBOARD_MAX));
    assert(tiku_clipboard_import(big, TIKU_CLIPBOARD_MAX - 1));
    assert(strlen(tiku_clipboard_text()) == TIKU_CLIPBOARD_MAX - 1);
    big[TIKU_CLIPBOARD_MAX - 2] = '\xc3';
    big[TIKU_CLIPBOARD_MAX - 1] = '\xa9';
    tiku_clipboard_set(big);
    assert(strlen(tiku_clipboard_text()) == TIKU_CLIPBOARD_MAX - 2);
    tiku_clipboard_set(tiku_clipboard_text()); /* Overlapping source is legal. */
    assert(tiku_clipboard_import(NULL, 0));
    tiku_field_t field;
    tiku_field_start(&field, "keep selected text");
    tiku_field_key(&field, TIKU_FIELD_PASTE, 0, 0);
    assert(!strcmp(field.text, "keep selected text") && field.sel_a != field.sel_b);
    tiku_field_input_t input;
    tiku_field_input_init(&input);
    tiku_event_t paste = {.type=TIKU_EVENT_KEY_DOWN, .key='v', .modifiers=TIKU_MOD_CMD};
    tiku_field_event(&field, &input, &paste, (tiku_rect_t){0,0,200,30}, 1);
    assert(!strcmp(field.text, "keep selected text") && field.sel_a != field.sel_b);
    assert(!input.nundo);
    puts("PASS: atomic clipboard imports, UTF-8 bounds and non-destructive empty paste");
    return 0;
}
