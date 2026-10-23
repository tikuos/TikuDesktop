/*
 * TikuDesktop applications.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_write_bookmarks.c - named document points and internal link navigation.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <stdio.h>
#include <string.h>

/** @brief Resolve a selected raw occurrence, or the current selection's link. */
static tiku_rich_status_t
link_target(write_app_t *app, tiku_richsnapshot_t *snapshot, tiku_rich_pos_t *out)
{
    tiku_rich_selection_t selection;
    tiku_rich_status_t status;

    if (app->link_match.generation == tiku_richdoc_generation(app->doc))
        status = tiku_richsnapshot_selection(snapshot, app->link_match.first,
                                               app->link_match.last, &selection);
    else status = tiku_richsession_format(tiku_richview_session(app->view),
                                            &selection);
    if (status != TIKU_RICH_OK) return status;
    if (!selection.link || (selection.mixed & TIKU_RICH_MIXED_LINK))
        return TIKU_RICH_NOT_FOUND;
    return tiku_richsnapshot_resolve_link(snapshot, selection.link, out);
}

/** @brief Queue a checked point without holding snapshot-owned pointers. */
static void
navigate(write_app_t *app, const char *name)
{
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_status_t status = tiku_richdoc_snapshot(app->doc, &snapshot);

    app->bookmark_pending = 0;
    if (status == TIKU_RICH_OK)
        status = name ? tiku_richsnapshot_find_bookmark(snapshot, name,
                           &app->bookmark_position) :
                       link_target(app, snapshot, &app->bookmark_position);
    tiku_richsnapshot_release(snapshot);
    write_refused(app, status);
    if (status == TIKU_RICH_UNSUPPORTED)
        snprintf(app->status, sizeof app->status,
                 "Follow document link opens #bookmark targets, not external URLs.");
    else if (status == TIKU_RICH_NOT_FOUND)
        snprintf(app->status, sizeof app->status,
                 "No matching bookmark. Names are case-sensitive.");
    if (status != TIKU_RICH_OK) return;
    write_links_clear(app);
    write_search_cancel(app);
    app->bookmark_generation = tiku_richdoc_generation(app->doc);
    app->bookmark_pending = 1;
    app->panel = W_PANEL_NONE;
    write_controls(app);
    snprintf(app->status, sizeof app->status, "Opening bookmark...");
    write_bookmarks_step(app);
}

/** @brief Open a single-name panel with a generation-bound caret for insertion. */
static void
panel(write_app_t *app, int action)
{
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_bookmark_t mark = {0};
    tiku_rich_pos_t anchor;

    if (action != W_BOOKMARK_ADD &&
        tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK)
        tiku_richsnapshot_bookmark(snapshot, 0, &mark);
    tiku_richsnapshot_release(snapshot);
    tiku_richsession_selection(tiku_richview_session(app->view), &anchor,
                                &app->bookmark_position);
    app->bookmark_generation = tiku_richdoc_generation(app->doc);
    app->bookmark_pending = 0;
    app->panel = W_PANEL_BOOKMARK_ADD + action - W_BOOKMARK_ADD;
    tiku_field_start(&app->fields[0], mark.name);
    tiku_field_input_init(&app->field_input[0]);
    write_search_cancel(app);
    write_links_clear(app);
    write_controls(app);
    tiku_dialog_focus(&app->dialog, W_FIELD);
    snprintf(app->status, sizeof app->status, "%s",
        action == W_BOOKMARK_ADD ?
        "Name this caret position (1-63 UTF-8 bytes). An existing name moves here." :
        action == W_BOOKMARK_GO ? "Go to bookmark by exact name." :
        "Remove a bookmark by exact name. Text is kept; Undo restores the bookmark.");
}

/** @brief Apply a checked name without discarding the panel on refusal. */
static void
apply(write_app_t *app)
{
    tiku_rich_status_t status;
    const char *name = app->fields[0].text;

    if (app->panel == W_PANEL_BOOKMARK_GO) { navigate(app, name); return; }
    status = app->panel == W_PANEL_BOOKMARK_ADD ?
        tiku_richdoc_bookmark_set(app->doc, app->bookmark_generation, name,
                                    app->bookmark_position) :
        tiku_richdoc_bookmark_remove(app->doc, app->bookmark_generation, name);
    write_refused(app, status);
    if (status == TIKU_RICH_STALE)
        snprintf(app->status, sizeof app->status,
                 "Document changed. Reopen the bookmark command and try again.");
    if (status != TIKU_RICH_OK && status != TIKU_RICH_NO_CHANGE) return;
    write_changed(app, app->bookmark_generation);
    snprintf(app->status, sizeof app->status, "%s: %.63s",
        app->panel == W_PANEL_BOOKMARK_ADD ? "Bookmark saved" : "Bookmark removed", name);
    app->panel = W_PANEL_NONE;
    write_controls(app);
    tiku_dialog_focus(&app->dialog, W_BODY);
}

int
write_bookmarks_action(write_app_t *app, int action)
{
    tiku_rich_cells_t cells;
    int apply_panel = action == W_APPLY &&
        app->panel >= W_PANEL_BOOKMARK_ADD && app->panel <= W_PANEL_BOOKMARK_REMOVE;
    int item = action >= W_BOOKMARK_ITEM && action < W_BOOKMARK_ITEM + 24;
    int edit = action == W_BOOKMARK_ADD || action == W_BOOKMARK_REMOVE ||
        (apply_panel && app->panel != W_PANEL_BOOKMARK_GO);

    if (!apply_panel && !item &&
        (action < W_BOOKMARK_ADD || action > W_LINK_FOLLOW)) return 0;
    if (app->preview || app->prompt || app->qcount ||
        tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK) return 1;
    if (edit && tiku_richsession_read_only(tiku_richview_session(app->view))) {
        write_refused(app, TIKU_RICH_READ_ONLY);
        return 1;
    }
    if (item) {
        if (app->bookmark_menu_generation != tiku_richdoc_generation(app->doc))
            write_refused(app, TIKU_RICH_STALE);
        else navigate(app, app->menu_bookmarks[action - W_BOOKMARK_ITEM]);
    } else if (apply_panel) apply(app);
    else if (action == W_LINK_FOLLOW) navigate(app, NULL);
    else panel(app, action);
    return 1;
}

void
write_bookmarks_step(write_app_t *app)
{
    const tiku_richlayout_t *layout;
    tiku_rich_pos_t position;
    tiku_rich_status_t status;

    if (!app->bookmark_pending) return;
    if (app->preview || app->prompt || app->bookmark_generation !=
        tiku_richdoc_generation(app->doc)) {
        app->bookmark_pending = 0;
        app->status[0] = 0;
        return;
    }
    if (app->qcount) return;
    layout = tiku_richview_layout(app->view);
    if (!layout && tiku_richview_status(app->view) == TIKU_RICH_PENDING) return;
    status = layout ? tiku_richlayout_snap(layout, app->bookmark_position,
        app->bookmark_position.affinity == TIKU_RICH_AFTER, &position) :
        tiku_richview_status(app->view);
    app->bookmark_pending = 0;
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_select(tiku_richview_session(app->view),
            app->bookmark_generation, position, position);
    write_refused(app, status);
    if (status != TIKU_RICH_OK) return;
    tiku_dialog_focus(&app->dialog, W_BODY);
    app->reveal = app->repaint = 1;
    snprintf(app->status, sizeof app->status, "Bookmark reached.");
}
