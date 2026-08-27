/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_panel_window.c - Open and Save panel window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_strings.h"
#include "tiku_iconpaint.h"
#include "tiku_trk_panel_window.h"
#include "tiku_trk_task.h"

#define PANEL_BUTTON_W 78
#define PANEL_BUTTON_MIN_H 22   /* what a 12 px face needs; see button_h() */
#define PANEL_PADDING  10
#define PANEL_FIELD_MIN_H 22
#define PANEL_WIDTH    460
#define PANEL_HEIGHT   320
#define PANEL_MINI_ICON 16

/*
 * A button, a name field and a favourite are each a row of text, so each
 * is as tall as the text in it.  They were written down against a 12 px
 * face; this panel is opened from a shell whose face is a preference, so
 * a name typed at 16 px was being typed into a field sized for 12.
 *
 * Each answers what it replaces at 12 px, and never less.
 */
static int
panel_at_least(int want, int least)
{
    return (want > least) ? want : least;
}

static int button_h(void)
{
    return panel_at_least(tiku_ui_row_h(), PANEL_BUTTON_MIN_H);
}

static int field_h(void)
{
    return panel_at_least(tiku_ui_row_h(), PANEL_FIELD_MIN_H);
}


typedef enum {
    PANEL_HIT_NONE = 0,
    PANEL_HIT_LIST,
    PANEL_HIT_NAME,
    PANEL_HIT_DEFAULT,
    PANEL_HIT_CANCEL,
    PANEL_HIT_FAVORITES,
    PANEL_HIT_NEWFOLDER,
    PANEL_HIT_UP,
    PANEL_HIT_SCROLL,
    PANEL_HIT_FAV_ROW
} panel_hit_t;

#define PANEL_FAV_MIN_H 18

static int fav_row_h(void)
{
    return panel_at_least(tiku_font_plain()->height + 3,
                          PANEL_FAV_MIN_H);
}

static void
mark_dirty(tiku_trk_panel_window_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
layout(const tiku_trk_panel_window_t *controller, tiku_rect_t content,
       tiku_rect_t *list, tiku_rect_t *name,
       tiku_rect_t *default_button, tiku_rect_t *cancel)
{
    int bottom = content.y + content.h - PANEL_PADDING - button_h();
    int field = controller->model.mode == TIKU_TRK_PANEL_SAVE
                    ? field_h() + PANEL_PADDING : 0;

    *list = (tiku_rect_t){ content.x + PANEL_PADDING,
                                content.y + PANEL_PADDING + button_h() +
                                    PANEL_PADDING,
                                content.w - 2 * PANEL_PADDING, 0 };
    list->h = bottom - list->y - PANEL_PADDING - field;
    if (list->h < 40) {
        list->h = 40;
    }
    *name = (tiku_rect_t){ list->x,
                                list->y + list->h + PANEL_PADDING,
                                list->w, field_h() };
    *default_button = (tiku_rect_t){
        content.x + content.w - PANEL_PADDING - PANEL_BUTTON_W,
        bottom, PANEL_BUTTON_W, button_h() };
    *cancel = *default_button;
    cancel->x -= PANEL_PADDING + PANEL_BUTTON_W;
}

/** @brief The scrollbar's place: the gutter to the list's right. */
static tiku_rect_t
scrollbar_rect(tiku_rect_t list)
{
    return (tiku_rect_t){ list.x + list.w + 2, list.y, 12, list.h };
}

/**
 * @brief The FOLDER THAT HOLDS this one, worn as the list's own first
 *        row rather than as a button.
 *
 * The one road a list of what is INSIDE a folder cannot offer on its
 * own rows -- so it is given the shape of a row, above them, the way
 * the modern desks keep the enclosing folder at the head of the list.
 * At the root there is nothing above, and no row says so.
 */
static tiku_rect_t
parent_row(const tiku_trk_panel_window_t *controller)
{
    tiku_rect_t content = tiku_window_content(controller->window);

    return (tiku_rect_t){ content.x + PANEL_PADDING,
                               content.y + PANEL_PADDING,
                               content.w - 2 * PANEL_PADDING,
                               button_h() };
}

/** @brief The path one rung up from @p dir, into @p out. */
static int
parent_of(const char *dir, char *out, size_t max)
{
    char *slash;

    if (strcmp(dir, "/") == 0) {
        return 0;               /* the root holds itself */
    }
    snprintf(out, max, "%s", dir);
    slash = strrchr(out, '/');
    if (slash == NULL) {
        return 0;
    }
    if (slash == out) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return 1;
}

/** @brief The Favorites button, bottom-left, and its open list. */
static tiku_rect_t
favorites_button(const tiku_trk_panel_window_t *controller)
{
    tiku_rect_t content = tiku_window_content(controller->window);

    return (tiku_rect_t){ content.x + PANEL_PADDING,
                               content.y + content.h - PANEL_PADDING -
                                   button_h(),
                               PANEL_BUTTON_W, button_h() };
}

/**
 * @brief The New folder button, beside Favorites; the SAVE panel only.
 *
 * Saving is the one time a person discovers the folder they meant to
 * save INTO does not exist yet -- a new project wants a folder of its
 * own -- and sending them away to make one loses the panel, the place
 * and the name they had already typed.
 */
static tiku_rect_t
new_folder_button(const tiku_trk_panel_window_t *controller)
{
    tiku_rect_t fav = favorites_button(controller);

    fav.x += PANEL_BUTTON_W + PANEL_PADDING;
    return fav;
}

static tiku_rect_t
favorites_list_rect(const tiku_trk_panel_window_t *controller)
{
    tiku_rect_t button = favorites_button(controller);
    int rows = (controller->fav_count > 0 ? controller->fav_count : 1) + 1;
    tiku_rect_t r = { button.x, 0, 200,
                           rows * fav_row_h() + 4 };

    r.y = button.y - r.h;
    return r;
}

/** @brief Fill the list: favourites first, then recent folders (CW-058). */
static void
favorites_fill(tiku_trk_panel_window_t *controller)
{
    char rows[12][TIKU_PATH_MAX];
    int n, i;

    controller->fav_count = 0;
    n = tiku_trk_panel_favorites(controller->store, rows, 6);
    n += tiku_trk_recent_list(controller->store, TIKU_TRK_RECENT_FOLDERS,
                              6, rows + n, 12 - n);
    for (i = 0; i < n && controller->fav_count < 12; i++) {
        const char *leaf = strrchr(rows[i], '/');
        int k, dup = 0;

        for (k = 0; k < controller->fav_count && !dup; k++) {
            dup = strcmp(controller->fav_path[k], rows[i]) == 0;
        }
        if (dup) {
            continue;
        }
        k = controller->fav_count;
        snprintf(controller->fav_path[k], TIKU_PATH_MAX, "%s", rows[i]);
        snprintf(controller->fav_label[k], sizeof controller->fav_label[k],
                 "%s", (leaf != NULL && leaf[1] != '\0') ? leaf + 1
                                                          : rows[i]);
        controller->fav_count++;
    }
}

static int
selection(const tiku_trk_panel_window_t *controller, tiku_model_t *out,
          int max)
{
    int i, count, selected = 0;

    if (controller->view == NULL) {
        return 0;
    }
    count = tiku_trk_poseview_count(controller->view);
    for (i = 0; i < count && selected < max; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(controller->view, i);

        if (pose != NULL && pose->selected) {
            out[selected++] = pose->model;
        }
    }
    return selected;
}

static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_panel_window_t *controller = context;
    const tiku_font_t *font = tiku_font_plain();
    tiku_rect_t list, name, default_button, cancel;
    tiku_model_t selected[8];
    int selected_count = selection(controller, selected, 8);
    int rows, scroll, total, i;
    /* The panel owns the keyboard only while it is the focused window --
     * the rule its own default button already keeps, eight lines from a
     * name field that was ignoring it. */
    int owns = (controller->window != NULL &&
                controller->window ==
                    tiku_workspace_focused(controller->workspace));

    (void)window;
    tiku_ui_panel(surface, content);
    layout(controller, content, &list, &name, &default_button, &cancel);
    tiku_ui_sunken(surface, list, TIKU_C_DOC);
    if (controller->view == NULL) {
        return;
    }
    rows = (list.h - 4) / controller->row_h;
    tiku_trk_poseview_set_visible_rows(controller->view, rows);
    scroll = controller->scroll;
    total = controller->nvis;
    tiku_clip_set(surface, tiku_inset(list, 2));
    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *pose =
            tiku_trk_poseview_at(controller->view,
                                 controller->vis[scroll + i]);
        tiku_rect_t row;
        int mini = controller->row_h - 4;

        if (pose == NULL) {
            continue;
        }
        row = (tiku_rect_t){ list.x + 2,
                                  list.y + 2 + i * controller->row_h,
                                  list.w - 4, controller->row_h };
        if (mini > PANEL_MINI_ICON) {
            mini = PANEL_MINI_ICON;
        }
        if (pose->selected) {
            tiku_fill(surface, row, TIKU_C_SELECT);
        }
        (void)tiku_icon_paint(surface,
            tiku_model_icon_name(&pose->model), row.x + 2,
            row.y + (controller->row_h - mini) / 2, mini);
        tiku_text(surface, font, row.x + 2 + mini + 4,
            row.y + (controller->row_h + font->height) / 2 - 2,
            pose->model.name,
            pose->selected ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    }
    tiku_clip_reset(surface);
    if (total > rows && rows > 0) {
        /* The same bar the folder windows wear, in the same place: one
         * scrollbar everywhere, or a person has to learn two. */
        tiku_ui_scrollbar(surface, scrollbar_rect(list),
            (float)scroll / (float)(total - rows),
            (float)rows / (float)total, 0);
    }
    if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
        tiku_ui_textfield_sel(surface, name, controller->name.text,
            (owns &&
             controller->model.focus == TIKU_TRK_PANEL_FOCUS_NAME)
                ? controller->name.caret : -1,
            controller->name.sel_a, controller->name.sel_b,
            (owns &&
             controller->model.focus == TIKU_TRK_PANEL_FOCUS_NAME)
                ? TIKU_S_FOCUS : 0u);
    }
    tiku_ui_button(surface, cancel, "Cancel", 0u);
    tiku_ui_button(surface, default_button,
        tiku_trk_panel_button_label(&controller->model, selected,
                                    selected_count),
        !tiku_trk_panel_button_enabled(&controller->model, selected,
                                       selected_count)
            ? TIKU_S_DISABLED
        /* The ring is drawn only while the panel owns the keyboard, so
         * it never promises a Return the window would not receive
         * (NeXT UIG ch7). */
        : (controller->window != NULL &&
           controller->window ==
               tiku_workspace_focused(controller->workspace))
            ? TIKU_S_DEFAULT : TIKU_S_NORMAL);
    if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
        tiku_ui_button(surface, new_folder_button(controller),
                            "New folder", 0u);
    }
    {
        const tiku_font_t *pf = tiku_font_plain();
        tiku_rect_t pr = parent_row(controller);
        char above[TIKU_PATH_MAX];

        if (parent_of(controller->model.dir, above, sizeof above)) {
            const char *says = above;
            int sw2 = tiku_text_width(pf, says);

            tiku_fill(surface, pr, TIKU_C_DOC);
            (void)tiku_icon_paint(surface, "folder", pr.x + 4,
                                      pr.y + (pr.h - 16) / 2, 16);
            /* The WHOLE path, tail kept when it will not fit: the leaf
             * end is the half a person navigates by. */
            while (*says != '\0' && sw2 > pr.w - 32) {
                says++;
                sw2 = tiku_text_width(pf, says);
            }
            tiku_text(surface, pf, pr.x + 26,
                           pr.y + (pr.h - pf->height) / 2 + pf->ascent,
                           says, TIKU_C_TEXT);
            tiku_hline(surface, pr.x, pr.y + pr.h - 1, pr.w,
                            tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
        } else {
            tiku_fill(surface, pr, TIKU_C_DOC);
        }
    }
    tiku_ui_button(surface, favorites_button(controller), "Favorites",
                        0u);
    if (controller->fav_open) {
        /* Favourites first, then recent folders, and "Add current folder"
         * as the last row (AW-062, CW-058). */
        tiku_rect_t fr = favorites_list_rect(controller);
        int i;

        tiku_fill(surface, fr, TIKU_C_PANEL);
        tiku_bevel(surface, fr,
            tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_2),
            tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
        if (controller->fav_count == 0) {
            /* An empty list says so, disabled, rather than showing
             * nothing (CW-058). */
            tiku_text(surface, font, fr.x + 8,
                fr.y + 2 + font->ascent + 1, "<No recent items>",
                tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
        }
        for (i = 0; i <= controller->fav_count; i++) {
            const char *text = (i == controller->fav_count)
                                   ? "Add current folder"
                                   : controller->fav_label[i];
            int at = (controller->fav_count == 0 &&
                      i == controller->fav_count) ? 1 : i;

            tiku_text(surface, font, fr.x + 8,
                fr.y + 2 + at * fav_row_h() + font->ascent + 1, text,
                TIKU_C_TEXT);
        }
    }
}

/**
 * @brief Which poses a person is shown: the machinery folders -- the
 *        dot-named ones -- are not choices, and a home that holds
 *        thirty of them buried everything a person actually keeps.
 */
static void
rebuild_visible(tiku_trk_panel_window_t *controller)
{
    int i, total;

    controller->nvis = 0;
    if (controller->view == NULL) {
        return;
    }
    total = tiku_trk_poseview_count(controller->view);
    for (i = 0; i < total &&
                controller->nvis <
                    (int)(sizeof controller->vis /
                          sizeof controller->vis[0]); i++) {
        tiku_trk_pose_t *pose =
            tiku_trk_poseview_at(controller->view, i);

        if (pose == NULL || pose->model.name[0] == '.' ||
            pose->invisible) {
            continue;
        }
        controller->vis[controller->nvis++] = i;
    }
    if (controller->scroll > controller->nvis - 1) {
        controller->scroll = (controller->nvis > 0)
                                 ? controller->nvis - 1 : 0;
    }
    if (controller->scroll < 0) {
        controller->scroll = 0;
    }
}

static void
reload(tiku_trk_panel_window_t *controller, const char *select)
{
    int i, count;

    if (controller->view != NULL) {
        tiku_trk_poseview_free(controller->view);
    }
    controller->view = tiku_trk_poseview_new(controller->backend,
                                              controller->model.dir);
    if (controller->view == NULL) {
        mark_dirty(controller);
        return;
    }
    tiku_trk_poseview_set_icons(controller->view, controller->icons);
    tiku_trk_poseview_set_mode(controller->view, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_set_multiple(controller->view,
                                   controller->model.multiple);
    (void)tiku_trk_poseview_reload(controller->view);
    controller->scroll = 0;
    rebuild_visible(controller);
    if (select != NULL && select[0] != '\0') {
        count = tiku_trk_poseview_count(controller->view);
        for (i = 0; i < count; i++) {
            tiku_trk_pose_t *pose =
                tiku_trk_poseview_at(controller->view, i);

            if (pose != NULL && strcmp(pose->model.name, select) == 0) {
                tiku_trk_poseview_click(controller->view, i, 0u);
                break;
            }
        }
    }
    if (controller->window != NULL) {
        tiku_trk_panel_title(&controller->model, controller->window->title,
                             sizeof controller->window->title);
    }
    mark_dirty(controller);
}

static void
go(tiku_trk_panel_window_t *controller, const char *directory)
{
    if (directory == NULL || directory[0] == '\0') {
        return;
    }
    snprintf(controller->model.dir, sizeof controller->model.dir, "%s",
             directory);
    reload(controller, NULL);
}

/** @brief Mirror the model's save name into the visible editor. */
static void
sync_name(tiku_trk_panel_window_t *controller)
{
    tiku_field_start(&controller->name, controller->model.name);
}

static void
commit(tiku_trk_panel_window_t *controller)
{
    tiku_model_t selected[8];
    char destination[TIKU_PATH_MAX];
    int count;

    if (controller->target == NULL) {
        return;
    }
    if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
        /* Checked BEFORE anything is written (AW-052): a refusal is said
         * and the name left selected for another try; an existing file is
         * a QUESTION, Cancel first, and the checked path waits for the
         * answer rather than being recomputed after it. */
        tiku_trk_save_t why = tiku_trk_panel_save_check(&controller->model,
            controller->backend, controller->pending_save,
            sizeof controller->pending_save);

        if (why == TIKU_TRK_SAVE_OK) {
            if (controller->commit != NULL) {
                controller->commit(controller->commit_context,
                                   controller->target, controller->command,
                                   controller->pending_save);
            }
            tiku_trk_panel_window_close(controller);
        } else if (why == TIKU_TRK_SAVE_REPLACE && controller->alert != NULL) {
            char ask[160];

            snprintf(ask, sizeof ask,
                     tiku_str("alert.msg.exists_replace"),
                     controller->model.name);
            tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                                TIKU_TRK_ALERT_TAG_SAVE_REPLACE, ask,
                                tiku_str("alert.btn.replace"));
        } else if (controller->alert != NULL) {
            tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                                TIKU_TRK_ALERT_TAG_ERROR,
                                tiku_trk_panel_save_text(why), "Cancel");
            controller->name.sel_a = 0;
            controller->name.sel_b = controller->name.len;
        }
        mark_dirty(controller);
        return;
    }
    count = selection(controller, selected, 8);
    if (count == 1 &&
        tiku_trk_panel_choosable(&controller->model, &selected[0])) {
        snprintf(destination, sizeof destination, "%s", selected[0].path);
    } else if ((controller->model.flavors & TIKU_TRK_PANEL_DIR) != 0u) {
        /* Nothing picked, and a folder is an answer: the one being
         * shown is what the button means (AW-055). */
        snprintf(destination, sizeof destination, "%s",
                 controller->model.dir);
    } else {
        /*
         * Asked for a file and given none: there is nothing to answer
         * with, so the panel stays up rather than handing back the
         * folder -- which the asker would take for a document.
         */
        return;
    }
    if (controller->commit != NULL) {
        controller->commit(controller->commit_context, controller->target,
                           controller->command, destination);
    }
    tiku_trk_panel_window_close(controller);
}

void
tiku_trk_panel_window_confirmed(tiku_trk_panel_window_t *controller)
{
    if (controller == NULL || controller->pending_save[0] == '\0') {
        return;
    }
    if (controller->commit != NULL) {
        controller->commit(controller->commit_context, controller->target,
                           controller->command, controller->pending_save);
    }
    tiku_trk_panel_window_close(controller);
}

void
tiku_trk_panel_window_set_alert(tiku_trk_panel_window_t *controller,
                                tiku_alert_t *alert)
{
    if (controller != NULL) {
        controller->alert = alert;
    }
}

void
tiku_trk_panel_window_drop(tiku_trk_panel_window_t *controller,
                           const char *path)
{
    char select[TIKU_NAME_MAX];

    if (controller == NULL || controller->window == NULL ||
        !controller->window->open || path == NULL) {
        return;
    }
    if (tiku_trk_panel_drop(&controller->model, controller->backend, path,
                            select, sizeof select)) {
        reload(controller, select[0] != '\0' ? select : NULL);
        if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
            sync_name(controller);
        }
        mark_dirty(controller);
    }
}

static void
activate_default(tiku_trk_panel_window_t *controller)
{
    tiku_model_t selected[8];
    int count = selection(controller, selected, 8);

    /* ENTERING a folder is navigation, not answering, so it is not
     * behind the button's enablement: a file panel with a folder
     * selected has somewhere to GO even when it has nothing to say. */
    if (count == 1 && tiku_model_is_container(&selected[0])) {
        go(controller, selected[0].path);
        return;
    }
    if (!tiku_trk_panel_button_enabled(&controller->model, selected, count)) {
        return;
    }
    commit(controller);
}

static panel_hit_t
hit(tiku_trk_panel_window_t *controller, int x, int y, int *row)
{
    tiku_rect_t content, list, name, default_button, cancel;

    if (row != NULL) {
        *row = -1;
    }
    if (controller->window == NULL || !controller->window->open) {
        return PANEL_HIT_NONE;
    }
    content = tiku_window_content(controller->window);
    layout(controller, content, &list, &name, &default_button, &cancel);
#define INSIDE(rect) (x >= (rect).x && x < (rect).x + (rect).w && \
                      y >= (rect).y && y < (rect).y + (rect).h)
    {
        tiku_rect_t fav = favorites_button(controller);

        if (controller->fav_open) {
            tiku_rect_t fr = favorites_list_rect(controller);

            if (INSIDE(fr)) {
                if (row != NULL) {
                    *row = (y - fr.y - 2) / fav_row_h();
                }
                return PANEL_HIT_FAV_ROW;
            }
        }
        if (INSIDE(fav)) { return PANEL_HIT_FAVORITES; }
        {
            tiku_rect_t up = parent_row(controller);
            char above[TIKU_PATH_MAX];

            if (INSIDE(up) &&
                parent_of(controller->model.dir, above,
                          sizeof above)) {
                return PANEL_HIT_UP;
            }
        }
        if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
            tiku_rect_t nf = new_folder_button(controller);

            if (INSIDE(nf)) { return PANEL_HIT_NEWFOLDER; }
        }
    }
    if (INSIDE(default_button)) { return PANEL_HIT_DEFAULT; }
    if (INSIDE(cancel))         { return PANEL_HIT_CANCEL; }
    if (controller->model.mode == TIKU_TRK_PANEL_SAVE && INSIDE(name)) {
        return PANEL_HIT_NAME;
    }
    {
        tiku_rect_t sb = scrollbar_rect(list);

        if (INSIDE(sb)) {
            if (row != NULL) {
                /* Which half of the track was pressed rides out in the
                 * row slot: negative for back, positive for on. */
                *row = (y < sb.y + sb.h / 2) ? -2 : -3;
            }
            return PANEL_HIT_SCROLL;
        }
    }
    if (INSIDE(list)) {
        int index = (y - list.y - 2) / controller->row_h;

        if (row != NULL && controller->view != NULL) {
            index += controller->scroll;
            *row = (index >= 0 && index < controller->nvis)
                       ? controller->vis[index] : -1;
        }
        return PANEL_HIT_LIST;
    }
#undef INSIDE
    return PANEL_HIT_NONE;
}

static int
event(tiku_window_t *window, const tiku_event_t *input,
      void *context)
{
    tiku_trk_panel_window_t *controller = context;
    int row = -1;
    int64_t now;

    (void)window;
    if (input->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    switch (hit(controller, input->x, input->y, &row)) {
    case PANEL_HIT_FAVORITES:
        if (!controller->fav_open) {
            favorites_fill(controller);
        }
        controller->fav_open = !controller->fav_open;
        break;
    case PANEL_HIT_SCROLL: {
        /* A press on the bar PAGES toward the hand, the way the track
         * of every scrollbar here answers a press beside its thumb. */
        tiku_rect_t content = tiku_window_content(controller->window);
        tiku_rect_t list2, nm, db, cb;
        int rows2;

        layout(controller, content, &list2, &nm, &db, &cb);
        rows2 = (list2.h - 4) / controller->row_h;
        if (rows2 < 1) {
            rows2 = 1;
        }
        controller->scroll += (row == -2) ? -(rows2 - 1) : (rows2 - 1);
        if (controller->scroll > controller->nvis - rows2) {
            controller->scroll = controller->nvis - rows2;
        }
        if (controller->scroll < 0) {
            controller->scroll = 0;
        }
        mark_dirty(controller);
        break;
    }
    case PANEL_HIT_UP: {
        /* Into the folder that holds this one. */
        char above[TIKU_PATH_MAX];

        if (parent_of(controller->model.dir, above, sizeof above)) {
            go(controller, above);
        }
        break;
    }
    case PANEL_HIT_NEWFOLDER: {
        /* The NAME field names the folder: a person typing "blinker"
         * and pressing the button lands INSIDE blinker/, with the name
         * the caller offered back in the field, ready to save. */
        char at[TIKU_PATH_MAX];
        struct stat sb;

        if (controller->model.name[0] == '\0') {
            if (controller->alert != NULL) {
                tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_ERROR,
                                    "Name the folder first: what is "
                                    "typed in the name field is what "
                                    "the folder will be called.",
                                    "Cancel");
            }
            break;
        }
        snprintf(at, sizeof at, "%s/%s", controller->model.dir,
                 controller->model.name);
        if (stat(at, &sb) == 0 && !S_ISDIR(sb.st_mode)) {
            if (controller->alert != NULL) {
                tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_ERROR,
                                    "Something of that name is already "
                                    "here, and it is not a folder.",
                                    "Cancel");
            }
            break;
        }
        /* Already a folder is not an error: the person is taken in,
         * which is what they were making it FOR. */
        if (stat(at, &sb) != 0 && mkdir(at, 0755) != 0) {
            if (controller->alert != NULL) {
                tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                                    TIKU_TRK_ALERT_TAG_ERROR,
                                    "The folder could not be made "
                                    "here.", "Cancel");
            }
            break;
        }
        go(controller, at);
        snprintf(controller->model.name, sizeof controller->model.name,
                 "%s", controller->offered);
        controller->model.name_len = (int)strlen(controller->model.name);
        sync_name(controller);
        break;
    }
    case PANEL_HIT_FAV_ROW:
        if (controller->fav_count == 0) {
            row -= 1;           /* row 0 is the disabled placeholder */
        }
        if (row == controller->fav_count) {
            /* The last row adds the folder being shown (AW-062). */
            (void)tiku_trk_panel_add_favorite(&controller->model,
                                              controller->store);
        } else if (row >= 0 && row < controller->fav_count) {
            go(controller, controller->fav_path[row]);
        }
        controller->fav_open = 0;
        break;
    case PANEL_HIT_CANCEL:
        tiku_trk_panel_window_close(controller);
        break;
    case PANEL_HIT_DEFAULT:
        activate_default(controller);
        break;
    case PANEL_HIT_NAME:
        controller->model.focus = TIKU_TRK_PANEL_FOCUS_NAME;
        controller->fav_open = 0;
        break;
    case PANEL_HIT_LIST:
        controller->fav_open = 0;
        controller->model.focus = TIKU_TRK_PANEL_FOCUS_LIST;
        tiku_trk_poseview_click(controller->view, row, 0u);
        {
            /* A lone file lends its name to the save field, focus left
             * where it is (AW-054). */
            tiku_model_t picked[8];
            int n = selection(controller, picked, 8);

            if (tiku_trk_panel_pick_name(&controller->model, picked, n)) {
                sync_name(controller);
            }
        }
        now = input->time_us != 0 ? input->time_us : tiku_trk_now();
        if (row >= 0 && now - controller->press_at < 500000 &&
            controller->press_row == row) {
            tiku_trk_pose_t *pose =
                tiku_trk_poseview_at(controller->view, row);

            if (pose != NULL && tiku_trk_panel_activate(&controller->model,
                    &pose->model) == TIKU_TRK_PANEL_ACT_ENTER) {
                go(controller, pose->model.path);
            } else if (pose != NULL) {
                commit(controller);
            }
        }
        controller->press_at = now;
        controller->press_row = row;
        break;
    default:
        return 0;
    }
    mark_dirty(controller);
    return 1;
}

void
tiku_trk_panel_window_init(tiku_trk_panel_window_t *controller,
    tiku_workspace_t *workspace, tiku_backend_t *backend,
    tiku_store_t *store, tiku_trk_nodeicon_t *icons, int row_h,
    int screen_w, int screen_h, int *dirty,
    tiku_trk_panel_window_commit_fn commit_callback, void *commit_context)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->backend = backend;
    controller->store = store;
    controller->icons = icons;
    controller->row_h = row_h;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->press_row = -1;
    controller->commit = commit_callback;
    controller->commit_context = commit_context;
}

void
tiku_trk_panel_window_open(tiku_trk_panel_window_t *controller, void *target,
                           int command, const char *initial_directory)
{
    tiku_rect_t frame;
    char title[TIKU_WINDOW_TITLE_MAX];
    int x = 0, y = 0, width = PANEL_WIDTH, height = PANEL_HEIGHT;

    if (target == NULL) {
        return;
    }
    controller->target = target;
    controller->command = command;
    tiku_trk_panel_open(&controller->model, TIKU_TRK_PANEL_OPEN,
        TIKU_TRK_PANEL_DIR, initial_directory, controller->backend,
        "TikuTracker");
    controller->model.multiple = 0;
    if (!tiku_trk_panel_restore_frame(controller->store, &x, &y, &width,
                                      &height)) {
        x = (controller->screen_w - width) / 2;
        y = (controller->screen_h - height) / 3;
    }
    frame = (tiku_rect_t){ x, y, width, height };
    tiku_field_start(&controller->name, "");
    if (controller->window != NULL && controller->window->open) {
        controller->window->frame = frame;
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
    } else {
        tiku_trk_panel_title(&controller->model, title, sizeof title);
        controller->window = tiku_workspace_open(controller->workspace,
            title, frame, draw, controller, &controller->model);
        if (controller->window != NULL) {
            tiku_window_set_handlers(controller->window, event,
                                           NULL, NULL);
        }
    }
    reload(controller, NULL);
}

void
tiku_trk_panel_window_open_file(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory)
{
    tiku_trk_panel_window_open(controller, target, command,
                               initial_directory);
    if (controller->target != NULL) {
        /* Only what may be ANSWERED with changes; the panel, its rows
         * and its walking are the ones the person already knows. */
        controller->model.flavors = TIKU_TRK_PANEL_FILE;
    }
}

void
tiku_trk_panel_window_open_save(tiku_trk_panel_window_t *controller,
                                void *target, int command,
                                const char *initial_directory,
                                const char *initial_name)
{
    tiku_trk_panel_window_open(controller, target, command,
                               initial_directory);
    if (controller->window == NULL) {
        return;
    }
    /* The same panel asked the other way round: a name to be written
     * rather than an entry to be chosen, keys landing in the name first. */
    controller->model.mode = TIKU_TRK_PANEL_SAVE;
    controller->model.focus = TIKU_TRK_PANEL_FOCUS_NAME;
    snprintf(controller->model.name, sizeof controller->model.name, "%s",
             initial_name != NULL ? initial_name : "");
    controller->model.name_len = (int)strlen(controller->model.name);
    snprintf(controller->offered, sizeof controller->offered, "%s",
             controller->model.name);
    tiku_field_start(&controller->name, controller->model.name);
    controller->pending_save[0] = '\0';
    snprintf(controller->window->title, sizeof controller->window->title,
             "%s", "Save");
    mark_dirty(controller);
}

void
tiku_trk_panel_window_close(tiku_trk_panel_window_t *controller)
{
    if (controller->window != NULL) {
        tiku_trk_panel_save_frame(controller->store,
            controller->window->frame.x, controller->window->frame.y,
            controller->window->frame.w, controller->window->frame.h);
        tiku_workspace_close(controller->workspace,
                                  controller->window);
        controller->window = NULL;
    }
    if (controller->view != NULL) {
        tiku_trk_poseview_free(controller->view);
        controller->view = NULL;
    }
    controller->target = NULL;
    controller->press_at = 0;
    controller->press_row = -1;
    mark_dirty(controller);
}

void
tiku_trk_panel_window_destroy(tiku_trk_panel_window_t *controller)
{
    tiku_trk_panel_window_close(controller);
}

tiku_window_t *
tiku_trk_panel_window_handle(tiku_trk_panel_window_t *controller)
{
    return controller->window;
}

tiku_trk_panel_t *
tiku_trk_panel_window_model(tiku_trk_panel_window_t *controller)
{
    return &controller->model;
}

int
tiku_trk_panel_window_key(tiku_trk_panel_window_t *controller, unsigned key,
                          unsigned modifiers, char character)
{
    if (controller->alert != NULL && controller->alert->open) {
        return 0;               /* the question owns the keys (AW-052) */
    }
    tiku_model_t selected[8];
    tiku_trk_panel_key_t action;
    unsigned tracker_mods = 0;
    int selected_count, panel_key = 0;

    if (controller->window == NULL || !controller->window->open ||
        tiku_workspace_focused(controller->workspace) !=
            controller->window) {
        return 0;
    }
    if ((modifiers & TIKU_MOD_SHIFT) != 0u) {
        tracker_mods |= TIKU_TRK_MOD_SHIFT;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u) {
        tracker_mods |= TIKU_TRK_MOD_CMD;
    }
    selected_count = selection(controller, selected, 8);
    if (key == TIKU_KEY_ESCAPE)      { panel_key = 27; }
    else if (key == TIKU_KEY_RETURN) { panel_key = '\n'; }
    else if (key == TIKU_KEY_TAB)    { panel_key = '\t'; }
    else if (key == TIKU_KEY_UP)     { panel_key = TIKU_TRK_KEY_UP; }
    else if (key == TIKU_KEY_DOWN)   { panel_key = TIKU_TRK_KEY_DOWN; }
    else if (key >= 'a' && key <= 'z')    { panel_key = (int)key; }
    action = tiku_trk_panel_key(&controller->model, panel_key, tracker_mods,
        controller->model.focus == TIKU_TRK_PANEL_FOCUS_LIST,
        selected_count > 0);
    switch (action) {
    case TIKU_TRK_PANEL_KEY_CANCEL:
        tiku_trk_panel_window_close(controller);
        break;
    case TIKU_TRK_PANEL_KEY_DEFAULT:
        activate_default(controller);
        break;
    case TIKU_TRK_PANEL_KEY_PARENT: {
        char parent[TIKU_PATH_MAX];

        if (tiku_trk_panel_parent(&controller->model, parent,
                                  sizeof parent)) {
            go(controller, parent);
        }
        break;
    }
    case TIKU_TRK_PANEL_KEY_ENTER:
        if (selected_count == 1 &&
            tiku_model_is_container(&selected[0])) {
            go(controller, selected[0].path);
        }
        break;
    case TIKU_TRK_PANEL_KEY_HOME:
        go(controller, controller->model.home);
        break;
    case TIKU_TRK_PANEL_KEY_DESKTOP:
        go(controller, controller->model.desktop);
        break;
    case TIKU_TRK_PANEL_KEY_FOCUS:
        if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
            controller->model.focus =
                controller->model.focus == TIKU_TRK_PANEL_FOCUS_LIST
                    ? TIKU_TRK_PANEL_FOCUS_NAME
                    : TIKU_TRK_PANEL_FOCUS_LIST;
        }
        break;
    default:
        if (controller->model.focus == TIKU_TRK_PANEL_FOCUS_LIST &&
            (key == TIKU_KEY_UP || key == TIKU_KEY_DOWN)) {
            /* Travel in the SHOWN rows: the arrows walk what the
             * person sees, never the machinery folders under it, and
             * the window follows the walk off either end. */
            tiku_model_t picked[8];
            int n, at = -1, i;

            for (i = 0; i < controller->nvis; i++) {
                tiku_trk_pose_t *pose = tiku_trk_poseview_at(
                    controller->view, controller->vis[i]);

                if (pose != NULL && pose->selected) {
                    at = i;
                    break;
                }
            }
            at += (key == TIKU_KEY_DOWN) ? 1 : (at > 0 ? -1 : 0);
            if (at >= controller->nvis) {
                at = controller->nvis - 1;
            }
            if (at >= 0 && at < controller->nvis) {
                tiku_rect_t content =
                    tiku_window_content(controller->window);
                tiku_rect_t list2, nm, db, cb;
                int rows2;

                tiku_trk_poseview_click(controller->view,
                                        controller->vis[at], 0u);
                layout(controller, content, &list2, &nm, &db, &cb);
                rows2 = (list2.h - 4) / controller->row_h;
                if (at < controller->scroll) {
                    controller->scroll = at;
                }
                if (rows2 > 0 && at >= controller->scroll + rows2) {
                    controller->scroll = at - rows2 + 1;
                }
            }
            n = selection(controller, picked, 8);
            if (tiku_trk_panel_pick_name(&controller->model, picked, n)) {
                sync_name(controller);
            }
        } else if (controller->model.mode == TIKU_TRK_PANEL_SAVE &&
                   controller->model.focus == TIKU_TRK_PANEL_FOCUS_NAME) {
            if (key == ' ' && (modifiers & TIKU_MOD_CMD) != 0u) {
                /* Cmd-Space completes the name against the folder shown
                 * (NeXT UIG ch3: file-name completion wherever a file
                 * name may be typed). */
                char full[TIKU_NAME_MAX];

                if (tiku_trk_panel_complete(controller->backend,
                        controller->model.dir, controller->name.text,
                        full, sizeof full) > 0) {
                    tiku_field_start(&controller->name, full);
                    controller->name.sel_a = controller->name.sel_b =
                        controller->name.caret = controller->name.len;
                }
            } else if ((modifiers & TIKU_MOD_CMD) != 0u &&
                       (key == 'x' || key == 'c' || key == 'v' ||
                        key == 'a')) {
                /* The desk's editing keys, in the panel's name too: a
                 * name copied from an editor's page lands here, and a
                 * name cut here lands there. */
                (void)tiku_field_key(&controller->name,
                    (key == 'x') ? TIKU_FIELD_CUT
                  : (key == 'c') ? TIKU_FIELD_COPY
                  : (key == 'v') ? TIKU_FIELD_PASTE
                                 : TIKU_FIELD_SELECT_ALL,
                    tracker_mods, '\0');
            } else if (key == TIKU_KEY_BACKSPACE) {
                (void)tiku_field_key(&controller->name,
                    TIKU_FIELD_BACKSPACE, tracker_mods, '\0');
            } else if (character != '\0') {
                (void)tiku_field_key(&controller->name,
                    TIKU_FIELD_CHAR, tracker_mods, character);
            }
            snprintf(controller->model.name, sizeof controller->model.name,
                     "%.*s", (int)sizeof controller->model.name - 1,
                     controller->name.text);
            controller->model.name_len = controller->name.len;
        }
        break;
    }
    mark_dirty(controller);
    return 1;
}

int
tiku_trk_panel_window_poll(tiku_trk_panel_window_t *controller, int64_t now,
    const tiku_model_change_t *changes, int count, int complete)
{
    unsigned result;

    if (controller->window == NULL || !controller->window->open ||
        controller->view == NULL) {
        return 0;
    }
    result = tiku_trk_poseview_poll(controller->view, now, changes, count,
                                    complete);
    if ((result & TIKU_TRK_NOTIFY_GONE) != 0u) {
        tiku_trk_panel_dir_gone(&controller->model, controller->backend);
        reload(controller, NULL);
    } else if ((result & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
        (void)tiku_trk_panel_dir_moved(&controller->model,
            controller->model.dir, tiku_trk_poseview_path(controller->view));
    }
    if (result != TIKU_TRK_NOTIFY_NOTHING) {
        mark_dirty(controller);
        return 1;
    }
    return 0;
}

int
tiku_trk_panel_window_device_gone(tiku_trk_panel_window_t *controller,
                                  const char *mount)
{
    size_t length;

    if (controller->window == NULL || !controller->window->open ||
        mount == NULL || mount[0] == '\0') {
        return 0;
    }
    length = strlen(mount);
    if (strncmp(controller->model.dir, mount, length) != 0 ||
        (controller->model.dir[length] != '\0' &&
         controller->model.dir[length] != '/')) {
        return 0;
    }
    tiku_trk_panel_dir_gone(&controller->model, controller->backend);
    reload(controller, NULL);
    return 1;
}

int
tiku_trk_panel_window_scroll(const tiku_trk_panel_window_t *controller)
{
    return controller->view != NULL
        ? tiku_trk_poseview_scroll(controller->view) : 0;
}

int
tiku_trk_panel_window_rows(const tiku_trk_panel_window_t *controller)
{
    return controller->view != NULL
        ? tiku_trk_poseview_count(controller->view) : 0;
}
