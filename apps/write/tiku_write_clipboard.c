/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_clipboard.c - rich clipboard publication and guarded replies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_clip_rpc.h"
#include "tiku_clipboard.h"
#include "tiku_richclip.h"
#include <stdlib.h>
#include <string.h>

/** @brief Display a transfer refusal without changing document contents. */
static void
failure(write_app_t *app, const char *text)
{
    app->clip_action = 0;
    snprintf(app->status, sizeof app->status, "%s", text);
    app->repaint = 1;
}

void
write_clip_probe(write_app_t *app)
{
    tiku_msg_t *request;

    app->clip_request = 1;
    request = tiku_clip_request(1, TIKU_CLIP_PROBE, NULL, NULL, NULL, 0);
    if (request && app->services->send_message)
        app->services->send_message(app->services->ctx, request);
    tiku_msg_free(request);
}

/** @brief Substitute picture markers with readable plain-text placeholders. */
static char *
fallback(tiku_richsession_t *session)
{
    char *text, *out, *at;
    size_t length = 0, pictures = 0;

    if (tiku_richsession_text(session, NULL, 0, &length) != TIKU_RICH_OK ||
        !length || length > TIKU_CLIPBOARD_LIMIT) return NULL;
    text = malloc(length + 1);
    if (!text) return NULL;
    if (tiku_richsession_text(session, text, length + 1, &length) !=
        TIKU_RICH_OK) { free(text); return NULL; }
    for (at = text; (at = strstr(at, "\xef\xbf\xbc")); at += 3) pictures++;
    if (pictures > (TIKU_CLIPBOARD_LIMIT - length) / 6) {
        free(text);
        return NULL;
    }
    out = malloc(length + pictures * 6 + 1);
    if (out) {
        char *dest = out;

        for (at = text; *at;) {
            if (!strncmp(at, "\xef\xbf\xbc", 3)) {
                memcpy(dest, "[Picture]", 9);
                dest += 9;
                at += 3;
            } else *dest++ = *at++;
        }
        *dest = 0;
    }
    free(text);
    return out;
}

/** @brief Capture a selection and prepare its rich/plain publication pair. */
static tiku_msg_t *
text_publication(write_app_t *app)
{
    tiku_richsession_t *session = tiku_richview_session(app->view);
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richfragment_t *fragment = NULL;
    tiku_rich_pos_t first, last;
    unsigned char *data = NULL;
    size_t bytes = 0;
    char *text = fallback(session);
    tiku_msg_t *request = NULL;

    if (!text) return NULL;
    if (tiku_richsession_range(session, &first, &last) == TIKU_RICH_OK &&
        tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK &&
        tiku_richfragment_create(snapshot, first, last, &fragment) ==
        TIKU_RICH_OK && tiku_richclip_write(fragment, NULL, &data, &bytes) ==
        TIKU_RICHIO_OK)
        request = write_clip_html_publish(app, text, TIKU_RICHCLIP_MIME, data, bytes);
    free(text);
    free(data);
    tiku_richfragment_release(fragment);
    tiku_richsnapshot_release(snapshot);
    return request;
}

/** @brief Serialize selected cells with a quoted, readable TSV fallback. */
static tiku_msg_t *
cell_publication(write_app_t *app)
{
    tiku_richsnapshot_t *snapshot = NULL;
    unsigned char *data = NULL;
    char *text = NULL;
    size_t length = 0, bytes = 0;
    tiku_msg_t *request = NULL;

    if (tiku_richdoc_snapshot(app->doc, &snapshot) != TIKU_RICH_OK)
        return NULL;
    if (tiku_richsnapshot_cells_fallback(snapshot, &app->clip_cells,
        NULL, 0, &length) != TIKU_RICH_OK || length > TIKU_CLIPBOARD_LIMIT)
        goto done;
    text = malloc(length + 1);
    if (!text || tiku_richsnapshot_cells_fallback(snapshot, &app->clip_cells,
        text, length + 1, &length) != TIKU_RICH_OK) goto done;
    if (tiku_richcellclip_write(snapshot, &app->clip_cells, NULL,
        &data, &bytes) == TIKU_RICHIO_OK)
        request = write_clip_html_publish(app, text, TIKU_RICHCELLCLIP_MIME, data, bytes);
done:
    free(text);
    free(data);
    tiku_richsnapshot_release(snapshot);
    return request;
}

void
write_clip_action(write_app_t *app, int action)
{
    tiku_richsession_t *session = tiku_richview_session(app->view);
    tiku_richcmd_state_t state = {.status = TIKU_RICH_INVALID};
    tiku_msg_t *request;
    size_t length = 0;
    int paste = action == W_PASTE || action == W_PASTE_TEXT;

    if (app->clip_action) {
        snprintf(app->status, sizeof app->status, "Clipboard request pending.");
        return;
    }
    (void)tiku_richview_cells(app->view, &app->clip_cells);
    if (action != W_COPY &&
        (tiku_richview_query(app->view, action == W_CUT ?
            TIKU_RICHCMD_DELETE : app->clip_cells.table ?
            TIKU_RICHCMD_REPLACE_TEXT : TIKU_RICHCMD_PASTE_FRAGMENT, &state) !=
            TIKU_RICH_OK || !state.enabled)) {
        write_refused(app, state.status);
        return;
    }
    if (!paste && !app->clip_cells.table &&
        (tiku_richsession_text(session, NULL, 0, &length) != TIKU_RICH_OK ||
         !length)) return;
    if (app->clip_request == INT64_MAX) {
        failure(app, "Clipboard request limit reached. Reopen TikuWrite.");
        return;
    }
    app->clip_request++;
    app->clip_generation = tiku_richdoc_generation(app->doc);
    tiku_richsession_selection(session, &app->clip_anchor, &app->clip_caret);
    request = paste ? tiku_clip_request(app->clip_request,
        TIKU_CLIP_GET, NULL, NULL, NULL, 0) : app->clip_cells.table ?
        cell_publication(app) : text_publication(app);
    if (!request) {
        failure(app, "Cannot prepare this selection for the clipboard; "
            "it may exceed the transfer limit or contain "
            "unsupported structure.");
        return;
    }
    app->clip_action = action;
    app->clip_deadline = app->now > INT64_MAX - 5000000 ? INT64_MAX :
        app->now + 5000000;
    snprintf(app->status, sizeof app->status, "Waiting for clipboard...");
    if (!app->services->send_message ||
        !app->services->send_message(app->services->ctx, request))
        failure(app, "Clipboard request could not be sent. Nothing removed.");
    tiku_msg_free(request);
    app->repaint = 1;
}

/** @brief Compare position values without reading structure padding. */
static int
same(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    return a.body == b.body && a.paragraph == b.paragraph &&
        a.byte == b.byte && a.affinity == b.affinity;
}

/** @brief Refuse application to changed content, selection or modal state. */
static int
current(write_app_t *app)
{
    tiku_rich_pos_t anchor, caret;
    tiku_rich_cells_t cells;

    (void)tiku_richview_cells(app->view, &cells);
    return !app->closing && !app->prompt && !app->panel && !app->preview &&
        !app->qcount &&
        app->clip_generation == tiku_richdoc_generation(app->doc) &&
        cells.table == app->clip_cells.table &&
        (!cells.table || (cells.row == app->clip_cells.row &&
            cells.column == app->clip_cells.column &&
            cells.rows == app->clip_cells.rows &&
            cells.columns == app->clip_cells.columns)) &&
        tiku_richsession_selection(tiku_richview_session(app->view),
            &anchor, &caret) == TIKU_RICH_OK &&
        same(anchor, app->clip_anchor) && same(caret, app->clip_caret);
}

/** @brief Decode cells for rectangle replacement or text-caret insertion. */
static tiku_rich_status_t
paste_cells(write_app_t *app, const void *data, size_t bytes)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;

    if (tiku_richcellclip_read(data, bytes, NULL, &doc, &cells) !=
        TIKU_RICHIO_OK)
        return TIKU_RICH_INVALID;
    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status == TIKU_RICH_OK)
        status = app->clip_cells.table ?
            tiku_richview_cells_paste(app->view, snapshot, &cells) :
            tiku_richview_cells_insert(app->view, snapshot, &cells);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    return status;
}

/** @brief Decode a rich reply or use its explicit plain-text representation. */
static tiku_rich_status_t
paste_reply(write_app_t *app, const tiku_msg_t *message, int plain)
{
    const char *mime = tiku_msg_find_string(message, "mime", 0);
    const char *text = tiku_msg_find_string(message, "text", 0);
    tiku_richfragment_t *fragment = NULL;
    tiku_richcmd_t command = {0};
    tiku_rich_status_t status;
    size_t bytes = 0;
    const void *data = tiku_msg_find_blob(message, "data", 0, &bytes);

    if (!text) return TIKU_RICH_INVALID;
    if (plain) mime = NULL;
    if (mime && !strcmp(mime, "text/html"))
        return write_clip_html_paste(app, data, bytes);
    if (mime && !strcmp(mime, TIKU_RICHCELLCLIP_MIME))
        return paste_cells(app, data, bytes);
    if (app->clip_cells.table) {
        if (mime && !strcmp(mime, TIKU_RICHCLIP_MIME))
            return TIKU_RICH_UNSUPPORTED;
        return !*text ? TIKU_RICH_NO_CHANGE :
            tiku_richview_cells_paste_text(app->view, text, strlen(text));
    }
    if (mime && !strcmp(mime, TIKU_RICHCLIP_MIME)) {
        if (tiku_richclip_read(data, bytes, NULL, &fragment) != TIKU_RICHIO_OK)
            return TIKU_RICH_INVALID;
        command.kind = TIKU_RICHCMD_PASTE_FRAGMENT;
        command.value.fragment = fragment;
    } else {
        if (!*text) return TIKU_RICH_NO_CHANGE;
        command.kind = TIKU_RICHCMD_REPLACE_TEXT;
        command.value.text.data = text;
        command.value.text.length = strlen(text);
    }
    status = tiku_richsession_execute(tiku_richview_session(app->view),
        app->clip_generation, &command);
    tiku_richfragment_release(fragment);
    return status;
}

void
write_clip_message(void *state, const struct tiku_msg *message)
{
    write_app_t *app = state;
    int64_t request;
    int32_t version, accepted, operation;
    int action;
    tiku_rich_status_t status = TIKU_RICH_OK;
    uint64_t before = tiku_richdoc_generation(app->doc);

    if (tiku_msg_what(message) != TIKU_MSG_CLIP_REPLY ||
        !tiku_msg_find_int64(message, "request", 0, &request) ||
        !tiku_msg_find_int32(message, "version", 0, &version) ||
        (version != 1 && version != 2) ||
        !tiku_msg_find_int32(message, "operation", 0, &operation) ||
        !tiku_msg_find_int32(message, "accepted", 0, &accepted) ||
        accepted != 1)
        return;
    if (request == 1 && operation == TIKU_CLIP_PROBE && version == 1) {
        int32_t formats = 0;

        tiku_msg_find_int32(message, "formats", 0, &formats);
        app->clip_formats = formats >= 2 ? 2 : 1;
        app->clip_supported = 1;
        return;
    }
    action = app->clip_action;
    if ((version == 2 && (operation != TIKU_CLIP_SET || app->clip_formats < 2)) ||
        !action || request != app->clip_request || operation !=
        (action == W_PASTE || action == W_PASTE_TEXT ? TIKU_CLIP_GET : TIKU_CLIP_SET)) return;
    app->clip_action = 0;
    app->clip_notices = 0;
    if (action != W_COPY && !current(app)) {
        failure(app, "Clipboard reply ignored because the document or "
            "selection changed. Nothing removed or pasted.");
        return;
    }
    if (action == W_CUT) {
        tiku_richcmd_t command = {.kind = TIKU_RICHCMD_DELETE};

        status = app->clip_cells.table ? tiku_richview_cells_clear(app->view) :
            tiku_richsession_execute(tiku_richview_session(app->view),
                app->clip_generation, &command);
    } else if (action == W_PASTE || action == W_PASTE_TEXT)
        status = paste_reply(app, message, action == W_PASTE_TEXT);
    write_changed(app, before);
    write_refused(app, status);
    if (status == TIKU_RICH_OK)
        snprintf(app->status, sizeof app->status, "%s",
            action == W_COPY ? "Copied with formatting." :
            action == W_CUT ? "Cut to clipboard." : "Pasted.");
    if (status == TIKU_RICH_OK && app->clip_notices) {
        char report[220];

        tiku_richmarkup_report(app->clip_notices, report, sizeof report);
        snprintf(app->status, sizeof app->status, "Pasted HTML. %s", report);
    }
    app->repaint = 1;
}

void
write_clip_tick(write_app_t *app)
{
    if (app->clip_action && app->now >= app->clip_deadline)
        failure(app, "Clipboard did not confirm the request. Nothing removed.");
}
