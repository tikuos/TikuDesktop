/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find.c - the Find panel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_find.h"
#include "tiku_desk_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Metrics from R5-SPEC.md, not chosen here: a control that is the wrong
 * height is the thing that makes a panel read as not-quite-right even when
 * every colour is correct. */
#define PAD        8
#define ROW_H     24
#define GAP        4
#define BTN_H     24     /* button height                                 */
#define BTN_W     75     /* minimum button width                          */
#define TEXT_H    20     /* text control height                           */
#define LABEL_W   64
#define FIELD_W  120
#define OP_W     110
#define JOIN_W    52
#define VALUE_W  150

/** @brief The fields a row can test, in the order the menu offers them. */
/* Text fields offer the five string operators; measured ones offer is,
 * greater than and less than.  A field also carries the operator it STARTS
 * on, because "contains" on a size is a question nobody asks. */
#define OPS_TEXT  0x1Fu       /* contains, is, is not, starts, ends       */
#define OPS_NUM   0x62u       /* is, greater than, less than              */
#define OPS_EXACT 0x06u       /* is, is not -- for a yes/no field         */

static const struct {
    tiku_trk_qfield_t field;
    const char       *label;
    unsigned          ops;          /* bit per tiku_trk_qop_t              */
    tiku_trk_qop_t    first;
} kFields[] = {
    { TIKU_TRK_QF_NAME,     "Name",       OPS_TEXT,  TIKU_TRK_OP_CONTAINS },
    { TIKU_TRK_QF_PATH,     "Path",       OPS_TEXT,  TIKU_TRK_OP_CONTAINS },
    { TIKU_TRK_QF_KIND,     "Kind",       OPS_TEXT,  TIKU_TRK_OP_IS },
    { TIKU_TRK_QF_VALUE,    "Value",      OPS_TEXT | OPS_NUM,
                                                     TIKU_TRK_OP_IS },
    { TIKU_TRK_QF_UNIT,     "Unit",       OPS_TEXT,  TIKU_TRK_OP_IS },
    { TIKU_TRK_QF_CAP,      "Capability", OPS_TEXT,  TIKU_TRK_OP_IS },
    { TIKU_TRK_QF_SIZE,     "Size",       OPS_NUM,   TIKU_TRK_OP_IS },
    { TIKU_TRK_QF_WRITABLE, "Writable",   OPS_EXACT, TIKU_TRK_OP_IS }
};
#define NFIELDS ((int)(sizeof kFields / sizeof kFields[0]))

/** @brief Operator labels, in the original's order. */
static const char *kOps[] = {
    "contains", "is", "is not", "starts with", "ends with",
    "greater than", "less than"
};

static const char *
field_label(tiku_trk_qfield_t f)
{
    int i;

    for (i = 0; i < NFIELDS; i++) {
        if (kFields[i].field == f) {
            return kFields[i].label;
        }
    }
    return "Name";
}

/** @brief The operators @p f offers, as a bit per operator. */
static unsigned
field_ops(tiku_trk_qfield_t f)
{
    int i;

    for (i = 0; i < NFIELDS; i++) {
        if (kFields[i].field == f) {
            return kFields[i].ops;
        }
    }
    return OPS_TEXT;
}

void
tiku_trk_find_init(tiku_trk_find_t *f)
{
    if (f == NULL) {
        return;
    }
    memset(f, 0, sizeof *f);
    f->mode = TIKU_TRK_QMODE_NAME;
    f->everywhere = 1;
    f->row_count = 1;
    f->row[0].field = TIKU_TRK_QF_NAME;
    f->row[0].op = TIKU_TRK_OP_CONTAINS;
    f->row[0].join = TIKU_TRK_JOIN_AND;
    f->focus = TIKU_TRK_FIND_FOCUS_NAME;
    f->popup_for = TIKU_TRK_FIND_HIT_NONE;
}

int
tiku_trk_find_add_place(tiku_trk_find_t *f, const char *name)
{
    int i;

    if (f == NULL || name == NULL ||
        f->place_count >= TIKU_TRK_FIND_PLACES_MAX) {
        return 0;
    }
    for (i = 0; i < f->place_count; i++) {
        if (strcmp(f->place[i], name) == 0) {
            return 0;
        }
    }
    snprintf(f->place[f->place_count], TIKU_TRK_NAME_MAX, "%s", name);
    f->place_count++;
    return 1;
}

void
tiku_trk_find_set_mode(tiku_trk_find_t *f, tiku_trk_qmode_t mode)
{
    if (f == NULL || f->mode == mode) {
        return;
    }
    if (mode == TIKU_TRK_QMODE_FORMULA) {
        /* Entering formula mode shows what the controls were asking, which
         * is how the predicate language gets learned. */
        tiku_trk_query_t q;

        tiku_trk_find_build(f, &q);
        (void)tiku_trk_query_predicate(&q, f->formula, sizeof f->formula);
        f->focus = TIKU_TRK_FIND_FOCUS_FORMULA;
    } else {
        /* Leaving formula mode, the original silently discards both the
         * formula and the attribute rows.  The rows are kept here instead:
         * losing a half-built query to a mis-click is a defect, not a
         * behaviour worth reproducing. */
        f->focus = (mode == TIKU_TRK_QMODE_NAME)
                       ? TIKU_TRK_FIND_FOCUS_NAME
                       : TIKU_TRK_FIND_FOCUS_ROW;
        /* The last row, not the first: it is the one still being filled in. */
        f->focus_row = (mode == TIKU_TRK_QMODE_ATTRIBUTE)
                           ? f->row_count - 1 : 0;
    }
    f->mode = mode;
    f->popup_for = TIKU_TRK_FIND_HIT_NONE;
}

int
tiku_trk_find_add_row(tiku_trk_find_t *f)
{
    tiku_trk_find_row_t *r;

    if (f == NULL || f->row_count >= TIKU_TRK_FIND_ROWS_MAX) {
        return 0;
    }
    r = &f->row[f->row_count];
    memset(r, 0, sizeof *r);
    r->field = kFields[0].field;
    r->op = kFields[0].first;
    r->join = TIKU_TRK_JOIN_AND;
    f->focus = TIKU_TRK_FIND_FOCUS_ROW;
    f->focus_row = f->row_count;
    f->row_count++;
    return 1;
}

int
tiku_trk_find_remove_row(tiku_trk_find_t *f)
{
    /* One row is the floor: a panel with no rows asks nothing. */
    if (f == NULL || f->row_count <= 1) {
        return 0;
    }
    f->row_count--;
    if (f->focus_row >= f->row_count) {
        f->focus_row = f->row_count - 1;
    }
    return 1;
}

void
tiku_trk_find_build(const tiku_trk_find_t *f, tiku_trk_query_t *q)
{
    int i;

    if (f == NULL || q == NULL) {
        return;
    }
    tiku_trk_query_init(q);
    q->mode = f->mode;
    switch (f->mode) {
    case TIKU_TRK_QMODE_NAME:
        tiku_trk_query_set_name(q, f->name_text);
        break;
    case TIKU_TRK_QMODE_ATTRIBUTE:
        for (i = 0; i < f->row_count; i++) {
            /* The join that reaches a row is the one recorded on the row
             * BEFORE it, which is where the control sits. */
            (void)tiku_trk_query_add_term(q, f->row[i].field, f->row[i].op,
                                          f->row[i].value,
                                          (i == 0) ? TIKU_TRK_JOIN_AND
                                                   : f->row[i - 1].join);
        }
        break;
    default:
        /* A formula is not re-parsed here: the field is a display of the
         * predicate, and running it uses the terms it was built from. */
        for (i = 0; i < f->row_count; i++) {
            (void)tiku_trk_query_add_term(q, f->row[i].field, f->row[i].op,
                                          f->row[i].value,
                                          (i == 0) ? TIKU_TRK_JOIN_AND
                                                   : f->row[i - 1].join);
        }
        break;
    }
    if (!f->everywhere) {
        for (i = 0; i < f->place_count; i++) {
            if (f->place_mask & (1u << i)) {
                char path[TIKU_TRK_PATH_MAX];

                snprintf(path, sizeof path, "/%s", f->place[i]);
                (void)tiku_trk_query_add_scope(q, path);
            }
        }
    }
}

int
tiku_trk_find_scope_label(const tiku_trk_find_t *f, char *out, size_t max)
{
    int i, n = 0, last = -1;

    if (out == NULL || max == 0u) {
        return 0;
    }
    if (f == NULL || f->everywhere) {
        snprintf(out, max, "Everywhere");
        return (int)strlen(out);
    }
    for (i = 0; i < f->place_count; i++) {
        if (f->place_mask & (1u << i)) {
            n++;
            last = i;
        }
    }
    /* One place is named; several are counted.  Listing them would not fit
     * and would not be read. */
    if (n == 0) {
        snprintf(out, max, "Everywhere");
    } else if (n == 1) {
        snprintf(out, max, "/%.*s", (int)max - 2, f->place[last]);
    } else {
        snprintf(out, max, "%d places", n);
    }
    return (int)strlen(out);
}

int
tiku_trk_find_default_name(const tiku_trk_find_t *f, long stamp, char *out,
                           size_t max)
{
    char desc[96];
    char when[40];
    size_t i;
    time_t t = (time_t)stamp;
    struct tm tmv;

    if (out == NULL || max == 0u) {
        return 0;
    }
    switch (f != NULL ? f->mode : TIKU_TRK_QMODE_NAME) {
    case TIKU_TRK_QMODE_NAME:
        snprintf(desc, sizeof desc, "name contains %.40s", f->name_text);
        break;
    case TIKU_TRK_QMODE_ATTRIBUTE: {
        /* Every row is described, joined by the join that reaches it, so two
         * saved queries differing only in their second row do not end up
         * with the same name. */
        size_t o = 0;
        int k;

        desc[0] = '\0';
        for (k = 0; k < f->row_count && o + 1u < sizeof desc; k++) {
            o += (size_t)snprintf(desc + o, sizeof desc - o, "%s%s %s %.20s",
                                  (k == 0) ? ""
                                  : (f->row[k - 1].join == TIKU_TRK_JOIN_OR)
                                      ? " or " : " and ",
                                  field_label(f->row[k].field),
                                  kOps[f->row[k].op], f->row[k].value);
        }
        break;
    }
    default:
        snprintf(desc, sizeof desc, "formula");
        break;
    }
    (void)gmtime_r(&t, &tmv);
    (void)strftime(when, sizeof when, " - %b %d, %H:%M:%S", &tmv);
    snprintf(out, max, "%s%s", desc, when);
    /* Sanitise: a name is going to become a path component. */
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '/' || out[i] == '\\' || out[i] == '\n' ||
            out[i] == '\r' || out[i] == '\t') {
            out[i] = '_';
        }
    }
    return (int)strlen(out);
}

/*---------------------------------------------------------------------------*/
/* layout                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Rows of controls the current mode needs. */
static int
control_rows(const tiku_trk_find_t *f)
{
    switch (f->mode) {
    case TIKU_TRK_QMODE_ATTRIBUTE: return f->row_count;
    default:                       return 1;
    }
}

int
tiku_trk_find_height(const tiku_trk_find_t *f)
{
    if (f == NULL) {
        return 0;
    }
    /* mode row + controls + scope row + buttons */
    return PAD + ROW_H + GAP + control_rows(f) * (ROW_H + GAP) +
           ROW_H + GAP + BTN_H + 8 + PAD;
}

/** @brief Rectangle of the mode selector. */
static tiku_desk_rect_t
mode_rect(tiku_desk_rect_t f)
{
    tiku_desk_rect_t r = { f.x + PAD + LABEL_W, f.y + PAD, FIELD_W, ROW_H };

    return r;
}

/** @brief Rectangle of control row @p i. */
static tiku_desk_rect_t
row_rect(tiku_desk_rect_t f, int i)
{
    tiku_desk_rect_t r;

    r.x = f.x + PAD;
    r.y = f.y + PAD + ROW_H + GAP + i * (ROW_H + GAP);
    r.w = f.w - 2 * PAD;
    r.h = ROW_H;
    return r;
}

static tiku_desk_rect_t
scope_rect(const tiku_trk_find_t *f, tiku_desk_rect_t fr)
{
    tiku_desk_rect_t r;

    r.x = fr.x + PAD + LABEL_W;
    r.y = fr.y + PAD + ROW_H + GAP + control_rows(f) * (ROW_H + GAP);
    r.w = FIELD_W + OP_W;
    r.h = ROW_H;
    return r;
}

static tiku_desk_rect_t
button_rect(const tiku_trk_find_t *f, tiku_desk_rect_t fr, int which)
{
    tiku_desk_rect_t r;
    /* The default button's ring lives INSIDE its rectangle (the face is
     * inset by 4), so it needs the extra room rather than overlapping its
     * neighbour. */
    int w = BTN_W + ((which == 0) ? 8 : 0);

    r.y = fr.y + PAD + ROW_H + GAP + control_rows(f) * (ROW_H + GAP) +
          ROW_H + GAP;
    r.h = BTN_H + ((which == 0) ? 8 : 0);
    r.w = w;
    /* Search is the default action and sits at the right, where the eye
     * finishes; Save is beside it. */
    if (which == 0) {
        r.x = fr.x + fr.w - PAD - w;
    } else {
        r.x = fr.x + fr.w - PAD - (BTN_W + 8) - GAP - w;
        r.y += 4;                 /* centred against the ringed default   */
    }
    return r;
}

/*---------------------------------------------------------------------------*/
/* drawing                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief A menu field, from the shared control look. */
static void
draw_popup_button(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                  const char *label)
{
    tiku_desk_ui_menufield(s, r, label, TIKU_DESK_S_NORMAL);
}

static void
draw_field(tiku_desk_surface_t *s, tiku_desk_rect_t r, const char *text,
           int focused)
{
    /* The shared text control owns the caret, so it blinks and sits where
     * every other field's does. */
    tiku_desk_ui_textfield(s, r, (text != NULL) ? text : "",
                           focused ? (int)strlen((text != NULL) ? text : "")
                                   : -1,
                           focused ? TIKU_DESK_S_FOCUS : TIKU_DESK_S_NORMAL);
}

static void
draw_button(tiku_desk_surface_t *s, tiku_desk_rect_t r, const char *label,
            unsigned state)
{
    tiku_desk_ui_button(s, r, label, state);
}

static void
label_at(tiku_desk_surface_t *s, int x, tiku_desk_rect_t r, const char *text)
{
    const tiku_desk_font_t *font = tiku_desk_font_plain();

    tiku_desk_text(s, font, x, r.y + (r.h - font->height) / 2 + font->ascent,
                   text, TIKU_DESK_C_TEXT);
}

void
tiku_trk_find_draw(tiku_trk_find_t *f, tiku_desk_surface_t *s,
                   tiku_desk_rect_t frame)
{
    static const char *kModes[] = { "by name", "by attribute", "by formula" };
    tiku_desk_rect_t r;
    char buf[TIKU_TRK_PATH_MAX];
    int i;

    if (f == NULL || s == NULL) {
        return;
    }
    tiku_desk_fill(s, frame, TIKU_DESK_C_PANEL);

    r = mode_rect(frame);
    label_at(s, frame.x + PAD, r, "Find");
    draw_popup_button(s, r, kModes[f->mode]);

    if (f->mode == TIKU_TRK_QMODE_NAME) {
        r = row_rect(frame, 0);
        label_at(s, r.x, r, "Name contains");
        r.x += LABEL_W + 40;
        r.w = VALUE_W + OP_W;
        r.y += (ROW_H - TEXT_H) / 2;
        r.h = TEXT_H;
        draw_field(s, r, f->name_text,
                   f->focus == TIKU_TRK_FIND_FOCUS_NAME);
    } else if (f->mode == TIKU_TRK_QMODE_FORMULA) {
        r = row_rect(frame, 0);
        label_at(s, r.x, r, "Formula");
        r.x += LABEL_W;
        r.w = frame.w - 2 * PAD - LABEL_W;
        r.y += (ROW_H - TEXT_H) / 2;
        r.h = TEXT_H;
        draw_field(s, r, f->formula,
                   f->focus == TIKU_TRK_FIND_FOCUS_FORMULA);
    } else {
        for (i = 0; i < f->row_count; i++) {
            tiku_desk_rect_t rr = row_rect(frame, i);
            tiku_desk_rect_t c = rr;

            c.w = FIELD_W;
            draw_popup_button(s, c, field_label(f->row[i].field));
            c.x += FIELD_W + GAP;
            c.w = OP_W;
            draw_popup_button(s, c, kOps[f->row[i].op]);
            c.x += OP_W + GAP;
            c.w = VALUE_W;
            c.y += (ROW_H - TEXT_H) / 2;
            c.h = TEXT_H;
            draw_field(s, c, f->row[i].value,
                       f->focus == TIKU_TRK_FIND_FOCUS_ROW &&
                       f->focus_row == i);
            /* The and/or belongs to the gap between this row and the next,
             * so the last row does not have one. */
            if (i + 1 < f->row_count) {
                c.x += VALUE_W + GAP;
                c.w = JOIN_W;
                c.y = rr.y;
                c.h = ROW_H;
                draw_popup_button(s, c,
                                  (f->row[i].join == TIKU_TRK_JOIN_OR)
                                      ? "or" : "and");
            }
        }
    }

    r = scope_rect(f, frame);
    label_at(s, frame.x + PAD, r, "Search in");
    (void)tiku_trk_find_scope_label(f, buf, sizeof buf);
    draw_popup_button(s, r, buf);

    if (f->mode == TIKU_TRK_QMODE_ATTRIBUTE) {
        tiku_desk_rect_t a = scope_rect(f, frame);

        a.x += a.w + GAP * 2;
        a.w = 26;
        a.h = ROW_H;
        draw_button(s, a, "+",
                    (f->row_count >= TIKU_TRK_FIND_ROWS_MAX)
                        ? TIKU_DESK_S_DISABLED : TIKU_DESK_S_NORMAL);
        a.x += a.w + GAP;
        /* Remove is disabled, not absent, while one row remains: the
         * control's place should not move as rows come and go.  Disabled is
         * the control look's own state, not a wash painted over it. */
        draw_button(s, a, "-",
                    (f->row_count <= 1) ? TIKU_DESK_S_DISABLED
                                        : TIKU_DESK_S_NORMAL);
    }

    draw_button(s, button_rect(f, frame, 0), "Search", TIKU_DESK_S_DEFAULT);
    draw_button(s, button_rect(f, frame, 1), "Save", TIKU_DESK_S_NORMAL);

    if (f->popup_for != TIKU_TRK_FIND_HIT_NONE) {
        tiku_trk_menu_draw(&f->popup, s, f->popup_frame);
    }
}

/*---------------------------------------------------------------------------*/
/* hit-testing and clicks                                                    */
/*---------------------------------------------------------------------------*/

static int
in(tiku_desk_rect_t r, int x, int y)
{
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

tiku_trk_find_hit_t
tiku_trk_find_hit(const tiku_trk_find_t *f, tiku_desk_rect_t frame, int x,
                  int y, int *row)
{
    tiku_desk_rect_t r;
    int i;

    if (row != NULL) {
        *row = -1;
    }
    if (f == NULL) {
        return TIKU_TRK_FIND_HIT_NONE;
    }
    if (in(mode_rect(frame), x, y)) {
        return TIKU_TRK_FIND_HIT_MODE;
    }
    if (in(button_rect(f, frame, 0), x, y)) {
        return TIKU_TRK_FIND_HIT_SEARCH;
    }
    if (in(button_rect(f, frame, 1), x, y)) {
        return TIKU_TRK_FIND_HIT_SAVE;
    }
    r = scope_rect(f, frame);
    if (in(r, x, y)) {
        return TIKU_TRK_FIND_HIT_SCOPE;
    }
    if (f->mode == TIKU_TRK_QMODE_ATTRIBUTE) {
        tiku_desk_rect_t a = r;

        a.x += a.w + GAP * 2;
        a.w = 26;
        if (in(a, x, y)) {
            return TIKU_TRK_FIND_HIT_ADD;
        }
        a.x += a.w + GAP;
        if (in(a, x, y)) {
            return TIKU_TRK_FIND_HIT_REMOVE;
        }
    }
    if (f->mode == TIKU_TRK_QMODE_NAME) {
        tiku_desk_rect_t v = row_rect(frame, 0);

        v.x += LABEL_W + 40;
        v.w = VALUE_W + OP_W;
        return in(v, x, y) ? TIKU_TRK_FIND_HIT_VALUE : TIKU_TRK_FIND_HIT_NONE;
    }
    if (f->mode == TIKU_TRK_QMODE_FORMULA) {
        tiku_desk_rect_t v = row_rect(frame, 0);

        v.x += LABEL_W;
        v.w = frame.w - 2 * PAD - LABEL_W;
        return in(v, x, y) ? TIKU_TRK_FIND_HIT_VALUE : TIKU_TRK_FIND_HIT_NONE;
    }
    for (i = 0; i < f->row_count; i++) {
        tiku_desk_rect_t rr = row_rect(frame, i);
        tiku_desk_rect_t c = rr;

        c.w = FIELD_W;
        if (in(c, x, y)) {
            if (row != NULL) { *row = i; }
            return TIKU_TRK_FIND_HIT_FIELD;
        }
        c.x += FIELD_W + GAP;
        c.w = OP_W;
        if (in(c, x, y)) {
            if (row != NULL) { *row = i; }
            return TIKU_TRK_FIND_HIT_OP;
        }
        c.x += OP_W + GAP;
        c.w = VALUE_W;
        if (in(c, x, y)) {
            if (row != NULL) { *row = i; }
            return TIKU_TRK_FIND_HIT_VALUE;
        }
        if (i + 1 < f->row_count) {
            c.x += VALUE_W + GAP;
            c.w = JOIN_W;
            if (in(c, x, y)) {
                if (row != NULL) { *row = i; }
                return TIKU_TRK_FIND_HIT_JOIN;
            }
        }
    }
    return TIKU_TRK_FIND_HIT_NONE;
}

/** @brief Build the popup a control owns and place it under that control. */
static void
open_popup(tiku_trk_find_t *f, tiku_trk_find_hit_t what, int row,
           tiku_desk_rect_t under, tiku_desk_rect_t screen)
{
    tiku_trk_menu_t *m = &f->popup;
    int i;

    tiku_trk_menu_clear(m);
    switch (what) {
    case TIKU_TRK_FIND_HIT_MODE:
        (void)tiku_trk_menu_add(m, "by name", 0, 0, 0u, 1);
        (void)tiku_trk_menu_add(m, "by attribute", 1, 0, 0u, 1);
        (void)tiku_trk_menu_add(m, "by formula", 2, 0, 0u, 1);
        m->item[f->mode].marked = 1;
        break;
    case TIKU_TRK_FIND_HIT_FIELD:
        for (i = 0; i < NFIELDS; i++) {
            (void)tiku_trk_menu_add(m, kFields[i].label, i, 0, 0u, 1);
            if (kFields[i].field == f->row[row].field) {
                m->item[i].marked = 1;
            }
        }
        break;
    case TIKU_TRK_FIND_HIT_OP: {
        /* Only the operators the field offers appear at all -- "greater
         * than" on a capability string is a question with no answer, and
         * showing it disabled would be worse than not showing it. */
        unsigned ops = field_ops(f->row[row].field);

        for (i = 0; i < 7; i++) {
            if ((ops & (1u << i)) == 0u) {
                continue;
            }
            (void)tiku_trk_menu_add(m, kOps[i], i, 0, 0u, 1);
            if ((tiku_trk_qop_t)i == f->row[row].op) {
                m->item[m->count - 1].marked = 1;
            }
        }
        break;
    }
    case TIKU_TRK_FIND_HIT_JOIN:
        (void)tiku_trk_menu_add(m, "and", 0, 0, 0u, 1);
        (void)tiku_trk_menu_add(m, "or", 1, 0, 0u, 1);
        m->item[f->row[row].join].marked = 1;
        break;
    case TIKU_TRK_FIND_HIT_SCOPE:
        (void)tiku_trk_menu_add(m, "Everywhere", 0, 0, 0u, 1);
        m->item[0].marked = f->everywhere;
        (void)tiku_trk_menu_add_separator(m);
        for (i = 0; i < f->place_count; i++) {
            char lbl[TIKU_TRK_NAME_MAX + 2];

            snprintf(lbl, sizeof lbl, "/%s", f->place[i]);
            (void)tiku_trk_menu_add(m, lbl, 100 + i, 0, 0u, 1);
            if (!f->everywhere && (f->place_mask & (1u << i))) {
                m->item[m->count - 1].marked = 1;
            }
        }
        break;
    default:
        return;
    }
    f->popup_for = what;
    f->popup_row = row;
    f->popup_frame = tiku_trk_menu_place(m, under.x, under.y + under.h,
                                         screen);
}

tiku_trk_find_hit_t
tiku_trk_find_click(tiku_trk_find_t *f, tiku_desk_rect_t frame, int x, int y)
{
    tiku_desk_rect_t screen = { 0, 0, 4096, 4096 };
    tiku_trk_find_hit_t what;
    int row = -1;

    if (f == NULL) {
        return TIKU_TRK_FIND_HIT_NONE;
    }
    /* A showing popup eats the click, whether it lands on an item or not. */
    if (f->popup_for != TIKU_TRK_FIND_HIT_NONE) {
        int pick = tiku_trk_menu_item_at(&f->popup, f->popup_frame, x, y);

        if (pick >= 0) {
            int cmd = f->popup.item[pick].command;

            switch (f->popup_for) {
            case TIKU_TRK_FIND_HIT_MODE:
                tiku_trk_find_set_mode(f, (tiku_trk_qmode_t)cmd);
                break;
            case TIKU_TRK_FIND_HIT_FIELD:
                f->row[f->popup_row].field = kFields[cmd].field;
                /* An operator the new field does not offer would be left
                 * showing something it cannot do, so the row drops to that
                 * field's own starting operator. */
                if ((kFields[cmd].ops &
                     (1u << (unsigned)f->row[f->popup_row].op)) == 0u) {
                    f->row[f->popup_row].op = kFields[cmd].first;
                }
                break;
            case TIKU_TRK_FIND_HIT_OP:
                f->row[f->popup_row].op = (tiku_trk_qop_t)cmd;
                break;
            case TIKU_TRK_FIND_HIT_JOIN:
                f->row[f->popup_row].join = (tiku_trk_qjoin_t)cmd;
                break;
            case TIKU_TRK_FIND_HIT_SCOPE:
                if (cmd == 0) {
                    /* Choosing everywhere clears every individual place, as
                     * "All disks" clears every volume mark. */
                    f->everywhere = 1;
                    f->place_mask = 0u;
                } else {
                    f->everywhere = 0;
                    f->place_mask ^= 1u << (unsigned)(cmd - 100);
                    if (f->place_mask == 0u) {
                        f->everywhere = 1;
                    }
                }
                break;
            default:
                break;
            }
        }
        f->popup_for = TIKU_TRK_FIND_HIT_NONE;
        return TIKU_TRK_FIND_HIT_NONE;
    }

    what = tiku_trk_find_hit(f, frame, x, y, &row);
    switch (what) {
    case TIKU_TRK_FIND_HIT_MODE:
        open_popup(f, what, 0, mode_rect(frame), screen);
        break;
    case TIKU_TRK_FIND_HIT_SCOPE:
        open_popup(f, what, 0, scope_rect(f, frame), screen);
        break;
    case TIKU_TRK_FIND_HIT_FIELD: {
        tiku_desk_rect_t c = row_rect(frame, row);

        c.w = FIELD_W;
        open_popup(f, what, row, c, screen);
        break;
    }
    case TIKU_TRK_FIND_HIT_OP: {
        tiku_desk_rect_t c = row_rect(frame, row);

        c.x += FIELD_W + GAP;
        c.w = OP_W;
        open_popup(f, what, row, c, screen);
        break;
    }
    case TIKU_TRK_FIND_HIT_JOIN: {
        tiku_desk_rect_t c = row_rect(frame, row);

        c.x += FIELD_W + GAP + OP_W + GAP + VALUE_W + GAP;
        c.w = JOIN_W;
        open_popup(f, what, row, c, screen);
        break;
    }
    case TIKU_TRK_FIND_HIT_VALUE:
        if (f->mode == TIKU_TRK_QMODE_NAME) {
            f->focus = TIKU_TRK_FIND_FOCUS_NAME;
        } else if (f->mode == TIKU_TRK_QMODE_FORMULA) {
            f->focus = TIKU_TRK_FIND_FOCUS_FORMULA;
        } else {
            f->focus = TIKU_TRK_FIND_FOCUS_ROW;
            f->focus_row = row;
        }
        break;
    case TIKU_TRK_FIND_HIT_ADD:
        (void)tiku_trk_find_add_row(f);
        break;
    case TIKU_TRK_FIND_HIT_REMOVE:
        (void)tiku_trk_find_remove_row(f);
        break;
    default:
        break;
    }
    return what;
}

/** @brief The field the caret is in, or NULL. */
static char *
focused_field(tiku_trk_find_t *f, size_t *max)
{
    switch (f->focus) {
    case TIKU_TRK_FIND_FOCUS_NAME:
        *max = sizeof f->name_text;
        return f->name_text;
    case TIKU_TRK_FIND_FOCUS_FORMULA:
        *max = sizeof f->formula;
        return f->formula;
    case TIKU_TRK_FIND_FOCUS_ROW:
        if (f->focus_row >= 0 && f->focus_row < f->row_count) {
            *max = sizeof f->row[0].value;
            return f->row[f->focus_row].value;
        }
        return NULL;
    default:
        return NULL;
    }
}

void
tiku_trk_find_key(tiku_trk_find_t *f, char ch)
{
    size_t max = 0;
    char *field;
    size_t n;

    if (f == NULL || ch < 0x20 || ch >= 0x7F) {
        return;
    }
    field = focused_field(f, &max);
    if (field == NULL) {
        return;
    }
    n = strlen(field);
    if (n + 1u < max) {
        field[n] = ch;
        field[n + 1u] = '\0';
    }
}

void
tiku_trk_find_backspace(tiku_trk_find_t *f)
{
    size_t max = 0;
    char *field;
    size_t n;

    if (f == NULL) {
        return;
    }
    field = focused_field(f, &max);
    if (field == NULL) {
        return;
    }
    n = strlen(field);
    if (n > 0u) {
        field[n - 1u] = '\0';
    }
}
