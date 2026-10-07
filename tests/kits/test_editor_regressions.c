/*
 * TikuOS kits -- editor regression tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editor_regressions.c - folding, annotation order and UTF-8, regex errors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview_private.h"
#include "tiku_dl.h"
#include "tiku_ui.h"

static int fails;

/** @brief Record a failed contract check. */
static void
check(int ok, const char *id, const char *what)
{
    if (!ok) {
        printf("FAIL %s: %s\n", id, what);
        fails++;
    }
}

/** @brief Create a view owning a document containing @p text. */
static tiku_codeview_t *
view(const char *text)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, text, strlen(text)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(2);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, (tiku_rect_t){0, 0, 320, 80});
    return v;
}

/** @brief Block keywords match independently of rule or document case. */
static void
fold_case(void)
{
    static const char *const blocks[][3] = {
        {"BEGIN", "END", NULL}, {"begin", "end", NULL},
        {"BeGiN", "eNd", NULL}
    };
    size_t i;

    for (i = 0; i < 3; i++) {
        tiku_codeview_t *v = view("bEgIn\nx\nBEGIN\ny\neNd\nEND");
        tiku_codeview_rules_t rules = {0};

        rules.blocks = blocks[i];
        tiku_codeview_set_rules(v, &rules);
        check(tiku_codeview_fold_end(v, 0) == 4 &&
              tiku_codeview_fold_end(v, 2) == 3, "D-156",
              "mixed-case nested blocks have matching boundaries");
        tiku_codeview_destroy(v);
    }
}

/** @brief Reusing a cleared annotation slot preserves insertion order. */
static void
annotation_order(void)
{
    tiku_codeview_t *v = view("a\nb");
    unsigned kind = 0;
    int i;

    (void)tiku_editdoc_annotation_add(v->doc, 0, "remove", 0);
    (void)tiku_editdoc_annotation_add(v->doc, 1, "older", 1);
    for (i = 0; i < 12; i++) {
        (void)tiku_editdoc_annotations_clear(v->doc, -1, 1u);
        (void)tiku_editdoc_annotation_add(v->doc, 1, "newer", 0);
        check(!strcmp(tiku_editdoc_annotation(v->doc, 1, 0, &kind), "older") &&
              kind == 1 &&
              !strcmp(tiku_editdoc_annotation(v->doc, 1, 1, NULL), "newer") &&
              tiku_editdoc_annotation(v->doc, 1, 2, NULL) == NULL,
              "D-159", "older annotations precede newly added annotations");
    }
    tiku_codeview_destroy(v);
}

/** @brief Annotation clipping never records an incomplete UTF-8 code point. */
static void
annotation_utf8(void)
{
    const char *glyphs[] = {"\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80"};
    size_t g, split;

    for (g = 0; g < 3; g++) {
        for (split = 0; split <= strlen(glyphs[g]); split++) {
            tiku_codeview_t *v = view("a");
            tiku_surface_t *s = tiku_surface_new(320, 80, 0);
            tiku_dl_t *dl = tiku_dl_new();
            tiku_cv_row_t row = {0};
            char text[300];
            unsigned char bytes[1024];
            size_t prefix = 255 - split, at = 0;
            tiku_dl_fact_t fact;
            int found = 0;

            memset(text, 'a', prefix);
            strcpy(text + prefix, glyphs[g]);
            (void)tiku_editdoc_annotation_add(v->doc, 0, text, 0);
            s->record = dl;
            tiku_cv_annotation_paint(v, s, &row, 0);
            check(tiku_dl_flatten(dl, bytes, sizeof bytes, NULL), "D-158",
                  "annotation paint can be recorded");
            while (tiku_dl_read(bytes, tiku_dl_flat_size(dl), &at, &fact)) {
                if (fact.kind == TIKU_DL_FACT_TEXT) {
                    found++;
                    check(strlen(fact.text) ==
                          (split == strlen(glyphs[g]) ? 255 : prefix) &&
                          !memcmp(fact.text, text, prefix), "D-158",
                          "truncation stops before a partial code point");
                }
            }
            check(found == 1, "D-158", "one annotation text command");
            tiku_dl_free(dl);
            tiku_surface_free(s);
            tiku_codeview_destroy(v);
        }
    }
}

/** @brief A NULL query text has the same error for regex and literal search. */
static void
regex_null(void)
{
    tiku_codeview_t *v = view("abc");
    tiku_edit_query_t q = {NULL, 1, TIKU_EDIT_REGEX};
    tiku_edit_range_t hit;

    check(tiku_edit_find(v->doc, &q, 0, 0, &hit, NULL) == TIKU_EDIT_INVALID_TEXT,
          "D-160", "NULL regex text is invalid text");
    check(tiku_edit_replace_all(v->doc, &q, "x", 1,
                                (tiku_edit_view_t){0}, NULL) ==
          TIKU_EDIT_INVALID_TEXT && tiku_editdoc_length(v->doc) == 3,
          "D-160", "NULL replacement query leaves the document unchanged");
    q.flags = 0;
    check(tiku_edit_find(v->doc, &q, 0, 0, &hit, NULL) == TIKU_EDIT_INVALID_TEXT,
          "D-160", "literal search reports the same error");
    tiku_codeview_destroy(v);
}

/** @brief Run four editor regression groups. */
int
main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    fold_case();
    annotation_order();
    annotation_utf8();
    regex_null();
    printf("%s: editor regressions\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
