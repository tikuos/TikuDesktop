/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * pdfpane.c - a PDF in a window of the program's own.
 *
 * The PDF kit's ready-made view (tiku_pdf_view.h) given a window: the
 * program lays out the view and a status line under it, hands the view
 * its events, and publishes menus that call the view's commands.  Pages
 * are read and drawn on workers; tiku_work_collect() in tick() is where
 * they land.  A program that wants the pages themselves -- to print, to
 * thumbnail, to search its own way -- uses tiku_pdf.h instead.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_measure.h"
#include "tiku_pdf_view.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_window_app.h"
#include "tiku_work.h"

enum { CMD_OPEN = 1, CMD_CLOSE, CMD_COPY, CMD_SELECT, CMD_FIT_WIDTH,
       CMD_FIT_PAGE, CMD_ACTUAL, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ROTATE,
       CMD_PREV, CMD_NEXT, CMD_BACK };

typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;        /* the window, as the desk shows it */
    tiku_window_t *window;
    tiku_pdf_view_t *view;
    int dirty;
} pane_t;

static void pane_menus(pane_t *p);
static void pane_pick_menu(tiku_window_t *w, int id, void *ctx);

/* ------------------------------------------------------------ layout  */

/** @brief The status line's height: a row of the plain face. */
static int pane_line_h(void)
{
    return tiku_font_plain()->height + 8;
}

/** @brief Where the view goes in the window's content @p r: all of it
 *         but the status line.  Drawing and events must agree on it. */
static tiku_rect_t pane_view_rect(tiku_rect_t r)
{
    return (tiku_rect_t){r.x, r.y, r.w, r.h - pane_line_h()};
}

/** @brief The window: the view, and under it what the view says and
 *         where it stands -- nothing at all while no document is open. */
static void pane_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                      void *ctx)
{
    pane_t *p = ctx;
    tiku_rect_t line = {r.x + 8, r.y + r.h - pane_line_h() + 3, r.w - 16,
                        pane_line_h() - 6};
    char where[64] = "";
    int ww = 0;

    (void)w;
    tiku_ui_panel(s, r);
    tiku_pdf_view_draw(p->view, s, pane_view_rect(r));
    if (tiku_pdf_view_pages(p->view)) {
        snprintf(where, sizeof where, "Page %d of %d   %d%%",
                 tiku_pdf_view_page(p->view) + 1,
                 tiku_pdf_view_pages(p->view),
                 (int)(tiku_pdf_view_scale(p->view) * 100 + 0.5));
        ww = tiku_measure_width(TIKU_MEASURE_PLAIN, where) + 8;
        tiku_text_line_draw(s, (tiku_rect_t){line.x + line.w - ww, line.y,
                            ww, line.h}, TIKU_MEASURE_PLAIN, where,
                            TIKU_C_TEXT);
    }
    tiku_text_line_draw(s, (tiku_rect_t){line.x, line.y, line.w - ww - 8,
                        line.h}, TIKU_MEASURE_PLAIN,
                        tiku_pdf_view_status(p->view),
                        tiku_ink(TIKU_INK_FAINT));
}

/* ----------------------------------------------------------- commands */

/** @brief Ask the shell's panel for a PDF; the answer comes to picked(). */
static void pane_ask(pane_t *p)
{
    if (p->services.pick && p->window)
        p->services.pick(p->services.ctx, tiku_window_app_id(p->host,
                         p->window), TIKU_APP_PICK_LOCAL_OPEN,
                         getenv("HOME"), NULL);
}

/** @brief Every menu row and shortcut ends here: most are one call into
 *         the view. */
static void pane_command(pane_t *p, int id)
{
    tiku_pdf_view_t *v = p->view;
    int page = tiku_pdf_view_page(v);

    switch (id) {
    case CMD_OPEN: pane_ask(p); break;
    case CMD_CLOSE:
        if (p->window) tiku_workspace_close(
            tiku_window_app_workspace(p->host), p->window);
        return;
    case CMD_COPY: (void)tiku_pdf_view_copy(v); break;
    case CMD_SELECT: (void)tiku_pdf_view_select_page(v); break;
    case CMD_FIT_WIDTH: tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIT_WIDTH, 0); break;
    case CMD_FIT_PAGE: tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIT_PAGE, 0); break;
    case CMD_ACTUAL: tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIXED, 1.0); break;
    case CMD_ZOOM_IN: tiku_pdf_view_zoom(v, 1); break;
    case CMD_ZOOM_OUT: tiku_pdf_view_zoom(v, -1); break;
    case CMD_ROTATE: tiku_pdf_view_turn(v, 90); break;
    case CMD_PREV: tiku_pdf_view_go(v, page - 1); break;
    case CMD_NEXT: tiku_pdf_view_go(v, page + 1); break;
    case CMD_BACK: (void)tiku_pdf_view_back(v); break;
    default: break;
    }
    pane_menus(p);
    p->dirty = 1;
}

/** @brief Command @p id as a row of menu @p m, enabled when the view
 *         says it would do something. */
static void pane_row(tiku_menu_list_t *m, int id, const char *label,
                     int enabled, char sc, int marked)
{
    tiku_menu_entry_t *e = &m->item[m->nitem++];

    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", label);
    e->command = id;
    e->enabled = (unsigned char)(enabled != 0);
    e->sc = sc;
    e->mods = sc ? TIKU_MOD_CMD : 0;
    e->marked = (unsigned char)(marked != 0);
}

/** @brief File, Edit, View and Go, as the view stands now. */
static void pane_menus(pane_t *p)
{
    tiku_pdf_view_t *v = p->view;
    int open = tiku_pdf_view_pages(v) > 0;
    tiku_pdf_view_fit_t fit = tiku_pdf_view_fitted(v);
    tiku_menuset_t m;

    if (!p->window) return;
    memset(&m, 0, sizeof m);
    m.nmenu = 4;
    snprintf(m.menu[0].title, sizeof m.menu[0].title, "File");
    snprintf(m.menu[1].title, sizeof m.menu[1].title, "Edit");
    snprintf(m.menu[2].title, sizeof m.menu[2].title, "View");
    snprintf(m.menu[3].title, sizeof m.menu[3].title, "Go");
    pane_row(&m.menu[0], CMD_OPEN, "Open...", !tiku_pdf_view_loading(v),
             'o', 0);
    pane_row(&m.menu[0], CMD_CLOSE, "Close", 1, 'w', 0);
    pane_row(&m.menu[1], CMD_COPY, "Copy",
             tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_COPY), 'c', 0);
    pane_row(&m.menu[1], CMD_SELECT, "Select Page Text", open, 'a', 0);
    pane_row(&m.menu[2], CMD_FIT_WIDTH, "Fit Width", open, 0,
             fit == TIKU_PDF_VIEW_FIT_WIDTH);
    pane_row(&m.menu[2], CMD_FIT_PAGE, "Fit Page", open, 0,
             fit == TIKU_PDF_VIEW_FIT_PAGE);
    pane_row(&m.menu[2], CMD_ACTUAL, "Actual Size", open, '0', 0);
    pane_row(&m.menu[2], CMD_ZOOM_IN, "Zoom In",
             tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_ZOOM_IN), '+', 0);
    pane_row(&m.menu[2], CMD_ZOOM_OUT, "Zoom Out",
             tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_ZOOM_OUT), '-', 0);
    pane_row(&m.menu[2], CMD_ROTATE, "Rotate", open, 'r', 0);
    pane_row(&m.menu[3], CMD_PREV, "Previous Page",
             tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_PREV), 0, 0);
    pane_row(&m.menu[3], CMD_NEXT, "Next Page",
             tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_NEXT), 0, 0);
    pane_row(&m.menu[3], CMD_BACK, "Back",
             tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_BACK), '[', 0);
    tiku_window_publish_menus(p->window, &m, pane_pick_menu, p);
}

/* ---------------------------------------------------- the view's host */

/** @brief The view has something new to show. */
static void pane_changed(void *ctx)
{
    ((pane_t *)ctx)->dirty = 1;
}

/** @brief An opening has ended: the window takes the document's title.
 *         A locked document says so in the status line; a program with a
 *         password field would call tiku_pdf_view_open() again with it. */
static void pane_opened(void *ctx, tiku_pdf_err_t err)
{
    pane_t *p = ctx;

    if (p->window && err == TIKU_PDF_OK)
        snprintf(p->window->title, sizeof p->window->title, "PDF Pane - %s",
                 tiku_pdf_view_title(p->view));
    pane_menus(p);
    p->dirty = 1;
}

/* ------------------------------------------------------------- window */

/** @brief Input for the window: the view first, then the keys the menus
 *         carry, for a desk that sends them as keys. */
static int pane_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    pane_t *p = ctx;
    int taken = tiku_pdf_view_event(p->view,
                                    pane_view_rect(tiku_window_content(w)), ev);

    if (!taken && ev->type == TIKU_EVENT_KEY_DOWN &&
        (ev->modifiers & TIKU_MOD_CMD)) {
        if (ev->key == 'o') pane_command(p, CMD_OPEN);
        else if (ev->key == 'w') pane_command(p, CMD_CLOSE);
        else if (ev->key == 'r') pane_command(p, CMD_ROTATE);
        else return 0;
        return 1;
    }
    if (taken) pane_menus(p);   /* what can be done may have changed */
    return taken;
}

/** @brief The window has gone: the program ends with it. */
static void pane_gone(tiku_window_t *w, void *ctx)
{
    pane_t *p = ctx;

    (void)w;
    p->window = NULL;
}

/** @brief A pick from the published menus. */
static void pane_pick_menu(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    pane_command(ctx, id);
}

/* ------------------------------------------------------ the lifecycle */

/** @brief The window, blank, and the view in it; opened on the document
 *         the program was started with, if any. */
static int pane_start(void **state, const tiku_app_services_t *services)
{
    pane_t *p = calloc(1, sizeof *p);
    tiku_pdf_view_host_t host = { pane_changed, pane_opened, NULL, p };

    if (!p) return -1;
    p->services = *services;
    p->host = tiku_window_app_new(services);
    p->view = tiku_pdf_view_new(&host);
    if (p->host) p->window = tiku_workspace_open(
        tiku_window_app_workspace(p->host), "PDF Pane",
        (tiku_rect_t){80, 60, 700, 540}, pane_draw, p, p);
    if (!p->window || !p->view) {
        tiku_pdf_view_free(p->view);
        tiku_window_app_free(p->host);
        free(p);
        return -1;
    }
    tiku_window_set_handlers(p->window, pane_event, NULL, pane_gone);
    tiku_window_set_size_controls(p->window, 1, 1);
    pane_menus(p);
    if (services->startup_document)
        (void)tiku_pdf_view_open(p->view, services->startup_document, NULL);
    *state = p;
    return tiku_window_app_sync(p->host, 1) ? 0 : -1;
}

/** @brief Let the view go; its workers finish on their own. */
static void pane_stop(void *state)
{
    pane_t *p = state;

    tiku_pdf_view_free(p->view);
    (void)tiku_work_collect();
    tiku_window_app_free(p->host);
    free(p);
}

/** @brief Each turn: the view's finished jobs land, and the window is
 *         published when something changed. */
static void pane_tick(void *state, int64_t now)
{
    pane_t *p = state;

    (void)now;
    (void)tiku_work_collect();
    tiku_window_app_sync(p->host, p->dirty);
    p->dirty = 0;
}

/** @brief Input for the window; the program ends when it has gone. */
static int pane_event_in(void *state, uint32_t id, const tiku_event_t *ev)
{
    pane_t *p = state;

    tiku_window_app_event(p->host, id, ev);
    p->dirty = 1;
    return !p->window;
}

/** @brief A menu pick; the program ends when the window has gone. */
static int pane_pick(void *state, uint32_t id, int command)
{
    pane_t *p = state;

    tiku_window_app_pick(p->host, id, command);
    p->dirty = 1;
    return !p->window;
}

/** @brief The file the panel came back with. */
static void pane_picked(void *state, uint32_t id, const char *path)
{
    pane_t *p = state;

    (void)id;
    if (path) (void)tiku_pdf_view_open(p->view, path, NULL);
    p->dirty = 1;
}

/** @brief The desktop closed the window. */
static void pane_closed(void *state, uint32_t id)
{
    tiku_window_app_closed(((pane_t *)state)->host, id);
}

const tiku_app_descriptor_t tiku_example_pdfpane = {
    .id = "org.tikuos.example.pdfpane",
    .name = "PDF Pane",
    .start = pane_start,
    .stop = pane_stop,
    .tick = pane_tick,
    .event_in = pane_event_in,
    .pick = pane_pick,
    .picked = pane_picked,
    .closed = pane_closed
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_example_pdfpane
};
#endif

#ifndef TIKU_EXAMPLE_EMBED
/** @brief Standalone: the example, connected to the running desk. */
int
main(void)
{
    return tiku_client_run(&tiku_example_pdfpane);
}
#endif
