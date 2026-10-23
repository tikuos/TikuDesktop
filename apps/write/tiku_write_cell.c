/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_cell.c - per-cell shading and checked colour fields.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
write_argb(const char *text, uint32_t *out)
{
    size_t size = strlen(text);

    if (*text != '#' || (size != 7 && size != 9)) return 0;
    for (size_t i = 1; i < size; i++)
        if (!strchr("0123456789abcdefABCDEF", text[i])) return 0;
    *out = (uint32_t)strtoul(text + 1, NULL, 16);
    if (size == 7) *out |= 0xff000000;
    return 1;
}

/** @brief Resolve the collapsed caret's immediate cell through a snapshot. */
static tiku_rich_status_t
cell(write_app_t *app, tiku_rich_block_t *out)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_pos_t first, last;
    tiku_rich_status_t status;
    tiku_rich_cells_t cells;

    if (tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK) {
        status = tiku_richdoc_snapshot(app->doc, &snapshot);
        if (status != TIKU_RICH_OK) return status;
        status = tiku_richsnapshot_cell_at(snapshot, &cells, 0, out);
        tiku_richsnapshot_release(snapshot);
        return status;
    }

    status = tiku_richsession_range(tiku_richview_session(app->view),
                                     &first, &last);
    if (status != TIKU_RICH_OK) return status;
    if (first.paragraph != last.paragraph || first.byte != last.byte)
        return TIKU_RICH_INVALID_RANGE;
    status = tiku_richdoc_snapshot(app->doc, &snapshot);
    if (status != TIKU_RICH_OK) return status;
    status = tiku_richsnapshot_cell(snapshot, first.paragraph, out);
    tiku_richsnapshot_release(snapshot);
    return status;
}

void
write_cell_action(write_app_t *app, int action)
{
    tiku_rich_block_t node;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_CELL_SET};
    tiku_rich_status_t status = cell(app, &node);
    char color[16];
    tiku_rich_cells_t cells;

    (void)tiku_richview_cells(app->view, &cells);

    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    if (action == W_CELL_FILL) {
        app->panel = W_PANEL_CELL_FILL;
        app->cell_id = node.id;
        app->cell_selection = cells;
        app->table_generation = tiku_richdoc_generation(app->doc);
        if (node.background) snprintf(color, sizeof color,
                                       "#%08X", node.background);
        else strcpy(color, "none");
        tiku_field_start(&app->fields[0], color);
        tiku_field_input_init(&app->field_input[0]);
        write_search_cancel(app);
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_FIELD);
        snprintf(app->status, sizeof app->status,
            "Cell fill: #RRGGBB, #AARRGGBB, or none to remove shading.");
        return;
    }
    if (action == W_APPLY && (app->cell_id != node.id ||
        cells.table != app->cell_selection.table ||
        cells.row != app->cell_selection.row ||
        cells.column != app->cell_selection.column ||
        cells.rows != app->cell_selection.rows ||
        cells.columns != app->cell_selection.columns ||
        app->table_generation != tiku_richdoc_generation(app->doc))) {
        snprintf(app->status, sizeof app->status,
            "Cell or document changed. Reopen Cell shading.");
        return;
    }
    if (action == W_APPLY && strcmp(app->fields[0].text, "none") &&
        !write_argb(app->fields[0].text, &command.value.color)) {
        snprintf(app->status, sizeof app->status,
            "Enter #RRGGBB, #AARRGGBB, or none.");
        return;
    }
    status = tiku_richview_execute(app->view, 0, &command);
    if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE) {
        app->panel = 0;
        app->status[0] = 0;
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_BODY);
    } else write_refused(app, status);
}
