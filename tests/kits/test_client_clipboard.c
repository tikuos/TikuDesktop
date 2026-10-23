/*
 * TikuDesktop application-kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_client_clipboard.c - exact clipboard publication before destructive cut.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_msg.h"
#include "tiku_remote.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int fail_alloc, fail_field, fail_send, sends;
static char sent_text[128];

/** @brief Inject failure in the client's owned clipboard copy. */
static void *
clipboard_malloc(size_t bytes)
{
    return fail_alloc ? NULL : malloc(bytes);
}

/** @brief Inject failure while constructing the clipboard message. */
static int
clipboard_field(tiku_msg_t *message, const char *name, const char *value)
{
    return fail_field ? 0 : tiku_msg_add_string(message, name, value);
}

/** @brief Accept exact outgoing text or simulate a disconnected desktop. */
static int
clipboard_send(tiku_remote_client_t *client, const tiku_msg_t *message)
{
    const char *text = NULL;

    (void)client;
    sends++;
    if (fail_send) return 0;
    text = tiku_msg_find_string(message, "text", 0);
    assert(text && strlen(text) < sizeof sent_text);
    strcpy(sent_text, text);
    return 1;
}

#define malloc clipboard_malloc
#define tiku_msg_add_string clipboard_field
#define tiku_remote_say clipboard_send
#include <tiku_client.c>
#undef malloc
#undef tiku_msg_add_string
#undef tiku_remote_say

/** @brief Reject incomplete publication without replacing the previous mirror. */
int
main(void)
{
    client_ctx_t client = {0};
    char *large = malloc(TIKU_CLIPBOARD_LIMIT + 2u);

    assert(large && svc_clip_set(&client, "original"));
    assert(!strcmp(client.clip, "original") && client.clip_stamp == 1);
    fail_alloc = 1;
    assert(!svc_clip_set(&client, "lost allocation"));
    fail_alloc = 0;
    assert(sends == 1 && !strcmp(client.clip, "original"));
    fail_field = 1;
    assert(!svc_clip_set(&client, "lost message field"));
    fail_field = 0;
    assert(sends == 1 && !strcmp(client.clip, "original"));
    fail_send = 1;
    assert(!svc_clip_set(&client, "lost transport"));
    fail_send = 0;
    assert(sends == 2 && !strcmp(client.clip, "original"));
    assert(client.clip_stamp == 1);
    memset(large, 'x', TIKU_CLIPBOARD_LIMIT + 1u);
    large[TIKU_CLIPBOARD_LIMIT + 1u] = 0;
    assert(!svc_clip_set(&client, large));
    assert(sends == 2 && !strcmp(client.clip, "original"));
    assert(svc_clip_set(&client, client.clip));
    assert(!strcmp(sent_text, "original"));
    assert(svc_clip_set(&client, NULL));
    assert(!*client.clip && !*sent_text && client.clip_stamp == 3);
    free(client.clip);
    free(large);
    puts("PASS clipboard: refused allocation, message, send and oversize "
         "preserve exact previous text");
    return 0;
}
