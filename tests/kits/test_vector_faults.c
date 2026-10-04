/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_vector_faults.c - the vector rasteriser under faults: path growth
 * failed at each allocation, a dash pattern finer than a pixel can show,
 * and PDF line states the rasteriser alone refuses, drawn through the PDF
 * kit's seam.  It links its own copy of the rasteriser, built to take
 * the realloc below (tests.mk).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_pdf_private.h"
#include "tiku_vector_raster.h"

static int fail_after = -1;         /* reallocs left before one fails;
                                       -1 for none                      */
static void *last;                  /* the last block realloc gave      */

/**
 * @brief The rasteriser's realloc: fails once fail_after successes are
 *        used up, and otherwise always moves the block.
 *
 * A block resized in place is moved anyway, so the old address is freed
 * every time: a path still holding it holds a dangling pointer, and a
 * check against the address returned finds it without a sanitizer.
 */
void *
tiku_vector_test_realloc(void *p, size_t bytes)
{
    void *q, *moved;

    if (fail_after == 0) {
        return NULL;
    }
    if (fail_after > 0) {
        fail_after--;
    }
    q = realloc(p, bytes);
    if (q != NULL && q == p && (moved = malloc(bytes)) != NULL) {
        memcpy(moved, q, bytes);
        free(q);
        q = moved;
    }
    last = q;
    return q;
}

/** @brief Each allocation of a new subpath failed in turn: the path keeps
 *         every block realloc handed it, for its free to give back. */
static void
growth_faults(void)
{
    tiku_vector_path_t p = {0};
    void *start;
    int failing, i;

    for (failing = 0; failing < 3; failing++) {
        tiku_vector_path_t q = {0};

        fail_after = failing;
        tiku_vector_path_moveto(&q, 1, 1);
        assert(q.failed);
        tiku_vector_path_free(&q);
    }
    fail_after = -1;
    for (i = 0; i < 16; i++) {
        tiku_vector_path_moveto(&p, i, i);
    }
    assert(p.nsub == 16 && !p.failed);
    /* The start array grows and moves; the closed flags then fail. */
    fail_after = 1;
    tiku_vector_path_moveto(&p, 20, 20);
    start = last;
    assert(p.failed == TIKU_VECTOR_NOMEM && p.nsub == 16);
    assert(p.start == start);
    fail_after = -1;
    tiku_vector_path_clear(&p);
    for (i = 0; i < 20; i++) {
        tiku_vector_path_moveto(&p, i, i);
    }
    assert(!p.failed && p.nsub == 20);
    tiku_vector_path_free(&p);
}

/**
 * @brief A 40 px line dashed in 1e-17 px steps: stroked into a bounded
 *        number of pieces, quickly, by a strict path and by a PDF one.
 *
 * No dash or gap is walked shorter than 1e-3 px, so the line is one dash
 * of four points every 2e-3 px: 20,000 of them, and one to spare.
 */
static void
tiny_dashes(void)
{
    static const double dash[] = {1e-17, 1e-17};
    tiku_vector_path_t line, out;
    clock_t t0 = clock();
    int pdf;

    for (pdf = 0; pdf < 2; pdf++) {
        if (pdf) {
            pdf_path_init(&line);
            pdf_path_init(&out);
        } else {
            tiku_vector_path_init(&line);
            tiku_vector_path_init(&out);
            line.strict = out.strict = 1;
        }
        tiku_vector_path_moveto(&line, 10, 10);
        tiku_vector_path_lineto(&line, 50, 10);
        if (pdf) {
            pdf_path_stroke(&line, 2, 0, 0, 10, dash, 2, 0, &out);
        } else {
            tiku_vector_path_stroke(&line, 2, 0, 0, 10, dash, 2, 0, &out);
        }
        assert(!out.failed && out.n > 0 && out.n <= 4 * (20000 + 1));
        tiku_vector_path_free(&line);
        tiku_vector_path_free(&out);
    }
    assert(clock() - t0 < 2 * CLOCKS_PER_SEC);
}

/** @brief How many of row @p y's pixels from @p x0 to @p x1 a stroke of
 *         @p line through the PDF seam covers past @p least. */
static int
inked(const pdf_path_t *line, double width, const double *dash, int ndash,
      double phase, double miter, int y, int x0, int x1, int least)
{
    pdf_path_t out;
    pdf_mask_t m;
    int x, n = 0;

    pdf_path_init(&out);
    pdf_path_stroke(line, width, 0, 0, miter, dash, ndash, phase, &out);
    assert(!out.failed);
    m = pdf_path_fill(&out, 0, 0, 0, 32, 16);
    for (x = x0; m.a != NULL && x <= x1; x++) {
        n += m.a[y * 32 + x] > least;
    }
    pdf_mask_free(&m);
    pdf_path_free(&out);
    return n;
}

/**
 * @brief PDF line states the rasteriser refuses are drawn through the
 *        seam: "-1 w" the thinnest line, "[3 -1] 0 d" nearly solid,
 *        "[1 -5 2] 1.5 d" dashed, and a width, phase or miter limit that
 *        is not finite drawn.
 *
 * The rasteriser called directly, as the circuit viewer calls it with
 * strict paths, still refuses them.
 */
static void
pdf_line_states(void)
{
    static const double gap[] = {3, -1}, odd[] = {1, -5, 2};
    pdf_path_t line;
    tiku_vector_path_t strict;

    pdf_path_init(&line);
    pdf_path_moveto(&line, 4, 8.5);
    pdf_path_lineto(&line, 28, 8.5);
    assert(inked(&line, -1, NULL, 0, 0, 10, 8, 5, 27, 100) == 23);
    assert(inked(&line, 2, gap, 2, 0, 10, 8, 5, 27, 240) == 23);
    assert(inked(&line, 2, odd, 3, 1.5, 10, 8, 5, 27, 240) > 0);
    assert(inked(&line, 2, odd, 3, 1.5, 10, 8, 5, 27, 0) < 23);
    assert(inked(&line, 2, gap, 2, NAN, INFINITY, 8, 5, 27, 240) == 23);
    assert(inked(&line, NAN, gap, 2, INFINITY, NAN, 8, 5, 27, 100) == 23);

    tiku_vector_path_init(&strict);
    strict.strict = 1;
    tiku_vector_path_stroke(&line, -1, 0, 0, 10, NULL, 0, 0, &strict);
    assert(strict.failed == TIKU_VECTOR_BAD);
    tiku_vector_path_stroke(&line, 2, 0, 0, 10, gap, 2, 0, &strict);
    assert(strict.failed == TIKU_VECTOR_BAD);
    tiku_vector_path_stroke(&line, 2, 0, 0, 10, gap, 2, NAN, &strict);
    assert(strict.failed == TIKU_VECTOR_BAD);
    tiku_vector_path_free(&strict);
    pdf_path_free(&line);
}

/** @brief A PDF path holds PDF_POINTS_MAX points, 2^21, and fails at one
 *         more; it keeps that limit once freed. */
static void
pdf_point_limit(void)
{
    pdf_path_t p;
    int i;

    assert(PDF_POINTS_MAX == 1 << 21);
    pdf_path_init(&p);
    pdf_path_moveto(&p, 0, 0);
    for (i = 1; i < PDF_POINTS_MAX; i++) {
        pdf_path_lineto(&p, i % 64, i / 64 % 64);
    }
    assert(!p.failed && p.n == PDF_POINTS_MAX);
    pdf_path_lineto(&p, 1, 1);
    assert(p.failed == TIKU_VECTOR_LIMIT && p.n == PDF_POINTS_MAX);
    pdf_path_free(&p);
    assert(p.max_points == PDF_POINTS_MAX && p.n == 0 && p.pt == NULL);
}

/** @brief Every group; an assertion stops the suite at the first fault. */
int
main(void)
{
    growth_faults();
    tiny_dashes();
    pdf_line_states();
    pdf_point_limit();
    puts("PASS: path growth keeps its blocks at each allocation failure, "
         "fine dashes end, PDF line states draw, a PDF path's limit");
    return 0;
}
