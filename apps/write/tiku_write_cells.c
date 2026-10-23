/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_cells.c - shared cell selection and tab-separated clipboard.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
write_cells_allowed(write_app_t *app, int action)
{
    tiku_rich_cells_t cells;

    if (tiku_richview_cells(app->view, &cells) != TIKU_RICH_OK) return 1;
    return (action >= W_NEW && action <= W_RECOVER) ||
        action == W_COPY || action == W_PASTE || action == W_PASTE_TEXT || action == W_ALL ||
        (action == W_CUT && app->clip_supported) ||
        action == W_CANCEL ||
        action == W_FIND || action == W_NEXT || action == W_WORDS ||
        (action >= W_ZOOM_IN && action <= W_ZOOM_RESET) ||
        (action >= W_EXPORT_TEXT && action <= W_EXPORT_PDF) ||
        action == W_EXPORT_HTML ||
        (action >= W_CELL_FILL && action <= W_CELLS_ERASE) ||
        (action == W_APPLY && app->panel == W_PANEL_CELL_FILL) ||
        action == W_FORMAT + TIKU_RICHCMD_UNDO ||
        action == W_FORMAT + TIKU_RICHCMD_REDO;
}

/** @brief Resolve the selected cell or the caret's immediate cell. */
static tiku_rich_status_t
context(write_app_t *app, const tiku_richsnapshot_t *snapshot,
        tiku_rich_cells_t *cells, tiku_rich_block_t *table)
{
    tiku_rich_block_t cell;
    tiku_rich_pos_t first, last;
    unsigned column;
    tiku_rich_status_t status;

    if (tiku_richview_cells(app->view, cells) == TIKU_RICH_OK) {
        for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot); i++) {
            tiku_richsnapshot_block(snapshot, i, table);
            if (table->id != cells->table) continue;
            cells->rows = cells->columns = 1;
            return TIKU_RICH_OK;
        }
        return TIKU_RICH_STALE;
    }
    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    status = tiku_richsnapshot_cell(snapshot, first.paragraph, &cell);
    if (status == TIKU_RICH_OK)
        status = tiku_richsnapshot_cells(snapshot, cell.id, cell.id, cells);
    if (status == TIKU_RICH_OK)
        status = tiku_richsnapshot_table(snapshot, first.paragraph, table,
                                         &column);
    return status;
}

/** @brief Expand the current cell to a row, column or complete table. */
static tiku_rich_status_t
select_cells(write_app_t *app, int action)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_block_t table;
    tiku_rich_cells_t cells;
    tiku_rich_status_t status = tiku_richdoc_snapshot(app->doc, &snapshot);

    if (status != TIKU_RICH_OK) return status;
    status = context(app, snapshot, &cells, &table);
    if (status == TIKU_RICH_OK) {
        if (action == W_SELECT_ROW || action == W_SELECT_TABLE) {
            cells.column = 0;
            cells.columns = table.table.columns;
        }
        if (action == W_SELECT_COLUMN || action == W_SELECT_TABLE) {
            cells.row = 0;
            cells.rows = table.table.rows;
        }
        status = tiku_richview_select_cells(app->view,
            tiku_richsnapshot_generation(snapshot), &cells);
    }
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICH_OK) {
        app->panel = 0;
        write_search_cancel(app);
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_BODY);
        snprintf(app->status, sizeof app->status,
            "%u x %u cells selected. Shift-click extends; "
            "Escape returns to text.",
            cells.rows, cells.columns);
    }
    return status;
}

/** @brief Publish lossless plain-cell text after complete export validation. */
static tiku_rich_status_t
copy_cells(write_app_t *app, const tiku_rich_cells_t *cells)
{
    tiku_richsnapshot_t *snapshot;
    size_t length;
    char *text = NULL;
    tiku_rich_status_t status = tiku_richdoc_snapshot(app->doc, &snapshot);

    if (status != TIKU_RICH_OK) return status;
    status = tiku_richsnapshot_cells_text(snapshot, cells, NULL, 0, &length);
    if (status == TIKU_RICH_OK) {
        text = malloc(length + 1);
        if (!text) status = TIKU_RICH_NO_MEMORY;
        else status = tiku_richsnapshot_cells_text(snapshot, cells,
                                                     text, length + 1, &length);
    }
    if (status == TIKU_RICH_OK && (!app->services->clip_set ||
        !app->services->clip_set(app->services->ctx, text)))
        status = TIKU_RICH_UNSUPPORTED;
    free(text);
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICH_OK)
        snprintf(app->status, sizeof app->status,
            "Copied quoted tab-separated text; formatting is not copied.");
    return status;
}

int
write_cells_action(write_app_t *app, int action)
{
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;

    if (action >= W_SELECT_CELL && action <= W_CELLS_ERASE) {
        if (app->preview) return 1;
        status = action == W_CELLS_ERASE ?
            tiku_richview_cells_clear(app->view) : select_cells(app, action);
        write_refused(app, status);
        return 1;
    }
    if (tiku_richview_cells(app->view, &cells) != TIKU_RICH_OK) return 0;
    if (app->clip_supported &&
        (action == W_COPY || action == W_CUT || action == W_PASTE ||
         action == W_PASTE_TEXT)) {
        write_clip_action(app, action);
        return 1;
    }
    if (action == W_PASTE || action == W_PASTE_TEXT) {
        const char *text = app->services->clip_text ?
            app->services->clip_text(app->services->ctx, NULL) : NULL;

        if (!text || !*text) return 1;
        status = tiku_richview_cells_paste_text(app->view, text, strlen(text));
        if (status == TIKU_RICH_INVALID_RANGE)
            snprintf(app->status, sizeof app->status,
                "Paste needs matching rows/columns, or one starting cell "
                "with enough room. The table is unchanged.");
        else if (status == TIKU_RICH_INVALID_TEXT)
            snprintf(app->status, sizeof app->status,
                "Invalid tab-separated text or quoting. Nothing was pasted.");
        else if (status == TIKU_RICH_UNSUPPORTED)
            snprintf(app->status, sizeof app->status,
                "Paste requires text-only data and no nested target tables.");
        else if (status == TIKU_RICH_LIMIT)
            snprintf(app->status, sizeof app->status,
                "Paste exceeds the document or operation limit. "
                "Try a smaller selection.");
        else if (status == TIKU_RICH_OK)
            snprintf(app->status, sizeof app->status,
                "Pasted cell text. Undo restores the previous contents.");
        else write_refused(app, status);
        return 1;
    }
    if (action == W_COPY) {
        status = copy_cells(app, &cells);
        if (status == TIKU_RICH_UNSUPPORTED)
            snprintf(app->status, sizeof app->status,
                "Cannot copy cells with pictures/nested tables, "
                "or the clipboard refused the transfer.");
        else write_refused(app, status);
        return 1;
    }
    if (write_cells_allowed(app, action)) return 0;
    snprintf(app->status, sizeof app->status,
        "Choose cell shading, Copy, Paste or Clear cell contents. "
        "Press Escape to edit text.");
    return 1;
}
