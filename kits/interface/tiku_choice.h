/* Bounded popup choices: behavior only, usable without a display. */
#ifndef TIKU_CHOICE_H_
#define TIKU_CHOICE_H_
#include "tiku_event.h"
#include "tiku_input.h"
#include "tiku_geom.h"
#define TIKU_CHOICE_MAX 32
#define TIKU_CHOICE_LABEL 64
typedef struct {
    char label[TIKU_CHOICE_LABEL];
    int enabled;
} tiku_choice_item_t;
typedef struct {
    tiku_choice_item_t item[TIKU_CHOICE_MAX];
    int count, selected, hot, top, open, enabled, pressed;
} tiku_choice_t;
typedef enum { TIKU_CHOICE_IGNORED, TIKU_CHOICE_HANDLED,
               TIKU_CHOICE_CHANGED } tiku_choice_result_t;
/** @brief Reset choices and selection; enable the control. */
void tiku_choice_init(tiku_choice_t *c);
/** @brief Append a labelled option; refuse overflow rather than truncate. */
int tiku_choice_add(tiku_choice_t *c, const char *label, int enabled);
/** @brief Select a valid enabled option, closing any popup. */
int tiku_choice_select(tiku_choice_t *c, int index);
/** @brief Close transient state without changing the selected value. */
void tiku_choice_close(tiku_choice_t *c);
/** @brief Place a bounded popup above/below its anchor, inside the screen. */
tiku_rect_t tiku_choice_popup(const tiku_choice_t *c, tiku_rect_t anchor,
                              tiku_rect_t screen);
/** @brief Input changes selection only; never invokes an application action. */
tiku_choice_result_t tiku_choice_event(tiku_choice_t *c,
    const tiku_input_event_t *ev, tiku_rect_t anchor, tiku_rect_t screen, int focused);
#endif
