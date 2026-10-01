/* tiku_ide_language.c -- BASIC keyword normalization and line renumbering.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/** @brief Renumber's output: only counted while @c p is NULL. */
typedef struct {
    char *p;                    /* the text, or NULL while counting     */
    size_t n;                   /* bytes produced                       */
    size_t cap;                 /* bytes p can hold                     */
} renum_out_t;

static int
renum_is_word(int c);
static int
renum_keyword(const char *s, int *list);
static int
renum_raw(const char *s);
static int
renum_map(int old, const int *olds, const int *news, int n);
static void
renum_put(renum_out_t *o, const char *s, size_t len);
static void
renum_emit(renum_out_t *o, const char *s,
           const int *olds, const int *news, int n);
static size_t
renum_lines(edit_t *e, renum_out_t *o, const int *olds, const int *news,
            int n);

int
tiku_ide_norm_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '$';
}

/**
 * @brief Uppercase the reserved word the caret just finished, 1982-style.
 *
 * The classifier that colours the page decides what is reserved, so a
 * keyword in a string or a remark is left alone.  A held selection goes
 * first, as the key that follows would take it, and the word is the one
 * before where it began.  The rewrite is one journal batch, so a single
 * Undo puts back what was typed.  BASIC only.
 */
void
tiku_ide_edit_normalize_word(edit_t *e)
{
    int cy, cx, s, i, off, ink = -1, span_s = -1, span_len = 0, n;
    const char *line;
    tiku_span_t span[64];
    char up[64];

    if (e->lang != TIKU_SYNTAX_BASIC) {
        return;
    }
    if (tiku_textview_selection(&e->tv, NULL, NULL, NULL, NULL)) {
        tiku_textview_backspace(&e->tv);
        if (tiku_textview_selection(&e->tv, NULL, NULL, NULL, NULL)) {
            return;             /* it could not go, so nothing is typed */
        }
    }
    tiku_textview_caret(&e->tv, &cy, &cx);
    line = tiku_textview_line(&e->tv, cy);
    if (line == NULL || cx <= 0 || !tiku_ide_norm_is_word((unsigned char)line[cx - 1])) {
        return;                 /* no word just ended here */
    }
    s = cx;
    while (s > 0 && tiku_ide_norm_is_word((unsigned char)line[s - 1])) {
        s--;
    }
    /* Which run the finished word falls in: only a RESERVED word is
     * touched, and the classifier already knows a keyword in a string
     * from one in the code. */
    n = tiku_syntax_spans(TIKU_SYNTAX_BASIC, line, span,
                          (int)(sizeof span / sizeof span[0]));
    for (i = 0, off = 0; i < n; i++) {
        if (cx - 1 >= off && cx - 1 < off + span[i].len) {
            ink = span[i].ink;
            span_s = off;
            span_len = span[i].len;
            break;
        }
        off += span[i].len;
    }
    if (ink != TIKU_INK_WORD || span_len <= 0 ||
        span_len >= (int)sizeof up) {
        return;
    }
    for (i = 0; i < span_len; i++) {
        char c = line[span_s + i];

        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[span_len] = '\0';
    if (memcmp(up, line + span_s, (size_t)span_len) == 0) {
        return;                 /* already shouting: nothing to do */
    }
    /* One batch: the word out, the word back in upper -- a single Undo
     * away from exactly what was typed. */
    tiku_textview_group_begin(&e->tv);
    tiku_textview_place(&e->tv, cy, span_s + span_len);
    for (i = 0; i < span_len; i++) {
        tiku_textview_backspace(&e->tv);
    }
    tiku_textview_insert_text(&e->tv, up);
    tiku_textview_group_end(&e->tv);
    tiku_textview_place(&e->tv, cy, cx);   /* case is length: caret stands */
}

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

/** @brief The new number an old one maps to, or the old if none. */
static int
renum_map(int old, const int *olds, const int *news, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (olds[i] == old) {
            return news[i];
        }
    }
    return old;
}

/** @brief Add @p len bytes of @p s to @p o; bytes past its room are
 *         counted but never written. */
static void
renum_put(renum_out_t *o, const char *s, size_t len)
{
    if (o->p != NULL && o->n <= o->cap && len <= o->cap - o->n) {
        memcpy(o->p + o->n, s, len);
    }
    o->n += len;
}

/**
 * @brief Copy @p s, rewriting the line numbers its branches name.
 *
 * A jump has to follow the line it names when the numbers move, or
 * renumbering would quietly break every branch.  Every number in the
 * comma list after GOTO or GOSUB (`ON A GOTO 10, 20`) is rewritten, and
 * the one after THEN or ELSE.  Quoted text, and the rest of a line after
 * REM, an apostrophe or DATA, are copied unchanged: the interpreter runs
 * none of it.
 */
static void
renum_emit(renum_out_t *o, const char *s,
           const int *olds, const int *news, int n)
{
    int prev_word = 0;

    while (*s != '\0') {
        int kw, list = 0;

        if (*s == '"') {
            const char *end = s + 1;

            while (*end != '\0' && *end != '"') {
                end += (*end == '\\' && end[1] != '\0') ? 2 : 1;
            }
            end += (*end == '"');
            renum_put(o, s, (size_t)(end - s));
            s = end;
            prev_word = 0;
            continue;
        }
        if (*s == '\'' || (!prev_word && renum_raw(s))) {
            renum_put(o, s, strlen(s));
            return;
        }
        kw = prev_word ? 0 : renum_keyword(s, &list);
        if (kw > 0) {
            renum_put(o, s, (size_t)kw);
            s += kw;
            for (;;) {
                const char *comma;
                int old = 0, nl;
                char nb[16];

                while (*s == ' ' || *s == '\t') {
                    renum_put(o, s++, 1);
                }
                if (*s < '0' || *s > '9') {
                    break;
                }
                while (*s >= '0' && *s <= '9') {
                    old = old * 10 + (*s - '0');
                    s++;
                }
                nl = snprintf(nb, sizeof nb, "%d",
                              renum_map(old, olds, news, n));
                renum_put(o, nb, (size_t)nl);
                for (comma = s; *comma == ' ' || *comma == '\t'; comma++) {
                }
                if (!list || *comma != ',') {
                    break;
                }
                renum_put(o, s, (size_t)(comma - s) + 1u);
                s = comma + 1;
            }
            prev_word = renum_is_word((unsigned char)s[-1]);
            continue;
        }
        prev_word = renum_is_word((unsigned char)*s);
        renum_put(o, s++, 1);
    }
}

/** @brief Produce the renumbered text of @p e's lines into @p o.
 *  @return The length of the longest line produced. */
static size_t
renum_lines(edit_t *e, renum_out_t *o, const int *olds, const int *news,
            int n)
{
    int total = tiku_textview_lines(&e->tv), L;
    size_t longest = 0;

    for (L = 0; L < total; L++) {
        const char *t = tiku_textview_line(&e->tv, L);
        size_t start = o->n;
        int d = 0;

        if (t == NULL) {
            t = "";
        }
        while (t[d] >= '0' && t[d] <= '9') {
            d++;
        }
        if (d > 0) {
            char nb[16];
            int nl = snprintf(nb, sizeof nb, "%d",
                              renum_map(atoi(t), olds, news, n));

            renum_put(o, nb, (size_t)nl);
        }
        renum_emit(o, t + d, olds, news, n);
        if (o->n - start > longest) {
            longest = o->n - start;
        }
        if (L < total - 1) {
            renum_put(o, "\n", 1);
        }
    }
    return longest;
}

/**
 * @brief Renumber a BASIC program by tens, branches following.
 *
 * Every line that starts with a number is given the next ten, and every
 * branch to an old number is rewritten to the new one.  The rewrite is
 * one journal batch, so a single Undo takes it all back.  A result line
 * that would pass 1023 bytes refuses the whole rewrite, as Replace does.
 */
void
tiku_ide_renumber_basic(edit_t *e)
{
    int total = tiku_textview_lines(&e->tv);
    int *olds, *news;
    int nmap = 0, next = 10, L;
    renum_out_t o = { NULL, 0, 0 };
    size_t size;

    if (total <= 0) {
        return;
    }
    olds = malloc((size_t)total * sizeof *olds);
    news = malloc((size_t)total * sizeof *news);
    if (olds == NULL || news == NULL) {
        free(olds); free(news);
        return;
    }
    for (L = 0; L < total; L++) {
        const char *t = tiku_textview_line(&e->tv, L);

        if (t != NULL && t[0] >= '0' && t[0] <= '9') {
            olds[nmap] = atoi(t);
            news[nmap] = next;
            nmap++;
            next += 10;
        }
    }
    /* Counted first and written second: a branch target can gain several
     * digits, so no fixed allowance per line bounds the result. */
    if (renum_lines(e, &o, olds, news, nmap) >= TIKU_TEXTVIEW_LINE_MAX) {
        snprintf(e->feedback, sizeof e->feedback,
                 "Not renumbered: a result line would exceed 1023 bytes");
        free(olds); free(news);
        return;
    }
    size = o.n;
    o.p = malloc(size + 1);
    if (o.p == NULL) {
        free(olds); free(news);
        return;
    }
    o.cap = size;
    o.n = 0;
    (void)renum_lines(e, &o, olds, news, nmap);
    o.p[size] = '\0';
    tiku_textview_group_begin(&e->tv);
    tiku_textview_select_all(&e->tv);
    tiku_textview_insert_text(&e->tv, o.p);
    tiku_textview_group_end(&e->tv);
    (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
    free(olds); free(news); free(o.p);
}
