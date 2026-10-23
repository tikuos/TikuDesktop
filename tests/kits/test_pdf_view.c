/* The PDF view driven without a desktop, as a program embedding it would
 * drive it: blank, opened on a worker, pages drawn and composed, scrolled,
 * zoomed and turned, links in the document and out of it, Back, found,
 * selected and copied, the outline, a start page asked before the first
 * draw, a file that is not a PDF, locked ones, and the view let go with
 * its jobs in flight.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include <tiku_pdf_view.c>
#include "../fixtures/pdf_crypt.inc"

#define PAGES 12

typedef struct { char *p; long n, cap, off[64]; int nobj; } pdfw_t;

/** @brief What the view told its program. */
static int changes, opens, last_err = -1;
static char address[256];

/** @brief Append @p s to the file being written. */
static void put(pdfw_t *b, const char *s)
{
    long n = (long)strlen(s);

    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, (size_t)b->cap);
        assert(b->p);
    }
    memcpy(b->p + b->n, s, (size_t)n + 1);
    b->n += n;
}

/** @brief Object @p num with @p body; a stream when @p data is given. */
static void object(pdfw_t *b, int num, const char *body, const char *data)
{
    char head[96];

    b->off[num] = b->n;
    if (num >= b->nobj) b->nobj = num + 1;
    if (data) {
        snprintf(head, sizeof head, "%d 0 obj\n<< /Length %zu >>\nstream\n",
                 num, strlen(data));
        put(b, head);
        put(b, data);
        put(b, "\nendstream\nendobj\n");
        return;
    }
    snprintf(head, sizeof head, "%d 0 obj\n", num);
    put(b, head);
    put(b, body);
    put(b, "\nendobj\n");
}

/** @brief The pages: each saying its number, the first and fifth with
 *         "TikuPDF", the first with a link to page ten and one out. */
static void pages(pdfw_t *b)
{
    char body[256], text[256];

    for (int i = 0; i < PAGES; i++) {
        snprintf(body, sizeof body, "<< /Type /Page /Parent 2 0 R /Contents "
                 "%d 0 R%s >>", 25 + i, i == 0 ? " /Annots [50 0 R 51 0 R]"
                                               : "");
        object(b, 10 + i, body, NULL);
        snprintf(text, sizeof text, "BT /F1 24 Tf 72 700 Td (Page %d) Tj ET%s",
                 i + 1, i == 0 ? " BT /F1 20 Tf 72 600 Td (Hello TikuPDF "
                                 "world) Tj ET" :
                        i == 4 ? " BT /F1 20 Tf 72 300 Td (Find TikuPDF "
                                 "here) Tj ET" : "");
        object(b, 25 + i, NULL, text);
    }
    object(b, 50, "<< /Type /Annot /Subtype /Link /Rect [72 590 400 625] "
                  "/Dest [19 0 R /XYZ 0 792 0] >>", NULL);
    object(b, 51, "<< /Type /Annot /Subtype /Link /Rect [72 100 300 130] "
                  "/A << /S /URI /URI (https://tiku-os.org/pdf) >> >>", NULL);
}

/** @brief TikuPDF's test document: twelve pages, and an outline of
 *         Start, and Ten with Eleven under it. */
static pdfw_t document(void)
{
    pdfw_t b = {0};
    char body[512];
    long xref;

    put(&b, "%PDF-1.4\n");
    object(&b, 1, "<< /Type /Catalog /Pages 2 0 R /Outlines 40 0 R >>", NULL);
    snprintf(body, sizeof body, "<< /Type /Pages /Count %d /Kids [", PAGES);
    for (int i = 0; i < PAGES; i++)
        snprintf(body + strlen(body), sizeof body - strlen(body), "%d 0 R ",
                 10 + i);
    strcat(body, "] /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R"
                 " >> >> >>");
    object(&b, 2, body, NULL);
    object(&b, 3, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
           NULL);
    pages(&b);
    object(&b, 40, "<< /Type /Outlines /First 41 0 R /Last 42 0 R >>", NULL);
    object(&b, 41, "<< /Title (Start) /Parent 40 0 R /Next 42 0 R "
                   "/Dest [10 0 R /Fit] >>", NULL);
    object(&b, 42, "<< /Title (Ten) /Parent 40 0 R /Prev 41 0 R /First 43 0 R"
                   " /Last 43 0 R /Count 1 /Dest [19 0 R /XYZ 0 792 0] >>",
           NULL);
    object(&b, 43, "<< /Title (Eleven) /Parent 42 0 R /Dest [20 0 R /Fit] >>",
           NULL);
    xref = b.n;
    snprintf(body, sizeof body, "xref\n0 %d\n0000000000 65535 f \n", b.nobj);
    put(&b, body);
    for (int i = 1; i < b.nobj; i++) {
        snprintf(body, sizeof body, b.off[i] ? "%010ld 00000 n \n"
                                             : "0000000000 65535 f \n",
                 b.off[i]);
        put(&b, body);
    }
    snprintf(body, sizeof body, "trailer\n<< /Size %d /Root 1 0 R >>\n"
             "startxref\n%ld\n%%%%EOF\n", b.nobj, xref);
    put(&b, body);
    return b;
}

/** @brief @p n bytes at @p p into the file @p path. */
static void write_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");

    assert(f && fwrite(p, 1, n, f) == n);
    fclose(f);
}

/** @brief The program's side: redraws asked for, openings, addresses. */
static void on_changed(void *ctx) { (void)ctx; changes++; }

/** @brief How an opening ended. */
static void on_opened(void *ctx, tiku_pdf_err_t err)
{
    (void)ctx;
    opens++;
    last_err = (int)err;
}

/** @brief A link out of the document. */
static void on_address(void *ctx, const char *uri)
{
    (void)ctx;
    snprintf(address, sizeof address, "%s", uri);
}

static tiku_surface_t *surf;
static const tiku_rect_t R = { 20, 30, 560, 440 };

/** @brief Draw and collect until the workers are done, as a program's
 *         loop would; a job stuck for five seconds fails the test. */
static void settle(tiku_pdf_view_t *v)
{
    for (int i = 0; i < 5000; i++) {
        (void)tiku_work_collect();
        tiku_pdf_view_draw(v, surf, R);
        if (!tiku_work_busy() && !v->loading && !v->inflight &&
            !v->searching) {
            (void)tiku_work_collect();
            tiku_pdf_view_draw(v, surf, R);
            if (!tiku_work_busy() && !v->inflight) return;
        }
        usleep(1000);
    }
    assert(!"a job never finished");
}

/** @brief The surface's colour at @p x, @p y. */
static uint32_t at(int x, int y)
{
    return surf->px[(size_t)y * surf->w + x] & 0xffffffu;
}

/** @brief The canvas point over @p ux, @p uy points on page @p p. */
static void point_on(tiku_pdf_view_t *v, int p, double ux, double uy,
                     int *x, int *y)
{
    vw_layout_t l = vw_layout(v, R);

    *x = vw_page_x(v, l.canvas, p) + (int)(ux * v->scale);
    *y = l.canvas.y + (int)(v->top[p] + uy * v->scale - v->vbar.value);
}

/** @brief An event of @p type at @p x, @p y, with @p key and @p mods. */
static int send(tiku_pdf_view_t *v, int type, int x, int y, unsigned key,
                unsigned mods)
{
    tiku_event_t ev = {.type = (tiku_event_type_t)type, .x = x, .y = y,
                       .button = 1, .key = key, .modifiers = mods};

    if (type == TIKU_EVENT_WHEEL) ev.dy = (int)key, ev.key = 0;
    return tiku_pdf_view_event(v, R, &ev);
}

/** @brief A press, taken, and its release at @p x, @p y. */
static void click(tiku_pdf_view_t *v, int x, int y)
{
    assert(send(v, TIKU_EVENT_POINTER_DOWN, x, y, 0, 0));
    (void)send(v, TIKU_EVENT_POINTER_UP, x, y, 0, 0);
}

/** @brief A key pressed, with @p mods. */
static int key(tiku_pdf_view_t *v, unsigned k, unsigned mods)
{
    return send(v, TIKU_EVENT_KEY_DOWN, 0, 0, k, mods);
}

/** @brief How many of the pixels in @p x0..x1, @p y0..y1 are dark. */
static int dark(int x0, int y0, int x1, int y1)
{
    int n = 0;

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            n += (at(x, y) & 0xff) < 0x80;
    return n;
}

/** @brief Before anything opens: blank, silent, nothing to do. */
static void blank_tests(tiku_pdf_view_t *v)
{
    tiku_pdf_view_draw(v, surf, R);
    assert(at(R.x + R.w / 2, R.y + R.h / 2) == (TIKU_C_DOC & 0xffffffu));
    assert(!tiku_pdf_view_pages(v) && !*tiku_pdf_view_status(v) &&
           !*tiku_pdf_view_path(v) && !tiku_pdf_view_can(v,
                                                  TIKU_PDF_VIEW_CAN_FIND));
    assert(!key(v, TIKU_KEY_PAGE_DOWN, 0));
    assert(!tiku_pdf_view_find(v, "x", 1, 0) && !tiku_pdf_view_copy(v));
}

/** @brief Opened on a worker, the program told; the first page drawn at
 *         the width of the view, over the ground, saying its number. */
static void open_tests(tiku_pdf_view_t *v, const char *file)
{
    int x, y, w;

    assert(tiku_pdf_view_open(v, file, NULL) && tiku_pdf_view_loading(v));
    assert(!tiku_pdf_view_open(v, file, NULL));     /* one at a time */
    assert(strstr(tiku_pdf_view_status(v), "still opening"));
    settle(v);
    assert(opens == 1 && last_err == TIKU_PDF_OK && changes > 0);
    assert(tiku_pdf_view_pages(v) == PAGES && !tiku_pdf_view_loading(v));
    assert(!strcmp(tiku_pdf_view_status(v), "12 pages, with an outline."));
    assert(!strcmp(tiku_pdf_view_path(v), file) &&
           !strcmp(tiku_pdf_view_title(v), "view.pdf"));
    assert(tiku_pdf_view_fitted(v) == TIKU_PDF_VIEW_FIT_WIDTH &&
           fabs(tiku_pdf_view_scale(v) - (R.w - VW_BAR - 24) / 612.0) < 1e-9);
    point_on(v, 0, 0, 0, &x, &y);
    w = vw_page_w(v, 0);
    assert(x == R.x + VW_MARGIN && y == R.y + VW_MARGIN);
    assert(at(R.x + 4, R.y + 4) == (VW_GROUND & 0xffffffu));
    assert(at(x + 6, y + 6) == 0xffffffu && at(x + w - 6, y + 6) == 0xffffff);
    assert(dark(x + (int)(72 * v->scale), y + (int)(70 * v->scale),
                x + (int)(160 * v->scale), y + (int)(95 * v->scale)) > 20);
    assert(tiku_pdf_view_page(v) == 0 && tiku_pdf_view_can(v,
           TIKU_PDF_VIEW_CAN_NEXT) && !tiku_pdf_view_can(v,
           TIKU_PDF_VIEW_CAN_PREV));
}

/** @brief The wheel, the page keys, Home and End, and the bars. */
static void scroll_tests(tiku_pdf_view_t *v)
{
    int top = v->vbar.value;

    assert(send(v, TIKU_EVENT_WHEEL, R.x + 50, R.y + 50, 3, 0));
    assert(v->vbar.value == top + 3 * VW_LINE);
    assert(!send(v, TIKU_EVENT_WHEEL, R.x - 5, R.y + 50, 3, 0)); /* outside */
    assert(key(v, TIKU_KEY_END, 0) && tiku_pdf_view_page(v) == PAGES - 1);
    assert(key(v, TIKU_KEY_HOME, 0) && tiku_pdf_view_page(v) == 0);
    assert(key(v, TIKU_KEY_PAGE_DOWN, 0) && v->vbar.value > 0);
    assert(key(v, TIKU_KEY_RIGHT, 0) && tiku_pdf_view_page(v) == 1);
    tiku_pdf_view_go(v, 6);
    assert(tiku_pdf_view_page(v) == 6 && tiku_pdf_view_back(v) &&
           tiku_pdf_view_page(v) == 1 && !tiku_pdf_view_back(v));
    /* The bar's lower arrow, pressed: a line down. */
    top = v->vbar.value;
    click(v, R.x + R.w - 7, R.y + R.h - 7);
    assert(v->vbar.value == top + VW_LINE);
    key(v, TIKU_KEY_HOME, 0);
    settle(v);
}

/** @brief Fixed sizes and the zoom's steps, Fit Page, and the turn. */
static void zoom_tests(tiku_pdf_view_t *v)
{
    tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIXED, 1.0);
    settle(v);
    assert(tiku_pdf_view_scale(v) == 1.0 &&
           tiku_pdf_view_fitted(v) == TIKU_PDF_VIEW_FIXED);
    tiku_pdf_view_zoom(v, 1);
    assert(fabs(tiku_pdf_view_scale(v) - 1.1) < 1e-9);
    assert(key(v, '-', TIKU_MOD_CMD) && tiku_pdf_view_scale(v) == 1.0);
    tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIXED, 9.0);     /* held at 4 */
    assert(tiku_pdf_view_scale(v) == 4.0 &&
           !tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_ZOOM_IN));
    tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIT_PAGE, 0);
    settle(v);
    assert(fabs(tiku_pdf_view_scale(v) - fmin((R.w - VW_BAR - 24) / 612.0,
                                              (R.h - 24) / 792.0)) < 1e-9);
    tiku_pdf_view_turn(v, 90);
    settle(v);
    assert(tiku_pdf_view_turned(v) == 90 &&
           vw_page_w(v, 0) == (int)(792 * v->scale + 0.5));
    tiku_pdf_view_turn(v, -90);
    tiku_pdf_view_turn(v, 45);                          /* not a quarter */
    assert(tiku_pdf_view_turned(v) == 0);
    tiku_pdf_view_fit(v, TIKU_PDF_VIEW_FIT_WIDTH, 0);
    settle(v);
}

/** @brief A link inside the document goes and Back returns; one out of it
 *         goes to the program, or with none taking it, to the clipboard. */
static void link_tests(tiku_pdf_view_t *v)
{
    int x, y;

    point_on(v, 0, 100, 185, &x, &y);
    send(v, TIKU_EVENT_POINTER_MOVE, x, y, 0, 0);
    assert(!strcmp(tiku_pdf_view_status(v), "Go to page 10"));
    click(v, x, y);
    assert(tiku_pdf_view_page(v) == 9 && !strcmp(v->status, "Page 10."));
    assert(key(v, '[', TIKU_MOD_CMD) && tiku_pdf_view_page(v) == 0);
    send(v, TIKU_EVENT_WHEEL, R.x + 50, R.y + 50, 8, 0);  /* down to it */
    point_on(v, 0, 100, 677, &x, &y);
    click(v, x, y);
    assert(!strcmp(address, "https://tiku-os.org/pdf"));
    v->host.address = NULL;
    click(v, x, y);
    assert(!strcmp(tiku_clipboard_text(), "https://tiku-os.org/pdf") &&
           strstr(tiku_pdf_view_status(v), "Address copied"));
    v->host.address = on_address;
}

/** @brief Found on workers, stepped through; case and whole words. */
static void find_tests(tiku_pdf_view_t *v)
{
    assert(tiku_pdf_view_find(v, "tikupdf", 1, 0));
    settle(v);
    assert(v->nmatch == 2 && !strcmp(tiku_pdf_view_status(v),
                                     "Match 1 of 2."));
    assert(tiku_pdf_view_find(v, "tikupdf", 1, 0));
    assert(v->cur == 1 && v->match[1].page == 4);
    settle(v);
    assert(tiku_pdf_view_page(v) == 4 || tiku_pdf_view_page(v) == 5);
    assert(tiku_pdf_view_find(v, "tikupdf", 1, TIKU_PDF_FIND_CASE));
    settle(v);
    assert(!v->nmatch && !strcmp(tiku_pdf_view_status(v),
                                 "Not found in this document."));
    assert(tiku_pdf_view_find(v, "Tiku", 1, TIKU_PDF_FIND_WORDS));
    settle(v);
    assert(!v->nmatch);
    assert(!tiku_pdf_view_find(v, "   ", 1, 0));
    key(v, TIKU_KEY_HOME, 0);
    settle(v);
}

/** @brief A drag selects, Copy copies, Escape lets go; the page's text
 *         selected whole; Command-A and Command-C do the same. */
static void select_tests(tiku_pdf_view_t *v)
{
    int x0, y0, x1, y1;

    point_on(v, 0, 60, 185, &x0, &y0);
    point_on(v, 0, 330, 185, &x1, &y1);
    assert(send(v, TIKU_EVENT_POINTER_DOWN, x0, y0, 0, 0));
    assert(send(v, TIKU_EVENT_POINTER_MOVE, x1, y1, 0, 0));
    assert(send(v, TIKU_EVENT_POINTER_UP, x1, y1, 0, 0));
    assert(tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_COPY));
    assert(tiku_pdf_view_copy(v) &&
           !strcmp(tiku_clipboard_text(), "Hello TikuPDF world"));
    assert(key(v, TIKU_KEY_ESCAPE, 0) &&
           !tiku_pdf_view_can(v, TIKU_PDF_VIEW_CAN_COPY));
    assert(key(v, 'a', TIKU_MOD_CMD) && key(v, 'c', TIKU_MOD_CMD));
    assert(!strncmp(tiku_clipboard_text(), "Page 1\nHello TikuPDF world",
                    26));
    click(v, x0, y0 + 200);                     /* a click lets it go */
    assert(!v->selected);
}

/** @brief The outline as read, followed; a start page asked before the
 *         view was first drawn. */
static void outline_tests(tiku_pdf_view_t *v, const char *file)
{
    const tiku_pdf_outline_t *items;
    tiku_pdf_view_t *w;

    assert(tiku_pdf_view_outline(v, &items) == 3);
    assert(!strcmp(items[0].title, "Start") && !strcmp(items[1].title, "Ten")
           && items[2].level == 1);
    tiku_pdf_view_follow(v, &items[1].to);
    assert(tiku_pdf_view_page(v) == 9 && tiku_pdf_view_back(v));
    w = tiku_pdf_view_new(NULL);
    assert(w);
    tiku_pdf_view_go(w, 5);
    assert(tiku_pdf_view_open(w, file, ""));
    settle(w);
    assert(tiku_pdf_view_page(w) == 5);
    tiku_pdf_view_free(w);
}

/** @brief A file that is not a PDF, said; the one shown stays.  A locked
 *         one wants its password; a no-copy one keeps its text in. */
static void refusal_tests(tiku_pdf_view_t *v, const char *dir)
{
    static const enc_t locked = {"locked", 5, 6, 256, 1, "secret", "boss", -4};
    static const enc_t nocopy = {"nocopy", 2, 3, 128, 0, "", "owner", -20};
    char path[256];
    encw_t b;

    snprintf(path, sizeof path, "%s/notes.pdf", dir);
    write_file(path, "just words\n", 11);
    assert(tiku_pdf_view_open(v, path, NULL));
    settle(v);
    assert(last_err == TIKU_PDF_NOT_PDF && tiku_pdf_view_pages(v) == PAGES &&
           !strcmp(tiku_pdf_view_status(v), "That file is not a PDF."));
    snprintf(path, sizeof path, "%s/locked.pdf", dir);
    b = enc_make(&locked);
    write_file(path, b.p, (size_t)b.n);
    free(b.p);
    assert(tiku_pdf_view_open(v, path, NULL));
    settle(v);
    assert(last_err == TIKU_PDF_PASSWORD && strstr(v->status, "locked"));
    assert(tiku_pdf_view_open(v, path, "wrong"));
    settle(v);
    assert(!strcmp(v->status, "That password does not open it."));
    assert(tiku_pdf_view_open(v, path, "secret"));
    settle(v);
    assert(last_err == TIKU_PDF_OK && tiku_pdf_view_title(v)[0]);
    snprintf(path, sizeof path, "%s/nocopy.pdf", dir);
    b = enc_make(&nocopy);
    write_file(path, b.p, (size_t)b.n);
    free(b.p);
    assert(tiku_pdf_view_open(v, path, NULL));
    settle(v);
    assert(tiku_pdf_view_select_page(v) && !tiku_pdf_view_copy(v) &&
           strstr(v->status, "does not allow"));
}

/** @brief Closed, blank again; let go while its jobs are in flight. */
static void close_tests(tiku_pdf_view_t *v, const char *file)
{
    tiku_pdf_view_close(v);
    tiku_pdf_view_draw(v, surf, R);
    assert(!tiku_pdf_view_pages(v) && !*tiku_pdf_view_status(v) &&
           at(R.x + R.w / 2, R.y + R.h / 2) == (TIKU_C_DOC & 0xffffffu));
    assert(tiku_pdf_view_open(v, file, NULL));
    tiku_pdf_view_close(v);                 /* the opening is dropped */
    for (int i = 0; i < 5000 && tiku_work_busy(); i++) {
        (void)tiku_work_collect();
        usleep(1000);
    }
    (void)tiku_work_collect();
    assert(!tiku_pdf_view_pages(v) && !tiku_pdf_view_loading(v));
    assert(tiku_pdf_view_open(v, file, NULL));
    settle(v);
    tiku_pdf_view_draw(v, surf, R);         /* pages asked for, then... */
    tiku_pdf_view_free(v);                  /* ...let go under them */
    for (int i = 0; i < 5000 && tiku_work_busy(); i++) {
        (void)tiku_work_collect();
        usleep(1000);
    }
    (void)tiku_work_collect();
}

/** @brief Every group, in order, on one view. */
int main(void)
{
    tiku_pdf_view_host_t host = { on_changed, on_opened, on_address, NULL };
    char dir[] = "/tmp/tikupdfview-XXXXXX", file[256], cmd[300];
    pdfw_t doc = document();
    tiku_pdf_view_t *v;

    assert(mkdtemp(dir));
    snprintf(file, sizeof file, "%s/view.pdf", dir);
    write_file(file, doc.p, (size_t)doc.n);
    free(doc.p);
    surf = tiku_surface_new(600, 500, TIKU_C_PANEL);
    v = tiku_pdf_view_new(&host);
    assert(surf && v);
    blank_tests(v);
    open_tests(v, file);
    scroll_tests(v);
    zoom_tests(v);
    link_tests(v);
    find_tests(v);
    select_tests(v);
    outline_tests(v, file);
    refusal_tests(v, dir);
    close_tests(v, file);
    tiku_surface_free(surf);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)system(cmd);
    puts("PASS: PDF view -- blank, opened, drawn, scrolled, zoomed, turned, "
         "links, Back, found, selected, copied, outline, start page, "
         "refusals, closed, let go in flight");
    return 0;
}
