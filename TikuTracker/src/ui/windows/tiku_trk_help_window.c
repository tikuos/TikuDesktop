/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_help_window.c - a help page on the screen, and its links.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_help_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_doc_paint.h"
#include "tiku_font.h"
#include "tiku_strings.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

enum { HELP_BACK = 1, HELP_FORWARD, HELP_HOME, HELP_DONE, HELP_PAGE };
_Static_assert(HELP_PAGE == TIKU_TRK_HELP_CONTROL_PAGE,
               "the page's control id is named in the header");

#define HELP_W 620
#define HELP_H 480
#define HELP_BAR 15                 /* the scrollbar's width */

/** @brief Where the parts of the window go, from one description. */
typedef struct {
    tiku_rect_t back, forward, home, done, title, paper, page, bar, status;
} help_layout_t;

/** @brief One geometry, read by the drawing and by the hit testing. */
static help_layout_t
layout(tiku_rect_t c)
{
    help_layout_t l;
    int bh = tiku_ui_button_h();
    int fh = tiku_font_plain()->height;
    int top = c.y + 10;
    int foot = c.y + c.h - fh - 12;

    l.back = (tiku_rect_t){c.x + 12, top, 74, bh};
    l.forward = (tiku_rect_t){c.x + 92, top, 84, bh};
    l.home = (tiku_rect_t){c.x + 182, top, 74, bh};
    l.done = (tiku_rect_t){c.x + c.w - 86, top, 74, bh};
    l.title = (tiku_rect_t){c.x + 266, top, c.w - 364, bh};
    l.paper = (tiku_rect_t){c.x + 12, top + bh + 10, c.w - 24,
                            foot - (top + bh + 10) - 8};
    /* A window dragged small enough would give the paper a negative
     * size, and every rectangle inside it one too. */
    if (l.paper.w < 40) l.paper.w = 40;
    if (l.paper.h < 24) l.paper.h = 24;
    l.page = (tiku_rect_t){l.paper.x + 3, l.paper.y + 3,
                           l.paper.w - 6 - HELP_BAR, l.paper.h - 6};
    if (l.page.w < 16) l.page.w = 16;
    l.bar = (tiku_rect_t){l.paper.x + l.paper.w - HELP_BAR - 1, l.paper.y + 1,
                          HELP_BAR, l.paper.h - 2};
    l.status = (tiku_rect_t){c.x + 12, foot, c.w - 24, fh + 6};
    return l;
}

/**
 * @brief Lay the page out again for the room it already had.
 *
 * A scroll restored before the page is laid out is clamped to the page
 * that came before it, so stepping back would land at the foot of a
 * short page instead of the paragraph it left.
 */
static void settle(tiku_trk_help_window_t *c)
{
    if (!c->doc || c->laid_for <= 0) return;
    (void)tiku_doc_layout(c->doc, c->laid_for);
    tiku_scroll_set_range(&c->bar, tiku_doc_height(c->doc), c->page_h);
}

/** @brief A name with no way out of the folder it is under. */
static int name_is_safe(const char *name)
{
    if (!name || !*name || *name == '/') return 0;
    if (strstr(name, "..")) return 0;
    for (const char *s = name; *s; s++) {
        if ((unsigned char)*s < 32) return 0;
    }
    return 1;
}

/** @brief Read a page of the help folder.  @return 1 when it was there. */
static int page_read(tiku_trk_help_window_t *c, const char *name)
{
    char path[TIKU_PATH_MAX];
    static char text[TIKU_TRK_HELP_PAGE_MAX];
    FILE *f;
    size_t n;

    if (!c->root[0] || !name_is_safe(name)) return 0;
    c->oversize = 0;
    if (snprintf(path, sizeof path, "%s/%s", c->root, name) >=
        (int)sizeof path) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    n = fread(text, 1, sizeof text - 1, f);
    {
        /* One byte past what was taken: a page that has more to give
         * was cut, and a reader must be told rather than shown a page
         * that simply stops. */
        char over;

        c->oversize = fread(&over, 1, 1, f) == 1;
    }
    fclose(f);
    if (!n) {
        /* A folder opens and reads as nothing, and so does an empty
         * file; neither is a page to show. */
        return 0;
    }
    text[n] = 0;
    if (!c->doc) {
        c->doc = calloc(1, sizeof *c->doc);
        if (!c->doc) return 0;
        tiku_doc_init(c->doc);
    }
    (void)tiku_doc_read(c->doc, text);
    c->focus_link = TIKU_DOC_NONE;
    snprintf(c->page, sizeof c->page, "%s", name);
    settle(c);
    tiku_scroll_to(&c->bar, 0);
    return 1;
}

/** @brief Remember where the reader was, before leaving it. */
/**
 * @brief Put @p page and @p at on the way back.
 *
 * Called only once a read has succeeded: a history committed before the
 * page it names is there drops an entry whenever the read fails, and
 * the oldest one whenever the stack is full.
 */
static void back_push(tiku_trk_help_window_t *c, const char *page, int at)
{
    int slot = c->nback;

    if (!page || !page[0]) return;
    if (slot == TIKU_TRK_HELP_HISTORY) {
        memmove(c->back, c->back + 1, sizeof c->back[0] * (size_t)(slot - 1));
        slot--;
    }
    snprintf(c->back[slot].page, sizeof c->back[slot].page, "%s", page);
    c->back[slot].at = at;
    c->nback = slot + 1;
}

/** @brief Put @p page and @p at on the way forward. */
static void forward_push(tiku_trk_help_window_t *c, const char *page, int at)
{
    int slot = c->nforward;

    if (!page || !page[0]) return;
    if (slot == TIKU_TRK_HELP_HISTORY) {
        memmove(c->forward, c->forward + 1,
                sizeof c->forward[0] * (size_t)(slot - 1));
        slot--;
    }
    snprintf(c->forward[slot].page, sizeof c->forward[slot].page, "%s", page);
    c->forward[slot].at = at;
    c->nforward = slot + 1;
}

/** @brief What the window tells the reader, in its own line. */
static void say(tiku_trk_help_window_t *c, const char *what)
{
    snprintf(c->status, sizeof c->status, "%s", what ? what : "");
}

void tiku_trk_help_window_init(tiku_trk_help_window_t *c,
                               tiku_workspace_t *workspace,
                               const char *root, int screen_w, int screen_h,
                               int *dirty)
{
    memset(c, 0, sizeof *c);
    c->workspace = workspace;
    c->screen_w = screen_w;
    c->screen_h = screen_h;
    c->dirty = dirty;
    c->focus_link = TIKU_DOC_NONE;
    snprintf(c->root, sizeof c->root, "%s", root ? root : "");
    tiku_scroll_init(&c->bar, tiku_font_plain()->height);
    tiku_dialog_init(&c->dialog);
}

void tiku_trk_help_window_opener(tiku_trk_help_window_t *c,
                                 tiku_trk_help_open_fn fn, void *ctx)
{
    c->opener = fn;
    c->opener_ctx = ctx;
}

const char *tiku_trk_help_window_status(const tiku_trk_help_window_t *c)
{ return c->status; }
const char *tiku_trk_help_window_page(const tiku_trk_help_window_t *c)
{ return c->page; }
const char *tiku_trk_help_window_wanted(const tiku_trk_help_window_t *c)
{ return c->wanted; }
tiku_window_t *tiku_trk_help_window_handle(tiku_trk_help_window_t *c)
{ return c->window; }

tiku_trk_help_where_t tiku_trk_help_window_go(tiku_trk_help_window_t *c,
                                              const char *href)
{
    if (!href || !*href) return TIKU_TRK_HELP_NOWHERE;
    if (href[0] == '#') {
        int y = c->doc ? tiku_doc_anchor_y(c->doc, href + 1) : TIKU_DOC_NONE;

        if (y == TIKU_DOC_NONE) {
            say(c, tiku_str("help.no_such_place"));
            return TIKU_TRK_HELP_NOWHERE;
        }
        back_push(c, c->page, c->bar.value);
        c->nforward = 0;
        tiku_scroll_to(&c->bar, y);
        say(c, "");
        return TIKU_TRK_HELP_ANCHOR;
    }
    if (href[0] == '/' || !strncmp(href, "tiku:", 5)) {
        const char *path = href[0] == '/' ? href : href + 5;

        if (path[0] != '/') {
            /* The desk opens a place in the namespace, which starts at
             * its root; anything else it would drop without a word. */
            say(c, tiku_str("help.not_a_place"));
            return TIKU_TRK_HELP_NOWHERE;
        }
        snprintf(c->wanted, sizeof c->wanted, "%s", path);
        if (c->opener && !c->opener(c->opener_ctx, path)) {
            say(c, tiku_str("help.not_a_place"));
            return TIKU_TRK_HELP_NOWHERE;
        }
        say(c, "");
        return TIKU_TRK_HELP_NAMESPACE;
    }
    if (strstr(href, "://") || !strncmp(href, "mailto:", 7)) {
        /* There is no browser here, and a help page that pretended
         * otherwise would leave a reader waiting for a window. */
        say(c, tiku_str("help.not_followed"));
        return TIKU_TRK_HELP_NOWHERE;
    }
    {
        char name[TIKU_PATH_MAX], place[128], from[TIKU_PATH_MAX];
        const char *hash = strchr(href, '#');
        size_t n = hash ? (size_t)(hash - href) : strlen(href);
        int was = c->bar.value;

        if (n >= sizeof name) return TIKU_TRK_HELP_NOWHERE;
        memcpy(name, href, n);
        name[n] = 0;
        /*
         * Both halves are copied out before the read.  An href handed
         * in by a link points into the page's own words, and reading
         * the next page writes over them.
         */
        snprintf(place, sizeof place, "%s", hash && hash[1] ? hash + 1 : "");
        snprintf(from, sizeof from, "%s", c->page);
        if (!page_read(c, name)) {
            say(c, tiku_str("help.no_such_page"));
            return TIKU_TRK_HELP_NOWHERE;
        }
        back_push(c, from, was);
        c->nforward = 0;
        say(c, "");
        snprintf(c->anchor, sizeof c->anchor, "%s", place);
        return TIKU_TRK_HELP_PAGE;
    }
}

int tiku_trk_help_window_home(tiku_trk_help_window_t *c)
{
    char from[TIKU_PATH_MAX];
    int was = c->bar.value;

    snprintf(from, sizeof from, "%s", c->page);
    if (!page_read(c, "index.html")) {
        say(c, tiku_str("help.no_such_page"));
        return 0;
    }
    back_push(c, from, was);
    /* A move forward from here would be to a page the reader never came
     * back through. */
    c->nforward = 0;
    say(c, "");
    return 1;
}

/** @brief Step to @p page at @p to, banking where it came from. */
static int step(tiku_trk_help_window_t *c, const char *page, int to, int back)
{
    char from[TIKU_PATH_MAX];
    int was = c->bar.value;

    snprintf(from, sizeof from, "%s", c->page);
    if (strcmp(page, c->page) && !page_read(c, page)) {
        /* Nothing has been taken off either stack yet, so a page that
         * has gone missing since it was read costs the reader nothing
         * but the step. */
        return 0;
    }
    if (back) forward_push(c, from, was);
    else back_push(c, from, was);
    tiku_scroll_to(&c->bar, to);
    say(c, "");
    return 1;
}

int tiku_trk_help_window_back(tiku_trk_help_window_t *c)
{
    int at = c->nback - 1;

    if (at < 0) return 0;
    if (!step(c, c->back[at].page, c->back[at].at, 1)) return 0;
    c->nback = at;
    return 1;
}

int tiku_trk_help_window_forward(tiku_trk_help_window_t *c)
{
    int at = c->nforward - 1;

    if (at < 0) return 0;
    if (!step(c, c->forward[at].page, c->forward[at].at, 0)) return 0;
    c->nforward = at;
    return 1;
}

/** @brief Lay the page out for the room it has now. */
static void refit(tiku_trk_help_window_t *c, help_layout_t l)
{
    if (!c->doc) return;
    c->page_h = l.page.h;
    c->laid_for = l.page.w;
    /* Asked every time: laying out is free when neither the width nor
     * the text size has moved, and a size that moved without the width
     * would otherwise keep the geometry of the size before it. */
    (void)tiku_doc_layout(c->doc, l.page.w);
    tiku_scroll_set_range(&c->bar, tiku_doc_height(c->doc), l.page.h);
    /* A page asked for by name is scrolled to it once it has a height. */
    if (c->anchor[0]) {
        int y = tiku_doc_anchor_y(c->doc, c->anchor);

        if (y != TIKU_DOC_NONE) tiku_scroll_to(&c->bar, y);
        c->anchor[0] = 0;
    }
}

/** @brief Register what can be pressed, and what may take the keyboard. */
static void controls(tiku_trk_help_window_t *c, help_layout_t l)
{
    tiku_dialog_begin(&c->dialog, TIKU_DIALOG_NONE, HELP_DONE);
    tiku_dialog_add(&c->dialog, HELP_PAGE, l.page, TIKU_DIALOG_INPUT, 1);
    tiku_dialog_add(&c->dialog, HELP_BACK, l.back, TIKU_DIALOG_BUTTON,
                    c->nback > 0);
    tiku_dialog_add(&c->dialog, HELP_FORWARD, l.forward, TIKU_DIALOG_BUTTON,
                    c->nforward > 0);
    tiku_dialog_add(&c->dialog, HELP_HOME, l.home, TIKU_DIALOG_BUTTON,
                    strcmp(c->page, "index.html") != 0);
    tiku_dialog_add(&c->dialog, HELP_DONE, l.done, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&c->dialog);
}

/** @brief Put the focused link on the screen if it is off it. */
static void reveal(tiku_trk_help_window_t *c, help_layout_t l)
{
    int first = 0, n = 0;
    tiku_rect_t r;

    if (!c->doc || c->focus_link == TIKU_DOC_NONE) return;
    if (!tiku_doc_link_pieces(c->doc, c->focus_link, &first, &n)) return;
    r = tiku_doc_piece_rect(c->doc, first);
    if (r.y < c->bar.value) tiku_scroll_to(&c->bar, r.y);
    else if (r.y + r.h > c->bar.value + l.page.h) {
        tiku_scroll_to(&c->bar, r.y + r.h - l.page.h);
    }
}

/** @brief The toolbar, the page in its paper, and what the window says. */
static void draw(tiku_window_t *window, tiku_surface_t *s,
                 tiku_rect_t content, void *ctx)
{
    tiku_trk_help_window_t *c = ctx;
    help_layout_t l;
    const char *labels[] = {"", tiku_str("button.back"),
                            tiku_str("button.forward"), tiku_str("help.home"),
                            tiku_str("prefs.fonts.done")};
    float pos = 0.0f, frac = 1.0f;

    content = tiku_window_content(window);
    l = layout(content);
    refit(c, l);
    controls(c, l);
    tiku_fill(s, content, TIKU_C_PANEL);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;

        if (d->id == HELP_PAGE) continue;
        if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&c->dialog, d->id)) flags |= TIKU_S_PRESSED;
        tiku_ui_button(s, d->rect, labels[d->id], flags);
    }
    {
        const char *title = c->doc ? tiku_doc_title(c->doc) : NULL;

        tiku_text_line_draw(s, l.title, TIKU_MEASURE_BOLD,
                            title ? title : c->page, TIKU_C_TEXT);
    }
    tiku_ui_sunken(s, l.paper, TIKU_C_DOC);
    if (c->doc) {
        const char *what = NULL;
        char cut[128];
        int cap = 0;
        tiku_doc_paint_t how;

        cut[0] = 0;
        if (c->oversize) {
            snprintf(cut, sizeof cut, "%s", tiku_str("help.too_long"));
        } else if (tiku_doc_full_first(c->doc, &what, &cap)) {
            snprintf(cut, sizeof cut, "%s %s (%d)",
                     tiku_str("help.cut"), what ? what : "", cap);
        }
        how.scroll = c->bar.value;
        how.focus_link = tiku_dialog_focused(&c->dialog, HELP_PAGE)
                             ? c->focus_link : TIKU_DOC_NONE;
        how.cut = cut[0] ? cut : NULL;
        how.image = NULL;
        how.image_ctx = NULL;
        tiku_doc_draw(s, c->doc, l.page, &how);
        if (tiku_scroll_max(&c->bar) > 0 && tiku_doc_height(c->doc) > 0) {
            pos = (float)c->bar.value / (float)tiku_scroll_max(&c->bar);
            frac = (float)l.page.h / (float)tiku_doc_height(c->doc);
        }
    }
    tiku_ui_scrollbar(s, l.bar, pos, frac, 0);
    tiku_text_line_draw(s, l.status, TIKU_MEASURE_PLAIN, c->status,
                        tiku_ink(TIKU_INK_FAINT));
}

/** @brief What a command does, whether it was clicked or typed. */
static void invoke(tiku_trk_help_window_t *c, int command)
{
    switch (command) {
    case HELP_BACK:    (void)tiku_trk_help_window_back(c); break;
    case HELP_FORWARD: (void)tiku_trk_help_window_forward(c); break;
    case HELP_HOME:    (void)tiku_trk_help_window_home(c); break;
    case HELP_DONE:    tiku_trk_help_window_close(c); break;
    default: break;
    }
}

/** @brief Keys the page answers first, then the panel's own controls. */
static int event(tiku_window_t *window, const tiku_event_t *ev, void *ctx)
{
    tiku_trk_help_window_t *c = ctx;
    help_layout_t l = layout(tiku_window_content(window));
    tiku_dialog_result_t r;
    int taken = 0;

    refit(c, l);
    controls(c, l);
    if (ev->type == TIKU_EVENT_KEY_DOWN && c->doc) {
        int page = tiku_scroll_page(&c->bar), line = tiku_scroll_line(&c->bar);

        switch (ev->key) {
        case TIKU_KEY_DOWN:  tiku_scroll_to(&c->bar, c->bar.value + line);
                             taken = 1; break;
        case TIKU_KEY_UP:    tiku_scroll_to(&c->bar, c->bar.value - line);
                             taken = 1; break;
        case TIKU_KEY_PAGE_DOWN:
                             tiku_scroll_to(&c->bar, c->bar.value + page);
                             taken = 1; break;
        case TIKU_KEY_PAGE_UP:
                             tiku_scroll_to(&c->bar, c->bar.value - page);
                             taken = 1; break;
        case TIKU_KEY_HOME:  tiku_scroll_to(&c->bar, 0); taken = 1; break;
        case TIKU_KEY_END:   tiku_scroll_to(&c->bar, tiku_scroll_max(&c->bar));
                             taken = 1; break;
        case TIKU_KEY_BACKSPACE:
                             (void)tiku_trk_help_window_back(c);
                             taken = 1; break;
        case TIKU_KEY_TAB:
            if (tiku_dialog_focused(&c->dialog, HELP_PAGE) ||
                c->dialog.focus == HELP_PAGE) {
                int back = (ev->modifiers & TIKU_MOD_SHIFT) != 0;
                int next = tiku_doc_link_next(c->doc, c->focus_link, back);

                /* The walk hands the Tab on when it comes round, so the
                 * buttons are still reachable from the keyboard. */
                if (next != TIKU_DOC_NONE &&
                    (c->focus_link == TIKU_DOC_NONE ||
                     (back ? next < c->focus_link : next > c->focus_link))) {
                    c->focus_link = next;
                    reveal(c, l);
                    taken = 1;
                } else {
                    c->focus_link = TIKU_DOC_NONE;
                }
            }
            break;
        case TIKU_KEY_RETURN:
            if (c->focus_link != TIKU_DOC_NONE &&
                c->dialog.focus == HELP_PAGE) {
                (void)tiku_trk_help_window_go(c,
                    tiku_doc_link_href(c->doc, c->focus_link));
                taken = 1;
            }
            break;
        default: break;
        }
    }
    if (!taken) {
        r = tiku_dialog_event(&c->dialog, ev);
        if (r.activated != TIKU_DIALOG_NONE) {
            invoke(c, r.activated);
            if (c->dirty) *c->dirty = 1;
            return 1;
        }
        taken = r.taken;
    }
    if (ev->type == TIKU_EVENT_WHEEL) {
        tiku_scroll_to(&c->bar, c->bar.value +
                       ev->dy * tiku_scroll_line(&c->bar));
        taken = 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 && c->doc) {
        if (ev->x >= l.bar.x && ev->x < l.bar.x + l.bar.w &&
            ev->y >= l.bar.y && ev->y < l.bar.y + l.bar.h) {
            /* The bar is drawn with a square arrow at each end and the
             * thumb in what is left, so that is the track a press is
             * measured against. */
            int arrow = l.bar.w;
            int track = l.bar.h - 2 * arrow;

            if (ev->y < l.bar.y + arrow) {
                tiku_scroll_to(&c->bar,
                               c->bar.value - tiku_scroll_line(&c->bar));
            } else if (ev->y >= l.bar.y + l.bar.h - arrow) {
                tiku_scroll_to(&c->bar,
                               c->bar.value + tiku_scroll_line(&c->bar));
            } else if (track > 0) {
                (void)tiku_scroll_press(&c->bar, track,
                                        ev->y - (l.bar.y + arrow));
            }
            taken = 1;
        } else if (ev->x >= l.page.x && ev->x < l.page.x + l.page.w &&
                   ev->y >= l.page.y && ev->y < l.page.y + l.page.h) {
            tiku_doc_hit_t h = tiku_doc_hit(c->doc, ev->x - l.page.x,
                                            ev->y - l.page.y + c->bar.value);

            if (h.kind == TIKU_DOC_HIT_LINK) {
                c->focus_link = h.link;
                (void)tiku_trk_help_window_go(c,
                    tiku_doc_link_href(c->doc, h.link));
            }
            taken = 1;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE && c->bar.tracking) {
        int arrow = l.bar.w;
        int track = l.bar.h - 2 * arrow;

        if (track > 0) {
            (void)tiku_scroll_drag(&c->bar, track,
                                   ev->y - (l.bar.y + arrow));
        }
        taken = 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_UP) tiku_scroll_release(&c->bar);
    if (c->dirty) *c->dirty = 1;
    return taken;
}

void tiku_trk_help_window_open(tiku_trk_help_window_t *c)
{
    tiku_rect_t frame;

    if (c->window && c->window->open) {
        tiku_workspace_activate(c->workspace, c->window);
        return;
    }
    if (!page_read(c, "index.html")) {
        /* No folder, or no page in it: the window still opens and says
         * so, since a Help that does nothing at all is worse. */
        if (!c->doc) {
            c->doc = calloc(1, sizeof *c->doc);
            if (c->doc) tiku_doc_init(c->doc);
        }
        if (c->doc) (void)tiku_doc_read(c->doc, "");
        say(c, tiku_str("help.no_folder"));
    }
    c->laid_for = 0;
    c->nback = c->nforward = 0;
    c->focus_link = TIKU_DOC_NONE;
    tiku_dialog_init(&c->dialog);
    frame.w = HELP_W;
    frame.h = HELP_H;
    frame.x = (c->screen_w - frame.w) / 2;
    frame.y = (c->screen_h - frame.h) / 3;
    if (frame.x < 0) frame.x = 0;
    if (frame.y < 0) frame.y = 0;
    c->window = tiku_workspace_open(c->workspace, tiku_str("title.help"),
                                    frame, draw, c, NULL);
    if (c->window) {
        tiku_window_set_handlers(c->window, event, NULL, NULL);
    }
    if (c->dirty) *c->dirty = 1;
}

void tiku_trk_help_window_close(tiku_trk_help_window_t *c)
{
    tiku_scroll_release(&c->bar);
    if (c->window && c->window->open) {
        tiku_workspace_close(c->workspace, c->window);
    }
    c->window = NULL;
    tiku_dialog_init(&c->dialog);
    if (c->dirty) *c->dirty = 1;
}

void tiku_trk_help_window_destroy(tiku_trk_help_window_t *c)
{
    tiku_trk_help_window_close(c);
    free(c->doc);
    c->doc = NULL;
}
