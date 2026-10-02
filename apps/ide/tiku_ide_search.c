/* tiku_ide_search.c -- search.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static tiku_edit_query_t
query_of(const edit_t *e);
static void
edit_find_run(edit_t *e);

/** @brief The find field as a literal, case-sensitive query. */
static tiku_edit_query_t
query_of(const edit_t *e)
{
    tiku_edit_query_t q;

    q.text = e->find.text;
    q.length = strlen(e->find.text);
    q.flags = TIKU_EDIT_MATCH_CASE;
    return q;
}

/** @brief Select from @p anchor to @p caret and bring the caret into
 *         view. */
void
tiku_ide_edit_select(edit_t *e, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    (void)tiku_codeview_select(e->view, anchor, caret);
    tiku_ide_edit_reveal(e);
}

/**
 * @brief Run the find field: a hit becomes the selection.
 *
 * For a BASIC file a search that is ALL DIGITS is first tried as a
 * line NUMBER -- the caret goes to that line and its number is
 * selected -- and only if no line wears it is it looked for as text.
 * Everything else is text, found forward from the caret with a wrap,
 * so Return walks the hits.
 */
static void
edit_find_run(edit_t *e)
{
    const char *q = e->find.text;
    tiku_edit_query_t query;
    tiku_codeview_found_t found;
    tiku_edit_pos_t anchor = 0, caret = 0;

    if (q[0] == '\0') {
        return;
    }
    if (e->lang == TIKU_SYNTAX_BASIC && !e->replacing) {
        size_t n = strlen(q), i;
        int alldig = n < 32;

        for (i = 0; q[i] != '\0'; i++) {
            if (q[i] < '0' || q[i] > '9') { alldig = 0; break; }
        }
        if (alldig) {
            int64_t total = tiku_editdoc_lines(e->doc), L;

            for (L = 0; L < total; L++) {
                tiku_edit_range_t r;
                char head[34];
                size_t got = 0;

                r.start = tiku_editdoc_line_start(e->doc, L);
                r.end = tiku_editdoc_line_end(e->doc, L);
                if (r.end - r.start > (int64_t)n + 1) {
                    r.end = r.start + (int64_t)n + 1;
                }
                if (tiku_editdoc_read(e->doc, r, head, sizeof head, &got) ==
                        TIKU_EDIT_OK && got >= n && memcmp(head, q, n) == 0 &&
                    (got == n || head[n] < '0' || head[n] > '9')) {
                    tiku_ide_edit_select(e, r.start, r.start + (int64_t)n);
                    return;
                }
            }
            /* no line wears it: fall through and find it as text */
        }
    }
    query = query_of(e);
    if (tiku_codeview_find(e->view, &query, 0, &found) == TIKU_EDIT_OK &&
        found.found &&
        tiku_codeview_selection(e->view, &anchor, &caret) == TIKU_EDIT_OK) {
        tiku_ide_edit_reveal(e);
        snprintf(e->feedback, sizeof e->feedback, "Match at line %lld",
                 (long long)tiku_editdoc_line_of(e->doc, anchor < caret
                                                         ? anchor : caret) + 1);
    } else {
        snprintf(e->feedback, sizeof e->feedback, "No matches");
    }
}

/** Project search uses unsaved editor text where available. */
void
tiku_ide_search_project(ide_t *st, edit_t *e)
{
    int i, skipped = 0;
    edit_t *temp;

    if (st->running) { return; }
    if (!e->find.text[0]) {
        tiku_ide_search_open(e, 1);
        snprintf(e->feedback, sizeof e->feedback,
                 "Enter Find text, then choose Edit > Find in project");
        return;
    }
    temp = calloc(1, sizeof *temp);
    if (!temp) { return; }
    st->nmsg = 0;
    st->msg_problems = 0;
    for (i = 0; i < st->proj.nrow && st->nmsg < IDE_MSGS; i++) {
        edit_t *source;
        int64_t y;

        if (st->proj.row[i].heading) { continue; }
        source = tiku_ide_edit_of_path(st, st->proj.row[i].path);
        if (!source) {
            snprintf(temp->path, sizeof temp->path, "%s", st->proj.row[i].path);
            temp->load_failed = 0;
            tiku_ide_edit_load(temp);
            source = temp;
        }
        if (source->load_failed) { skipped++; }
        for (y = 0; !source->load_failed &&
                    y < tiku_editdoc_lines(source->doc) &&
                    st->nmsg < IDE_MSGS; y++) {
            char *line = tiku_ide_line_dup(source, y, NULL), name[49];
            msg_t *m;

            if (line == NULL || !strstr(line, e->find.text)) {
                free(line);
                continue;
            }
            snprintf(name, sizeof name, "%s", st->proj.row[i].name);
            m = &st->msg[st->nmsg++];
            memset(m, 0, sizeof *m);
            snprintf(m->file, sizeof m->file, "%s", source->path);
            m->line = (int)y + 1;
            snprintf(m->text, sizeof m->text, "%s:%d: %.90s", name, m->line,
                     line);
            free(line);
        }
        if (source == temp) { tiku_ide_text_free(temp); }
    }
    free(temp);
    snprintf(e->feedback, sizeof e->feedback,
             "%d matching lines%s; %d unreadable files skipped", st->nmsg,
             st->nmsg == IDE_MSGS ? " (result limit reached)" : "", skipped);
    tiku_list_set_count(&st->msg_list, st->nmsg);
    if (st->nmsg) { tiku_ide_messages_show(st); }
    else { tiku_ide_messages_paint(st); }
}

/* Literal, case-sensitive search. Replacement never interprets BASIC numbers. */
void
tiku_ide_search_open(edit_t *e, int replacing)
{
    tiku_ide_assist_close(e);
    e->finding = 1;
    e->replacing = replacing;
    e->find_focus = 0;
    /* The last query is kept but SELECTED, so a new one typed over it
     * replaces rather than appends to it. */
    tiku_field_start(&e->find, e->find.text);
    e->find.active = e->replacement.active = 1;
    e->find.editable = e->replacement.editable = 1;
    snprintf(e->feedback, sizeof e->feedback,
             "Case-sensitive text • Enter: next • Tab: change field • Esc: close");
}

tiku_rect_t
tiku_ide_search_button(int i)
{
    return (tiku_rect_t){ 6 + i * 86, SEARCH_TOP + FIND_H * 2 + 2, 80, 22 };
}

void
tiku_ide_search_paint(edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t field = { 78, SEARCH_TOP + FIND_H + 3,
                          e->w - 84, FIND_H - 6 };
    static const char *const labels[] = { "Next", "Replace", "All", "Close" };
    int i;

    tiku_text(e->surface, f, 6, field.y + f->ascent + 1,
              "Replace:", TIKU_C_TEXT);
    tiku_ui_textfield_sel(e->surface, field, e->replacement.text,
                      e->find_focus == 1 ? e->replacement.caret : -1,
                      e->replacement.sel_a, e->replacement.sel_b, 0u);
    for (i = 0; i < 4; i++) {
        tiku_ui_button(e->surface, tiku_ide_search_button(i), labels[i], 0u);
    }
}

/** @brief Replace the selected match and select the next one, or with
 *         @p all every match as one undo step, each selection kept on
 *         its text. */
void
tiku_ide_search_replace(edit_t *e, int all)
{
    tiku_edit_query_t q = query_of(e);
    tiku_codeview_found_t found;
    tiku_edit_range_t selected;
    tiku_edit_pos_t anchor = 0, caret = 0;
    size_t rn = strlen(e->replacement.text);
    int count;

    e->replacing = 1;
    if (!q.length) { return; }
    (void)tiku_codeview_selection(e->view, &anchor, &caret);
    selected.start = anchor < caret ? anchor : caret;
    selected.end = anchor < caret ? caret : anchor;
    if (!all && !tiku_edit_is_match(e->doc, &q, selected)) {
        edit_find_run(e); /* First select a match; never replace unrelated text. */
        return;
    }
    if (!strcmp(e->find.text, e->replacement.text)) {
        /* Nothing would change.  A selected match is stepped past, so
         * the next press goes on to the next one. */
        if (!all) {
            (void)tiku_codeview_select(e->view, selected.end, selected.end);
        }
        snprintf(e->feedback, sizeof e->feedback,
                 "Nothing changed: Replace text is the same as Find text");
        return;
    }
    if (all) {
        (void)tiku_codeview_replace_all(e->view, &q, e->replacement.text, rn,
                                        &found);
    } else {
        (void)tiku_codeview_replace(e->view, &q, e->replacement.text, rn,
                                    &found);
    }
    tiku_ide_edit_reveal(e);
    count = (int)found.replaced;
    snprintf(e->feedback, sizeof e->feedback, "%d replacement%s%s", count,
             count == 1 ? "" : "s", all ? " in this file (Undo available)" : "");
}

int
tiku_ide_search_pointer(edit_t *e, int x, int y)
{
    int i;

    if (!e->finding || y < SEARCH_TOP || y >= tiku_ide_edit_page_top(e)) { return 0; }
    if (y < SEARCH_TOP + FIND_H * 2) {
        tiku_field_t *field;
        int left, col;
        char head[TIKU_FIELD_MAX];
        e->find_focus = y >= SEARCH_TOP + FIND_H;
        left = e->find_focus ? 78 : MARGIN + 6 +
               tiku_text_width(tiku_font_plain(),
                   e->lang == TIKU_SYNTAX_BASIC && !e->replacing ?
                   "Find / line:" : "Find:");
        field = e->find_focus ? &e->replacement : &e->find;
        for (col = 0; col < field->len; col++) {
            memcpy(head, field->text, (size_t)col + 1);
            head[col + 1] = '\0';
            if (tiku_text_width(tiku_font_plain(), head) > x - left - 3) { break; }
        }
        field->caret = field->sel_a = field->sel_b = col;
        if (e->find_focus) { e->replacing = 1; }
        return 1;
    }
    for (i = 0; i < 4; i++) {
        tiku_rect_t r = tiku_ide_search_button(i);
        if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) { continue; }
        if (i == 0) { edit_find_run(e); }
        if (i == 1 || i == 2) { tiku_ide_search_replace(e, i == 2); }
        if (i == 3) { e->finding = 0; }
    }
    return 1;
}

void
tiku_ide_search_key(ide_t *st, edit_t *e, const tiku_event_t *ev)
{
    tiku_field_t *field = e->find_focus ? &e->replacement : &e->find;
    unsigned ch = ev->text[0] ? (unsigned char)ev->text[0] : ev->key;
    int shift = (ev->modifiers & TIKU_MOD_SHIFT) != 0;
    tiku_field_key_t key = TIKU_FIELD_CHAR;

    if (ev->key == TIKU_KEY_ESCAPE) { e->finding = 0; return; }
    if (ev->key == TIKU_KEY_TAB) {
        e->find_focus = !e->find_focus;
        e->replacing = 1;
        return;
    }
    if (ev->key == TIKU_KEY_RETURN) {
        if (e->find_focus) { tiku_ide_search_replace(e, 0); }
        else { edit_find_run(e); }
        return;
    }
    if (ev->modifiers & TIKU_MOD_CMD) {
        /* A chord is told by its key: on X11 a Ctrl chord's text is a
         * control byte, not its letter. */
        unsigned k = ev->key;
        int from, to;

        if (k >= 'A' && k <= 'Z') { k += 32u; }
        if (k == 'a') { key = TIKU_FIELD_SELECT_ALL; }
        else if (k == 'c' || k == 'x') {
            char taken[TIKU_FIELD_MAX];

            if (!tiku_field_selection(field, &from, &to)) { return; }
            memcpy(taken, field->text + from, (size_t)(to - from));
            taken[to - from] = '\0';
            if (st->services->clip_set) {
                (void)st->services->clip_set(st->services->ctx, taken);
            }
            if (k == 'x') {
                (void)tiku_field_key(field, TIKU_FIELD_BACKSPACE, 0, 0);
            }
            return;
        } else if (k == 'v' && st->services->clip_text) {
            const char *clip = st->services->clip_text(st->services->ctx, NULL);
            int i;
            for (i = 0; clip[i] && i < TIKU_FIELD_MAX - 1; i++) {
                if ((unsigned char)clip[i] >= 32 && (unsigned char)clip[i] < 127) {
                    tiku_field_key(field, TIKU_FIELD_CHAR, 0, clip[i]);
                }
            }
            return;
        } else { return; }
    } else if (ev->key == TIKU_KEY_BACKSPACE) { key = TIKU_FIELD_BACKSPACE; }
    else if (ev->key == TIKU_KEY_DELETE) { key = TIKU_FIELD_DELETE; }
    else if (ev->key == TIKU_KEY_LEFT) { key = TIKU_FIELD_LEFT; }
    else if (ev->key == TIKU_KEY_RIGHT) { key = TIKU_FIELD_RIGHT; }
    else if (ev->key == TIKU_KEY_HOME) { key = TIKU_FIELD_HOME; }
    else if (ev->key == TIKU_KEY_END) { key = TIKU_FIELD_END; }
    else if (ch < 32 || ch >= 127) { return; }
    tiku_field_key(field, key, shift, (char)ch);
}
