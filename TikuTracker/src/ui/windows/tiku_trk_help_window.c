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
#include <strings.h>

#include "tiku_doc_paint.h"
#include "tiku_markdown.h"
#include "tiku_font.h"
#include "tiku_strings.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_clipboard.h"
#include "tiku_field_event.h"
#include "tiku_list.h"
#include "tiku_image.h"
#include "tiku_iconpaint.h"
#include "tiku_icons_tiku.h"
#include "tiku_dl.h"
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

enum { HELP_BACK = 1, HELP_FORWARD, HELP_HOME, HELP_DONE, HELP_PAGE,
       HELP_FIND, HELP_PREV, HELP_NEXT, HELP_SEARCH, HELP_COPY, HELP_CODE,
       HELP_OUTLINE, HELP_BOOKMARK, HELP_BOOKMARKS, HELP_LINK, HELP_SIDE,
       HELP_LOCATION, HELP_GO, HELP_FIND_CLOSE, HELP_FIND_TOGGLE };
_Static_assert(HELP_PAGE == TIKU_TRK_HELP_CONTROL_PAGE,
               "the page's control id is named in the header");

#define HELP_W 820
#define HELP_H 560
#define HELP_BAR 15                 /* the scrollbar's width */

/** @brief Where the parts of the window go, from one description. */
typedef struct {
    tiku_rect_t back, forward, home, done, title, paper, page, bar, status;
    tiku_rect_t find, prev, next, search, copy, code, outline, bookmark;
    tiku_rect_t bookmarks, link, side;
    tiku_rect_t location, location_label, go, find_close, find_toggle;
} help_layout_t;

static void say(tiku_trk_help_window_t *c, const char *what);
static void document_location_sync(tiku_trk_help_window_t *c);

/** @brief File suffix selects the format; report bodies remain HTML. */
static int reader_markdown(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".md") || !strcasecmp(ext, ".markdown"));
}

/** @brief Search and display use exactly the same bounded source conversion. */
static int reader_parse(tiku_doc_t *doc, const char *name, const char *source)
{
    if (!reader_markdown(name)) { tiku_doc_read(doc, source); return 1; }
    size_t cap = tiku_markdown_html_size(source);
    if (!cap || cap > TIKU_TRK_READER_MEMORY_MAX) return 0;
    char *html = malloc(cap);
    if (!html) return 0;
    int ok = tiku_markdown_html(html, cap, source);
    if (ok) tiku_doc_read(doc, html);
    free(html); return ok;
}

#include "tiku_trk_reader_storage.inl"

#include "tiku_trk_help_reader.inl"

#include "tiku_trk_document_layout.inl"

/** @brief One geometry, read by the drawing and by the hit testing. */
static help_layout_t
layout(tiku_trk_help_window_t *owner, tiku_rect_t c)
{
    if (owner->document_mode) return document_layout(owner, c);
    help_layout_t l = {0};
    int bh = tiku_ui_button_h();
    int fh = tiku_font_plain()->height;
    int top = c.y + 10;
    int foot = c.y + c.h - fh - 12;

    l.back = (tiku_rect_t){c.x + 12, top, 74, bh};
    l.forward = (tiku_rect_t){c.x + 92, top, 84, bh};
    l.home = (tiku_rect_t){c.x + 182, top, 74, bh};
    l.done = (tiku_rect_t){c.x + c.w - 86, top, 74, bh};
    l.title = (tiku_rect_t){c.x + 266, top, c.w - 364, bh};
    if (l.title.w < 0) l.title.w = 0;
    int width = c.w - 24;
    if (width < 300) width = 300;
    int field = width - 248;
    l.find = (tiku_rect_t){c.x + 12, top + bh + 6, field, bh};
    l.prev = (tiku_rect_t){l.find.x + field + 4, l.find.y, 48, bh};
    l.next = (tiku_rect_t){l.prev.x + 52, l.find.y, 48, bh};
    l.search = (tiku_rect_t){l.next.x + 52, l.find.y, 144, bh};
    int tools = top + 2 * (bh + 6);
    l.outline = (tiku_rect_t){c.x + 12, tools, 80, bh};
    l.bookmarks = (tiku_rect_t){c.x + 96, tools, 88, bh};
    l.bookmark = (tiku_rect_t){c.x + 188, tools, 38, bh};
    l.copy = (tiku_rect_t){c.x + 230, tools, 54, bh};
    l.code = (tiku_rect_t){c.x + 288, tools, 88, bh};
    l.link = (tiku_rect_t){c.x + 380, tools, 88, bh};
    l.paper = (tiku_rect_t){c.x + 12, tools + bh + 8, c.w - 24,
                            foot - (tools + bh + 8) - 8};
    if (c.w < 500) {
        l.copy.x = c.x + 12; l.code.x = c.x + 70;
        l.link.x = c.x + 162;
        l.copy.y = l.code.y = l.link.y = tools + bh + 6;
        l.paper.y += bh + 6; l.paper.h -= bh + 6;
    }
    l.side = (tiku_rect_t){0};
    if (owner->reader && owner->reader->side) {
        if (c.w >= 640) {
            l.side = (tiku_rect_t){l.paper.x, l.paper.y, 188, l.paper.h};
            l.paper.x += 196; l.paper.w -= 196;
        } else {
            l.side = (tiku_rect_t){l.paper.x, l.paper.y, l.paper.w, 96};
            l.paper.y += 104; l.paper.h -= 104;
        }
    }
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

    if (name && name[0] == '@' && c->reader) {
        for (int i = 0; i < HELP_REPORTS; i++) {
            if (strcmp(name, c->reader->reports[i].name) ||
                !c->reader->reports[i].html) continue;
            if (!reader_replace(c, name, c->reader->reports[i].html)) return 0;
            reader_loaded(c); c->oversize = 0; c->focus_link = TIKU_DOC_NONE;
            snprintf(c->page, sizeof c->page, "%s", name);
            settle(c); tiku_scroll_to(&c->bar, 0); return 1;
        }
        return 0;
    }

    if (c->document_mode) {
        if (!document_path(name, path, sizeof path)) return 0;
    } else {
        if (!c->root[0] || !name_is_safe(name)) return 0;
        if (!reader_path(c, name, path, sizeof path)) return 0;
    }
    const char *why;
    char *text = reader_source(path, &why);
    if (!text) { say(c, why); return 0; }
    int ok = (*text || c->document_mode) && reader_replace(c, name, text);
    free(text);
    if (!ok) return 0;
    if (c->document_mode) {
        snprintf(c->root, sizeof c->root, "%s", path);
        char *slash = strrchr(c->root, '/');
        if (slash == c->root) slash[1] = 0;
        else if (slash) *slash = 0;
    }
    c->oversize = 0;
    reader_loaded(c);
    c->focus_link = TIKU_DOC_NONE;
    snprintf(c->page, sizeof c->page, "%s", c->document_mode ? path : name);
    if (c->document_mode) document_location_sync(c);
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
static void back_push(tiku_trk_help_window_t *c, const char *page,
                      tiku_trk_help_position_t at)
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
static void forward_push(tiku_trk_help_window_t *c, const char *page,
                         tiku_trk_help_position_t at)
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

#include "tiku_trk_help_navigation.inl"

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
    reader_init(c);
}

void tiku_trk_help_window_opener(tiku_trk_help_window_t *c,
                                 tiku_trk_help_open_fn fn, void *ctx)
{
    c->opener = fn;
    c->opener_ctx = ctx;
}

#include "tiku_trk_document_window.inl"

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
        char decoded[128];
        const char *anchor = href + 1;
        if (c->document_mode) {
            if (!document_unescape(anchor, strlen(anchor), decoded,
                                   sizeof decoded))
                return TIKU_TRK_HELP_NOWHERE;
            anchor = decoded;
        }
        int y = c->doc ? tiku_doc_anchor_y(c->doc, anchor) : TIKU_DOC_NONE;

        if (y == TIKU_DOC_NONE) {
            say(c, tiku_str("help.no_such_place"));
            return TIKU_TRK_HELP_NOWHERE;
        }
        back_push(c, c->page, reader_position(c));
        c->nforward = 0;
        tiku_scroll_to(&c->bar, y);
        say(c, "");
        return TIKU_TRK_HELP_ANCHOR;
    }
    if (c->document_mode) {
        char path[TIKU_PATH_MAX], anchor[128];
        if (!document_link(c, href, path, sizeof path, anchor, sizeof anchor)) {
            say(c, "Cannot open link. Only existing local Markdown and HTML "
                   "files are supported.");
            return TIKU_TRK_HELP_NOWHERE;
        }
        if (!tiku_trk_document_window_file(c, path))
            return TIKU_TRK_HELP_NOWHERE;
        snprintf(c->anchor, sizeof c->anchor, "%s", anchor);
        return TIKU_TRK_HELP_PAGE;
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
        tiku_trk_help_position_t was = reader_position(c);

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
    if (c->document_mode) {
        if (c->choose_file) c->choose_file(c->choose_ctx);
        return c->choose_file != NULL;
    }
    char from[TIKU_PATH_MAX];
    tiku_trk_help_position_t was = reader_position(c);

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
static int step(tiku_trk_help_window_t *c, const char *page,
                tiku_trk_help_position_t to, int back)
{
    char from[TIKU_PATH_MAX];
    tiku_trk_help_position_t was = reader_position(c);

    snprintf(from, sizeof from, "%s", c->page);
    if (strcmp(page, c->page) && !page_read(c, page)) {
        /* Nothing has been taken off either stack yet, so a page that
         * has gone missing since it was read costs the reader nothing
         * but the step. */
        return 0;
    }
    if (back) forward_push(c, from, was);
    else back_push(c, from, was);
    tiku_scroll_to(&c->bar, c->doc && (c->doc->laid_width != to.width ||
        c->doc->laid_height != to.height) ?
        tiku_doc_place_y(c->doc, to.place) : to.pixel);
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
    tiku_doc_place_t place = tiku_doc_place(c->doc, c->bar.value);
    int had = tiku_doc_laid(c->doc);
    int changed = c->doc->laid_width != l.page.w ||
        c->doc->laid_height != tiku_measure_height(TIKU_MEASURE_PLAIN);
    c->page_h = l.page.h;
    c->laid_for = l.page.w;
    /* Asked every time: laying out is free when neither the width nor
     * the text size has moved, and a size that moved without the width
     * would otherwise keep the geometry of the size before it. */
    (void)tiku_doc_layout(c->doc, l.page.w);
    tiku_scroll_set_range(&c->bar, tiku_doc_height(c->doc), l.page.h);
    if (had && changed)
        tiku_scroll_to(&c->bar, tiku_doc_place_y(c->doc, place));
    /* A page asked for by name is scrolled to it once it has a height. */
    if (c->anchor[0]) {
        int y = tiku_doc_anchor_y(c->doc, c->anchor);

        if (y != TIKU_DOC_NONE) tiku_scroll_to(&c->bar, y);
        else say(c, tiku_str("help.no_such_place"));
        c->anchor[0] = 0;
    }
}

/** @brief Register what can be pressed, and what may take the keyboard. */
static void controls(tiku_trk_help_window_t *c, help_layout_t l)
{
    tiku_dialog_begin(&c->dialog, TIKU_DIALOG_NONE,
                      c->document_mode ? TIKU_DIALOG_NONE : HELP_DONE);
    tiku_dialog_add(&c->dialog, HELP_PAGE, l.page, TIKU_DIALOG_INPUT, 1);
    tiku_dialog_add(&c->dialog, HELP_BACK, l.back, TIKU_DIALOG_BUTTON,
                    c->nback > 0);
    tiku_dialog_add(&c->dialog, HELP_FORWARD, l.forward, TIKU_DIALOG_BUTTON,
                    c->nforward > 0);
    tiku_dialog_add(&c->dialog, HELP_HOME, l.home, TIKU_DIALOG_BUTTON,
                    c->document_mode ? c->choose_file != NULL :
                    strcmp(c->page, "index.html") != 0);
    tiku_dialog_add(&c->dialog, HELP_DONE, l.done, TIKU_DIALOG_BUTTON,
                    !c->document_mode || c->page[0] == '/');
    if (c->reader) {
        if (l.find.w)
            tiku_dialog_add(&c->dialog, HELP_FIND, l.find,
                            TIKU_DIALOG_INPUT, 1);
        const int ids[] = {HELP_PREV, HELP_NEXT, HELP_SEARCH,
            HELP_COPY, HELP_CODE,
            HELP_OUTLINE, HELP_BOOKMARK, HELP_BOOKMARKS, HELP_LINK};
        const tiku_rect_t boxes[] = {l.prev, l.next, l.search, l.copy, l.code,
            l.outline, l.bookmark, l.bookmarks, l.link};
        for (int i = 0; i < 9; i++)
            if (boxes[i].w) tiku_dialog_add(&c->dialog, ids[i], boxes[i],
                            TIKU_DIALOG_BUTTON, 1);
        if (c->document_mode) {
            tiku_dialog_add(&c->dialog, HELP_LOCATION, l.location,
                            TIKU_DIALOG_INPUT, 1);
            tiku_dialog_add(&c->dialog, HELP_GO, l.go,
                            TIKU_DIALOG_BUTTON, 1);
            tiku_dialog_add(&c->dialog, HELP_FIND_TOGGLE, l.find_toggle,
                            TIKU_DIALOG_BUTTON, 1);
            if (l.find_close.w)
                tiku_dialog_add(&c->dialog, HELP_FIND_CLOSE, l.find_close,
                                TIKU_DIALOG_BUTTON, 1);
        }
        if (l.side.w) tiku_dialog_add(&c->dialog, HELP_SIDE, l.side,
                                      TIKU_DIALOG_INPUT, 1);
    }
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

#include "tiku_trk_document_browser.inl"
#include "tiku_trk_help_reader_ui.inl"

/** @brief The toolbar, the page in its paper, and what the window says. */
static void draw(tiku_window_t *window, tiku_surface_t *s,
                 tiku_rect_t content, void *ctx)
{
    tiku_trk_help_window_t *c = ctx;
    help_layout_t l;
    const char *labels[] = {"", tiku_str("button.back"),
                            tiku_str("button.forward"),
                            tiku_str(c->document_mode ?
                                     "reader.open" : "help.home"),
                            tiku_str(c->document_mode ?
                                     "reader.reload" : "prefs.fonts.done")};
    float pos = 0.0f, frac = 1.0f;

    content = tiku_window_content(window);
    l = layout(c, content);
    refit(c, l);
    controls(c, l);
    if (c->document_mode) document_menus(c);
    tiku_fill(s, content, TIKU_C_PANEL);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;

        if (d->id >= HELP_PAGE) continue;
        if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&c->dialog, d->id)) flags |= TIKU_S_PRESSED;
        if (c->document_mode)
            document_button(c, s, d->rect, labels[d->id], d->id, flags);
        else tiku_ui_button(s, d->rect, labels[d->id], flags);
    }
    if (c->document_mode) document_chrome(c, s, l);
    reader_tools_draw(c, s, l);
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
        how.image = reader_image_draw;
        how.image_ctx = c;
        if (c->reader) c->reader->surface = s;
        if (l.page.x < s->clip.x+s->clip.w &&
            l.page.y < s->clip.y+s->clip.h &&
            l.page.x+l.page.w > s->clip.x &&
            l.page.y+l.page.h > s->clip.y)
            tiku_doc_draw_marked(s, c->doc, l.page, &how, reader_mark, c);
        if (tiku_scroll_max(&c->bar) > 0 && tiku_doc_height(c->doc) > 0) {
            pos = (float)c->bar.value / (float)tiku_scroll_max(&c->bar);
            frac = (float)l.page.h / (float)tiku_doc_height(c->doc);
        }
    }
    tiku_ui_scrollbar(s, l.bar, pos, frac, 0);
    tiku_text_line_draw(s, l.status, TIKU_MEASURE_PLAIN,
                        c->reader && c->reader->hovered[0] ?
                        c->reader->hovered : c->status[0] ? c->status :
                        c->document_mode ? document_caption(c) : "",
                        tiku_ink(TIKU_INK_FAINT));
    if (c->document_mode) document_tip(c, s, content);
}

/** @brief What a command does, whether it was clicked or typed. */
static void invoke(tiku_trk_help_window_t *c, int command)
{
    if (c->document_mode && document_action(c, command)) return;
    if (reader_action(c, command)) return;
    switch (command) {
    case HELP_BACK:    (void)tiku_trk_help_window_back(c); break;
    case HELP_FORWARD: (void)tiku_trk_help_window_forward(c); break;
    case HELP_HOME:    (void)tiku_trk_help_window_home(c); break;
    case HELP_DONE:
        if (c->document_mode) (void)tiku_trk_document_window_reload(c);
        else tiku_trk_help_window_close(c);
        break;
    default: break;
    }
}

/** @brief Published native menus use the very same toolbar commands. */
static void document_menu_pick(tiku_window_t *w, int command, void *ctx)
{
    tiku_trk_help_window_t *c = ctx;
    (void)w;
    if (command == 100) tiku_trk_help_window_close(c);
    else if (command == HELP_FIND) document_find(c, 1);
    else invoke(c, command);
    if (c->dirty) *c->dirty = 1;
}

/** @brief Reflect history and reload availability in the desktop menu. */
static void document_menus(tiku_trk_help_window_t *c)
{
    if (!c->window) return;
    tiku_menuset_t set = { .nmenu = 3 };
    const char *titles[] = {"File", "Edit", "View"};
    const char *labels[][6] = {
        {"reader.open", "reader.reload", "reader.close"},
        {"reader.find", "help.copy", "help.copy_code", "help.copy_link",
         "reader.search_folder"},
        {"button.back", "button.forward", "help.outline", "help.bookmarks",
         "reader.bookmark", "reader.location_focus"}
    };
    const int ids[][6] = {{HELP_HOME, HELP_DONE, 100},
        {HELP_FIND, HELP_COPY, HELP_CODE, HELP_LINK, HELP_SEARCH},
        {HELP_BACK, HELP_FORWARD, HELP_OUTLINE, HELP_BOOKMARKS,
         HELP_BOOKMARK, HELP_LOCATION}};
    const char keys[][6] = {{'o','r','w'}, {'f','c',0,0,0}, {0,0,0,0,'d','l'}};
    const int counts[] = {3, 5, 6};
    for (int m = 0; m < 3; m++) {
        tiku_menu_list_t *menu = &set.menu[m];
        snprintf(menu->title, sizeof menu->title, "%s", titles[m]);
        menu->nitem = counts[m];
        for (int i = 0; i < menu->nitem; i++) {
            tiku_menu_entry_t *item = &menu->item[i];
            snprintf(item->label, sizeof item->label, "%s",
                     tiku_str(labels[m][i]));
            item->command = ids[m][i]; item->enabled = 1;
            item->sc = keys[m][i]; item->mods = item->sc ? TIKU_MOD_CMD : 0;
            if (item->command == HELP_BACK) item->enabled = c->nback > 0;
            if (item->command == HELP_FORWARD) item->enabled = c->nforward > 0;
            if (item->command == HELP_DONE) item->enabled = c->page[0] == '/';
            if (item->command == HELP_HOME)
                item->enabled = c->choose_file != NULL;
            if (c->reader && item->command == HELP_OUTLINE)
                item->marked = c->reader->side == 1;
            if (c->reader && item->command == HELP_BOOKMARKS)
                item->marked = c->reader->side == 3;
        }
    }
    tiku_window_publish_menus(c->window, &set, document_menu_pick, c);
}

/** @brief Keys the page answers first, then the panel's own controls. */
static int event(tiku_window_t *window, const tiku_event_t *ev, void *ctx)
{
    tiku_trk_help_window_t *c = ctx;
    help_layout_t l = layout(c, tiku_window_content(window));
    tiku_dialog_result_t r;
    int taken = 0;

    c->hover_damage = (tiku_rect_t){0};
    if (document_hover(c, ev, l)) return 1;
    refit(c, l);
    controls(c, l);
    if (c->document_mode && document_event(c, ev, l)) {
        if (c->dirty) *c->dirty = 1;
        return 1;
    }
    if (c->document_mode && ev->type == TIKU_EVENT_KEY_DOWN) {
        int cmd = (ev->modifiers & TIKU_MOD_CMD) != 0;
        if (cmd && (ev->key == 'o' || ev->key == 'O')) {
            invoke(c, HELP_HOME); return 1;
        }
        if (cmd && (ev->key == 'r' || ev->key == 'R')) {
            invoke(c, HELP_DONE); return 1;
        }
        if ((ev->modifiers & TIKU_MOD_OPTION) &&
            (ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT)) {
            invoke(c, ev->key == TIKU_KEY_LEFT ? HELP_BACK : HELP_FORWARD);
            return 1;
        }
    }
    if (reader_event(c, ev, l)) {
        if (c->dirty) *c->dirty = 1;
        return 1;
    }
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

int tiku_trk_help_window_ready(const tiku_trk_help_window_t *c)
{
    return c != NULL && c->root[0] != '\0';
}

void tiku_trk_help_window_open_page(tiku_trk_help_window_t *c,
                                    const char *page)
{
    if (c == NULL) return;
    if (c->window && c->window->open) {
        /* Already open on some other page: it goes to this one, and the
         * page it was showing joins the history the way a followed link
         * does. */
        tiku_workspace_activate(c->workspace, c->window);
        if (page != NULL && *page) (void)tiku_trk_help_window_go(c, page);
        return;
    }
    tiku_trk_help_window_open(c);
    if (page != NULL && *page && c->window != NULL) {
        if (tiku_trk_help_window_go(c, page) == TIKU_TRK_HELP_PAGE) {
            /* Opened ON the page, not at the contents by way of it:
             * there is nowhere behind, and Contents is the way home. */
            c->nback = c->nforward = 0;
        }
    }
}

void tiku_trk_help_window_open(tiku_trk_help_window_t *c)
{
    tiku_rect_t frame;

    if (c->window && c->window->open) {
        tiku_workspace_activate(c->workspace, c->window);
        return;
    }
    if (c->document_mode && !c->doc) {
        c->doc = calloc(1, sizeof *c->doc);
        if (c->doc) {
            tiku_doc_init(c->doc);
            tiku_doc_read(c->doc, "<h1>TikuReader</h1>"
                "<p>A quiet place to read local documents.</p>"
                "<h2>Open a document</h2><p>Choose Open or press Ctrl+O "
                "to browse Markdown "
                "(.md, .markdown) and HTML (.html, .htm) files.</p>"
                "<h2>Find your way</h2><p>Follow local links, move Back "
                "and Forward, or show Outline and Bookmarks from View. "
                "Ctrl+L selects the Location bar. Find opens a search bar "
                "below the page; Escape hides it. Edit &gt; Search folder "
                "looks through documents beside this one.</p>"
                "<h2>Read safely</h2><p>Documents are read-only. "
                "Scripts, forms and web requests are not run. "
                "Reload reads your latest saved changes.</p>");
            reader_loaded(c);
        }
    } else if (!c->document_mode && !page_read(c, "index.html")) {
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
    if (!c->document_mode) c->nback = c->nforward = 0;
    c->focus_link = TIKU_DOC_NONE;
    tiku_dialog_init(&c->dialog);
    frame.w = HELP_W;
    frame.h = HELP_H;
    frame.x = (c->screen_w - frame.w) / 2;
    frame.y = (c->screen_h - frame.h) / 3;
    /* Leave the right-hand Deskbar clear of the reader's Open/Reload row. */
    if (c->document_mode && frame.x > 24) frame.x = 24;
    if (frame.x < 0) frame.x = 0;
    if (frame.y < 0) frame.y = 0;
    c->window = tiku_workspace_open(c->workspace,
                                    c->document_mode ? "TikuReader" :
                                                       tiku_str("title.help"),
                                    frame, draw, c, NULL);
    if (c->window) {
        tiku_window_set_handlers(c->window, event, NULL, NULL);
        if (c->document_mode) document_menus(c);
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
    reader_doc_free(c->doc);
    c->doc = NULL;
    if (c->reader) {
        reader_images_clear(c->reader);
        for (int i = 0; i < HELP_REPORTS; i++) free(c->reader->reports[i].html);
        free(c->reader->text); free(c->reader->starts); free(c->reader->folded);
        free(c->reader); c->reader = NULL;
    }
}
