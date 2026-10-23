/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_links.c - incremental navigation to link text without activation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <string.h>

void
write_links_clear(write_app_t *app)
{
    if (app->linkscan) app->status[0] = 0;
    tiku_richlinkscan_release(app->linkscan);
    app->linkscan = NULL;
    memset(&app->link_match, 0, sizeof app->link_match);
}

/** @brief Compare ordered text endpoints without depending on struct padding. */
static int
same(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    return a.body == b.body && a.paragraph == b.paragraph &&
           a.byte == b.byte && a.affinity == b.affinity;
}

void
write_links_action(write_app_t *app, int action)
{
    tiku_rich_pos_t first, last;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;
    int forward = action == W_LINK_NEXT;

    if (app->preview || app->prompt || app->qcount ||
        tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK) return;
    status = tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    if (app->link_match.generation == tiku_richdoc_generation(app->doc) &&
        same(first, app->link_first) && same(last, app->link_last)) {
        first = app->link_match.first;
        last = app->link_match.last;
    }
    tiku_richlinkscan_release(app->linkscan);
    app->linkscan = NULL;
    app->link_forward = forward;
    app->link_generation = tiku_richdoc_generation(app->doc);
    status = tiku_richdoc_snapshot(app->doc, &snapshot);
    if (status == TIKU_RICH_OK)
        status = tiku_richlinkscan_create(snapshot, forward ? last : first,
                                           forward, &app->linkscan);
    tiku_richsnapshot_release(snapshot);
    write_refused(app, status);
    if (status == TIKU_RICH_OK)
        snprintf(app->status, sizeof app->status, "Finding %s hyperlink...",
                   forward ? "next" : "previous");
    app->repaint = 1;
}

/** @brief Copy a bounded target label without splitting a UTF-8 scalar. */
static void
target_label(const char *target, char out[232])
{
    size_t length = strlen(target), bytes = length > 228 ? 228 : length;

    while (bytes && ((unsigned char)target[bytes] & 0xc0) == 0x80) bytes--;
    memcpy(out, target, bytes);
    out[bytes] = 0;
    if (bytes < length) strcat(out, "...");
}

/** @brief Select complete graphemes and preserve raw endpoints for repeated navigation. */
static void
select_match(write_app_t *app, tiku_rich_link_match_t match)
{
    const tiku_richlayout_t *layout = tiku_richview_layout(app->view);
    tiku_rich_pos_t first, last;
    tiku_rich_status_t status;
    char target[232];

    target_label(match.target, target);
    match.target = NULL;
    status = tiku_richlayout_snap(layout, match.first, 0, &first);
    if (status == TIKU_RICH_OK)
        status = tiku_richlayout_snap(layout, match.last, 1, &last);
    write_links_clear(app);
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_select(tiku_richview_session(app->view),
                                           match.generation, first, last);
    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    app->link_match = match;
    app->link_first = first;
    app->link_last = last;
    write_search_cancel(app);
    tiku_dialog_focus(&app->dialog, W_BODY);
    app->reveal = app->repaint = 1;
    snprintf(app->status, sizeof app->status, "Link: %s", target);
}

void
write_links_step(write_app_t *app)
{
    tiku_rich_link_match_t match;
    tiku_rich_status_t status;

    if (!app->linkscan) return;
    if (app->preview || app->prompt || app->link_generation !=
        tiku_richdoc_generation(app->doc)) {
        write_links_clear(app);
        return;
    }
    if (app->qcount) return;
    if (!tiku_richview_layout(app->view)) {
        status = tiku_richview_status(app->view);
        if (status != TIKU_RICH_PENDING) {
            write_links_clear(app);
            write_refused(app, status);
        }
        return;
    }
    status = tiku_richlinkscan_step(app->linkscan, 512);
    if (status == TIKU_RICH_PENDING) return;
    if (status == TIKU_RICH_OK) {
        tiku_richlinkscan_result(app->linkscan, &match);
        select_match(app, match);
    } else {
        tiku_richlinkscan_release(app->linkscan);
        app->linkscan = NULL;
        snprintf(app->status, sizeof app->status, "No %s hyperlink.",
            app->link_forward ? "next" : "previous");
    }
    app->repaint = 1;
}
