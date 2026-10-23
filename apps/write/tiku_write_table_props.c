/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_table_props.c - table size, border and column-width controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Resolve the caret's immediate table through a public snapshot. */
static tiku_rich_status_t
current(write_app_t *app, tiku_rich_block_t *table, unsigned *column)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_pos_t first, last;
    tiku_rich_status_t status;

    status = tiku_richsession_range(tiku_richview_session(app->view),
                                     &first, &last);
    if (status != TIKU_RICH_OK) return status;
    if (first.paragraph != last.paragraph || first.byte != last.byte)
        return TIKU_RICH_INVALID_RANGE;
    status = tiku_richdoc_snapshot(app->doc, &snapshot);
    if (status != TIKU_RICH_OK) return status;
    status = tiku_richsnapshot_table(snapshot, first.paragraph, table, column);
    tiku_richsnapshot_release(snapshot);
    return status;
}

/** @brief Parse a complete finite decimal within an inclusive range. */
static int
number(const char *text, double low, double high, float *out)
{
    char *end;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno || end == text || *end || !isfinite(value) ||
        value < low || value > high) return 0;
    *out = (float)value;
    return 1;
}

/** @brief Populate values and capture the property panel's document target. */
static void
open_panel(write_app_t *app, int action, const tiku_rich_block_t *node,
           unsigned column)
{
    char first[64], second[64] = "";
    const tiku_rich_table_t *table = &node->table;

    app->table_id = node->id;
    app->table_column = column;
    app->table_generation = tiku_richdoc_generation(app->doc);
    if (action == W_TABLE_LAYOUT) {
        app->panel = W_PANEL_TABLE_LAYOUT;
        snprintf(first, sizeof first, "%.9g", table->width);
        snprintf(second, sizeof second, "%.9g", table->padding);
        snprintf(app->status, sizeof app->status,
            "Points: width 0 fits the available space; padding is 0-72.");
    } else if (action == W_TABLE_BORDER) {
        app->panel = W_PANEL_TABLE_BORDER;
        snprintf(first, sizeof first, "%.9g", table->border);
        snprintf(second, sizeof second, "#%08X", table->color);
        snprintf(app->status, sizeof app->status,
            "Border 0 hides the grid. Colour: #RRGGBB or #AARRGGBB.");
    } else if (action == W_TABLE_HEADERS) {
        app->panel = W_PANEL_TABLE_HEADERS;
        snprintf(first, sizeof first, "%u", table->header_rows);
        snprintf(app->status, sizeof app->status,
            "Repeat leading rows on continued pages: 0-%u (0 disables).",
            table->rows);
    } else {
        app->panel = W_PANEL_TABLE_COLUMN;
        snprintf(first, sizeof first, "%.9g",
            100 * tiku_rich_table_fraction(table, column));
        snprintf(app->status, sizeof app->status,
            "Column %u of %u: percentage of table width. "
            "Other columns keep their relative proportions.",
            column + 1, table->columns);
    }
    tiku_field_start(&app->fields[0], first);
    tiku_field_start(&app->fields[1], second);
    tiku_field_input_init(&app->field_input[0]);
    tiku_field_input_init(&app->field_input[1]);
    write_search_cancel(app);
    write_controls(app);
    tiku_dialog_focus(&app->dialog, W_FIELD);
}

/** @brief Change one share, preserving the relative widths of other columns. */
static int
column_width(tiku_rich_table_t *table, unsigned column, const char *text)
{
    float percent;
    double other = 0, weights[64];

    if (!number(text, table->columns == 1 ? 100 : 1,
                table->columns == 1 ? 100 : 99, &percent)) return 0;
    if (fabs(percent - 100 * tiku_rich_table_fraction(table, column)) <
        0.00001) return 1;
    for (unsigned i = 0; i < table->columns; i++) {
        weights[i] = table->weights[0] ? table->weights[i] : 1;
        if (i != column) other += weights[i];
    }
    for (unsigned i = 0; i < table->columns; i++)
        table->weights[i] = i == column ? percent :
            (float)(weights[i] * (100 - percent) / other);
    return tiku_rich_table_validate(table) == TIKU_RICH_OK;
}

/** @brief Validate both fields before constructing a property transaction. */
static int
values(write_app_t *app, tiku_rich_table_t *table, unsigned column)
{
    if (app->panel == W_PANEL_TABLE_HEADERS) {
        char *end;
        unsigned long rows;

        if (!isdigit((unsigned char)app->fields[0].text[0])) return 0;
        errno = 0;
        rows = strtoul(app->fields[0].text, &end, 10);
        if (errno || *end || rows > table->rows) return 0;
        table->header_rows = (unsigned)rows;
        return 1;
    }
    if (app->panel == W_PANEL_TABLE_LAYOUT)
        return number(app->fields[0].text, 0, 14400, &table->width) &&
            (!table->width || table->width >= 1) &&
            number(app->fields[1].text, 0, 72, &table->padding);
    if (app->panel == W_PANEL_TABLE_COLUMN)
        return column_width(table, column, app->fields[0].text);
    return number(app->fields[0].text, 0, 12, &table->border) &&
        write_argb(app->fields[1].text, &table->color);
}

void
write_table_properties(write_app_t *app, int action)
{
    tiku_rich_block_t node;
    unsigned column;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_TABLE_SET};
    tiku_rich_status_t status = current(app, &node, &column);

    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    if (action != W_APPLY && action != W_TABLE_EQUAL) {
        open_panel(app, action, &node, column);
        return;
    }
    if (action == W_APPLY && (node.id != app->table_id ||
        column != app->table_column ||
        tiku_richdoc_generation(app->doc) != app->table_generation)) {
        snprintf(app->status, sizeof app->status,
            "Table or selection changed. Reopen its settings.");
        return;
    }
    command.value.table = node.table;
    if (action == W_TABLE_EQUAL)
        memset(command.value.table.weights, 0, sizeof node.table.weights);
    else if (!values(app, &command.value.table, column)) {
        snprintf(app->status, sizeof app->status, "%s",
            app->panel == W_PANEL_TABLE_LAYOUT ?
            "Width: 0 or 1-14400 pt. Padding: 0-72 pt." :
            app->panel == W_PANEL_TABLE_HEADERS ?
            "Enter a whole number of leading rows, no larger than the table." :
            app->panel == W_PANEL_TABLE_BORDER ?
            "Border: 0-12 pt. Colour: #RRGGBB or #AARRGGBB." :
            "Width: 1-99% (100% for one column). Keep each column visible.");
        return;
    }
    status = tiku_richsession_execute(tiku_richview_session(app->view),
                                       0, &command);
    if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE) {
        app->panel = 0;
        app->status[0] = 0;
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_BODY);
        app->reveal = 1;
    } else write_refused(app, status);
}
