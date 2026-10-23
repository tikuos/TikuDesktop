/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_search.c - bounded search and generation-checked replacement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Obtain the first or last position in the retained body. */
static tiku_rich_pos_t
edge(const tiku_richsnapshot_t *snapshot, int end)
{
    tiku_rich_paragraph_t p;
    size_t index = end ? tiku_richsnapshot_paragraphs(snapshot) - 1 : 0;

    tiku_richsnapshot_paragraph(snapshot, index, &p);
    return (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), p.id,
        end ? p.length : 0, TIKU_RICH_AFTER};
}

void
write_search_cancel(write_app_t *app)
{
    tiku_richsearch_release(app->search);
    tiku_richsnapshot_release(app->search_source);
    app->search = NULL;
    app->search_source = NULL;
    app->match_count = 0;
}

void
write_search_start(write_app_t *app, int all)
{
    tiku_rich_pos_t first, last, anchor;
    tiku_rich_status_t status;

    write_search_cancel(app);
    app->match.generation = 0;
    if (app->panel == W_PANEL_FIND) {
        strcpy(app->find_text, app->fields[0].text);
        strcpy(app->replace_text, app->fields[1].text);
    }
    if (!*app->find_text) {
        snprintf(app->status, sizeof app->status, "Enter text to find.");
        return;
    }
    status = tiku_richdoc_snapshot(app->doc, &app->search_source);
    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    tiku_richsession_range(tiku_richview_session(app->view), &anchor, &first);
    if (all) first = edge(app->search_source, 0);
    last = edge(app->search_source, 1);
    app->search_origin = first;
    app->search_wrapped = 0;
    app->search_replace_all = all;
    app->search_generation = tiku_richdoc_generation(app->doc);
    status = tiku_richsearch_create_options(app->search_source, first, last,
        app->find_text, strlen(app->find_text),
        app->whole_words ? TIKU_RICH_SEARCH_WHOLE_WORD : 0, &app->search);
    if (status != TIKU_RICH_OK) {
        write_search_cancel(app);
        write_refused(app, status);
    } else snprintf(app->status, sizeof app->status, "Searching...");
}

/** @brief Commit reversed, nonoverlapping in-paragraph edits in one group. */
static void
replace_all(write_app_t *app)
{
    tiku_rich_edit_t *edits;
    tiku_rich_transaction_t tx = {0};
    tiku_rich_status_t status;
    size_t count = app->match_count;
    uint64_t before = tiku_richdoc_generation(app->doc);

    if (!count) {
        write_search_cancel(app);
        snprintf(app->status, sizeof app->status, "No matches.");
        return;
    }
    edits = calloc(count, sizeof *edits);
    if (!edits) {
        write_search_cancel(app);
        write_refused(app, TIKU_RICH_NO_MEMORY);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        const tiku_rich_match_t *m = &app->matches[count - i - 1];

        edits[i] = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = m->first.paragraph,
            .start = m->first.byte, .end = m->last.byte,
            .text = app->replace_text,
            .length = strlen(app->replace_text)};
    }
    tx = (tiku_rich_transaction_t){app->search_generation, edits, count};
    status = tiku_richdoc_apply(app->doc, &tx);
    free(edits);
    write_search_cancel(app);
    write_changed(app, before);
    if (status == TIKU_RICH_OK)
        snprintf(app->status, sizeof app->status,
                 "Replaced %zu matches.", count);
    else write_refused(app, status);
}

void
write_search_step(write_app_t *app)
{
    tiku_rich_match_t match;
    tiku_rich_status_t status;

    if (!app->search) return;
    if (app->search_generation != tiku_richdoc_generation(app->doc)) {
        write_search_cancel(app);
        snprintf(app->status, sizeof app->status,
                 "Search cancelled: the document changed.");
        return;
    }
    status = tiku_richsearch_next(app->search, 8192, &match);
    if (status == TIKU_RICH_PENDING) return;
    app->repaint = 1;
    if (status == TIKU_RICH_OK && app->search_replace_all) {
        if (app->match_count == 1024 ||
            match.first.paragraph != match.last.paragraph) {
            write_search_cancel(app);
            snprintf(app->status, sizeof app->status,
                "Replace All refused: maximum 1024 single-paragraph matches.");
        } else app->matches[app->match_count++] = match;
        return;
    }
    if (status == TIKU_RICH_OK) {
        tiku_richsession_select(tiku_richview_session(app->view),
                               match.generation, match.first, match.last);
        app->match = match;
        app->reveal = 1;
        snprintf(app->status, sizeof app->status, "%s",
                 app->search_wrapped ? "Found (wrapped)." : "Found.");
        write_search_cancel(app);
    } else if (status == TIKU_RICH_NOT_FOUND && app->search_replace_all) {
        replace_all(app);
    } else if (status == TIKU_RICH_NOT_FOUND && !app->search_wrapped) {
        tiku_richsearch_release(app->search);
        app->search = NULL;
        app->search_wrapped = 1;
        status = tiku_richsearch_create_options(app->search_source,
            edge(app->search_source, 0), app->search_origin,
            app->find_text, strlen(app->find_text),
            app->whole_words ? TIKU_RICH_SEARCH_WHOLE_WORD : 0, &app->search);
        if (status != TIKU_RICH_OK) {
            write_search_cancel(app);
            write_refused(app, status);
        }
    } else {
        write_search_cancel(app);
        snprintf(app->status, sizeof app->status, "No matches.");
    }
}

void
write_replace(write_app_t *app)
{
    tiku_rich_pos_t first, last;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_REPLACE_TEXT};
    tiku_rich_status_t status;
    uint64_t before = tiku_richdoc_generation(app->doc);

    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    if (app->match.generation != before ||
        first.paragraph != app->match.first.paragraph ||
        last.paragraph != app->match.last.paragraph ||
        first.byte != app->match.first.byte ||
        last.byte != app->match.last.byte) {
        write_search_start(app, 0);
        return;
    }
    command.value.text.data = app->replace_text;
    command.value.text.length = strlen(app->replace_text);
    status = tiku_richsession_execute(tiku_richview_session(app->view),
                                       before, &command);
    write_changed(app, before);
    if (status == TIKU_RICH_OK) write_search_start(app, 0);
    else write_refused(app, status);
}
