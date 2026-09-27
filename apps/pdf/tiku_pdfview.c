/* TikuPDF: a document viewer over the PDF kit.  Every read of a document
 * is a worker's, one at a time per document; the window only draws what
 * came back, so a 1,400-page datasheet scrolls while pages are drawn.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_pdfview.h"
#include "tiku_clipboard.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_font.h"
#include "tiku_list.h"
#include "tiku_measure.h"
#include "tiku_model.h"
#include "tiku_pdf.h"
#include "tiku_scroll.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PV_MARGIN      12           /* around the pages, in pixels      */
#define PV_GAP         10           /* between two pages                */
#define PV_SIDE_W      220          /* the outline's column             */
#define PV_BAR         15           /* a scrollbar's width              */
#define PV_TILES       48           /* pages kept drawn                 */
#define PV_PIXELS      (24L << 20)  /* the pixels those may hold        */
#define PV_PAGE_PIXELS (16L << 20)  /* the most one page is drawn with  */
#define PV_INFLIGHT    2            /* pages asked for, not yet back    */
#define PV_TEXT_KEEP   64           /* pages whose text is kept         */
#define PV_MATCHES     5000
#define PV_SEARCH_STEP 24           /* pages one search job reads       */
#define PV_HISTORY     32
#define PV_LINE        40           /* what an arrow or a wheel notch scrolls */
#define PV_THUMB_W     150          /* a thumbnail's box, in pixels     */
#define PV_THUMB_H     190
#define PV_THUMB_KEEP  64           /* thumbnails kept drawn            */

enum { PV_OPEN = 1, PV_CLOSE, PV_COPY, PV_SELECT_ALL, PV_FIND, PV_FIND_NEXT,
       PV_FIND_PREV, PV_OUTLINE, PV_ZOOM_IN, PV_ZOOM_OUT, PV_ACTUAL,
       PV_FIT_WIDTH, PV_FIT_PAGE, PV_PREV, PV_NEXT, PV_FIRST, PV_LAST,
       PV_BACK, PV_MATCH_CASE, PV_WHOLE_WORDS, PV_THUMBS, PV_PAGE_FIELD,
       PV_FIND_FIELD, PV_CANVAS, PV_SIDE };
enum { PV_FIXED, PV_BY_WIDTH, PV_BY_PAGE };

/** @brief An open document as its jobs share it. */
typedef struct {
    tiku_pdf_t *pdf;                /* read by one job at a time: the key */
    int refs;                       /* the viewer and each job; loop only */
    char key[TIKU_WORK_KEY];
    atomic_int search;              /* the search a job still belongs to */
} pv_doc_t;

/** @brief A page drawn at one scale; pending while a worker draws it. */
typedef struct {
    int page, w, h, pending;
    double scale;                   /* the view's scale it was asked at  */
    uint32_t *px;
    unsigned long used;             /* the frame that last showed it     */
} pv_tile_t;

/** @brief What a page says, once a worker has read it: links, text. */
typedef struct {
    tiku_pdf_link_t *link;
    tiku_pdf_char_t *ch;
    int nlink, text, failed;
    long nch;
    unsigned long used;
} pv_page_t;

/** @brief A search hit: its page and a box a line, four at most. */
typedef struct {
    int page, nr;
    float r[4][4];
} pv_match_t;

/** @brief A place in the text: a page, and a gap between characters. */
typedef struct { int page; long at; } pv_mark_t;

/** @brief Where the parts of the window go, from one description. */
typedef struct {
    tiku_rect_t tools, side, canvas, vbar, hbar, status, page, of, find;
} pv_layout_t;

struct tiku_pdfview {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty, refs, generation, loading;
    void (*choose)(void *ctx);
    void *ctx;
    char path[TIKU_PATH_MAX], title[256], status[320], hint[320];
    pv_doc_t *doc;
    int npage, widest;              /* pages, and the widest one         */
    float *pw, *ph;                 /* each page as drawn at scale 1     */
    long *top, height, width;       /* the pages laid out at the scale   */
    pv_page_t *pages;
    int ntext;                      /* pages holding their text          */
    double scale, laid_scale;
    int fit, laid_w, laid_h;
    tiku_scroll_t vbar, hbar;
    int vdrag, hdrag;
    pv_tile_t tile[PV_TILES];
    int inflight;
    unsigned long frame;
    tiku_pdf_outline_t *outline;    /* the document's, depth first       */
    int noutline, nrows, side, follow;
    int thumbs;                     /* the side column shows the pages   */
    pv_tile_t thumb[PV_THUMB_KEEP]; /* pages drawn small for it          */
    int thumb_top, thumb_busy;      /* scrolled by; a thumbnail asked for */
    int thumb_seen;                 /* the page the column last followed */
    unsigned char *folded;
    int *rows;                      /* outline entries shown, in order   */
    tiku_list_t list;
    tiku_dialog_t dialog;
    tiku_field_t find, go, pass;
    tiku_field_input_t find_in, go_in, pass_in;
    int locked, may_copy;           /* waiting for a password; copying ok */
    char locked_path[TIKU_PATH_MAX];
    pv_match_t *match;
    int nmatch, cur, searching, searched, search_gen, want_next;
    char query[TIKU_FIELD_MAX];     /* what the matches are of           */
    int match_case, whole_words;    /* how the query is compared         */
    pv_mark_t anchor, focus;        /* a selection runs between the two  */
    int selecting, selected, pressing, press_x, press_y;
    int link_page, link_at;         /* the link under the pointer, or -1 */
    int press_page, press_link;     /* the link a press began on         */
    struct { int page; double y; } back[PV_HISTORY];
    int nback;
    uint32_t *view;                 /* the canvas, composed each frame   */
    int view_w, view_h;
};

static void pv_menus(tiku_pdfview_t *c);
static void pv_action(tiku_pdfview_t *c, int id);
static int pv_enabled(tiku_pdfview_t *c, int id);
static void pv_zoom_step(tiku_pdfview_t *c, int dir);
static void pv_outline_start(tiku_pdfview_t *c);
static int pv_query(const char *text, uint32_t *q, int max, int keep_case);
static void pv_marks(tiku_pdfview_t *c, int p, int x, int y);
static void pv_side_draw(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r);
static void pv_open_address(tiku_pdfview_t *c, const char *uri);
static void pv_forget_password(tiku_pdfview_t *c);

/** @brief Something the window shows has changed. */
static void pv_changed(tiku_pdfview_t *c)
{
    if (c->dirty) *c->dirty = 1;
}

/** @brief Say @p text in the status line until something else is said. */
static void pv_say(tiku_pdfview_t *c, const char *text)
{
    snprintf(c->status, sizeof c->status, "%s", text ? text : "");
    pv_changed(c);
}

/** @brief The same half-open rectangle rule for every hit. */
static int pv_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief One job or the viewer fewer; the last one closes it. */
static void pv_doc_unref(pv_doc_t *d)
{
    if (d && --d->refs == 0) {
        tiku_pdf_close(d->pdf);
        free(d);
    }
}

/** @brief Everything the shown document brought, given back; the view
 *         is left empty.  Jobs still queued for it are cancelled. */
static void pv_forget(tiku_pdfview_t *c)
{
    if (c->doc) {
        atomic_store(&c->doc->search, -1);
        (void)tiku_work_cancel(c->doc->key);
        pv_doc_unref(c->doc);
        c->doc = NULL;
    }
    for (int i = 0; i < PV_TILES; i++) {
        free(c->tile[i].px);
        memset(&c->tile[i], 0, sizeof c->tile[i]);
        c->tile[i].page = -1;
    }
    for (int i = 0; i < PV_THUMB_KEEP; i++) {
        free(c->thumb[i].px);
        memset(&c->thumb[i], 0, sizeof c->thumb[i]);
        c->thumb[i].page = -1;
    }
    c->thumb_top = c->thumb_busy = 0;
    c->thumb_seen = -1;
    for (int p = 0; c->pages && p < c->npage; p++) {
        tiku_pdf_links_free(c->pages[p].link, c->pages[p].nlink);
        free(c->pages[p].ch);
    }
    free(c->pages); free(c->pw); free(c->ph); free(c->top);
    free(c->folded); free(c->rows); free(c->match);
    tiku_pdf_outline_free(c->outline, c->noutline);
    c->pages = NULL; c->pw = c->ph = NULL; c->top = NULL;
    c->folded = NULL; c->rows = NULL; c->match = NULL; c->outline = NULL;
    c->npage = c->noutline = c->nrows = c->nmatch = c->ntext = 0;
    c->inflight = c->searching = c->searched = c->want_next = 0;
    c->selected = c->selecting = c->pressing = c->nback = 0;
    c->cur = c->link_page = c->link_at = c->press_page = c->press_link = -1;
    c->widest = 0;
    c->height = c->width = 0; c->laid_w = c->laid_h = 0;
    c->laid_scale = 0; c->search_gen++; c->query[0] = 0;
    c->path[0] = c->title[0] = c->hint[0] = 0;
    if (c->window)
        snprintf(c->window->title, sizeof c->window->title, "TikuPDF");
    (void)tiku_list_set_count(&c->list, 0);
    tiku_scroll_init(&c->vbar, PV_LINE);
    tiku_scroll_init(&c->hbar, PV_LINE);
}

/** @brief Release the viewer after the last queued job retires. */
static void pv_unref(tiku_pdfview_t *c)
{
    if (--c->refs) return;
    pv_forget(c);
    free(c->view);
    free(c);
}

#include "tiku_pdfview_jobs.inl"
#include "tiku_pdfview_view.inl"
#include "tiku_pdfview_text.inl"

/** @brief Menu and control labels, by command. */
static const char *pv_label(int id)
{
    static const char *const labels[] = { "", "Open...", "Close", "Copy",
        "Select Page Text", "Find...", "Find Next", "Find Previous",
        "Outline", "Zoom In", "Zoom Out", "Actual Size", "Fit Width",
        "Fit Page", "Previous Page", "Next Page", "First Page", "Last Page",
        "Back", "Match Case", "Whole Words", "Thumbnails" };
    return id >= PV_OPEN && id <= PV_THUMBS ? labels[id] : "";
}

/** @brief Whether command @p id can do anything now. */
static int pv_enabled(tiku_pdfview_t *c, int id)
{
    int page = c->npage ? pv_current(c) : 0;

    if (id == PV_OPEN) return c->choose && !c->loading;
    if (id == PV_CLOSE) return 1;
    if (!c->doc) return 0;
    if (id == PV_OUTLINE) return c->noutline > 0;
    if (id == PV_COPY) return c->selected;
    if (id == PV_FIND_NEXT || id == PV_FIND_PREV)
        return c->find.text[0] && !c->locked;
    if (id == PV_PREV || id == PV_FIRST) return page > 0;
    if (id == PV_NEXT || id == PV_LAST) return page + 1 < c->npage;
    if (id == PV_BACK) return c->nback > 0;
    if (id == PV_ZOOM_IN) return c->scale < 3.99;
    if (id == PV_ZOOM_OUT) return c->scale > 0.26;
    return 1;
}

/** @brief One step up or down the zoom ladder from the scale shown. */
static void pv_zoom_step(tiku_pdfview_t *c, int dir)
{
    static const double steps[] = { 0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0,
        1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0 };
    int n = (int)(sizeof steps / sizeof steps[0]), i;
    double to = c->scale;

    if (dir > 0) {
        for (i = 0; i < n && steps[i] <= c->scale * 1.01; i++) {}
        to = i < n ? steps[i] : steps[n - 1];
    } else {
        for (i = n - 1; i >= 0 && steps[i] >= c->scale * 0.99; i--) {}
        to = i >= 0 ? steps[i] : steps[0];
    }
    pv_set_scale(c, PV_FIXED, to);
}

/** @brief Every button, menu row and shortcut ends here. */
static void pv_action(tiku_pdfview_t *c, int id)
{
    int page = c->npage ? pv_current(c) : 0;

    if (id == PV_OPEN) {
        if (c->choose && !c->loading) c->choose(c->ctx);
    } else if (id == PV_CLOSE) {
        tiku_pdfview_close(c);
        return;
    } else if (!c->doc) {
        return;
    } else if (id == PV_COPY) pv_copy(c);
    else if (id == PV_SELECT_ALL) pv_select_page(c, page);
    else if (id == PV_FIND) {
        tiku_dialog_focus(&c->dialog, PV_FIND_FIELD);
        tiku_field_key(&c->find, TIKU_FIELD_SELECT_ALL, 0, 0);
    } else if (id == PV_FIND_NEXT) pv_find(c, 1);
    else if (id == PV_FIND_PREV) pv_find(c, -1);
    else if (id == PV_MATCH_CASE || id == PV_WHOLE_WORDS) {
        if (id == PV_MATCH_CASE) c->match_case = !c->match_case;
        else c->whole_words = !c->whole_words;
        if (c->query[0]) {          /* the matches were of the other way */
            c->query[0] = '\0';
            pv_find(c, 1);
        }
    }
    else if (id == PV_OUTLINE) {
        c->side = !(c->side && !c->thumbs) && c->noutline > 0;
        c->thumbs = 0;
    } else if (id == PV_THUMBS) {
        c->side = !(c->side && c->thumbs);
        c->thumbs = c->side;
    }
    else if (id == PV_ZOOM_IN) pv_zoom_step(c, 1);
    else if (id == PV_ZOOM_OUT) pv_zoom_step(c, -1);
    else if (id == PV_ACTUAL) pv_set_scale(c, PV_FIXED, 1.0);
    else if (id == PV_FIT_WIDTH) pv_set_scale(c, PV_BY_WIDTH, c->scale);
    else if (id == PV_FIT_PAGE) pv_set_scale(c, PV_BY_PAGE, c->scale);
    else if (id == PV_PREV && page > 0) pv_go(c, page - 1, 0, 0);
    else if (id == PV_NEXT && page + 1 < c->npage) pv_go(c, page + 1, 0, 0);
    else if (id == PV_FIRST) pv_go(c, 0, 0, 0);
    else if (id == PV_LAST) pv_go(c, c->npage - 1, 0, 0);
    else if (id == PV_BACK) pv_back(c);
    pv_menus(c);
    pv_changed(c);
}

/** @brief Keys the canvas answers: scrolling, paging, zoom steps. */
static int pv_canvas_key(tiku_pdfview_t *c, const tiku_event_t *ev)
{
    unsigned key = ev->key;
    int page = c->laid_h - PV_LINE, shift = ev->modifiers & TIKU_MOD_SHIFT;

    if (!c->doc) return 0;
    if (key == TIKU_KEY_UP) pv_scroll(c, 0, -PV_LINE);
    else if (key == TIKU_KEY_DOWN) pv_scroll(c, 0, PV_LINE);
    else if (key == TIKU_KEY_PAGE_UP || (key == ' ' && shift))
        pv_scroll(c, 0, -page);
    else if (key == TIKU_KEY_PAGE_DOWN || key == ' ') pv_scroll(c, 0, page);
    else if (key == TIKU_KEY_HOME) pv_action(c, PV_FIRST);
    else if (key == TIKU_KEY_END) pv_action(c, PV_LAST);
    else if (key == TIKU_KEY_LEFT || key == TIKU_KEY_RIGHT) {
        int dir = key == TIKU_KEY_LEFT ? -1 : 1;
        if (tiku_scroll_max(&c->hbar) > 0) pv_scroll(c, dir * PV_LINE, 0);
        else pv_action(c, dir < 0 ? PV_PREV : PV_NEXT);
    } else if (key == '+' || key == '=') pv_action(c, PV_ZOOM_IN);
    else if (key == '-') pv_action(c, PV_ZOOM_OUT);
    else return 0;
    return 1;
}

/** @brief Command keys, whatever has the focus, unless a field wants
 *         its own copy, paste and select all. */
static int pv_command_key(tiku_pdfview_t *c, const tiku_event_t *ev)
{
    int field = c->dialog.focus == PV_FIND_FIELD ||
                c->dialog.focus == PV_PAGE_FIELD;
    unsigned key = ev->key;

    if (!(ev->modifiers & TIKU_MOD_CMD)) return 0;
    if (field && (key == 'c' || key == 'x' || key == 'v' || key == 'a' ||
                  key == 'z')) return 0;
    if (key == 'o') pv_action(c, PV_OPEN);
    else if (key == 'w' || key == 'q') pv_action(c, PV_CLOSE);
    else if (key == 'c') pv_action(c, PV_COPY);
    else if (key == 'a') pv_action(c, PV_SELECT_ALL);
    else if (key == 'f') pv_action(c, PV_FIND);
    else if (key == 'g') pv_action(c, ev->modifiers & TIKU_MOD_SHIFT ?
                                      PV_FIND_PREV : PV_FIND_NEXT);
    else if (key == '+' || key == '=') pv_action(c, PV_ZOOM_IN);
    else if (key == '-') pv_action(c, PV_ZOOM_OUT);
    else if (key == '0') pv_action(c, PV_ACTUAL);
    else if (key == '[') pv_action(c, PV_BACK);
    else if (key == TIKU_KEY_UP) pv_action(c, PV_FIRST);
    else if (key == TIKU_KEY_DOWN) pv_action(c, PV_LAST);
    else return 0;
    return 1;
}

/** @brief The password field emptied, the password gone from memory. */
static void pv_forget_password(tiku_pdfview_t *c)
{
    memset(c->pass.text, 0, sizeof c->pass.text);
    tiku_field_start(&c->pass, "");
    tiku_field_input_init(&c->pass_in);
}

/** @brief A key or typed text for the focused field: Return in the find
 *         field searches (or, for a locked document, tries the password
 *         typed there), in the page field goes. */
static int pv_field_event(tiku_pdfview_t *c, const tiku_event_t *ev,
                          const pv_layout_t *l)
{
    int find = c->dialog.focus == PV_FIND_FIELD;
    tiku_field_t *f = find ? (c->locked ? &c->pass : &c->find) : &c->go;
    tiku_field_input_t *in = find ? (c->locked ? &c->pass_in : &c->find_in)
                                  : &c->go_in;
    tiku_field_result_t r;

    if (!find && c->dialog.focus != PV_PAGE_FIELD) return 0;
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_TAB) return 0;
    r = tiku_field_event(f, in, ev, find ? l->find : l->page, 1);
    if (r == TIKU_FIELD_COMMITTED && find && c->locked) {
        (void)pv_load(c, c->locked_path, c->pass.text);
        pv_forget_password(c);
    } else if (r == TIKU_FIELD_COMMITTED && find) {
        pv_find(c, ev->modifiers & TIKU_MOD_SHIFT ? -1 : 1);
    } else if (r == TIKU_FIELD_COMMITTED) {
        pv_go_typed(c);
    } else if (r == TIKU_FIELD_CANCELLED) {
        if (find && c->locked) {
            c->locked = 0;
            pv_forget_password(c);
            pv_say(c, "The locked PDF was left closed.");
        }
        tiku_dialog_focus(&c->dialog, PV_CANVAS);
    } else if (r == TIKU_FIELD_REJECTED) {
        pv_say(c, "That text does not fit the field.");
    }
    pv_menus(c);
    pv_changed(c);
    return ev->type == TIKU_EVENT_KEY_DOWN ||
           ev->type == TIKU_EVENT_TEXT_INPUT || r != TIKU_FIELD_IGNORED;
}

/** @brief Keys: commands first, then the focused field, list or canvas. */
static int pv_key(tiku_pdfview_t *c, const tiku_event_t *ev,
                  const pv_layout_t *l)
{
    if (ev->type == TIKU_EVENT_KEY_DOWN && pv_command_key(c, ev)) return 1;
    if (pv_field_event(c, ev, l)) return 1;
    if (ev->type != TIKU_EVENT_KEY_DOWN) return 0;
    if (c->dialog.focus == PV_SIDE && pv_side_key(c, ev, l)) return 1;
    if (ev->key == TIKU_KEY_ESCAPE && c->selected) {
        c->selected = 0;
        pv_changed(c);
        return 1;
    }
    if (c->dialog.focus == PV_CANVAS || c->dialog.focus == TIKU_DIALOG_NONE)
        return pv_canvas_key(c, ev);
    return 0;
}

/** @brief Pointer input for the fields, outline, bars and canvas; the
 *         toolbar buttons are the dialog's. */
static int pv_pointer(tiku_pdfview_t *c, const tiku_event_t *ev,
                      const pv_layout_t *l)
{
    int down = ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1;

    if (down || ev->type == TIKU_EVENT_WHEEL) {
        if (pv_in(l->find, ev->x, ev->y) || pv_in(l->page, ev->x, ev->y)) {
            if (down) {
                tiku_dialog_focus(&c->dialog, pv_in(l->find, ev->x, ev->y)
                                  ? PV_FIND_FIELD : PV_PAGE_FIELD);
                (void)pv_field_event(c, ev, l);
            }
            return 1;
        }
    } else if ((c->find_in.dragging || c->go_in.dragging) &&
               pv_field_event(c, ev, l)) {
        return 1;
    }
    if (pv_side_pointer(c, ev, l)) return 1;
    if (pv_bars_pointer(c, ev, l)) return 1;
    return pv_canvas_pointer(c, ev, l);
}

/** @brief Every window event: keys, then pointer areas, then buttons. */
static int pv_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_pdfview_t *c = ctx;
    pv_layout_t l = pv_layout(c, tiku_window_content(w));
    int pointer = ev->type == TIKU_EVENT_POINTER_DOWN ||
                  ev->type == TIKU_EVENT_POINTER_MOVE ||
                  ev->type == TIKU_EVENT_POINTER_UP ||
                  ev->type == TIKU_EVENT_WHEEL;

    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        c->pressing = c->selecting = c->vdrag = c->hdrag = 0;
        tiku_scroll_release(&c->vbar);
        tiku_scroll_release(&c->hbar);
    }
    if ((ev->type == TIKU_EVENT_KEY_DOWN ||
         ev->type == TIKU_EVENT_TEXT_INPUT) && pv_key(c, ev, &l)) return 1;
    if (pointer && c->dialog.capture == TIKU_DIALOG_NONE) {
        if (ev->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_dialog_result_t r = tiku_dialog_event(&c->dialog, ev);
            if (r.taken) { pv_changed(c); return 1; }
        }
        if (pv_pointer(c, ev, &l)) return 1;
        if (ev->type == TIKU_EVENT_POINTER_DOWN) return 0;
    }
    tiku_dialog_result_t r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0) pv_action(c, r.activated);
    if (r.taken) pv_changed(c);
    return r.taken;
}

/** @brief The native window went away: nothing may point at it. */
static void pv_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_pdfview_t *c = ctx;
    (void)w;
    c->window = NULL;
    c->generation++;
    pv_forget(c);
    pv_changed(c);
}

/** @brief Published menus hand their picks to the one action path. */
static void pv_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    pv_action(ctx, id);
}

/** @brief Add command @p id to menu @p m, with its shortcut. */
static void pv_menu_add(tiku_pdfview_t *c, tiku_menu_list_t *m, int id,
                        char sc, unsigned mods, int marked)
{
    tiku_menu_entry_t *e;

    if (m->nitem >= TIKU_MENUSET_ITEMS) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", pv_label(id));
    e->command = id;
    e->enabled = (unsigned char)pv_enabled(c, id);
    e->sc = sc;
    e->mods = sc ? mods : 0;
    e->marked = (unsigned char)marked;
}

/** @brief File, Edit, View and Go, as the controls stand. */
static void pv_menus(tiku_pdfview_t *c)
{
    tiku_menuset_t menus;
    static const char *const titles[] = { "File", "Edit", "View", "Go" };

    if (!c->window) return;
    memset(&menus, 0, sizeof menus);
    menus.nmenu = 4;
    for (int i = 0; i < 4; i++)
        snprintf(menus.menu[i].title, sizeof menus.menu[i].title, "%s",
                 titles[i]);
    pv_menu_add(c, &menus.menu[0], PV_OPEN, 'o', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[0], PV_CLOSE, 'w', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[1], PV_COPY, 'c', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[1], PV_SELECT_ALL, 'a', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[1], PV_FIND, 'f', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[1], PV_FIND_NEXT, 'g', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[1], PV_FIND_PREV, 'g',
                TIKU_MOD_CMD | TIKU_MOD_SHIFT, 0);
    pv_menu_add(c, &menus.menu[1], PV_MATCH_CASE, 0, 0, c->match_case);
    pv_menu_add(c, &menus.menu[1], PV_WHOLE_WORDS, 0, 0, c->whole_words);
    pv_menu_add(c, &menus.menu[2], PV_OUTLINE, 0, 0,
                c->side && !c->thumbs && c->noutline > 0);
    pv_menu_add(c, &menus.menu[2], PV_THUMBS, 0, 0, c->side && c->thumbs);
    pv_menu_add(c, &menus.menu[2], PV_ZOOM_IN, '+', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[2], PV_ZOOM_OUT, '-', TIKU_MOD_CMD, 0);
    pv_menu_add(c, &menus.menu[2], PV_ACTUAL, '0', TIKU_MOD_CMD,
                c->fit == PV_FIXED && fabs(c->scale - 1) < 1e-9);
    pv_menu_add(c, &menus.menu[2], PV_FIT_WIDTH, 0, 0,
                c->fit == PV_BY_WIDTH);
    pv_menu_add(c, &menus.menu[2], PV_FIT_PAGE, 0, 0, c->fit == PV_BY_PAGE);
    pv_menu_add(c, &menus.menu[3], PV_PREV, 0, 0, 0);
    pv_menu_add(c, &menus.menu[3], PV_NEXT, 0, 0, 0);
    pv_menu_add(c, &menus.menu[3], PV_FIRST, 0, 0, 0);
    pv_menu_add(c, &menus.menu[3], PV_LAST, 0, 0, 0);
    pv_menu_add(c, &menus.menu[3], PV_BACK, '[', TIKU_MOD_CMD, 0);
    tiku_window_publish_menus(c->window, &menus, pv_menu_pick, c);
}

tiku_pdfview_t *tiku_pdfview_new(tiku_workspace_t *workspace, int *dirty,
                                 void (*choose)(void *ctx), void *ctx)
{
    tiku_pdfview_t *c = calloc(1, sizeof *c);

    if (!c) return NULL;
    c->workspace = workspace;
    c->dirty = dirty;
    c->refs = 1;
    c->choose = choose;
    c->ctx = ctx;
    c->fit = PV_BY_WIDTH;
    c->scale = 1;
    c->side = 1;
    tiku_dialog_init(&c->dialog);
    tiku_list_init(&c->list, 1);
    tiku_field_start(&c->find, "");
    tiku_field_start(&c->go, "");
    tiku_field_input_init(&c->find_in);
    tiku_field_input_init(&c->go_in);
    pv_forget_password(c);
    pv_forget(c);
    return c;
}

void tiku_pdfview_open(tiku_pdfview_t *c)
{
    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, "TikuPDF",
            (tiku_rect_t){70, 40, 820, 600}, pv_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, pv_event, NULL, pv_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
        pv_say(c, "Open a PDF: a datasheet, a manual or a paper.");
    }
    tiku_workspace_activate(c->workspace, c->window);
    pv_menus(c);
    pv_changed(c);
}

void tiku_pdfview_close(tiku_pdfview_t *c)
{
    if (c && c->window) tiku_workspace_close(c->workspace, c->window);
}

void tiku_pdfview_destroy(tiku_pdfview_t *c)
{
    if (!c) return;
    tiku_pdfview_close(c);
    c->generation++;
    c->workspace = NULL;
    c->dirty = NULL;
    c->choose = NULL;
    c->ctx = NULL;
    pv_unref(c);
}

tiku_window_t *tiku_pdfview_handle(tiku_pdfview_t *c)
{
    return c ? c->window : NULL;
}

const char *tiku_pdfview_path(tiku_pdfview_t *c)
{
    return c && c->doc ? c->path : "";
}

int tiku_pdfview_file(tiku_pdfview_t *c, const char *path)
{
    if (!c || !c->workspace) return 0;
    tiku_pdfview_open(c);
    return pv_load(c, path, "");
}
