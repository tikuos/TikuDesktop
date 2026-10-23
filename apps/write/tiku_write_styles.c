/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_styles.c - named paragraph style application and definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <string.h>

/** @brief Find a paragraph style by ID or exact name in one snapshot. */
static tiku_rich_status_t
find_style(write_app_t *app, uint64_t id, const char *name,
           tiku_rich_style_spec_t *spec, uint64_t *found)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_style_t style;
    tiku_rich_status_t status = tiku_richdoc_snapshot(app->doc, &snapshot);

    if (status != TIKU_RICH_OK) return status;
    status = TIKU_RICH_NOT_FOUND;
    for (size_t i = 0; i < tiku_richsnapshot_styles(snapshot); i++) {
        tiku_richsnapshot_style(snapshot, i, &style);
        if (style.kind != TIKU_RICH_PARAGRAPH_STYLE ||
            (id ? style.id != id : !name || strcmp(style.name, name)))
            continue;
        *found = style.id;
        *spec = (tiku_rich_style_spec_t){.kind = style.kind,
            .parent = style.parent, .chars = style.chars,
            .paragraph = style.paragraph};
        strcpy(spec->name, style.name);
        status = TIKU_RICH_OK;
        break;
    }
    tiku_richsnapshot_release(snapshot);
    return status;
}

/** @brief Build a named style from uniformly formatted selected text. */
static tiku_rich_status_t
definition(write_app_t *app, int update, tiku_richcmd_t *command)
{
    tiku_rich_selection_t selection;
    tiku_rich_style_spec_t *spec = &command->value.definition.value;
    tiku_rich_status_t status = tiku_richsession_format(
        tiku_richview_session(app->view), &selection);

    if (status != TIKU_RICH_OK) return status;
    if (selection.mixed_chars || selection.mixed_props)
        return TIKU_RICH_INVALID_STYLE;
    if (update) {
        if ((selection.mixed & TIKU_RICH_MIXED_PARAGRAPH_STYLE) ||
            !selection.paragraph_style) return TIKU_RICH_INVALID_STYLE;
        status = find_style(app, selection.paragraph_style, NULL,
                             spec, &command->value.definition.style);
        if (status != TIKU_RICH_OK) return status;
    } else {
        if (!*app->fields[0].text ||
            strlen(app->fields[0].text) >= sizeof spec->name)
            return TIKU_RICH_INVALID_STYLE;
        strcpy(spec->name, app->fields[0].text);
    }
    spec->kind = TIKU_RICH_PARAGRAPH_STYLE;
    spec->chars = selection.chars;
    spec->paragraph = selection.props;
    command->kind = TIKU_RICHCMD_STYLE_DEFINE;
    return TIKU_RICH_OK;
}

void
write_style_action(write_app_t *app, int action)
{
    tiku_richcmd_t command = {0};
    tiku_rich_style_spec_t spec;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (action == W_STYLE_CLEAR) {
        command.kind = TIKU_RICHCMD_PARAGRAPH_FORMAT;
        command.value.paragraph.clear_props = 63;
        command.value.paragraph.clear_chars = 511;
    } else if (action == W_STYLE_NONE) {
        command.kind = TIKU_RICHCMD_PARAGRAPH_STYLE;
    } else if (action == W_STYLE_UPDATE ||
        (action == W_APPLY && app->panel == W_PANEL_STYLE_DEFINE)) {
        status = definition(app, action == W_STYLE_UPDATE, &command);
    } else if (action >= W_STYLE_ITEM && action < W_STYLE_ITEM + 32) {
        command.kind = TIKU_RICHCMD_PARAGRAPH_STYLE;
        command.value.style = app->menu_styles[action - W_STYLE_ITEM];
        if (!command.value.style) status = TIKU_RICH_STALE;
    } else if (action == W_APPLY && app->panel == W_PANEL_STYLE_APPLY) {
        command.kind = TIKU_RICHCMD_PARAGRAPH_STYLE;
        status = find_style(app, 0, app->fields[0].text, &spec,
                             &command.value.style);
    } else return;
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_execute(tiku_richview_session(app->view),
                                           0, &command);
    if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE) {
        if (action == W_APPLY) {
            app->panel = 0;
            write_controls(app);
            tiku_dialog_focus(&app->dialog, W_BODY);
        }
    } else if (status == TIKU_RICH_NOT_FOUND ||
               status == TIKU_RICH_INVALID_STYLE) {
        snprintf(app->status, sizeof app->status,
            "Use an existing style name, or create a unique name "
            "from uniformly formatted text.");
    } else write_refused(app, status);
}
