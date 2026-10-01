/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview.c - a view's life: its document, its selection, how it
 * is set up and where it is scrolled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <string.h>

/** @brief Keep the first row on the text it showed when a change lands
 *         above it, or on the change when the change covers it.  A
 *         change ends a drag and the x vertical moves were keeping. */
static void
heard(void *ctx, tiku_editdoc_t *doc, const tiku_edit_change_t *change)
{
    tiku_codeview_t *v = ctx;
    tiku_edit_range_t before, after;
    int64_t first, last, delta;

    v->goal = -1;
    v->dragging = 0;
    if (change->count == 0)
        return;
    if (change->cause == TIKU_EDIT_LOADED) {
        v->top = 0;
        v->left = 0;
        return;
    }
    tiku_edit_change_span(change, 0, &before, &after);
    first = tiku_editdoc_line_of(doc, after.start);
    tiku_edit_change_span(change, change->count - 1, &before, &after);
    delta = change->lines_after - change->lines_before;
    last = tiku_editdoc_line_of(doc, after.end) - delta;
    if (last < v->top)
        v->top += delta;
    else if (first < v->top)
        v->top = first;
    tiku_cv_clamp(v);
}

tiku_edit_status_t
tiku_codeview_create(tiku_editdoc_t *doc, tiku_codeview_t **view)
{
    tiku_codeview_t *v;
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    *view = NULL;
    if (doc == NULL)
        return TIKU_EDIT_INVALID;
    v = tiku_ed_alloc(sizeof *v);
    if (v == NULL)
        return TIKU_EDIT_NO_MEMORY;
    memset(v, 0, sizeof *v);
    v->doc = doc;
    v->tab_width = TIKU_CODEVIEW_TAB_DEFAULT;
    v->focused = 1;
    v->goal = -1;
    st = tiku_editdoc_view_attach(doc, &v->view);
    if (st == TIKU_EDIT_OK) {
        st = tiku_editdoc_listen(doc, heard, v, &v->listener);
        if (st != TIKU_EDIT_OK)
            tiku_editdoc_view_detach(doc, v->view);
    }
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(v);
        return st;
    }
    tiku_editdoc_retain(doc);
    *view = v;
    return TIKU_EDIT_OK;
}

void
tiku_codeview_destroy(tiku_codeview_t *view)
{
    if (view == NULL)
        return;
    tiku_editdoc_unlisten(view->doc, view->listener);
    tiku_editdoc_view_detach(view->doc, view->view);
    tiku_editdoc_release(view->doc);
    tiku_ed_free(view);
}

tiku_editdoc_t *
tiku_codeview_doc(const tiku_codeview_t *view)
{
    return view != NULL ? view->doc : NULL;
}

tiku_edit_view_t
tiku_codeview_doc_view(const tiku_codeview_t *view)
{
    tiku_edit_view_t none = { 0, 0 };

    return view != NULL ? view->view : none;
}

void
tiku_codeview_set_bounds(tiku_codeview_t *view, tiku_rect_t bounds)
{
    if (view == NULL)
        return;
    if (bounds.w < 0)
        bounds.w = 0;
    if (bounds.h < 0)
        bounds.h = 0;
    view->bounds = bounds;
    tiku_cv_clamp(view);
}

tiku_rect_t
tiku_codeview_bounds(const tiku_codeview_t *view)
{
    tiku_rect_t none = { 0, 0, 0, 0 };

    return view != NULL ? view->bounds : none;
}

void
tiku_codeview_set_font(tiku_codeview_t *view, const tiku_font_t *font)
{
    if (view == NULL)
        return;
    view->font = font;
    tiku_cv_clamp(view);
}

const tiku_font_t *
tiku_codeview_font(const tiku_codeview_t *view)
{
    return view != NULL ? tiku_cv_font(view) : NULL;
}

tiku_edit_status_t
tiku_codeview_set_tab_width(tiku_codeview_t *view, int columns)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (columns < 1 || columns > TIKU_CODEVIEW_TAB_MAX)
        return TIKU_EDIT_LIMIT;
    view->tab_width = columns;
    return TIKU_EDIT_OK;
}

int
tiku_codeview_tab_width(const tiku_codeview_t *view)
{
    return view != NULL ? view->tab_width : 0;
}

void
tiku_codeview_set_focus(tiku_codeview_t *view, int focused)
{
    if (view != NULL)
        view->focused = focused != 0;
}

int
tiku_codeview_focused(const tiku_codeview_t *view)
{
    return view != NULL && view->focused;
}

int
tiku_codeview_row_height(const tiku_codeview_t *view)
{
    if (view == NULL)
        return 0;
    return tiku_cv_font(view)->height + TIKU_CV_ROW_GAP;
}

int64_t
tiku_codeview_rows_visible(const tiku_codeview_t *view)
{
    int64_t rows;

    if (view == NULL)
        return 0;
    rows = (int64_t)(view->bounds.h - TIKU_CV_PAD_Y) /
           tiku_codeview_row_height(view);
    return rows > 0 ? rows : 1;
}

void
tiku_cv_clamp(tiku_codeview_t *v)
{
    int64_t last = tiku_editdoc_lines(v->doc) -
                   tiku_codeview_rows_visible(v);

    if (v->top > last)
        v->top = last;
    if (v->top < 0)
        v->top = 0;
    if (v->left < 0)
        v->left = 0;
}

int64_t
tiku_codeview_top(const tiku_codeview_t *view)
{
    return view != NULL ? view->top : 0;
}

int64_t
tiku_codeview_left(const tiku_codeview_t *view)
{
    return view != NULL ? view->left : 0;
}

void
tiku_codeview_scroll_to(tiku_codeview_t *view, int64_t top, int64_t left)
{
    if (view == NULL)
        return;
    view->top = top;
    view->left = left;
    tiku_cv_clamp(view);
}

tiku_edit_status_t
tiku_codeview_selection(const tiku_codeview_t *view, tiku_edit_pos_t *anchor,
                        tiku_edit_pos_t *caret)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    return tiku_editdoc_view_selection(view->doc, view->view, anchor, caret);
}

tiku_edit_status_t
tiku_codeview_select(tiku_codeview_t *view, tiku_edit_pos_t anchor,
                     tiku_edit_pos_t caret)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    view->goal = -1;
    return tiku_editdoc_view_select(view->doc, view->view, anchor, caret);
}
