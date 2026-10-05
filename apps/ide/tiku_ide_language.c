/* tiku_ide_language.c -- BASIC keyword normalization and line renumbering.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/** @brief One number Renumber rewrites: where it is, and its new
 *         digits. */
typedef struct {
    int64_t start, end;         /* the digits in the document           */
    char text[24];              /* what replaces them                   */
} renum_edit_t;

/** @brief Renumber's map and the edits it collects. */
typedef struct {
    long *olds, *news;          /* old number -> new, in line order     */
    int n;                      /* entries in olds and news             */
    renum_edit_t *edits;        /* the edits collected                  */
    size_t count, cap;          /* edits used, and room for             */
    int failed;                 /* an edit could not be kept            */
} renum_t;

static int renum_is_word(int c);
static int renum_keyword(const char *s, int *list);
static int renum_raw(const char *s);
static long renum_map(const renum_t *r, long old);
static void renum_number(renum_t *r, int64_t at, const char *digits,
                         size_t n);
static void renum_scan(renum_t *r, const char *s, int64_t at);

/** @brief Whether @p c can be part of a word keyword capitals apply to: a
 *         letter, a digit or "$". */
int
tiku_ide_norm_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '$';
}

/**
 * @brief The run of @p text that holds byte @p at, when it is a reserved
 *        word: @return its length, with its start in @p from; 0 when it
 *        is not.
 *
 * The syntax classifier does not mark a keyword inside a string or a
 * remark as reserved.
 */
static int
norm_reserved(const char *text, int at, int *from)
{
    tiku_span_t span[256];
    int i, n, off = 0;

    n = tiku_syntax_spans(TIKU_SYNTAX_BASIC, text, span,
                          (int)(sizeof span / sizeof span[0]));
    for (i = 0; i < n; i++) {
        if (at >= off && at < off + span[i].len) {
            *from = off;
            return span[i].ink == TIKU_INK_WORD ? span[i].len : 0;
        }
        off += span[i].len;
    }
    return 0;
}

/**
 * @brief Put the reserved word the caret just finished in capitals.
 *
 * The classifier that colours the page decides what is reserved, so a
 * keyword in a string or a remark is left alone.  A held selection goes
 * first, as the key that follows would take it, and the word is the one
 * before where it began.  The rewrite is one undo step, so a single Undo
 * puts back what was typed.  BASIC only.
 */
void
tiku_ide_edit_normalize_word(edit_t *e)
{
    tiku_edit_pos_t anchor, caret, start = 0;
    char text[IDE_CARET_TEXT], up[64];
    size_t got = 0;
    int i, cx, span_s = 0, span_len;

    if (e->lang != TIKU_SYNTAX_BASIC ||
        tiku_codeview_selection(e->view, &anchor, &caret) != TIKU_EDIT_OK) {
        return;
    }
    if (anchor != caret) {
        (void)tiku_codeview_delete(e->view, 0);
        if (tiku_codeview_selection(e->view, &anchor, &caret) !=
                TIKU_EDIT_OK || anchor != caret) {
            return;             /* it could not go, so nothing is typed */
        }
    }
    /* The line up to the end of the word the caret is in: what lies
     * further right cannot change how that word is read. */
    cx = tiku_ide_caret_text(e, text, &start, &got);
    if (cx <= 0 || !tiku_ide_norm_is_word((unsigned char)text[cx - 1])) {
        return;                 /* no word just ended here */
    }
    for (i = cx; i < (int)got &&
                 tiku_ide_norm_is_word((unsigned char)text[i]); i++) {
    }
    text[i] = '\0';
    span_len = norm_reserved(text, cx - 1, &span_s);
    if (span_len <= 0 || span_len >= (int)sizeof up) {
        return;
    }
    for (i = 0; i < span_len; i++) {
        char c = text[span_s + i];

        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    if (memcmp(up, text + span_s, (size_t)span_len) == 0) {
        return;                 /* already in capitals */
    }
    {
        /* One step: the word in upper case over itself, the caret where
         * it stood, since case does not change a word's length. */
        tiku_edit_replace_t edit = { { start + span_s,
                                       start + span_s + span_len },
                                     up, (size_t)span_len };
        tiku_edit_transaction_t t;

        memset(&t, 0, sizeof t);
        t.edits = &edit;
        t.count = 1;
        t.view = tiku_codeview_doc_view(e->view);
        t.select = 1;
        t.anchor = t.caret = caret;
        (void)tiku_editdoc_apply(e->doc, &t);
    }
}

/** @brief Whether @p c can be part of a BASIC word when renumbering: a
 *         letter, a digit or "_". */
static int
renum_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/** @brief The length of a branch keyword starting here, or 0; @p list is
 *         set when a comma may bring further targets, as after GOTO. */
static int
renum_keyword(const char *s, int *list)
{
    static const char *const K[] = { "goto", "gosub", "then", "else" };
    size_t i;

    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        int n = (int)strlen(K[i]);

        if (strncasecmp(s, K[i], (size_t)n) == 0 && !renum_is_word(s[n])) {
            *list = i < 2;
            return n;
        }
    }
    return 0;
}

/** @brief Whether REM or DATA starts here, after which the interpreter
 *         keeps the rest of the line as raw text. */
static int
renum_raw(const char *s)
{
    return (strncasecmp(s, "rem", 3) == 0 && !renum_is_word(s[3])) ||
           (strncasecmp(s, "data", 4) == 0 && !renum_is_word(s[4]));
}

/** @brief The new number an old one maps to, or the old if none; when
 *         several lines have the number, the first of them gives it. */
static long
renum_map(const renum_t *r, long old)
{
    int i;

    for (i = 0; i < r->n; i++) {
        if (r->olds[i] == old) {
            return r->news[i];
        }
    }
    return old;
}

/** @brief Keep an edit of the @p n digits at @p at to the number they map
 *         to, when its digits differ: a mapped number, or an unmapped one
 *         losing its leading zeros.  A number too long to read is left. */
static void
renum_number(renum_t *r, int64_t at, const char *digits, size_t n)
{
    renum_edit_t *edit;
    long old = 0;
    size_t i;
    int len;
    char nb[24];

    if (n > 9) {
        return;
    }
    for (i = 0; i < n; i++) {
        old = old * 10 + (digits[i] - '0');
    }
    len = snprintf(nb, sizeof nb, "%ld", renum_map(r, old));
    if ((size_t)len == n && memcmp(nb, digits, n) == 0) {
        return;
    }
    if (r->count == r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 64;
        renum_edit_t *grown = realloc(r->edits, cap * sizeof *grown);

        if (grown == NULL) {
            r->failed = 1;
            return;
        }
        r->edits = grown;
        r->cap = cap;
    }
    edit = &r->edits[r->count++];
    edit->start = at;
    edit->end = at + (int64_t)n;
    memcpy(edit->text, nb, (size_t)len + 1);
}

/**
 * @brief Queue edits for the line numbers the branches in @p s name, @p s
 *        starting at document position @p at.
 *
 * Every number in the comma list after GOTO or GOSUB (`ON A GOTO 10, 20`)
 * counts, and the one after THEN or ELSE.  Quoted text, and the rest of a
 * line after REM, an apostrophe or DATA, are skipped: the interpreter
 * runs none of it.
 */
static void
renum_scan(renum_t *r, const char *s, int64_t at)
{
    const char *base = s;
    int prev_word = 0;

    while (*s != '\0') {
        int kw, list = 0;

        if (*s == '"') {
            const char *end = s + 1;

            while (*end != '\0' && *end != '"') {
                end += (*end == '\\' && end[1] != '\0') ? 2 : 1;
            }
            s = end + (*end == '"');
            prev_word = 0;
            continue;
        }
        if (*s == '\'' || (!prev_word && renum_raw(s))) {
            return;
        }
        kw = prev_word ? 0 : renum_keyword(s, &list);
        if (kw > 0) {
            s += kw;
            for (;;) {
                const char *comma, *digits;

                while (*s == ' ' || *s == '\t') {
                    s++;
                }
                if (*s < '0' || *s > '9') {
                    break;
                }
                digits = s;
                while (*s >= '0' && *s <= '9') {
                    s++;
                }
                renum_number(r, at + (digits - base), digits,
                             (size_t)(s - digits));
                for (comma = s; *comma == ' ' || *comma == '\t'; comma++) {
                }
                if (!list || *comma != ',') {
                    break;
                }
                s = comma + 1;
            }
            prev_word = renum_is_word((unsigned char)s[-1]);
            continue;
        }
        prev_word = renum_is_word((unsigned char)*s);
        s++;
    }
}

/**
 * @brief Renumber a BASIC program by tens, branches following.
 *
 * Every line that starts with a digit gets the next multiple of ten, and
 * every branch to an old number is rewritten to the new one; a number
 * several lines share maps to the first of them.  Only digits that change
 * are replaced, in one undo step, so the caret, the selection and the
 * breakpoints stay on their lines; a program numbered 10, 20, 30 and on
 * changes nothing.
 */
void
tiku_ide_renumber_basic(edit_t *e)
{
    int64_t total = tiku_editdoc_lines(e->doc), line;
    renum_t r;
    long next = 10;

    memset(&r, 0, sizeof r);
    r.olds = malloc((size_t)total * sizeof *r.olds);
    r.news = malloc((size_t)total * sizeof *r.news);
    if (r.olds == NULL || r.news == NULL) {
        free(r.olds);
        free(r.news);
        return;
    }
    for (line = 0; line < total; line++) {
        tiku_edit_pos_t at = tiku_editdoc_line_start(e->doc, line);
        int b = tiku_editdoc_byte_at(e->doc, at);

        if (b >= '0' && b <= '9' &&
            at < tiku_editdoc_line_end(e->doc, line)) {
            r.olds[r.n] = tiku_ide_line_number(e, line, 0);
            r.news[r.n++] = next;
            next += 10;
        }
    }
    for (line = 0; line < total && !r.failed; line++) {
        size_t len = 0, d = 0;
        char *text = tiku_ide_line_dup(e, line, &len);
        int64_t at = tiku_editdoc_line_start(e->doc, line);

        if (text == NULL) {
            r.failed = 1;
            break;
        }
        while (d < len && text[d] >= '0' && text[d] <= '9') {
            d++;
        }
        if (d > 0) {
            renum_number(&r, at, text, d);
        }
        renum_scan(&r, text + d, at + (int64_t)d);
        free(text);
    }
    if (!r.failed && r.count > 0) {
        tiku_edit_replace_t *edits = malloc(r.count * sizeof *edits);
        tiku_edit_transaction_t t;
        size_t i;

        if (edits != NULL) {
            for (i = 0; i < r.count; i++) {
                edits[i].range.start = r.edits[i].start;
                edits[i].range.end = r.edits[i].end;
                edits[i].text = r.edits[i].text;
                edits[i].length = strlen(r.edits[i].text);
            }
            memset(&t, 0, sizeof t);
            t.edits = edits;
            t.count = r.count;
            t.view = tiku_codeview_doc_view(e->view);
            if (tiku_editdoc_apply(e->doc, &t) == TIKU_EDIT_OK) {
                tiku_ide_edit_reveal(e);
            }
            free(edits);
        }
    }
    free(r.edits);
    free(r.olds);
    free(r.news);
}
