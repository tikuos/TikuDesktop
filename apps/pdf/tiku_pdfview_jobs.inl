/* Jobs: everything that reads a document, one job at a time for each
 * document (they share its key), and only their done callbacks touch
 * the viewer. */

/** @brief Page @p p's size as drawn at scale 1: rotation applied. */
static void pv_page_size(tiku_pdf_t *pdf, int p, float *w, float *h)
{
    double pw = 612, ph = 792;
    int rot = 0;

    (void)tiku_pdf_page_size(pdf, p, &pw, &ph, &rot);
    if (rot == 90 || rot == 270) {
        double t = pw;
        pw = ph;
        ph = t;
    }
    *w = (float)pw;
    *h = (float)ph;
}

/* ------------------------------------------------------------ opening  */

/** @brief Opening a file: the document and all a view needs up front. */
typedef struct {
    tiku_pdfview_t *owner;
    int generation, npage, noutline, may_copy, tried;
    tiku_pdf_err_t err;
    char path[TIKU_PATH_MAX], *title, password[TIKU_FIELD_MAX];
    tiku_pdf_t *pdf;
    float *pw, *ph;
    tiku_pdf_outline_t *outline;
} pv_load_t;

/** @brief On a worker: open, measure every page, read the outline. */
static void pv_load_run(void *ctx)
{
    pv_load_t *j = ctx;

    j->err = tiku_pdf_open_password(j->path, j->password, &j->pdf);
    memset(j->password, 0, sizeof j->password);   /* used; not kept */
    if (j->err != TIKU_PDF_OK) return;
    j->may_copy = tiku_pdf_may_copy(j->pdf);
    j->npage = tiku_pdf_pages(j->pdf);
    if (j->npage <= 0) {
        j->err = TIKU_PDF_BAD;
        return;
    }
    j->pw = malloc((size_t)j->npage * sizeof *j->pw);
    j->ph = malloc((size_t)j->npage * sizeof *j->ph);
    if (!j->pw || !j->ph) {
        j->err = TIKU_PDF_BAD;
        return;
    }
    for (int p = 0; p < j->npage; p++)
        pv_page_size(j->pdf, p, &j->pw[p], &j->ph[p]);
    j->noutline = tiku_pdf_outline(j->pdf, &j->outline);
    if (j->noutline < 0) j->noutline = 0;
    j->title = tiku_pdf_title(j->pdf);
}

/** @brief Why a file did not open, in a person's words. */
static const char *pv_load_error(tiku_pdf_err_t err)
{
    if (err == TIKU_PDF_NOT_PDF) return "That file is not a PDF.";
    if (err == TIKU_PDF_UNSUPPORTED)
        return "That PDF is encrypted in a way TikuPDF does not read.";
    return "That PDF is damaged beyond what TikuPDF can repair.";
}

/** @brief On the loop: the old document goes, the new one is shown from
 *         its first page, fitted to the window's width. */
static void pv_load_done(void *ctx)
{
    pv_load_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    pv_doc_t *d;
    const char *name = strrchr(j->path, '/');

    if (j->generation != c->generation || !c->workspace) return;
    if (j->err == TIKU_PDF_PASSWORD) {
        /* Locked: the find field asks for its password, masked. */
        c->locked = 1;
        snprintf(c->locked_path, sizeof c->locked_path, "%s", j->path);
        pv_forget_password(c);
        tiku_dialog_focus(&c->dialog, PV_FIND_FIELD);
        pv_say(c, j->tried ? "That password does not open it. Try again, "
                             "or press Escape." :
                             "This PDF is locked. Type its password in the "
                             "field above and press Enter.");
        return;
    }
    if (j->err != TIKU_PDF_OK) {
        pv_say(c, pv_load_error(j->err));
        return;
    }
    c->locked = 0;
    d = calloc(1, sizeof *d);
    if (!d) {
        pv_say(c, "Not enough memory to open that PDF.");
        return;
    }
    pv_forget(c);
    d->pdf = j->pdf;
    j->pdf = NULL;
    d->refs = 1;
    snprintf(d->key, sizeof d->key, "pdf:%p", (void *)d);
    atomic_init(&d->search, c->search_gen);
    c->doc = d;
    c->npage = j->npage;
    c->pw = j->pw;
    c->ph = j->ph;
    c->outline = j->outline;
    c->noutline = j->noutline;
    j->pw = j->ph = NULL;
    j->outline = NULL;
    c->pages = calloc((size_t)c->npage, sizeof *c->pages);
    c->top = calloc((size_t)c->npage, sizeof *c->top);
    c->folded = calloc((size_t)c->noutline + 1, 1);
    c->rows = calloc((size_t)c->noutline + 1, sizeof *c->rows);
    if (!c->pages || !c->top || !c->folded || !c->rows) {
        pv_forget(c);
        pv_say(c, "Not enough memory to show that PDF.");
        return;
    }
    for (int p = 1; p < c->npage; p++)
        if (c->pw[p] > c->pw[c->widest]) c->widest = p;
    pv_outline_start(c);
    snprintf(c->path, sizeof c->path, "%s", j->path);
    snprintf(c->title, sizeof c->title, "%s",
             j->title && j->title[0] ? j->title : name ? name + 1 : j->path);
    if (c->window)
        snprintf(c->window->title, sizeof c->window->title, "TikuPDF - %.140s",
                 name ? name + 1 : j->path);
    c->fit = PV_BY_WIDTH;
    c->may_copy = j->may_copy;
    char said[160];
    snprintf(said, sizeof said, "%d page%s%s. Drag across text to select "
             "it; Ctrl+F finds.", c->npage, c->npage == 1 ? "" : "s",
             c->noutline ? ", with an outline" : "");
    pv_say(c, said);
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/** @brief After done, or instead of it: give back what was not taken. */
static void pv_load_free(void *ctx)
{
    pv_load_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    tiku_pdf_close(j->pdf);
    free(j->pw);
    free(j->ph);
    tiku_pdf_outline_free(j->outline, j->noutline);
    free(j->title);
    c->loading = 0;
    if (c->workspace) {
        pv_menus(c);
        pv_changed(c);
    }
    pv_unref(c);
    free(j);
}

/** @brief Ask a worker to open @p path, with @p password when it is
 *         locked ("" for none).  One opening at a time. */
static int pv_load(tiku_pdfview_t *c, const char *path, const char *password)
{
    pv_load_t *j;
    tiku_work_job_t work;

    if (!path || !*path || strlen(path) >= TIKU_PATH_MAX) return 0;
    if (c->loading) {
        pv_say(c, "A PDF is still opening. Try again in a moment.");
        return 0;
    }
    j = calloc(1, sizeof *j);
    if (!j) return 0;
    j->owner = c;
    j->generation = c->generation;
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->password, sizeof j->password, "%s", password);
    j->tried = password[0] != '\0';
    memset(&work, 0, sizeof work);
    work.run = pv_load_run;
    work.done = pv_load_done;
    work.free = pv_load_free;
    work.ctx = j;
    c->refs++;
    c->loading = 1;
    if (tiku_work_post(&work) != 0) {
        pv_load_free(j);
        pv_say(c, "The workers are busy. Try again in a moment.");
        return 0;
    }
    pv_say(c, "Opening...");
    pv_menus(c);
    return 1;
}

/* ------------------------------------------------------------ drawing  */

/** @brief Drawing one page, and reading its links and text the first
 *         time it is drawn. */
typedef struct {
    tiku_pdfview_t *owner;
    pv_doc_t *doc;
    int slot, page, text, nlink, failed;
    double scale;                   /* what the page is drawn at        */
    long nch;
    tiku_pdf_pixmap_t pm;
    tiku_pdf_link_t *link;
    tiku_pdf_char_t *ch;
} pv_render_t;

/** @brief On a worker: the page's pixels, then what it says. */
static void pv_render_run(void *ctx)
{
    pv_render_t *j = ctx;

    if (tiku_pdf_render(j->doc->pdf, j->page, j->scale, 0, &j->pm) != 0)
        j->failed = 1;
    if (!j->text) return;
    j->nlink = tiku_pdf_links(j->doc->pdf, j->page, &j->link);
    if (j->nlink < 0) j->nlink = 0;
    j->nch = tiku_pdf_page_chars(j->doc->pdf, j->page, &j->ch);
    if (j->nch < 0) j->nch = 0;
}

/** @brief Keep the text of the pages used last, @c PV_TEXT_KEEP of them,
 *         never a page the selection or the view is on. */
static void pv_trim_text(tiku_pdfview_t *c)
{
    while (c->ntext > PV_TEXT_KEEP) {
        int old = -1;

        for (int p = 0; p < c->npage; p++) {
            pv_page_t *pg = &c->pages[p];
            if (!pg->text || pg->used == c->frame ||
                (c->selected && p >= c->anchor.page && p <= c->focus.page) ||
                (c->selected && p <= c->anchor.page && p >= c->focus.page))
                continue;
            if (old < 0 || pg->used < c->pages[old].used) old = p;
        }
        if (old < 0) return;
        tiku_pdf_links_free(c->pages[old].link, c->pages[old].nlink);
        free(c->pages[old].ch);
        c->pages[old].link = NULL;
        c->pages[old].ch = NULL;
        c->pages[old].nlink = 0;
        c->pages[old].nch = 0;
        c->pages[old].text = 0;
        c->ntext--;
    }
}

/** @brief On the loop: the pixels go to their slot, the text to the page. */
static void pv_render_done(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    pv_tile_t *t = &c->tile[j->slot];

    if (!c->workspace || j->doc != c->doc) return;
    if (t->pending && t->page == j->page) {
        t->pending = 0;
        if (j->failed) {
            t->page = -1;
            c->pages[j->page].failed = 1;
        } else {
            t->px = j->pm.px;
            t->w = j->pm.w;
            t->h = j->pm.h;
            j->pm.px = NULL;
        }
    }
    if (j->text && !c->pages[j->page].text) {
        pv_page_t *pg = &c->pages[j->page];
        pg->link = j->link;
        pg->nlink = j->nlink;
        pg->ch = j->ch;
        pg->nch = j->nch;
        pg->text = 1;
        pg->used = c->frame;
        j->link = NULL;
        j->ch = NULL;
        c->ntext++;
        pv_trim_text(c);
    }
    pv_changed(c);
}

/** @brief After done or a cancel: the slot is let go if it still waits. */
static void pv_render_free(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    if (j->doc == c->doc) {
        pv_tile_t *t = &c->tile[j->slot];
        c->inflight--;
        if (t->pending && t->page == j->page) {
            t->pending = 0;
            t->page = -1;
        }
        if (c->workspace) pv_changed(c);
    }
    tiku_pdf_pixmap_free(&j->pm);
    tiku_pdf_links_free(j->link, j->nlink);
    free(j->ch);
    pv_doc_unref(j->doc);
    pv_unref(c);
    free(j);
}

/** @brief The scale page @p p is drawn at: the view's, unless that would
 *         take more than @c PV_PAGE_PIXELS, when it is shown enlarged. */
static double pv_draw_scale(tiku_pdfview_t *c, int p)
{
    double area = (double)c->pw[p] * c->ph[p] * c->scale * c->scale;

    if (area <= (double)PV_PAGE_PIXELS) return c->scale;
    return sqrt((double)PV_PAGE_PIXELS / ((double)c->pw[p] * c->ph[p]));
}

/** @brief A slot for @p pixels more, giving back the pages shown least
 *         recently, never one on screen now.  @return it, or -1. */
static int pv_slot(tiku_pdfview_t *c, long pixels)
{
    for (;;) {
        long held = 0;
        int free_slot = -1, old = -1;

        for (int i = 0; i < PV_TILES; i++) {
            pv_tile_t *t = &c->tile[i];
            if (t->px) held += (long)t->w * t->h;
            if (t->pending) held += pixels;
            if (t->page < 0 && !t->pending) {
                if (free_slot < 0) free_slot = i;
            } else if (!t->pending && t->used != c->frame &&
                       (old < 0 || t->used < c->tile[old].used)) {
                old = i;
            }
        }
        if (free_slot >= 0 && held + pixels <= PV_PIXELS) return free_slot;
        if (old < 0) return -1;
        free(c->tile[old].px);
        memset(&c->tile[old], 0, sizeof c->tile[old]);
        c->tile[old].page = -1;
    }
}

/** @brief Ask a worker to draw page @p p at the view's scale. */
static int pv_request(tiku_pdfview_t *c, int p)
{
    pv_render_t *j;
    tiku_work_job_t work;
    double scale;
    int slot;

    if (!c->doc || c->inflight >= PV_INFLIGHT || c->pages[p].failed)
        return 0;
    scale = pv_draw_scale(c, p);
    slot = pv_slot(c, (long)(c->pw[p] * scale + 1) * (long)(c->ph[p] * scale
                                                             + 1));
    if (slot < 0) return 0;
    j = calloc(1, sizeof *j);
    if (!j) return 0;
    j->owner = c;
    j->doc = c->doc;
    j->slot = slot;
    j->page = p;
    j->scale = scale;
    j->text = !c->pages[p].text;
    memset(&work, 0, sizeof work);
    work.run = pv_render_run;
    work.done = pv_render_done;
    work.free = pv_render_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->doc->key);
    c->tile[slot].page = p;
    c->tile[slot].scale = c->scale;
    c->tile[slot].pending = 1;
    c->tile[slot].used = c->frame;
    c->refs++;
    c->doc->refs++;
    c->inflight++;
    if (tiku_work_post(&work) != 0) {
        c->tile[slot].page = -1;
        c->tile[slot].pending = 0;
        c->refs--;
        c->doc->refs--;
        c->inflight--;
        free(j);
        return 0;
    }
    return 1;
}

/* --------------------------------------------------------- thumbnails  */

/** @brief The scale page @p p fits the thumbnail box at. */
static double pv_thumb_scale(tiku_pdfview_t *c, int p)
{
    double sx = PV_THUMB_W / (c->pw[p] > 1 ? c->pw[p] : 1);
    double sy = PV_THUMB_H / (c->ph[p] > 1 ? c->ph[p] : 1);

    return sx < sy ? sx : sy;
}

/** @brief Page @p p's thumbnail, drawn or on its way; NULL for neither. */
static pv_tile_t *pv_thumb(tiku_pdfview_t *c, int p)
{
    for (int i = 0; i < PV_THUMB_KEEP; i++)
        if (c->thumb[i].page == p) return &c->thumb[i];
    return NULL;
}

/** @brief On the loop: the small page goes to its slot. */
static void pv_thumb_done(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    pv_tile_t *t = &c->thumb[j->slot];

    if (!c->workspace || j->doc != c->doc) return;
    if (t->pending && t->page == j->page) {
        t->pending = 0;
        if (j->failed) {
            t->page = -1;
            c->pages[j->page].failed = 1;
        } else {
            t->px = j->pm.px;
            t->w = j->pm.w;
            t->h = j->pm.h;
            j->pm.px = NULL;
        }
    }
    pv_changed(c);
}

/** @brief After done or a cancel: the slot is let go if it still waits. */
static void pv_thumb_free(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    if (j->doc == c->doc) {
        pv_tile_t *t = &c->thumb[j->slot];
        c->thumb_busy--;
        if (t->pending && t->page == j->page) {
            t->pending = 0;
            t->page = -1;
        }
        if (c->workspace) pv_changed(c);
    }
    tiku_pdf_pixmap_free(&j->pm);
    pv_doc_unref(j->doc);
    pv_unref(c);
    free(j);
}

/**
 * @brief Ask a worker to draw page @p p small, when the pages in view
 *        wait for nothing: thumbnails come one at a time, behind them.
 *        The slot is the free one, or the one shown least lately.
 */
static int pv_thumb_request(tiku_pdfview_t *c, int p)
{
    pv_render_t *j;
    tiku_work_job_t work;
    int slot = -1;

    if (!c->doc || c->inflight || c->thumb_busy || c->pages[p].failed)
        return 0;
    for (int i = 0; i < PV_THUMB_KEEP; i++) {
        pv_tile_t *t = &c->thumb[i];
        if (t->pending) continue;
        if (t->page < 0) { slot = i; break; }
        if (t->used != c->frame &&
            (slot < 0 || t->used < c->thumb[slot].used)) slot = i;
    }
    if (slot < 0 || !(j = calloc(1, sizeof *j))) return 0;
    free(c->thumb[slot].px);
    memset(&c->thumb[slot], 0, sizeof c->thumb[slot]);
    j->owner = c;
    j->doc = c->doc;
    j->slot = slot;
    j->page = p;
    j->scale = pv_thumb_scale(c, p);
    memset(&work, 0, sizeof work);
    work.run = pv_render_run;       /* the page's pixels, no text */
    work.done = pv_thumb_done;
    work.free = pv_thumb_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->doc->key);
    c->thumb[slot].page = p;
    c->thumb[slot].pending = 1;
    c->thumb[slot].used = c->frame;
    c->refs++;
    c->doc->refs++;
    c->thumb_busy++;
    if (tiku_work_post(&work) != 0) {
        c->thumb[slot].page = -1;
        c->thumb[slot].pending = 0;
        c->refs--;
        c->doc->refs--;
        c->thumb_busy--;
        free(j);
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------- searching  */

/** @brief One stretch of pages searched for the folded query. */
typedef struct {
    tiku_pdfview_t *owner;
    pv_doc_t *doc;
    int gen, from, to, nq, nmatch, cap;
    int keep_case, whole;           /* Match Case, Whole Words          */
    uint32_t q[TIKU_FIELD_MAX];
    pv_match_t *match;
} pv_search_t;

/** @brief A character as a search compares it: the kinds of space,
 *         hyphen and quote a typeset page uses folded together, and case
 *         too unless @p keep_case. */
static uint32_t pv_fold(uint32_t cp, int keep_case)
{
    if (cp == '\n' || cp == '\t' || cp == '\r' || cp == 0xa0) return ' ';
    if (keep_case && cp < 0x80) return cp;
    if (cp < 0x80) return (uint32_t)tolower((int)cp);
    if (!keep_case && cp >= 0xc0 && cp <= 0xde && cp != 0xd7) return cp + 0x20;
    if (cp == 0x2010 || cp == 0x2011 || cp == 0x2013 || cp == 0x2212)
        return '-';
    if (cp == 0x2018 || cp == 0x2019) return '\'';
    if (cp == 0x201c || cp == 0x201d) return '"';
    return cp;
}

/** @brief The boxes of characters @p a to @p b (inclusive): one a line. */
static void pv_match_boxes(pv_match_t *m, const tiku_pdf_char_t *ch, long a,
                           long b)
{
    m->nr = 0;
    for (long i = a; i <= b; i++) {
        const tiku_pdf_char_t *k = &ch[i];
        float *r;

        if (k->x1 <= k->x0) continue;
        r = m->nr ? m->r[m->nr - 1] : NULL;
        if (r && k->y0 < r[3] - 1 && k->y1 > r[1] + 1) {
            if (k->x0 < r[0]) r[0] = k->x0;
            if (k->y0 < r[1]) r[1] = k->y0;
            if (k->x1 > r[2]) r[2] = k->x1;
            if (k->y1 > r[3]) r[3] = k->y1;
        } else if (m->nr < 4) {
            r = m->r[m->nr++];
            r[0] = k->x0; r[1] = k->y0; r[2] = k->x1; r[3] = k->y1;
        } else {
            r = m->r[3];
            if (k->x0 < r[0]) r[0] = k->x0;
            if (k->x1 > r[2]) r[2] = k->x1;
            if (k->y1 > r[3]) r[3] = k->y1;
        }
    }
}

/** @brief Whether @p cp belongs to a word: a letter or digit (or _). */
static int pv_wordy(uint32_t cp)
{
    return cp < 0x80 ? isalnum((int)cp) || cp == '_'
                     : cp >= 0xc0 && cp != 0xd7 && cp != 0xf7;
}

/** @brief Every place on page @p p where the query stands, spaces folded
 *         to one so a phrase broken over two lines is still found; with
 *         Whole Words, only where no letter touches either end. */
static void pv_search_page(pv_search_t *j, int p, const tiku_pdf_char_t *ch,
                           long n)
{
    uint32_t *f = malloc((size_t)n * sizeof *f);
    long *at = malloc((size_t)n * sizeof *at), m = 0;

    for (long i = 0; f && at && i < n; i++) {
        uint32_t cp = pv_fold(ch[i].cp, j->keep_case);
        if (cp == ' ' && (m == 0 || f[m - 1] == ' ')) continue;
        f[m] = cp;
        at[m++] = i;
    }
    for (long i = 0; f && at && i + j->nq <= m; i++) {
        int k = 0;
        while (k < j->nq && f[i + k] == j->q[k]) k++;
        if (k < j->nq) continue;
        if (j->whole && ((i > 0 && pv_wordy(f[i - 1])) ||
                         (i + j->nq < m && pv_wordy(f[i + j->nq]))))
            continue;
        if (j->nmatch == j->cap) {
            int cap = j->cap ? j->cap * 2 : 32;
            pv_match_t *grown = realloc(j->match, (size_t)cap * sizeof *grown);
            if (!grown) break;
            j->match = grown;
            j->cap = cap;
        }
        j->match[j->nmatch].page = p;
        pv_match_boxes(&j->match[j->nmatch], ch, at[i], at[i + j->nq - 1]);
        j->nmatch++;
        i += j->nq - 1;
        if (j->nmatch >= PV_MATCHES) break;
    }
    free(f);
    free(at);
}

/** @brief On a worker: read each page's text and look for the query;
 *         a newer search, or a closed document, stops it between pages. */
static void pv_search_run(void *ctx)
{
    pv_search_t *j = ctx;

    for (int p = j->from; p < j->to && j->nmatch < PV_MATCHES; p++) {
        tiku_pdf_char_t *ch = NULL;
        long n;

        if (atomic_load(&j->doc->search) != j->gen) return;
        n = tiku_pdf_page_chars(j->doc->pdf, p, &ch);
        if (n > 0) pv_search_page(j, p, ch, n);
        free(ch);
    }
}

static int pv_search_post(tiku_pdfview_t *c, int from);
static void pv_find_step(tiku_pdfview_t *c, int dir);

/** @brief On the loop: the stretch's matches join the rest, and the next
 *         stretch is asked for behind any pages waiting to be drawn. */
static void pv_search_done(void *ctx)
{
    pv_search_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    char said[160];

    if (!c->workspace || j->doc != c->doc || j->gen != c->search_gen) return;
    if (j->nmatch > 0) {
        int room = PV_MATCHES - c->nmatch;
        int take = j->nmatch < room ? j->nmatch : room;
        pv_match_t *all = realloc(c->match, (size_t)(c->nmatch + take) *
                                            sizeof *all);
        if (all) {
            memcpy(all + c->nmatch, j->match, (size_t)take * sizeof *all);
            c->match = all;
            c->nmatch += take;
        }
    }
    c->searched = j->to;
    if (c->searched >= c->npage || c->nmatch >= PV_MATCHES ||
        !pv_search_post(c, c->searched))
        c->searching = 0;
    if (c->want_next) pv_find_step(c, c->want_next);
    else if (c->cur >= 0 && c->cur < c->nmatch) {
        snprintf(said, sizeof said, "Match %d of %d%s.", c->cur + 1,
                 c->nmatch, c->searching ? " so far" : "");
        pv_say(c, said);
    }
    if (c->searching && !c->nmatch) {
        snprintf(said, sizeof said, "Searching... %d of %d pages read.",
                 c->searched, c->npage);
        pv_say(c, said);
    } else if (!c->searching && !c->nmatch) {
        pv_say(c, "Not found in this document.");
    }
    pv_changed(c);
}

/** @brief After done or a cancel. */
static void pv_search_free(void *ctx)
{
    pv_search_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    free(j->match);
    pv_doc_unref(j->doc);
    pv_unref(c);
    free(j);
}

/** @brief Ask a worker to search the next stretch from page @p from. */
static int pv_search_post(tiku_pdfview_t *c, int from)
{
    pv_search_t *j = calloc(1, sizeof *j);
    tiku_work_job_t work;

    if (!j) return 0;
    j->owner = c;
    j->doc = c->doc;
    j->gen = c->search_gen;
    j->from = from;
    j->to = from + PV_SEARCH_STEP < c->npage ? from + PV_SEARCH_STEP
                                             : c->npage;
    j->keep_case = c->match_case;
    j->whole = c->whole_words;
    j->nq = pv_query(c->query, j->q, TIKU_FIELD_MAX, j->keep_case);
    memset(&work, 0, sizeof work);
    work.run = pv_search_run;
    work.done = pv_search_done;
    work.free = pv_search_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->doc->key);
    c->refs++;
    c->doc->refs++;
    if (tiku_work_post(&work) != 0) {
        c->refs--;
        c->doc->refs--;
        free(j);
        return 0;
    }
    return 1;
}
