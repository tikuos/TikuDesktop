/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_table.c - rectangular table creation and structural deletion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Parse positive row and column counts separated by x. */
static int
dimensions(const char *text, tiku_rich_table_t *table)
{
    char *end;
    unsigned long rows, columns;

    errno = 0;
    while (isspace((unsigned char)*text)) text++;
    if (!isdigit((unsigned char)*text)) return 0;
    rows = strtoul(text, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (*end != 'x' && *end != 'X') return 0;
    text = end + 1;
    while (isspace((unsigned char)*text)) text++;
    if (!isdigit((unsigned char)*text)) return 0;
    columns = strtoul(text, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (errno || *end || !rows || rows > 256 || !columns || columns > 64)
        return 0;
    *table = (tiku_rich_table_t){.rows = (unsigned)rows,
        .columns = (unsigned)columns, .padding = 4, .border = 1,
        .color = 0xff707070};
    return tiku_rich_table_validate(table) == TIKU_RICH_OK;
}

void
write_table_action(write_app_t *app, int action)
{
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_TABLE_REMOVE};
    tiku_rich_status_t status;

    if (action == W_TABLE_INSERT) {
        app->panel = W_PANEL_TABLE;
        tiku_field_start(&app->fields[0], "2 x 3");
        tiku_field_input_init(&app->field_input[0]);
        write_search_cancel(app);
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_FIELD);
        snprintf(app->status, sizeof app->status,
            "Insert after this paragraph. Enter rows x columns, e.g. 2 x 3.");
        return;
    }
    if (action == W_APPLY) {
        command.kind = TIKU_RICHCMD_TABLE_INSERT;
        if (!dimensions(app->fields[0].text, &command.value.table)) {
            snprintf(app->status, sizeof app->status,
                "Use 1-256 rows x 1-64 columns, at most 4096 cells.");
            return;
        }
    }
    status = tiku_richsession_execute(tiku_richview_session(app->view),
                                       0, &command);
    if (status == TIKU_RICH_OK) {
        app->panel = 0;
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_BODY);
        app->reveal = 1;
    } else write_refused(app, status);
    app->repaint = 1;
}
