/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_notes_resources.c - owner-thread note documents and autosave settings.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_notes_private.h"
#include "tiku_document_resources.h"
/** @brief Start the native immutable note save without bypassing its worker. */
static tiku_resource_error_t save_native(void *state)
{
    note_t *note = state;
    if (note->save)
        return TIKU_RESOURCE_BUSY;
    return note_save_start(note) < 0 ? TIKU_RESOURCE_APPLY_FAILED : 0;
}
/** @brief Borrow one note's native document and independent view. */
static dr_document_t document(note_t *note)
{
    return (dr_document_t){.doc = note->doc,
                           .view = note->view,
                           .path = note->path,
                           .type = "text/plain; charset=utf-8",
                           .window = note->id,
                           .dirty = note_dirty(note),
                           .busy = note->closing,
                           .revision = note->metadata,
                           .context = note,
                           .save = save_native};
}
/** @brief Detect additions and removals without inspecting serialized model memory. */
static uint64_t revision(void *state)
{
    notes_t *app = state;
    uint64_t value = 1;
    for (size_t i = 0; i < NOTES_MAX; i++)
        value =
            value * 31 +
            (app->note[i].used ? tiku_richdoc_identity(app->note[i].doc) : 0);
    return value ? value : 1;
}
/** @brief Enumerate each live note with separate document identities. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    notes_t *app = state;
    size_t count = 8 + tiku_richcmd_count() + 6;
    for (size_t i = 0; i < NOTES_MAX; i++)
        if (app->note[i].used) {
            if (index < count) {
                note_t *note = &app->note[i];
                dr_document_t d = document(note);
                size_t base = count - 2;
                if (index < base)
                    return dr_catalog(owner, &d, index, out);
                const char *id = tiku_app_resources_document(
                    owner, tiku_richdoc_identity(note->doc));
                const char *path = tiku_app_resources_path(
                    owner, "documents/%s/view/%s", id,
                    index == base ? "important" : "paper");
                int result = tiku_app_resource_initial(
                    out, path, TIKU_RESOURCE_SETTING,
                    index == base ? TIKU_RESOURCE_BOOLEAN
                                  : TIKU_RESOURCE_INTEGER,
                    tiku_resource_value_integer(
                        index == base ? TIKU_RESOURCE_BOOLEAN
                                      : TIKU_RESOURCE_INTEGER,
                        index == base ? note->important : note->paper));
                out->spec.schema.integer_max = index == base ? 1 : NPAPERS - 1;
                return result;
            }
            index -= count;
        }
    return 0;
}
/** @brief Find the live owner of a relative document path. */
static note_t *find(notes_t *app, tiku_app_resources_t *owner,
                    const char *path)
{
    for (size_t i = 0; i < NOTES_MAX; i++)
        if (app->note[i].used) {
            dr_document_t d = document(&app->note[i]);
            if (dr_suffix(owner, &d, path))
                return &app->note[i];
        }
    return NULL;
}
/** @brief Match document-local note metadata retained by native autosave. */
static int view_key(tiku_app_resources_t *owner, const dr_document_t *d,
                    const char *path)
{
    const char *name = dr_suffix(owner, d, path);
    return !name                             ? -1
           : !strcmp(name, "view/important") ? 0
           : !strcmp(name, "view/paper")     ? 1
                                             : -1;
}
/** @brief Capture current note facts through the owning native document. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    note_t *note = find(state, owner, path);
    if (!note)
        return TIKU_RESOURCE_UNSUPPORTED;
    dr_document_t d = document(note);
    int key = view_key(owner, &d, path);
    if (key >= 0)
        return tiku_app_resource_snapshot(
                   out,
                   tiku_resource_value_integer(
                       key ? TIKU_RESOURCE_INTEGER : TIKU_RESOURCE_BOOLEAN,
                       key ? note->paper : note->important),
                   "observed")
                   ? TIKU_RESOURCE_NO_MEMORY
                   : 0;
    return dr_capture(owner, &d, path, out);
}
/** @brief Run a context-checked native edit and retain its ordered autosave. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    notes_t *app = state;
    note_t *note = find(app, owner, path);
    if (!note)
        return TIKU_RESOURCE_UNSUPPORTED;
    dr_document_t d = document(note);
    int key = view_key(owner, &d, path);
    if (key >= 0) {
        if (invoke)
            return TIKU_RESOURCE_UNSUPPORTED;
        int64_t n;
        const char *type = tiku_msg_find_string(value, "type", 0);
        if (!type || strcmp(type, key ? "integer" : "boolean") ||
            !tiku_app_resource_scalar(
                value, key ? TIKU_RESOURCE_INTEGER : TIKU_RESOURCE_BOOLEAN,
                &n) ||
            n < 0 || n > (key ? NPAPERS - 1 : 1))
            return TIKU_RESOURCE_VALUE;
        int *field = key ? &note->paper : &note->important;
        if (*field != (int)n) {
            *field = n;
            note->metadata++;
            note->closing = 0;
            app->quitting = 0;
            (void)note_save_start(note);
            note_refresh(app, note);
        }
        *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
        return capture(state, owner, path, out);
    }
    uint64_t before=tiku_richdoc_generation(note->doc);
    tiku_resource_error_t error =
        dr_change(owner, &d, path, value, invoke, out, effect);
    if(!error&&before!=tiku_richdoc_generation(note->doc))note->edited_at=0;
    if (!error && *effect != TIKU_RESOURCE_EFFECT_NONE) {
        note->closing = 0;
        note->reveal = 1;
        note_refresh(app, note);
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
