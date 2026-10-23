/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_pages.c - persistent paper controls and incremental page preview.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>

void
write_pages_step(write_app_t *app)
{
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richlayout_options_t options = {.leading = 3};
    tiku_rich_status_t status;
    uint64_t generation = tiku_richdoc_generation(app->doc);

    if (!app->preview) return;
    if (!app->pages || app->pages_generation != generation) {
        tiku_richpages_release(app->pages);
        app->pages = NULL;
        app->pages_generation = generation;
        app->preview_page = 0;
        options.provider = tiku_richfont_provider(app->fonts);
        status = tiku_richdoc_snapshot(app->doc, &snapshot);
        if (status == TIKU_RICH_OK)
            status = tiku_richpages_create(snapshot, &options, 0, &app->pages);
        tiku_richsnapshot_release(snapshot);
        if (status != TIKU_RICH_OK) {
            app->preview = 0;
            tiku_richsession_set_read_only(tiku_richview_session(app->view), 0);
            write_refused(app, status);
            return;
        }
        app->repaint = 1;
    }
    if (tiku_richpages_status(app->pages) != TIKU_RICH_PENDING) return;
    status = tiku_richpages_step(app->pages, 8192);
    if (status != TIKU_RICH_PENDING) {
        app->repaint = 1;
        if (status != TIKU_RICH_OK)
            snprintf(app->status, sizeof app->status,
                "%s", status == TIKU_RICH_UNSUPPORTED ?
                "Remove page breaks inside table cells to preview this file." :
                "Cannot paginate: check line/picture/header size, width "
                "and page limit.");
    }
}

void
write_page_action(write_app_t *app, int command)
{
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_PAGE_SETUP};
    tiku_rich_transaction_t tx = {0, &edit, 1};
    tiku_richsnapshot_t *snapshot;
    tiku_rich_status_t status;

    if (command == W_PREVIEW) {
        app->preview = !app->preview;
        if (app->preview) app->status[0] = 0;
        tiku_richsession_set_read_only(tiku_richview_session(app->view),
                                       app->preview);
        app->panel = 0;
        write_pages_step(app);
        return;
    }
    if (command == W_PREV_PAGE || command == W_NEXT_PAGE) {
        if (command == W_PREV_PAGE && app->preview_page) app->preview_page--;
        else if (command == W_NEXT_PAGE &&
            app->preview_page + 1 < tiku_richpages_count(app->pages))
            app->preview_page++;
        return;
    }
    status = tiku_richdoc_snapshot(app->doc, &snapshot);
    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    tiku_richsnapshot_page(snapshot, &edit.page);
    tiku_richsnapshot_release(snapshot);
    if (command == W_PAGE_A4) {
        edit.page.width = 595.2756f;
        edit.page.height = 841.8898f;
    } else if (command == W_PAGE_LETTER) {
        edit.page.width = 612;
        edit.page.height = 792;
    } else if (command == W_LANDSCAPE) {
        float width = edit.page.width;

        edit.page.width = edit.page.height;
        edit.page.height = width;
    } else if (command == W_MARGINS) {
        float margin = edit.page.left == 54 ? 36 : 54;

        edit.page.left = edit.page.right = edit.page.top =
            edit.page.bottom = margin;
    } else return;
    write_refused(app, tiku_richdoc_apply(app->doc, &tx));
}
