/* TikuOS interface kit. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_PROPERTIES_H_
#define TIKU_PROPERTIES_H_
#include "tiku_dialog.h"
#define TIKU_PROPERTIES_MAX 32
#define TIKU_PROPERTY_UNAVAILABLE 1u
#define TIKU_PROPERTY_HEADING 2u

typedef struct {
    const char *label, *value; /* Borrowed; keep alive through drawing. */
    unsigned flags;
} tiku_property_t;
typedef struct {
    tiku_property_t row[TIKU_PROPERTIES_MAX];
    int count, top, selected, row_h; /* row_h == 0 uses font metrics. */
} tiku_properties_t;

void tiku_properties_init(tiku_properties_t *p);
/** @brief Append a borrowed label/value pair; -1 refuses overflow. */
int tiku_properties_add(tiku_properties_t *p, const char *label,
                         const char *value, unsigned flags);
int tiku_properties_row_height(const tiku_properties_t *p);
int tiku_properties_visible(const tiku_properties_t *p, tiku_rect_t body);
/** @brief Visible complete rows only; others return an empty rectangle. */
tiku_rect_t tiku_properties_row(const tiku_properties_t *p,
                                 tiku_rect_t body, int row);
/** @brief Return an ordinary row index, excluding headings and bevels. */
int tiku_properties_at(const tiku_properties_t *p, tiku_rect_t body, int x, int y);
/** @brief Font-measured label column, capped at half the inner width. */
int tiku_properties_label_width(const tiku_properties_t *p, tiku_rect_t body);
/** @brief Select an ordinary row and scroll it into view. */
int tiku_properties_select(tiku_properties_t *p, tiku_rect_t body, int row);
/** @brief Add visible ordinary rows to an already-begun dialog.
 * IDs are base + row. Unavailable values remain selectable for explanation.
 * Returns 0 on success, -1 on invalid IDs or insufficient dialog capacity;
 * failure leaves the dialog unchanged. Read-only values are ordinary rows.
 */
int tiku_properties_controls(const tiku_properties_t *p, tiku_rect_t body,
                              tiku_dialog_t *dialog, int base, int enabled);
#endif
