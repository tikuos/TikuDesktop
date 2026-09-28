/* tiku_ide_search.c -- search.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static int
find_forward(const tiku_textview_t *tv, const char *q,
             int sy, int sx, int *oy, int *ox);
static void
edit_find_run(edit_t *e);

/**
 * @brief The first hit of @p q at or after (sy,sx), wrapping once.
 *
 * Single-line matches -- a find is one line's worth of text -- swept
 * from the caret to the end and then, if nothing, from the top back to
 * the caret, so the second Return after the last hit lands on the
 * first.  @return 1 and the place, 0 for no hit.
 */
static int
find_forward(const tiku_textview_t *tv, const char *q,
             int sy, int sx, int *oy, int *ox)
{
    int qlen = (int)strlen(q);
    int total = tiku_textview_lines(tv);
    int L, i;

    if (qlen == 0) {
        return 0;
    }
    for (L = sy; L < total; L++) {
        const char *t = tiku_textview_line(tv, L);
        int ll = tiku_textview_line_len(tv, L);
        int start = (L == sy) ? sx : 0;

        for (i = start; i + qlen <= ll; i++) {
            if (memcmp(t + i, q, (size_t)qlen) == 0) {
                *oy = L; *ox = i; return 1;
            }
        }
    }
    for (L = 0; L <= sy && L < total; L++) {
        const char *t = tiku_textview_line(tv, L);
        int ll = tiku_textview_line_len(tv, L);
        int limit = (L == sy) ? sx : ll;

        for (i = 0; i + qlen <= ll && i < limit; i++) {
            if (memcmp(t + i, q, (size_t)qlen) == 0) {
                *oy = L; *ox = i; return 1;
            }
        }
    }
    return 0;
}

/** @brief Select the span [ (y,x0) , (y,x1) ] and bring it into view. */
void
tiku_ide_edit_select_span(edit_t *e, int y, int x0, int x1)
{
    tiku_textview_place(&e->tv, y, x0);
    tiku_textview_select_none(&e->tv);
    tiku_textview_select_begin(&e->tv);
    tiku_textview_place(&e->tv, y, x1);
    (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
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
    int cy, cx, hy, hx;

    if (q[0] == '\0') {
        return;
    }
    if (e->lang == TIKU_SYNTAX_BASIC && !e->replacing) {
        int alldig = 1, i;

        for (i = 0; q[i] != '\0'; i++) {
            if (q[i] < '0' || q[i] > '9') { alldig = 0; break; }
        }
        if (alldig) {
            int total = tiku_textview_lines(&e->tv);
            int L, n = (int)strlen(q);

            for (L = 0; L < total; L++) {
                const char *t = tiku_textview_line(&e->tv, L);
                int d = 0;

                while (t[d] >= '0' && t[d] <= '9') {
                    d++;
                }
                if (d == n && memcmp(t, q, (size_t)n) == 0) {
                    tiku_ide_edit_select_span(e, L, 0, d);
                    return;
                }
            }
            /* no line wears it: fall through and find it as text */
        }
    }
    tiku_textview_caret(&e->tv, &cy, &cx);
    if (find_forward(&e->tv, q, cy, cx, &hy, &hx)) {
        tiku_ide_edit_select_span(e, hy, hx, hx + (int)strlen(q));
        snprintf(e->feedback, sizeof e->feedback, "Match at line %d", hy + 1);
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
    for (i = 0; i < st->proj.nrow && st->nmsg < IDE_MSGS; i++) {
        edit_t *source;
        int y;

        if (st->proj.row[i].heading) { continue; }
        source = tiku_ide_edit_of_path(st, st->proj.row[i].path);
        if (!source) {
            snprintf(temp->path, sizeof temp->path, "%s", st->proj.row[i].path);
            temp->load_failed = 0;
            tiku_ide_edit_load(temp);
            source = temp;
        }
        if (source->load_failed) { skipped++; }
        for (y = 0; !source->load_failed && y < source->tv.nline &&
                    st->nmsg < IDE_MSGS; y++) {
            const char *line = tiku_textview_line(&source->tv, y);
            msg_t *m;
            if (!strstr(line, e->find.text)) { continue; }
            m = &st->msg[st->nmsg++];
            memset(m, 0, sizeof *m);
            snprintf(m->file, sizeof m->file, "%s", source->path);
            m->line = y + 1;
            snprintf(m->text, sizeof m->text, "%.48s:%d: %.90s",
                     st->proj.row[i].name, y + 1, line);
        }
        if (source == temp) { tiku_textview_free(&temp->tv); }
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
    e->assist_open = 0;
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

/** Build the result first so a line-length failure cannot partly replace it. */
void
tiku_ide_search_replace(edit_t *e, int all)
{
    size_t qn = strlen(e->find.text), rn = strlen(e->replacement.text);
    size_t cap = TIKU_TEXTVIEW_LINES_MAX * TIKU_TEXTVIEW_LINE_MAX;
    char *result;
    size_t used = 0;
    int y, count = 0, y0 = -1, x0 = 0, y1, x1;

    e->replacing = 1;
    if (!qn) { return; }
    if (!all && (!tiku_textview_selection(&e->tv, &y0, &x0, &y1, &x1) ||
        y0 != y1 || x1 - x0 != (int)qn ||
        memcmp(tiku_textview_line(&e->tv, y0) + x0, e->find.text, qn))) {
        edit_find_run(e); /* First select a match; never replace unrelated text. */
        return;
    }
    result = malloc(cap + 1);
    if (!result) { return; }
    for (y = 0; y < tiku_textview_lines(&e->tv); y++) {
        const char *line = tiku_textview_line(&e->tv, y);
        size_t x = 0, start = used;

        if (y) { result[used++] = '\n'; start++; }
        while (line[x]) {
            int match = (all || (y == y0 && x == (size_t)x0)) &&
                        strncmp(line + x, e->find.text, qn) == 0;
            size_t n = match ? rn : 1;

            if (used - start + n >= TIKU_TEXTVIEW_LINE_MAX) {
                snprintf(e->feedback, sizeof e->feedback,
                         "Not replaced: a result line would exceed 1023 bytes");
                free(result);
                return;
            }
            memcpy(result + used, match ? e->replacement.text : line + x, n);
            used += n;
            x += match ? qn : 1;
            count += match;
        }
    }
    result[used] = '\0';
    if (count && strcmp(e->find.text, e->replacement.text)) {
        tiku_textview_group_begin(&e->tv);
        tiku_textview_select_all(&e->tv);
        tiku_textview_insert_text(&e->tv, result);
        tiku_textview_group_end(&e->tv);
        if (!all) { tiku_textview_place(&e->tv, y0, x0 + (int)rn); }
        tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
    }
    free(result);
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
        if (ch == 'a') { key = TIKU_FIELD_SELECT_ALL; }
        else if (ch == 'v' && st->services->clip_text) {
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
