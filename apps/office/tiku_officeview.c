/* tiku_officeview.c -- the viewer behind TikuDoc and TikuSlides: the
 * office kit lays a file out as a PDF on a worker, and the PDF kit's view
 * shows it; Export as PDF... writes that PDF where the Save panel says.
 * Slides fit a slide to the window, and Present gives the window to one.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_officeview.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_icons_tiku.h"
#include "tiku_list.h"
#include "tiku_measure.h"
#include "tiku_model.h"
#include "tiku_office.h"
#include "tiku_pdf_view.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OV_OPEN = 1, OV_EXPORT, OV_CLOSE, OV_COPY, OV_SELECT, OV_FIND,
       OV_FIND_NEXT, OV_FIND_PREV, OV_FIT_WIDTH, OV_FIT_PAGE, OV_ACTUAL,
       OV_ZOOM_IN, OV_ZOOM_OUT, OV_PRESENT, OV_PREV, OV_NEXT, OV_FIRST,
       OV_LAST, OV_BACK, OV_OUTLINE, OV_FIND_FIELD, OV_VIEW, OV_SIDE };

/** @brief The viewer's state; only the loop touches it. */
struct tiku_officeview {
    tiku_workspace_t      *workspace;
    tiku_window_t         *window;
    int                   *dirty;       /* the application's: draw again  */
    tiku_officeview_kind_t kind;
    tiku_officeview_host_t host;
    tiku_pdf_view_t       *view;        /* the pages                      */
    tiku_dialog_t          dialog;      /* the toolbar and find field     */
    tiku_field_t           find;
    tiku_field_input_t     find_in;
    tiku_list_t            outline;     /* the headings' or titles' rows  */
    int                    outline_on;  /* the column shown, when there is
                                           an outline to show             */
    int                    hover_tool;  /* the button pointed at          */
    int                    to_view;     /* the pages take the keys, once
                                           they can                       */
    int                    presenting;  /* slides, one to the window      */
    int                    exporting;   /* a PDF being written            */
    int                    refs;        /* the application and its jobs   */
    int                    generation;  /* bumped: jobs' answers dropped  */
    char                   path[TIKU_PATH_MAX];     /* the file shown     */
    char                   opening[TIKU_PATH_MAX];  /* the file opening   */
    char                   said[320];   /* an export's end, in words      */
};

static void ov_menus(tiku_officeview_t *c);
static void ov_action(tiku_officeview_t *c, int id);
static void ov_go(tiku_officeview_t *c, int page);
static void ov_find(tiku_officeview_t *c, int dir);

/** @brief Something changed: the window is drawn again. */
static void ov_changed(tiku_officeview_t *c)
{
    if (c->dirty) *c->dirty = 1;
}

/** @brief Drop a hold on the viewer; the last one frees it and its
 *         view. */
static void ov_unref(tiku_officeview_t *c)
{
    if (--c->refs > 0) return;
    tiku_pdf_view_free(c->view);
    free(c);
}

/** @brief Whether a document is shown. */
static int ov_open_now(const tiku_officeview_t *c)
{
    return tiku_pdf_view_pages(c->view) > 0;
}

/** @brief The document's outline: its headings, or its slides' titles;
 *         into @p items.  @return how many, none with nothing open. */
static int ov_entries(const tiku_officeview_t *c,
                      const tiku_pdf_outline_t **items)
{
    int n = ov_open_now(c) ? tiku_pdf_view_outline(c->view, items) : 0;

    return n > TIKU_LIST_MAX ? TIKU_LIST_MAX : n;
}

/* ------------------------------------------------------------ words   */

/** @brief Command @p id's name, as its menu row and narration say it. */
static const char *ov_label(const tiku_officeview_t *c, int id)
{
    int s = c->kind.slides;

    switch (id) {
    case OV_OPEN: return "Open...";
    case OV_EXPORT: return "Export as PDF...";
    case OV_CLOSE: return "Close";
    case OV_COPY: return "Copy";
    case OV_SELECT: return s ? "Select Slide Text" : "Select Page Text";
    case OV_FIND: return "Find...";
    case OV_FIND_NEXT: return "Find Next";
    case OV_FIND_PREV: return "Find Previous";
    case OV_FIT_WIDTH: return "Fit Width";
    case OV_FIT_PAGE: return s ? "Fit Slide" : "Fit Page";
    case OV_ACTUAL: return "Actual Size";
    case OV_ZOOM_IN: return "Zoom In";
    case OV_ZOOM_OUT: return "Zoom Out";
    case OV_PRESENT: return c->presenting ? "Stop Presenting" : "Present";
    case OV_PREV: return s ? "Previous Slide" : "Previous Page";
    case OV_NEXT: return s ? "Next Slide" : "Next Page";
    case OV_FIRST: return s ? "First Slide" : "First Page";
    case OV_LAST: return s ? "Last Slide" : "Last Page";
    case OV_BACK: return "Back";
    case OV_OUTLINE: return "Outline";
    }
    return "";
}

/** @brief Whether command @p id would do anything now. */
static int ov_enabled(const tiku_officeview_t *c, int id)
{
    tiku_pdf_view_t *v = c->view;
    int open = ov_open_now(c), busy = tiku_pdf_view_loading(v);

    switch (id) {
    case OV_OPEN: return !busy;
    case OV_EXPORT: return open && !busy && !c->exporting;
    case OV_CLOSE: return 1;
    case OV_COPY: return tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_COPY);
    case OV_FIND_NEXT: case OV_FIND_PREV: return open && c->find.text[0];
    case OV_ZOOM_IN: return tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_ZOOM_IN);
    case OV_ZOOM_OUT: return tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_ZOOM_OUT);
    case OV_PRESENT: return open && c->kind.slides;
    case OV_PREV: return tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_PREV);
    case OV_NEXT: return tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_NEXT);
    case OV_BACK: return tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_BACK);
    case OV_OUTLINE: return open && !c->presenting && ov_entries(c, NULL);
    }
    return open;
}

/** @brief The window's title: the document's, then the application's. */
static void ov_title(tiku_officeview_t *c)
{
    if (!c->window) return;
    if (ov_open_now(c))
        snprintf(c->window->title, sizeof c->window->title, "%s - %s",
                 tiku_pdf_view_title(c->view), c->kind.name);
    else
        snprintf(c->window->title, sizeof c->window->title, "%s",
                 c->kind.name);
}

/* ----------------------------------------------------------- export   */

/** @brief Writing the file shown as a PDF, on a worker. */
typedef struct {
    tiku_officeview_t *owner;
    int generation, err;
    char src[TIKU_PATH_MAX], dst[TIKU_PATH_MAX], why[256];
} ov_export_t;

/** @brief On a worker: the file made a PDF again, written whole. */
static void ov_export_run(void *ctx)
{
    ov_export_t *j = ctx;

    j->err = tiku_office_export(j->src, j->dst, j->why, sizeof j->why);
}

/** @brief On the loop: what became of it, said in the status line. */
static void ov_export_done(void *ctx)
{
    ov_export_t *j = ctx;
    tiku_officeview_t *c = j->owner;
    const char *name = strrchr(j->dst, '/');

    if (j->generation != c->generation) return;
    if (j->err == 0)
        snprintf(c->said, sizeof c->said, "Exported as %.255s.",
                 name ? name + 1 : j->dst);
    else
        snprintf(c->said, sizeof c->said, "%s", j->why);
}

/** @brief After done, or instead of it: free the job and drop its hold on
 *         the viewer. */
static void ov_export_free(void *ctx)
{
    ov_export_t *j = ctx;
    tiku_officeview_t *c = j->owner;

    c->exporting = 0;
    if (c->workspace) {
        ov_menus(c);
        ov_changed(c);
    }
    ov_unref(c);
    free(j);
}

/** @brief Export as PDF...: the Save panel, offering the file's own name
 *         with .pdf for its ending, beside it. */
static void ov_ask_export(tiku_officeview_t *c)
{
    char dir[TIKU_PATH_MAX], name[TIKU_PATH_MAX + 8];
    const char *slash = strrchr(c->path, '/'), *dot;

    if (!c->host.pick || !c->path[0]) return;
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - c->path) : 0,
             c->path);
    snprintf(name, sizeof name, "%s", slash ? slash + 1 : c->path);
    dot = strrchr(name, '.');
    snprintf(name + (dot ? (size_t)(dot - name) : strlen(name)),
             sizeof name - (dot ? (size_t)(dot - name) : strlen(name)),
             ".pdf");
    c->host.pick(c->host.ctx, 1, dir[0] ? dir : NULL, name);
}

/* --------------------------------------------------------- the view   */

/** @brief The view has something new to show. */
static void ov_view_changed(void *ctx)
{
    tiku_officeview_t *c = ctx;

    if (c->workspace) ov_changed(c);
}

/** @brief The view finished an opening.  On success the file is shown and
 *         titled, its outline listed, and slides fitted to the page; on
 *         failure the document before it stays, a running presentation
 *         ends, and the view's status says why. */
static void ov_view_opened(void *ctx, tiku_pdf_err_t err)
{
    tiku_officeview_t *c = ctx;

    if (!c->workspace) return;
    if (err == TIKU_PDF_OK) {
        snprintf(c->path, sizeof c->path, "%s", c->opening);
        c->said[0] = '\0';
        c->to_view = 1;
        tiku_list_init(&c->outline, 1);
        (void)tiku_list_set_count(&c->outline, ov_entries(c, NULL));
        if (c->kind.slides)
            tiku_pdf_view_fit(c->view, TIKU_PDF_VIEW_FIT_PAGE, 0);
    } else if (c->presenting) {
        c->presenting = 0;
        tiku_pdf_view_single(c->view, 0);
    }
    ov_title(c);
    ov_menus(c);
    ov_changed(c);
}

#include "tiku_officeview_view.inl"

/* ---------------------------------------------------------- commands  */

/** @brief Go to page @p page, kept within the document. */
static void ov_go(tiku_officeview_t *c, int page)
{
    int n = tiku_pdf_view_pages(c->view);

    if (n <= 0) return;
    tiku_pdf_view_go(c->view, page < 0 ? 0 : page >= n ? n - 1 : page);
}

/** @brief Present the slides, one to the window, or with @p on 0 stop. */
static void ov_present(tiku_officeview_t *c, int on)
{
    c->presenting = on && ov_open_now(c) && c->kind.slides;
    tiku_pdf_view_single(c->view, c->presenting);
    c->to_view = 1;
}

/** @brief Find: the field's text looked for, forwards (@p dir 1) or back,
 *         the field given the keys when it is empty. */
static void ov_find(tiku_officeview_t *c, int dir)
{
    if (!c->find.text[0]) {
        (void)tiku_dialog_focus(&c->dialog, OV_FIND_FIELD);
        return;
    }
    (void)tiku_pdf_view_find(c->view, c->find.text, dir, 0);
}

/** @brief Carry out command @p id, from a menu row, a toolbar button or a
 *         key. */
static void ov_action(tiku_officeview_t *c, int id)
{
    tiku_pdf_view_t *v = c->view;

    if (!ov_enabled(c, id) && id != OV_FIND) return;
    switch (id) {
    case OV_OPEN:
        if (c->host.pick) c->host.pick(c->host.ctx, 0, NULL, NULL);
        break;
    case OV_EXPORT: ov_ask_export(c); break;
    case OV_CLOSE:
        if (c->window) tiku_workspace_close(c->workspace, c->window);
        return;
    case OV_COPY: (void)tiku_pdf_view_copy(v); break;
    case OV_SELECT: (void)tiku_pdf_view_select_page(v); break;
    case OV_FIND: (void)tiku_dialog_focus(&c->dialog, OV_FIND_FIELD); break;
    case OV_FIND_NEXT: ov_find(c, 1); break;
    case OV_FIND_PREV: ov_find(c, -1); break;
    case OV_FIT_WIDTH: tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIT_WIDTH, 0); break;
    case OV_FIT_PAGE: tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIT_PAGE, 0); break;
    case OV_ACTUAL: tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIXED, 1.0); break;
    case OV_ZOOM_IN: tiku_pdf_view_zoom(v, 1); break;
    case OV_ZOOM_OUT: tiku_pdf_view_zoom(v, -1); break;
    case OV_PRESENT: ov_present(c, !c->presenting); break;
    case OV_PREV: ov_go(c, tiku_pdf_view_page(v) - 1); break;
    case OV_NEXT: ov_go(c, tiku_pdf_view_page(v) + 1); break;
    case OV_FIRST: ov_go(c, 0); break;
    case OV_LAST: ov_go(c, tiku_pdf_view_pages(v) - 1); break;
    case OV_BACK: (void)tiku_pdf_view_back(v); break;
    case OV_OUTLINE: c->outline_on = !c->outline_on; break;
    default: break;
    }
    ov_title(c);
    ov_menus(c);
    ov_changed(c);
}

/* ------------------------------------------------------------- menus  */

/** @brief A menu pick, carried out as its command. */
static void ov_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    ov_action(ctx, id);
}

/** @brief Command @p id as a row of menu @p m, with its shortcut. */
static void ov_menu_add(tiku_officeview_t *c, tiku_menu_list_t *m, int id,
                        char sc, unsigned mods, int marked)
{
    tiku_menu_entry_t *e;

    if (m->nitem >= TIKU_MENUSET_ITEMS) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", ov_label(c, id));
    e->command = id;
    e->enabled = (unsigned char)ov_enabled(c, id);
    e->sc = sc;
    e->mods = sc ? mods : 0;
    e->marked = (unsigned char)(marked != 0);
}

/** @brief Publish the File, Edit, View and Go menus, each item enabled and
 *         marked as the view stands. */
static void ov_menus(tiku_officeview_t *c)
{
    static const char *const titles[] = { "File", "Edit", "View", "Go" };
    tiku_pdf_view_fit_t fit = tiku_pdf_view_fitted(c->view);
    tiku_menuset_t m;

    if (!c->window) return;
    memset(&m, 0, sizeof m);
    m.nmenu = 4;
    for (int i = 0; i < 4; i++)
        snprintf(m.menu[i].title, sizeof m.menu[i].title, "%s", titles[i]);
    ov_menu_add(c, &m.menu[0], OV_OPEN, 'o', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[0], OV_EXPORT, 'e', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[0], OV_CLOSE, 'w', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[1], OV_COPY, 'c', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[1], OV_SELECT, 'a', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[1], OV_FIND, 'f', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[1], OV_FIND_NEXT, 'g', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[1], OV_FIND_PREV, 'g',
                TIKU_MOD_CMD | TIKU_MOD_SHIFT, 0);
    ov_menu_add(c, &m.menu[2], OV_OUTLINE, 0, 0,
                c->outline_on && ov_enabled(c, OV_OUTLINE));
    ov_menu_add(c, &m.menu[2], OV_FIT_WIDTH, 0, 0,
                fit == TIKU_PDF_VIEW_FIT_WIDTH);
    ov_menu_add(c, &m.menu[2], OV_FIT_PAGE, 0, 0,
                fit == TIKU_PDF_VIEW_FIT_PAGE);
    ov_menu_add(c, &m.menu[2], OV_ACTUAL, '0', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[2], OV_ZOOM_IN, '+', TIKU_MOD_CMD, 0);
    ov_menu_add(c, &m.menu[2], OV_ZOOM_OUT, '-', TIKU_MOD_CMD, 0);
    if (c->kind.slides)
        ov_menu_add(c, &m.menu[2], OV_PRESENT, 'p',
                    TIKU_MOD_CMD | TIKU_MOD_SHIFT, c->presenting);
    ov_menu_add(c, &m.menu[3], OV_PREV, 0, 0, 0);
    ov_menu_add(c, &m.menu[3], OV_NEXT, 0, 0, 0);
    ov_menu_add(c, &m.menu[3], OV_FIRST, 0, 0, 0);
    ov_menu_add(c, &m.menu[3], OV_LAST, 0, 0, 0);
    ov_menu_add(c, &m.menu[3], OV_BACK, '[', TIKU_MOD_CMD, 0);
    tiku_window_publish_menus(c->window, &m, ov_menu_pick, c);
}

/* ------------------------------------------------------------- API   */

tiku_officeview_t *tiku_officeview_new(tiku_workspace_t *workspace,
                                       int *dirty,
                                       const tiku_officeview_kind_t *kind,
                                       const tiku_officeview_host_t *host)
{
    tiku_officeview_t *c = calloc(1, sizeof *c);
    tiku_pdf_view_host_t vh = { ov_view_changed, ov_view_opened, NULL, c };

    if (!c) return NULL;
    c->workspace = workspace;
    c->dirty = dirty;
    c->refs = 1;
    c->kind = *kind;
    if (host) c->host = *host;
    if (!(c->view = tiku_pdf_view_new(&vh))) {
        free(c);
        return NULL;
    }
    (void)tiku_icons_tiku_register();   /* the toolbar's icons */
    c->outline_on = 1;
    tiku_list_init(&c->outline, 1);
    tiku_dialog_init(&c->dialog);
    tiku_field_start(&c->find, "");
    tiku_field_input_init(&c->find_in);
    return c;
}

void tiku_officeview_open(tiku_officeview_t *c)
{
    tiku_rect_t at = c && c->kind.slides ? (tiku_rect_t){70, 50, 800, 520}
                                         : (tiku_rect_t){90, 40, 720, 600};

    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, c->kind.name, at,
                                        ov_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, ov_event, NULL, ov_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
    }
    tiku_workspace_activate(c->workspace, c->window);
    ov_title(c);
    ov_menus(c);
    ov_changed(c);
}

void tiku_officeview_destroy(tiku_officeview_t *c)
{
    if (!c) return;
    if (c->window) tiku_workspace_close(c->workspace, c->window);
    c->generation++;
    c->workspace = NULL;
    c->dirty = NULL;
    memset(&c->host, 0, sizeof c->host);
    ov_unref(c);
}

tiku_window_t *tiku_officeview_handle(tiku_officeview_t *c)
{
    return c ? c->window : NULL;
}

int tiku_officeview_file(tiku_officeview_t *c, const char *path)
{
    if (!c || !c->workspace || !path || !*path ||
        strlen(path) >= sizeof c->opening) return 0;
    tiku_officeview_open(c);
    snprintf(c->opening, sizeof c->opening, "%s", path);
    c->said[0] = '\0';
    if (!tiku_pdf_view_open_as(c->view, path, tiku_office_pdf)) return 0;
    ov_menus(c);
    ov_changed(c);
    return 1;
}

int tiku_officeview_export_to(tiku_officeview_t *c, const char *pdf)
{
    ov_export_t *j;
    tiku_work_job_t work;
    size_t n = pdf ? strlen(pdf) : 0;

    if (!c || !c->path[0] || c->exporting || n == 0 ||
        n + 5 > sizeof j->dst || !(j = calloc(1, sizeof *j))) return 0;
    j->owner = c;
    j->generation = c->generation;
    snprintf(j->src, sizeof j->src, "%s", c->path);
    snprintf(j->dst, sizeof j->dst, "%s%s", pdf, n > 4 &&
             strcmp(pdf + n - 4, ".pdf") == 0 ? "" : ".pdf");
    memset(&work, 0, sizeof work);
    work.run = ov_export_run;
    work.done = ov_export_done;
    work.free = ov_export_free;
    work.ctx = j;
    c->refs++;
    c->exporting = 1;
    if (tiku_work_post(&work) != 0) {
        snprintf(c->said, sizeof c->said, "The workers are busy. Try again "
                 "in a moment.");
        ov_export_free(j);
        return 0;
    }
    snprintf(c->said, sizeof c->said, "Exporting...");
    ov_menus(c);
    ov_changed(c);
    return 1;
}

const char *tiku_officeview_path(tiku_officeview_t *c)
{
    return c && ov_open_now(c) ? c->path : "";
}
