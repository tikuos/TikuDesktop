/* tiku_editor_app.c -- Editor descriptor, launch document and lifecycle.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static char edit_path[512];

/** Put the view's selection on the desk clipboard, its line breaks as LF
 *  whatever the document uses.  @return 0 when the clipboard took it. */
static int clip_set(void *ctx, const char *text, size_t length)
{
    edit_state_t *st = ctx;
    char *lf = st->services->clip_set ? malloc(length + 1) : NULL;
    size_t out = 0;
    int taken;
    if (!lf) return -1;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n') continue;
        lf[out++] = text[i] == '\r' ? '\n' : text[i];
    }
    lf[out] = 0;
    taken = st->services->clip_set(st->services->ctx, lf) >= 0;
    free(lf);
    return taken ? 0 : -1;
}

/** The desk clipboard's text with each CRLF, CR or LF made the document's
 *  line break; NULL when it holds nothing or cannot be copied. */
static const char *clip_text(void *ctx, size_t *length)
{
    edit_state_t *st = ctx;
    const char *held = st->services->clip_text
                       ? st->services->clip_text(st->services->ctx, NULL) : NULL;
    size_t breaks = 0, n, wide = strlen(st->newline), out = 0;
    free(st->paste); st->paste = NULL;
    if (!held || !held[0]) return NULL;
    n = strlen(held);
    for (size_t i = 0; i < n; i++) breaks += held[i] == '\n' || held[i] == '\r';
    if (breaks > (SIZE_MAX - n - 1) / 2 || !(st->paste = malloc(n + breaks * wide + 1)))
        return NULL;
    for (size_t i = 0; i < n; i++) {
        if (held[i] != '\r' && held[i] != '\n') { st->paste[out++] = held[i]; continue; }
        i += held[i] == '\r' && held[i + 1] == '\n';
        memcpy(st->paste + out, st->newline, wide); out += wide;
    }
    st->paste[out] = 0;
    *length = out;
    return st->paste;
}

/** Make the document and the view that shows it, with the clipboard the
 *  services lend.  @return 0, or -1 with nothing kept. */
static int document_new(edit_state_t *st)
{
    tiku_editdoc_limits_t limits = {INT_MAX - 1u, TIKU_EDITDOC_HISTORY_DEFAULT};
    tiku_codeview_host_t host = {.ctx = st, .clip_set = clip_set, .clip_text = clip_text};
    if (tiku_editdoc_create(&limits, &st->doc) != TIKU_EDIT_OK) return -1;
    if (tiku_codeview_create(st->doc, &st->view) != TIKU_EDIT_OK) {
        tiku_editdoc_release(st->doc); st->doc = NULL; return -1;
    }
    tiku_codeview_set_host(st->view, &host);
    snprintf(st->newline, sizeof st->newline, "\n");
    tiku_codeview_set_options(st->view, TIKU_CODEVIEW_LINE_NUMBERS);
    return 0;
}

/** Free the view, the document and what the clipboard left. */
static void document_free(edit_state_t *st)
{
    tiku_codeview_destroy(st->view); st->view = NULL;
    tiku_editdoc_release(st->doc); st->doc = NULL;
    free(st->paste); st->paste = NULL;
}

int
tiku_editor_start(void **state, const tiku_app_services_t *services)
{
    edit_state_t *st = calloc(1, sizeof *st);
    const char *slash;
    char title[64];

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->w = EDIT_W; st->h = EDIT_H; st->numbers = 1;
    st->find_focus = -1; st->child_fd = -1;
    st->hover_tool = st->pressed_tool = -1; st->match_case = 1;
    tiku_icons_tiku_register();
    tiku_scroll_init(&st->vertical, 1);
    tiku_scroll_init(&st->horizontal, tiku_font_mono_cell(0));
    for (int i = 0; i < 3; i++) {
        tiku_field_start(&st->field[i], "");
        tiku_field_input_init(&st->field_input[i]);
    }
    if (document_new(st)) { free(st); return -1; }
    st->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_DOC);
    /* Only worth keeping when something can use it; a NULL one simply
     * means every frame goes over as pixels, which is what happens
     * linked into a desktop anyway. */
    st->dl = (services->present != NULL) ? tiku_dl_new() : NULL;
    if (st->surface == NULL) {
        document_free(st);
        tiku_dl_free(st->dl);
        free(st);
        return -1;
    }
    const char *document = services->startup_document;
    if (!document) document = edit_path;
    if (strlen(document) >= sizeof st->path) {
        document_free(st);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        tiku_surface_free(st->search_surface);
        free(st);
        return -1;
    }
    snprintf(st->path, sizeof st->path, "%s", document);
    /* The name decides, once, before anything is drawn; after that the
     * menu does.  A file called .bas opens coloured, and everything else
     * opens as what it is -- prose. */
    st->lang = tiku_syntax_of_path(st->path);
    tiku_codeview_set_language(st->view, st->lang);
    tiku_editor_load(st);
    slash = strrchr(st->path, '/');
    snprintf(title, sizeof title, "%.63s",
             (slash != NULL) ? slash + 1
             : (st->path[0] != '\0') ? st->path : "Edit");
    st->id = services->open(services->ctx, title, EDIT_W, EDIT_H);
    if (!st->id) {
        document_free(st);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        tiku_surface_free(st->search_surface);
        free(st);
        return -1;
    }
    if (services->sizeable) services->sizeable(services->ctx, st->id);
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/editor.html");
    }
    tiku_editor_paint(st);
    tiku_editor_publish(st);
    *state = st;
    return 0;
}

void
tiku_editor_stop(void *state)
{
    edit_state_t *st = state;

    if (st != NULL) {
        if (st->running && st->child > 0) {
            kill(st->child, SIGKILL);
            while (waitpid(st->child, NULL, 0) < 0 && errno == EINTR) {}
        }
        if (st->child_fd >= 0) close(st->child_fd);
        if (st->scratch[0]) unlink(st->scratch);
        if (tiku_editor_modified(st)) {
            char recovery[] = "/tmp/TikuEdit-recovery-XXXXXX";
            int fd = mkstemp(recovery);
            if (fd >= 0) {
                FILE *f = fdopen(fd, "wb");
                if (f) {
                    int ok = tiku_editor_write_document(st, f) && !fflush(f) && !fsync(fd);
                    if (fclose(f)) ok = 0;
                    fprintf(stderr, "Editor recovery %s: %s\n", ok ? "saved" : "incomplete", recovery);
                } else close(fd);
            }
        }
        document_free(st);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        tiku_surface_free(st->search_surface);
        free(st);
    }
}

const tiku_app_descriptor_t tiku_edit_app = {
    .id = "org.tikuos.edit",
    .name = "Edit",
    .start = tiku_editor_start,
    .stop = tiku_editor_stop,
    .event = tiku_editor_event,
    .event_in = tiku_editor_event_in,
    .pick = tiku_editor_pick,
    .tick = tiku_editor_tick,
    .picked = tiku_editor_picked,
    .closed = tiku_editor_closed
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_edit_app
};
#endif

/** Name the document a standalone launch opens.  @return 0, naming none,
 *  when @p path does not fit; it is never cut short. */
int
tiku_editor_startup_path(const char *path)
{
    size_t n = path != NULL ? strlen(path) : 0;

    edit_path[0] = '\0';
    if (n >= sizeof edit_path) {
        return 0;
    }
    memcpy(edit_path, path != NULL ? path : "", n + 1);
    return 1;
}
