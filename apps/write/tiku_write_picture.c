/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_picture.c - inline picture insertion and aspect-preserving size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Compare a document position without structure-padding bytes. */
static int
same(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    return a.body == b.body && a.paragraph == b.paragraph &&
           a.byte == b.byte && a.affinity == b.affinity;
}

void
write_picture_finish(write_app_t *app, write_job_t *job)
{
    tiku_rich_pos_t first, last;
    tiku_rich_image_t image;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_IMAGE_INSERT};
    tiku_richsnapshot_t *snapshot;
    tiku_rich_page_setup_t page;
    double scale;
    tiku_rich_status_t status;

    if (job->status != TIKU_RICHIO_OK) {
        snprintf(app->status, sizeof app->status,
            "Picture not inserted. Use PNG, JPEG or BMP, at most 4 Mi pixels.");
        return;
    }
    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    if (job->generation != tiku_richdoc_generation(app->doc) ||
        !same(first, job->first) || !same(last, job->last)) {
        snprintf(app->status, sizeof app->status,
                 "Picture insertion cancelled: the document or caret moved.");
        return;
    }
    status = tiku_richdoc_snapshot(app->doc, &snapshot);
    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    tiku_richsnapshot_page(snapshot, &page);
    tiku_richsnapshot_release(snapshot);
    scale = fmin(0.75, fmin((page.width - page.left - page.right) /
        job->image.w, (page.height - page.top - page.bottom - 24) /
        job->image.h));
    image = (tiku_rich_image_t){(unsigned)job->image.w,
        (unsigned)job->image.h, job->image.px,
        (float)fmax(1, job->image.w * scale),
        (float)fmax(1, job->image.h * scale), ""};
    command.value.image = &image;
    status = tiku_richsession_execute(tiku_richview_session(app->view),
                                       job->generation, &command);
    write_changed(app, job->generation);
    write_refused(app, status);
}

/** @brief Borrow the image covered exactly by the current selection. */
static const tiku_rich_image_t *
selected(write_app_t *app, const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_pos_t first, last;
    tiku_rich_paragraph_t p;
    size_t at = 0;

    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    if (first.paragraph != last.paragraph || last.byte - first.byte != 3)
        return NULL;
    for (size_t i = 0; i < tiku_richsnapshot_paragraphs(snapshot); i++) {
        tiku_richsnapshot_paragraph(snapshot, i, &p);
        if (p.id != first.paragraph) continue;
        for (size_t j = 0; j < p.run_count; j++) {
            if (at == first.byte) return p.runs[j].image;
            at += p.runs[j].length;
        }
        break;
    }
    return NULL;
}

/** @brief Validate and apply width or alternate-text input to one picture. */
static tiku_rich_status_t
apply(write_app_t *app, const tiku_rich_image_t *source)
{
    tiku_rich_image_t image = *source;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_IMAGE_SET};
    char *end;
    double width;

    if (app->panel == W_PANEL_PICTURE_SIZE) {
        width = strtod(app->fields[0].text, &end);
        if (!*app->fields[0].text || *end || !isfinite(width) ||
            width < 1 || width > 14400) return TIKU_RICH_INVALID;
        image.height = (float)(image.height * width / image.width);
        image.width = (float)width;
    } else image.alt = app->fields[0].text;
    command.value.image = &image;
    return tiku_richsession_execute(tiku_richview_session(app->view),
                                     0, &command);
}

void
write_picture_action(write_app_t *app, int command)
{
    tiku_richsnapshot_t *snapshot;
    const tiku_rich_image_t *image;
    tiku_rich_status_t status = tiku_richdoc_snapshot(app->doc, &snapshot);

    if (status != TIKU_RICH_OK) { write_refused(app, status); return; }
    image = selected(app, snapshot);
    if (!image) {
        snprintf(app->status, sizeof app->status,
                 "Select one picture with Shift+arrow or by dragging.");
    } else if (command == W_APPLY) {
        status = apply(app, image);
        if (status == TIKU_RICH_OK) {
            app->panel = 0;
            write_controls(app);
            tiku_dialog_focus(&app->dialog, W_BODY);
        } else write_refused(app, status);
    } else {
        char value[256];

        if (command == W_PICTURE_ALT && strlen(image->alt) >= sizeof value) {
            snprintf(app->status, sizeof app->status,
                "This picture has alternate text longer than this field "
                "supports. The existing text is retained.");
            tiku_richsnapshot_release(snapshot);
            return;
        }
        app->panel = command == W_PICTURE_SIZE ? W_PANEL_PICTURE_SIZE :
                                                W_PANEL_PICTURE_ALT;
        if (command == W_PICTURE_SIZE)
            snprintf(value, sizeof value, "%.2f", image->width);
        else snprintf(value, sizeof value, "%s", image->alt);
        tiku_field_start(&app->fields[0], value);
        tiku_field_input_init(&app->field_input[0]);
        write_search_cancel(app);
        write_controls(app);
        tiku_dialog_focus(&app->dialog, W_FIELD);
    }
    tiku_richsnapshot_release(snapshot);
    app->repaint = 1;
}
