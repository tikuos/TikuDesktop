/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_prefs_window.c - dressing the Desktop.
 *
 * The pictures offered are the ones already on the Desktop or in its
 * wallpapers directory: a chooser a person fills by dropping a file in,
 * which is how everything else about this device is configured.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_prefs_window.h"

#include <stdio.h>
#include <string.h>

#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"
#include "tiku_desk_ui.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_strings.h"

#define PAD        16
#define ROW_H      22
#define LIST_ROWS  6

/* Where the controls sit, in content coordinates. */
#define LIST_Y     42
#define MODE_Y     (LIST_Y + LIST_ROWS * ROW_H + 34)
#define ICON_Y     (MODE_Y + 4 * ROW_H + 26)
#define BUTTON_Y   (ICON_Y + 44)

/* The keys of the four laying modes; the words come from the catalogue
 * when a row is drawn, so a translation reaches them too. */
static const char *const kModeKeys[4] = {
    "prefs.desktop.mode.corner", "prefs.desktop.mode.centred",
    "prefs.desktop.mode.scaled", "prefs.desktop.mode.tiled"
};

static int
inside(tiku_desk_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief The name a picture is offered under: its file's own. */
static const char *
leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL) ? slash + 1 : path;
}

/** @brief Put what the window says onto the Desktop it is about. */
static void
apply(tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_folder_window_t *desk =
        tiku_trk_desktop_view_folder(controller->desktop_view);

    (void)tiku_trk_deskprefs_save(&controller->model, controller->store,
                                  controller->node);

    (void)tiku_trk_store_flush(controller->store);
    if (desk != NULL && desk->view != NULL) {
        tiku_trk_poseview_set_icon_size(desk->view,
                                        controller->model.value.icon_size);
        /* The pictures behind it are cached by the path last shown, and
         * forgetting is how a changed record reaches the screen. */
        tiku_trk_folder_window_forget(desk);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
draw(tiku_desk_window_t *window, tiku_desk_surface_t *surface,
     tiku_desk_rect_t content, void *context)
{
    tiku_trk_desktop_prefs_window_t *controller = context;
    const tiku_trk_deskprefs_values_t *v = &controller->model.value;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    const tiku_desk_font_t *bold = tiku_desk_font_bold();
    char number[32];
    int i;

    (void)window;
    tiku_desk_fill(surface, content, TIKU_DESK_C_PANEL);
    tiku_desk_text(surface, bold, content.x + PAD,
                   content.y + 22, tiku_trk_str("prefs.desktop.picture"),
                   TIKU_DESK_C_TEXT);

    /* The list: what is there to choose, and which one is showing. */
    {
        tiku_desk_rect_t list = { content.x + PAD, content.y + LIST_Y,
                                  content.w - 2 * PAD,
                                  LIST_ROWS * ROW_H };

        tiku_desk_fill(surface, list, TIKU_DESK_C_DOC);
        tiku_desk_frame(surface, list,
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_DARKEN_2));
        for (i = 0; i < LIST_ROWS; i++) {
            tiku_desk_rect_t row = { list.x + 1, list.y + 1 + i * ROW_H,
                                     list.w - 2, ROW_H };
            const char *label;
            int chosen;

            if (i == 0) {
                label = tiku_trk_str("prefs.desktop.no_picture");
                chosen = !v->have_bg;
            } else if (i - 1 < controller->pictures.count) {
                label = leaf_of(controller->pictures.picture[i - 1]);
                chosen = v->have_bg &&
                         strcmp(controller->pictures.picture[i - 1],
                                v->bg.path) == 0;
            } else {
                continue;
            }
            if (chosen) {
                tiku_desk_fill(surface, row, TIKU_DESK_C_SELECT);
            }
            tiku_desk_text(surface, font, row.x + 8,
                           row.y + (ROW_H - font->height) / 2 + font->ascent,
                           label, chosen ? TIKU_DESK_C_SELTEXT
                                         : TIKU_DESK_C_TEXT);
        }
        if (controller->pictures.count == 0) {
            tiku_desk_text(surface, font, list.x + 8,
                           list.y + ROW_H + 15,
                           tiku_trk_str("prefs.desktop.drop_hint"),
                           tiku_desk_tint(TIKU_DESK_C_PANEL, 1.45f));
        }
    }

    tiku_desk_text(surface, bold, content.x + PAD,
                   content.y + MODE_Y - 12, tiku_trk_str("prefs.desktop.how_laid"),
                   TIKU_DESK_C_TEXT);
    for (i = 0; i < 4; i++) {
        int y = content.y + MODE_Y + i * ROW_H;
        tiku_desk_rect_t box = { content.x + PAD + 2, y + 4, 12, 12 };

        tiku_desk_fill(surface, box, TIKU_DESK_C_DOC);
        tiku_desk_frame(surface, box, TIKU_DESK_C_TEXT);
        if ((int)v->bg.mode == i) {
            tiku_desk_fill(surface, tiku_desk_inset(box, 3),
                           TIKU_DESK_C_TEXT);
        }
        tiku_desk_text(surface, font, content.x + PAD + 24,
                       y + 4 + font->ascent, tiku_trk_str(kModeKeys[i]),
                       v->have_bg ? TIKU_DESK_C_TEXT
                                  : tiku_desk_tint(TIKU_DESK_C_PANEL,
                                                   1.45f));
    }

    tiku_desk_text(surface, bold, content.x + PAD,
                   content.y + ICON_Y + font->ascent, tiku_trk_str("label.icon_size"),
                   TIKU_DESK_C_TEXT);
    snprintf(number, sizeof number, "-   %d px   +", v->icon_size);
    tiku_desk_text(surface, font, content.x + 240,
                   content.y + ICON_Y + font->ascent, number,
                   TIKU_DESK_C_TEXT);

    tiku_desk_ui_button(surface,
                        (tiku_desk_rect_t){ content.x + PAD,
                                            content.y + BUTTON_Y, 112, 26 },
                        tiku_trk_str("button.defaults"),
                        tiku_trk_deskprefs_defaultable(&controller->model)
                            ? 0u : TIKU_DESK_S_DISABLED);
    tiku_desk_ui_button(surface,
                        (tiku_desk_rect_t){ content.x + PAD + 122,
                                            content.y + BUTTON_Y, 100, 26 },
                        tiku_trk_str("button.revert"),
                        tiku_trk_deskprefs_revertable(&controller->model)
                            ? 0u : TIKU_DESK_S_DISABLED);
}

static int
event(tiku_desk_window_t *window, const tiku_desk_event_t *event,
      void *context)
{
    tiku_trk_desktop_prefs_window_t *controller = context;
    tiku_desk_rect_t content = tiku_desk_window_content(window);
    int x = event->x - content.x, y = event->y - content.y;

    if (event->type != TIKU_DESK_EVENT_POINTER_DOWN ||
        !inside(content, event->x, event->y)) {
        return 0;
    }
    if (y >= LIST_Y && y < LIST_Y + LIST_ROWS * ROW_H &&
        x >= PAD && x < content.w - PAD) {
        int row = (y - LIST_Y - 1) / ROW_H;

        if (row == 0) {
            tiku_trk_deskprefs_clear_picture(&controller->model);
            apply(controller);
        } else if (row - 1 < controller->pictures.count) {
            tiku_trk_deskprefs_set_picture(&controller->model,
                controller->pictures.picture[row - 1]);
            apply(controller);
        }
        return 1;
    }
    if (y >= MODE_Y && y < MODE_Y + 4 * ROW_H) {
        /* Only while a picture is showing: a way of laying nothing is
         * not a choice anybody can see the result of. */
        if (controller->model.value.have_bg) {
            tiku_trk_deskprefs_set_mode(&controller->model,
                (tiku_trk_bg_mode_t)((y - MODE_Y) / ROW_H));
            apply(controller);
        }
        return 1;
    }
    if (y >= ICON_Y && y < ICON_Y + ROW_H && x >= 240) {
        if (tiku_trk_deskprefs_step_icon(&controller->model, x >= 300)) {
            apply(controller);
        }
        return 1;
    }
    if (inside((tiku_desk_rect_t){ PAD, BUTTON_Y, 112, 26 }, x, y) &&
        tiku_trk_deskprefs_defaultable(&controller->model)) {
        tiku_trk_deskprefs_defaults(&controller->model);
        apply(controller);
        return 1;
    }
    if (inside((tiku_desk_rect_t){ PAD + 122, BUTTON_Y, 100, 26 }, x, y) &&
        tiku_trk_deskprefs_revertable(&controller->model)) {
        tiku_trk_deskprefs_revert(&controller->model);
        apply(controller);
        return 1;
    }
    return 0;
}

void
tiku_trk_desktop_prefs_window_init(
    tiku_trk_desktop_prefs_window_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_store_t *store, int screen_w, int screen_h, int *dirty)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->desktop_view = desktop_view;
    controller->store = store;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    (void)tiku_trk_desktop_dir(controller->node, sizeof controller->node);
    snprintf(controller->pictures_dir, sizeof controller->pictures_dir,
             "%s/../wallpapers", controller->node);
    tiku_trk_deskprefs_init(&controller->model);
}

void
tiku_trk_desktop_prefs_window_close(
    tiku_trk_desktop_prefs_window_t *controller)
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
tiku_trk_desktop_prefs_window_destroy(
    tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_desktop_prefs_window_close(controller);
}

void
tiku_trk_desktop_prefs_window_open(
    tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_folder_window_t *desk;
    const char *dirs[2];
    tiku_desk_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_desk_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    /* Read afresh every time it opens: the Desktop may have been dressed
     * from somewhere else since, and a stale window would undo it. */
    desk = tiku_trk_desktop_view_folder(controller->desktop_view);
    (void)tiku_trk_deskprefs_load(&controller->model, controller->store,
        controller->node,
        (desk != NULL && desk->view != NULL)
            ? tiku_trk_poseview_icon_size(desk->view) : 0);
    dirs[0] = controller->node;
    dirs[1] = controller->pictures_dir;
    (void)tiku_trk_deskprefs_pictures(&controller->pictures, dirs, 2);
    tiku_trk_deskprefs_mark_opened(&controller->model);

    frame.w = TIKU_TRK_DESK_PREFS_W;
    frame.h = TIKU_TRK_DESK_PREFS_H;
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_desk_workspace_open(controller->workspace,
        tiku_trk_str("title.desktop_prefs"), frame, draw, controller,
        NULL);
    if (controller->window != NULL) {
        tiku_desk_window_set_size_controls(controller->window, 0, 0);
        tiku_desk_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_desk_window_t *
tiku_trk_desktop_prefs_window_handle(
    tiku_trk_desktop_prefs_window_t *controller)
{
    return controller->window;
}
