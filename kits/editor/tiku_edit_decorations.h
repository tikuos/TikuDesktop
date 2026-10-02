/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_decorations.h - marks a document keeps on its lines: line
 * markers, which follow their lines through edits, and the execution
 * line, which a change makes stale instead of moving.
 *
 * What a kind of marker means, and which line a runtime stopped at, are
 * the application's to decide; the document keeps them on its text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EDIT_DECORATIONS_H_
#define TIKU_EDIT_DECORATIONS_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_editdoc.h"

/** @brief Kinds of line marker; kind k is bit k of a set of kinds. */
#define TIKU_EDIT_MARKER_KINDS 32

/** @brief A marker on one line, valid until it is removed. */
typedef struct {
    uint32_t slot;              /* index + 1; 0 is none                 */
    uint32_t generation;        /* the slot's life it belongs to        */
} tiku_edit_marker_t;

/**
 * @brief Mark @p line with @p kind, from 0 to TIKU_EDIT_MARKER_KINDS - 1.
 *
 * A marker follows its line's text.  Text inserted at the start of the
 * line moves it down past the line breaks the text brings, and an edit
 * that removes the line break before its line moves it to the line the
 * edit starts on.  Undo does not move it back, and a load removes every
 * marker.  @p marker may be NULL.
 */
tiku_edit_status_t tiku_editdoc_marker_add(tiku_editdoc_t *doc, int64_t line,
                                           unsigned kind,
                                           tiku_edit_marker_t *marker);
/** @brief The line @p marker is on now, or -1 once it is removed. */
int64_t tiku_editdoc_marker_line(const tiku_editdoc_t *doc,
                                 tiku_edit_marker_t marker);
/** @brief Remove @p marker; its handle stops working. */
void tiku_editdoc_marker_remove(tiku_editdoc_t *doc,
                                tiku_edit_marker_t marker);
/** @brief The kinds marked on @p line as a set; 0 for a line that is not
 *         there. */
uint32_t tiku_editdoc_markers_on(const tiku_editdoc_t *doc, int64_t line);
/** @brief The first line at or after @p from marked with a kind in
 *         @p kinds, or -1. */
int64_t tiku_editdoc_markers_next(const tiku_editdoc_t *doc, int64_t from,
                                  uint32_t kinds);
/** @brief Remove the markers of @p kinds on @p line, or on every line when
 *         @p line is -1.  @return how many were removed. */
size_t tiku_editdoc_markers_clear(tiku_editdoc_t *doc, int64_t line,
                                  uint32_t kinds);

/**
 * @brief Mark @p line as the line a runtime stopped at, for the text as
 *        it is now.
 *
 * The mark is never moved.  Any edit or load makes it stale, and an undo
 * or redo back to the text it was set for makes it current again.
 */
tiku_edit_status_t tiku_editdoc_execution_set(tiku_editdoc_t *doc,
                                              int64_t line);
/** @brief Remove the execution mark. */
void tiku_editdoc_execution_clear(tiku_editdoc_t *doc);
/** @brief The execution line, in @p line: TIKU_EDIT_OK while it is
 *         current, TIKU_EDIT_STALE while it is stale, and
 *         TIKU_EDIT_NOTHING, @p line -1, when none is set. */
tiku_edit_status_t tiku_editdoc_execution(const tiku_editdoc_t *doc,
                                          int64_t *line);

#endif /* TIKU_EDIT_DECORATIONS_H_ */
