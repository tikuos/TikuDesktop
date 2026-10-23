/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_format.c - typed character, paragraph and list controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Validate a panel value before changing formatting. */
static int
panel_value(write_app_t *app, tiku_richcmd_t *command)
{
    const char *text = app->fields[0].text;
    char *end;
    double size;
    unsigned long color;

    command->kind = TIKU_RICHCMD_CHARACTER_FORMAT;
    if (app->panel == W_PANEL_FONT) {
        if (strcmp(text, "sans-serif") && strcmp(text, "serif") &&
            strcmp(text, "monospace"))
            return -1;
        command->value.character.values.present = TIKU_RICH_FONT;
        strcpy(command->value.character.values.font, text);
    } else if (app->panel == W_PANEL_SIZE) {
        errno = 0;
        size = strtod(text, &end);
        if (errno || end == text || *end || !isfinite(size) ||
            size < 6 || size > 144) return -1;
        command->value.character.values.present = TIKU_RICH_SIZE;
        command->value.character.values.size = (float)size;
    } else if (app->panel == W_PANEL_COLOR) {
        if (*text != '#' || strlen(text) != 7) return -1;
        for (int i = 1; i < 7; i++)
            if (!strchr("0123456789abcdefABCDEF", text[i])) return -1;
        color = strtoul(text + 1, &end, 16);
        command->value.character.values.present = TIKU_RICH_FOREGROUND;
        command->value.character.values.foreground = 0xff000000u | color;
    } else if (app->panel == W_PANEL_LINK) {
        command->kind = *text ? TIKU_RICHCMD_LINK_SET :
                                TIKU_RICHCMD_LINK_REMOVE;
        command->value.link = text;
    } else return -1;
    return 0;
}

/** @brief Apply paragraph and list controls from uniform selection values. */
static tiku_rich_status_t
paragraph_control(write_app_t *app, int action)
{
    tiku_rich_selection_t selection;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_PARAGRAPH_FORMAT};
    tiku_rich_para_t *props = &command.value.paragraph.values;
    tiku_richsession_t *session = tiku_richview_session(app->view);
    tiku_rich_status_t status = tiku_richsession_format(session, &selection);

    if (status != TIKU_RICH_OK) return status;
    if (action >= W_LIST_DEEPER && action <= W_LIST_RESTART) {
        if ((selection.mixed & TIKU_RICH_MIXED_LIST) ||
            selection.list.kind == TIKU_RICH_LIST_NONE)
            return TIKU_RICH_INVALID;
        command.kind = TIKU_RICHCMD_LIST_SET;
        command.value.list = selection.list;
        command.value.list.restart = 0;
        if (action == W_LIST_RESTART) {
            if (selection.list.kind != TIKU_RICH_LIST_DECIMAL)
                return TIKU_RICH_INVALID;
            command.value.list.restart = 1;
        } else if (action == W_LIST_DEEPER) {
            if (selection.list.level == 8) return TIKU_RICH_NO_CHANGE;
            command.value.list.level++;
        } else {
            if (!selection.list.level) return TIKU_RICH_NO_CHANGE;
            command.value.list.level--;
        }
    } else if (action <= W_FIRST_INDENT) {
        if (selection.mixed_props & TIKU_RICH_INDENT)
            return TIKU_RICH_INVALID;
        *props = selection.props;
        props->present = TIKU_RICH_INDENT;
        if (action == W_FIRST_INDENT) props->first = props->first ? 0 : 18;
        else props->left = fmax(0, fmin(360, props->left +
                                       (action == W_INDENT ? 18 : -18)));
    } else if (action <= W_SPACE_ROOMY) {
        props->present = TIKU_RICH_SPACING;
        props->after = action == W_SPACE_COMPACT ? 0 :
            action == W_SPACE_NORMAL ? 6 : 12;
    } else if (action == W_PAGE_BREAK) {
        props->present = TIKU_RICH_PAGE_BREAK;
        props->break_before = !selection.props.break_before;
    } else {
        if (selection.mixed_props & TIKU_RICH_KEEP)
            return TIKU_RICH_INVALID;
        *props = selection.props;
        props->present = TIKU_RICH_KEEP;
        if (action == W_KEEP_NEXT) props->keep_next = !props->keep_next;
        else props->keep_lines = !props->keep_lines;
    }
    return tiku_richsession_execute(session, 0, &command);
}

void
write_format(write_app_t *app, int action)
{
    tiku_richcmd_t command = {0};
    tiku_rich_status_t status;

    if (app->preview && (action == W_TITLE || action == W_HEADING ||
        action == W_TEXT || (action >= W_HEADING_LEVEL &&
        action < W_HEADING_LEVEL + 6))) return;
    if ((action >= W_STYLE_APPLY && action <= W_STYLE_NONE) ||
        (action >= W_STYLE_ITEM && action < W_STYLE_ITEM + 32) ||
        (action == W_APPLY && app->panel >= W_PANEL_STYLE_APPLY)) {
        write_style_action(app, action);
        return;
    }
    if (action >= W_INDENT && action <= W_LIST_RESTART) {
        write_refused(app, paragraph_control(app, action));
        return;
    }
    if (action > W_FORMAT && action < W_FORMAT + 100)
        command.kind = (tiku_richcmd_kind_t)(action - W_FORMAT);
    else if (action == W_BULLET || action == W_NUMBER || action == W_UNLIST) {
        command.kind = TIKU_RICHCMD_LIST_SET;
        command.value.list.kind = action == W_BULLET ? TIKU_RICH_LIST_BULLET :
            action == W_NUMBER ? TIKU_RICH_LIST_DECIMAL : TIKU_RICH_LIST_NONE;
    } else if (action == W_SUPER || action == W_SUB || action == W_NORMAL) {
        command.kind = TIKU_RICHCMD_CHARACTER_FORMAT;
        command.value.character.values.present = TIKU_RICH_BASELINE;
        command.value.character.values.baseline =
            action == W_SUPER ? 1 : action == W_SUB ? -1 : 0;
    } else if (action == W_HIGHLIGHT) {
        command.kind = TIKU_RICHCMD_CHARACTER_FORMAT;
        command.value.character.values.present = TIKU_RICH_HIGHLIGHT;
        command.value.character.values.highlight = 0xffffeb80;
    } else if (action == W_TITLE || action == W_HEADING || action == W_TEXT ||
               (action >= W_HEADING_LEVEL && action < W_HEADING_LEVEL + 6)) {
        unsigned level = action == W_HEADING ? 1 :
            action >= W_HEADING_LEVEL ? (unsigned)(action - W_HEADING_LEVEL + 1) : 0;

        command.kind = TIKU_RICHCMD_PARAGRAPH_FORMAT;
        command.value.paragraph.chars.present = TIKU_RICH_SIZE | TIKU_RICH_BOLD;
        command.value.paragraph.chars.size =
            action == W_TITLE ? 28 : level ? (float)(19 - level) : 12;
        command.value.paragraph.chars.bold = action != W_TEXT;
        command.value.paragraph.values.present = TIKU_RICH_SPACING |
            TIKU_RICH_KEEP | TIKU_RICH_OUTLINE;
        command.value.paragraph.values.outline = (unsigned char)level;
        command.value.paragraph.values.after = action == W_TEXT ? 6 : 12;
        command.value.paragraph.values.keep_next = action != W_TEXT;
    } else if (action == W_APPLY) {
        if (panel_value(app, &command)) {
            snprintf(app->status, sizeof app->status,
                "%s", app->panel == W_PANEL_FONT ?
                "Choose sans-serif, serif, or monospace." :
                "Enter a size from 6 to 144, a #RRGGBB colour, "
                "or a valid URL.");
            return;
        }
    } else return;
    status = tiku_richsession_execute(tiku_richview_session(app->view),
                                       0, &command);
    write_refused(app, status);
    if (action == W_APPLY && status == TIKU_RICH_OK) {
        app->panel = 0;
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_BODY);
    }
}
