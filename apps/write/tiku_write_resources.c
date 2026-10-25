/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_write_resources.c - application-owned document and view resources.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_document_resources.h"
/** @brief Queue the existing version-checked native snapshot save. */
static tiku_resource_error_t save_native(void *state)
{
    write_app_t *app = state;
    if (app->job)
        return TIKU_RESOURCE_BUSY;
    if (!app->path[0] || app->imported)
        return TIKU_RESOURCE_UNSUPPORTED;
    return write_job_start(app, W_JOB_SAVE, app->path, &app->version, 0)
               ? TIKU_RESOURCE_APPLY_FAILED
               : 0;
}
/** @brief Dispatch through the application's active native formatting target. */
static tiku_rich_status_t execute_native(void *state, uint64_t generation,
                                         const tiku_richcmd_t *command)
{
    write_app_t *app = state;
    uint64_t before = tiku_richdoc_generation(app->doc);
    tiku_rich_status_t status =
        tiku_richview_execute(app->view, generation, command);
    write_changed(app, before);
    app->repaint = 1;
    return status;
}
/** @brief Borrow live public document facts; private jobs remain application-owned. */
static dr_document_t document(write_app_t *app)
{
    return (dr_document_t){.doc = app->doc,
                           .view = app->view,
                           .path = app->path,
                           .type = "application/x-tiku-rich-document",
                           .window = app->id,
                           .dirty =
                               tiku_richdoc_dirty(app->doc) || app->imported,
                           .busy = app->job != NULL,
                           .read_only = app->preview || app->prompt != 0,
                           .revision = 0,
                           .context = app,
                           .execute = execute_native,
                           .save = save_native};
}
static const char *const view_keys[] = {"preview", "preview_page",
                                        "show_ruler", "zoom"};
/** @brief Match one document-local view option. */
static int view_key(tiku_app_resources_t *owner, const dr_document_t *d,
                    const char *path)
{
    const char *name = dr_suffix(owner, d, path);
    if (!name || strncmp(name, "view/", 5))
        return -1;
    for (int i = 0; i < 4; i++)
        if (!strcmp(name + 5, view_keys[i]))
            return i;
    return -1;
}
/** @brief Capture native view options; document dirty state is reported separately. */
static tiku_msg_t *view_value(write_app_t *app, int key)
{
    if (key == 3)
        return tiku_app_resource_number_value(app->zoom);
    return tiku_resource_value_integer(key == 1 ? TIKU_RESOURCE_INTEGER
                                                : TIKU_RESOURCE_BOOLEAN,
                                       key == 0   ? app->preview
                                       : key == 1 ? (int64_t)app->preview_page
                                                  : app->show_ruler);
}
/** @brief Native document catalog changes when a new source is installed. */
static uint64_t revision(void *state)
{
    return tiku_richdoc_identity(((write_app_t *)state)->doc);
}
/** @brief Enumerate native session surfaces and their existing command catalog. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    write_app_t *app = state;
    dr_document_t d = document(app);
    size_t base = 8 + tiku_richcmd_count() + 4;
    if (index < base)
        return dr_catalog(owner, &d, index, out);
    index -= base;
    if (index >= 4)
        return 0;
    const char *id =
        tiku_app_resources_document(owner, tiku_richdoc_identity(app->doc));
    const char *path = tiku_app_resources_path(owner, "documents/%s/view/%s",
                                               id, view_keys[index]);
    int result =
        tiku_app_resource_initial(out, path, TIKU_RESOURCE_SETTING,
                                  index == 3   ? TIKU_RESOURCE_NUMBER
                                  : index == 1 ? TIKU_RESOURCE_INTEGER
                                               : TIKU_RESOURCE_BOOLEAN,
                                  view_value(app, index));
    if (index == 1)
        out->spec.schema.integer_max = INT32_MAX;
    if (index == 3) {
        out->spec.schema.number_min = 0.5;
        out->spec.schema.number_max = 2.5;
    }
    return result;
}
/** @brief Capture public session state from the active native controller. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    write_app_t *app = state;
    dr_document_t d = document(app);
    int key = view_key(owner, &d, path);
    if (key >= 0)
        return tiku_app_resource_snapshot(out, view_value(app, key),
                                          "observed")
                   ? TIKU_RESOURCE_NO_MEMORY
                   : 0;
    return dr_capture(owner, &d, path, out);
}
/** @brief Execute a document command with native identity and selection checks. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    write_app_t *app = state;
    dr_document_t d = document(app);
    int key = view_key(owner, &d, path);
    if (key >= 0) {
        if (invoke || app->prompt || app->job)
            return TIKU_RESOURCE_BUSY;
        int64_t n = 0;
        const char *type = tiku_msg_find_string(value, "type", 0);
        if (key == 3) {
            double zoom;
            if (!type || strcmp(type, "number") ||
                !tiku_msg_find_double(value, "value", 0, &zoom) ||
                !isfinite(zoom) || zoom < 0.5 || zoom > 2.5 ||
                fabs(zoom * 4 - round(zoom * 4)) > 0.000001)
                return TIKU_RESOURCE_VALUE;
            while (fabs(app->zoom - zoom) > 0.000001) {
                double before = app->zoom;
                write_action(app, app->zoom < zoom ? W_ZOOM_IN : W_ZOOM_OUT);
                if (before == app->zoom)
                    return TIKU_RESOURCE_APPLY_FAILED;
            }
        } else {
            if (!type || strcmp(type, key == 1 ? "integer" : "boolean") ||
                !tiku_app_resource_scalar(value,
                                          key == 1 ? TIKU_RESOURCE_INTEGER
                                                   : TIKU_RESOURCE_BOOLEAN,
                                          &n) ||
                n < 0 || (key != 1 && n > 1))
                return TIKU_RESOURCE_VALUE;
            if (key == 0 && n != app->preview)
                write_action(app, W_PREVIEW);
            if (key == 2 && n != app->show_ruler)
                write_action(app, W_RULER);
            if (key == 1) {
                if (!app->preview || !app->pages ||
                    n >= (int64_t)tiku_richpages_count(app->pages))
                    return TIKU_RESOURCE_VALUE;
                while (app->preview_page != (size_t)n) {
                    size_t before = app->preview_page;
                    write_action(app, app->preview_page < (size_t)n
                                          ? W_NEXT_PAGE
                                          : W_PREV_PAGE);
                    if (before == app->preview_page)
                        return TIKU_RESOURCE_APPLY_FAILED;
                }
            }
        }
        app->repaint = 1;
        *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
        return tiku_app_resource_snapshot(out, view_value(app, key),
                                          "native_complete")
                   ? TIKU_RESOURCE_NO_MEMORY
                   : 0;
    }
    tiku_resource_error_t error =
        dr_change(owner, &d, path, value, invoke, out, effect);
    if (!error) {
        app->repaint = app->reveal = 1;
    }
    return error;
}
/** @brief Optional versioned native application resource callbacks. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog_revision = revision,
    .catalog = catalog,
    .capture = capture,
    .change = change};
