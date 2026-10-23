/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_document_model.c - styled text, atomic edits and snapshots without UI.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc.h"
#include "tiku_richcmd.h"
#include "tiku_richsession.h"
#include "tiku_richwatch.h"

#include <stdio.h>

/** @brief Import two paragraphs and an inherited heading style. */
static tiku_rich_status_t
load_document(tiku_richdoc_t *doc)
{
    const tiku_rich_style_source_t styles[2] = {
        {.name = "Body",
         .chars = {.present = TIKU_RICH_SIZE, .size = 12}},
        {.name = "Heading", .parent = 1,
         .chars = {.present = TIKU_RICH_SIZE | TIKU_RICH_BOLD,
                   .size = 18, .bold = 1}}
    };
    const tiku_rich_paragraph_source_t paragraphs[2] = {
        {.text = "Field notes", .length = 11, .style = 2},
        {.text = "Temperature: 23 C", .length = 17, .style = 1}
    };
    const tiku_rich_source_t source = {styles, 2, paragraphs, 2};

    return tiku_richdoc_import(doc, &source, 0);
}

/** @brief Replace one reading and emphasize it in one history group. */
static tiku_rich_status_t
change_reading(tiku_richdoc_t *doc, const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_paragraph_t paragraph;
    tiku_rich_edit_t edits[2] = {{0}, {0}};
    tiku_rich_transaction_t change = {
        tiku_richsnapshot_generation(snapshot), edits, 2
    };
    tiku_rich_status_t status;

    status = tiku_richsnapshot_paragraph(snapshot, 1, &paragraph);
    if (status != TIKU_RICH_OK)
        return status;
    edits[0] = (tiku_rich_edit_t){.paragraph = paragraph.id,
        .start = 13, .end = 15, .text = "24", .length = 2};
    edits[1] = (tiku_rich_edit_t){.kind = TIKU_RICH_FORMAT,
        .paragraph = paragraph.id, .start = 13, .end = 15,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    return tiku_richdoc_apply(doc, &change);
}

/** @brief Print the live text and use one undo to restore the loaded state. */
static tiku_rich_status_t
show_and_undo(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_status_t status;
    char text[256];
    size_t length;

    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_text(snapshot, text, sizeof text, &length);
    if (status == TIKU_RICH_OK)
        printf("%s\nDirty after edit: %d\n", text, tiku_richdoc_dirty(doc));
    tiku_richsnapshot_release(snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richdoc_undo(doc);
    if (status == TIKU_RICH_OK)
        printf("Dirty after undo: %d\n", tiku_richdoc_dirty(doc));
    return status;
}

/** @brief Paste a paragraph and check that a tracked caret follows undo. */
static tiku_rich_status_t
paste_with_caret(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richanchor_t *caret = NULL;
    tiku_rich_paragraph_t paragraph;
    tiku_rich_pos_t before, after;
    tiku_rich_status_t status;
    const char text[] = "\nHumidity: 40%";

    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_paragraph(snapshot, 1, &paragraph);
    if (status != TIKU_RICH_OK)
        goto done;
    before = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
        paragraph.id, paragraph.length, TIKU_RICH_AFTER};
    status = tiku_richanchor_create(doc, before, &caret);
    if (status != TIKU_RICH_OK)
        goto done;
    status = tiku_richdoc_replace_range(doc,
        tiku_richsnapshot_generation(snapshot), before, before,
        text, sizeof text - 1);
    if (status != TIKU_RICH_OK)
        goto done;
    tiku_richsnapshot_release(snapshot);
    snapshot = NULL;
    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        goto done;
    printf("After multiline paste: %zu paragraphs\n",
           tiku_richsnapshot_paragraphs(snapshot));
    status = tiku_richdoc_undo(doc);
    if (status != TIKU_RICH_OK)
        goto done;
    status = tiku_richanchor_get(caret, &after);
    if (status == TIKU_RICH_OK && (after.paragraph != before.paragraph ||
        after.byte != before.byte || after.affinity != before.affinity))
        status = TIKU_RICH_INVALID_RANGE;
    if (status == TIKU_RICH_OK)
        puts("Undo restored the tracked caret");
done:
    tiku_richsnapshot_release(snapshot);
    tiku_richanchor_release(caret);
    return status;
}

/** @brief Change a named style and a paragraph's alignment in one group. */
static tiku_rich_status_t
style_heading(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_style_t body;
    tiku_rich_paragraph_t heading;
    tiku_rich_style_spec_t spec = {.name = "Body",
        .chars = {.present = TIKU_RICH_SIZE, .size = 14}};
    tiku_rich_edit_t edits[2] = {{0}, {0}};
    tiku_rich_transaction_t change = {.edits = edits, .count = 2};
    tiku_rich_status_t status;

    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_style(snapshot, 0, &body);
    if (status == TIKU_RICH_OK)
        status = tiku_richsnapshot_paragraph(snapshot, 0, &heading);
    if (status == TIKU_RICH_OK) {
        change.generation = tiku_richsnapshot_generation(snapshot);
        edits[0] = (tiku_rich_edit_t){.kind = TIKU_RICH_STYLE_DEFINE,
            .style = body.id, .definition = &spec};
        edits[1] = (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_FORMAT,
            .paragraph = heading.id,
            .props = {.present = TIKU_RICH_ALIGNMENT,
                      .align = TIKU_RICH_CENTER}};
        status = tiku_richdoc_apply(doc, &change);
    }
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICH_OK)
        puts("Body style: 14 pt; heading centered");
    return status;
}

/** @brief Find a measurement and replace using the captured generation. */
static tiku_rich_status_t
find_reading(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richsearch_t *search = NULL;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t first, last;
    tiku_rich_match_t match;
    tiku_rich_status_t status;

    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_paragraph(snapshot, 1, &p);
    if (status == TIKU_RICH_OK) {
        first = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
            p.id, 0, TIKU_RICH_AFTER};
        last = first;
        last.byte = p.length;
        status = tiku_richsearch_create(snapshot, first, last, "23", 2,
                                         &search);
    }
    if (status == TIKU_RICH_OK) {
        do {
            status = tiku_richsearch_next(search, 8, &match);
        } while (status == TIKU_RICH_PENDING);
    }
    if (status == TIKU_RICH_OK)
        status = tiku_richdoc_replace_range(doc, match.generation,
            match.first, match.last, "25", 2);
    tiku_richsearch_release(search);
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICH_OK)
        puts("Search replaced the reading with 25 C");
    return status;
}

/** @brief Add a numbered reading and a linked heading in one transaction. */
static tiku_rich_status_t
list_and_link(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t heading, reading;
    tiku_rich_edit_t edits[2] = {{0}, {0}};
    tiku_rich_transaction_t change = {.edits = edits, .count = 2};
    tiku_rich_list_t list;
    uint64_t ordinal;
    tiku_rich_status_t status;

    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_paragraph(snapshot, 0, &heading);
    if (status == TIKU_RICH_OK)
        status = tiku_richsnapshot_paragraph(snapshot, 1, &reading);
    if (status == TIKU_RICH_OK) {
        change.generation = tiku_richsnapshot_generation(snapshot);
        edits[0] = (tiku_rich_edit_t){.kind = TIKU_RICH_LIST_SET,
            .paragraph = reading.id,
            .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}};
        edits[1] = (tiku_rich_edit_t){.kind = TIKU_RICH_LINK_SET,
            .paragraph = heading.id, .end = heading.length,
            .link = "https://example.org/field-notes"};
        status = tiku_richdoc_apply(doc, &change);
    }
    tiku_richsnapshot_release(snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_list(snapshot, 1, &list, &ordinal);
    if (status == TIKU_RICH_OK)
        printf("Reading list item: %llu; heading link stored\n",
               (unsigned long long)ordinal);
    tiku_richsnapshot_release(snapshot);
    return status;
}

/** @brief Get the beginning and end of a snapshot's main text body. */
static tiku_rich_status_t
document_bounds(const tiku_richsnapshot_t *snapshot,
                tiku_rich_pos_t *first, tiku_rich_pos_t *last)
{
    tiku_rich_paragraph_t paragraph;
    tiku_rich_status_t status;

    status = tiku_richsnapshot_paragraph(snapshot, 0, &paragraph);
    if (status != TIKU_RICH_OK)
        return status;
    *first = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), paragraph.id,
                               0, TIKU_RICH_BEFORE};
    status = tiku_richsnapshot_paragraph(snapshot,
        tiku_richsnapshot_paragraphs(snapshot) - 1, &paragraph);
    if (status == TIKU_RICH_OK)
        *last = (tiku_rich_pos_t){first->body, paragraph.id, paragraph.length,
                                  TIKU_RICH_AFTER};
    return status;
}

/** @brief Inspect selected formatting and transfer the styled content. */
static tiku_rich_status_t
copy_styled_document(tiku_richdoc_t *doc)
{
    tiku_richdoc_t *copy = NULL;
    tiku_richsnapshot_t *source = NULL, *target = NULL;
    tiku_richfragment_t *fragment = NULL;
    tiku_rich_paragraph_t empty;
    tiku_rich_selection_t state;
    tiku_rich_pos_t first, last;
    tiku_rich_status_t status;

    status = tiku_richdoc_snapshot(doc, &source);
    if (status != TIKU_RICH_OK)
        goto done;
    status = document_bounds(source, &first, &last);
    if (status != TIKU_RICH_OK)
        goto done;
    status = tiku_richsnapshot_selection(source, first, last, &state);
    if (status != TIKU_RICH_OK)
        goto done;
    printf("Selection has mixed font sizes: %s\n",
           state.mixed_chars & TIKU_RICH_SIZE ? "yes" : "no");
    status = tiku_richfragment_create(source, first, last, &fragment);
    if (status == TIKU_RICH_OK)
        status = tiku_richdoc_create(NULL, &copy);
    if (status == TIKU_RICH_OK)
        status = tiku_richdoc_snapshot(copy, &target);
    if (status == TIKU_RICH_OK)
        status = tiku_richsnapshot_paragraph(target, 0, &empty);
    if (status != TIKU_RICH_OK)
        goto done;
    first = (tiku_rich_pos_t){tiku_richsnapshot_body(target), empty.id,
                              0, TIKU_RICH_AFTER};
    status = tiku_richdoc_paste_fragment(copy,
        tiku_richsnapshot_generation(target), first, first, fragment);
    if (status == TIKU_RICH_OK)
        puts("Copied styled paragraphs, list and link into another document");
done:
    tiku_richfragment_release(fragment);
    tiku_richsnapshot_release(source);
    tiku_richsnapshot_release(target);
    tiku_richdoc_release(copy);
    return status;
}

/** @brief Query, execute and undo a toolbar toggle through shared commands. */
static tiku_rich_status_t
command_example(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richcmd_context_t context = {0};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_BOLD};
    tiku_richcmd_state_t state;
    tiku_rich_status_t status;

    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK)
        return status;
    context.generation = tiku_richsnapshot_generation(snapshot);
    status = document_bounds(snapshot, &context.first, &context.last);
    if (status == TIKU_RICH_OK)
        status = tiku_richcmd_query(doc, &context, command.kind, &state);
    if (status == TIKU_RICH_OK && !state.enabled)
        status = state.status;
    if (status == TIKU_RICH_OK) {
        printf("Command catalog: %zu; Bold state: %s\n", tiku_richcmd_count(),
               state.check == TIKU_RICHCMD_CHECK_MIXED ? "mixed" : "uniform");
        status = tiku_richcmd_execute(doc, &context, &command);
    }
    if (status == TIKU_RICH_OK) {
        context.generation = tiku_richdoc_generation(doc);
        command.kind = TIKU_RICHCMD_UNDO;
        status = tiku_richcmd_execute(doc, &context, &command);
    }
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICH_OK)
        puts("One Undo restored both paragraphs after the Bold command");
    return status;
}

/** @brief Count published changes; a graphical host can queue a repaint. */
static void
document_changed(void *ctx, tiku_richdoc_t *doc,
                 const tiku_rich_change_t *change)
{
    unsigned *count = ctx;

    (void)doc;
    if (change->kind == TIKU_RICH_CHANGE_EDIT ||
        change->kind == TIKU_RICH_CHANGE_UNDO)
        (*count)++;
}

/** @brief Keep two views' typing state separate and observe committed edits. */
static tiku_rich_status_t
session_example(void)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsession_t *left = NULL, *right = NULL;
    tiku_richwatch_t *watch = NULL;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_BOLD};
    tiku_richcmd_state_t state;
    tiku_rich_status_t status;
    unsigned changes = 0;

    status = tiku_richdoc_create(NULL, &doc);
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_create(doc, &left);
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_create(doc, &right);
    if (status == TIKU_RICH_OK)
        status = tiku_richwatch_create(doc, document_changed, &changes, &watch);
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_execute(left, 0, &command);
    if (status == TIKU_RICH_OK)
        status = tiku_richsession_query(right, command.kind, &state);
    if (status == TIKU_RICH_OK &&
        state.check != TIKU_RICHCMD_CHECK_UNSPECIFIED)
        status = TIKU_RICH_INVALID;
    if (status == TIKU_RICH_OK) {
        command.kind = TIKU_RICHCMD_REPLACE_TEXT;
        command.value.text.data = "Shared notes";
        command.value.text.length = 12;
        status = tiku_richsession_execute(left, 0, &command);
    }
    if (status == TIKU_RICH_OK) {
        command.kind = TIKU_RICHCMD_UNDO;
        status = tiku_richsession_execute(right, 0, &command);
    }
    if (status == TIKU_RICH_OK)
        printf("Two views: independent typing formats; %u published changes\n",
               changes);
    tiku_richwatch_destroy(watch);
    tiku_richsession_destroy(left);
    tiku_richsession_destroy(right);
    tiku_richdoc_release(doc);
    return status;
}

/** @brief Run the model example without a window, event loop or device. */
int
main(void)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *before = NULL;
    tiku_rich_status_t status;

    status = tiku_richdoc_create(NULL, &doc);
    if (status == TIKU_RICH_OK)
        status = load_document(doc);
    if (status == TIKU_RICH_OK)
        status = tiku_richdoc_snapshot(doc, &before);
    if (status == TIKU_RICH_OK)
        status = change_reading(doc, before);
    if (status == TIKU_RICH_OK)
        status = show_and_undo(doc);
    if (status == TIKU_RICH_OK)
        status = paste_with_caret(doc);
    if (status == TIKU_RICH_OK)
        status = style_heading(doc);
    if (status == TIKU_RICH_OK)
        status = find_reading(doc);
    if (status == TIKU_RICH_OK)
        status = list_and_link(doc);
    if (status == TIKU_RICH_OK)
        status = copy_styled_document(doc);
    if (status == TIKU_RICH_OK)
        status = command_example(doc);
    if (status == TIKU_RICH_OK)
        status = session_example();
    tiku_richsnapshot_release(before);
    tiku_richdoc_release(doc);
    if (status != TIKU_RICH_OK)
        fprintf(stderr, "Document example failed: %d\n", (int)status);
    return status == TIKU_RICH_OK ? 0 : 1;
}
