/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_syntax.h - one line of a program, told apart into what its parts
 * mean.
 *
 * Given a line, this says which stretches are reserved words, numbers,
 * quoted text and remarks; it paints nothing and hands back a table of
 * tiku_span_t for a painter to colour through the theme.  The one
 * language is TikuOS BASIC, known from the interpreter's own tables, so
 * the highlighting cannot lie.  BASIC classifies line by line; C would
 * need to carry whether a line began inside a remark, which
 * tiku_syntax_spans_on() takes and returns.  doc/syntax.md has the
 * reasoning.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SYNTAX_H_
#define TIKU_SYNTAX_H_

#include "tiku_gfx.h"

/** @brief The languages this build can tell apart. */
typedef enum {
    TIKU_SYNTAX_NONE = 0,   /* prose: every byte is the document's ink */
    TIKU_SYNTAX_BASIC,      /* TikuOS BASIC                            */
    TIKU_SYNTAX_C           /* C, which carries a remark between lines */
} tiku_syntax_lang_t;

/**
 * @brief What a line was left in the middle of.
 *
 * The whole of what one line of C tells the next.  It is small on
 * purpose: a state that grew to hold nesting, or a string continued by
 * a backslash, would be a parser pretending to be a highlighter.
 */
typedef unsigned tiku_syntax_state_t;

#define TIKU_SYNTAX_OPEN   0u   /* nothing carried in                  */
#define TIKU_SYNTAX_BLOCK  1u   /* a block remark is still open        */

/**
 * @brief Tell @p line apart into runs of meaning, into @p out.
 *
 * Runs are returned in order and cover the line from its first byte; a
 * caller paints them in order and gives whatever they do not reach the
 * plain ink, so a table that stops short is a shorter answer and never a
 * wrong one.  Adjacent plain stretches are merged, which is what keeps
 * an ordinary line to one run.
 *
 * @param max How many runs @p out can hold.  Past it the line simply
 *            stops being told apart -- the remainder is plain, which is
 *            legible, rather than truncated, which would not be.
 * @return the number of runs written; 0 for a NULL or empty line, and 0
 *         for TIKU_SYNTAX_NONE, which is prose by definition.
 */
int tiku_syntax_spans(tiku_syntax_lang_t lang, const char *line,
                      tiku_span_t *out, int max);

/**
 * @brief Tell @p line apart, saying what it was begun in and what it
 *        leaves behind.
 *
 * The same answer as tiku_syntax_spans() for a language that carries
 * nothing, which is why that one is still the whole of the BASIC road.
 *
 * @param in    what the line before this one left open.
 * @param out_state where this line's own leaving is written, when given.
 *              A caller painting a window walks from a line it knows the
 *              state of -- the document's first, which is OPEN -- and
 *              carries the answer forward.
 */
int tiku_syntax_spans_on(tiku_syntax_lang_t lang, const char *line,
                         tiku_syntax_state_t in,
                         tiku_syntax_state_t *out_state,
                         tiku_span_t *out, int max);

/**
 * @brief Whether @p name is a word TikuOS BASIC reserves.
 *
 * Case-insensitive, and the whole word must match -- PRINTER is not
 * PRINT.  Exposed because "is this word taken" is a question an editor
 * asks for its own reasons (a name it is about to accept, a completion
 * it is about to offer), not only while painting.
 */
int tiku_syntax_basic_word(const char *name);

/**
 * @brief The language a file called @p path is written in.
 *
 * By its suffix, because that is the only thing a name can honestly
 * say: ".bas" is BASIC, ".c" and ".h" are C, and everything else is
 * prose.  Sniffing the contents would guess, and a guess that
 * highlights an ordinary letter as a program is the lie this whole file
 * exists to avoid.
 */
tiku_syntax_lang_t tiku_syntax_of_path(const char *path);

#endif /* TIKU_SYNTAX_H_ */
