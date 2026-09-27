/* TikuPDF driven without a desktop: opening, drawing pages on workers,
 * zoom and paging, the outline, links and Back, search, selection and
 * copy, the page field, a file that is not a PDF, and closing.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include "../../apps/pdf/tiku_pdfview.c"
#include "../fixtures/pdf_crypt.inc"

#define PAGES 12

typedef struct { char *p; long n, cap, off[64]; int nobj; } pdfw_t;

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
    char head[64];
    b->off[num] = b->n;
    if (num >= b->nobj) b->nobj = num + 1;
    if (data) {
        char dict[256];
        snprintf(dict, sizeof dict, "%d 0 obj\n<< /Length %zu >>\nstream\n",
                 num, strlen(data));
        put(b, dict);
        put(b, data);
        put(b, "\nendstream\nendobj\n");
        return;
    }
    snprintf(head, sizeof head, "%d 0 obj\n", num);
    put(b, head);
    put(b, body);
    put(b, "\nendobj\n");
}

/**
 * @brief Twelve pages, each saying its number; the first with a link to
 *        page ten and one to an address, the first and fifth with
 *        "TikuPDF"; an outline of Start, and Ten with Eleven under it.
 */
static pdfw_t document(void)
{
    pdfw_t b = {0};
    char body[512], text[256];
    long xref;

    put(&b, "%PDF-1.4\n");
    object(&b, 1, "<< /Type /Catalog /Pages 2 0 R /Outlines 40 0 R >>", NULL);
    snprintf(body, sizeof body, "<< /Type /Pages /Count %d /Kids [", PAGES);
    for (int i = 0; i < PAGES; i++) {
        char kid[16];
        snprintf(kid, sizeof kid, "%d 0 R ", 10 + i);
        strcat(body, kid);
    }
    strcat(body, "] /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R"
                 " >> >> >>");
    object(&b, 2, body, NULL);
    object(&b, 3, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
           NULL);
    for (int i = 0; i < PAGES; i++) {
        snprintf(body, sizeof body, "<< /Type /Page /Parent 2 0 R /Contents "
                 "%d 0 R%s >>", 25 + i, i == 0 ? " /Annots [50 0 R 51 0 R]"
                                               : "");
        object(&b, 10 + i, body, NULL);
        snprintf(text, sizeof text, "BT /F1 24 Tf 72 700 Td (Page %d) Tj ET%s",
                 i + 1, i == 0 ? " BT /F1 20 Tf 72 600 Td (Hello TikuPDF "
                                 "world) Tj ET" :
                        i == 4 ? " BT /F1 20 Tf 72 300 Td (Find TikuPDF "
                                 "here) Tj ET" : "");
        object(&b, 25 + i, NULL, text);
    }
    object(&b, 40, "<< /Type /Outlines /First 41 0 R /Last 42 0 R >>", NULL);
    object(&b, 41, "<< /Title (Start) /Parent 40 0 R /Next 42 0 R "
                   "/Dest [10 0 R /Fit] >>", NULL);
    object(&b, 42, "<< /Title (Ten) /Parent 40 0 R /Prev 41 0 R /First 43 0 R"
                   " /Last 43 0 R /Count 1 /Dest [19 0 R /XYZ 0 792 0] >>",
           NULL);
    object(&b, 43, "<< /Title (Eleven) /Parent 42 0 R /Dest [20 0 R /Fit] >>",
           NULL);
    object(&b, 50, "<< /Type /Annot /Subtype /Link /Rect [72 590 400 625] "
                   "/Dest [19 0 R /XYZ 0 792 0] >>", NULL);
    object(&b, 51, "<< /Type /Annot /Subtype /Link /Rect [72 100 300 130] "
                   "/A << /S /URI /URI (https://tiku-os.org/pdf) >> >>", NULL);
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

static tiku_workspace_t *desk;

/** @brief Draw and collect until the workers are done, as the app's loop
 *         would; a job stuck for five seconds fails the test. */
static void settle(tiku_pdfview_t *c)
{
    for (int i = 0; i < 5000; i++) {
        (void)tiku_work_collect();
        if (desk) tiku_workspace_draw(desk);
        if (!tiku_work_busy() && (!c || (!c->loading && !c->inflight &&
                                         !c->searching))) {
            if (desk) tiku_workspace_draw(desk);
            if (!tiku_work_busy()) return;
        }
        usleep(1000);
    }
    assert(!"a job never finished");
}

/** @brief Count the file-chooser requests. */
static void chosen(void *ctx)
{
    (*(int *)ctx)++;
}

/** @brief The content rect's layout, as input and drawing see it. */
static pv_layout_t layout(tiku_pdfview_t *c)
{
    return pv_layout(c, tiku_window_content(c->window));
}

/** @brief A press and a release at @p x, @p y. */
static void click(tiku_pdfview_t *c, int x, int y)
{
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = x, .y = y};
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_UP;
    pv_event(c->window, &ev, c);
}

/** @brief The canvas point over @p ux, @p uy points on page @p p. */
static void point_on(tiku_pdfview_t *c, int p, double ux, double uy,
                     int *x, int *y)
{
    pv_layout_t l = layout(c);
    *x = pv_page_x(c, l.canvas, p) + (int)(ux * c->scale);
    *y = l.canvas.y + (int)(c->top[p] + uy * c->scale - c->vbar.value);
}

/** @brief Opening, drawing, the page as composed, and the outline. */
static void open_tests(tiku_pdfview_t *c, const char *file)
{
    pv_layout_t l;
    pv_tile_t *t;

    assert(tiku_pdfview_file(c, file));
    assert(!tiku_pdfview_file(c, file));        /* one opening at a time */
    settle(c);
    assert(c->doc && c->npage == PAGES && c->noutline == 3);
    assert(!strcmp(tiku_pdfview_path(c), file));
    assert(strstr(c->window->title, "tiku.pdf"));
    assert(c->fit == PV_BY_WIDTH && c->side && c->nrows == 3);
    l = layout(c);
    assert(fabs(c->scale - (l.canvas.w - 2.0 * PV_MARGIN) / 612) < 1e-9);
    t = pv_tile(c, 0, 1);
    assert(t && t->w == pv_page_w(c, 0) && t->h == pv_page_h(c, 0));
    assert(c->pages[0].text && c->pages[0].nlink == 2 &&
           c->pages[0].nch > 20);
    /* Paper where the page is, the desk's grey between pages. */
    int x = pv_page_x(c, l.canvas, 0) - l.canvas.x + 5;
    int y = (int)(c->top[0] - c->vbar.value) + 5;
    assert(c->view[(size_t)y * c->view_w + x] == 0xffffffffu);
    assert(c->view[(size_t)2 * c->view_w + 2] == 0xff4b4e52u);
    assert(pv_current(c) == 0);
}

/** @brief Zoom steps and fits keep the page in view; paging moves. */
static void view_tests(tiku_pdfview_t *c)
{
    double fitted = c->scale;
    pv_layout_t l;

    pv_action(c, PV_ZOOM_IN);
    l = layout(c);
    assert(c->fit == PV_FIXED && c->scale > fitted && l.hbar.w > 0);
    pv_action(c, PV_ACTUAL);
    l = layout(c);
    assert(c->scale == 1.0);
    pv_action(c, PV_FIT_PAGE);
    l = layout(c);
    assert(pv_page_h(c, 0) <= l.canvas.h - 2 * PV_MARGIN + 1);
    pv_action(c, PV_FIT_WIDTH);
    l = layout(c);
    assert(fabs(c->scale - fitted) < 1e-9 && !l.hbar.w);
    pv_action(c, PV_NEXT);
    assert(pv_current(c) == 1);
    pv_action(c, PV_LAST);
    assert(pv_current(c) == PAGES - 1 && !pv_enabled(c, PV_NEXT));
    pv_action(c, PV_FIRST);
    assert(pv_current(c) == 0 && !pv_enabled(c, PV_PREV));
    settle(c);
}

/** @brief The outline and links go where they say; Back returns. */
static void navigation_tests(tiku_pdfview_t *c)
{
    pv_layout_t l = layout(c);
    tiku_rect_t row = tiku_list_row_rect(&c->list, l.side, 1);
    int x, y;

    click(c, row.x + 60, row.y + row.h / 2);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 9 && c->nback == 1);
    click(c, row.x + 4, row.y + row.h / 2);     /* the sign folds Ten */
    assert(c->folded[1] && c->nrows == 2);
    pv_action(c, PV_BACK);
    assert(pv_current(c) == 0 && c->nback == 0);
    settle(c);

    /* The link over "Hello TikuPDF world": hovered, then followed. */
    point_on(c, 0, 100, 792 - 610, &x, &y);
    tiku_event_t move = {.type = TIKU_EVENT_POINTER_MOVE, .x = x, .y = y};
    pv_event(c->window, &move, c);
    assert(c->link_at == 0 && strstr(c->hint, "page 10"));
    click(c, x, y);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 9 && c->nback == 1);
    pv_action(c, PV_BACK);
    settle(c);
    /* The address link is low on the page: scroll it into view first. */
    tiku_scroll_to(&c->vbar, (int)(c->top[0] + (792 - 200) * c->scale));
    point_on(c, 0, 100, 792 - 115, &x, &y);
    assert(pv_in(layout(c).canvas, x, y));
    click(c, x, y);
    assert(!strcmp(tiku_clipboard_text(), "https://tiku-os.org/pdf"));
    assert(strstr(c->status, "Address copied"));
}

/** @brief Find steps through the matches in order and says what it
 *         found; a word not there is said to be not found; Match Case
 *         and Whole Words narrow it and search again. */
static void search_tests(tiku_pdfview_t *c)
{
    tiku_field_start(&c->find, "tikupdf");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 2 && c->match[0].page == 0 && c->match[1].page == 4);
    assert(c->cur == 0 && strstr(c->status, "Match 1 of 2"));
    assert(c->match[0].nr == 1 && c->match[0].r[0][2] > c->match[0].r[0][0]);
    pv_action(c, PV_FIND_NEXT);
    assert(c->cur == 1 && pv_current(c) == 4);
    pv_action(c, PV_FIND_NEXT);
    assert(c->cur == 0);
    tiku_field_start(&c->find, "Hello   TikuPDF");     /* spaces fold */
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 1);
    tiku_field_start(&c->find, "no such words");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 0 && strstr(c->status, "Not found"));
    tiku_field_start(&c->find, "tikupdf");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 2);
    pv_action(c, PV_MATCH_CASE);        /* the search starts again, cased */
    settle(c);
    assert(c->match_case && c->nmatch == 0);
    tiku_field_start(&c->find, "TikuPDF");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 2);
    pv_action(c, PV_MATCH_CASE);
    settle(c);
    tiku_field_start(&c->find, "Page 1");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch > 1);              /* Page 1, Page 10, Page 11 ... */
    pv_action(c, PV_WHOLE_WORDS);
    settle(c);
    assert(c->whole_words && c->nmatch == 1 && c->match[0].page == 0);
    pv_action(c, PV_WHOLE_WORDS);
    settle(c);
    pv_action(c, PV_FIRST);
    settle(c);
}

/** @brief A drag selects the characters it crosses; Copy takes them;
 *         Select Page Text takes the page. */
static void selection_tests(tiku_pdfview_t *c)
{
    const pv_page_t *pg = &c->pages[0];
    long h = -1;
    int x0, y0, x1, y1;

    assert(!pv_enabled(c, PV_COPY));
    for (long i = 0; i < pg->nch; i++)
        if (pg->ch[i].cp == 'H') { h = i; break; }
    assert(h >= 0 && pg->ch[h + 4].cp == 'o');
    point_on(c, 0, pg->ch[h].x0 + 1, (pg->ch[h].y0 + pg->ch[h].y1) / 2,
             &x0, &y0);
    point_on(c, 0, pg->ch[h + 4].x1 - 1, (pg->ch[h].y0 + pg->ch[h].y1) / 2,
             &x1, &y1);
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = x0, .y = y0};
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_MOVE; ev.x = x1; ev.y = y1;
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_UP;
    pv_event(c->window, &ev, c);
    assert(c->selected && pv_enabled(c, PV_COPY));
    pv_action(c, PV_COPY);
    assert(!strcmp(tiku_clipboard_text(), "Hello"));
    click(c, x1, y1 + 200);                     /* a click lets it go */
    assert(!c->selected);
    pv_action(c, PV_SELECT_ALL);
    pv_action(c, PV_COPY);
    assert(!strncmp(tiku_clipboard_text(), "Page 1\nHello TikuPDF world",
                    25));
}

/** @brief The page field goes where it is told, and says when it cannot. */
static void field_tests(tiku_pdfview_t *c)
{
    tiku_dialog_focus(&c->dialog, PV_PAGE_FIELD);
    tiku_field_start(&c->go, "7");
    pv_go_typed(c);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 6);
    assert(c->dialog.focus == PV_CANVAS);
    tiku_dialog_focus(&c->dialog, PV_PAGE_FIELD);
    tiku_field_start(&c->go, "99");
    pv_go_typed(c);
    assert(strstr(c->status, "no page 99"));
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/** @brief A key pressed in the viewer. */
static void press(tiku_pdfview_t *c, int key)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key};

    pv_event(c->window, &ev, c);
}

/** @brief The Pages column: each page drawn small, the page in view
 *         marked and followed, a click, a key or the wheel moving it, a
 *         thumbnail on its way let go when another document opens, and
 *         Outline taking the column back. */
static void thumbnail_tests(tiku_pdfview_t *c, tiku_surface_t *s,
                            const char *file)
{
    int rh = pv_thumb_row(), back, x, y;
    tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .dy = 2};
    pv_layout_t l;
    pv_tile_t *t;

    pv_action(c, PV_FIRST);
    pv_action(c, PV_THUMBS);
    assert(c->side && c->thumbs);
    settle(c);
    l = layout(c);
    t = pv_thumb(c, 0);
    assert(l.side.w == PV_SIDE_W && t && t->px && !t->pending);
    assert(t->w <= PV_THUMB_W && t->h <= PV_THUMB_H && t->h >= PV_THUMB_H - 1);
    assert(pv_thumb(c, 1) && pv_thumb(c, 1)->px && !pv_thumb(c, 6));
    /* Paper inside, the page in view ringed with the selection. */
    x = l.side.x + (l.side.w - t->w) / 2;
    y = l.side.y + 6 + (PV_THUMB_H - t->h) / 2;
    assert((s->px[(size_t)(y + 2) * s->w + x + 2] & 0xffffffu) == 0xffffffu);
    assert((s->px[(size_t)(y - 3) * s->w + x + 5] & 0xffffffu) ==
           (TIKU_C_SELECT & 0xffffffu));

    back = c->nback;
    click(c, l.side.x + l.side.w / 2, l.side.y + rh + rh / 2);
    assert(pv_current(c) == 1 && c->nback == back + 1);
    assert(c->dialog.focus == PV_SIDE);
    press(c, TIKU_KEY_END);
    assert(pv_current(c) == PAGES - 1);
    settle(c);
    assert(c->thumb_top == PAGES * rh - l.side.h);
    assert(pv_thumb(c, PAGES - 1) && pv_thumb(c, PAGES - 1)->px);
    press(c, TIKU_KEY_UP);
    assert(pv_current(c) == PAGES - 2);
    press(c, TIKU_KEY_HOME);
    settle(c);
    assert(pv_current(c) == 0 && c->thumb_top == 0);
    wheel.x = l.side.x + 10;
    wheel.y = l.side.y + 10;
    pv_event(c->window, &wheel, c);
    settle(c);
    assert(c->thumb_top == rh && pv_current(c) == 0);

    /* Rows not drawn yet: one is asked for, then the document goes. */
    c->thumb_top = 6 * rh;
    tiku_workspace_draw(desk);
    assert(c->thumb_busy == 1 && pv_thumb(c, 6)->pending);
    assert(tiku_pdfview_file(c, file));
    settle(c);
    assert(c->doc && !c->thumb_busy && c->thumb_top == 0);
    assert(pv_thumb(c, 0) && pv_thumb(c, 0)->px && !pv_thumb(c, 6));

    pv_action(c, PV_OUTLINE);
    assert(c->side && !c->thumbs);
    pv_action(c, PV_THUMBS);
    pv_action(c, PV_THUMBS);
    assert(!c->side && !c->thumbs && !layout(c).side.w);
    pv_action(c, PV_OUTLINE);
    assert(c->side && !c->thumbs && layout(c).side.w);
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/** @brief Write @p e's encrypted fixture to @p path. */
static void write_locked(const enc_t *e, const char *path)
{
    encw_t b = enc_make(e);
    FILE *f = fopen(path, "wb");

    assert(f && fwrite(b.p, 1, (size_t)b.n, f) == (size_t)b.n);
    fclose(f);
    free(b.p);
}

/** @brief Return in the focused field, as the keyboard sends it. */
static void enter(tiku_pdfview_t *c)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    pv_event(c->window, &ev, c);
}

/** @brief A locked PDF asks for its password in the find field's place
 *         and forgets it once tried; the author's no-copy is kept. */
static void locked_tests(tiku_pdfview_t *c, const char *dir)
{
    static const enc_t locked = {"locked", 5, 6, 256, 1, "secret", "boss", -4};
    static const enc_t nocopy = {"nocopy", 2, 3, 128, 0, "", "owner", -20};
    char path[256], other[256];

    snprintf(path, sizeof path, "%s/locked.pdf", dir);
    snprintf(other, sizeof other, "%s/nocopy.pdf", dir);
    write_locked(&locked, path);
    write_locked(&nocopy, other);
    assert(tiku_pdfview_file(c, path));
    settle(c);
    assert(c->locked && strstr(c->status, "locked") && c->npage == PAGES);
    assert(c->dialog.focus == PV_FIND_FIELD);
    tiku_field_start(&c->pass, "wrong");
    enter(c);
    assert(!c->pass.text[0]);                   /* forgotten once tried */
    settle(c);
    assert(c->locked && strstr(c->status, "does not open"));
    tiku_field_start(&c->pass, "secret");
    enter(c);
    settle(c);
    assert(!c->locked && c->npage == 1 && c->may_copy);
    assert(c->pages[0].text && c->pages[0].nch > 5);
    assert(tiku_pdfview_file(c, other));
    settle(c);
    assert(c->npage == 1 && !c->may_copy);
    tiku_clipboard_set("before");
    pv_select_page(c, 0);
    pv_action(c, PV_COPY);
    assert(!strcmp(tiku_clipboard_text(), "before"));
    assert(strstr(c->status, "does not allow"));
    unlink(path);
    unlink(other);
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/tikupdf-XXXXXX", file[256], junk[256], shot[256];
    pdfw_t b = document();
    int dirty = 0, picks = 0;
    FILE *f;

    (void)argv;
    assert(mkdtemp(dir));
    snprintf(file, sizeof file, "%s/tiku.pdf", dir);
    snprintf(junk, sizeof junk, "%s/notes.pdf", dir);
    snprintf(shot, sizeof shot, "%s/viewer.png", dir);
    f = fopen(file, "wb");
    assert(f && fwrite(b.p, 1, (size_t)b.n, f) == (size_t)b.n);
    fclose(f);
    free(b.p);
    f = fopen(junk, "wb");
    assert(f && fputs("just some notes\n", f) >= 0);
    fclose(f);

    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    desk = tiku_workspace_new(s);
    tiku_pdfview_t *c = tiku_pdfview_new(desk, &dirty, chosen, &picks);
    assert(c);
    tiku_pdfview_open(c);
    assert(c->window && !c->doc && !pv_enabled(c, PV_COPY));
    pv_action(c, PV_OPEN);
    assert(picks == 1);
    open_tests(c, file);
    view_tests(c);
    navigation_tests(c);
    search_tests(c);
    selection_tests(c);
    field_tests(c);
    thumbnail_tests(c, s, file);
    pv_action(c, PV_FIRST);
    settle(c);
    tiku_field_start(&c->find, "TikuPDF");
    pv_find(c, 1);
    settle(c);
    tiku_workspace_draw(desk);
    if (argc > 1) assert(tiku_surface_png(s, shot) == 0);

    /* Not a PDF: said so, and the document shown stays. */
    assert(tiku_pdfview_file(c, junk));
    settle(c);
    assert(strstr(c->status, "not a PDF") && c->npage == PAGES);
    locked_tests(c, dir);

    tiku_pdfview_close(c);
    settle(c);
    assert(!c->window && !c->doc && !c->npage);
    tiku_pdfview_open(c);
    assert(c->window && !c->doc);
    assert(tiku_pdfview_file(c, file));
    tiku_pdfview_destroy(c);
    desk = NULL;
    settle(NULL);
    tiku_workspace_free(tiku_workspace_new(s));
    tiku_surface_free(s);
    if (argc > 1) printf("Preview: %s\n", shot);
    else { unlink(file); unlink(junk); rmdir(dir); }
    puts("PDF viewer: open, draw, zoom, outline, links, find, copy, "
         "thumbnails, passwords, close PASS");
    return 0;
}
