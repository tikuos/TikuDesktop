/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gui.h - a process declares a form; the desktop draws it and writes
 * what is pressed back.
 *
 * A form is up to sixteen rows, each bound to the process's own storage:
 * a text line, a gauge over an int, a toggle over an int, a field over a
 * char buffer, a button that counts.  Adding the form publishes it under
 * /gui/<form>/: a "panel" node the desktop reads as a described panel,
 * and one node per row.  A write to a row's node, which arrives in the
 * shell's context, stores the value and posts TIKU_EVENT_GUI to the
 * owning process with the row as the payload, so the process's own
 * callback runs in its own thread.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GUI_H_
#define TIKU_GUI_H_

#include "tiku.h"
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tiku_vfs.h>

#ifndef TIKU_GUI_FORMS
#define TIKU_GUI_FORMS 2            /**< forms the library can publish */
#endif
#define TIKU_GUI_ROWS  16           /**< rows per form, the panel's limit */
#define TIKU_GUI_NAME  16           /**< a form or row name, with terminator */

/** @brief The event a row's write posts; data is the tiku_gui_row_t*. */
#define TIKU_EVENT_GUI (TIKU_EVENT_USER + 0x40)

typedef enum {
    TIKU_GUI_TEXT,                  /**< a line of text, read only */
    TIKU_GUI_GAUGE,                 /**< an int between lo and hi */
    TIKU_GUI_TOGGLE,                /**< an int, 0 or 1 */
    TIKU_GUI_FIELD,                 /**< a string the person may type */
    TIKU_GUI_BUTTON,                /**< a press, counted */
    TIKU_GUI_SLIDER,                /**< an int the person may set, lo..hi */
    TIKU_GUI_CHOICE                 /**< one word of a few, the person's */
} tiku_gui_kind_t;

struct tiku_gui_row;

/** @brief Called from the owning process's thread after a row changed. */
typedef void (*tiku_gui_changed_fn)(struct tiku_gui_row *row);

typedef struct tiku_gui_row {
    const char          *name;      /**< the node's name, at most 15 chars */
    const char          *label;     /**< what the desktop draws beside it */
    tiku_gui_kind_t      kind;
    int                  lo, hi;    /**< a gauge's range */
    int                 *value;     /**< a gauge's or toggle's storage */
    char                *text;      /**< a text row's or field's storage */
    size_t               text_max;  /**< the field buffer's size */
    const char          *options;   /**< a choice's words, | between */
    uint32_t             presses;   /**< a button's count */
    tiku_gui_changed_fn  changed;   /**< optional, run by tiku_gui_event() */
    struct tiku_gui_form *form;
} tiku_gui_row_t;

typedef struct tiku_gui_form {
    const char          *name;      /**< the directory under /gui */
    const char          *title;     /**< the panel's title row */
    struct tiku_process *owner;     /**< receives TIKU_EVENT_GUI */
    tiku_gui_row_t       row[TIKU_GUI_ROWS];
    uint8_t              count;
} tiku_gui_form_t;

/** @brief The /gui node for the VFS root; children are the forms added. */
const tiku_vfs_node_t *tiku_gui_vfs_get(void);

/** @brief Start @p form empty, owned by @p owner. */
void tiku_gui_form_init(tiku_gui_form_t *form, const char *name,
                        const char *title, struct tiku_process *owner);

/** @brief A line of text; @p text is borrowed, and a change to it is
 *         shown once tiku_gui_changed() is called for the row. */
tiku_gui_row_t *tiku_gui_text(tiku_gui_form_t *form, const char *name,
                              const char *text);

/** @brief A gauge over @p *value, drawn between @p lo and @p hi. */
tiku_gui_row_t *tiku_gui_gauge(tiku_gui_form_t *form, const char *name,
                               const char *label, int *value, int lo,
                               int hi);

/** @brief A toggle over @p *value; @p changed hears every flip. */
tiku_gui_row_t *tiku_gui_toggle(tiku_gui_form_t *form, const char *name,
                                const char *label, int *value,
                                tiku_gui_changed_fn changed);

/** @brief A field over @p text of @p max bytes; @p changed hears a set. */
tiku_gui_row_t *tiku_gui_field(tiku_gui_form_t *form, const char *name,
                               const char *label, char *text, size_t max,
                               tiku_gui_changed_fn changed);

/** @brief A slider over @p *value between @p lo and @p hi, set by the
 *         person; @p changed hears every move. */
tiku_gui_row_t *tiku_gui_slider(tiku_gui_form_t *form, const char *name,
                                const char *label, int *value, int lo,
                                int hi, tiku_gui_changed_fn changed);

/**
 * @brief A choice of the words in @p options ("slow|fast"), the chosen
 *        one kept in @p text of @p max bytes; @p changed hears a pick.
 */
tiku_gui_row_t *tiku_gui_choice(tiku_gui_form_t *form, const char *name,
                                const char *label, const char *options,
                                char *text, size_t max,
                                tiku_gui_changed_fn changed);

/** @brief A button; @p changed hears every press, and presses counts. */
tiku_gui_row_t *tiku_gui_button(tiku_gui_form_t *form, const char *name,
                                const char *label,
                                tiku_gui_changed_fn changed);

/**
 * @brief Publish @p form under /gui/<name>/.
 * @return 0, or -1 when the table is full or the form has no rows.
 */
int tiku_gui_add(tiku_gui_form_t *form);

/** @brief Withdraw @p form; its nodes are gone on the next listing. */
void tiku_gui_remove(tiku_gui_form_t *form);

/** @brief The process moved a row's value itself: tell the watchers. */
void tiku_gui_changed(tiku_gui_row_t *row);

/**
 * @brief From the owning process's thread: for TIKU_EVENT_GUI, run the
 *        row's callback and return the row; NULL for any other event.
 */
tiku_gui_row_t *tiku_gui_event(tiku_event_t ev, tiku_event_data_t data);

#endif /* TIKU_GUI_H_ */
