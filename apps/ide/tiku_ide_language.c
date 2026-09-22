/* tiku_ide_language.c -- BASIC keyword normalization and line renumbering.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static int
renum_is_word(int c);
static int
renum_keyword(const char *s);
static int
renum_map(int old, const int *olds, const int *news, int n);
static void
renum_emit(char *out, size_t *u, const char *s,
           const int *olds, const int *news, int n);

int
tiku_ide_norm_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '$';
}

/**
 * @brief Uppercase the reserved word the caret just finished, 1982-style.
 *
 * The spelling authority is the CLASSIFIER's -- the same table the
 * colours come from -- so a word is uppercased exactly when it would be
 * painted as reserved, which also means a keyword typed inside a string
 * or a remark is left alone for free.  The rewrite is one journal
 * batch, so a single Undo puts back what the hands typed.  BASIC only:
 * C is not a language of shouted keywords.
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
           (c >= '0' && c <= '9');
}

/** @brief The length of a target keyword starting here, or 0. */
static int
renum_keyword(const char *s)
{
    static const char *const K[] = { "goto", "gosub", "then", "else" };
    size_t i;

    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        int n = (int)strlen(K[i]);

        if (strncasecmp(s, K[i], (size_t)n) == 0 && !renum_is_word(s[n])) {
            return n;
        }
    }
    return 0;
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

/**
 * @brief Copy @p s, rewriting a number after GOTO/GOSUB/THEN/ELSE.
 *
 * A jump has to follow the line it names when the numbers move, or
 * renumbering would quietly break every branch.  Only a number that
 * FOLLOWS one of the branch words is touched; a number sitting in a
 * string or a REM is left as it stands.
 */
static void
renum_emit(char *out, size_t *u, const char *s,
           const int *olds, const int *news, int n)
{
    int prev_word = 0;

    while (*s != '\0') {
        int kw = prev_word ? 0 : renum_keyword(s);

        if (kw > 0) {
            memcpy(out + *u, s, (size_t)kw);
            *u += (size_t)kw;
            s += kw;
            while (*s == ' ' || *s == '\t') {
                out[(*u)++] = *s++;
            }
            if (*s >= '0' && *s <= '9') {
                int old = 0;
                char nb[16];
                int nl;

                while (*s >= '0' && *s <= '9') {
                    old = old * 10 + (*s - '0');
                    s++;
                }
                nl = snprintf(nb, sizeof nb, "%d",
                              renum_map(old, olds, news, n));
                memcpy(out + *u, nb, (size_t)nl);
                *u += (size_t)nl;
            }
            prev_word = 1;
            continue;
        }
        prev_word = renum_is_word((unsigned char)*s);
        out[(*u)++] = *s++;
    }
}

/**
 * @brief Renumber a BASIC program by tens, branches following.
 *
 * Every line that WEARS a number is given the next ten; every branch to
 * an old number is rewritten to the new one.  The whole rewrite is ONE
 * journal batch, so a single Undo takes it all back -- which is the
 * only thing standing in the way of a person trying it.
 */
void
tiku_ide_renumber_basic(edit_t *e)
{
    int total = tiku_textview_lines(&e->tv);
    int *olds, *news;
    int nmap = 0, next = 10, L;
    size_t cap = 64, used = 0;
    char *out;

    if (total <= 0) {
        return;
    }
    olds = malloc((size_t)total * sizeof *olds);
    news = malloc((size_t)total * sizeof *news);
    for (L = 0; L < total; L++) {
        cap += (size_t)tiku_textview_line_len(&e->tv, L) + 16u;
    }
    out = malloc(cap);
    if (olds == NULL || news == NULL || out == NULL) {
        free(olds); free(news); free(out);
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
    for (L = 0; L < total; L++) {
        const char *t = tiku_textview_line(&e->tv, L);
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
                              renum_map(atoi(t), olds, news, nmap));

            memcpy(out + used, nb, (size_t)nl);
            used += (size_t)nl;
            renum_emit(out, &used, t + d, olds, news, nmap);
        } else {
            renum_emit(out, &used, t, olds, news, nmap);
        }
        if (L < total - 1) {
            out[used++] = '\n';
        }
    }
    out[used] = '\0';
    tiku_textview_group_begin(&e->tv);
    tiku_textview_select_all(&e->tv);
    tiku_textview_insert_text(&e->tv, out);
    tiku_textview_group_end(&e->tv);
    (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
    free(olds); free(news); free(out);
}
