/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_view.c - compact document controls, live menus and frame drawing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_ui.h"
#include "tiku_text_layout_paint.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief Toolbar geometry and labels, also used for keyboard focus. */
typedef struct {
    int command, x, width;
    const char *label;
} tool_t;
static const tool_t tools[] = {
    {W_NEW, 8, 46, "New"}, {W_OPEN, 58, 48, "Open"},
    {W_SAVE, 110, 46, "Save"},
    {W_FORMAT + TIKU_RICHCMD_BOLD, 172, 28, "B"},
    {W_FORMAT + TIKU_RICHCMD_ITALIC, 202, 28, "I"},
    {W_FORMAT + TIKU_RICHCMD_UNDERLINE, 232, 28, "U"},
    {W_FONT, 276, 62, "Font..."},
    {W_SIZE, 342, 58, "Size..."},
    {W_COLOR, 404, 64, "Colour..."},
    {W_FIND, 482, 56, "Find"}, {W_PREVIEW, 542, 76, "Pages"},
    {W_OUTLINE, 622, 78, "Outline"}
};

tiku_rect_t
write_body(const write_app_t *app)
{
    int narrow = app->width < 660;
    int pair = app->panel == W_PANEL_TABLE_LAYOUT ||
               app->panel == W_PANEL_TABLE_BORDER;
    int top = 66 + (app->prompt ? 94 : app->panel ?
        app->panel == W_PANEL_FIND ? narrow ? 136 : 76 :
        pair ? narrow ? 112 : 76 : narrow ? 80 : 48 : 0);
    int sidebar = write_outline_width(app);
    if (app->show_ruler && !app->preview) top += 32;
    int width = app->width - 96 - sidebar, height = app->height - top - 38;

    return (tiku_rect_t){40 + sidebar, top, width > 1 ? width : 1,
                        height > 1 ? height : 1};
}

/** @brief Whether a typed formatting command is currently available. */
static int
available(write_app_t *app, int command, int *checked)
{
    tiku_richcmd_state_t state;

    *checked = 0;
    if (app->prompt || app->qcount) return 0;
    if (command == W_RULER) {
        *checked = app->show_ruler;
        return !app->preview;
    }
    if (command == W_LINK_PREV || command == W_LINK_NEXT ||
        (command >= W_BOOKMARK_ADD && command <= W_LINK_FOLLOW) ||
        (command >= W_BOOKMARK_ITEM && command < W_BOOKMARK_ITEM + 24)) {
        tiku_rich_cells_t cells;

        if ((command == W_BOOKMARK_ADD || command == W_BOOKMARK_REMOVE) &&
            tiku_richsession_read_only(tiku_richview_session(app->view))) return 0;
        return !app->preview && tiku_richview_cells(app->view, &cells) != TIKU_RICH_OK;
    }
    if (command >= W_OUTLINE && command <= W_OUTLINE_NEXT) {
        *checked = command == W_OUTLINE && app->show_outline;
        return !app->preview;
    }
    if (command >= W_HEADING_LEVEL && command < W_HEADING_LEVEL + 6) {
        tiku_rich_selection_t selection;

        if (app->preview || tiku_richsession_read_only(
            tiku_richview_session(app->view))) return 0;
        if (tiku_richsession_format(tiku_richview_session(app->view),
                                    &selection) == TIKU_RICH_OK)
            *checked = !(selection.mixed_props & TIKU_RICH_OUTLINE) &&
                selection.props.outline == command - W_HEADING_LEVEL + 1;
        return write_cells_allowed(app, command);
    }
    if (!write_cells_allowed(app, command)) return 0;
    if (command >= W_SELECT_CELL && command <= W_CELLS_ERASE) {
        tiku_rich_cells_t cells;

        if (app->preview) return 0;
        if (command == W_CELLS_ERASE)
            return !tiku_richsession_read_only(tiku_richview_session(app->view))
                && tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK;
        tiku_richsession_query(tiku_richview_session(app->view),
                               TIKU_RICHCMD_TABLE_SET, &state);
        return state.enabled;
    }
    if (command >= W_TABLE_INSERT && command <= W_CELL_CLEAR) {
        if (app->preview) return 0;
        tiku_richsession_query(tiku_richview_session(app->view),
            command == W_TABLE_INSERT ? TIKU_RICHCMD_TABLE_INSERT :
            command == W_TABLE_REMOVE ? TIKU_RICHCMD_TABLE_REMOVE :
            command >= W_CELL_FILL ? TIKU_RICHCMD_CELL_SET :
            TIKU_RICHCMD_TABLE_SET, &state);
        return state.enabled;
    }
    if (command >= W_PICTURE_INSERT && command <= W_PICTURE_ALT) {
        if (app->preview || app->job) return 0;
        tiku_richsession_query(tiku_richview_session(app->view),
            command == W_PICTURE_INSERT ? TIKU_RICHCMD_IMAGE_INSERT :
            TIKU_RICHCMD_IMAGE_SET, &state);
        return state.enabled;
    }
    if (app->preview && (command == W_CUT || command == W_PASTE || command == W_PASTE_TEXT ||
        command == W_FIND || command == W_REPLACE ||
        command == W_REPLACE_ALL || command == W_FONT ||
        command == W_SIZE || command == W_COLOR)) return 0;
    if (command == W_CUT) {
        tiku_richview_query(app->view, TIKU_RICHCMD_DELETE, &state);
        return state.enabled;
    }
    if (command > W_FORMAT && command < W_FORMAT + 100) {
        if (app->preview && command >= W_FORMAT + TIKU_RICHCMD_ROW_BEFORE)
            return 0;
        tiku_richview_query(app->view,
            (tiku_richcmd_kind_t)(command - W_FORMAT), &state);
        *checked = state.check == TIKU_RICHCMD_CHECK_ON;
        return state.enabled;
    }
    if (command == W_SAVE) return !app->job && write_dirty(app);
    if (command >= W_NEW && command <= W_RECOVER) return !app->job;
    if (command == W_EXPORT_TEXT || command == W_EXPORT_PDF ||
        command == W_EXPORT_HTML || command == W_EXPORT_MARKDOWN) return !app->job;
    if (command >= W_INDENT && command <= W_LIST_RESTART) {
        tiku_rich_selection_t selection;

        if (app->preview) return 0;
        if (tiku_richsession_format(tiku_richview_session(app->view),
                                    &selection) != TIKU_RICH_OK) return 0;
        if (command == W_KEEP_NEXT) *checked = selection.props.keep_next;
        if (command == W_KEEP_LINES) *checked = selection.props.keep_lines;
        if (command == W_PAGE_BREAK) *checked = selection.props.break_before;
        if (command >= W_LIST_DEEPER)
            return !(selection.mixed & TIKU_RICH_MIXED_LIST) &&
                selection.list.kind != TIKU_RICH_LIST_NONE;
    }
    if ((command >= W_STYLE_APPLY && command <= W_STYLE_NONE) ||
        (command >= W_STYLE_ITEM && command < W_STYLE_ITEM + 32) ||
        (command >= W_HEADING_LEVEL && command < W_HEADING_LEVEL + 6))
        return !app->preview;
    return 1;
}

/** @brief List one auxiliary control at its window-local rectangle. */
static void
control(write_app_t *app, int id, int x, int y, int w, int input)
{
    tiku_dialog_add(&app->dialog, id, (tiku_rect_t){x, y, w, 24},
        input ? TIKU_DIALOG_INPUT : TIKU_DIALOG_BUTTON, !app->job || input);
}

void
write_controls(write_app_t *app)
{
    tiku_rect_t body = write_body(app);
    int checked, narrow = app->width < 660;
    int pair = app->panel == W_PANEL_TABLE_LAYOUT ||
               app->panel == W_PANEL_TABLE_BORDER;

    tiku_dialog_begin(&app->dialog,
        app->prompt ? W_CONFIRM : app->panel ?
        app->panel == W_PANEL_FIND ? W_NEXT : W_APPLY : TIKU_DIALOG_NONE,
        app->prompt || app->panel ? W_CANCEL : TIKU_DIALOG_NONE);
    for (size_t i = 0; i < sizeof tools / sizeof tools[0]; i++) {
        if (tools[i].x + tools[i].width <= app->width - 8)
            tiku_dialog_add(&app->dialog, tools[i].command,
                (tiku_rect_t){tools[i].x, 8, tools[i].width, 24},
                TIKU_DIALOG_BUTTON, available(app, tools[i].command, &checked));
    }
    if (app->prompt) {
        control(app, W_CONFIRM, app->width - 282, 106, 84, 0);
        control(app, W_CANCEL, app->width - 190, 106, 84, 0);
        if (app->prompt == W_PROMPT_UNSAVED)
            control(app, W_DISCARD, app->width - 98, 106, 84, 0);
    } else if (app->panel) {
        control(app, W_FIELD, 92, 52, app->width - (narrow ? 108 : 398), 1);
        if (pair)
            control(app, W_FIELD2, 92, 84,
                    app->width - (narrow ? 108 : 398), 1);
        if (app->panel == W_PANEL_FIND) {
            control(app, W_FIELD2, 92, 84,
                    app->width - (narrow ? 108 : 398), 1);
            control(app, W_NEXT, narrow ? 14 : app->width - 290,
                    narrow ? 116 : 52, 86, 0);
            control(app, W_WORDS, narrow ? 106 : app->width - 198,
                    narrow ? 116 : 52, 100, 0);
            control(app, W_REPLACE, narrow ? 14 : app->width - 290,
                    narrow ? 148 : 84, 86, 0);
            control(app, W_REPLACE_ALL, narrow ? 106 : app->width - 198,
                    narrow ? 148 : 84, 100, 0);
        } else control(app, W_APPLY, narrow ? 14 : app->width - 290,
                       narrow ? pair ? 116 : 84 : 52, 86, 0);
        control(app, W_CANCEL, app->width - 90,
                narrow ? app->panel == W_PANEL_FIND || pair ?
                116 : 84 : 52, 76, 0);
    } else if (app->preview) {
        if (app->width >= 766) {
            control(app, W_PREV_PAGE, 630, 8, 62, 0);
            control(app, W_NEXT_PAGE, 696, 8, 62, 0);
        }
    }
    tiku_dialog_add(&app->dialog, W_BODY, body, TIKU_DIALOG_INPUT,
                    !app->prompt && !app->preview);
    if (write_outline_width(app))
        tiku_dialog_add(&app->dialog, W_OUTLINE_LIST,
            (tiku_rect_t){8, body.y - 12, write_outline_width(app), body.h + 24},
            TIKU_DIALOG_INPUT, !app->prompt);
    if (app->show_ruler && !app->preview)
        tiku_dialog_add(&app->dialog, W_RULER_CONTROL,
            write_ruler_bounds(app), TIKU_DIALOG_INPUT, !app->prompt);
    tiku_dialog_end(&app->dialog);
    if (app->view) tiku_richview_viewport(app->view, body.w, body.h, app->zoom);
}

/** @brief Append one enabled, checked menu item within host limits. */
static void
row(write_app_t *app, tiku_menu_list_t *menu, const char *label,
    int command, int key)
{
    int i, checked;

    if (menu->nitem >= TIKU_MENUSET_ITEMS) return;
    i = menu->nitem++;
    snprintf(menu->item[i].label, sizeof menu->item[i].label, "%s", label);
    menu->item[i].command = command;
    menu->item[i].sc = key;
    menu->item[i].mods = key ? TIKU_MOD_CMD : 0;
    menu->item[i].enabled = available(app, command, &checked);
    menu->item[i].marked = checked;
    if (command == W_RECOVER) menu->item[i].enabled &= !!*app->offered;
}

/** @brief Publish bounded named style shortcuts using stable document IDs. */
static void
style_menu(write_app_t *app, tiku_menu_list_t *menu)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_style_t style;
    tiku_rich_selection_t selection;
    size_t count = 0;
    int first = menu->nitem;

    row(app, menu, "Paragraph styles", 0, 0);
    row(app, menu, "Apply paragraph style...", W_STYLE_APPLY, 0);
    row(app, menu, "Create style from selection...", W_STYLE_DEFINE, 0);
    row(app, menu, "Update assigned style from selection", W_STYLE_UPDATE, 0);
    row(app, menu, "Clear paragraph overrides", W_STYLE_CLEAR, 0);
    row(app, menu, "No paragraph style", W_STYLE_NONE, 0);
    for (int i = first + 1; i < menu->nitem; i++) menu->item[i].level = 1;
    memset(app->menu_styles, 0, sizeof app->menu_styles);
    if (tiku_richdoc_snapshot(app->doc, &snapshot) != TIKU_RICH_OK) return;
    memset(&selection, 0, sizeof selection);
    tiku_richsession_format(tiku_richview_session(app->view), &selection);
    for (size_t i = 0; i < tiku_richsnapshot_styles(snapshot) && count < 32 &&
         menu->nitem < TIKU_MENUSET_ITEMS;
         i++) {
        tiku_richsnapshot_style(snapshot, i, &style);
        if (style.kind != TIKU_RICH_PARAGRAPH_STYLE) continue;
        app->menu_styles[count] = style.id;
        row(app, menu, style.name, W_STYLE_ITEM + (int)count++, 0);
        menu->item[menu->nitem - 1].level = 1;
        menu->item[menu->nitem - 1].marked =
            !(selection.mixed & TIKU_RICH_MIXED_PARAGRAPH_STYLE) &&
            style.id == selection.paragraph_style;
    }
    tiku_richsnapshot_release(snapshot);
}

/** @brief Append bookmark commands and as many exact-name shortcuts as fit. */
static void
bookmark_menu(write_app_t *app, tiku_menu_list_t *menu)
{
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_bookmark_t mark;
    int first = menu->nitem;

    row(app, menu, "Bookmarks", 0, 0);
    row(app, menu, "Add or move bookmark...", W_BOOKMARK_ADD, 0);
    row(app, menu, "Go to bookmark...", W_BOOKMARK_GO, 0);
    row(app, menu, "Remove bookmark...", W_BOOKMARK_REMOVE, 0);
    memset(app->menu_bookmarks, 0, sizeof app->menu_bookmarks);
    app->bookmark_menu_generation = tiku_richdoc_generation(app->doc);
    if (tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK) {
        for (size_t i = 0; i < 24 && menu->nitem < TIKU_MENUSET_ITEMS &&
             i < tiku_richsnapshot_bookmarks(snapshot); i++) {
            tiku_richsnapshot_bookmark(snapshot, i, &mark);
            strcpy(app->menu_bookmarks[i], mark.name);
            row(app, menu, mark.name, W_BOOKMARK_ITEM + (int)i, 0);
        }
    }
    tiku_richsnapshot_release(snapshot);
    for (int i = first + 1; i < menu->nitem; i++) menu->item[i].level = 1;
}

/** @brief Publish native menus using the same actions as the toolbar. */
static void
menus(write_app_t *app)
{
    tiku_menuset_t set = {0};
    tiku_menu_list_t *m;

    set.nmenu = 6;
    m = &set.menu[0];
    strcpy(m->title, "File");
    row(app, m, "New", W_NEW, 'n');
    row(app, m, "Open...", W_OPEN, 'o');
    row(app, m, "Save", W_SAVE, 's');
    row(app, m, "Save As...", W_SAVEAS, 0);
    row(app, m, "Export plain text...", W_EXPORT_TEXT, 0);
    row(app, m, "Export PDF...", W_EXPORT_PDF, 0);
    row(app, m, "Export HTML...", W_EXPORT_HTML, 0);
    row(app, m, "Export Markdown...", W_EXPORT_MARKDOWN, 0);
    row(app, m, "Recover last draft", W_RECOVER, 0);
    row(app, m, "Close", W_CLOSE, 'w');
    m = &set.menu[1];
    strcpy(m->title, "Edit");
    row(app, m, "Undo", W_FORMAT + TIKU_RICHCMD_UNDO, 'z');
    row(app, m, "Redo", W_FORMAT + TIKU_RICHCMD_REDO, 'y');
    row(app, m, "Cut", W_CUT, 'x');
    row(app, m, "Copy", W_COPY, 'c');
    row(app, m, "Paste", W_PASTE, 'v');
    row(app, m, "Paste as plain text", W_PASTE_TEXT, 'v');
    m->item[m->nitem - 1].mods |= TIKU_MOD_SHIFT;
    row(app, m, "Select all", W_ALL, 'a');
    row(app, m, "Find and Replace...", W_FIND, 'f');
    row(app, m, "Find next", W_NEXT, 'g');
    m = &set.menu[2];
    strcpy(m->title, "Format");
    row(app, m, "Bold", W_FORMAT + TIKU_RICHCMD_BOLD, 'b');
    row(app, m, "Italic", W_FORMAT + TIKU_RICHCMD_ITALIC, 'i');
    row(app, m, "Underline", W_FORMAT + TIKU_RICHCMD_UNDERLINE, 'u');
    row(app, m, "Strike through", W_FORMAT + TIKU_RICHCMD_STRIKE, 0);
    row(app, m, "Font...", W_FONT, 0);
    row(app, m, "Size...", W_SIZE, 0);
    row(app, m, "Text colour...", W_COLOR, 0);
    row(app, m, "Yellow highlight", W_HIGHLIGHT, 0);
    row(app, m, "Superscript", W_SUPER, 0);
    row(app, m, "Subscript", W_SUB, 0);
    row(app, m, "Normal baseline", W_NORMAL, 0);
    row(app, m, "Link...", W_LINK, 0);
    row(app, m, "Insert picture...", W_PICTURE_INSERT, 0);
    row(app, m, "Picture width...", W_PICTURE_SIZE, 0);
    row(app, m, "Picture alternate text...", W_PICTURE_ALT, 0);
    row(app, m, "Clear direct formatting",
        W_FORMAT + TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES, 0);
    style_menu(app, m);
    m = &set.menu[3];
    strcpy(m->title, "Paragraph");
    row(app, m, "Align left", W_FORMAT + TIKU_RICHCMD_ALIGN_LEFT, 0);
    row(app, m, "Align centre", W_FORMAT + TIKU_RICHCMD_ALIGN_CENTER, 0);
    row(app, m, "Align right", W_FORMAT + TIKU_RICHCMD_ALIGN_RIGHT, 0);
    row(app, m, "Justify", W_FORMAT + TIKU_RICHCMD_ALIGN_JUSTIFY, 0);
    row(app, m, "Bulleted list", W_BULLET, 0);
    row(app, m, "Numbered list", W_NUMBER, 0);
    row(app, m, "Remove list", W_UNLIST, 0);
    row(app, m, "Title formatting", W_TITLE, 0);
    for (int i = 0; i < 6; i++) {
        char name[24];

        snprintf(name, sizeof name, "Heading %d", i + 1);
        row(app, m, name, W_HEADING_LEVEL + i, 0);
    }
    row(app, m, "Body formatting", W_TEXT, 0);
    row(app, m, "Increase left indent", W_INDENT, 0);
    row(app, m, "Decrease left indent", W_OUTDENT, 0);
    row(app, m, "Toggle first-line indent", W_FIRST_INDENT, 0);
    row(app, m, "No paragraph spacing", W_SPACE_COMPACT, 0);
    row(app, m, "Normal paragraph spacing", W_SPACE_NORMAL, 0);
    row(app, m, "Wide paragraph spacing", W_SPACE_ROOMY, 0);
    row(app, m, "Keep with next paragraph", W_KEEP_NEXT, 0);
    row(app, m, "Keep paragraph lines together", W_KEEP_LINES, 0);
    row(app, m, "Start paragraph on a new page", W_PAGE_BREAK, 0);
    row(app, m, "Increase list level", W_LIST_DEEPER, 0);
    row(app, m, "Decrease list level", W_LIST_SHALLOWER, 0);
    row(app, m, "Restart numbering at 1", W_LIST_RESTART, 0);
    m = &set.menu[4];
    strcpy(m->title, "View");
    row(app, m, "Zoom in", W_ZOOM_IN, '+');
    row(app, m, "Zoom out", W_ZOOM_OUT, '-');
    row(app, m, "Actual size", W_ZOOM_RESET, '0');
    row(app, m, "Paragraph ruler", W_RULER, 0);
    row(app, m, "Heading outline", W_OUTLINE, 0);
    row(app, m, "Previous heading", W_OUTLINE_PREV, 0);
    row(app, m, "Next heading", W_OUTLINE_NEXT, 0);
    row(app, m, "Previous hyperlink", W_LINK_PREV, 0);
    row(app, m, "Next hyperlink", W_LINK_NEXT, 0);
    row(app, m, "Follow document link", W_LINK_FOLLOW, 0);
    row(app, m, app->preview ? "Return to editing" : "Page preview",
        W_PREVIEW, 0);
    row(app, m, "Previous page", W_PREV_PAGE, 0);
    row(app, m, "Next page", W_NEXT_PAGE, 0);
    row(app, m, "A4 paper", W_PAGE_A4, 0);
    row(app, m, "Letter paper", W_PAGE_LETTER, 0);
    row(app, m, "Rotate paper", W_LANDSCAPE, 0);
    row(app, m, "Toggle 36 / 54 pt margins", W_MARGINS, 0);
    bookmark_menu(app, m);
    m = &set.menu[5];
    strcpy(m->title, "Table");
    row(app, m, "Insert table...", W_TABLE_INSERT, 0);
    row(app, m, "Insert row above", W_FORMAT + TIKU_RICHCMD_ROW_BEFORE, 0);
    row(app, m, "Insert row below", W_FORMAT + TIKU_RICHCMD_ROW_AFTER, 0);
    row(app, m, "Delete row", W_FORMAT + TIKU_RICHCMD_ROW_REMOVE, 0);
    row(app, m, "Insert column left", W_FORMAT + TIKU_RICHCMD_COLUMN_BEFORE, 0);
    row(app, m, "Insert column right", W_FORMAT + TIKU_RICHCMD_COLUMN_AFTER, 0);
    row(app, m, "Delete column", W_FORMAT + TIKU_RICHCMD_COLUMN_REMOVE, 0);
    row(app, m, "Table size and padding...", W_TABLE_LAYOUT, 0);
    row(app, m, "Table borders...", W_TABLE_BORDER, 0);
    row(app, m, "Column width...", W_TABLE_COLUMN, 0);
    row(app, m, "Equal column widths", W_TABLE_EQUAL, 0);
    row(app, m, "Repeat header rows...", W_TABLE_HEADERS, 0);
    row(app, m, "Cell shading...", W_CELL_FILL, 0);
    row(app, m, "Clear cell shading", W_CELL_CLEAR, 0);
    row(app, m, "Delete table", W_TABLE_REMOVE, 0);
    row(app, m, "Select cell", W_SELECT_CELL, 0);
    row(app, m, "Select row", W_SELECT_ROW, 0);
    row(app, m, "Select column", W_SELECT_COLUMN, 0);
    row(app, m, "Select table", W_SELECT_TABLE, 0);
    row(app, m, "Clear cell contents", W_CELLS_ERASE, 0);
    app->services->menus(app->services->ctx, app->id, &set);
}

/** @brief Draw one clipped interface text line. */
static void
label(write_app_t *app, int x, int y, int width, const char *text)
{
    tiku_text_line_draw(app->surface, (tiku_rect_t){x, y, width, 22},
                       TIKU_MEASURE_PLAIN, text, TIKU_C_TEXT);
}

/** @brief Paint auxiliary controls and an explicit non-destructive question. */
static void
panels(write_app_t *app)
{
    if (app->prompt) {
        const char *question = app->prompt == W_PROMPT_UNSAVED ?
            "Save changes before continuing?" :
            app->prompt == W_PROMPT_OVERWRITE ? "Replace the selected file?" :
            "The file has changed outside TikuWrite.";
        const char *detail = app->prompt == W_PROMPT_UNSAVED ?
            "Save keeps your work. Discard abandons this document." :
            app->prompt == W_PROMPT_OVERWRITE ?
            "Replacement is checked again before the file is changed." :
            "Save As creates another copy; Cancel keeps editing.";
        tiku_ui_sunken(app->surface,
                       (tiku_rect_t){8, 44, app->width - 16, 98}, 0xfff4d6);
        label(app, 22, 52, app->width - 44, question);
        label(app, 22, 76, app->width - 44, detail);
    } else if (app->panel) {
        const char *name = app->panel == W_PANEL_FIND ? "Find:" :
            app->panel >= W_PANEL_BOOKMARK_ADD ? "Bookmark:" :
            app->panel == W_PANEL_CELL_FILL ? "Cell fill:" :
            app->panel == W_PANEL_TABLE_LAYOUT ? "Width pt:" :
            app->panel == W_PANEL_TABLE_BORDER ? "Border pt:" :
            app->panel == W_PANEL_TABLE_COLUMN ? "Width %:" :
            app->panel == W_PANEL_TABLE_HEADERS ? "Headers:" :
            app->panel == W_PANEL_TABLE ? "Rows x cols:" :
            app->panel == W_PANEL_PICTURE_SIZE ? "Width pt:" :
            app->panel == W_PANEL_PICTURE_ALT ? "Alt text:" :
            app->panel == W_PANEL_FONT ? "Family:" :
            app->panel == W_PANEL_SIZE ? "Points:" :
            app->panel == W_PANEL_COLOR ? "Colour:" :
            app->panel >= W_PANEL_STYLE_APPLY ? "Style:" : "URL:";
        label(app, 14, 52, 74, name);
        if (app->panel == W_PANEL_FIND)
            label(app, 14, 84, 74, "Replace:");
        else if (app->panel == W_PANEL_TABLE_LAYOUT)
            label(app, 14, 84, 74, "Padding pt:");
        else if (app->panel == W_PANEL_TABLE_BORDER)
            label(app, 14, 84, 74, "Colour:");
    }
    for (int i = 0; i < app->dialog.count; i++) {
        const tiku_dialog_control_t *c = &app->dialog.control[i];
        const char *text = NULL;
        unsigned flags = (c->enabled ? 0 : TIKU_S_DISABLED) |
            (tiku_dialog_focused(&app->dialog, c->id) ? TIKU_S_FOCUS : 0) |
            (tiku_dialog_pressed(&app->dialog, c->id) ? TIKU_S_PRESSED : 0);
        int checked;

        if (c->id == W_BODY) continue;
        if (c->id == W_FIELD || c->id == W_FIELD2) {
            int k = c->id - W_FIELD;
            tiku_ui_textfield_scroll(app->surface, c->rect,
                app->fields[k].text, app->fields[k].caret,
                app->fields[k].sel_a, app->fields[k].sel_b, flags,
                app->field_input[k].scroll);
            continue;
        }
        for (size_t t = 0; t < sizeof tools / sizeof tools[0]; t++)
            if (tools[t].command == c->id) text = tools[t].label;
        available(app, c->id, &checked);
        if (checked) flags |= TIKU_S_PRESSED;
        if (!text) text = c->id == W_CONFIRM ?
            app->prompt == W_PROMPT_UNSAVED ? "Save" :
            app->prompt == W_PROMPT_OVERWRITE ? "Replace" : "Save As..." :
            c->id == W_DISCARD ? "Discard" : c->id == W_CANCEL ? "Cancel" :
            c->id == W_NEXT ? "Find next" : c->id == W_REPLACE ? "Replace" :
            c->id == W_REPLACE_ALL ? "Replace all" : "Apply";
        if (c->id == W_APPLY && app->panel >= W_PANEL_BOOKMARK_ADD)
            text = app->panel == W_PANEL_BOOKMARK_ADD ? "Save" :
                app->panel == W_PANEL_BOOKMARK_GO ? "Go" : "Remove";
        if (c->id == W_PREV_PAGE) text = "Back";
        if (c->id == W_NEXT_PAGE) text = "Next";
        if (c->id == W_PREVIEW && app->preview) text = "Edit";
        if (c->id == W_WORDS) {
            text = "Whole words";
            if (app->whole_words) flags |= TIKU_S_PRESSED;
        }
        tiku_ui_button(app->surface, c->rect, text, flags);
    }
}

/** @brief Paint a fitted paper page without allowing text edits in preview. */
static void
preview(write_app_t *app, tiku_rect_t body)
{
    tiku_rich_page_setup_t page;
    tiku_rich_status_t status = tiku_richpages_status(app->pages);
    double scale, x, y;

    tiku_fill(app->surface, body, 0xb9b9b9);
    if (status != TIKU_RICH_OK) {
        label(app, body.x + 12, body.y + 12, body.w - 24,
            status == TIKU_RICH_PENDING ? "Paginating..." :
            "Preview unavailable. Return to editing or change page settings.");
        return;
    }
    tiku_richpages_setup(app->pages, &page);
    scale = fmin((body.w - 16) / page.width, (body.h - 16) / page.height);
    if (scale < 0.1) return;
    x = body.x + (body.w - page.width * scale) / 2;
    y = body.y + 8;
    tiku_richoutput_page(app->fonts, app->pages, app->preview_page,
                         app->surface, x, y, scale);
}

void
write_paint(write_app_t *app)
{
    tiku_rect_t body = write_body(app);
    const tiku_richlayout_t *layout;
    tiku_rich_status_t status;
    char title[192], footer[256];
    const char *name = strrchr(app->path, '/');
    double height, visible, scroll;

    if (!app->id || !app->surface) return;
    write_controls(app);
    app->repaint = 0;
    layout = tiku_richview_layout(app->view);
    tiku_clip_reset(app->surface);
    tiku_fill(app->surface, (tiku_rect_t){0, 0, app->width, app->height},
              TIKU_C_PANEL);
    tiku_hline(app->surface, 0, 39, app->width, 0x909090);
    tiku_fill(app->surface, (tiku_rect_t){body.x - 16, body.y - 12,
        body.w + 32, body.h + 24}, TIKU_C_DOC);
    tiku_frame(app->surface, (tiku_rect_t){body.x - 17, body.y - 13,
        body.w + 34, body.h + 26}, 0x969696);
    status = app->preview ? TIKU_RICH_OK :
        tiku_richfont_draw_view(app->fonts, app->view, app->surface,
        body, app->active ? 0x604a82c0 : 0x40666666, TIKU_C_TEXT,
        app->active && !app->prompt && app->dialog.focus == W_BODY);
    if (status != TIKU_RICH_OK) {
        tiku_fill(app->surface, body, TIKU_C_DOC);
        label(app, body.x, body.y, body.w,
            status == TIKU_RICH_PENDING ? "Laying out document..." :
                                         "Cannot display this layout.");
    }
    if (app->preview) preview(app, body);
    height = layout ? tiku_richlayout_height(layout) : body.h / app->zoom;
    visible = body.h / app->zoom;
    scroll = tiku_richview_scroll_y(app->view);
    tiku_ui_scrollbar(app->surface,
        (tiku_rect_t){app->width - 19, body.y - 12, 16, body.h + 24},
        height > visible ? scroll / (height - visible) : 0,
        height > visible ? visible / height : 1, 0);
    panels(app);
    write_outline_paint(app);
    write_ruler_paint(app);
    snprintf(footer, sizeof footer, "%.0f%%   %s%.180s", app->zoom * 100,
        app->job ? app->job->kind == W_JOB_LOAD ? "Opening...  " :
            app->job->kind == W_JOB_RECOVERY ? "Recovery checkpoint...  " :
                                            "Saving...  " : "",
        *app->status ? app->status : write_dirty(app) ? "Modified" : "Saved");
    if (!*app->status && !app->job && layout &&
        (tiku_richlayout_warnings(layout) &
        (TIKU_RICH_LAYOUT_SHAPING | TIKU_RICH_LAYOUT_FALLBACK)))
        snprintf(footer, sizeof footer, "%.0f%%   %s",
                 app->zoom * 100,
                 "Font substitution or limited script shaping");
    if (app->preview && !app->job && !*app->status &&
        tiku_richpages_count(app->pages))
        snprintf(footer, sizeof footer, "Page %zu of %zu%s",
            app->preview_page + 1, tiku_richpages_count(app->pages),
            tiku_richpages_warnings(app->pages) ?
            "   Oversized keep group split between lines" :
            tiku_richlayout_warnings(tiku_richpages_layout(app->pages)) ?
            "   Layout has font, shaping or overflow warnings" : "");
    label(app, 10, app->height - 23, app->width - 20, footer);
    snprintf(title, sizeof title, "%s%s — TikuWrite",
        write_dirty(app) ? "* " : "", name ? name + 1 : "Untitled");
    if (app->services->configure)
        app->services->configure(app->services->ctx, app->id,
                                 title, 0, 0, 1, 1);
    app->services->frame(app->services->ctx, app->id, app->surface->px,
                         app->width, app->height);
    menus(app);
}
