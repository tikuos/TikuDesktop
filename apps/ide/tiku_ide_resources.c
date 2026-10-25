/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ide_resources.c - retained native source-page observations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ide_private.h"
#include "tiku_viewer_resources.h"
#include "tiku_crypto.h"

static const char *const commands[] = {
    "undo",       "redo",      "copy",      "cut",          "paste",
    "select-all", "newline",   "backspace", "delete",       "indent",
    "unindent",   "comment",   "duplicate", "delete-lines", "fold",
    "unfold",     "line-ends", "keep-main", "move",         "save"};
/** @brief Hash the full native selection and the document lifetime for owner preconditions. */
static int context(edit_t *edit,char out[65])
{
 size_t main=0,count=tiku_codeview_selections(edit->view,NULL,0,&main);
 if(count>SIZE_MAX/sizeof(tiku_edit_selection_t))return 0;
 tiku_edit_selection_t *ranges = count ? malloc(count * sizeof *ranges) : NULL;
 if (count && !ranges)
     return 0;
 if (tiku_codeview_selections(edit->view, ranges, count, &main) != count) {
     free(ranges);
     return 0;
 }
 tiku_hash_t hash;
 unsigned char bytes[32];
 char row[160];
 tiku_hash_init(&hash, TIKU_HASH_SHA256);
 int n = snprintf(row, sizeof row, "%llu:%llu:%zu:%u:%u;",
                  (unsigned long long)tiku_editdoc_identity(edit->doc),
                  (unsigned long long)tiku_editdoc_generation(edit->doc), main,
                  tiku_codeview_doc_view(edit->view).slot,
                  tiku_codeview_doc_view(edit->view).generation);
 tiku_hash_update(&hash, row, n);
 for (size_t i = 0; i < count; i++) {
     n = snprintf(row, sizeof row, "%lld:%lld;", (long long)ranges[i].anchor,
                  (long long)ranges[i].caret);
     tiku_hash_update(&hash, row, n);
 }
 free(ranges);
 tiku_hash_final(&hash, bytes);
 for (size_t i = 0; i < 32; i++)
     snprintf(out + 2 * i, 3, "%02x", bytes[i]);
 return 1;
}

/** @brief Copy native source selection, viewport and history summaries. */
static tiku_resource_error_t observe(void *state, const char *field,
                                     tiku_msg_t **out)
{
    edit_t *edit = state;
    tiku_msg_t *value = tiku_app_resource_object(0);
    int bad = 0;
    if (!strcmp(field, "selection")) {
        tiku_edit_pos_t a = 0, b = 0;
        if (tiku_codeview_selection(edit->view, &a, &b)) {
            tiku_msg_free(value);
            return TIKU_RESOURCE_UNAVAILABLE;
        }
        char token[65];
        if (!context(edit, token)) {
            tiku_msg_free(value);
            return TIKU_RESOURCE_UNAVAILABLE;
        }
        bad |= VR_TEXT(value,"context",token);
        bad |= VR_INT(value, "anchor", a);
        bad |= VR_INT(value, "caret", b);
        bad |= VR_COUNTER(value, "generation",
                          tiku_editdoc_generation(edit->doc));
        bad |= VR_INT(value, "count",
                      tiku_codeview_selections(edit->view, NULL, 0, NULL));
    } else if (!strcmp(field, "view")) {
        bad |= VR_INT(value, "zoom", tiku_codeview_zoom(edit->view));
        bad |= VR_INT(value, "options", tiku_codeview_options(edit->view));
        bad |= VR_BOOL(value, "debug_panel", edit->debug_show);
        bad |= VR_BOOL(value, "find", edit->finding);
    } else if (!strcmp(field, "objects")) {
        bad |= VR_INT(value, "bytes", tiku_editdoc_length(edit->doc));
        bad |= VR_TEXT(value, "scope", "native_source_page");
    } else {
        tiku_msg_free(value);
        return TIKU_RESOURCE_UNSUPPORTED;
    }
    if (bad) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    *out = value;
    return 0;
}
/** @brief Borrow one retained source page without depending on board services. */
static vr_document_t document(edit_t *edit)
{
    return (vr_document_t){.identity = tiku_editdoc_identity(edit->doc),
                           .generation = tiku_editdoc_generation(edit->doc),
                           .window = edit->id,
                           .path = edit->path,
                           .type = "text/plain; charset=utf-8",
                           .available = 1,
                           .dirty = tiku_editdoc_modified(edit->doc),
                           .read_only = tiku_editdoc_read_only(edit->doc),
                           .controller = edit,
                           .observe = observe};
}
/** @brief Enumerate the live native source pages independently of project configuration. */
/** @brief Copy the native run/debug owner and its selected execution target. */
static tiku_msg_t *development(ide_t *app, size_t index)
{
    char target[64] = {0};
    if (app->running)
        snprintf(target, sizeof target, "%s", app->board);
    else if (app->services)
        (void)tiku_ide_dest_board(app, target, sizeof target);
    if (index < 2)
        return tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                          index == 0 ? "debug" : target);
    tiku_msg_t *value = tiku_app_resource_object(0);
    int bad = VR_TEXT(value, "target", target) |
              VR_BOOL(value, "running", app->running) |
              VR_BOOL(value, "debugging", app->debugging) |
              VR_BOOL(value, "paused", app->debug_paused) |
              VR_INT(value, "pc", app->debug_pc) |
              VR_INT(value, "child_pid", app->child) |
              VR_BOOL(value, "saving", app->saving) |
              VR_TEXT(value, "status", app->target_status) |
              VR_TEXT(value, "commands", "commands/invoke") |
              VR_TEXT(value, "command_inventory", "menus") |
              VR_TEXT(value, "confirmation", "native_application_policy");
    bad |= VR_COUNTER(value, "source_state", app->debug_state);
    if (bad) {
        tiku_msg_free(value);
        return NULL;
    }
    return value;
}
static const char *const development_paths[] = {
    "development/kind", "development/target", "development/state"};
/** @brief Enumerate development observations and retained source-page controls. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    ide_t *app = state;
    if (index < 3)
        return tiku_app_resource_initial(
            out, development_paths[index], TIKU_RESOURCE_STATE,
            index < 2 ? TIKU_RESOURCE_TEXT : TIKU_RESOURCE_OBJECT,
            development(app, index));
    index -= 3;
    for (size_t i = 0; i < IDE_EDITS; i++)
        if (app->edit[i].surface && app->edit[i].doc) {
            if (index < 8+sizeof commands/sizeof commands[0]) {
                vr_document_t d = document(&app->edit[i]);
                if(index<8)return vr_catalog(owner, &d, index, out);
                const char *path = tiku_app_resources_path(
                    owner, "documents/%s/commands/%s",
                    tiku_app_resources_document(owner, d.identity),
                    commands[index - 8]);
                tiku_msg_t *value = tiku_app_resource_object(0);
                if (VR_TEXT(value, "required", "context")) {
                    tiku_msg_free(value);
                    return -1;
                }
                return tiku_app_resource_initial(out, path,
                                                 TIKU_RESOURCE_ACTION,
                                                 TIKU_RESOURCE_OBJECT, value);
            }
            index -= 8+sizeof commands/sizeof commands[0];
        }
    return 0;
}
/** @brief Capture the source page whose native lifetime matches the requested path. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    ide_t *app = state;
    for (size_t i = 0; i < 3; i++)
        if (!strcmp(path, development_paths[i]))
            return tiku_app_resource_snapshot(out, development(app, i),
                                              "native_development_owner")
                       ? TIKU_RESOURCE_NO_MEMORY
                       : 0;
    for (size_t i = 0; i < IDE_EDITS; i++)
        if (app->edit[i].surface && app->edit[i].doc) {
            vr_document_t d = document(&app->edit[i]);
            const char *suffix=vr_suffix(owner,&d,path);
            if(suffix){
              if(!strncmp(suffix,"commands/",9)){
                  for (size_t j = 0; j < sizeof commands / sizeof commands[0];
                       j++)
                      if (!strcmp(suffix + 9, commands[j])) {
                          tiku_msg_t *v = tiku_app_resource_object(0);
                          if (VR_TEXT(v, "required", "context")) {
                              tiku_msg_free(v);
                              return TIKU_RESOURCE_NO_MEMORY;
                          }
                          return tiku_app_resource_snapshot(out, v,
                                                            "native_command")
                                     ? TIKU_RESOURCE_NO_MEMORY
                                     : 0;
                      }
              }
              return vr_capture(owner,&d,path,out);
            }
        }
    return TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Dispatch guarded source edits and saves through the native view and file controller. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
 ide_t *app=state;if(!invoke)return TIKU_RESOURCE_UNSUPPORTED;
 for(size_t i=0;i<IDE_EDITS;i++){
  edit_t *edit=&app->edit[i];if(!edit->surface||!edit->doc)continue;
  vr_document_t d = document(edit);
  const char *suffix = vr_suffix(owner, &d, path);
  if (!suffix || strncmp(suffix, "commands/", 9))
      continue;
  const char *name = suffix + 9;
  size_t j;
  for (j = 0; j < sizeof commands / sizeof commands[0]; j++)
      if (!strcmp(name, commands[j]))
          break;
  if (j == sizeof commands / sizeof commands[0])
      return TIKU_RESOURCE_UNSUPPORTED;
  if(app->asked||edit->finding||edit->assist_open)return TIKU_RESOURCE_BUSY;
  char expected[65], actual[65];
  if (!tiku_app_resource_text(value, "context", expected, sizeof expected))
      return TIKU_RESOURCE_VALUE;
  if (!context(edit, actual))
      return TIKU_RESOURCE_UNAVAILABLE;
  if (strcmp(actual, expected))
      return TIKU_RESOURCE_STALE;
  const char *plain[] = {"context"}, *moving[] = {"context", "move", "extend"};
  int64_t move = 0, extend = 0;
  if (j == 18) {
      if (!tiku_app_resource_keys(value, moving, 3) ||
          !tiku_app_resource_integer(value, "move", 0, TIKU_CODEVIEW_DOC_END,
                                     &move) ||
          !tiku_app_resource_integer(value, "extend", 0, 1, &extend))
          return TIKU_RESOURCE_VALUE;
  } else if (!tiku_app_resource_keys(value, plain, 1))
      return TIKU_RESOURCE_VALUE;
  tiku_edit_status_t result=TIKU_EDIT_OK;
  if (j == 19) {
      if (!edit->path[0])
          return TIKU_RESOURCE_UNSUPPORTED;
      if (!tiku_ide_edit_save(edit))
          return TIKU_RESOURCE_APPLY_FAILED;
  } else {
      tiku_codeview_command_t cmd = TIKU_CODEVIEW_CMD_UNDO + j;
      if (!tiku_codeview_command_enabled(edit->view, cmd))
          return tiku_editdoc_read_only(edit->doc) ? TIKU_RESOURCE_READ_ONLY
                                                   : TIKU_RESOURCE_UNSUPPORTED;
      result = tiku_codeview_execute(edit->view, cmd, move, extend);
  }
  if (result != TIKU_EDIT_OK && result != TIKU_EDIT_NOTHING)
      return result == TIKU_EDIT_READ_ONLY ? TIKU_RESOURCE_READ_ONLY
             : result == TIKU_EDIT_STALE   ? TIKU_RESOURCE_STALE
                                           : TIKU_RESOURCE_VALUE;
  *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
  tiku_ide_edit_reveal(edit);
  tiku_ide_edit_publish(app, edit);
  tiku_ide_edit_paint(app, edit);
  return capture(state,owner,path,out);
 }
 return TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Detect source-page additions, closure and successful replacement. */
static uint64_t revision(void *state)
{
    ide_t *app = state;
    uint64_t value = 1;
    for (size_t i = 0; i < IDE_EDITS; i++)
        value = value * 31 + (app->edit[i].surface && app->edit[i].doc
                                  ? tiku_editdoc_identity(app->edit[i].doc)
                                  : 0);
    return value ? value : 1;
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
