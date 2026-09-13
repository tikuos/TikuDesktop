/* Editor's explicit launch document and ordinary empty launch.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#define TIKU_APP_EMBED
#include "../../apps/edit/tiku_edit.c"

static char title_seen[128];

/** @brief Capture the title without opening a real desktop window. */
static uint32_t window_open(void *ctx, const char *title, int w, int h)
{
    (void)ctx; (void)w; (void)h;
    snprintf(title_seen, sizeof title_seen, "%s", title);
    return 1;
}

/** @brief The headless host accepts the frame. */
static int window_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx; (void)id; (void)px; (void)w; (void)h;
    return 0;
}

/** @brief The headless host accepts the application's menus. */
static int window_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    (void)ctx; (void)id; (void)set;
    return 0;
}

/** @brief A launch argument loads its contents, never a remembered document. */
int main(void)
{
    char dir[] = "/tmp/tiku-editor-document-XXXXXX", path[512];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/notes literal;name.TXT", dir);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("Sensor notes\nSecond line\n", f) >= 0);
    assert(!fclose(f));
    tiku_app_services_t services = {.open = window_open, .frame = window_frame,
        .menus = window_menus, .startup_document = path};
    void *state = NULL;
    assert(!edit_start(&state, &services));
    edit_state_t *st = state;
    assert(!strcmp(st->path, path));
    assert(!strcmp(title_seen, "notes literal;name.TXT"));
    assert(!strcmp(tiku_textview_line(&st->tv, 0), "Sensor notes"));
    assert(!tiku_textview_modified(&st->tv) && !st->running);
    edit_stop(state);
    services.startup_document = NULL;
    assert(!edit_start(&state, &services));
    st = state;
    assert(!st->path[0] && !strcmp(title_seen, "Edit"));
    edit_stop(state);
    assert(!unlink(path) && !rmdir(dir));
    puts("PASS: Editor document launch, contents, literal paths and blank launch");
    return 0;
}
