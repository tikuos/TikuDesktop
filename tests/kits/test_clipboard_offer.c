/*
 * TikuDesktop native clipboard tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_clipboard_offer.c - atomic X11 offer snapshots on allocation refusal.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned attempts, fail_at;

/** @brief Refuse an exact offer allocation without touching the display. */
static void *allocate(size_t bytes)
{
    return ++attempts == fail_at ? NULL : malloc(bytes);
}

#define malloc allocate
#include <tiku_x11_clipboard.c>
#undef malloc

/** @brief Prepare, refuse and replace offers without opening an X connection. */
int main(void)
{
    tiku_x11_clipboard_t c = {0};
    char *text, *html;

    assert(tiku_clipboard_import_typed("old", 3, "text/html", "<b>old</b>", 10));
    assert(prepare_offer(&c));
    text = c.outgoing; html = c.out_html;
    assert(tiku_clipboard_import_typed("new", 3, "text/html", "<b>new</b>", 10));
    for (fail_at = 1; fail_at <= 2; fail_at++) {
        attempts = 0;
        assert(!prepare_offer(&c));
        assert(c.outgoing == text && c.out_html == html);
        assert(c.outlen == 3 && !memcmp(c.outgoing, "old", 3));
        assert(c.out_html_len == 10 && !memcmp(c.out_html, "<b>old</b>", 10));
    }
    fail_at = 0;
    assert(prepare_offer(&c));
    assert(!strcmp(c.outgoing, "new") && !memcmp(c.out_html, "<b>new</b>", 10));
    tiku_clipboard_set("text");
    assert(prepare_offer(&c) && !c.out_html && !c.out_html_len);
    tiku_x11_clipboard_close(&c);
    tiku_clipboard_set(NULL);
    puts("PASS: two native-offer allocation refusals preserve complete published data");
    return 0;
}
