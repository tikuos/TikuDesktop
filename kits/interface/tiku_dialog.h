/* Shared dialog navigation and release-to-activate controls. */
#ifndef TIKU_DIALOG_H_
#define TIKU_DIALOG_H_
#include "tiku_event.h"
#include "tiku_input.h"
#include "tiku_geom.h"

#define TIKU_DIALOG_MAX 96
#define TIKU_DIALOG_NONE (-1)
typedef enum { TIKU_DIALOG_BUTTON, TIKU_DIALOG_TOGGLE,
               TIKU_DIALOG_INPUT } tiku_dialog_kind_t;
typedef struct {
    int id, enabled;
    tiku_dialog_kind_t kind;
    tiku_rect_t rect;
} tiku_dialog_control_t;
typedef struct {
    tiku_dialog_control_t control[TIKU_DIALOG_MAX];
    int count, focus, capture, inside, keyboard, dflt, cancel;
    int focus_at;                   /* where focus stood before a rebuild */
} tiku_dialog_t;
typedef struct { int taken, activated; } tiku_dialog_result_t;

/** @brief Initialize with no focus, capture, default or cancel action. */
void tiku_dialog_init(tiku_dialog_t *d);
/** @brief Rebuild geometry each frame/event, retaining stable focus/capture IDs. */
void tiku_dialog_begin(tiku_dialog_t *d, int dflt, int cancel);
/** @brief Register a unique nonnegative ID; return 0, or -1 if refused. */
int tiku_dialog_add(tiku_dialog_t *d, int id, tiku_rect_t rect,
                    tiku_dialog_kind_t kind, int enabled);
/** @brief Repair focus/capture after hidden or disabled controls disappear. */
void tiku_dialog_end(tiku_dialog_t *d);
/** @brief Focus an enabled registered control; return whether it exists. */
int tiku_dialog_focus(tiku_dialog_t *d, int id);
/** @brief Query keyboard focus or armed pointer state for painting. */
int tiku_dialog_focused(const tiku_dialog_t *d, int id);
int tiku_dialog_pressed(const tiku_dialog_t *d, int id);
/** @brief Route Tab, Return/Escape and button/toggle presses. Inputs keep keys. */
tiku_dialog_result_t tiku_dialog_event(tiku_dialog_t *d,
                                       const tiku_input_event_t *ev);
#endif
