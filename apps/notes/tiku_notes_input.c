/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notes_input.c - shared editing, ordered input and plain clipboard.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_notes_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Display an operation refusal without discarding pending edits. */
static void
refused(note_t *n, tiku_rich_status_t status)
{
    if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE ||
        status == TIKU_RICH_PENDING) return;
    snprintf(n->error, sizeof n->error, "%s",
        status == TIKU_RICH_LIMIT ? "Note or history limit reached" :
        status == TIKU_RICH_NO_MEMORY ? "Not enough memory; edit refused" :
        status == TIKU_RICH_INVALID_TEXT ? "Invalid text; edit refused" :
        "Operation refused; text unchanged");
}

/** @brief Copy text first; cut removes it only after publication succeeds. */
static tiku_rich_status_t
copy_text(notes_t *st, note_t *n, int cut)
{
    tiku_richsession_t *session = tiku_richview_session(n->view);
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_DELETE};
    tiku_rich_status_t status;
    size_t length;
    char *text;
    int published;

    if (!st->services->clip_set) return TIKU_RICH_UNSUPPORTED;
    status = tiku_richsession_text(session, NULL, 0, &length);
    if (status != TIKU_RICH_OK || !length) return status;
    text = malloc(length + 1);
    if (!text) return TIKU_RICH_NO_MEMORY;
    status = tiku_richsession_text(session, text, length + 1, &length);
    published = status == TIKU_RICH_OK &&
        st->services->clip_set(st->services->ctx, text);
    free(text);
    if (!published) return TIKU_RICH_UNSUPPORTED;
    return cut ? tiku_richsession_execute(session, 0, &command) :
                 TIKU_RICH_OK;
}

void
note_edit(notes_t *st, note_t *n, int action)
{
    tiku_richsession_t *session = tiku_richview_session(n->view);
    tiku_richcmd_t command = {0};
    tiku_rich_status_t status = TIKU_RICH_NO_CHANGE;
    uint64_t before = tiku_richdoc_generation(n->doc);

    if (n->queue_count) {
        snprintf(n->error, sizeof n->error, "Wait for queued input to finish");
        note_refresh(st, n);
        return;
    }
    if (action == CMD_UNDO || action == CMD_REDO) {
        command.kind = action == CMD_UNDO ? TIKU_RICHCMD_UNDO :
                                           TIKU_RICHCMD_REDO;
    } else if (action == CMD_PASTE && st->services->clip_text) {
        command.kind = TIKU_RICHCMD_REPLACE_TEXT;
        command.value.text.data = st->services->clip_text(st->services->ctx,
                                                         NULL);
        if (!command.value.text.data) command.value.text.data = "";
        command.value.text.length = strlen(command.value.text.data);
        if (!command.value.text.length) command.kind = 0;
    } else if (action == CMD_CUT || action == CMD_COPY) {
        status = copy_text(st, n, action == CMD_CUT);
    } else if (action == CMD_SELECT_ALL) {
        status = tiku_richview_select_all(n->view);
    }
    if (command.kind) status = tiku_richsession_execute(session, 0, &command);
    refused(n, status);
    if (before != tiku_richdoc_generation(n->doc)) {
        n->closing = 0;
        st->quitting = 0;
        n->reveal = 1;
        n->edited_at = 0;
    }
    note_step(st, n);
    note_refresh(st, n);
}

/** @brief Handle a double or triple press using the shared word/line query. */
static tiku_rich_status_t
press(note_t *n, const tiku_event_t *event)
{
    tiku_rich_status_t status;
    int handled;

    status = tiku_richview_event(n->view, event, &handled);
    if (status != TIKU_RICH_OK) return status;
    if (!(event->modifiers & TIKU_MOD_SHIFT) && n->clicks &&
        event->time_us >= n->clicked_at &&
        event->time_us - n->clicked_at <= DOUBLE_CLICK_US &&
        abs(event->x - n->click_x) <= 4 && abs(event->y - n->click_y) <= 4)
        n->clicks = n->clicks % 3 + 1;
    else n->clicks = 1;
    n->clicked_at = event->time_us;
    n->click_x = event->x;
    n->click_y = event->y;
    if (n->clicks > 1)
        status = tiku_richview_select_at(n->view, event->x, event->y,
                                         n->clicks == 3);
    return status;
}

/** @brief Dispatch one ready event, suppressing nonpersistent formatting. */
static int
deliver(note_t *n, tiku_event_t event)
{
    uint64_t before = tiku_richdoc_generation(n->doc);
    tiku_rich_status_t status;
    int handled = 0;
    unsigned key = event.key;

    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    if (event.type == TIKU_EVENT_KEY_DOWN && event.modifiers & TIKU_MOD_CMD) {
        if (key == 'b' || key == 'i' || key == 'u') {
            event.key = 0;
            event.text[0] = '\0';
        }
    }
    if (event.type == TIKU_EVENT_POINTER_DOWN && event.button == 1) {
        status = press(n, &event);
        handled = 1;
    } else status = tiku_richview_event(n->view, &event, &handled);
    refused(n, status);
    if (before != tiku_richdoc_generation(n->doc)) {
        n->edited_at = event.time_us;
        n->reveal = 1;
    }
    return status != TIKU_RICH_PENDING &&
        (handled || status != TIKU_RICH_NO_CHANGE);
}

void
note_step(notes_t *st, note_t *n)
{
    tiku_rich_status_t before = tiku_richview_status(n->view);
    tiku_rich_status_t status = tiku_richview_step(n->view, 8192);
    int changed = before != status, remaining = 16;

    refused(n, status);
    if (status != TIKU_RICH_OK && status != TIKU_RICH_PENDING &&
        n->queue_count) {
        n->queue_count = 0;
        n->cancel_input = 1;
        changed = 1;
    }
    while (status == TIKU_RICH_OK && n->queue_count && remaining--) {
        tiku_event_t event = n->queued[n->queue_first];

        n->queue_first = (n->queue_first + 1) % NOTE_QUEUE_MAX;
        n->queue_count--;
        changed |= deliver(n, event);
        status = tiku_richview_status(n->view);
    }
    if (status == TIKU_RICH_OK && n->reveal) {
        tiku_richview_reveal(n->view);
        n->reveal = 0;
        changed = 1;
    }
    if (!n->queue_count && n->cancel_input) {
        tiku_event_t cancel = {.type = TIKU_EVENT_DEACTIVATED};
        int handled;

        tiku_richview_event(n->view, &cancel, &handled);
        n->cancel_input = 0;
    }
    if (changed) note_refresh(st, n);
}

void
note_input(notes_t *st, note_t *n, const tiku_event_t *event)
{
    tiku_event_t local = *event;
    size_t at;

    if (event->type == TIKU_EVENT_KEY_DOWN ||
        event->type == TIKU_EVENT_TEXT_INPUT ||
        event->type == TIKU_EVENT_POINTER_DOWN) {
        n->closing = 0;
        st->quitting = 0;
    }
    if (event->type == TIKU_EVENT_POINTER_DOWN ||
        event->type == TIKU_EVENT_POINTER_MOVE ||
        event->type == TIKU_EVENT_POINTER_UP) {
        local.x -= MARGIN;
        local.y -= MARGIN;
    }
    if (n->queue_count == NOTE_QUEUE_MAX || n->cancel_input) {
        n->cancel_input = 1;
        snprintf(n->error, sizeof n->error, "Input queue full; input refused");
        note_refresh(st, n);
        return;
    }
    at = (n->queue_first + n->queue_count) % NOTE_QUEUE_MAX;
    n->queued[at] = local;
    n->queue_count++;
    note_step(st, n);
    /* Small notes finish the edit's new layout in the same event turn. */
    if (tiku_richview_status(n->view) == TIKU_RICH_PENDING) note_step(st, n);
}
