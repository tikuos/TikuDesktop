/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_actions.c - file continuations, clipboard and formatting panels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
write_dirty(const write_app_t *app)
{
    return app->imported || tiku_richdoc_dirty(app->doc);
}

void
write_refused(write_app_t *app, tiku_rich_status_t status)
{
    if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE ||
        status == TIKU_RICH_PENDING) return;
    snprintf(app->status, sizeof app->status, "%s",
        status == TIKU_RICH_NO_MEMORY ? "Not enough memory. Edit refused." :
        status == TIKU_RICH_LIMIT ? "Resource limit reached. Edit refused." :
        status == TIKU_RICH_INVALID_TEXT ? "Invalid text. Edit refused." :
        status == TIKU_RICH_STALE ? "Document changed. Repeat the command." :
        "Operation refused; document retained.");
    app->repaint = 1;
}

void
write_changed(write_app_t *app, uint64_t before)
{
    if (before == tiku_richdoc_generation(app->doc)) return;
    write_search_cancel(app);
    app->match.generation = 0;
    app->edited_at = app->now;
    app->reveal = app->repaint = 1;
    app->status[0] = 0;
}

/** @brief Publish clipboard bytes before performing a destructive cut. */
static void
clipboard(write_app_t *app, int action)
{
    tiku_richsession_t *session = tiku_richview_session(app->view);
    tiku_richcmd_t command = {0};
    tiku_richcmd_state_t state;
    tiku_rich_status_t status = TIKU_RICH_OK;
    const char *paste;
    char *text;
    size_t length = 0;

    if (app->clip_supported) { write_clip_action(app, action); return; }
    if (action == W_PASTE_TEXT) action = W_PASTE;

    if (action == W_CUT) {
        status = tiku_richsession_query(session, TIKU_RICHCMD_DELETE, &state);
        if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
        if (!state.enabled) {
            snprintf(app->status, sizeof app->status, "%s", state.reason);
            app->repaint = 1;
            return;
        }
    }
    if (action == W_PASTE) {
        paste = app->services->clip_text ?
            app->services->clip_text(app->services->ctx, NULL) : NULL;
        if (!paste || !*paste) return;
        command.kind = TIKU_RICHCMD_REPLACE_TEXT;
        command.value.text.data = paste;
        command.value.text.length = strlen(paste);
    } else {
        status = tiku_richsession_text(session, NULL, 0, &length);
        if (status != TIKU_RICH_OK || !length) return;
        text = malloc(length + 1);
        if (!text) { write_refused(app, TIKU_RICH_NO_MEMORY); return; }
        status = tiku_richsession_text(session, text, length + 1, &length);
        if (status == TIKU_RICH_OK && strstr(text, "\xef\xbf\xbc")) {
            free(text);
            snprintf(app->status, sizeof app->status,
                "The system clipboard cannot carry pictures. "
                "Copy or cut a text-only selection.");
            return;
        }
        if (status == TIKU_RICH_OK && (!app->services->clip_set ||
            !app->services->clip_set(app->services->ctx, text)))
            status = TIKU_RICH_UNSUPPORTED;
        free(text);
        if (action == W_CUT && status == TIKU_RICH_OK)
            command.kind = TIKU_RICHCMD_DELETE;
    }
    if (command.kind) status = tiku_richsession_execute(session, 0, &command);
    write_refused(app, status);
}

/** @brief Open a host picker and retain the current generation for Open. */
static void
picker(write_app_t *app, int kind)
{
    char directory[WRITE_PATH];
    const char *start = getenv("HOME");
    const char *slash = strrchr(app->path, '/');

    if (!app->services->pick) return;
    if (slash) {
        size_t length = slash == app->path ? 1 : (size_t)(slash - app->path);

        memcpy(directory, app->path, length);
        directory[length] = 0;
        start = directory;
    }
    app->pick_kind = kind;
    app->pick_generation = tiku_richdoc_generation(app->doc);
    if (kind == W_PICTURE_INSERT)
        tiku_richsession_range(tiku_richview_session(app->view),
                               &app->pick_first, &app->pick_last);
    if (!app->services->pick(app->services->ctx, app->id,
        kind == W_OPEN || kind == W_PICTURE_INSERT ?
            TIKU_APP_PICK_LOCAL_OPEN : TIKU_APP_PICK_SAVE,
        kind == W_OPEN || kind == W_PICTURE_INSERT ? start : NULL,
        kind == W_OPEN || kind == W_PICTURE_INSERT ? NULL :
        kind == W_EXPORT_TEXT ? "document.txt" :
        kind == W_EXPORT_PDF ? "document.pdf" :
        kind == W_EXPORT_MARKDOWN ? "document.md" :
        kind == W_EXPORT_HTML ? "document.html" : "document.tkrd")) {
        app->pick_kind = 0;
        snprintf(app->status, sizeof app->status, "File panel unavailable.");
    }
}

/** @brief Save to the already accepted path or request a new native path. */
static void
save(write_app_t *app)
{
    if (!*app->path || app->imported) picker(app, W_SAVEAS);
    else if (write_job_start(app, W_JOB_SAVE, app->path, &app->version, 0))
        snprintf(app->status, sizeof app->status, "Could not start saving.");
}

/** @brief Apply a confirmed continuation, retaining state until adoption. */
static void
proceed(write_app_t *app, int action)
{
    tiku_richdoc_t *doc = NULL;

    if (action == W_NEW) {
        if (tiku_richdoc_create(NULL, &doc) != TIKU_RICH_OK ||
            write_adopt(app, doc)) {
            tiku_richdoc_release(doc);
            write_refused(app, TIKU_RICH_NO_MEMORY);
        } else {
            app->path[0] = 0;
            app->imported = 0;
            memset(&app->version, 0, sizeof app->version);
            if (*app->recovery) unlink(app->recovery);
        }
    } else if (action == W_OPEN) picker(app, action);
    else if (action == W_RECOVER && *app->offered) {
        if (write_job_start(app, W_JOB_LOAD, app->offered, NULL, 1))
            snprintf(app->status, sizeof app->status, "Recovery open failed.");
    } else if (action == W_CLOSE) {
        app->closing = 1;
        if (app->services->close && app->id)
            app->services->close(app->services->ctx, app->id);
        app->id = 0;
    }
}

/** @brief Resolve questions without accepting any unconfirmed overwrite. */
static int
question(write_app_t *app, int action)
{
    int pending = app->pending, prompt = app->prompt;

    if (!prompt) return 0;
    if (action == W_CANCEL) {
        app->prompt = app->pending = 0;
    } else if (prompt == W_PROMPT_UNSAVED && action == W_CONFIRM) {
        save(app);
    } else if (prompt == W_PROMPT_UNSAVED && action == W_DISCARD) {
        app->prompt = app->pending = 0;
        proceed(app, pending);
    } else if (prompt == W_PROMPT_CONFLICT && action == W_CONFIRM) {
        app->prompt = 0;
        picker(app, W_SAVEAS);
    } else if (prompt == W_PROMPT_OVERWRITE && action == W_CONFIRM) {
        app->prompt = 0;
        if (write_job_start(app,
            app->pick_kind == W_EXPORT_TEXT ? W_JOB_TEXT :
            app->pick_kind == W_EXPORT_HTML ? W_JOB_HTML :
            app->pick_kind == W_EXPORT_MARKDOWN ? W_JOB_MARKDOWN :
            app->pick_kind == W_EXPORT_PDF ? W_JOB_PDF : W_JOB_SAVE,
            app->target, &app->target_version, 0))
            snprintf(app->status, sizeof app->status,
                     "Could not start saving.");
    }
    return 1;
}

/** @brief Set up a compact auxiliary panel with keyboard focus in its field. */
static void
panel(write_app_t *app, int action)
{
    write_search_cancel(app);
    app->panel = action == W_FIND ? W_PANEL_FIND :
        action == W_FONT ? W_PANEL_FONT : action == W_SIZE ? W_PANEL_SIZE :
        action == W_COLOR ? W_PANEL_COLOR :
        action == W_STYLE_APPLY ? W_PANEL_STYLE_APPLY :
        action == W_STYLE_DEFINE ? W_PANEL_STYLE_DEFINE : W_PANEL_LINK;
    if (app->panel != W_PANEL_FIND) {
        tiku_field_start(&app->fields[0],
            action == W_FONT ? "sans-serif" :
            action == W_SIZE ? "12" : action == W_COLOR ? "#202020" :
            action == W_LINK ? "https://" : "");
        tiku_field_input_init(&app->field_input[0]);
    } else {
        tiku_field_start(&app->fields[0], app->find_text);
        tiku_field_start(&app->fields[1], app->replace_text);
        tiku_field_input_init(&app->field_input[0]);
        tiku_field_input_init(&app->field_input[1]);
    }
    write_controls(app);
    tiku_dialog_focus(&app->dialog, W_FIELD);
}

void
write_action(write_app_t *app, int action)
{
    uint64_t before = tiku_richdoc_generation(app->doc);
    tiku_event_t boundary = {.type = TIKU_EVENT_DEACTIVATED};
    int handled;

    if (app->preview && action >= W_FORMAT + TIKU_RICHCMD_ROW_BEFORE &&
        action <= W_FORMAT + TIKU_RICHCMD_COLUMN_REMOVE) return;
    if (app->panel == W_PANEL_FIND) {
        strcpy(app->find_text, app->fields[0].text);
        strcpy(app->replace_text, app->fields[1].text);
    }
    if (app->qcount) {
        snprintf(app->status, sizeof app->status, "Finishing queued input...");
        return;
    }
    app->ruler_drag = 0;
    tiku_richview_event(app->view, &boundary, &handled);
    app->repaint = 1;
    if (app->job && ((action >= W_NEW && action <= W_RECOVER) ||
        action == W_EXPORT_TEXT || action == W_EXPORT_PDF ||
        action == W_EXPORT_HTML || action == W_EXPORT_MARKDOWN ||
        action == W_PICTURE_INSERT)) {
        snprintf(app->status, sizeof app->status,
                 "A file operation is running.");
        return;
    }
    if (app->job && app->prompt) return;
    if (question(app, action)) return;
    if (write_bookmarks_action(app, action)) {
        return;
    }
    if (action == W_LINK_PREV || action == W_LINK_NEXT) {
        write_links_action(app, action);
        return;
    }
    if (action == W_RULER) {
        if (app->preview) return;
        app->show_ruler = !app->show_ruler;
        write_controls(app);
        tiku_dialog_focus(&app->dialog,
            app->show_ruler ? W_RULER_CONTROL : W_BODY);
        snprintf(app->status, sizeof app->status, "%s", app->show_ruler ?
            "Ruler: drag a marker; Up/Down selects it, Left/Right moves it." : "");
        return;
    }
    if (action >= W_OUTLINE && action <= W_OUTLINE_NEXT) {
        write_outline_action(app, action);
        return;
    }
    if (write_cells_action(app, action)) {
        write_changed(app, before);
        return;
    }
    if (action == W_CELL_FILL || action == W_CELL_CLEAR ||
        (action == W_APPLY && app->panel == W_PANEL_CELL_FILL)) {
        if (app->preview) return;
        write_cell_action(app, action);
        write_changed(app, before);
        return;
    }
    if ((action >= W_TABLE_LAYOUT && action <= W_TABLE_EQUAL) ||
        (action == W_APPLY && app->panel >= W_PANEL_TABLE_LAYOUT &&
         app->panel <= W_PANEL_TABLE_HEADERS)) {
        if (app->preview) return;
        write_table_properties(app, action);
        write_changed(app, before);
        return;
    }
    if (action == W_TABLE_INSERT || action == W_TABLE_REMOVE ||
        (action == W_APPLY && app->panel == W_PANEL_TABLE)) {
        if (app->preview) return;
        write_table_action(app, action);
        write_changed(app, before);
        return;
    }
    if (action >= W_PICTURE_INSERT && action <= W_PICTURE_ALT) {
        if (app->preview) return;
        if (action == W_PICTURE_INSERT) picker(app, action);
        else write_picture_action(app, action);
        return;
    }
    if (action == W_APPLY && (app->panel == W_PANEL_PICTURE_SIZE ||
        app->panel == W_PANEL_PICTURE_ALT)) {
        write_picture_action(app, action);
        write_changed(app, before);
        return;
    }
    if (action == W_NEW || action == W_OPEN || action == W_CLOSE ||
        action == W_RECOVER) {
        if (write_dirty(app)) {
            app->pending = action;
            app->prompt = W_PROMPT_UNSAVED;
        } else proceed(app, action);
    } else if (action == W_SAVE) save(app);
    else if (action == W_SAVEAS || action == W_EXPORT_TEXT ||
             action == W_EXPORT_PDF || action == W_EXPORT_HTML ||
             action == W_EXPORT_MARKDOWN)
        picker(app, action);
    else if (action >= W_PAGE_A4 && action <= W_EXPORT_PDF)
        write_page_action(app, action);
    else if (action == W_CANCEL) {
        app->panel = 0;
        app->bookmark_pending = 0;
        write_search_cancel(app);
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_BODY);
    } else if (action == W_FIND || action == W_FONT || action == W_SIZE ||
               action == W_COLOR || action == W_LINK ||
               action == W_STYLE_APPLY || action == W_STYLE_DEFINE)
        panel(app, action);
    else if (action == W_NEXT) write_search_start(app, 0);
    else if (action == W_REPLACE_ALL) write_search_start(app, 1);
    else if (action == W_REPLACE) write_replace(app);
    else if (action == W_WORDS) {
        app->whole_words = !app->whole_words;
        app->match.generation = 0;
        write_search_cancel(app);
    }
    else if (action == W_CUT || action == W_COPY || action == W_PASTE ||
             action == W_PASTE_TEXT)
        clipboard(app, action);
    else if (action == W_ALL) tiku_richview_select_all(app->view);
    else if (action == W_ZOOM_IN || action == W_ZOOM_OUT ||
             action == W_ZOOM_RESET) {
        double zoom = action == W_ZOOM_RESET ? 1.0 :
            app->zoom + (action == W_ZOOM_IN ? 0.25 : -0.25);

        if (zoom >= 0.5 && zoom <= 2.5) app->zoom = zoom;
        write_controls(app);
    } else write_format(app, action);
    write_changed(app, before);
}

void
write_picked(void *state, uint32_t window, const char *path)
{
    write_app_t *app = state;
    const char *ext;
    int kind = app->pick_kind;

    if (window != app->id || !kind || app->job) return;
    if (!path || !*path) {
        app->pick_kind = app->pending = 0;
        app->prompt = 0;
        return;
    }
    app->repaint = 1;
    if (*path != '/' || strlen(path) >= WRITE_PATH) {
        snprintf(app->status, sizeof app->status,
                 "Choose a local path shorter than 1024 bytes.");
        return;
    }
    if (kind == W_PICTURE_INSERT) {
        app->pick_kind = 0;
        if (app->pick_generation != tiku_richdoc_generation(app->doc))
            snprintf(app->status, sizeof app->status,
                     "Picture insertion cancelled: the document changed.");
        else if (write_job_start(app, W_JOB_IMAGE, path, NULL, 0))
            snprintf(app->status, sizeof app->status,
                     "Could not start reading the picture.");
        return;
    }
    if (kind == W_OPEN) {
        app->pick_kind = 0;
        if (app->pick_generation != tiku_richdoc_generation(app->doc))
            snprintf(app->status, sizeof app->status,
                     "Open cancelled: edits were made while choosing a file.");
        else if (write_job_start(app, W_JOB_LOAD, path, NULL, 0))
            snprintf(app->status, sizeof app->status,
                     "Could not start opening.");
        return;
    }
    ext = strrchr(path, '.');
    if (!ext || strcasecmp(ext, kind == W_EXPORT_TEXT ? ".txt" :
                          kind == W_EXPORT_HTML ? ".html" :
                          kind == W_EXPORT_MARKDOWN ? ".md" :
                          kind == W_EXPORT_PDF ? ".pdf" : ".tkrd")) {
        snprintf(app->status, sizeof app->status,
                 "Use the %s extension for this operation.",
                 kind == W_EXPORT_TEXT ? ".txt" :
                 kind == W_EXPORT_HTML ? ".html" :
                 kind == W_EXPORT_MARKDOWN ? ".md" :
                 kind == W_EXPORT_PDF ? ".pdf" : ".tkrd");
        return;
    }
    strcpy(app->target, path);
    if (write_job_start(app, W_JOB_PROBE, path, NULL, 0))
        snprintf(app->status, sizeof app->status, "Could not start saving.");
}
