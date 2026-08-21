/*
 * The new Tracker for TikuOS.
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

#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"
#include "tiku_desk_ui.h"
#include "tiku_trk_deskbar_prefs.h"

#define PAD        16
#define ROW_H      22
#define LIST_Y     42
#define STEP_X     240          /* where the size stepper starts */
#define PLUS_X     300          /* and where its + half begins */

/** @brief How many families the list shows: the folder decides. */
static int
rows_of(const tiku_trk_fonts_window_t *controller)
{
    int n = controller->offer.count;

    return (n > 0) ? n : 1;
}

/** @brief Where the size stepper sits, under however many families. */
static int
size_y(const tiku_trk_fonts_window_t *controller)
{
    return LIST_Y + rows_of(controller) * ROW_H + 30;
}

/** @brief Where the specimen sits, under that. */
static int
sample_y(const tiku_trk_fonts_window_t *controller)
{
    return size_y(controller) + 34;
}

/** @brief And the buttons, under that. */
static int
button_y(const tiku_trk_fonts_window_t *controller)
{
    return sample_y(controller) + 62;
}

/** @brief The whole window, title bar and all. */
static int
window_height(const tiku_trk_fonts_window_t *controller)
{
    return button_y(controller) + 26 + PAD + 26;
}

static int
inside(tiku_desk_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief Put what the window says onto the interface it is drawn in. */
static void
apply(tiku_trk_fonts_window_t *controller)
{
    tiku_trk_deskbar_prefs_t record;

    (void)tiku_trk_fontprefs_save(&controller->model, controller->store);
    (void)tiku_trk_store_flush(controller->store);
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
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
draw(tiku_desk_window_t *window, tiku_desk_surface_t *surface,
     tiku_desk_rect_t content, void *context)
{
    tiku_trk_fonts_window_t *controller = context;
    const tiku_trk_fontprefs_values_t *v = &controller->model.value;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    const tiku_desk_font_t *bold = tiku_desk_font_bold();
    char number[32];
    int rows = rows_of(controller);
    int chosen_row = tiku_trk_fontprefs_chosen(&controller->model,
                                               &controller->offer);
    int i;

    (void)window;
    tiku_desk_fill(surface, content, TIKU_DESK_C_PANEL);
    tiku_desk_text(surface, bold, content.x + PAD, content.y + 22,
                   "Family", TIKU_DESK_C_TEXT);
    {
        tiku_desk_rect_t list = { content.x + PAD, content.y + LIST_Y,
                                  content.w - 2 * PAD, rows * ROW_H };

        tiku_desk_fill(surface, list, TIKU_DESK_C_DOC);
        tiku_desk_frame(surface, list,
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_DARKEN_2));
        for (i = 0; i < rows; i++) {
            tiku_desk_rect_t row = { list.x + 1, list.y + 1 + i * ROW_H,
                                     list.w - 2, ROW_H };
            const char *name = (i < controller->offer.count)
                                   ? controller->offer.name[i] : NULL;
            int chosen = (i == chosen_row);

            if (name == NULL || name[0] == '\0') {
                continue;
            }
            if (chosen) {
                tiku_desk_fill(surface, row, TIKU_DESK_C_SELECT);
            }
            tiku_desk_text(surface, font, row.x + 8,
                           row.y + (ROW_H - font->height) / 2 + font->ascent,
                           name, chosen ? TIKU_DESK_C_SELTEXT
                                        : TIKU_DESK_C_TEXT);
        }
    }

    tiku_desk_text(surface, bold, content.x + PAD,
                   content.y + size_y(controller) + font->ascent, "Size",
                   TIKU_DESK_C_TEXT);
    snprintf(number, sizeof number, "-   %d px   +", v->size);
    tiku_desk_text(surface, font, content.x + STEP_X,
                   content.y + size_y(controller) + font->ascent, number,
                   TIKU_DESK_C_TEXT);

    /* The specimen: the face itself, at the size itself, drawn on paper
     * rather than on the panel so the ink is judged against what a
     * document will show it against. */
    {
        tiku_desk_rect_t paper = { content.x + PAD,
                                   content.y + sample_y(controller),
                                   content.w - 2 * PAD, 52 };

        tiku_desk_fill(surface, paper, TIKU_DESK_C_DOC);
        tiku_desk_frame(surface, paper,
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_DARKEN_2));
        tiku_desk_text(surface, bold, paper.x + 10,
                       paper.y + 6 + bold->ascent, "TikuOS Tracker",
                       TIKU_DESK_C_TEXT);
        tiku_desk_text(surface, font, paper.x + 10,
                       paper.y + 10 + bold->height + font->ascent,
                       "Handgloves 0123456789", TIKU_DESK_C_TEXT);
    }

    tiku_desk_ui_button(surface,
                        (tiku_desk_rect_t){ content.x + PAD,
                                            content.y + button_y(controller),
                                            112, 26 },
                        "Defaults",
                        tiku_trk_fontprefs_defaultable(&controller->model)
                            ? 0u : TIKU_DESK_S_DISABLED);
    tiku_desk_ui_button(surface,
                        (tiku_desk_rect_t){ content.x + PAD + 122,
                                            content.y + button_y(controller),
                                            100, 26 },
                        "Revert",
                        tiku_trk_fontprefs_revertable(&controller->model)
                            ? 0u : TIKU_DESK_S_DISABLED);
}

static int
event(tiku_desk_window_t *window, const tiku_desk_event_t *event,
      void *context)
{
    tiku_trk_fonts_window_t *controller = context;
    tiku_desk_rect_t content = tiku_desk_window_content(window);
    int x = event->x - content.x, y = event->y - content.y;
    int rows = rows_of(controller);

    if (event->type != TIKU_DESK_EVENT_POINTER_DOWN ||
        !inside(content, event->x, event->y)) {
        return 0;
    }
    if (y >= LIST_Y && y < LIST_Y + rows * ROW_H &&
        x >= PAD && x < content.w - PAD) {
        if (tiku_trk_fontprefs_choose(&controller->model, &controller->offer,
                                      (y - LIST_Y - 1) / ROW_H)) {
            apply(controller);
        }
        return 1;
    }
    if (y >= size_y(controller) && y < size_y(controller) + ROW_H &&
        x >= STEP_X) {
        if (tiku_trk_fontprefs_step_size(&controller->model, x >= PLUS_X)) {
            apply(controller);
        }
        return 1;
    }
    if (inside((tiku_desk_rect_t){ PAD, button_y(controller), 112, 26 },
               x, y) &&
        tiku_trk_fontprefs_defaultable(&controller->model)) {
        tiku_trk_fontprefs_defaults(&controller->model);
        apply(controller);
        return 1;
    }
    if (inside((tiku_desk_rect_t){ PAD + 122, button_y(controller), 100, 26 },
               x, y) &&
        tiku_trk_fontprefs_revertable(&controller->model)) {
        tiku_trk_fontprefs_revert(&controller->model);
        apply(controller);
        return 1;
    }
    return 0;
}

void
tiku_trk_fonts_window_init(tiku_trk_fonts_window_t *controller,
                           tiku_desk_workspace_t *workspace,
                           tiku_trk_deskbar_t *deskbar,
                           tiku_trk_store_t *store, int screen_w,
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
        tiku_desk_workspace_close(controller->workspace, controller->window);
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
    tiku_desk_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_desk_workspace_activate(controller->workspace,
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
    controller->window = tiku_desk_workspace_open(controller->workspace,
        "Fonts", frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_desk_window_set_size_controls(controller->window, 0, 0);
        tiku_desk_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_desk_window_t *
tiku_trk_fonts_window_handle(tiku_trk_fonts_window_t *controller)
{
    return controller->window;
}
