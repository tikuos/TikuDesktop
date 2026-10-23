/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_document_file.c - save and reopen a styled native document package.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Create a short report with a named heading and a numbered item. */
static tiku_rich_status_t
report(tiku_richdoc_t **out)
{
    tiku_rich_style_source_t heading = {.name = "Heading",
        .chars = {.present = TIKU_RICH_BOLD | TIKU_RICH_SIZE,
            .bold = 1, .size = 20},
        .paragraph = {.present = TIKU_RICH_SPACING, .after = 12}};
    tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "Sensor report", .length = 13, .style = 1},
        {.text = "Temperature: 24 C", .length = 17,
         .list = {TIKU_RICH_LIST_DECIMAL, 0, 1}}
    };
    tiku_rich_source_t source = {&heading, 1, paragraphs, 2};
    tiku_rich_status_t status = tiku_richdoc_create(NULL, out);

    if (status == TIKU_RICH_OK) status = tiku_richdoc_import(*out, &source, 0);
    return status;
}

/** @brief Print reopened text and its retained named-style count. */
static int
show(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    size_t i;

    if (tiku_richdoc_snapshot(doc, &snapshot) != TIKU_RICH_OK) return 1;
    for (i = 0; i < tiku_richsnapshot_paragraphs(snapshot); i++) {
        if (tiku_richsnapshot_paragraph(snapshot, i, &p) != TIKU_RICH_OK) {
            tiku_richsnapshot_release(snapshot);
            return 1;
        }
        printf("%.*s\n", (int)p.length, p.text);
    }
    printf("Reopened %zu paragraphs and %zu named style; clean=%d\n",
           tiku_richsnapshot_paragraphs(snapshot),
           tiku_richsnapshot_styles(snapshot), !tiku_richdoc_dirty(doc));
    tiku_richsnapshot_release(snapshot);
    return 0;
}

/** @brief Replace the explicitly named output path, then reopen its package. */
int
main(int argc, char **argv)
{
    tiku_richdoc_t *doc = NULL, *loaded = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richio_status_t status;
    int result = 1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s output.tkrd\nReplaces that output file.\n",
                argv[0]);
        return 2;
    }
    if (report(&doc) != TIKU_RICH_OK ||
        tiku_richdoc_snapshot(doc, &snapshot) != TIKU_RICH_OK) {
        fputs("Could not create the report.\n", stderr);
        goto done;
    }
    status = tiku_richio_save(argv[1], snapshot, NULL);
    if (status != TIKU_RICHIO_OK) {
        fprintf(stderr, "%s\n", tiku_richio_strerror(status));
        goto done;
    }
    if (tiku_richdoc_mark_saved(doc, snapshot) != TIKU_RICH_OK) goto done;
    status = tiku_richio_load(argv[1], NULL, &loaded);
    if (status != TIKU_RICHIO_OK) {
        fprintf(stderr, "%s\n", tiku_richio_strerror(status));
        goto done;
    }
    result = show(loaded);
done:
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(loaded);
    tiku_richdoc_release(doc);
    return result;
}
