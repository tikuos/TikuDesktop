/*
 * TikuDesktop applications.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_write_clipboard_html.c - bounded native and external HTML interchange.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_clip_rpc.h"
#include "tiku_richclip.h"
#include <stdlib.h>
#include <string.h>

/** @brief Independent conversion limits for an untrusted clipboard selection. */
static const tiku_richio_options_t options = {
    .xml_bytes = TIKU_CLIPBOARD_LIMIT, .memory_bytes = 64u << 20,
    .document = {.memory_bytes = 64u << 20}
};

/** @brief Read a validated native selection into an immutable export snapshot. */
static tiku_richsnapshot_t *
selection(const char *mime, const void *data, size_t bytes)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richfragment_t *fragment = NULL;
    tiku_rich_cells_t cells;

    if (!strcmp(mime, TIKU_RICHCELLCLIP_MIME)) {
        if (tiku_richcellclip_read(data, bytes, &options, &doc, &cells) == TIKU_RICHIO_OK)
            tiku_richdoc_snapshot(doc, &snapshot);
    } else if (tiku_richclip_read(data, bytes, &options, &fragment) == TIKU_RICHIO_OK)
        tiku_richfragment_snapshot(fragment, &snapshot);
    tiku_richfragment_release(fragment);
    tiku_richdoc_release(doc);
    return snapshot;
}

tiku_msg_t *
write_clip_html_publish(write_app_t *app, const char *text,
    const char *mime, const void *data, size_t bytes)
{
    tiku_richsnapshot_t *snapshot;
    tiku_msg_t *request = NULL;
    unsigned char *html = NULL;
    size_t length = 0;
    tiku_clipboard_format_t formats[2] = {{mime, data, bytes}};

    if (app->clip_formats < 2)
        return tiku_clip_request(app->clip_request, TIKU_CLIP_SET,
                                 text, mime, data, bytes);
    snapshot = selection(mime, data, bytes);
    if (snapshot && tiku_richhtml_write(snapshot, &options, &html, &length, NULL)
        == TIKU_RICHIO_OK) {
        formats[1] = (tiku_clipboard_format_t){"text/html", html, length};
        request = tiku_clip_request_formats(app->clip_request, text, formats, 2);
    }
    free(html);
    tiku_richsnapshot_release(snapshot);
    return request;
}

/** @brief Recognize one top-level table with empty surrounding paragraphs. */
static tiku_rich_status_t
table_range(const tiku_richsnapshot_t *snapshot, tiku_rich_cells_t *cells)
{
    tiku_rich_block_t block;
    tiku_rich_paragraph_t p;
    size_t paragraph = 0;
    uint64_t body = tiku_richsnapshot_body(snapshot);

    memset(cells, 0, sizeof *cells);
    for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot); i++) {
        tiku_richsnapshot_block(snapshot, i, &block);
        if (block.kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            tiku_richsnapshot_paragraph(snapshot, paragraph++, &p);
            if (block.parent == body && p.length) return TIKU_RICH_UNSUPPORTED;
        } else if (block.parent == body) {
            if (block.kind != TIKU_RICH_BLOCK_TABLE || cells->table)
                return TIKU_RICH_UNSUPPORTED;
            *cells = (tiku_rich_cells_t){block.id, 0, 0,
                                       block.table.rows, block.table.columns};
        }
    }
    return cells->table ? TIKU_RICH_OK : TIKU_RICH_UNSUPPORTED;
}

/** @brief Insert a complete flat HTML body through the checked fragment command. */
static tiku_rich_status_t
paste_text(write_app_t *app, tiku_richsnapshot_t *snapshot)
{
    tiku_rich_paragraph_t first, last;
    tiku_richfragment_t *fragment = NULL;
    uint64_t body = tiku_richsnapshot_body(snapshot);
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_PASTE_FRAGMENT};
    tiku_rich_status_t status;

    if (app->clip_cells.table) return TIKU_RICH_UNSUPPORTED;
    tiku_richsnapshot_paragraph(snapshot, 0, &first);
    if (tiku_richsnapshot_paragraphs(snapshot) == 1 && !first.length)
        return TIKU_RICH_NO_CHANGE;
    tiku_richsnapshot_paragraph(snapshot,
        tiku_richsnapshot_paragraphs(snapshot) - 1, &last);
    status = tiku_richfragment_create(snapshot,
        (tiku_rich_pos_t){body, first.id, 0, TIKU_RICH_BEFORE},
        (tiku_rich_pos_t){body, last.id, last.length, TIKU_RICH_AFTER}, &fragment);
    if (status == TIKU_RICH_OK) {
        command.value.fragment = fragment;
        status = tiku_richsession_execute(tiku_richview_session(app->view),
            app->clip_generation, &command);
    }
    tiku_richfragment_release(fragment);
    return status;
}

tiku_rich_status_t
write_clip_html_paste(write_app_t *app, const void *data, size_t bytes)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;
    tiku_richio_status_t decoded;

    if (!data || !bytes || bytes > TIKU_CLIPBOARD_LIMIT) return TIKU_RICH_INVALID;
    decoded = tiku_richmarkup_read_html(data, bytes, &options, &doc, &app->clip_notices);
    if (decoded != TIKU_RICHIO_OK)
        return decoded == TIKU_RICHIO_NO_MEMORY ? TIKU_RICH_NO_MEMORY :
            decoded == TIKU_RICHIO_LIMIT ? TIKU_RICH_LIMIT : TIKU_RICH_INVALID;
    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status == TIKU_RICH_OK) {
        if (tiku_richsnapshot_blocks(snapshot) == tiku_richsnapshot_paragraphs(snapshot))
            status = paste_text(app, snapshot);
        else if ((status = table_range(snapshot, &cells)) == TIKU_RICH_OK)
            status = app->clip_cells.table ?
                tiku_richview_cells_paste(app->view, snapshot, &cells) :
                tiku_richview_cells_insert(app->view, snapshot, &cells);
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    return status;
}
