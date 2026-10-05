/* tiku_ide_problems.c -- problems a run or build names, on the pages.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#include "tiku_ide_private.h"

#include <string.h>

/* The kinds a page draws: an error, a warning and a note. */
#define PROBLEM_ERROR   0u
#define PROBLEM_WARNING 1u
#define PROBLEM_NOTE    2u
#define PROBLEM_KINDS   0x7u

/** @brief The words of @p m without the place a compiler puts before
 *         them: "file:line:col: error: ..." gives "error: ...". */
static const char *
words_of(const msg_t *m)
{
    const char *p = m->text;
    size_t n = strlen(m->file);
    int i;

    if (m->numbered || n == 0 || strncmp(p, m->file, n) != 0 || p[n] != ':')
        return p;
    p += n + 1;
    for (i = 0; i < 2 && *p >= '0' && *p <= '9'; i++) {
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p == ':')
            p++;
    }
    while (*p == ' ')
        p++;
    return *p ? p : m->text;
}

/** @brief The kind of problem @p words are: a warning or a note when
 *         they say so first, else an error. */
static unsigned
kind_of(const char *words)
{
    if (strncmp(words, "warning", 7) == 0)
        return PROBLEM_WARNING;
    if (strncmp(words, "note", 4) == 0)
        return PROBLEM_NOTE;
    return PROBLEM_ERROR;
}

/** @brief Whether byte @p b is part of a name. */
static int
name_byte(int b)
{
    return b == '_' || (b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') ||
           (b >= 'A' && b <= 'Z');
}

/** @brief What to underline on @p line: the name at byte column @p col,
 *         or the character there; with no column, the line's text past
 *         its indentation.  Empty when the line has nothing to mark. */
static tiku_edit_range_t
range_of(const edit_t *e, int64_t line, int col)
{
    tiku_edit_range_t r;
    tiku_edit_pos_t end = tiku_editdoc_line_end(e->doc, line);

    r.start = tiku_editdoc_line_start(e->doc, line);
    if (col <= 0) {
        while (r.start < end && (tiku_editdoc_byte_at(e->doc, r.start) == ' ' ||
                                 tiku_editdoc_byte_at(e->doc, r.start) == '\t'))
            r.start++;
        r.end = end;
        return r;
    }
    if (r.start + col - 1 < end)
        r.start += col - 1;
    else if (end > r.start)
        r.start = tiku_editdoc_prev(e->doc, end);
    r.end = r.start;
    while (r.end < end && name_byte(tiku_editdoc_byte_at(e->doc, r.end)))
        r.end++;
    if (r.end == r.start && r.start < end)
        r.end = tiku_editdoc_next(e->doc, r.start);
    return r;
}

/** @brief Put each message about page @p e onto it. */
static void
apply(ide_t *st, edit_t *e)
{
    int i;

    for (i = 0; i < st->nmsg; i++) {
        const msg_t *m = &st->msg[i];
        const char *words;
        tiku_edit_range_t r;
        int64_t line;

        if (m->line <= 0 || m->file[0] == '\0' ||
            tiku_ide_edit_of_path(st, m->file) != e)
            continue;
        line = m->numbered ? tiku_ide_line_of_number(e, m->line)
                           : m->line - 1;
        if (line < 0 || line >= tiku_editdoc_lines(e->doc))
            continue;
        words = words_of(m);
        r = range_of(e, line, m->numbered ? 0 : m->col);
        if (r.end > r.start)
            (void)tiku_editdoc_indicator_add(e->doc, r, kind_of(words), NULL);
        (void)tiku_editdoc_annotation_add(e->doc, line, words,
                                          kind_of(words));
    }
}

/** @brief Set how page @p e marks problems: errors and warnings with a
 *         wavy underline, notes with a straight one. */
void
tiku_ide_problems_style(edit_t *e)
{
    (void)tiku_codeview_set_mark(e->view, PROBLEM_ERROR,
                                 TIKU_CODEVIEW_MARK_SQUIGGLE,
                                 tiku_ink(TIKU_INK_WRONG));
    (void)tiku_codeview_set_mark(e->view, PROBLEM_WARNING,
                                 TIKU_CODEVIEW_MARK_SQUIGGLE,
                                 TIKU_RGB(214, 140, 0));
    (void)tiku_codeview_set_mark(e->view, PROBLEM_NOTE,
                                 TIKU_CODEVIEW_MARK_LINE,
                                 TIKU_RGB(40, 110, 200));
}

/** @brief Mark the last run's or build's problems on page @p e.
 *  @note The page must carry no problem marks: marks are added, not
 *        replaced; tiku_ide_problems_show() clears them first. */
void
tiku_ide_problems_page(ide_t *st, edit_t *e)
{
    if (st->msg_problems && e->doc != NULL)
        apply(st, e);
}

/** @brief Mark the last run's or build's problems on every open page,
 *         replacing the marks there.  Search hits are not marked. */
void
tiku_ide_problems_show(ide_t *st)
{
    int i;

    if (!st->msg_problems)
        return;
    for (i = 0; i < IDE_EDITS; i++) {
        edit_t *e = &st->edit[i];

        if (e->surface == NULL || e->doc == NULL)
            continue;
        (void)tiku_editdoc_indicators_clear(e->doc, PROBLEM_KINDS);
        (void)tiku_editdoc_annotations_clear(e->doc, -1, PROBLEM_KINDS);
        apply(st, e);
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
    }
}

/** @brief Whether page @p e shows a problem to go to. */
int
tiku_ide_problems_any(const edit_t *e)
{
    return e->doc != NULL &&
           tiku_editdoc_indicator_next(e->doc, -1, PROBLEM_KINDS, 0, NULL,
                                       NULL) == TIKU_EDIT_OK;
}

/** @brief Select the next problem on page @p e, or with @p backward the
 *         one before, round the page.  @return 0. */
int
tiku_ide_problems_go(ide_t *st, edit_t *e, int backward)
{
    if (tiku_codeview_goto_indicator(e->view, PROBLEM_KINDS, backward) ==
        TIKU_EDIT_OK) {
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
    }
    return 0;
}
