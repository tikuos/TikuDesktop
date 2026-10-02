/* tiku_ide_text.c -- a page's text: the editor kit's document, the view
 * that shows it, and its lines read out.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

#include <limits.h>

/* The fill of a line holding a breakpoint, and of the line a paused
 * debug run stopped before. */
#define BREAK_FILL TIKU_RGB(255, 222, 222)
#define EXEC_FILL  TIKU_RGB(211, 238, 185)

/** @brief Put the selection on the desk's clipboard, each CRLF and CR
 *         made LF.  @return 0 when the clipboard took it. */
static int
clip_set(void *ctx, const char *text, size_t length)
{
    edit_t *e = ctx;
    size_t out = 0, i;
    int taken;
    char *lf;

    if (e->services == NULL || e->services->clip_set == NULL ||
        length > TIKU_CLIPBOARD_LIMIT || (lf = malloc(length + 1)) == NULL) {
        return -1;
    }
    for (i = 0; i < length; i++) {
        if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n') {
            continue;
        }
        lf[out++] = text[i] == '\r' ? '\n' : text[i];
    }
    lf[out] = '\0';
    taken = e->services->clip_set(e->services->ctx, lf) >= 0;
    free(lf);
    return taken ? 0 : -1;
}

/** @brief The desk clipboard's text with each CRLF, CR or LF made the
 *         page's line break; NULL when it holds nothing or cannot be
 *         copied. */
static const char *
clip_text(void *ctx, size_t *length)
{
    edit_t *e = ctx;
    const char *held = NULL;
    size_t breaks = 0, n, wide = strlen(e->newline), out = 0, i;

    if (e->services != NULL && e->services->clip_text != NULL) {
        held = e->services->clip_text(e->services->ctx, NULL);
    }
    free(e->paste);
    e->paste = NULL;
    if (held == NULL || held[0] == '\0') {
        return NULL;
    }
    n = strlen(held);
    for (i = 0; i < n; i++) {
        breaks += held[i] == '\n' || held[i] == '\r';
    }
    if (breaks > (SIZE_MAX - n - 1) / 2 ||
        (e->paste = malloc(n + breaks * wide + 1)) == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (held[i] != '\r' && held[i] != '\n') {
            e->paste[out++] = held[i];
            continue;
        }
        i += held[i] == '\r' && held[i + 1] == '\n';
        memcpy(e->paste + out, e->newline, wide);
        out += wide;
    }
    e->paste[out] = '\0';
    *length = out;
    return e->paste;
}

/**
 * @brief Give the page an empty document and a view of it, in the
 *        proportional face, coloured by the page's language.
 *
 * The view holds the document; the page's pointer to it lives as long as
 * the view does.  @return 1, or 0 with neither made.
 */
int
tiku_ide_text_new(edit_t *e)
{
    tiku_editdoc_limits_t limits = { INT_MAX - 1u,
                                     TIKU_EDITDOC_HISTORY_DEFAULT };
    tiku_codeview_host_t host = { e, clip_set, clip_text, NULL };
    tiku_editdoc_t *doc = NULL;

    if (e->view != NULL) {
        return 1;
    }
    if (tiku_editdoc_create(&limits, &doc) != TIKU_EDIT_OK) {
        return 0;
    }
    if (tiku_codeview_create(doc, &e->view) != TIKU_EDIT_OK) {
        e->view = NULL;
        tiku_editdoc_release(doc);
        return 0;
    }
    tiku_editdoc_release(doc);
    e->doc = doc;
    tiku_codeview_set_host(e->view, &host);
    tiku_codeview_set_font(e->view, tiku_font_plain());
    tiku_codeview_set_language(e->view, e->lang);
    (void)tiku_codeview_set_marker_fill(e->view, IDE_MARK_BREAK, BREAK_FILL);
    tiku_codeview_set_execution_fill(e->view, EXEC_FILL);
    snprintf(e->newline, sizeof e->newline, "\n");
    return 1;
}

/** @brief Let the page's view and document go, and the paste copy. */
void
tiku_ide_text_free(edit_t *e)
{
    tiku_codeview_destroy(e->view);
    e->view = NULL;
    e->doc = NULL;
    free(e->paste);
    e->paste = NULL;
}

/**
 * @brief Replace the page's text with @p n bytes of @p text, history
 *        cleared and the result counted as saved.
 *
 * Return types the line break @p text uses most, the one used first when
 * two are as common, and LF when it has none.
 */
tiku_edit_status_t
tiku_ide_text_load(edit_t *e, const char *text, size_t n)
{
    static const char *const kinds[] = { "\n", "\r\n", "\r" };
    size_t count[3] = { 0, 0, 0 }, first[3] = { SIZE_MAX, SIZE_MAX, SIZE_MAX };
    tiku_edit_status_t st;
    int best = 0, k;
    size_t i;

    if (!tiku_ide_text_new(e)) {
        return TIKU_EDIT_NO_MEMORY;
    }
    st = tiku_editdoc_load(e->doc, text, n);
    if (st != TIKU_EDIT_OK) {
        return st;
    }
    for (i = 0; i < n; i++) {
        int kind;

        if (text[i] == '\n') {
            kind = 0;
        } else if (text[i] != '\r') {
            continue;
        } else {
            kind = i + 1 < n && text[i + 1] == '\n' ? 1 : 2;
        }
        if (count[kind]++ == 0) {
            first[kind] = i;
        }
        i += kind == 1;
    }
    for (k = 1; k < 3; k++) {
        if (count[k] > count[best] ||
            (count[k] == count[best] && first[k] < first[best])) {
            best = k;
        }
    }
    snprintf(e->newline, sizeof e->newline, "%s", kinds[best]);
    (void)tiku_codeview_set_newline(e->view, e->newline);
    return TIKU_EDIT_OK;
}

/**
 * @brief A copy of @p line's text, its line break left out, NUL-closed.
 *
 * @return the copy, which the caller frees, with its length in
 *         @p length (may be NULL); NULL for a line that is not there or
 *         no memory.
 */
char *
tiku_ide_line_dup(const edit_t *e, int64_t line, size_t *length)
{
    tiku_edit_range_t r;
    size_t n = 0;
    char *copy;

    if (e->doc == NULL) {
        return NULL;
    }
    r.start = tiku_editdoc_line_start(e->doc, line);
    r.end = tiku_editdoc_line_end(e->doc, line);
    if (r.start < 0 || r.end < r.start ||
        (copy = malloc((size_t)(r.end - r.start) + 1)) == NULL) {
        return NULL;
    }
    if (tiku_editdoc_read(e->doc, r, copy, (size_t)(r.end - r.start) + 1,
                          &n) != TIKU_EDIT_OK) {
        free(copy);
        return NULL;
    }
    if (length != NULL) {
        *length = n;
    }
    return copy;
}

/**
 * @brief The BASIC number @p line starts with, read as strtol() reads
 *        it: blanks first when @p blanks, then digits.
 *
 * @return the number, or 0 when the line starts with none.
 */
long
tiku_ide_line_number(const edit_t *e, int64_t line, int blanks)
{
    char head[64], *end;
    const char *p = head;
    tiku_edit_range_t r;
    size_t n = 0;
    long number;

    if (e->doc == NULL) {
        return 0;
    }
    r.start = tiku_editdoc_line_start(e->doc, line);
    r.end = tiku_editdoc_line_end(e->doc, line);
    if (r.start < 0 || r.end < r.start) {
        return 0;
    }
    if (blanks) {
        int b;

        while (r.start < r.end &&
               ((b = tiku_editdoc_byte_at(e->doc, r.start)) == ' ' ||
                b == '\t')) {
            r.start++;
        }
    }
    if (r.end - r.start > (int64_t)sizeof head - 1) {
        r.end = r.start + (int64_t)sizeof head - 1;
    }
    if (tiku_editdoc_read(e->doc, r, head, sizeof head, &n) !=
            TIKU_EDIT_OK || !(head[0] >= '0' && head[0] <= '9')) {
        return 0;
    }
    number = strtol(p, &end, 10);
    return end != p ? number : 0;
}

/** @brief The caret's position, and its line and byte column in
 *         @p line and @p column, either of which may be NULL. */
tiku_edit_pos_t
tiku_ide_caret(const edit_t *e, int64_t *line, int64_t *column)
{
    tiku_edit_pos_t anchor = 0, caret = 0;
    int64_t at;

    (void)tiku_codeview_selection(e->view, &anchor, &caret);
    at = tiku_editdoc_line_of(e->doc, caret);
    if (line != NULL) {
        *line = at;
    }
    if (column != NULL) {
        *column = caret - tiku_editdoc_line_start(e->doc, at);
    }
    return caret;
}

/**
 * @brief Read the caret's line from its start to 63 bytes past the caret,
 *        or to the line's end, into @p text, NUL-closed.
 *
 * @p text holds IDE_CARET_TEXT bytes.  @return the caret's byte column,
 * with the line's start in @p start and the bytes read in @p got; -1 when
 * the caret is further into its line than the buffer reaches.
 */
int
tiku_ide_caret_text(const edit_t *e, char *text, tiku_edit_pos_t *start,
                    size_t *got)
{
    tiku_edit_range_t r;
    int64_t line = 0;
    tiku_edit_pos_t caret = tiku_ide_caret(e, &line, NULL);

    r.start = tiku_editdoc_line_start(e->doc, line);
    r.end = tiku_editdoc_line_end(e->doc, line);
    if (caret - r.start > IDE_CARET_TEXT - 64) {
        return -1;
    }
    if (r.end > caret + 63) {
        r.end = caret + 63;
    }
    if (tiku_editdoc_read(e->doc, r, text, IDE_CARET_TEXT, got) !=
        TIKU_EDIT_OK) {
        return -1;
    }
    *start = r.start;
    return (int)(caret - r.start);
}

/** @brief Put an empty caret at byte @p column of @p line, each clamped
 *         into the text and moved back off a character's middle or the
 *         middle of a CRLF. */
void
tiku_ide_place(edit_t *e, int64_t line, int64_t column)
{
    int64_t lines = tiku_editdoc_lines(e->doc);
    tiku_edit_pos_t start, end, at;

    if (line >= lines) {
        line = lines - 1;
    }
    if (line < 0) {
        line = 0;
    }
    start = tiku_editdoc_line_start(e->doc, line);
    end = tiku_editdoc_line_end(e->doc, line);
    at = column < 0 ? start : column > end - start ? end : start + column;
    while (at > start && !tiku_editdoc_is_caret(e->doc, at)) {
        at--;
    }
    (void)tiku_codeview_select(e->view, at, at);
}

/**
 * @brief Replace [@p start, @p end) with @p n bytes of @p text as one undo
 *        step, the caret after them.
 *
 * With @p typing the step may join the typing before it.
 */
tiku_edit_status_t
tiku_ide_replace(edit_t *e, tiku_edit_pos_t start, tiku_edit_pos_t end,
                 const char *text, size_t n, int typing)
{
    tiku_edit_replace_t edit;
    tiku_edit_transaction_t t;

    edit.range.start = start;
    edit.range.end = end;
    edit.text = n > 0 ? text : NULL;
    edit.length = n;
    memset(&t, 0, sizeof t);
    t.edits = &edit;
    t.count = 1;
    t.view = tiku_codeview_doc_view(e->view);
    t.select = 1;
    t.anchor = t.caret = start + (tiku_edit_pos_t)n;
    t.flags = typing ? TIKU_EDIT_TYPING : 0u;
    return tiku_editdoc_apply(e->doc, &t);
}
