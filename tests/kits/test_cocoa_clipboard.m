/*
 * TikuDesktop native clipboard tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_cocoa_clipboard.m - text/HTML interchange on an isolated pasteboard.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku_cocoa.m>
#include <assert.h>
#include <stdio.h>

/** @brief Check Cocoa helpers without opening or modifying the general pasteboard. */
int main(void)
{
    @autoreleasepool {
        NSPasteboard *board = [NSPasteboard pasteboardWithUniqueName];
        NSPasteboardItem *item;
        NSData *html;
        const void *bytes;
        size_t length;
        NSInteger count;

        if (!board) { puts("SKIP: no native pasteboard server"); return 0; }
        assert(tiku_clipboard_import_typed("Bold", 4, "text/html", "<b>Bold</b>", 11));
        assert(clipboard_write(board));
        assert([[board stringForType:NSPasteboardTypeString] isEqualToString:@"Bold"]);
        html = [board dataForType:NSPasteboardTypeHTML];
        assert([html length] == 11 && !memcmp([html bytes], "<b>Bold</b>", 11));
        tiku_clipboard_set("local");
        assert(clipboard_read(board, [board changeCount]) == TIKU_CLIPBOARD_OK);
        bytes = tiku_clipboard_find("text/html", &length);
        assert(bytes && length == 11 && !memcmp(bytes, "<b>Bold</b>", 11));
        count = [board changeCount];
        [board clearContents];
        assert(clipboard_read(board, count) == TIKU_CLIPBOARD_UNAVAILABLE);
        assert(!strcmp(tiku_clipboard_text(), "Bold"));
        item = [[NSPasteboardItem alloc] init];
        [item setData:[NSData dataWithBytes:"<i>Only</i>" length:11]
              forType:NSPasteboardTypeHTML];
        assert([board writeObjects:@[item]]); [item release];
        assert(clipboard_read(board, [board changeCount]) == TIKU_CLIPBOARD_OK);
        assert(!tiku_clipboard_length() && tiku_clipboard_find("text/html", NULL));
        [board clearContents];
        item = [[NSPasteboardItem alloc] init];
        [item setString:[NSString stringWithCharacters:(unichar[]){'a', 0, 'b'} length:3]
                forType:NSPasteboardTypeString];
        assert([board writeObjects:@[item]]); [item release];
        assert(clipboard_read(board, [board changeCount]) == TIKU_CLIPBOARD_INVALID);
        {
            tiku_host_t host = {0};

            tiku_clipboard_set("older selection");
            host.change_count = [board changeCount] - 1;
            host.stamp = tiku_clipboard_stamp();
            assert(!clipboard_refresh_from(&host, board, 0));
            assert(!strcmp(tiku_clipboard_text(), "older selection"));
            assert(host.change_count != [board changeCount]);
            assert(clipboard_refresh_from(&host, board, 1));
            assert(!tiku_clipboard_length());
            assert(host.queue[0].key == TIKU_CLIPBOARD_INVALID);
        }
        [board clearContents];
        assert(clipboard_read(board, [board changeCount]) == TIKU_CLIPBOARD_OK);
        assert(!tiku_clipboard_length() && !tiku_clipboard_data(NULL, NULL));
        [board releaseGlobally];
        tiku_clipboard_set(NULL);
    }
    puts("PASS: isolated Cocoa text/HTML, HTML-only, stale changes and embedded-NUL refusal");
    return 0;
}
