/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_diff.c - the line difference against what must hold of any: its
 * runs turn one side into the other, and no script is shorter than the
 * one a longest common subsequence gives, checked by dynamic programming
 * on thousands of random pairs.  The unified text is held to what diff -u
 * writes for the same fixtures, and fed back through patch(1) where the
 * machine has one.  A large pair that differs wholly still answers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_diff.h"

static int fails;
static unsigned long long seed = 0x9e3779b97f4a7c15ull;

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief The next of a fixed stream of numbers, below @p n. */
static unsigned
rnd(unsigned n)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (unsigned)((seed >> 11) % n);
}

/** @brief Whether @p x and @p y are the same bytes. */
static int
same_line(const tiku_diff_line_t *x, const tiku_diff_line_t *y)
{
    return x->len == y->len && memcmp(x->text, y->text, x->len) == 0;
}

/** @brief Whether @p d's runs, in order, walk both sides whole, with the
 *         kept lines equal: that it turns @p a into @p b. */
static int
applies(const tiku_diff_t *d, const tiku_diff_line_t *a, long na,
        const tiku_diff_line_t *b, long nb)
{
    long ia = 0, ib = 0, k, i, removed = 0, added = 0;

    for (k = 0; k < d->nrun; k++) {
        const tiku_diff_run_t *r = &d->run[k];

        if (r->a != ia || r->b != ib || r->n <= 0) return 0;
        for (i = 0; r->kind == TIKU_DIFF_SAME && i < r->n; i++)
            if (!same_line(&a[ia + i], &b[ib + i])) return 0;
        if (r->kind != TIKU_DIFF_ADD) ia += r->n;
        if (r->kind != TIKU_DIFF_DEL) ib += r->n;
        if (r->kind == TIKU_DIFF_DEL) removed += r->n;
        if (r->kind == TIKU_DIFF_ADD) added += r->n;
    }
    return ia == na && ib == nb && removed == d->removed &&
           added == d->added;
}

/** @brief The length of a longest common subsequence, the plain way. */
static long
lcs(const tiku_diff_line_t *a, long na, const tiku_diff_line_t *b, long nb)
{
    long *row = calloc((size_t)(nb + 1) * 2, sizeof *row), i, j, best;
    long *prev = row, *cur = row + nb + 1;

    for (i = 1; i <= na; i++) {
        for (j = 1; j <= nb; j++) {
            if (same_line(&a[i - 1], &b[j - 1])) cur[j] = prev[j - 1] + 1;
            else cur[j] = prev[j] > cur[j - 1] ? prev[j] : cur[j - 1];
        }
        memcpy(prev, cur, (size_t)(nb + 1) * sizeof *row);
    }
    best = prev[nb];
    free(row);
    return best;
}

/** @brief @p text as lines, into @p lines.  @return how many. */
static long
lines_of(const char *text, tiku_diff_line_t **lines)
{
    return tiku_diff_split(text, strlen(text), lines);
}

/** @brief Cutting text into lines: LF kept with each, a last line
 *         without one kept too, nothing at all for nothing. */
static void
split_tests(void)
{
    tiku_diff_line_t *l = NULL;
    long n = lines_of("ab\n\nc", &l);

    check(n == 3 && l[0].len == 3 && l[1].len == 1 && l[2].len == 1 &&
          !memcmp(l[2].text, "c", 1), "DF-001",
          "three lines, the last without its LF");
    free(l);
    n = lines_of("", &l);
    check(n == 0 && l != NULL, "DF-002", "no text is no lines");
    free(l);
    n = lines_of("x\n", &l);
    check(n == 1 && l[0].len == 2, "DF-003", "one line and its LF");
    free(l);
}

/** @brief Diff two texts with @p flags; @return removed*1000 + added. */
static long
counts(const char *x, const char *y, int flags)
{
    tiku_diff_line_t *a = NULL, *b = NULL;
    long na = lines_of(x, &a), nb = lines_of(y, &b), got = -1;
    tiku_diff_t d;

    if (tiku_diff_lines(a, na, b, nb, flags, &d) == 0) {
        got = applies(&d, a, na, b, nb) || flags ? d.removed * 1000 + d.added
                                                 : -2;
        tiku_diff_free(&d);
    }
    free(a);
    free(b);
    return got;
}

/** @brief Small pairs whose answer is known by hand. */
static void
fixed_tests(void)
{
    check(counts("a\nb\nc\n", "a\nb\nc\n", 0) == 0, "DF-010",
          "the same text: nothing changed");
    check(counts("", "a\nb\n", 0) == 2, "DF-011",
          "from nothing: two lines added");
    check(counts("a\nb\n", "", 0) == 2000, "DF-012",
          "to nothing: two lines removed");
    check(counts("a\nb\nc\n", "a\nX\nc\n", 0) == 1001, "DF-013",
          "one line changed: one out, one in");
    check(counts("a\nb\nc\nd\n", "b\nc\nd\ne\n", 0) == 1001, "DF-014",
          "one gone from the top, one come at the foot");
    check(counts("x\ny", "x\ny\n", 0) == 1001, "DF-015",
          "a last line without its LF is a different line");
    check(counts("a  b\t\nc\n", "a b\nc\n", TIKU_DIFF_SPACE) == 0,
          "DF-016", "SPACE: runs of blanks and blanks at the end ignored");
    check(counts("a b\n", "ab\n", TIKU_DIFF_SPACE) == 1001, "DF-017",
          "SPACE still sees blanks that come or go between words");
    check(counts("x\r\ny\r\n", "x\ny\n", TIKU_DIFF_CR) == 0 &&
          counts("x\r\n", "x\n", 0) == 1001, "DF-018",
          "CR: a CRLF line equals its LF twin; without it, it does not");
}

/** @brief Random pairs: every script valid, and as short as the LCS
 *         says, bounded or not. */
static void
random_tests(void)
{
    static const char *words[] = { "a\n", "b\n", "c\n", "}\n", "\n", "d\n" };
    tiku_diff_line_t a[80], b[80];
    long round, bad = 0, longer = 0, i;

    for (round = 0; round < 4000; round++) {
        long na = (long)rnd(60), nb, k;
        int flags = (round & 1) ? TIKU_DIFF_EXACT : 0;
        tiku_diff_t d;

        for (i = 0; i < na; i++) {
            const char *w = words[rnd(round % 3 ? 6 : 3)];
            a[i].text = w;
            a[i].len = strlen(w);
        }
        for (i = nb = 0; i < na && nb < 78; i++) {      /* edit a copy */
            unsigned what = rnd(10);
            if (what == 0) continue;                   /* removed */
            if (what == 1) {                           /* one put in */
                const char *w = words[rnd(6)];
                b[nb].text = w;
                b[nb++].len = strlen(w);
            }
            b[nb++] = a[i];
        }
        for (k = rnd(3); k > 0 && nb < 80; k--) {
            const char *w = words[rnd(6)];
            b[nb].text = w;
            b[nb++].len = strlen(w);
        }
        if (tiku_diff_lines(a, na, b, nb, flags, &d) != 0 ||
            !applies(&d, a, na, b, nb)) {
            bad++;
            continue;
        }
        if (d.removed + d.added != na + nb - 2 * lcs(a, na, b, nb)) longer++;
        tiku_diff_free(&d);
    }
    check(bad == 0, "DF-020", "4,000 random pairs: every script turns a "
                              "into b");
    check(longer == 0, "DF-021", "and each is as short as the longest "
                                 "common subsequence allows");
}

/** @brief A hundred thousand lines of code-like repeats, and the same
 *         with every hundredth gone: exact, and quick. */
static void
long_file_test(tiku_diff_line_t *a, tiku_diff_line_t *b, char *pool)
{
    long n = 100000, i, nb;
    char *w = pool;
    tiku_diff_t d;
    clock_t t0 = clock();
    double secs;
    int ok;

    for (i = 0; i < n; i++) {                  /* repeats, as code has */
        int k = snprintf(w, 16, "l%u\n", rnd(5000));
        a[i].text = w;
        a[i].len = (size_t)k;
        w += k + 1;
    }
    for (i = nb = 0; i < n; i++)
        if (i % 100 != 7) b[nb++] = a[i];
    ok = tiku_diff_lines(a, n, b, nb, 0, &d) == 0 &&
         applies(&d, a, n, b, nb) && d.removed == n / 100 && d.added == 0;
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    tiku_diff_free(&d);
    printf("           100,000 lines, 1,000 removed: %.3f s\n", secs);
    check(ok && secs < 5.0, "DF-030", "a hundred thousand lines with a "
                                      "thousand gone: exact, and quick");
}

/** @brief Two sides of twenty thousand random x and y lines each, where
 *         nothing lines up for long: the step bound keeps it prompt. */
static void
random_sides_test(tiku_diff_line_t *a, tiku_diff_line_t *b)
{
    tiku_diff_t d;
    clock_t t0;
    double secs;
    int ok;

    for (long i = 0; i < 20000; i++) {         /* all of it different */
        a[i].text = rnd(2) ? "x\n" : "y\n";
        a[i].len = 2;
        b[i].text = rnd(2) ? "x\n" : "y\n";
        b[i].len = 2;
    }
    t0 = clock();
    ok = tiku_diff_lines(a, 20000, b, 20000, 0, &d) == 0 &&
         applies(&d, a, 20000, b, 20000);
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("           20,000 random x and y lines each side: %.3f s, %s\n",
           secs, d.shortest ? "shortest" : "bounded");
    tiku_diff_free(&d);
    check(ok && secs < 5.0, "DF-031", "two long random sides: a correct "
                                      "script, the step bound holding");
}

/** @brief The big pairs, in room made once for both. */
static void
size_tests(void)
{
    tiku_diff_line_t *a = malloc(100000 * sizeof *a);
    tiku_diff_line_t *b = malloc(100000 * sizeof *b);
    char *pool = malloc(100000 * 16);

    long_file_test(a, b, pool);
    random_sides_test(a, b);
    free(a);
    free(b);
    free(pool);
}

/** @brief The unified text of @p x against @p y with three lines of
 *         context: whether it is @p want. */
static int
unified_is(const char *x, const char *y, const char *want)
{
    tiku_diff_line_t *a = NULL, *b = NULL;
    long na = lines_of(x, &a), nb = lines_of(y, &b);
    tiku_diff_t d;
    char *u = NULL;
    size_t len = 0;
    int ok;

    ok = tiku_diff_lines(a, na, b, nb, 0, &d) == 0 &&
         (u = tiku_diff_unified(&d, a, b, "a", "b", 3, &len)) != NULL &&
         strlen(u) == len && !strcmp(u, want);
    if (!ok && u) printf("got:\n%s\nwanted:\n%s\n", u, want);
    free(u);
    tiku_diff_free(&d);
    free(a);
    free(b);
    return ok;
}

/** @brief What diff -u writes for the same pairs, byte for byte. */
static void
unified_tests(void)
{
    check(unified_is("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n",
                     "a\nb\nc\nD\ne\nf\ng\nh\ni\nJ\nk\n",
                     "--- a\n+++ b\n@@ -1,10 +1,11 @@\n a\n b\n c\n-d\n"
                     "+D\n e\n f\n g\n h\n i\n-j\n+J\n+k\n"),
          "DF-040", "changes five kept lines apart share one hunk");
    check(unified_is("x\ny", "x\ny\n",
                     "--- a\n+++ b\n@@ -1,2 +1,2 @@\n x\n-y\n"
                     "\\ No newline at end of file\n+y\n"),
          "DF-041", "a last line without its LF is marked as diff marks it");
    check(unified_is("b\n", "a\nb\n",
                     "--- a\n+++ b\n@@ -1 +1,2 @@\n+a\n b\n"),
          "DF-042", "a count of one is left off");
    check(unified_is("", "a\n", "--- a\n+++ b\n@@ -0,0 +1 @@\n+a\n"),
          "DF-043", "from nothing: -0,0");
    check(unified_is("l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n"
                     "l13\nl14\nl15\nl16\nl17\nl18\nl19\nl20\n",
                     "l1\nL2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n"
                     "l13\nl14\nl15\nl16\nl17\nL18\nl19\nl20\n",
                     "--- a\n+++ b\n@@ -1,5 +1,5 @@\n l1\n-l2\n+L2\n l3\n"
                     " l4\n l5\n@@ -15,6 +15,6 @@\n l15\n l16\n l17\n-l18\n"
                     "+L18\n l19\n l20\n"),
          "DF-044", "changes far apart are two hunks");
    check(unified_is("same\n", "same\n", ""), "DF-045",
          "no difference is no text");
}

/** @brief A function put in between two others ends on its blank line,
 *         as a person reads it, not its neighbour's. */
static void
slide_tests(void)
{
    check(unified_is("}\n\nint h;\n", "}\n\nint g;\n}\n\nint h;\n",
                     "--- a\n+++ b\n@@ -1,3 +1,6 @@\n }\n \n+int g;\n+}\n"
                     "+\n int h;\n"),
          "DF-050", "an addition slides to end on a blank line");
}

/** @brief Words of a changed line, as numbers: what changed inside it. */
static void
ints_tests(void)
{
    uint32_t a[] = { 1, 2, 3, 4 }, b[] = { 1, 9, 3, 4, 5 };
    tiku_diff_t d;
    int ok = tiku_diff_ints(a, 4, b, 5, 0, &d) == 0 && d.removed == 1 &&
             d.added == 2 && d.nrun == 5 && d.run[1].kind == TIKU_DIFF_DEL &&
             d.run[1].a == 1 && d.run[2].kind == TIKU_DIFF_ADD &&
             d.run[4].kind == TIKU_DIFF_ADD && d.run[4].b == 4;

    tiku_diff_free(&d);
    check(ok, "DF-060", "numbers compared as lines are: 2 for 9, 5 added");
}

/** @brief @p text into a file at @p path. */
static void
write_file(const char *path, const char *text, size_t len)
{
    FILE *f = fopen(path, "wb");

    if (f) {
        fwrite(text, 1, len, f);
        fclose(f);
    }
}

/** @brief Random pairs through patch(1), where there is one: the unified
 *         text rebuilds b from a exactly, missing last LFs included. */
static void
patch_tests(void)
{
    char dir[] = "/tmp/tikudiff-XXXXXX", pa[64], pd[64], po[64], cmd[256];
    char x[512], y[512];
    int round, bad = 0;

    if (system("command -v patch > /dev/null 2>&1") != 0 || !mkdtemp(dir)) {
        printf("  [SKIP] DF-070    no patch(1) here\n");
        return;
    }
    snprintf(pa, sizeof pa, "%s/a", dir);
    snprintf(pd, sizeof pd, "%s/d", dir);
    snprintf(po, sizeof po, "%s/o", dir);
    for (round = 0; round < 60; round++) {
        tiku_diff_line_t *a = NULL, *b = NULL;
        size_t xn = 0, yn = 0, ulen = 0, got;
        long na, nb, i;
        tiku_diff_t d;
        char *u, back[600];
        FILE *f;

        for (i = rnd(30); i > 0; i--)
            xn += (size_t)snprintf(x + xn, sizeof x - xn, "w%u\n", rnd(6));
        for (i = 0; i < (long)xn; i++)       /* b: a, a few lines touched */
            if (x[i] == '\n' || rnd(12)) y[yn++] = x[i]; else y[yn++] = 'Q';
        if (rnd(3) == 0 && yn > 0) yn--;     /* its last LF gone */
        if (rnd(3) == 0 && xn > 0) xn--;
        na = tiku_diff_split(x, xn, &a);
        nb = tiku_diff_split(y, yn, &b);
        if (tiku_diff_lines(a, na, b, nb, 0, &d) != 0 ||
            (u = tiku_diff_unified(&d, a, b, pa, po, 3, &ulen)) == NULL) {
            bad++;
            continue;
        }
        write_file(pa, x, xn);
        write_file(pd, u, ulen);
        unlink(po);
        snprintf(cmd, sizeof cmd, "patch -s -o %s %s < %s > /dev/null 2>&1",
                 po, pa, pd);
        if (ulen > 0 && system(cmd) != 0) bad++;
        else if (ulen > 0) {
            f = fopen(po, "rb");
            got = f ? fread(back, 1, sizeof back, f) : 0;
            if (f) fclose(f);
            if (got != yn || memcmp(back, y, yn)) bad++;
        }
        free(u);
        tiku_diff_free(&d);
        free(a);
        free(b);
    }
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)system(cmd);
    check(bad == 0, "DF-070", "60 random pairs: patch(1) rebuilds b from a "
                              "and the unified text");
}

/** @brief Every group; @return 0 when every row passes. */
int
main(void)
{
    printf("Diff: lines compared, scripted, and written as diff -u does\n");
    split_tests();
    fixed_tests();
    random_tests();
    size_tests();
    unified_tests();
    slide_tests();
    ints_tests();
    patch_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: lines compared, the scripts "
                                    "valid, shortest and patchable");
    return fails ? 1 : 0;
}
