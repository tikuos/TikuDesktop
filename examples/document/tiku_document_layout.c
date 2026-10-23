/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_document_layout.c - render a native document fixture without a window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont.h"
#include "tiku_image.h"

#include <stdio.h>
#include <string.h>

/** @brief Import mixed-size headings, prose and list paragraphs. */
static tiku_rich_status_t
populate(tiku_richdoc_t *doc)
{
    tiku_rich_paragraph_source_t paragraphs[5] = {
        {.text = "A native document",
         .chars = {.present = TIKU_RICH_SIZE | TIKU_RICH_BOLD |
            TIKU_RICH_FOREGROUND, .size = 27, .bold = 1,
            .foreground = 0xff23465a},
         .paragraph = {.present = TIKU_RICH_SPACING, .after = 16}},
        {.text = "Text, formatting and geometry belong to one document. "
            "Each view keeps its own selection, scroll position and zoom. "
            "Resizing wraps the same text without changing its contents.",
         .paragraph = {.present = TIKU_RICH_SPACING, .after = 18}},
        {.text = "One layout for drawing and editing",
         .chars = {.present = TIKU_RICH_BOLD | TIKU_RICH_FOREGROUND,
            .bold = 1, .foreground = 0xff23465a},
         .paragraph = {.present = TIKU_RICH_SPACING, .after = 10}},
        {.text = "Glyph positions also determine caret placement, "
                 "mouse selection and highlighted ranges.",
         .list = {TIKU_RICH_LIST_DECIMAL, 0, 1},
         .paragraph = {.present = TIKU_RICH_SPACING, .after = 8}},
        {.text = "Layout work can pause between steps. A cancelled job "
                 "does not change the document or another view.",
         .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}}
    };
    tiku_rich_source_t source = {.paragraphs = paragraphs,
                                 .paragraph_count = 5};
    size_t i;

    for (i = 0; i < 5; i++) paragraphs[i].length = strlen(paragraphs[i].text);
    return tiku_richdoc_import(doc, &source, 0);
}

/** @brief Render document-point geometry with owned system fonts. */
static int
render(const char *path)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richlayout_t *layout = NULL;
    tiku_richfont_t *fonts = NULL;
    tiku_surface_t *surface = NULL;
    tiku_richlayout_options_t options = {.width = 448, .leading = 2,
        .defaults = {.present = TIKU_RICH_SIZE, .size = 13}};
    tiku_rich_status_t status;
    int result = 1;

    status = tiku_richfont_system(NULL, &fonts);
    if (status != TIKU_RICH_OK) goto done;
    options.provider = tiku_richfont_provider(fonts);
    status = tiku_richdoc_create(NULL, &doc);
    if (status != TIKU_RICH_OK) goto done;
    status = populate(doc);
    if (status != TIKU_RICH_OK) goto done;
    status = tiku_richdoc_snapshot(doc, &snapshot);
    if (status != TIKU_RICH_OK) goto done;
    status = tiku_richlayout_create(snapshot, &options, &layout);
    if (status != TIKU_RICH_OK) goto done;
    do status = tiku_richlayout_step(layout, 256);
    while (status == TIKU_RICH_PENDING);
    if (status != TIKU_RICH_OK) goto done;
    surface = tiku_surface_new(640, 450, 0xffffff);
    if (!surface) { status = TIKU_RICH_NO_MEMORY; goto done; }
    status = tiku_richfont_draw(fonts, layout, surface, 40, 36, 1.25, 0,
                               tiku_richlayout_lines(layout));
    if (status != TIKU_RICH_OK) goto done;
    if (tiku_png_write(path, surface->px, surface->w, surface->h, 0))
        goto done;
    printf("Wrote %s (%zu lines; font diagnostics 0x%x)\n", path,
           tiku_richlayout_lines(layout), tiku_richlayout_warnings(layout));
    result = 0;
done:
    if (result) fprintf(stderr, "Document rendering failed: status %d\n",
                        status);
    tiku_surface_free(surface);
    tiku_richlayout_release(layout);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    tiku_richfont_release(fonts);
    return result;
}

/** @brief Write the requested PNG without starting desktop services. */
int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s output.png\n", argv[0]);
        return 2;
    }
    return render(argv[1]);
}
