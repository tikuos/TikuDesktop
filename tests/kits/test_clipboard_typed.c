/*
 * TikuDesktop clipboard tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_clipboard_typed.c - atomic representation pairs and request replies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_clip_rpc.h"
#include "tiku_clipboard.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned attempts, fail_at;

/** @brief Refuse one store allocation while retaining the old pair. */
static void *
allocate(size_t bytes)
{
    return ++attempts == fail_at ? NULL : malloc(bytes);
}

#define malloc allocate
#include <tiku_clipboard.c>
#undef malloc

/** @brief Require both representations and exactly one publication stamp. */
static void
expect(unsigned version)
{
    const char *mime;
    size_t bytes;
    const void *data = tiku_clipboard_data(&mime, &bytes);

    assert(tiku_clipboard_stamp() == version);
    assert(!strcmp(tiku_clipboard_text(), "fallback"));
    assert(mime && !strcmp(mime, "application/x-test"));
    assert(bytes == 4 && !memcmp(data, "\0\1\xff\2", 4));
}

/** @brief Serialize and reconstruct a message before dispatching it. */
static tiku_msg_t *
wire(tiku_msg_t *message)
{
    tiku_msg_t *copy = tiku_msg_copy(message);

    assert(copy);
    tiku_msg_free(message);
    return copy;
}

/** @brief Check multi-format publication, old readers and atomic refusals. */
static void
multiple(void)
{
    tiku_clipboard_format_t formats[2] = {
        {"application/x-test", "native", 6}, {"text/html", "<b>text</b>", 11}};
    tiku_msg_t *request, *reply;
    unsigned stamp;
    size_t bytes;
    int32_t maximum;

    request = tiku_clip_request(1, TIKU_CLIP_PROBE, NULL, NULL, NULL, 0);
    reply = tiku_clip_reply(request);
    assert(reply && tiku_msg_find_int32(reply, "formats", 0, &maximum) && maximum >= 2);
    tiku_msg_free(request); tiku_msg_free(reply);
    assert(tiku_clipboard_import_formats("text", 4, formats, 2));
    stamp = tiku_clipboard_stamp();
    assert(tiku_clipboard_find("text/html", &bytes) && bytes == 11);
    assert(!memcmp(tiku_clipboard_find("text/html", NULL), formats[1].data, 11));
    for (unsigned i = 1; i <= 3; i++) {
        attempts = 0; fail_at = i;
        assert(!tiku_clipboard_import_formats("new", 3, formats, 2));
        assert(tiku_clipboard_stamp() == stamp && !strcmp(tiku_clipboard_text(), "text"));
        assert(tiku_clipboard_find("text/html", &bytes) && bytes == 11);
    }
    fail_at = 0;
    formats[0].data = tiku_clipboard_data(&formats[0].mime, &formats[0].bytes);
    formats[1].data = tiku_clipboard_find("text/html", &formats[1].bytes);
    request = wire(tiku_clip_request_formats(8, tiku_clipboard_text(), formats, 2));
    assert(tiku_clipboard_import_formats(tiku_clipboard_text(),
        tiku_clipboard_length(), formats, 2));
    reply = wire(tiku_clip_reply(request));
    assert(tiku_clipboard_find("text/html", &bytes) && bytes == 11);
    tiku_msg_free(request); tiku_msg_free(reply);
    request = tiku_clip_request(9, TIKU_CLIP_GET, NULL, NULL, NULL, 0);
    reply = tiku_clip_reply(request);
    assert(reply && tiku_msg_count(reply, "mime") == 1);
    assert(!strcmp(tiku_msg_find_string(reply, "mime", 0), "application/x-test"));
    tiku_msg_free(request); tiku_msg_free(reply);
    stamp = tiku_clipboard_stamp();
    formats[0] = (tiku_clipboard_format_t){"text/html", "x", 1};
    formats[1] = formats[0];
    request = tiku_clip_request_formats(10, "", formats, 2);
    assert(request && !tiku_clip_reply(request));
    assert(tiku_clipboard_stamp() == stamp);
    tiku_msg_free(request);
    assert(!tiku_clipboard_import_formats("", 0, formats, 5));
    assert(!tiku_clipboard_import_formats("", 0, NULL, 1));
    formats[0].bytes = TIKU_CLIPBOARD_LIMIT;
    formats[1].mime = "a/b";
    assert(!tiku_clipboard_import_formats("", 0, formats, 2));
    formats[0] = (tiku_clipboard_format_t){"text/plain; charset=UTF-8", "x", 1};
    assert(!tiku_clipboard_import_formats("", 0, formats, 1));
    assert(tiku_clipboard_stamp() == stamp);
    assert(!tiku_clipboard_find("missing/type", &bytes) && !bytes);
    assert(tiku_clipboard_import_typed("text", 4, "text/plain", "x", 1));
    tiku_clipboard_set("plain");
    assert(!tiku_clipboard_find("text/html", &bytes) && !bytes);
    puts("multi clipboard: three allocation refusals, aliases, v1/v2 RPC and limits PASS");
}

/** @brief Exercise failure atomicity, aliasing, clearing and typed RPC. */
int
main(void)
{
    const char *mime;
    const void *data;
    size_t bytes;
    unsigned version;
    tiku_msg_t *request, *reply;
    int32_t accepted;
    int64_t id;

    assert(tiku_clipboard_import_typed("fallback", 8, "application/x-test",
        "\0\1\xff\2", 4));
    version = tiku_clipboard_stamp();
    expect(version);
    for (unsigned i = 1; i <= 2; i++) {
        attempts = 0; fail_at = i;
        assert(!tiku_clipboard_import_typed("replacement", 11,
            "application/x-test", "new", 3));
        expect(version);
    }
    fail_at = 0;
    assert(!tiku_clipboard_import_typed("bad\xff", 4, "a/b", "x", 1));
    assert(!tiku_clipboard_import_typed("a\0b", 3, "a/b", "x", 1));
    assert(!tiku_clipboard_import_typed("x", 1, "a/b", "x", SIZE_MAX));
    assert(!tiku_clipboard_import_typed("x", 1, "a/b", NULL, 1));
    assert(!tiku_clipboard_import_typed("x", 1, "a/b", "", 0));
    assert(!tiku_clipboard_import_typed("x", 1, NULL, "x", 1));
    {
        const char *bad[] = {"", "/x", "x/", "a/b/c", "a /b", "a/b;z", "a/\xff"};

        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
            assert(!tiku_clipboard_import_typed("x", 1, bad[i], "x", 1));
    }
    expect(version);
    data = tiku_clipboard_data(&mime, &bytes);
    assert(tiku_clipboard_import_typed(tiku_clipboard_text(),
        tiku_clipboard_length(), mime, data, bytes));
    expect(++version);
    request = wire(tiku_clip_request(7, TIKU_CLIP_GET, NULL, NULL, NULL, 0));
    reply = wire(tiku_clip_reply(request));
    assert(tiku_msg_find_int64(reply, "request", 0, &id) && id == 7);
    assert(tiku_msg_find_int32(reply, "accepted", 0, &accepted) && accepted);
    assert(!strcmp(tiku_msg_find_string(reply, "text", 0), "fallback"));
    data = tiku_msg_find_blob(reply, "data", 0, &bytes);
    assert(bytes == 4 && !memcmp(data, "\0\1\xff\2", 4));
    tiku_msg_free(request);
    tiku_msg_free(reply);
    expect(version);
    request = wire(tiku_clip_request(8, TIKU_CLIP_SET, "next", "a/b", "raw", 3));
    attempts = 0; fail_at = 2;
    assert(!tiku_clip_reply(request));
    expect(version);
    fail_at = 0;
    reply = tiku_clip_reply(request);
    assert(reply && tiku_msg_find_int64(reply, "request", 0, &id) && id == 8);
    assert(tiku_clipboard_stamp() == version + 1);
    assert(!strcmp(tiku_clipboard_text(), "next"));
    tiku_msg_free(reply);
    assert(tiku_msg_add_string(request, "text", "duplicate"));
    assert(!tiku_clip_reply(request));
    assert(!strcmp(tiku_clipboard_text(), "next"));
    tiku_msg_free(request);
    assert(tiku_clipboard_import("plain", 5));
    assert(!tiku_clipboard_data(&mime, &bytes) && !mime && !bytes);
    assert(tiku_clipboard_import_typed("", 0, "a/b", "x", 1));
    tiku_clipboard_set("same plain");
    assert(!tiku_clipboard_data(NULL, NULL));
    tiku_clipboard_set(NULL);
    multiple();
    tiku_clipboard_set(NULL);
    puts("typed clipboard: atomic pairs, allocation refusal, aliasing, RPC PASS");
    return 0;
}
