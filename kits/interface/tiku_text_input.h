/* Atomic UTF-8 commits over the existing event ABI.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TEXT_INPUT_H_
#define TIKU_TEXT_INPUT_H_
#include <stddef.h>
#include "tiku_input.h"
#define TIKU_TEXT_INPUT_MAX 1024
#define TIKU_TEXT_BEGIN 1u
#define TIKU_TEXT_END 2u
#define TIKU_TEXT_REJECT 4u
typedef struct {
    char text[TIKU_TEXT_INPUT_MAX];
    int length, offset, active, total;
} tiku_text_input_t;
enum { TIKU_TEXT_IGNORED, TIKU_TEXT_PENDING, TIKU_TEXT_COMPLETE,
       TIKU_TEXT_REJECTED };

void tiku_text_input_reset(tiku_text_input_t *input);
/** @brief Append one validated single-line commit to a bounded legacy buffer.
 * Failure leaves the buffer unchanged; no truncation or control characters.
 */
int tiku_text_input_append(char *buffer, size_t capacity, const char *text);
/** @brief Remove the last UTF-8 codepoint from a bounded legacy buffer. */
void tiku_text_input_backspace(char *buffer, size_t capacity);
/** @brief Validate and stage a complete UTF-8 commit; failure leaves it empty. */
int tiku_text_input_set(tiku_text_input_t *input, const char *text, size_t bytes);
/** @brief Produce the next ABI-compatible event, preserving codepoint boundaries.
 * TEXT_INPUT: button has BEGIN/END flags; dx is byte offset, dy total bytes.
 * No key or shortcut modifiers are generated. Returns 0 when finished.
 */
int tiku_text_input_next(tiku_text_input_t *input, tiku_input_event_t *event);
/** @brief Assemble and validate a commit without exposing partial text.
 * Non-text key/button/focus events cancel an incomplete commit. COMPLETE is
 * the only result that permits insertion. Focus owners keep separate states.
 */
int tiku_text_input_event(tiku_text_input_t *input, const tiku_input_event_t *event);
#endif
