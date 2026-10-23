/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notes_view.c - note frames, document geometry and menu state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_notes_private.h"
#include "tiku_font.h"
#include <stdio.h>
#include <string.h>

const note_paper_t PAPERS[NPAPERS] = {
    {"Yellow", 0xffee8c}, {"Green", 0xc8f5aa},
    {"Blue", 0xb4dcff}, {"Pink", 0xffc8dc}
};

void
note_title(const note_t *n, char *out, size_t capacity)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t paragraph;
    const char *text = "Note";
    size_t length = 4, prefix, room;

    if (!out || !capacity) return;
    snapshot = NULL;
    if (tiku_richdoc_snapshot(n->doc, &snapshot) == TIKU_RICH_OK &&
        tiku_richsnapshot_paragraph(snapshot, 0, &paragraph) == TIKU_RICH_OK) {
        text = paragraph.text;
        length = paragraph.length;
        while (length && (*text == ' ' || *text == '\t')) { text++; length--; }
        if (!length) { text = "Note"; length = 4; }
    }
    prefix = n->important && capacity > 2 ? 2 : 0;
    room = capacity - 1 - prefix;
    if (length > room) {
        length = room;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80)
            length--;
    }
    if (prefix) memcpy(out, "! ", 2);
    memcpy(out + prefix, text, length);
    out[prefix + length] = '\0';
    tiku_richsnapshot_release(snapshot);
}

void
note_paint(notes_t *st, note_t *n)
{
    tiku_rgb_t face = PAPERS[n->paper].face;
    const tiku_font_t *font = tiku_font_plain();
    const tiku_richlayout_t *layout = tiku_richview_layout(n->view);
    tiku_rich_status_t status = tiku_richview_status(n->view);
    const char *message = n->error;
    int i;

    if (!n->surface || !n->id) return;
    tiku_clip_reset(n->surface);
    tiku_fill(n->surface, (tiku_rect_t){0, 0, n->w, n->h}, face);
    tiku_vline(n->surface, 0, 0, n->h, tiku_tint(face, TIKU_DARKEN_2));
    if (layout) {
        status = tiku_richfont_draw_view(st->fonts, n->view, n->surface,
            (tiku_rect_t){MARGIN, MARGIN, n->w - 2 * MARGIN,
                         n->h - 2 * MARGIN},
            n->active ? 0x704b7fcc : 0x50757575, TIKU_C_TEXT, n->active);
        if (status != TIKU_RICH_OK) {
            tiku_fill(n->surface, (tiku_rect_t){MARGIN, MARGIN,
                n->w - 2 * MARGIN, n->h - 2 * MARGIN}, face);
        }
    }
    if (status != TIKU_RICH_OK)
        message = status == TIKU_RICH_PENDING ? "Laying out note..." :
                                               "Cannot display note";
    else if (!*message && n->closing) message = "Saving before closing...";
    else if (!*message && tiku_richlayout_warnings(layout) &
             TIKU_RICH_LAYOUT_SHAPING) message = "Limited script shaping";
    if (*message) {
        int y = n->h - font->height - 3;

        tiku_fill(n->surface, (tiku_rect_t){MARGIN, y,
            n->w - 2 * MARGIN, font->height + 3}, face);
        tiku_clip_set(n->surface, (tiku_rect_t){MARGIN, y,
            n->w - 2 * MARGIN, font->height + 3});
        tiku_text(n->surface, font, MARGIN, y + font->ascent, message,
                  0x703b18);
        tiku_clip_reset(n->surface);
    }
    if (n->important)
        for (i = 0; i < 14; i++)
            tiku_hline(n->surface, n->w - 14 + i, i, 14 - i,
                        tiku_tint(face, TIKU_DARKEN_2));
    st->services->frame(st->services->ctx, n->id, n->surface->px, n->w, n->h);
}

/** @brief Append one menu row; an empty label is a separator. */
static void
row(tiku_menu_list_t *menu, const char *label, int command, int key,
    int enabled)
{
    int i = menu->nitem++;

    snprintf(menu->item[i].label, sizeof menu->item[i].label, "%s", label);
    menu->item[i].command = command;
    menu->item[i].sc = key;
    menu->item[i].mods = key ? TIKU_MOD_CMD : 0;
    menu->item[i].enabled = !!enabled;
}

/** @brief Append editing commands using document and clipboard availability. */
static void
edit_menu(notes_t *st, note_t *n, tiku_menu_list_t *menu)
{
    tiku_richsession_t *session = tiku_richview_session(n->view);
    tiku_richcmd_state_t state;
    size_t selected = 0;
    int available = !n->queue_count;
    const char *clipboard = st->services->clip_text ?
        st->services->clip_text(st->services->ctx, NULL) : "";

    tiku_richsession_text(session, NULL, 0, &selected);
    snprintf(menu->title, sizeof menu->title, "Edit");
    tiku_richsession_query(session, TIKU_RICHCMD_UNDO, &state);
    row(menu, "Undo", CMD_UNDO, 'z', available && state.enabled);
    tiku_richsession_query(session, TIKU_RICHCMD_REDO, &state);
    row(menu, "Redo", CMD_REDO, 'y', available && state.enabled);
    row(menu, "", 0, 0, 0);
    row(menu, "Cut", CMD_CUT, 'x',
        available && selected && st->services->clip_set);
    row(menu, "Copy", CMD_COPY, 'c',
        available && selected && st->services->clip_set);
    row(menu, "Paste", CMD_PASTE, 'v', available && clipboard && *clipboard);
    row(menu, "Select all", CMD_SELECT_ALL, 'a', available);
}

void
note_publish(notes_t *st, note_t *n)
{
    tiku_menuset_t menus = {0};
    tiku_menu_list_t *menu = &menus.menu[0];
    int i, pass;

    if (!n->id) return;
    menus.nmenu = 3;
    snprintf(menu->title, sizeof menu->title, "Note");
    row(menu, "New note", CMD_NEW, 'n', 1);
    row(menu, "Save", CMD_SAVE, 's', note_dirty(n) && !n->save);
    row(menu, n->important ? "Not important" : "Important", CMD_IMPORTANT,
        0, 1);
    row(menu, "", 0, 0, 0);
    for (i = 0; i < NPAPERS; i++)
        row(menu, PAPERS[i].name, CMD_COLOUR + i, 0, i != n->paper);
    row(menu, "", 0, 0, 0);
    row(menu, "Delete note", CMD_DELETE, 0, !n->save);
    row(menu, "Quit", CMD_QUIT, 'q', 1);
    edit_menu(st, n, &menus.menu[1]);
    menu = &menus.menu[2];
    snprintf(menu->title, sizeof menu->title, "Notes");
    for (pass = 1; pass >= 0; pass--)
        for (i = 0; i < NOTES_MAX; i++) {
            note_t *other = &st->note[i];
            char title[TIKU_MENUSET_LABEL];

            if (!other->used || !other->id || other->important != pass)
                continue;
            note_title(other, title, sizeof title);
            row(menu, title, CMD_SHOW + i, 0, other != n);
        }
    st->services->menus(st->services->ctx, n->id, &menus);
}

void
note_refresh(notes_t *st, note_t *n)
{
    char title[128];

    if (!n->id) return;
    note_title(n, title, sizeof title);
    if (st->services->configure)
        st->services->configure(st->services->ctx, n->id, title, 0, 0, 1, 1);
    note_paint(st, n);
    note_publish(st, n);
}
