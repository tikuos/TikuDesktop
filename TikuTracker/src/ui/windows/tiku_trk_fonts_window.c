/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fonts_window.c - the letters the interface is drawn in.
 *
 * A choice takes effect the moment it is made, so the specimen at the
 * bottom is simply the interface itself: what a person is judging is the
 * window they are judging it in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_fonts_window.h"

#include <stdio.h>
#include <string.h>

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_ui.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_strings.h"

#define PAD 16

/*
 * Every measurement below is asked of the face in force rather than
 * written down, because this is the one window in which the face CHANGES
 * WHILE IT IS OPEN.  A row written down as 22 pixels is a row that stops
 * holding its own words the moment somebody presses the + it contains.
 *
 * The arithmetic is chosen so that at 12 px -- what the interface has
 * always defaulted to -- each answers exactly the number that used to be
 * written here, and the window is pixel for pixel the one it was.
 */

/** @brief How many families the list shows: the folder decides. */
static int
rows_of(const tiku_trk_fonts_window_t *controller)
{
    int n = controller->offer.count;

    return (n > 0) ? n : 1;
}

/** @brief One family in the list. */
static int
row_h(void)
{
    return tiku_ui_row_h();                        /* 22 at 12 px */
}

/** @brief Where the list starts, under the word above it. */
static int
list_y(void)
{
    return tiku_font_bold()->height + 27;          /* 42 at 12 px */
}

/** @brief Where the size stepper sits, under however many families. */
static int
size_y(const tiku_trk_fonts_window_t *controller)
{
    return list_y() + rows_of(controller) * row_h() + row_h() + 8;
}

/** @brief How tall the specimen's sheet of paper is. */
static int
paper_h(void)
{
    return tiku_font_bold()->height +
           tiku_font_plain()->height + 22;          /* 52 at 12 px */
}

/** @brief Where the specimen sits, under the stepper. */
static int
sample_y(const tiku_trk_fonts_window_t *controller)
{
    return size_y(controller) + tiku_ui_button_h() + 8;
}

/** @brief And the buttons, under that. */
static int
button_y(const tiku_trk_fonts_window_t *controller)
{
    return sample_y(controller) + paper_h() + 10;
}

/*
 * The size stepper: two real buttons with the number between them.
 *
 * It used to be the string "-   12 px   +" with a click anywhere right of
 * pixel 300 counted as a plus.  At 12 px the plus sign happened to land
 * just past 300 and it worked; at 10 px the whole string is shorter, the
 * plus sign sits at 289, and every click ON IT was read as a minus -- so
 * the size could be taken down and never brought back up.  A control is
 * where it is drawn, so these hand back the same rectangles the drawing
 * uses, and the hit test asks them rather than guessing.
 *
 * They are laid out from the RIGHT edge in.  That is not a detail: it is
 * what keeps the two buttons within a few pixels of the same place at
 * every size, so a hand going back for a second press finds them there.
 */
static tiku_rect_t
plus_rect(const tiku_trk_fonts_window_t *controller,
          tiku_rect_t content)
{
    tiku_rect_t r;

    r.w = tiku_ui_step_w();
    r.h = tiku_ui_button_h();
    r.x = content.x + content.w - PAD - r.w;
    r.y = content.y + size_y(controller);
    return r;
}

/** @brief How wide the number between them needs to be, at any size. */
static int
number_w(void)
{
    return tiku_text_width(tiku_font_plain(), "00 px") + 20;
}

static tiku_rect_t
minus_rect(const tiku_trk_fonts_window_t *controller,
           tiku_rect_t content)
{
    tiku_rect_t r = plus_rect(controller, content);

    r.x -= number_w() + 8 + r.w;
    return r;
}

/*
 * The two at the foot, wide enough for whatever the words are.  They were
 * 112 and 100 pixels, which held the English at 12 px and nothing else --
 * a translation, or a larger face, and the label ran past its own button.
 * The old numbers stay as floors, so at 12 px in English they are exactly
 * the buttons they were.
 */
static tiku_rect_t
defaults_rect(const tiku_trk_fonts_window_t *controller,
              tiku_rect_t content)
{
    tiku_rect_t r;

    r.x = content.x + PAD;
    r.y = content.y + button_y(controller);
    r.w = tiku_ui_button_w(tiku_str("button.defaults"), 112);
    r.h = tiku_ui_button_h();
    return r;
}

static tiku_rect_t
revert_rect(const tiku_trk_fonts_window_t *controller,
            tiku_rect_t content)
{
    tiku_rect_t d = defaults_rect(controller, content);
    tiku_rect_t r = d;

    r.x = d.x + d.w + 10;
    r.w = tiku_ui_button_w(tiku_str("button.revert"), 100);
    return r;
}

/** @brief The whole window, title bar and all. */
static int
window_height(const tiku_trk_fonts_window_t *controller)
{
    return button_y(controller) + tiku_ui_button_h() + PAD +
           tiku_window_tab_h() + TIKU_WINDOW_BORDER;
}

/**
 * @brief Grow or shrink the window to what it now needs.
 *
 * The face can change from inside this window and from the Deskbar's own
 * preferences, so the fit is taken every time it is drawn rather than
 * only where a change is noticed -- there is no arrangement of the two
 * windows that leaves this one the wrong size for its contents.
 *
 * The top-left corner does NOT move.  A window that re-centred itself on
 * every press would walk out from under the button being pressed, which
 * is the same bug in a different costume.
 */
static void
refit(tiku_trk_fonts_window_t *controller)
{
    int want;

    if (controller->window == NULL || !controller->window->open) {
        return;
    }
    want = window_height(controller);
    if (controller->window->frame.h != want) {
        controller->window->frame.h = want;
        if (controller->dirty != NULL) {
            *controller->dirty = 1;
        }
    }
}

static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief Put what the window says onto the interface it is drawn in. */
static void
apply(tiku_trk_fonts_window_t *controller)
{
    tiku_trk_deskbar_prefs_t record;

    (void)tiku_trk_fontprefs_save(&controller->model, controller->store);
    (void)tiku_store_flush(controller->store);
    /* Through the Deskbar rather than straight at the toolkit: it holds
     * the settings everything else is applied from, and a copy of them
     * left saying the old face would put it back on the next change. */
    tiku_trk_deskbar_prefs_init(&record);
    if (controller->store != NULL) {
        (void)tiku_trk_deskbar_prefs_load(&record, controller->store);
    } else {
        record.value.font_family = controller->model.value.family;
        record.value.font_size = controller->model.value.size;
    }
    tiku_trk_deskbar_apply_settings(controller->deskbar, &record.value);
    /* Last, because the Deskbar's own apply puts a BAKED family in force
     * and a dropped one has to win. */
    tiku_trk_fontprefs_apply(&controller->model);
    refit(controller);
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_fonts_window_t *controller = context;
    const tiku_trk_fontprefs_values_t *v = &controller->model.value;
    const tiku_font_t *font = tiku_font_plain();
    const tiku_font_t *bold = tiku_font_bold();
    char number[32];
    int rows = rows_of(controller);
    int chosen_row = tiku_trk_fontprefs_chosen(&controller->model,
                                               &controller->offer);
    int i;

    (void)window;
    refit(controller);
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_text(surface, bold, content.x + PAD,
                   content.y + bold->ascent + 10,
                   tiku_str("prefs.fonts.family"), TIKU_C_TEXT);
    {
        tiku_rect_t list = { content.x + PAD, content.y + list_y(),
                                  content.w - 2 * PAD, rows * row_h() };

        tiku_fill(surface, list, TIKU_C_DOC);
        tiku_frame(surface, list,
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
        for (i = 0; i < rows; i++) {
            tiku_rect_t row = { list.x + 1, list.y + 1 + i * row_h(),
                                     list.w - 2, row_h() };
            const char *name = (i < controller->offer.count)
                                   ? controller->offer.name[i] : NULL;
            int chosen = (i == chosen_row);

            if (name == NULL || name[0] == '\0') {
                continue;
            }
            if (chosen) {
                tiku_fill(surface, row, TIKU_C_SELECT);
            }
            tiku_text(surface, font, row.x + 8,
                           row.y + (row_h() - font->height) / 2 +
                               font->ascent,
                           name, chosen ? TIKU_C_SELTEXT
                                        : TIKU_C_TEXT);
        }
    }

    {
        tiku_rect_t minus = minus_rect(controller, content);
        tiku_rect_t plus = plus_rect(controller, content);
        tiku_rect_t shown = { minus.x + minus.w + 4, minus.y,
                                   number_w(), minus.h };

        tiku_text(surface, bold, content.x + PAD,
                       minus.y + (minus.h - bold->height) / 2 + bold->ascent,
                       tiku_str("prefs.fonts.size"), TIKU_C_TEXT);
        tiku_ui_button(surface, minus, "-",
                            (v->size > TIKU_TRK_FONT_MIN)
                                ? 0u : TIKU_S_DISABLED);
        tiku_ui_button(surface, plus, "+",
                            (v->size < TIKU_TRK_FONT_MAX)
                                ? 0u : TIKU_S_DISABLED);
        snprintf(number, sizeof number, "%d px", v->size);
        (void)tiku_text_centered(surface, font, shown, number,
                                      TIKU_C_TEXT);
    }

    /* The specimen: the face itself, at the size itself, drawn on paper
     * rather than on the panel so the ink is judged against what a
     * document will show it against. */
    {
        tiku_rect_t paper = { content.x + PAD,
                                   content.y + sample_y(controller),
                                   content.w - 2 * PAD, paper_h() };

        tiku_fill(surface, paper, TIKU_C_DOC);
        tiku_frame(surface, paper,
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
        tiku_text(surface, bold, paper.x + 10,
                       paper.y + 6 + bold->ascent,
                       tiku_str("prefs.fonts.sample_title"),
                       TIKU_C_TEXT);
        tiku_text(surface, font, paper.x + 10,
                       paper.y + 10 + bold->height + font->ascent,
                       tiku_str("prefs.fonts.sample_body"),
                       TIKU_C_TEXT);
    }

    {
        tiku_rect_t d = defaults_rect(controller, content);
        tiku_rect_t r = revert_rect(controller, content);

        tiku_ui_button(surface, d, tiku_str("button.defaults"),
                            tiku_trk_fontprefs_defaultable(&controller->model)
                                ? 0u : TIKU_S_DISABLED);
        tiku_ui_button(surface, r, tiku_str("button.revert"),
                            tiku_trk_fontprefs_revertable(&controller->model)
                                ? 0u : TIKU_S_DISABLED);
    }
}

static int
event(tiku_window_t *window, const tiku_event_t *event,
      void *context)
{
    tiku_trk_fonts_window_t *controller = context;
    tiku_rect_t content = tiku_window_content(window);
    int x = event->x - content.x, y = event->y - content.y;
    int rows = rows_of(controller);

    if (event->type != TIKU_EVENT_POINTER_DOWN ||
        !inside(content, event->x, event->y)) {
        return 0;
    }
    if (y >= list_y() && y < list_y() + rows * row_h() &&
        x >= PAD && x < content.w - PAD) {
        if (tiku_trk_fontprefs_choose(&controller->model, &controller->offer,
                                      (y - list_y() - 1) / row_h())) {
            apply(controller);
        }
        return 1;
    }
    /*
     * The stepper is asked for its rectangles rather than told where they
     * are.  Whatever the face, a press lands on the button it looks like
     * it landed on -- which is the whole of the bug this replaces.
     */
    if (inside(minus_rect(controller, content), event->x, event->y) ||
        inside(plus_rect(controller, content), event->x, event->y)) {
        int up = inside(plus_rect(controller, content), event->x, event->y);

        if (tiku_trk_fontprefs_step_size(&controller->model, up)) {
            apply(controller);
        }
        return 1;
    }
    if (inside(defaults_rect(controller, content), event->x, event->y) &&
        tiku_trk_fontprefs_defaultable(&controller->model)) {
        tiku_trk_fontprefs_defaults(&controller->model);
        apply(controller);
        return 1;
    }
    if (inside(revert_rect(controller, content), event->x, event->y) &&
        tiku_trk_fontprefs_revertable(&controller->model)) {
        tiku_trk_fontprefs_revert(&controller->model);
        apply(controller);
        return 1;
    }
    return 0;
}

void
tiku_trk_fonts_window_init(tiku_trk_fonts_window_t *controller,
                           tiku_workspace_t *workspace,
                           tiku_trk_deskbar_t *deskbar,
                           tiku_store_t *store, int screen_w,
                           int screen_h, int *dirty)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->deskbar = deskbar;
    controller->store = store;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    tiku_trk_fontprefs_init(&controller->model);
}

void
tiku_trk_fonts_window_close(tiku_trk_fonts_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

void
tiku_trk_fonts_window_destroy(tiku_trk_fonts_window_t *controller)
{
    tiku_trk_fonts_window_close(controller);
}

void
tiku_trk_fonts_window_open(tiku_trk_fonts_window_t *controller)
{
    tiku_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    /* Read afresh: the size can be changed from the Deskbar's own
     * preferences, and a stale window would put the old one back. */
    (void)tiku_trk_fontprefs_load(&controller->model, controller->store);
    tiku_trk_fontprefs_mark_opened(&controller->model);
    /* The folder is the list, so it is read every time the window opens:
     * a face dropped in while it was shut is offered when it reopens. */
    (void)tiku_trk_fontprefs_offer(&controller->offer);

    frame.w = TIKU_TRK_FONTS_W;
    frame.h = window_height(controller);
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.fonts"), frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_window_t *
tiku_trk_fonts_window_handle(tiku_trk_fonts_window_t *controller)
{
    return controller->window;
}
