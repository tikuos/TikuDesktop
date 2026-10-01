/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_search.h - finding literal text in an editor document.
 *
 * A match lies within one line.  Each line is read from its start, left
 * to right, and a match is taken whole before the next is looked for, so
 * matches never overlap and the same ones are found whichever way a
 * search goes.  Without TIKU_EDIT_MATCH_CASE only ASCII letters fold.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EDIT_SEARCH_H_
#define TIKU_EDIT_SEARCH_H_

#include <stddef.h>

#include "tiku_editdoc.h"

/* Letters match only in the case they are written in. */
#define TIKU_EDIT_MATCH_CASE 0x1u
/* A match is neither preceded nor followed by a word byte: an ASCII
 * letter, a digit, '_' or any byte past ASCII. */
#define TIKU_EDIT_WHOLE_WORD 0x2u

/** @brief What to look for: valid UTF-8, not empty, with no NUL, CR or
 *         LF; anything else is refused with TIKU_EDIT_INVALID_TEXT. */
typedef struct {
    const char *text;
    size_t length;
    unsigned flags;             /* TIKU_EDIT_MATCH_CASE, _WHOLE_WORD  */
} tiku_edit_query_t;

/**
 * @brief The first match starting at or after @p from, or with
 *        @p backward the last one starting before it.
 *
 * With none that way the search starts again from the other end, and
 * @p wrapped (when given) is set.  TIKU_EDIT_NOTHING when there is no
 * match anywhere.
 */
tiku_edit_status_t tiku_edit_find(tiku_editdoc_t *doc,
                                  const tiku_edit_query_t *q,
                                  tiku_edit_pos_t from, int backward,
                                  tiku_edit_range_t *match, int *wrapped);

/** @brief Whether @p range holds exactly one match of @p q. */
int tiku_edit_is_match(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                       tiku_edit_range_t range);

/**
 * @brief Replace every match with @p length bytes of @p with, as one
 *        undo step that names @p view as its origin.
 *
 * @p count, when given, takes the number replaced.  TIKU_EDIT_NOTHING
 * when there is no match; any failure changes nothing.
 */
tiku_edit_status_t tiku_edit_replace_all(tiku_editdoc_t *doc,
                                         const tiku_edit_query_t *q,
                                         const char *with, size_t length,
                                         tiku_edit_view_t view,
                                         size_t *count);

#endif /* TIKU_EDIT_SEARCH_H_ */
