/* Optional rich interaction for the small, existing field model. */
#ifndef TIKU_FIELD_EVENT_H_
#define TIKU_FIELD_EVENT_H_
#include "tiku_field.h"
#include "tiku_event.h"
#include "tiku_input.h"
#include "tiku_geom.h"
#include "tiku_text_input.h"
#define TIKU_FIELD_HISTORY 4
typedef struct {
    char text[TIKU_FIELD_MAX];
    int caret, a, b;
} tiku_field_snapshot_t;
typedef struct {
    tiku_field_snapshot_t undo[TIKU_FIELD_HISTORY], redo[TIKU_FIELD_HISTORY];
    int nundo, nredo, dragging, anchor, scroll;
    tiku_text_input_t commit;
} tiku_field_input_t;
/** @brief Reset history and pointer state after loading a different value. */
void tiku_field_input_init(tiku_field_input_t *input);
/** @brief Shared input; commits/cancels are reported without deactivating the model.
 * Inserts/pastes and assembled text commits are atomic and UTF-8 codepoint-safe
 * (not grapheme editing or inline composition).
 * Oversized or invalid input is refused without changing selection or text.
 * Route pointer move/up here while dragging, even outside the field.
 */
tiku_field_result_t tiku_field_event(tiku_field_t *field,
    tiku_field_input_t *input, const tiku_input_event_t *ev,
    tiku_rect_t rect, int focused);
#endif
