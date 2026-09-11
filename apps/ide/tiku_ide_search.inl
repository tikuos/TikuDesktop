/* Literal, case-sensitive search. Replacement never interprets BASIC numbers. */
static void
search_open(edit_t *e, int replacing)
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

static tiku_rect_t
search_button(int i)
{
    return (tiku_rect_t){ 6 + i * 86, SEARCH_TOP + FIND_H * 2 + 2, 80, 22 };
}

static void
search_paint(edit_t *e)
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
        tiku_ui_button(e->surface, search_button(i), labels[i], 0u);
    }
}

/** Build the result first so a line-length failure cannot partly replace it. */
static void
search_replace(edit_t *e, int all)
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
        tiku_textview_reveal(&e->tv, edit_rows(e));
    }
    free(result);
    snprintf(e->feedback, sizeof e->feedback, "%d replacement%s%s", count,
             count == 1 ? "" : "s", all ? " in this file (Undo available)" : "");
}

static int
search_pointer(edit_t *e, int x, int y)
{
    int i;

    if (!e->finding || y < SEARCH_TOP || y >= edit_page_top(e)) { return 0; }
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
        tiku_rect_t r = search_button(i);
        if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) { continue; }
        if (i == 0) { edit_find_run(e); }
        if (i == 1 || i == 2) { search_replace(e, i == 2); }
        if (i == 3) { e->finding = 0; }
    }
    return 1;
}

static void
search_key(ide_t *st, edit_t *e, const tiku_event_t *ev)
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
        if (e->find_focus) { search_replace(e, 0); }
        else { edit_find_run(e); }
        return;
    }
    if (ev->modifiers & TIKU_MOD_CMD) {
        char clip[TIKU_FIELD_MAX];
        if (ch == 'a') { key = TIKU_FIELD_SELECT_ALL; }
        else if (ch == 'v' && st->services->clip_get) {
            int i;
            clip[0] = '\0';
            st->services->clip_get(st->services->ctx, clip, sizeof clip);
            clip[sizeof clip - 1] = '\0';
            for (i = 0; clip[i]; i++) {
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
