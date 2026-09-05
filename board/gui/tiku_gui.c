/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gui.c - the forms as a dynamic directory under /gui.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static tiku_gui_form_t *forms[TIKU_GUI_FORMS];

/**
 * @brief Split "<form>/<row>" into the form and the row's name.
 * @return the form, or NULL; @p *rest is "" for the form itself.
 */
static tiku_gui_form_t *
locate(const char *name, const char **rest)
{
    const char *slash = strchr(name, '/');
    size_t len = slash ? (size_t)(slash - name) : strlen(name);
    uint8_t i;

    *rest = slash ? slash + 1 : "";
    for (i = 0; i < TIKU_GUI_FORMS; i++) {
        if (forms[i] != NULL && strlen(forms[i]->name) == len &&
            memcmp(forms[i]->name, name, len) == 0) {
            return forms[i];
        }
    }
    return NULL;
}

/** @brief The row of @p form named @p name, or NULL. */
static tiku_gui_row_t *
row_of(tiku_gui_form_t *form, const char *name)
{
    uint8_t i;

    for (i = 0; i < form->count; i++) {
        if (strcmp(form->row[i].name, name) == 0) {
            return &form->row[i];
        }
    }
    return NULL;
}

/** @brief The panel text the desktop reads: a row per line. */
static int
render_panel(const tiku_gui_form_t *form, char *buf, size_t max)
{
    size_t used = 0;
    uint8_t i;

    used += (size_t)snprintf(buf, max, "title %s\n", form->title);
    for (i = 0; i < form->count && used < max; i++) {
        const tiku_gui_row_t *r = &form->row[i];
        char *at = buf + used;
        size_t room = max - used;

        switch (r->kind) {
        case TIKU_GUI_TEXT:
            /* Bound rather than baked: the desktop re-reads the row, so
             * text the process changes is seen to change. */
            used += (size_t)snprintf(at, room, "text /gui/%s/%s\n",
                                     form->name, r->name);
            break;
        case TIKU_GUI_GAUGE:
            used += (size_t)snprintf(at, room, "gauge %s /gui/%s/%s %d %d\n",
                                     r->label, form->name, r->name, r->lo,
                                     r->hi);
            break;
        case TIKU_GUI_TOGGLE:
            used += (size_t)snprintf(at, room, "toggle %s /gui/%s/%s\n",
                                     r->label, form->name, r->name);
            break;
        case TIKU_GUI_FIELD:
            used += (size_t)snprintf(at, room, "entry %s /gui/%s/%s\n",
                                     r->label, form->name, r->name);
            break;
        case TIKU_GUI_SLIDER:
            used += (size_t)snprintf(at, room,
                                     "slider %s /gui/%s/%s %d %d\n",
                                     r->label, form->name, r->name, r->lo,
                                     r->hi);
            break;
        case TIKU_GUI_CHOICE:
            used += (size_t)snprintf(at, room, "choice %s /gui/%s/%s %s\n",
                                     r->label, form->name, r->name,
                                     r->options);
            break;
        case TIKU_GUI_BUTTON:
            used += (size_t)snprintf(at, room, "button %s /gui/%s/%s 1\n",
                                     r->label, form->name, r->name);
            break;
        }
    }
    return (int)used;
}

/** @brief A row's value as text. */
static int
render_row(const tiku_gui_row_t *r, char *buf, size_t max)
{
    switch (r->kind) {
    case TIKU_GUI_GAUGE:
    case TIKU_GUI_TOGGLE:
    case TIKU_GUI_SLIDER:
        return snprintf(buf, max, "%d\n", r->value ? *r->value : 0);
    case TIKU_GUI_BUTTON:
        return snprintf(buf, max, "%lu\n", (unsigned long)r->presses);
    default:
        return snprintf(buf, max, "%s\n", r->text ? r->text : "");
    }
}

/** @brief Store @p len bytes of @p buf into @p r.  @return 0 or -1. */
static int
take(tiku_gui_row_t *r, const char *buf, size_t len)
{
    char word[24];
    size_t n = (len < sizeof word - 1) ? len : sizeof word - 1;

    memcpy(word, buf, n);
    word[n] = '\0';
    switch (r->kind) {
    case TIKU_GUI_CHOICE: {
        /* One of the words on offer, and no other. */
        const char *at = r->options;
        size_t wl = strlen(word);

        while (at != NULL && *at != '\0') {
            const char *bar = strchr(at, '|');
            size_t ol = bar ? (size_t)(bar - at) : strlen(at);

            if (ol == wl && memcmp(at, word, wl) == 0 && wl < r->text_max) {
                memcpy(r->text, word, wl + 1);
                return 0;
            }
            at = bar ? bar + 1 : NULL;
        }
        return -1;
    }
    case TIKU_GUI_GAUGE:
    case TIKU_GUI_SLIDER: {
        int v = atoi(word);

        if (v < r->lo || v > r->hi) {
            return -1;
        }
        *r->value = v;
        return 0;
    }
    case TIKU_GUI_TOGGLE:
        *r->value = (strcmp(word, "1") == 0 || strcmp(word, "on") == 0 ||
                     strcmp(word, "true") == 0);
        return 0;
    case TIKU_GUI_FIELD:
        if (len >= r->text_max) {
            return -1;
        }
        memcpy(r->text, buf, len);
        r->text[len] = '\0';
        return 0;
    case TIKU_GUI_BUTTON:
        r->presses++;
        return 0;
    default:
        return -1;
    }
}

/** @brief The forms as "<name>/", or one form's "panel" and rows. */
static void
gui_list_dir(const char *prefix, tiku_vfs_dyn_list_cb cb, void *ctx)
{
    char entry[TIKU_GUI_NAME + 1];
    uint8_t i;

    if (prefix[0] == '\0') {
        for (i = 0; i < TIKU_GUI_FORMS; i++) {
            if (forms[i] != NULL) {
                snprintf(entry, sizeof entry, "%s/", forms[i]->name);
                cb(entry, ctx);
            }
        }
    } else {
        const char *rest;
        tiku_gui_form_t *form = locate(prefix, &rest);

        if (form == NULL || rest[0] != '\0') {
            return;
        }
        cb("panel", ctx);
        for (i = 0; i < form->count; i++) {
            cb(form->row[i].name, ctx);
        }
    }
}

/** @brief The flat listing, for a host that knows no sub-folders. */
static void
gui_list(tiku_vfs_dyn_list_cb cb, void *ctx)
{
    gui_list_dir("", cb, ctx);
}

/** @brief Read "<form>/panel" or "<form>/<row>". */
static int
gui_read(const char *name, char *buf, size_t max)
{
    const char *rest;
    tiku_gui_form_t *form = locate(name, &rest);
    tiku_gui_row_t *r;

    if (form == NULL || rest[0] == '\0') {
        return -1;
    }
    if (strcmp(rest, "panel") == 0) {
        return render_panel(form, buf, max);
    }
    r = row_of(form, rest);
    return r ? render_row(r, buf, max) : -1;
}

/**
 * @brief Write a row: store the value here, in the shell's context, and
 *        post the row to its process, whose thread does the rest.
 */
static int
gui_write(const char *name, const char *buf, size_t len)
{
    const char *rest;
    tiku_gui_form_t *form = locate(name, &rest);
    tiku_gui_row_t *r;

    if (form == NULL || rest[0] == '\0') {
        return -1;
    }
    r = row_of(form, rest);
    if (r == NULL || take(r, buf, len) != 0) {
        return -1;
    }
    if (form->owner != NULL) {
        (void)tiku_process_post(form->owner, TIKU_EVENT_GUI, r);
    }
    return 0;
}

static const tiku_vfs_dynops_t gui_dynops = {
    gui_list, gui_read, gui_write, NULL, gui_list_dir
};

static const tiku_vfs_node_t gui_node = {
    "gui", TIKU_VFS_DIR, NULL, NULL, NULL, 0, NULL, &gui_dynops, 0
};

const tiku_vfs_node_t *
tiku_gui_vfs_get(void)
{
    return &gui_node;
}

void
tiku_gui_form_init(tiku_gui_form_t *form, const char *name,
                   const char *title, struct tiku_process *owner)
{
    memset(form, 0, sizeof *form);
    form->name = name;
    form->title = title;
    form->owner = owner;
}

/** @brief The next free row of @p form, named and kinded, or NULL. */
static tiku_gui_row_t *
next_row(tiku_gui_form_t *form, const char *name, const char *label,
         tiku_gui_kind_t kind)
{
    tiku_gui_row_t *r;

    if (form->count >= TIKU_GUI_ROWS || strlen(name) >= TIKU_GUI_NAME ||
        strchr(name, '/') != NULL) {
        return NULL;
    }
    r = &form->row[form->count++];
    memset(r, 0, sizeof *r);
    r->name = name;
    r->label = label;
    r->kind = kind;
    r->form = form;
    return r;
}

tiku_gui_row_t *
tiku_gui_text(tiku_gui_form_t *form, const char *name, const char *text)
{
    tiku_gui_row_t *r = next_row(form, name, text, TIKU_GUI_TEXT);

    if (r != NULL) {
        r->text = (char *)text;
    }
    return r;
}

tiku_gui_row_t *
tiku_gui_gauge(tiku_gui_form_t *form, const char *name, const char *label,
               int *value, int lo, int hi)
{
    tiku_gui_row_t *r = next_row(form, name, label, TIKU_GUI_GAUGE);

    if (r != NULL) {
        r->value = value;
        r->lo = lo;
        r->hi = hi;
    }
    return r;
}

tiku_gui_row_t *
tiku_gui_toggle(tiku_gui_form_t *form, const char *name, const char *label,
                int *value, tiku_gui_changed_fn changed)
{
    tiku_gui_row_t *r = next_row(form, name, label, TIKU_GUI_TOGGLE);

    if (r != NULL) {
        r->value = value;
        r->changed = changed;
    }
    return r;
}

tiku_gui_row_t *
tiku_gui_field(tiku_gui_form_t *form, const char *name, const char *label,
               char *text, size_t max, tiku_gui_changed_fn changed)
{
    tiku_gui_row_t *r = next_row(form, name, label, TIKU_GUI_FIELD);

    if (r != NULL) {
        r->text = text;
        r->text_max = max;
        r->changed = changed;
    }
    return r;
}

tiku_gui_row_t *
tiku_gui_slider(tiku_gui_form_t *form, const char *name, const char *label,
                int *value, int lo, int hi, tiku_gui_changed_fn changed)
{
    tiku_gui_row_t *r = next_row(form, name, label, TIKU_GUI_SLIDER);

    if (r != NULL) {
        r->value = value;
        r->lo = lo;
        r->hi = hi;
        r->changed = changed;
    }
    return r;
}

tiku_gui_row_t *
tiku_gui_choice(tiku_gui_form_t *form, const char *name, const char *label,
                const char *options, char *text, size_t max,
                tiku_gui_changed_fn changed)
{
    tiku_gui_row_t *r = next_row(form, name, label, TIKU_GUI_CHOICE);

    if (r != NULL) {
        r->options = options;
        r->text = text;
        r->text_max = max;
        r->changed = changed;
    }
    return r;
}

tiku_gui_row_t *
tiku_gui_button(tiku_gui_form_t *form, const char *name, const char *label,
                tiku_gui_changed_fn changed)
{
    tiku_gui_row_t *r = next_row(form, name, label, TIKU_GUI_BUTTON);

    if (r != NULL) {
        r->changed = changed;
    }
    return r;
}

int
tiku_gui_add(tiku_gui_form_t *form)
{
    uint8_t i;

    if (form->count == 0 || strlen(form->name) >= TIKU_GUI_NAME) {
        return -1;
    }
    for (i = 0; i < TIKU_GUI_FORMS; i++) {
        if (forms[i] == form) {
            return 0;
        }
    }
    for (i = 0; i < TIKU_GUI_FORMS; i++) {
        if (forms[i] == NULL) {
            forms[i] = form;
            tiku_gui_changed(&form->row[0]);
            return 0;
        }
    }
    return -1;
}

void
tiku_gui_remove(tiku_gui_form_t *form)
{
    uint8_t i;

    for (i = 0; i < TIKU_GUI_FORMS; i++) {
        if (forms[i] == form) {
            forms[i] = NULL;
        }
    }
}

void
tiku_gui_changed(tiku_gui_row_t *row)
{
    /* The rows have no node of their own: the directory rings, and a
     * host re-reads what it shows of it. */
    const tiku_vfs_node_t *live = tiku_vfs_resolve("/gui");

    (void)row;
    if (live != NULL) {
        tiku_vfs_notify(live);
    }
}

tiku_gui_row_t *
tiku_gui_event(tiku_event_t ev, tiku_event_data_t data)
{
    tiku_gui_row_t *r = (ev == TIKU_EVENT_GUI) ? (tiku_gui_row_t *)data
                                               : NULL;

    if (r != NULL && r->changed != NULL) {
        r->changed(r);
    }
    return r;
}
