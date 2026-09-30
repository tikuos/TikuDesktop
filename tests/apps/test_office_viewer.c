/* TikuDoc and TikuSlides driven without a desktop: the empty window, a
 * Word file laid out on a worker and shown, its outline followed, found
 * in, exported as a PDF; a file that is no document refused with the
 * shown one kept; slides fitted whole and presented, stepped through and
 * left; closing.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../apps/office/tiku_officeview.c"
#include "tiku_fontset.h"
#include "tiku_text_input.h"
#include "../fixtures/office.inc"

static tiku_workspace_t *desk;
static tiku_surface_t *screen;
static const char *shots[2];        /* where to keep the slides' pictures */
static char scratch[PATH_MAX];
static int picks, saves;            /* the questions put to the person   */

/** @brief The Open and Save panels, as counted questions. */
static void host_pick(void *ctx, int save, const char *start,
                      const char *name)
{
    (void)ctx;
    (void)start;
    if (save) {
        saves++;
        assert(name && strcmp(name, "features.pdf") == 0);
    } else {
        picks++;
    }
}

/** @brief Every job run and landed, the window drawn each turn. */
static void settle(tiku_officeview_t *c)
{
    for (int i = 0; i < 20000; i++) {
        (void)tiku_work_collect();
        if (desk) tiku_workspace_draw(desk);
        if (!tiku_work_busy() && (!c || (!tiku_pdf_view_loading(c->view) &&
                                         !c->exporting))) return;
        usleep(1000);
    }
    assert(!"a job never finished");
}

/** @brief A fixture's bytes written to @p name in the scratch folder;
 *         its path into @p out. */
static void fixture(const char *which, const char *name, char *out)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *b64 = NULL;
    unsigned v = 0;
    int bits = 0;
    FILE *f;

    for (size_t k = 0; k < sizeof fixtures / sizeof fixtures[0]; k++)
        if (!strcmp(fixtures[k].name, which)) b64 = fixtures[k].data;
    assert(b64);
    snprintf(out, PATH_MAX, "%s/%s", scratch, name);
    f = fopen(out, "wb");
    assert(f);
    for (; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            fputc((int)(v >> bits) & 0xff, f);
        }
    }
    fclose(f);
}

/** @brief A key pressed in the viewer. */
static void press(tiku_officeview_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key,
                       .modifiers = mods};

    ov_event(c->window, &ev, c);
}

/** @brief Text typed into whatever has the focus. */
static void type(tiku_officeview_t *c, const char *text)
{
    for (; *text; text++) {
        tiku_text_input_t in;
        tiku_event_t ev;
        char one[2] = { *text, 0 };

        (void)tiku_text_input_set(&in, one, 1);
        while (tiku_text_input_next(&in, &ev)) ov_event(c->window, &ev, c);
    }
}

/** @brief Before a file is open: the pages' place blank, the status line
 *         saying nothing, the window called by the application's name. */
static void empty_tests(tiku_officeview_t *c, const tiku_surface_t *s)
{
    ov_layout_t l = ov_layout(c, tiku_window_content(c->window));

    tiku_workspace_draw(desk);
    assert(!ov_open_now(c) && !c->said[0] &&
           !tiku_pdf_view_status(c->view)[0]);
    assert(!strcmp(c->window->title, "TikuDoc"));
    for (int y = l.view.y + 1; y < l.view.y + l.view.h - 1; y += 3)
        for (int x = l.view.x + 1; x < l.view.x + l.view.w - 1; x += 3)
            assert((s->px[(size_t)y * s->w + x] & 0xffffffu) ==
                   (TIKU_C_DOC & 0xffffffu));
    assert(!ov_enabled(c, OV_EXPORT) && ov_enabled(c, OV_OPEN));
    ov_action(c, OV_OPEN);
    assert(picks == 1);
}

/** @brief A Word file laid out on a worker: its pages shown, the window
 *         titled by it, Export offered its name as a PDF's. */
static void open_tests(tiku_officeview_t *c, const char *docx)
{
    assert(tiku_officeview_file(c, docx));
    settle(c);
    assert(ov_open_now(c) && tiku_pdf_view_pages(c->view) == 2);
    assert(!strcmp(tiku_officeview_path(c), docx));
    assert(strstr(c->window->title, " - TikuDoc"));
    assert(strstr(tiku_pdf_view_status(c->view), "2 pages"));
    assert(tiku_pdf_view_fitted(c->view) == TIKU_PDF_VIEW_FIT_WIDTH);
    ov_action(c, OV_EXPORT);
    assert(saves == 1);
}

/** @brief The outline down the left: shown with the document's headings,
 *         a click on one follows it, and it hides and comes back. */
static void outline_tests(tiku_officeview_t *c)
{
    ov_layout_t l = ov_layout(c, tiku_window_content(c->window));
    const tiku_pdf_outline_t *items = NULL;
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1};
    int n = ov_entries(c, &items);

    assert(n >= 1 && l.side.w > 0 && l.view.x > l.side.x);
    assert(!strcmp(items[0].title, "Word features"));
    press.x = l.side.x + 20;
    press.y = tiku_list_row_rect(&c->outline, l.side, 0).y + 4;
    ov_event(c->window, &press, c);
    settle(c);
    assert(tiku_list_chosen(&c->outline) == 0 &&
           tiku_dialog_focused(&c->dialog, OV_SIDE));
    ov_action(c, OV_OUTLINE);
    l = ov_layout(c, tiku_window_content(c->window));
    assert(!c->outline_on && l.side.w == 0);
    ov_action(c, OV_OUTLINE);
    l = ov_layout(c, tiku_window_content(c->window));
    assert(c->outline_on && l.side.w > 0);
}

/** @brief Found: the find field takes the keys, Return looks. */
static void find_tests(tiku_officeview_t *c)
{
    press(c, 'f', TIKU_MOD_CMD);
    assert(tiku_dialog_focused(&c->dialog, OV_FIND_FIELD));
    type(c, "strong");
    press(c, TIKU_KEY_RETURN, 0);
    settle(c);
    assert(!strcmp(c->find.text, "strong") && ov_enabled(c, OV_FIND_NEXT));
    press(c, TIKU_KEY_ESCAPE, 0);
    assert(!c->find.text[0] && !tiku_dialog_focused(&c->dialog,
                                                    OV_FIND_FIELD));
}

/** @brief Exported: a PDF where the Save panel said, its ending added,
 *         and said so. */
static void export_tests(tiku_officeview_t *c)
{
    char out[PATH_MAX];
    struct stat st;

    snprintf(out, sizeof out, "%s/exported", scratch);
    assert(tiku_officeview_export_to(c, out));
    settle(c);
    snprintf(out, sizeof out, "%s/exported.pdf", scratch);
    assert(stat(out, &st) == 0 && st.st_size > 1000);
    assert(!strcmp(c->said, "Exported as exported.pdf."));
    unlink(out);
}

/** @brief A file that is no document: refused in words, the one shown
 *         kept. */
static void refused_tests(tiku_officeview_t *c, const char *junk,
                          const char *docx)
{
    assert(tiku_officeview_file(c, junk));
    settle(c);
    assert(ov_open_now(c) && !strcmp(tiku_officeview_path(c), docx));
    assert(strstr(tiku_pdf_view_status(c->view), "not a document"));
}

/** @brief TikuSlides: slides fitted whole, presented and stepped through
 *         with the keys and a click, and left with Escape. */
static void slides_tests(const char *pptx)
{
    static const tiku_officeview_kind_t kind = { "TikuSlides", 1 };
    tiku_officeview_host_t host = { host_pick, NULL };
    int dirty = 0;
    tiku_officeview_t *c = tiku_officeview_new(desk, &dirty, &kind, &host);
    tiku_event_t click = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1};

    assert(c);
    tiku_officeview_open(c);
    assert(tiku_officeview_file(c, pptx));
    settle(c);
    assert(tiku_pdf_view_pages(c->view) == 3 &&
           tiku_pdf_view_fitted(c->view) == TIKU_PDF_VIEW_FIT_PAGE);
    assert(!strcmp(ov_label(c, OV_NEXT), "Next Slide"));
    tiku_workspace_draw(desk);
    if (shots[0]) assert(tiku_surface_png(screen, shots[0]) == 0);
    ov_action(c, OV_PRESENT);
    assert(c->presenting);
    settle(c);
    tiku_workspace_draw(desk);
    if (shots[1]) assert(tiku_surface_png(screen, shots[1]) == 0);
    press(c, TIKU_KEY_RIGHT, 0);
    settle(c);
    assert(tiku_pdf_view_page(c->view) == 1);
    press(c, TIKU_KEY_LEFT, 0);
    settle(c);
    assert(tiku_pdf_view_page(c->view) == 0);
    click.x = c->window->frame.x + 200;
    click.y = c->window->frame.y + 200;
    ov_event(c->window, &click, c);
    settle(c);
    assert(tiku_pdf_view_page(c->view) == 1);
    press(c, TIKU_KEY_ESCAPE, 0);
    assert(!c->presenting);
    tiku_officeview_destroy(c);
    settle(NULL);
}

int main(int argc, char **argv)
{
    tiku_officeview_host_t host = { host_pick, NULL };
    static const tiku_officeview_kind_t kind = { "TikuDoc", 0 };
    char docx[PATH_MAX], pptx[PATH_MAX], junk[PATH_MAX];
    const char *tmp = getenv("TMPDIR");
    int dirty = 0;

    if (tiku_fontset_count() == 0) {
        printf("TikuDoc: no faces on this machine to set text in: SKIP\n");
        return 0;
    }
    snprintf(scratch, sizeof scratch, "%s/tikuoffice-XXXXXX",
             tmp ? tmp : "/tmp");
    assert(mkdtemp(scratch));
    fixture("docx_features", "features.docx", docx);
    fixture("pptx_features", "features.pptx", pptx);
    fixture("not_office", "junk.docx", junk);

    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    screen = s;
    shots[0] = argc > 2 ? argv[2] : NULL;
    shots[1] = argc > 3 ? argv[3] : NULL;
    desk = tiku_workspace_new(s);
    tiku_officeview_t *c = tiku_officeview_new(desk, &dirty, &kind, &host);
    assert(c);
    tiku_officeview_open(c);
    assert(c->window);
    empty_tests(c, s);
    open_tests(c, docx);
    outline_tests(c);
    find_tests(c);
    export_tests(c);
    refused_tests(c, junk, docx);
    tiku_workspace_draw(desk);
    if (argc > 1) assert(tiku_surface_png(s, argv[1]) == 0);
    ov_action(c, OV_CLOSE);
    settle(c);
    assert(!c->window);
    tiku_officeview_destroy(c);
    slides_tests(pptx);
    tiku_workspace_free(desk);
    desk = NULL;
    settle(NULL);
    tiku_surface_free(s);
    unlink(docx);
    unlink(pptx);
    unlink(junk);
    rmdir(scratch);
    printf("TikuDoc and TikuSlides: empty window, open, outline, find, "
           "export, a refused file, slides presented PASS\n");
    return 0;
}
