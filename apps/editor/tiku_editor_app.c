/* tiku_editor_app.c -- Editor descriptor, launch document and lifecycle.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

static char edit_path[512];

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
    if (!tiku_editor_buffer_init(&st->tv)) { free(st); return -1; }
    st->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_DOC);
    /* Only worth keeping when something can use it; a NULL one simply
     * means every frame goes over as pixels, which is what happens
     * linked into a desktop anyway. */
    st->dl = (services->present != NULL) ? tiku_dl_new() : NULL;
    if (st->surface == NULL) {
        tiku_editor_buffer_free(&st->tv);
        tiku_dl_free(st->dl);
        free(st);
        return -1;
    }
    const char *document = services->startup_document;
    if (!document) document = edit_path;
    if (strlen(document) >= sizeof st->path) {
        tiku_editor_buffer_free(&st->tv);
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
    tiku_editor_load(st);
    slash = strrchr(st->path, '/');
    snprintf(title, sizeof title, "%.63s",
             (slash != NULL) ? slash + 1
             : (st->path[0] != '\0') ? st->path : "Edit");
    st->id = services->open(services->ctx, title, EDIT_W, EDIT_H);
    if (!st->id) {
        tiku_editor_buffer_free(&st->tv);
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
        if (tiku_editor_buffer_modified(&st->tv)) {
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
        tiku_editor_buffer_free(&st->tv);
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

void
tiku_editor_startup_path(const char *path)
{
    snprintf(edit_path, sizeof edit_path, "%s", path ? path : "");
}
