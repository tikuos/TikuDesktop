/* TikuGrab: the desk, or one of its windows, as a picture.  The desktop
 * takes it -- it is the one that can see the desk -- and writes it into
 * Desktop/Screenshots; this window chooses what, waits out the delay,
 * and shows what came back.  SPDX-License-Identifier: Apache-2.0 */
#include "tiku_grabview.h"
#include "tiku_attr.h"
#include "tiku_dialog.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_icons_tiku.h"
#include "tiku_image.h"
#include "tiku_measure.h"
#include "tiku_msg.h"
#include "tiku_remote.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GB_TARGETS    64            /* windows the list holds             */
#define GB_LIST_W     230           /* its width                          */
#define GB_REFRESH_US 1500000       /* the list asked for again this often */
#define GB_CHECK      8             /* the checkerboard behind a picture  */

enum { GB_GRAB = 1, GB_DELAY, GB_OPEN, GB_SHOW, GB_SAVE, GB_CLOSE,
       GB_DELAY_NONE, GB_DELAY_3, GB_DELAY_5, GB_DELAY_10, GB_LIST };

/** @brief Something to grab: the desk (id 0), or a window by its id. */
typedef struct {
    char title[128];
    int64_t id;
} gb_target_t;

/** @brief Where the window's parts go. */
typedef struct {
    tiku_rect_t list, view, status;
    int rh;
} gb_layout_t;

struct tiku_grabview {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty;
    tiku_grabview_host_t host;
    gb_target_t target[GB_TARGETS + 1];
    int ntarget, sel, hover_tool;
    int delay, waiting;             /* seconds to wait; an answer owed    */
    int64_t now, due, asked;        /* the clock; the grab's; the list's  */
    char path[TIKU_PATH_MAX];       /* the last picture's file, or ""     */
    long long size;
    tiku_image_t shot;              /* it, read back                      */
    uint32_t *view;                 /* it, fitted to the preview          */
    int view_w, view_h, fit_w, fit_h;
    char status[320];
    tiku_dialog_t dialog;
};

static void gb_menus(tiku_grabview_t *c);
static void gb_action(tiku_grabview_t *c, int id);

/** @brief Ask for a redraw. */
static void gb_changed(tiku_grabview_t *c)
{
    if (c->dirty) *c->dirty = 1;
}

/** @brief Put @p text in the status line. */
static void gb_say(tiku_grabview_t *c, const char *text)
{
    snprintf(c->status, sizeof c->status, "%s", text);
    gb_changed(c);
}

/** @brief Whether @p x, @p y is inside @p r. */
static int gb_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief The last part of @p path. */
static const char *gb_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

/* ------------------------------------------------------------ asking  */

/** @brief A request to the desktop: the list (@p what TIKU_GRAB_LIST), or
 *         a picture of the desk or of window @p id. */
static void gb_ask(tiku_grabview_t *c, int what, int64_t id)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRAB);
    int sent;

    if (!m) return;
    (void)tiku_msg_add_int32(m, "what", what);
    if (what == TIKU_GRAB_WINDOW) (void)tiku_msg_add_int64(m, "id", id);
    sent = c->host.send && c->host.send(c->host.ctx, m);
    tiku_msg_free(m);
    if (what == TIKU_GRAB_LIST) {
        c->asked = c->now;
    } else if (!sent) {
        c->waiting = 0;
        gb_say(c, "The desktop could not be asked for a screenshot.");
    }
}

/** @brief The grab itself, of whatever the list has chosen. */
static void gb_take(tiku_grabview_t *c)
{
    const gb_target_t *t = &c->target[c->sel];

    c->due = 0;
    c->waiting = 1;
    gb_say(c, "Grabbing...");
    gb_ask(c, t->id ? TIKU_GRAB_WINDOW : TIKU_GRAB_DESK, t->id);
    gb_menus(c);
}

/** @brief The list from the desktop's answer, the chosen one kept chosen
 *         when it is still there. */
static void gb_targets(tiku_grabview_t *c, const tiku_msg_t *m)
{
    int64_t chosen = c->target[c->sel].id, id;
    const char *title;

    c->ntarget = 1;
    c->sel = 0;
    for (int i = 0; c->ntarget <= GB_TARGETS &&
         (title = tiku_msg_find_string(m, "title", i)) != NULL; i++) {
        gb_target_t *t = &c->target[c->ntarget];

        if (tiku_msg_find_int64(m, "id", i, &id) != 1) continue;
        snprintf(t->title, sizeof t->title, "%s", *title ? title : "Untitled");
        t->id = id;
        if (id == chosen) c->sel = c->ntarget;
        c->ntarget++;
    }
    gb_changed(c);
}

/* ----------------------------------------------------------- picture  */

/** @brief The mean of the picture's pixels in the box from (@p x0, @p y0)
 *         to (@p x1, @p y1), laid over @p back where it lets it through.
 *  @return that pixel, opaque. */
static uint32_t gb_mean(const tiku_image_t *im, int x0, int y0, int x1,
                        int y1, uint32_t back)
{
    unsigned long sum[4] = { 0, 0, 0, 0 }, n = 0;
    uint32_t out = 0xff000000u;

    for (int y = y0; y < y1 && y < im->h; y++)
        for (int x = x0; x < x1 && x < im->w; x++, n++) {
            uint32_t p = im->px[(size_t)y * im->w + x];

            sum[3] += p >> 24;
            for (int ch = 0; ch < 3; ch++)
                sum[ch] += ((p >> (16 - 8 * ch)) & 255u) * (p >> 24);
        }
    for (int ch = 0; n && ch < 3; ch++) {
        unsigned long a = sum[3] / n, b = (back >> (16 - 8 * ch)) & 255u;
        unsigned long v = (sum[ch] / n + b * (255 - a)) / 255;

        out |= (uint32_t)(v > 255 ? 255 : v) << (16 - 8 * ch);
    }
    return out;
}

/** @brief The last picture fitted into @p w by @p h: shrunk, never grown,
 *         each pixel the mean of those it covers, over a checkerboard
 *         where the picture lets it through. */
static void gb_fit(tiku_grabview_t *c, int w, int h)
{
    const tiku_image_t *im = &c->shot;
    double k = 1.0;

    free(c->view);
    c->view = NULL;
    c->fit_w = w;
    c->fit_h = h;
    if (!im->px || w < 8 || h < 8) return;
    if (im->w > w) k = (double)w / im->w;
    if (im->h * k > h) k = (double)h / im->h;
    c->view_w = (int)(im->w * k) > 0 ? (int)(im->w * k) : 1;
    c->view_h = (int)(im->h * k) > 0 ? (int)(im->h * k) : 1;
    if (!(c->view = malloc((size_t)c->view_w * c->view_h * sizeof *c->view)))
        return;
    for (int y = 0; y < c->view_h; y++) {
        int y0 = (int)(y / k), y1 = (int)((y + 1) / k);

        for (int x = 0; x < c->view_w; x++) {
            int x0 = (int)(x / k), x1 = (int)((x + 1) / k);

            c->view[(size_t)y * c->view_w + x] = gb_mean(im, x0, y0,
                x1 > x0 ? x1 : x0 + 1, y1 > y0 ? y1 : y0 + 1,
                ((x / GB_CHECK + y / GB_CHECK) & 1) ? 0xe4e4e4u : 0xffffffu);
        }
    }
}

/** @brief A width, for fitting a size into the status line. */
static int gb_measure(const char *text, void *ctx)
{
    (void)ctx;
    return tiku_measure_width(TIKU_MEASURE_PLAIN, text);
}

/** @brief The picture the desktop wrote, read back to be shown. */
static void gb_landed(tiku_grabview_t *c, const tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int64_t size = 0;
    char said[96];

    if (!path) return;
    (void)tiku_msg_find_int64(m, "size", 0, &size);
    snprintf(c->path, sizeof c->path, "%s", path);
    c->size = (long long)size;
    tiku_image_free(&c->shot);
    if (tiku_image_load(c->path, &c->shot) != TIKU_IMG_OK)
        memset(&c->shot, 0, sizeof c->shot);
    c->fit_w = c->fit_h = -1;               /* fitted when next drawn */
    (void)tiku_attr_size_fit(size, 90, gb_measure, NULL, said, sizeof said);
    snprintf(c->status, sizeof c->status, "Saved as %.200s, %s.",
             gb_leaf(c->path), said);
    gb_ask(c, TIKU_GRAB_LIST, 0);           /* titles may have moved on */
    gb_menus(c);
    gb_changed(c);
}

/** @brief The picture's file copied to @p to.  @return 0, -1 when it
 *         could not be. */
static int gb_copy(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    char buf[65536];
    size_t n;
    int bad = !out;

    while (!bad && (n = fread(buf, 1, sizeof buf, in)) > 0)
        bad = fwrite(buf, 1, n, out) != n;
    if (in) fclose(in);
    if (out && fclose(out) != 0) bad = 1;
    return bad ? -1 : 0;
}

/* -------------------------------------------------------------- view  */

/** @brief A toolbar button's icon, from the desk's set. */
static const char *gb_tool_icon(int id)
{
    switch (id) {
    case GB_GRAB: return "screen_grab";
    case GB_DELAY: return "grab_timer";
    case GB_OPEN: return "image_viewer";
    case GB_SHOW: return "folder";
    }
    return "file";
}

/** @brief What a toolbar button does, as its tip says it. */
static const char *gb_tool_tip(const tiku_grabview_t *c, int id)
{
    static char delay[48];

    switch (id) {
    case GB_GRAB: return "Grab (Ctrl+G)";
    case GB_DELAY:
        snprintf(delay, sizeof delay, c->delay ? "Delay: %d seconds"
                 : "No delay", c->delay);
        return delay;
    case GB_OPEN: return "Open in TikuImage (Ctrl+O)";
    case GB_SHOW: return "Show in Tracker";
    }
    return "";
}

/** @brief Whether command @p id can do anything now. */
static int gb_enabled(const tiku_grabview_t *c, int id)
{
    switch (id) {
    case GB_GRAB: return !c->waiting && !c->due;
    case GB_OPEN:
    case GB_SHOW:
    case GB_SAVE: return c->path[0] != '\0';
    }
    return 1;
}

/** @brief A command's words, for its menu item and a reader. */
static const char *gb_label(int id)
{
    switch (id) {
    case GB_GRAB: return "Grab";
    case GB_DELAY: return "Delay";
    case GB_OPEN: return "Open in TikuImage";
    case GB_SHOW: return "Show in Tracker";
    case GB_SAVE: return "Save As...";
    case GB_CLOSE: return "Close";
    case GB_DELAY_NONE: return "No Delay";
    case GB_DELAY_3: return "3 Seconds";
    case GB_DELAY_5: return "5 Seconds";
    case GB_DELAY_10: return "10 Seconds";
    }
    return "";
}

/** @brief Where everything goes in content @p r; the toolbar's buttons
 *         into the dialog. */
static gb_layout_t gb_layout(tiku_grabview_t *c, tiku_rect_t r)
{
    static const int ids[] = { GB_GRAB, GB_DELAY, GB_OPEN, GB_SHOW };
    int bh = tiku_ui_button_h(), bw = bh > 30 ? bh : 30, x = r.x + 6;
    int fh = tiku_font_plain()->height + 6, below = r.y + bw + 8;
    gb_layout_t l;

    tiku_dialog_begin(&c->dialog, -1, -1);
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        int w = ids[i] == GB_DELAY ? bw + 26 : bw;

        if (ids[i] == GB_OPEN) x += 10;
        (void)tiku_dialog_add(&c->dialog, ids[i],
                              (tiku_rect_t){x, r.y + 4, w, bw},
                              TIKU_DIALOG_BUTTON, gb_enabled(c, ids[i]));
        x += w + 2;
    }
    l.rh = tiku_measure_row_h() + 8;
    l.status = (tiku_rect_t){r.x + 8, r.y + r.h - (bh > fh ? bh : fh) - 3,
                             r.w - 16, bh > fh ? bh : fh};
    l.list = (tiku_rect_t){r.x + 6, below, GB_LIST_W,
                           l.status.y - 4 - below};
    l.view = (tiku_rect_t){l.list.x + l.list.w + 6, below,
                           r.x + r.w - 6 - (l.list.x + l.list.w + 6),
                           l.list.h};
    (void)tiku_dialog_add(&c->dialog, GB_LIST, l.list, TIKU_DIALOG_INPUT, 1);
    tiku_dialog_end(&c->dialog);
    if (c->dialog.focus == TIKU_DIALOG_NONE)
        (void)tiku_dialog_focus(&c->dialog, GB_LIST);
    return l;
}

/** @brief A toolbar button: its icon, raised under the pointer, sunk
 *         while pressed; the delay's seconds beside the timer. */
static void gb_button(tiku_grabview_t *c, tiku_surface_t *s,
                      const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    int held = tiku_dialog_pressed(&c->dialog, d->id), size = d->rect.h - 6;
    char secs[8];

    if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
    if (held) flags |= TIKU_S_PRESSED;
    if (c->hover_tool == d->id) flags |= TIKU_S_HOT;
    tiku_ui_button(s, d->rect, gb_label(d->id), flags | TIKU_S_ICON);
    if (size > 24) size = 24;
    (void)tiku_icon_paint_dim(s, gb_tool_icon(d->id),
                              d->rect.x + (d->rect.h - size) / 2 + held,
                              d->rect.y + (d->rect.h - size) / 2 + held, size,
                              d->enabled ? 0.0f : 0.72f, TIKU_C_PANEL);
    if (d->id != GB_DELAY) return;
    snprintf(secs, sizeof secs, c->delay ? "%ds" : "off", c->delay);
    tiku_text_line_draw(s, (tiku_rect_t){d->rect.x + d->rect.h - 2, d->rect.y,
                        d->rect.w - d->rect.h, d->rect.h}, TIKU_MEASURE_PLAIN,
                        secs, TIKU_C_TEXT);
}

/** @brief The list: the desk, then each window, the chosen one lit. */
static void gb_list_draw(tiku_grabview_t *c, tiku_surface_t *s,
                         const gb_layout_t *l)
{
    tiku_rect_t clip = s->clip;

    tiku_ui_sunken(s, l->list, TIKU_C_DOC);
    tiku_clip_set(s, (tiku_rect_t){l->list.x + 2, l->list.y + 2,
                  l->list.w - 4, l->list.h - 4});
    for (int i = 0; i < c->ntarget; i++) {
        tiku_rect_t r = { l->list.x + 2, l->list.y + 2 + i * l->rh,
                          l->list.w - 4, l->rh };
        int sel = i == c->sel;

        if (sel) tiku_fill(s, r, TIKU_C_SELECT);
        (void)tiku_icon_paint(s, i ? "grab_window" : "grab_desk", r.x + 6,
                              r.y + (r.h - 20) / 2, 20);
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 32, r.y, r.w - 38, r.h},
                            TIKU_MEASURE_PLAIN, c->target[i].title,
                            sel ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    }
    tiku_clip_set(s, clip);
}

/** @brief The preview: the last picture, fitted and centred; blank until
 *         there is one. */
static void gb_view_draw(tiku_grabview_t *c, tiku_surface_t *s,
                         const gb_layout_t *l)
{
    tiku_rect_t in = { l->view.x + 8, l->view.y + 8, l->view.w - 16,
                       l->view.h - 16 };

    tiku_ui_sunken(s, l->view, TIKU_C_DOC);
    if (c->shot.px && (c->fit_w != in.w || c->fit_h != in.h))
        gb_fit(c, in.w, in.h);
    if (c->view)
        tiku_picture(s, in.x + (in.w - c->view_w) / 2,
                     in.y + (in.h - c->view_h) / 2, c->view_w, c->view_h,
                     c->view);
}

/** @brief The tip of the toolbar button under the pointer, below it. */
static void gb_tip_draw(tiku_grabview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    for (int i = 0; i < c->dialog.count && c->hover_tool; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        const char *text = gb_tool_tip(c, d->id);
        tiku_rect_t tip;

        if (d->id != c->hover_tool || d->kind != TIKU_DIALOG_BUTTON) continue;
        tip = tiku_ui_tip_size(text);
        tip.x = d->rect.x;
        tip.y = d->rect.y + d->rect.h + 4;
        if (tip.x + tip.w > r.x + r.w - 4) tip.x = r.x + r.w - 4 - tip.w;
        tiku_ui_tip(s, tip, text);
    }
}

/** @brief The whole window, laid out afresh each time. */
static void gb_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                    void *ctx)
{
    tiku_grabview_t *c = ctx;
    tiku_rect_t clip = s->clip;
    gb_layout_t l;

    (void)w;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    l = gb_layout(c, r);
    for (int i = 0; i < c->dialog.count; i++)
        if (c->dialog.control[i].kind == TIKU_DIALOG_BUTTON)
            gb_button(c, s, &c->dialog.control[i]);
    gb_list_draw(c, s, &l);
    gb_view_draw(c, s, &l);
    tiku_text_line_draw(s, l.status, TIKU_MEASURE_PLAIN, c->status,
                        tiku_ink(TIKU_INK_FAINT));
    gb_tip_draw(c, s, r);           /* last: nothing paints over it */
    s->clip = clip;
}

/* ----------------------------------------------------------- actions  */

/** @brief Carry out command @p id. */
static void gb_action(tiku_grabview_t *c, int id)
{
    char dir[TIKU_PATH_MAX], *slash;
    static const int delays[] = { 0, 3, 5, 10 };

    if (!gb_enabled(c, id)) return;
    switch (id) {
    case GB_GRAB:
        if (!c->delay) {
            gb_take(c);
            break;
        }
        c->due = c->now + (int64_t)c->delay * 1000000;
        snprintf(c->status, sizeof c->status, "Grabbing in %d...", c->delay);
        break;
    case GB_DELAY:
        c->delay = c->delay == 0 ? 3 : c->delay == 3 ? 5 : c->delay == 5 ? 10
                                                                          : 0;
        break;
    case GB_DELAY_NONE: case GB_DELAY_3: case GB_DELAY_5: case GB_DELAY_10:
        c->delay = delays[id - GB_DELAY_NONE];
        break;
    case GB_OPEN:
        if (c->host.open_path) (void)c->host.open_path(c->host.ctx, c->path);
        break;
    case GB_SHOW:
        snprintf(dir, sizeof dir, "%s", c->path);
        if ((slash = strrchr(dir, '/'))) *slash = '\0';
        if (c->host.open_path) (void)c->host.open_path(c->host.ctx, dir);
        break;
    case GB_SAVE:
        if (c->host.ask_save)
            c->host.ask_save(c->host.ctx, getenv("HOME"), gb_leaf(c->path));
        break;
    case GB_CLOSE: tiku_grabview_close(c); return;
    }
    gb_menus(c);
    gb_changed(c);
}

/** @brief Keys: the command keys, and the list's Up, Down and Return. */
static int gb_key(tiku_grabview_t *c, const tiku_event_t *ev)
{
    unsigned key = ev->key;

    if (ev->modifiers & TIKU_MOD_CMD) {
        if (key == 'g') gb_action(c, GB_GRAB);
        else if (key == 's') gb_action(c, GB_SAVE);
        else if (key == 'o') gb_action(c, GB_OPEN);
        else if (key == 'w' || key == 'q') gb_action(c, GB_CLOSE);
        else return 0;
        return 1;
    }
    if (key == TIKU_KEY_UP && c->sel > 0) c->sel--;
    else if (key == TIKU_KEY_DOWN && c->sel < c->ntarget - 1) c->sel++;
    else if (key == TIKU_KEY_RETURN) gb_action(c, GB_GRAB);
    else if (key != TIKU_KEY_UP && key != TIKU_KEY_DOWN) return 0;
    gb_changed(c);
    return 1;
}

/** @brief The toolbar button under the pointer, lit and tipped. */
static void gb_hover(tiku_grabview_t *c, const tiku_event_t *ev)
{
    int tool = 0;

    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        c->dialog.capture == TIKU_DIALOG_NONE)
        for (int i = 0; i < c->dialog.count; i++)
            if (c->dialog.control[i].kind == TIKU_DIALOG_BUTTON &&
                gb_in(c->dialog.control[i].rect, ev->x, ev->y))
                tool = c->dialog.control[i].id;
    if (tool != c->hover_tool) {
        c->hover_tool = tool;
        gb_changed(c);
    }
}

/** @brief Every window event: keys, a press on the list, then the
 *         toolbar's buttons. */
static int gb_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_grabview_t *c = ctx;
    gb_layout_t l = gb_layout(c, tiku_window_content(w));
    tiku_dialog_result_t r;

    gb_hover(c, ev);
    if (ev->type == TIKU_EVENT_ACTIVATED) gb_ask(c, TIKU_GRAB_LIST, 0);
    if (ev->type == TIKU_EVENT_KEY_DOWN && gb_key(c, ev)) return 1;
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        gb_in(l.list, ev->x, ev->y) && c->dialog.capture == TIKU_DIALOG_NONE) {
        int row = (ev->y - l.list.y - 2) / l.rh;

        if (row >= 0 && row < c->ntarget) c->sel = row;
        (void)tiku_dialog_focus(&c->dialog, GB_LIST);
        gb_changed(c);
        return 1;
    }
    r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0) gb_action(c, r.activated);
    if (r.taken) gb_changed(c);
    return r.taken;
}

/** @brief The native window went away. */
static void gb_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_grabview_t *c = ctx;

    (void)w;
    c->window = NULL;
    gb_changed(c);
}

/** @brief Published menus hand their picks to the one action path. */
static void gb_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    gb_action(ctx, id);
}

/** @brief Add command @p id to menu @p m, with its shortcut. */
static void gb_menu_add(tiku_grabview_t *c, tiku_menu_list_t *m, int id,
                        char sc, int marked)
{
    tiku_menu_entry_t *e;

    if (m->nitem >= TIKU_MENUSET_ITEMS) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", gb_label(id));
    e->command = id;
    e->enabled = (unsigned char)gb_enabled(c, id);
    e->sc = sc;
    e->mods = sc ? TIKU_MOD_CMD : 0;
    e->marked = (unsigned char)marked;
}

/** @brief File and Delay, as the controls stand. */
static void gb_menus(tiku_grabview_t *c)
{
    tiku_menuset_t m;

    if (!c->window) return;
    memset(&m, 0, sizeof m);
    m.nmenu = 2;
    snprintf(m.menu[0].title, sizeof m.menu[0].title, "File");
    snprintf(m.menu[1].title, sizeof m.menu[1].title, "Delay");
    gb_menu_add(c, &m.menu[0], GB_GRAB, 'g', 0);
    gb_menu_add(c, &m.menu[0], GB_SAVE, 's', 0);
    gb_menu_add(c, &m.menu[0], GB_OPEN, 'o', 0);
    gb_menu_add(c, &m.menu[0], GB_SHOW, 0, 0);
    gb_menu_add(c, &m.menu[0], GB_CLOSE, 'w', 0);
    gb_menu_add(c, &m.menu[1], GB_DELAY_NONE, 0, c->delay == 0);
    gb_menu_add(c, &m.menu[1], GB_DELAY_3, 0, c->delay == 3);
    gb_menu_add(c, &m.menu[1], GB_DELAY_5, 0, c->delay == 5);
    gb_menu_add(c, &m.menu[1], GB_DELAY_10, 0, c->delay == 10);
    tiku_window_publish_menus(c->window, &m, gb_menu_pick, c);
}

/* -------------------------------------------------------------- API  */

tiku_grabview_t *tiku_grabview_new(tiku_workspace_t *workspace, int *dirty,
                                   const tiku_grabview_host_t *host)
{
    tiku_grabview_t *c = calloc(1, sizeof *c);

    if (!c) return NULL;
    c->workspace = workspace;
    c->dirty = dirty;
    if (host) c->host = *host;
    snprintf(c->target[0].title, sizeof c->target[0].title, "Whole desk");
    c->ntarget = 1;
    c->fit_w = c->fit_h = -1;
    (void)tiku_icons_tiku_register();
    tiku_dialog_init(&c->dialog);
    return c;
}

void tiku_grabview_open(tiku_grabview_t *c)
{
    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, "TikuGrab",
            (tiku_rect_t){120, 90, 720, 460}, gb_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, gb_event, NULL, gb_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
        gb_say(c, "");              /* an empty window says nothing */
    }
    tiku_workspace_activate(c->workspace, c->window);
    gb_ask(c, TIKU_GRAB_LIST, 0);
    gb_menus(c);
    gb_changed(c);
}

void tiku_grabview_close(tiku_grabview_t *c)
{
    if (c && c->window) tiku_workspace_close(c->workspace, c->window);
}

void tiku_grabview_destroy(tiku_grabview_t *c)
{
    if (!c) return;
    tiku_grabview_close(c);
    tiku_image_free(&c->shot);
    free(c->view);
    free(c);
}

tiku_window_t *tiku_grabview_handle(tiku_grabview_t *c)
{
    return c ? c->window : NULL;
}

void tiku_grabview_message(tiku_grabview_t *c, const struct tiku_msg *m)
{
    int32_t list = 0;
    const char *why;

    if (!c || !m || tiku_msg_what(m) != TIKU_MSG_GRABBED) return;
    if (tiku_msg_find_int32(m, "list", 0, &list) == 1 && list) {
        gb_targets(c, m);
        return;
    }
    c->waiting = 0;
    if ((why = tiku_msg_find_string(m, "error", 0)) != NULL) gb_say(c, why);
    else gb_landed(c, m);
    gb_menus(c);
}

void tiku_grabview_picked(tiku_grabview_t *c, const char *path)
{
    if (!c || !path || !c->path[0]) return;
    if (gb_copy(c->path, path) == 0)
        snprintf(c->status, sizeof c->status, "A copy is in %s.",
                 gb_leaf(path));
    else
        snprintf(c->status, sizeof c->status, "Could not write %s.",
                 gb_leaf(path));
    gb_changed(c);
}

void tiku_grabview_tick(tiku_grabview_t *c, int64_t now_us)
{
    int left;

    if (!c) return;
    c->now = now_us;
    if (c->due && now_us >= c->due) {
        gb_take(c);
    } else if (c->due) {
        left = (int)((c->due - now_us + 999999) / 1000000);
        snprintf(c->status, sizeof c->status, "Grabbing in %d...", left);
        gb_changed(c);
    } else if (!c->waiting && now_us - c->asked >= GB_REFRESH_US) {
        gb_ask(c, TIKU_GRAB_LIST, 0);
    }
}
