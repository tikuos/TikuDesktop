/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * doc_demo.c - a page of help, served by the board that it is about.
 *
 * The same reader the desktop's Help window uses, laid out and drawn
 * here: tiku_doc turns the page into lines and pieces, tiku_doc_paint
 * writes them into the window's display list, and the desktop draws the
 * list.  A board that has something to explain explains it in its own
 * window, in its own words, with no page on the desk to keep in step.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw.h"

#include "tiku_doc.h"
#include "tiku_doc_paint.h"
#include "tiku_event.h"
#include "tiku_font.h"
#include "tiku_gfx.h"

#include <stdio.h>
#include <string.h>

TIKU_PROCESS(doc_demo_process, "doc-demo");

/* Larger than the ordinary window's: a page of words is more ops than a
 * panel of controls, and the desk takes a frame of up to four kilobytes.
 * What does not fit is not drawn, so the window is small enough that
 * what it shows fits. */
#define DOC_DEMO_LIST 3584
#define DOC_DEMO_W 320
#define DOC_DEMO_H 240

static tiku_draw_window_t win;
static unsigned char list_buf[DOC_DEMO_LIST];
static tiku_surface_t surface;
static tiku_doc_t page;
static int scroll;

/*
 * The page itself, in the firmware.  A board with a filesystem would
 * read one out of it; what this sample shows is the reader, so the words
 * are here where they can be read beside the code they describe.
 */
static const char *const text =
    "<h1>This board</h1>"
    "<p>You are reading a page the <b>board</b> laid out, drawn through "
    "the same display list its other windows use. The desktop did not "
    "send it and does not have a copy.</p>"
    "<h2 id=\"roads\">Two roads</h2>"
    "<table>"
    "<tr><th>Road</th><th>What the board does</th></tr>"
    "<tr><td>Forms</td><td>publishes rows under <code>/gui</code></td></tr>"
    "<tr><td>Drawing</td><td>writes the desktop's own commands</td></tr>"
    "</table>"
    "<h2 id=\"keys\">Reading it</h2>"
    "<ul>"
    "<li>The arrow keys and the wheel move the page</li>"
    "<li><b>Home</b> and <b>End</b> reach its ends</li>"
    "<li>A link goes to a place in this page: try "
    "<a href=\"#roads\">the two roads</a></li>"
    "</ul>"
    "<p>A picture would be an <code>img</code>, which this reader "
    "measures and leaves empty: a board sends words.</p>";

/** @brief The rectangle the page is read in, inside the window. */
static tiku_rect_t
frame(void)
{
    tiku_rect_t r;

    r.x = 8;
    r.y = 8;
    r.w = win.w - 16;
    r.h = win.h - 16;
    if (r.w < 16) { r.w = 16; }
    if (r.h < 16) { r.h = 16; }
    return r;
}

/** @brief Keep the scroll inside the page it scrolls. */
static void
clamp(void)
{
    int most = tiku_doc_height(&page) - frame().h;

    if (most < 0) { most = 0; }
    if (scroll > most) { scroll = most; }
    if (scroll < 0) { scroll = 0; }
}

static void
paint(void)
{
    tiku_rect_t r = frame();
    tiku_doc_paint_t how;

    memset(&how, 0, sizeof how);
    how.scroll = scroll;
    how.focus_link = TIKU_DOC_NONE;
    how.cut = "the rest of this page was cut";
    tiku_draw_begin(&win);
    tiku_fill(&surface, (tiku_rect_t){0, 0, win.w, win.h}, TIKU_C_PANEL);
    (void)tiku_doc_layout(&page, r.w);
    clamp();
    tiku_doc_draw(&surface, &page, r, &how);
    (void)tiku_draw_present(&win);
}

/** @brief A press: a link under it is followed, in this page. */
static void
press(const tiku_draw_event_t *e)
{
    tiku_rect_t r = frame();
    tiku_doc_hit_t h = tiku_doc_hit(&page, e->x - r.x, e->y - r.y + scroll);
    const char *href;
    int at;

    if (h.kind != TIKU_DOC_HIT_LINK) { return; }
    href = tiku_doc_link_href(&page, h.link);
    if (href[0] != '#') { return; }     /* nothing off this page to go to */
    at = tiku_doc_anchor_y(&page, href + 1);
    if (at >= 0) {
        scroll = at;
        clamp();
    }
}

/** @brief A key: the page moves, nothing is typed into it. */
static void
key(const tiku_draw_event_t *e)
{
    int step = frame().h - 24;

    if (step < 16) { step = 16; }
    switch (e->key) {
    case TIKU_KEY_UP:        scroll -= 16; break;
    case TIKU_KEY_DOWN:      scroll += 16; break;
    case TIKU_KEY_PAGE_UP:   scroll -= step; break;
    case TIKU_KEY_PAGE_DOWN: scroll += step; break;
    case TIKU_KEY_HOME:      scroll = 0; break;
    case TIKU_KEY_END:       scroll = tiku_doc_height(&page); break;
    default: return;
    }
    clamp();
}

TIKU_PROCESS_THREAD(doc_demo_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    (void)tiku_process_register("doc-demo", &doc_demo_process);
    tiku_doc_init(&page);
    (void)tiku_doc_read(&page, text);
    scroll = 0;
    (void)tiku_draw_open(&win, "About this board", DOC_DEMO_W, DOC_DEMO_H,
                         &doc_demo_process, list_buf, sizeof list_buf);
    tiku_draw_sizeable(&win);
    tiku_surface_on_window(&surface, &win);
    paint();

    while (1) {
        tiku_draw_window_t *w;
        int changed = 0;

        TIKU_PROCESS_WAIT_EVENT();
        while ((w = tiku_draw_event(ev, data)) != NULL) {
            const tiku_draw_event_t *e = &w->event;

            switch (e->kind) {
            case TIKU_DRAW_POINTER_DOWN:
                press(e);
                changed = 1;
                break;
            case TIKU_DRAW_KEY_DOWN:
                key(e);
                changed = 1;
                break;
            case TIKU_DRAW_WHEEL:
                scroll += e->dy * 16;
                clamp();
                changed = 1;
                break;
            case TIKU_DRAW_RESIZE:
                /* The width changed, so the page is laid out again:
                 * paint() asks for the layout it draws. */
                changed = 1;
                break;
            case TIKU_DRAW_SESSION:
                /* A desktop arrived and the window was announced again,
                 * so the page starts where it started at boot. */
                scroll = 0;
                changed = 1;
                break;
            default:
                break;
            }
        }
        if (changed) {
            paint();
        }
    }

    TIKU_PROCESS_END();
}
