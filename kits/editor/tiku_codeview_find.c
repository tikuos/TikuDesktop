/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_find.c - Find, Replace and Replace All from a view: the
 * search starts from the view's own selection and leaves its match
 * selected and shown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <string.h>

tiku_edit_status_t
tiku_codeview_find(tiku_codeview_t *view, const tiku_edit_query_t *q,
                   int backward, tiku_codeview_found_t *out)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_range_t match;
    tiku_edit_status_t st;
    int wrapped = 0;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    st = tiku_edit_find(view->doc, q,
                        backward ? (anchor < caret ? anchor : caret) : caret,
                        backward, &match, &wrapped);
    if (st != TIKU_EDIT_OK)
        return st;
    st = tiku_codeview_select(view, match.start, match.end);
    if (st != TIKU_EDIT_OK)
        return st;
    tiku_codeview_reveal(view);
    if (out != NULL) {
        out->found = 1;
        out->wrapped = wrapped;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_replace(tiku_codeview_t *view, const tiku_edit_query_t *q,
                      const char *with, size_t length,
                      tiku_codeview_found_t *out)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_range_t selected;
    tiku_edit_status_t st;
    int replaced = 0;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    selected.start = anchor < caret ? anchor : caret;
    selected.end = anchor < caret ? caret : anchor;
    if (tiku_edit_is_match(view->doc, q, selected)) {
        st = tiku_codeview_insert(view, with, length, 0);
        if (st != TIKU_EDIT_OK)
            return st;
        replaced = 1;
    }
    st = tiku_codeview_find(view, q, 0, out);
    if (out != NULL)
        out->replaced = (size_t)replaced;
    if (replaced && st == TIKU_EDIT_NOTHING)
        return TIKU_EDIT_OK;
    return st;
}

tiku_edit_status_t
tiku_codeview_replace_all(tiku_codeview_t *view, const tiku_edit_query_t *q,
                          const char *with, size_t length,
                          tiku_codeview_found_t *out)
{
    tiku_edit_status_t st;
    size_t count = 0;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_edit_replace_all(view->doc, q, with, length, view->view,
                               &count);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    if (out != NULL)
        out->replaced = count;
    return st;
}
