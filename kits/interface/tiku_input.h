/* Platform-neutral UI input, distinct from the kernel's byte-sized event ID. */
#ifndef TIKU_INPUT_H_
#define TIKU_INPUT_H_
#include <stdint.h>

typedef enum {
    TIKU_EVENT_NONE = 0,
    TIKU_EVENT_EXPOSE,
    TIKU_EVENT_CLOSE,
    TIKU_EVENT_RESIZE,
    TIKU_EVENT_KEY_DOWN,
    TIKU_EVENT_KEY_UP,
    TIKU_EVENT_POINTER_DOWN,
    TIKU_EVENT_POINTER_UP,
    TIKU_EVENT_POINTER_MOVE,
    TIKU_EVENT_WHEEL,
    TIKU_EVENT_ACTIVATED,
    TIKU_EVENT_DEACTIVATED,
    TIKU_EVENT_TICK,
    TIKU_EVENT_CLIPBOARD,
    TIKU_EVENT_TEXT_INPUT
} tiku_event_type_t;

typedef struct {
    tiku_event_type_t type;
    int64_t                time_us;
    unsigned               modifiers;
    unsigned               key;
    unsigned               button;
    unsigned               repeat;
    char                   text[8];
    int                    x, y;
    int                    dx, dy;
    int                    width, height;
} tiku_input_event_t;
#endif
