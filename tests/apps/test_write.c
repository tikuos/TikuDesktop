/*
 * TikuDesktop application tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_write.c - document application, file safety and recovery workflows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"
#include "tiku_pdf.h"
#include "tiku_sys.h"
#include "tiku_host.h"
#include "tiku_msg.h"
#include "tiku_clip_rpc.h"
#include "tiku_clipboard.h"
#include "tiku_richclip.h"
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>

static int fail_surface;
/** @brief Refuse frame allocation without changing the existing surface. */
static tiku_surface_t *
checked_surface(int w, int h, tiku_rgb_t color)
{
    return fail_surface ? NULL : tiku_surface_new(w, h, color);
}
#define tiku_surface_new checked_surface
#include "../../apps/write/tiku_write.c"
#undef tiku_surface_new

static unsigned next_window = 1;
static int frames, closes, fail_clip;
static int cursor_shape;
static char clip[4096];
static char last_title[256];
static char picker_directory[WRITE_PATH];
static int typed_clip;
static tiku_msg_t *clip_request;

/** @brief Allocate a fake window identity. */
static uint32_t
open_window(void *ctx, const char *title, int w, int h)
{
    (void)ctx;
    assert(title && w >= 320 && h >= 240);
    return next_window++;
}
/** @brief Validate published full frames. */
static int
frame(void *ctx, uint32_t id, const uint32_t *pixels, int w, int h)
{
    (void)ctx;
    assert(id && pixels && w >= 320 && h >= 240);
    frames++;
    return 1;
}
/** @brief Require resizable and zoomable document windows. */
static int
configure(void *ctx, uint32_t id, const char *title,
          int w, int h, int r, int z)
{
    (void)ctx; (void)w; (void)h;
    assert(id && r && z);
    snprintf(last_title, sizeof last_title, "%s", title);
    return 1;
}
/** @brief Check the published menu categories. */
static int
publish(void *ctx, uint32_t id, const tiku_menuset_t *menus)
{
    (void)ctx;
    assert(id && menus->nmenu == 6);
    assert(!strcmp(menus->menu[5].title, "Table"));
    for (int i = 0; i < menus->nmenu; i++)
        assert(menus->menu[i].nitem <= TIKU_MENUSET_ITEMS);
    assert(!strcmp(menus->menu[0].title, "File"));
    return 1;
}
/** @brief Record a requested close. */
static void
close_window(void *ctx, uint32_t id)
{
    (void)ctx;
    assert(id);
    closes++;
}
/** @brief Accept or reject a clipboard publication. */
static int
clip_set(void *ctx, const char *text)
{
    (void)ctx;
    if (fail_clip) return 0;
    assert(strlen(text) < sizeof clip);
    strcpy(clip, text);
    return 1;
}
/** @brief Return the test clipboard. */
static const char *
clip_text(void *ctx, unsigned *stamp)
{
    (void)ctx;
    if (stamp) *stamp = 1;
    return clip;
}
/** @brief Accept host picker requests without choosing a file. */
static int
pick_file(void *ctx, uint32_t id, int saving, const char *dir, const char *name)
{
    (void)ctx; (void)saving; (void)name;
    snprintf(picker_directory, sizeof picker_directory, "%s", dir ? dir : "");
    assert(id);
    assert(saving != TIKU_APP_PICK_LOCAL_OPEN || !name);
    return 1;
}

/** @brief Accept cursor messages and retain the requested shape. */
static int
send_message(void *ctx, const struct tiku_msg *message)
{
    int32_t shape;

    (void)ctx;
    if (typed_clip && tiku_msg_what(message) == TIKU_MSG_CLIP_REQUEST) {
        assert(!clip_request);
        clip_request = tiku_msg_copy(message);
        assert(clip_request);
    }
    if (tiku_msg_find_int32(message, "shape", 0, &shape)) cursor_shape = shape;
    return 1;
}
static const tiku_app_services_t services = {
    .open = open_window, .frame = frame, .configure = configure,
    .menus = publish, .close = close_window, .clip_set = clip_set,
    .clip_text = clip_text, .pick = pick_file, .send_message = send_message
};

/** @brief Run layout and queued input to completion with a finite cap. */
static void
settle(write_app_t *app)
{
    unsigned count = 0;

    while (tiku_richview_status(app->view) == TIKU_RICH_PENDING ||
           app->qcount) {
        write_step(app);
        assert(++count < 100000);
    }
    assert(tiku_richview_status(app->view) == TIKU_RICH_OK);
}
/** @brief Copy and compare complete document text. */
static void
expect(write_app_t *app, const char *text)
{
    tiku_richsnapshot_t *snapshot;
    char buffer[8192];
    size_t length;

    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_text(snapshot, buffer, sizeof buffer, &length) ==
           TIKU_RICH_OK);
    assert(!strcmp(buffer, text));
    tiku_richsnapshot_release(snapshot);
}
/** @brief Replace the complete document through its public session. */
static void
replace_document(write_app_t *app, const char *text)
{
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_REPLACE_TEXT};
    uint64_t before = tiku_richdoc_generation(app->doc);

    settle(app);
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    cmd.value.text.data = text;
    cmd.value.text.length = strlen(text);
    assert(tiku_richsession_execute(tiku_richview_session(app->view),
        0, &cmd) == TIKU_RICH_OK);
    write_changed(app, before);
    settle(app);
}
/** @brief Read a saved native package independently of the active app. */
static void
saved_text(const char *path, const char *expected)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    char text[8192];
    size_t length;

    assert(tiku_richio_load(path, NULL, &doc) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_text(snapshot, text, sizeof text, &length) ==
           TIKU_RICH_OK);
    assert(!strcmp(text, expected));
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}
/** @brief Write an external replacement for conflict tests. */
static void
external_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");

    assert(file && fputs(text, file) >= 0 && !fclose(file));
}
/** @brief Exercise rich formatting, clipboard failures and atomic replacement. */
static void
editing(write_app_t *app)
{
    tiku_rich_selection_t formatting;
    uint64_t generation;

    replace_document(app, "hello world hello");
    write_action(app, W_ALL);
    write_action(app, W_FORMAT + TIKU_RICHCMD_BOLD);
    assert(tiku_richsession_format(tiku_richview_session(app->view),
        &formatting) == TIKU_RICH_OK && formatting.chars.bold);
    settle(app);
    generation = tiku_richdoc_generation(app->doc);
    fail_clip = 1;
    write_action(app, W_CUT);
    assert(tiku_richdoc_generation(app->doc) == generation);
    expect(app, "hello world hello");
    fail_clip = 0;
    write_action(app, W_COPY);
    assert(!strcmp(clip, "hello world hello"));
    write_action(app, W_CUT);
    expect(app, "");
    settle(app);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    expect(app, "hello world hello");
    settle(app);
    write_action(app, W_FIND);
    tiku_field_start(&app->fields[0], "hello");
    tiku_field_start(&app->fields[1], "greetings");
    write_search_start(app, 1);
    for (unsigned i = 0; app->search; i++) {
        assert(i < 100);
        write_search_step(app);
    }
    expect(app, "greetings world greetings");
    settle(app);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    expect(app, "hello world hello");
    settle(app);
    write_action(app, W_CANCEL);
}

/** @brief Create, apply and update named styles through visible actions. */
static void
styles(write_app_t *app)
{
    tiku_rich_selection_t selection;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_style_t style;
    uint64_t generation;

    replace_document(app, "Reusable style");
    write_action(app, W_ALL);
    write_action(app, W_HEADING);
    write_action(app, W_STYLE_DEFINE);
    tiku_field_start(&app->fields[0], "Report heading");
    write_action(app, W_APPLY);
    assert(!app->panel);
    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_styles(snapshot) == 1);
    assert(tiku_richsnapshot_style(snapshot, 0, &style) == TIKU_RICH_OK);
    assert(!strcmp(style.name, "Report heading") && style.chars.size == 18);
    tiku_richsnapshot_release(snapshot);
    write_action(app, W_STYLE_DEFINE);
    tiku_field_start(&app->fields[0], "Report heading");
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_APPLY);
    assert(app->panel && generation == tiku_richdoc_generation(app->doc));
    write_action(app, W_CANCEL);
    write_action(app, W_STYLE_CLEAR);
    write_action(app, W_FORMAT + TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES);
    write_action(app, W_STYLE_APPLY);
    tiku_field_start(&app->fields[0], "Report heading");
    write_action(app, W_APPLY);
    assert(!app->panel);
    assert(tiku_richsession_format(tiku_richview_session(app->view),
        &selection) == TIKU_RICH_OK);
    assert(selection.paragraph_style == style.id && selection.chars.size == 18);
    write_action(app, W_SIZE);
    tiku_field_start(&app->fields[0], "24");
    write_action(app, W_APPLY);
    write_action(app, W_STYLE_UPDATE);
    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_style(snapshot, 0, &style) == TIKU_RICH_OK);
    assert(style.chars.size == 24);
    tiku_richsnapshot_release(snapshot);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_style(snapshot, 0, &style) == TIKU_RICH_OK);
    assert(style.chars.size == 18);
    tiku_richsnapshot_release(snapshot);
}
/** @brief Publish many styles without exceeding the menu transport capacity. */
static void
many_styles(write_app_t *app)
{
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_STYLE_DEFINE};

    for (unsigned i = 0; i < 40; i++) {
        snprintf(command.value.definition.value.name,
            sizeof command.value.definition.value.name, "Extra style %u", i);
        assert(tiku_richsession_execute(tiku_richview_session(app->view),
            0, &command) == TIKU_RICH_OK);
    }
    settle(app);
    write_paint(app);
    assert(app->menu_styles[0]);
    assert(!app->menu_styles[TIKU_MENUSET_ITEMS - 1]);
}

/** @brief Save a captured state, preserve newer edits and refuse stale versions. */
static void
file_safety(write_app_t *app, const char *path)
{
    write_version_t absent = {0}, version;
    struct stat st;

    replace_document(app, "captured");
    assert(!write_job_start(app, W_JOB_SAVE, path, &absent, 0));
    replace_document(app, "newer");
    assert(write_job_finish(app, 1) == 1);
    assert(write_dirty(app));
    saved_text(path, "captured");
    assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    write_action(app, W_SAVE);
    assert(app->job && write_job_finish(app, 1) == 1);
    assert(!write_dirty(app));
    saved_text(path, "newer");
    replace_document(app, "local");
    external_text(path, "external");
    write_action(app, W_SAVE);
    assert(app->job && write_job_finish(app, 1) == -1);
    assert(app->prompt == W_PROMPT_CONFLICT && write_dirty(app));
    assert(!write_version(path, &version) && version.stat.st_size == 8);
    expect(app, "local");
    write_action(app, W_CANCEL);
    assert(!app->prompt);
    assert(!unlink(path));
    assert(!write_job_start(app, W_JOB_SAVE, path, &absent, 0));
    assert(write_job_finish(app, 1) == 1);
    assert(!chmod(path, 0400));
    assert(!write_version(path, &app->version));
    replace_document(app, "readonly");
    write_action(app, W_SAVE);
    assert(app->job && write_job_finish(app, 1) == -1);
    saved_text(path, "local");
    write_action(app, W_CANCEL);
    assert(!chmod(path, 0600));
}

/** @brief Finish async picker probes and their automatic save continuations. */
static void
file_jobs(write_app_t *app)
{
    const struct timespec pause = {.tv_nsec = 1000000};

    for (unsigned i = 0; app->job; i++) {
        assert(i < 10000);
        write_job_finish(app, 0);
        if (app->job) nanosleep(&pause, NULL);
    }
}

/** @brief Verify paper controls, read-only preview, guarded PDF and Save As. */
static void
page_workflow(write_app_t *app, const char *home)
{
    tiku_rich_selection_t selection;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_page_setup_t paper;
    tiku_pdf_t *pdf;
    char path[WRITE_PATH];
    uint64_t generation;

    replace_document(app, "First page\nSecond page");
    write_action(app, W_PAGE_LETTER);
    write_action(app, W_LANDSCAPE);
    write_action(app, W_MARGINS);
    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_page(snapshot, &paper) == TIKU_RICH_OK);
    assert(paper.width == 792 && paper.height == 612 && paper.left == 36);
    tiku_richsnapshot_release(snapshot);
    write_action(app, W_INDENT);
    write_action(app, W_FIRST_INDENT);
    write_action(app, W_KEEP_LINES);
    write_action(app, W_PAGE_BREAK);
    assert(tiku_richsession_format(tiku_richview_session(app->view),
        &selection) == TIKU_RICH_OK);
    assert(selection.props.left == 18 && selection.props.first == 18 &&
           selection.props.keep_lines && selection.props.break_before);
    snprintf(app->status, sizeof app->status, "Opened document.");
    write_action(app, W_PREVIEW);
    assert(!*app->status);
    for (unsigned i = 0;
         tiku_richpages_status(app->pages) == TIKU_RICH_PENDING; i++) {
        assert(i < 10000);
        write_pages_step(app);
    }
    assert(tiku_richpages_count(app->pages) == 2);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_FORMAT + TIKU_RICHCMD_BOLD);
    assert(generation == tiku_richdoc_generation(app->doc));
    write_action(app, W_NEXT_PAGE);
    assert(app->preview_page == 1);
    write_action(app, W_NEXT_PAGE);
    assert(app->preview_page == 1);
    write_paint(app);
    snprintf(path, sizeof path, "%s/export.pdf", home);
    write_action(app, W_EXPORT_PDF);
    write_picked(app, app->id, path);
    file_jobs(app);
    assert(!app->prompt && write_dirty(app));
    assert(tiku_pdf_open(path, &pdf) == TIKU_PDF_OK);
    assert(tiku_pdf_pages(pdf) == 2 && !tiku_pdf_repaired(pdf));
    tiku_pdf_close(pdf);
    write_action(app, W_PREVIEW);
    snprintf(path, sizeof path, "%s/new-native.tkrd", home);
    write_action(app, W_SAVEAS);
    write_picked(app, app->id, path);
    file_jobs(app);
    assert(!write_dirty(app) && !strcmp(app->path, path));
    saved_text(path, "First page\nSecond page");
    write_action(app, W_SAVEAS);
    write_picked(app, app->id, path);
    file_jobs(app);
    assert(app->prompt == W_PROMPT_OVERWRITE);
    external_text(path, "changed after confirmation was offered");
    write_action(app, W_CONFIRM);
    file_jobs(app);
    assert(app->prompt == W_PROMPT_CONFLICT);
    write_action(app, W_CANCEL);
    replace_document(app, "after page tests");
}
/** @brief Refuse obsolete picker and loader results without losing edits. */
static void
stale_open(write_app_t *app, const char *path)
{
    uint64_t generation;

    replace_document(app, "keep");
    write_action(app, W_OPEN);
    assert(app->prompt == W_PROMPT_UNSAVED);
    write_action(app, W_DISCARD);
    assert(app->pick_kind == W_OPEN);
    replace_document(app, "keep newer");
    write_picked(app, app->id, path);
    assert(!app->job);
    expect(app, "keep newer");
    generation = tiku_richdoc_generation(app->doc);
    assert(!write_job_start(app, W_JOB_LOAD, path, NULL, 0));
    replace_document(app, "edited during load");
    assert(tiku_richdoc_generation(app->doc) != generation);
    write_job_finish(app, 1);
    expect(app, "edited during load");
}
/** @brief A forced stop checkpoints privately; a fresh launch stays empty. */
static write_app_t *
recovery(write_app_t *app)
{
    char path[WRITE_PATH];
    write_app_t *next = NULL;
    struct stat st;

    replace_document(app, "recover this draft");
    strcpy(path, app->recovery);
    assert(*path);
    stop(app);
    assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    saved_text(path, "recover this draft");
    assert(!start((void **)&next, &services));
    settle(next);
    expect(next, "");
    assert(!strcmp(next->offered, path));
    write_action(next, W_RECOVER);
    assert(next->job);
    write_job_finish(next, 1);
    settle(next);
    expect(next, "recover this draft");
    assert(!*next->path && next->imported && write_dirty(next));
    return next;
}
/** @brief Verify resize rollback, focus, maximal windows and close questions. */
static void
windows(write_app_t *app)
{
    tiku_surface_t *old = app->surface;
    int width = app->width, height = app->height;
    uint32_t id = app->id;

    replace_document(app, "Unsaved window test");
    fail_surface = 1;
    resize(app, 1000, 700);
    assert(app->surface == old && app->width == width && app->height == height);
    fail_surface = 0;
    for (int i = 0; i < 16; i++) {
        resize(app, i & 1 ? 820 : 1100, i & 1 ? 600 : 700);
        settle(app);
        write_paint(app);
    }
    resize(app, 320, 320);
    write_action(app, W_FIND);
    write_controls(app);
    for (int i = 0; i < app->dialog.count; i++) {
        tiku_rect_t r = app->dialog.control[i].rect;

        assert(r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0);
        assert(r.x + r.w <= 320 && r.y + r.h <= 320);
    }
    write_paint(app);
    write_action(app, W_CANCEL);
    resize(app, 820, 600);
    closed(app, id);
    assert(app->id != id && app->prompt == W_PROMPT_UNSAVED);
    write_action(app, W_CANCEL);
    assert(!app->prompt && !app->closing);
    write_action(app, W_CLOSE);
    assert(app->prompt == W_PROMPT_UNSAVED);
    write_action(app, W_DISCARD);
    assert(app->closing && !app->id && closes);
}
/** @brief File versions detect subsecond timestamps even with identical bytes. */
static void
file_versions(const char *home)
{
    char path[WRITE_PATH];
    write_version_t before, after;
    struct timespec times[2] = {{1000000000, 100}, {1000000000, 100}};
    int fd;

    snprintf(path, sizeof path, "%s/version.txt", home);
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, "same", 4) == 4);
    assert(!futimens(fd, times));
    assert(!write_version(path, &before));
    assert(tiku_sys_stat_same(&before.stat, &before.stat));
    assert(!tiku_sys_stat_same(NULL, &before.stat));
    times[1].tv_nsec = 900000000;
    assert(!futimens(fd, times));
    assert(!write_version(path, &after));
    assert(!memcmp(before.hash, after.hash, sizeof before.hash));
    assert(!tiku_sys_stat_same(&before.stat, &after.stat));
    assert(!write_version_equal(&before, &after));
    assert(!close(fd) && !unlink(path));
}

/** @brief Compare selected picture metadata without retaining borrowed data. */
static void
picture_check(write_app_t *app, float width, const char *alt)
{
    tiku_richsnapshot_t *s;
    tiku_rich_paragraph_t p;

    assert(tiku_richdoc_snapshot(app->doc, &s) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    assert(p.run_count == 1 && p.runs[0].image);
    assert(p.runs[0].image->width == width);
    assert(p.runs[0].image->height == width);
    assert(!strcmp(p.runs[0].image->alt, alt));
    tiku_richsnapshot_release(s);
}

/** @brief Insert, resize, describe, undo, save and reopen an embedded image. */
static void
pictures(write_app_t *app, const char *home)
{
    const uint32_t pixel = 0xffff0000;
    char path[WRITE_PATH], saved[WRITE_PATH];
    uint64_t generation;

    snprintf(path, sizeof path, "%s/picture.png", home);
    snprintf(saved, sizeof saved, "%s/pictures.tkrd", home);
    assert(!tiku_png_write(path, &pixel, 1, 1, 1));
    replace_document(app, "");
    write_action(app, W_PICTURE_INSERT);
    assert(app->pick_kind == W_PICTURE_INSERT);
    write_picked(app, app->id, path);
    assert(app->job && app->job->kind == W_JOB_IMAGE);
    file_jobs(app);
    settle(app);
    expect(app, "\xef\xbf\xbc");
    picture_check(app, 1, "");
    write_action(app, W_ALL);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_CUT);
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(strstr(app->status, "clipboard cannot carry pictures"));
    write_action(app, W_PICTURE_SIZE);
    tiku_field_start(&app->fields[0], "80");
    write_action(app, W_APPLY);
    settle(app);
    picture_check(app, 80, "");
    write_action(app, W_PICTURE_ALT);
    tiku_field_start(&app->fields[0], "Red camera sample");
    write_action(app, W_APPLY);
    picture_check(app, 80, "Red camera sample");
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    picture_check(app, 80, "");
    write_action(app, W_FORMAT + TIKU_RICHCMD_REDO);
    picture_check(app, 80, "Red camera sample");
    settle(app);
    write_paint(app);
    assert(!tiku_png_write("/tmp/tiku-write-picture.png", app->surface->px,
        app->surface->w, app->surface->h, 0));
    write_action(app, W_SAVEAS);
    write_picked(app, app->id, saved);
    file_jobs(app);
    assert(!write_dirty(app));
    assert(!unlink(path));
    assert(!write_job_start(app, W_JOB_LOAD, saved, NULL, 0));
    file_jobs(app);
    settle(app);
    picture_check(app, 80, "Red camera sample");
    assert(!tiku_png_write(path, &pixel, 1, 1, 1));
    replace_document(app, "");
    write_action(app, W_PICTURE_INSERT);
    write_picked(app, app->id, path);
    replace_document(app, "Edited during image load");
    file_jobs(app);
    expect(app, "Edited during image load");
    assert(strstr(app->status, "cancelled"));
    assert(!unlink(path));
}

/** @brief Exercise property panels, validation, undo, resize and stale targets. */
static void
table_properties(write_app_t *app)
{
    tiku_rich_grid_box_t grid;
    uint64_t generation;
    int width = app->width;

    write_action(app, W_TABLE_LAYOUT);
    assert(app->panel == W_PANEL_TABLE_LAYOUT);
    tiku_field_start(&app->fields[0], "240");
    tiku_field_start(&app->fields[1], "8");
    write_action(app, W_APPLY);
    assert(!app->panel);
    settle(app);
    write_action(app, W_TABLE_COLUMN);
    assert(app->panel == W_PANEL_TABLE_COLUMN);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_APPLY);
    assert(!app->panel && tiku_richdoc_generation(app->doc) == generation);
    write_action(app, W_TABLE_COLUMN);
    tiku_field_start(&app->fields[0], "nan");
    write_action(app, W_APPLY);
    assert(app->panel && tiku_richdoc_generation(app->doc) == generation);
    tiku_field_start(&app->fields[0], "60");
    write_action(app, W_APPLY);
    settle(app);
    assert(!app->panel);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && fabs(grid.box.width - 144) < 0.001);
    write_action(app, W_TABLE_EQUAL);
    settle(app);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && fabs(grid.box.width - 120) < 0.001);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && fabs(grid.box.width - 144) < 0.001);
    write_action(app, W_TABLE_BORDER);
    tiku_field_start(&app->fields[0], "2");
    tiku_field_start(&app->fields[1], "#224466");
    resize(app, 440, app->height);
    settle(app);
    write_paint(app);
    assert(!tiku_png_write("/tmp/tiku-write-table-settings.png", app->surface->px,
        app->surface->w, app->surface->h, 0));
    for (int i = 0; i < app->dialog.count; i++) {
        const tiku_dialog_control_t *control = &app->dialog.control[i];

        if (control->id == W_FIELD || control->id == W_FIELD2 ||
            control->id == W_APPLY || control->id == W_CANCEL) {
            assert(control->rect.x >= 0 && control->rect.w > 0);
            assert(control->rect.x + control->rect.w <= 440);
            assert(control->rect.y + control->rect.h < write_body(app).y);
        }
    }
    resize(app, width, app->height);
    write_action(app, W_APPLY);
    settle(app);
    assert(!app->panel);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && grid.border == 2 && grid.color == 0xff224466);
    write_action(app, W_TABLE_LAYOUT);
    assert(tiku_richsession_type(tiku_richview_session(app->view),
        0, "x", 1) == TIKU_RICH_OK);
    settle(app);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_APPLY);
    assert(app->panel && tiku_richdoc_generation(app->doc) == generation);
    assert(strstr(app->status, "changed"));
    write_action(app, W_CANCEL);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    app->preview = 1;
    write_action(app, W_TABLE_BORDER);
    assert(!app->panel);
    app->preview = 0;
}

/** @brief Validate integer header counts and unchanged settings. */
static void
table_headers(write_app_t *app)
{
    const char *invalid[] = {"-1", "3", "1.5", "1.0000000001", "4294967296"};
    uint64_t generation = tiku_richdoc_generation(app->doc);

    write_action(app, W_TABLE_HEADERS);
    assert(app->panel == W_PANEL_TABLE_HEADERS);
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        tiku_field_start(&app->fields[0], invalid[i]);
        write_action(app, W_APPLY);
        assert(app->panel && tiku_richdoc_generation(app->doc) == generation);
    }
    tiku_field_start(&app->fields[0], "1");
    write_action(app, W_APPLY);
    settle(app);
    assert(!app->panel && tiku_richdoc_generation(app->doc) == generation + 1);
    write_action(app, W_TABLE_HEADERS);
    assert(!strcmp(app->fields[0].text, "1"));
    write_action(app, W_APPLY);
    assert(!app->panel && tiku_richdoc_generation(app->doc) == generation + 1);
}

/** @brief Exercise shading fields, clear/undo and a changed target cell. */
static void
cell_shading(write_app_t *app)
{
    tiku_rich_grid_box_t grid;
    tiku_rich_pos_t first, last, other;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t paragraph;
    uint64_t generation = tiku_richdoc_generation(app->doc);

    write_action(app, W_CELL_FILL);
    assert(app->panel == W_PANEL_CELL_FILL);
    tiku_field_start(&app->fields[0], "#oops");
    write_action(app, W_APPLY);
    assert(app->panel && tiku_richdoc_generation(app->doc) == generation);
    tiku_field_start(&app->fields[0], "#DAEEFF");
    write_action(app, W_APPLY);
    settle(app);
    assert(!app->panel);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && grid.background == 0xffdaeeff);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_CELL_FILL);
    write_action(app, W_APPLY);
    assert(!app->panel && tiku_richdoc_generation(app->doc) == generation);
    write_action(app, W_CELL_CLEAR);
    settle(app);
    tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid);
    assert(!grid.background);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    write_action(app, W_CELL_FILL);
    tiku_richsession_selection(tiku_richview_session(app->view), &first, &last);
    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 4, &paragraph) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    other = first;
    other.paragraph = paragraph.id;
    other.byte = 0;
    assert(tiku_richsession_select(tiku_richview_session(app->view),
        0, other, other) == TIKU_RICH_OK);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_APPLY);
    assert(app->panel && tiku_richdoc_generation(app->doc) == generation);
    assert(strstr(app->status, "changed"));
    write_action(app, W_CANCEL);
    assert(tiku_richsession_select(tiku_richview_session(app->view),
        0, first, last) == TIKU_RICH_OK);
}

/** @brief Exercise cell-selection menus, clipboard, bulk edits and stale panels. */
static void
cell_ranges(write_app_t *app)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t position;
    tiku_rich_cells_t cells;
    tiku_rich_rect_t boxes[3];
    tiku_rect_t body;
    size_t count;
    uint64_t generation;
    uint32_t before_pixel;
    int x, y;

    tiku_richdoc_snapshot(app->doc, &snapshot);
    tiku_richsnapshot_paragraph(snapshot, 1, &p);
    position = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), p.id, 0, 0};
    tiku_richsnapshot_release(snapshot);
    tiku_richsession_select(tiku_richview_session(app->view), 0, position, position);
    tiku_richview_scroll(app->view, 0);
    settle(app);
    write_action(app, W_SELECT_ROW);
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK);
    assert(cells.rows == 1 && cells.columns == 2);
    assert(tiku_richview_cells_rectangles(app->view, boxes, 3, &count) == TIKU_RICH_OK);
    body = write_body(app);
    x = body.x + (int)((boxes[0].x + 3) * app->zoom);
    y = body.y + (int)((boxes[0].y + 3) * app->zoom);
    tiku_richview_select_cells(app->view, 0, NULL);
    write_paint(app);
    before_pixel = app->surface->px[y * app->surface->w + x];
    write_action(app, W_SELECT_ROW);
    write_paint(app);
    assert(app->surface->px[y * app->surface->w + x] != before_pixel);
    assert(!tiku_png_write("/tmp/tiku-write-cell-range.png", app->surface->px,
        app->surface->w, app->surface->h, 0));
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_COPY);
    assert(!strcmp(clip, "\"Temperature\nCelsius\"\t\"24.5\""));
    fail_clip = 1;
    strcpy(clip, "Keep clipboard");
    write_action(app, W_COPY);
    fail_clip = 0;
    assert(!strcmp(clip, "Keep clipboard"));
    write_action(app, W_CUT);
    write_action(app, W_PASTE);
    write_action(app, W_FORMAT + TIKU_RICHCMD_BOLD);
    assert(!strcmp(clip, "Keep clipboard"));
    assert(tiku_richdoc_generation(app->doc) == generation);
    strcpy(clip, "\"New\nparagraph\"\tRight\nBottom\tLast");
    write_action(app, W_SELECT_CELL);
    write_action(app, W_PASTE);
    settle(app);
    expect(app, "Report\nNew\nparagraph\nRight\nBottom\nLast\n");
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK);
    assert(cells.rows == 2 && cells.columns == 2);
    write_action(app, W_COPY);
    assert(!strcmp(clip, "\"New\nparagraph\"\t\"Right\"\n\"Bottom\"\t\"Last\""));
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    expect(app, "Report\nTemperature\nCelsius\n24.5\n\n\n");
    write_action(app, W_SELECT_ROW);
    write_action(app, W_CELLS_ERASE);
    settle(app);
    expect(app, "Report\n\n\n\n\n\n");
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    expect(app, "Report\nTemperature\nCelsius\n24.5\n\n\n");
    write_action(app, W_SELECT_TABLE);
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK &&
           cells.rows == 2 && cells.columns == 2);
    write_action(app, W_CELL_FILL);
    tiku_field_start(&app->fields[0], "#112233");
    write_action(app, W_APPLY);
    settle(app);
    for (size_t i = 1; i <= 4; i++) {
        tiku_rich_grid_box_t box;

        tiku_richlayout_grid(tiku_richview_layout(app->view), i, &box);
        assert(box.background == 0xff112233);
    }
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    write_action(app, W_SELECT_COLUMN);
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK &&
           cells.rows == 2 && cells.columns == 1);
    write_action(app, W_CELL_FILL);
    generation = tiku_richdoc_generation(app->doc);
    cells.columns = 2;
    tiku_richview_select_cells(app->view, 0, &cells);
    tiku_field_start(&app->fields[0], "#FFFFFF");
    write_action(app, W_APPLY);
    assert(app->panel && tiku_richdoc_generation(app->doc) == generation);
    assert(strstr(app->status, "changed"));
    write_action(app, W_CANCEL);
    tiku_richview_select_cells(app->view, 0, NULL);
    settle(app);
}

/** @brief Route a divider drag through the app, with hover and one undo. */
static void
column_drag(write_app_t *app)
{
    tiku_rich_grid_box_t grid;
    tiku_rich_divider_t divider;
    tiku_rect_t body = write_body(app);
    tiku_event_t pointer = {.type = TIKU_EVENT_POINTER_MOVE, .button = 1};
    uint64_t generation = tiku_richdoc_generation(app->doc);

    tiku_richview_scroll(app->view, 0);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK);
    pointer.x = body.x + (int)lround((grid.box.x + grid.box.width) * app->zoom);
    pointer.y = body.y + (int)lround((grid.box.y + 3) * app->zoom);
    event(app, &pointer);
    assert(cursor_shape == TIKU_CURSOR_RESIZE_H);
    pointer.type = TIKU_EVENT_POINTER_DOWN;
    event(app, &pointer);
    pointer.type = TIKU_EVENT_POINTER_MOVE;
    pointer.x += 20;
    event(app, &pointer);
    assert(tiku_richview_divider(app->view, &divider) == TIKU_RICH_OK);
    assert(tiku_richdoc_generation(app->doc) == generation);
    pointer.type = TIKU_EVENT_POINTER_UP;
    event(app, &pointer);
    settle(app);
    assert(tiku_richdoc_generation(app->doc) == generation + 1);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && fabs(grid.box.width - (144 + 20 / app->zoom)) < .001);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK && fabs(grid.box.width - 144) < .001);
    pointer.type = TIKU_EVENT_DEACTIVATED;
    event(app, &pointer);
    assert(cursor_shape == TIKU_CURSOR_ARROW);
    pointer.type = TIKU_EVENT_ACTIVATED;
    event(app, &pointer);
}

/** @brief Keep scroll after a drag whose release is above the document. */
static void
scrolled_drag(write_app_t *app)
{
    int height = app->height;
    double zoom = app->zoom, scroll;
    tiku_rich_grid_box_t grid;
    tiku_rect_t body;
    tiku_event_t pointer = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1};

    app->zoom = 2;
    resize(app, app->width, 240);
    settle(app);
    assert(tiku_richview_move(app->view, TIKU_RICH_MOVE_START, 0) ==
           TIKU_RICH_OK);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid) ==
           TIKU_RICH_OK);
    scroll = grid.box.y / 2;
    assert(scroll > 0);
    assert(tiku_richview_scroll(app->view, scroll) == TIKU_RICH_OK);
    assert(tiku_richview_scroll_y(app->view) == scroll);
    body = write_body(app);
    pointer.x = body.x + (int)lround((grid.box.x + grid.box.width) * app->zoom);
    pointer.y = body.y + (int)lround((grid.box.y + 3 - scroll) * app->zoom);
    event(app, &pointer);
    pointer.x += 20;
    pointer.y = 10;
    pointer.type = TIKU_EVENT_POINTER_MOVE;
    event(app, &pointer);
    pointer.type = TIKU_EVENT_POINTER_UP;
    event(app, &pointer);
    settle(app);
    assert(tiku_richview_scroll_y(app->view) == scroll);
    tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &grid);
    assert(fabs(grid.box.width - 154) < 0.001);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    app->zoom = zoom;
    resize(app, app->width, height);
    settle(app);
}

/** @brief Insert and edit cells, traverse them, save, reopen and delete. */
static void
tables(write_app_t *app, const char *home)
{
    char saved[WRITE_PATH];
    tiku_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_TAB};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {"Temperature", 11}};
    uint64_t generation;

    replace_document(app, "Report");
    assert(tiku_richview_move(app->view, TIKU_RICH_MOVE_FINISH, 0) ==
           TIKU_RICH_OK);
    write_action(app, W_TABLE_INSERT);
    assert(app->panel == W_PANEL_TABLE);
    tiku_field_start(&app->fields[0], "2 x 2");
    write_action(app, W_APPLY);
    assert(!app->panel);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 5);
    assert(tiku_richsession_execute(tiku_richview_session(app->view),
        0, &command) == TIKU_RICH_OK);
    settle(app);
    assert(tiku_richsession_type(tiku_richview_session(app->view), 0,
        "\nCelsius", 8) == TIKU_RICH_OK);
    settle(app);
    write_input(app, &event);
    settle(app);
    command.value.text.data = "24.5";
    command.value.text.length = 4;
    assert(tiku_richsession_execute(tiku_richview_session(app->view),
        0, &command) == TIKU_RICH_OK);
    settle(app);
    expect(app, "Report\nTemperature\nCelsius\n24.5\n\n\n");
    generation = tiku_richdoc_generation(app->doc);
    event.key = TIKU_KEY_HOME;
    write_input(app, &event);
    event.key = TIKU_KEY_BACKSPACE;
    write_input(app, &event);
    settle(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    strcpy(clip, "Keep clipboard contents");
    write_action(app, W_ALL);
    write_action(app, W_CUT);
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(!strcmp(clip, "Keep clipboard contents"));
    assert(strstr(app->status, "cannot cross table or cell boundaries"));
    assert(tiku_richview_move(app->view, TIKU_RICH_MOVE_START, 0) ==
           TIKU_RICH_OK);
    assert(tiku_richview_move(app->view, TIKU_RICH_MOVE_DOWN, 0) ==
           TIKU_RICH_OK);
    table_properties(app);
    table_headers(app);
    cell_shading(app);
    column_drag(app);
    scrolled_drag(app);
    cell_ranges(app);
    write_paint(app);
    assert(!tiku_png_write("/tmp/tiku-write-tables.png", app->surface->px,
        app->surface->w, app->surface->h, 0));
    snprintf(saved, sizeof saved, "%s/tables.tkrd", home);
    write_action(app, W_SAVEAS);
    write_picked(app, app->id, saved);
    file_jobs(app);
    assert(!write_dirty(app));
    assert(!write_job_start(app, W_JOB_LOAD, saved, NULL, 0));
    file_jobs(app);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 5);
    {
        tiku_rich_grid_box_t grid;

        assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1,
            &grid) == TIKU_RICH_OK && fabs(grid.box.width - 144) < 0.001);
        assert(grid.border == 2 && grid.color == 0xff224466);
        assert(grid.background == 0xffdaeeff);
        tiku_richsnapshot_t *snapshot;
        tiku_rich_block_t table;

        assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_block(snapshot, 1, &table) == TIKU_RICH_OK);
        assert(table.table.header_rows == 1);
        tiku_richsnapshot_release(snapshot);
    }
    assert(tiku_richview_move(app->view, TIKU_RICH_MOVE_DOWN, 0) ==
           TIKU_RICH_OK);
    write_action(app, W_FORMAT + TIKU_RICHCMD_ROW_AFTER);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 7);
    write_action(app, W_FORMAT + TIKU_RICHCMD_COLUMN_AFTER);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 10);
    command.value.text.data = "New cell";
    command.value.text.length = 8;
    assert(tiku_richsession_execute(tiku_richview_session(app->view),
        0, &command) == TIKU_RICH_OK);
    settle(app);
    write_action(app, W_FORMAT + TIKU_RICHCMD_COLUMN_REMOVE);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 7);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 10);
    write_action(app, W_FORMAT + TIKU_RICHCMD_REDO);
    settle(app);
    write_action(app, W_FORMAT + TIKU_RICHCMD_ROW_REMOVE);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 5);
    expect(app, "Report\nTemperature\nCelsius\n24.5\n\n\n");
    write_action(app, W_TABLE_REMOVE);
    settle(app);
    assert(!tiku_richlayout_grids(tiku_richview_layout(app->view)));
    expect(app, "Report\n");
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    settle(app);
    assert(tiku_richlayout_grids(tiku_richview_layout(app->view)) == 5);
    write_action(app, W_FORMAT + TIKU_RICHCMD_REDO);
    settle(app);
}

/** @brief Extend cells with keys and scroll a captured drag through app ticks. */
static void
cell_navigation(write_app_t *app)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_DOWN,
        .modifiers = TIKU_MOD_SHIFT};
    tiku_rich_cells_t cells;
    tiku_rich_grid_box_t box;
    tiku_rect_t body;
    double before;
    uint64_t generation;
    int previous_frames;

    replace_document(app, "Scroll test");
    tiku_richview_move(app->view, TIKU_RICH_MOVE_FINISH, 0);
    write_action(app, W_TABLE_INSERT);
    tiku_field_start(&app->fields[0], "40 x 2");
    write_action(app, W_APPLY);
    assert(!app->panel);
    settle(app);
    write_action(app, W_SELECT_CELL);
    generation = tiku_richdoc_generation(app->doc);
    event(app, &ev);
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK);
    assert(cells.row == 0 && cells.rows == 2 && cells.columns == 1);
    ev.key = TIKU_KEY_RIGHT;
    event(app, &ev);
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK);
    assert(cells.rows == 2 && cells.columns == 2);
    resize(app, 500, 280);
    settle(app);
    tiku_richview_scroll(app->view, 0);
    body = write_body(app);
    assert(tiku_richlayout_grid(tiku_richview_layout(app->view), 1, &box) == TIKU_RICH_OK);
    ev = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .modifiers = TIKU_MOD_OPTION,
        .x = body.x + (int)((box.box.x + 8) * app->zoom),
        .y = body.y + (int)((box.box.y + 8) * app->zoom)};
    event(app, &ev);
    assert(app->body_drag);
    ev.type = TIKU_EVENT_POINTER_MOVE;
    ev.y = app->height + 100;
    event(app, &ev);
    before = tiku_richview_scroll_y(app->view);
    app->active = 1;
    tick(app, app->now);
    previous_frames = frames;
    tick(app, app->now + 100000);
    assert(tiku_richview_scroll_y(app->view) > before);
    assert(frames > previous_frames);
    assert(tiku_richview_cells(app->view, &cells) == TIKU_RICH_OK && cells.rows > 2);
    ev.type = TIKU_EVENT_POINTER_UP;
    ev.y = 30;
    event(app, &ev);
    assert(!app->body_drag && !app->panel);
    before = tiku_richview_scroll_y(app->view);
    tick(app, app->now + 100000);
    assert(tiku_richview_scroll_y(app->view) == before);
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(tiku_richview_select_cells(app->view, 0, NULL) == TIKU_RICH_OK);
}

/** @brief Deliver an actual server response after the application returns. */
static void
clipboard_reply(write_app_t *app)
{
    tiku_msg_t *reply;

    assert(clip_request);
    reply = tiku_clip_reply(clip_request);
    assert(reply);
    tiku_msg_free(clip_request);
    clip_request = NULL;
    write_clip_message(app, reply);
    tiku_msg_free(reply);
    settle(app);
}

/** @brief Preserve styled pictures and require acknowledgment before Cut. */
static void
rich_clipboard(write_app_t *app)
{
    const uint32_t pixel = 0xff123456;
    const tiku_rich_image_t image = {1, 1, &pixel, 12, 12, "Test"};
    tiku_rich_run_source_t runs[] = {
        {.length = 4, .chars = {.present = TIKU_RICH_BOLD, .bold = 1}},
        {.length = 3, .image = &image}};
    tiku_rich_paragraph_source_t p = {.text = "Rich\xef\xbf\xbc",
        .length = 7, .runs = runs, .run_count = 2};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t got;
    tiku_rich_pos_t anchor, caret;

    typed_clip = 1;
    write_clip_probe(app);
    clipboard_reply(app);
    assert(app->clip_supported);
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(!write_adopt(app, doc));
    settle(app);
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    write_action(app, W_CUT);
    expect(app, "Rich\xef\xbf\xbc");
    assert(app->clip_action == W_CUT);
    clipboard_reply(app);
    expect(app, "");
    assert(!strcmp(tiku_clipboard_text(), "Rich[Picture]"));
    assert(tiku_clipboard_data(NULL, NULL));
    assert(tiku_clipboard_find("text/html", NULL));
    write_action(app, W_PASTE);
    expect(app, "");
    clipboard_reply(app);
    expect(app, "Rich\xef\xbf\xbc");
    assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 0, &got);
    assert(got.runs[0].chars.bold && got.runs[1].image);
    assert(got.runs[1].image->pixels[0] == pixel);
    tiku_richsnapshot_release(snapshot);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    expect(app, "");
    write_action(app, W_FORMAT + TIKU_RICHCMD_REDO);
    expect(app, "Rich\xef\xbf\xbc");
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    write_action(app, W_CUT);
    tiku_richsession_selection(tiku_richview_session(app->view), &anchor, &caret);
    assert(tiku_richsession_select(tiku_richview_session(app->view),
        0, caret, caret) == TIKU_RICH_OK);
    clipboard_reply(app);
    expect(app, "Rich\xef\xbf\xbc");
    assert(strstr(app->status, "selection changed"));
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    write_action(app, W_CUT);
    app->now = app->clip_deadline;
    write_clip_tick(app);
    assert(!app->clip_action);
    clipboard_reply(app);
    expect(app, "Rich\xef\xbf\xbc");
    assert(tiku_clipboard_import("plain", 5));
    write_action(app, W_PASTE);
    clipboard_reply(app);
    expect(app, "plain");
    assert(!tiku_clipboard_data(NULL, NULL));
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    write_action(app, W_CUT);
    assert(tiku_richsession_set_read_only(tiku_richview_session(app->view),
        1) == TIKU_RICH_OK);
    clipboard_reply(app);
    expect(app, "plain");
    assert(tiku_richsession_set_read_only(tiku_richview_session(app->view),
        0) == TIKU_RICH_OK);
    assert(tiku_clipboard_import_typed("fallback", 8, TIKU_RICHCLIP_MIME,
        "damaged", 7));
    write_action(app, W_PASTE);
    clipboard_reply(app);
    expect(app, "plain");
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    write_action(app, W_CUT);
    replace_document(app, "newer content");
    clipboard_reply(app);
    expect(app, "newer content");
    typed_clip = app->clip_supported = 0;
    tiku_clipboard_set(NULL);
}

/** @brief Copy rich cells, defer Cut, expand Paste and refuse stale rectangles. */
static void
rich_cell_clipboard(write_app_t *app)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_rich_paragraph_t p;
    tiku_rich_block_t table, cell;
    uint32_t pixel = 0xff123456;
    tiku_rich_image_t image = {1, 1, &pixel, 12, 12, "Camera"};
    tiku_rich_edit_t insert = {.kind = TIKU_RICH_TABLE_INSERT,
        .table = {.rows = 2, .columns = 2}};
    tiku_rich_transaction_t tx = {0, &insert, 1};
    tiku_rich_cells_t cells, changed;

    typed_clip = app->clip_supported = 1;
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(s, 0, &p);
    insert.paragraph = p.id;
    tiku_richsnapshot_release(s);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    tiku_richsnapshot_block(s, 1, &table);
    tiku_richsnapshot_block(s, 2, &cell);
    tiku_richsnapshot_paragraph(s, 1, &p);
    tiku_richsnapshot_release(s);
    tiku_rich_edit_t edits[] = {
        {.kind = TIKU_RICH_REPLACE, .paragraph = p.id, .text = "Bold", .length = 4,
         .chars = {.present = TIKU_RICH_BOLD, .bold = 1}},
        {.kind = TIKU_RICH_IMAGE_INSERT, .paragraph = p.id, .start = 4, .end = 4, .image = &image},
        {.kind = TIKU_RICH_CELL_SET, .object = cell.id, .background = 0xffaabbcc}};
    tx = (tiku_rich_transaction_t){0, edits, 3};
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(!write_adopt(app, doc));
    settle(app);
    cells = (tiku_rich_cells_t){table.id, 0, 0, 1, 2};
    assert(tiku_richview_select_cells(app->view, 0, &cells) == TIKU_RICH_OK);
    assert(write_cells_allowed(app, W_CUT));
    uint64_t generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_CUT);
    assert(app->clip_action == W_CUT && tiku_richdoc_generation(app->doc) == generation);
    clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation + 1);
    assert(!strcmp(tiku_clipboard_text(), "\"Bold[Picture]\"\t\"\""));
    cells.row = 1; cells.columns = 1;
    assert(tiku_richview_select_cells(app->view, 0, &cells) == TIKU_RICH_OK);
    write_action(app, W_PASTE);
    clipboard_reply(app);
    assert(tiku_richview_cells(app->view, &changed) == TIKU_RICH_OK && changed.columns == 2);
    assert(tiku_richdoc_snapshot(app->doc, &s) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(s, 3, &p);
    assert(!strcmp(p.text, "Bold\xef\xbf\xbc") && p.runs[0].chars.bold);
    assert(p.runs[1].image->pixels[0] == pixel);
    tiku_richsnapshot_cell_at(s, &changed, 0, &cell);
    assert(cell.background == 0xffaabbcc);
    tiku_richsnapshot_release(s);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    write_action(app, W_FORMAT + TIKU_RICHCMD_REDO);
    assert(tiku_richview_select_cells(app->view, 0, &changed) == TIKU_RICH_OK);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_CUT);
    assert(tiku_richview_select_cells(app->view, 0, &cells) == TIKU_RICH_OK);
    clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(strstr(app->status, "selection changed"));
    assert(tiku_clipboard_import_typed("fallback", 8, TIKU_RICHCELLCLIP_MIME, "bad", 3));
    write_action(app, W_PASTE);
    clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(tiku_clipboard_import_typed("plain\ttext", 10,
        TIKU_RICHCELLCLIP_MIME, "bad", 3));
    write_action(app, W_PASTE_TEXT);
    clipboard_reply(app);
    assert(tiku_richdoc_snapshot(app->doc, &s) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(s, 3, &p);
    assert(!strcmp(p.text, "plain"));
    tiku_richsnapshot_release(s);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    assert(tiku_richview_select_cells(app->view, 0, &cells) == TIKU_RICH_OK);
    generation = tiku_richdoc_generation(app->doc);
    assert(tiku_clipboard_import("plain\ttext", 10));
    write_action(app, W_PASTE);
    clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation + 1);
    write_action(app, W_COPY);
    clipboard_reply(app);
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(!write_adopt(app, doc));
    replace_document(app, "beforeafter");
    assert(tiku_richview_select_all(app->view) == TIKU_RICH_OK);
    generation = tiku_richdoc_generation(app->doc);
    write_action(app, W_PASTE);
    clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    expect(app, "beforeafter");
    assert(tiku_richdoc_snapshot(app->doc, &s) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(s, 0, &p);
    tiku_rich_pos_t caret = {tiku_richsnapshot_body(s), p.id, 6, TIKU_RICH_AFTER};
    tiku_richsnapshot_release(s);
    assert(tiku_richsession_select(tiku_richview_session(app->view), 0,
        caret, caret) == TIKU_RICH_OK);
    write_action(app, W_PASTE);
    clipboard_reply(app);
    expect(app, "before\nplain\ntext\nafter");
    assert(tiku_richview_cells(app->view, &changed) == TIKU_RICH_OK);
    assert(changed.rows == 1 && changed.columns == 2);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    expect(app, "beforeafter");
    write_action(app, W_FORMAT + TIKU_RICHCMD_REDO);
    expect(app, "before\nplain\ntext\nafter");
    typed_clip = app->clip_supported = 0;
    tiku_clipboard_set(NULL);
}

/** @brief Verify menu-driven HTML export, source state and replacement guard. */
static void
html_export(write_app_t *app, const char *home)
{
    char path[WRITE_PATH], native[WRITE_PATH], data[32768];
    uint64_t generation;
    int dirty;
    FILE *file;
    size_t n;

    {
        static const uint32_t pixel = 0x80ff0088;
        const tiku_rich_image_t image = {1, 1, &pixel, 20, 10, "Export picture"};
        tiku_richsnapshot_t *snapshot;
        tiku_rich_paragraph_t p;
        tiku_rich_edit_t edit = {.kind = TIKU_RICH_IMAGE_INSERT, .image = &image};
        tiku_rich_transaction_t change = {0, &edit, 1};

        assert(tiku_richdoc_snapshot(app->doc, &snapshot) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
        edit.paragraph = p.id;
        tiku_richsnapshot_release(snapshot);
        assert(tiku_richdoc_apply(app->doc, &change) == TIKU_RICH_OK);
    }
    snprintf(native, sizeof native, "%s", app->path);
    generation = tiku_richdoc_generation(app->doc);
    dirty = write_dirty(app);
    snprintf(path, sizeof path, "%s/export.html", home);
    write_action(app, W_EXPORT_HTML);
    assert(app->pick_kind == W_EXPORT_HTML);
    write_picked(app, app->id, path);
    file_jobs(app);
    assert(!app->prompt && !strcmp(app->path, native));
    assert(write_dirty(app) == dirty);
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(strstr(app->status, "Exported HTML."));
    assert(strstr(app->status, "Browser controls layout."));
    assert(!strstr(app->status, "alternate text"));
    file = fopen(path, "rb");
    assert(file);
    n = fread(data, 1, sizeof data - 1, file);
    assert(!ferror(file) && feof(file) && !fclose(file));
    data[n] = 0;
    assert(strstr(data, "<table") && strstr(data, "before"));
    assert(strstr(data, "plain") && strstr(data, "after"));
    assert(strstr(data, "src=\"data:image/png;base64,"));
    assert(strstr(data, "alt=\"Export picture\""));
    write_action(app, W_EXPORT_HTML);
    write_picked(app, app->id, path);
    file_jobs(app);
    assert(app->prompt == W_PROMPT_OVERWRITE);
    file = fopen(path, "wb");
    assert(file && fputs("external HTML change", file) >= 0 && !fclose(file));
    write_action(app, W_CONFIRM);
    file_jobs(app);
    assert(app->prompt == W_PROMPT_CONFLICT);
    file = fopen(path, "rb");
    assert(file);
    n = fread(data, 1, sizeof data - 1, file);
    assert(!fclose(file));
    data[n] = 0;
    assert(!strcmp(data, "external HTML change"));
    write_action(app, W_CANCEL);
    write_action(app, W_EXPORT_HTML);
    write_picked(app, app->id, "/tmp/refuse-html-export.tkrd");
    assert(!app->job && strstr(app->status, ".html"));
    assert(tiku_richdoc_generation(app->doc) == generation);
    assert(!strcmp(app->path, native) && write_dirty(app) == dirty);
}

/** @brief Open editable markup copies and export without replacing their sources. */
static void
markup_files(write_app_t *app, const char *home)
{
    char path[WRITE_PATH], output[WRITE_PATH], original[WRITE_PATH];
    const char *inputs[] = {"<h1>Imported</h1><p>Hello <b>world</b>.</p>",
                            "# Imported\n\nHello **world**.\n"};
    const char *extensions[] = {"html", "md"};

    for (unsigned i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/import.%s", home, extensions[i]);
        FILE *file = fopen(path, "wb");

        assert(file && fputs(inputs[i], file) >= 0 && !fclose(file));
        assert(!write_job_start(app, W_JOB_LOAD, path, NULL, 0));
        file_jobs(app);
        assert(app->imported && !*app->path);
        expect(app, "Imported\nHello world.");
        write_action(app, W_SAVE);
        assert(app->pick_kind == W_SAVEAS);
        write_picked(app, app->id, "");
        snprintf(original, sizeof original, "%s", app->path);
        snprintf(output, sizeof output, "%s/export-%u.md", home, i);
        uint64_t generation = tiku_richdoc_generation(app->doc);

        write_action(app, W_EXPORT_MARKDOWN);
        assert(app->pick_kind == W_EXPORT_MARKDOWN);
        write_picked(app, app->id, output);
        file_jobs(app);
        assert(strstr(app->status, "Exported Markdown"));
        assert(!strcmp(app->path, original) && app->imported);
        assert(tiku_richdoc_generation(app->doc) == generation);
        file = fopen(path, "rb");
        char bytes[256];
        size_t length = fread(bytes, 1, sizeof bytes - 1, file);

        assert(!fclose(file)); bytes[length] = 0;
        assert(!strcmp(bytes, inputs[i]));
        write_action(app, W_EXPORT_MARKDOWN);
        write_picked(app, app->id, output);
        file_jobs(app);
        assert(app->prompt == W_PROMPT_OVERWRITE);
        file = fopen(output, "wb");
        assert(file && fputs("external", file) >= 0 && !fclose(file));
        write_action(app, W_CONFIRM);
        file_jobs(app);
        assert(app->prompt == W_PROMPT_CONFLICT);
        write_action(app, W_CANCEL);
    }
    snprintf(path, sizeof path, "%s/bad.html", home);
    FILE *file = fopen(path, "wb");

    assert(file && fputs("<table><tr><td colspan='2'>bad</td></tr></table>", file) >= 0 && !fclose(file));
    uint64_t generation = tiku_richdoc_generation(app->doc);

    assert(!write_job_start(app, W_JOB_LOAD, path, NULL, 0));
    file_jobs(app);
    expect(app, "Imported\nHello world.");
    assert(tiku_richdoc_generation(app->doc) == generation);
}

/** @brief Headings, virtual outline selection, refresh, and non-editing navigation. */
static void
outline_workflow(write_app_t *app)
{
    tiku_richdoc_t *doc = NULL;
    unsigned notices;
    const char *html = "<h1>First</h1><p>Body</p><h2>Second</h2><h3>Third</h3>";
    tiku_rich_pos_t anchor, caret;
    tiku_rich_heading_t heading;
    tiku_rich_selection_t format;
    tiku_event_t down = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_DOWN};
    tiku_event_t enter = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};

    assert(tiku_richmarkup_read_html(html, strlen(html), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(!write_adopt(app, doc));
    settle(app);
    uint64_t generation = tiku_richdoc_generation(doc);
    tiku_rect_t original = write_body(app);

    write_action(app, W_OUTLINE);
    settle(app);
    assert(app->show_outline && app->outline_status == TIKU_RICH_OK);
    assert(tiku_richoutline_count(app->outline) == 3);
    assert(write_body(app).x > original.x && write_body(app).w < original.w);
    assert(app->dialog.focus == W_OUTLINE_LIST);
    event(app, &down); event(app, &enter);
    assert(app->dialog.focus == W_BODY && app->outline_selected == 1);
    assert(tiku_richoutline_get(app->outline, 1, &heading) == TIKU_RICH_OK);
    tiku_richsession_selection(tiku_richview_session(app->view), &anchor, &caret);
    assert(caret.paragraph == heading.position.paragraph && !caret.byte);
    assert(tiku_richdoc_generation(doc) == generation && !tiku_richdoc_dirty(doc));
    assert(tiku_richsession_set_read_only(tiku_richview_session(app->view), 1) ==
           TIKU_RICH_OK);
    write_action(app, W_OUTLINE_NEXT);
    assert(app->outline_selected == 2);
    write_action(app, W_HEADING_LEVEL + 5);
    assert(tiku_richdoc_generation(doc) == generation);
    assert(tiku_richsession_set_read_only(tiku_richview_session(app->view), 0) ==
           TIKU_RICH_OK);
    write_action(app, W_OUTLINE_PREV);
    assert(app->outline_selected == 1);
    write_action(app, W_HEADING_LEVEL + 4);
    assert(tiku_richsession_format(tiku_richview_session(app->view), &format) == TIKU_RICH_OK);
    assert(format.props.outline == 5);
    tiku_event_t click = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 18, .y = write_body(app).y + 18};

    assert(!write_outline_event(app, &click));
    write_outline_step(app);
    assert(tiku_richoutline_get(app->outline, 1, &heading) == TIKU_RICH_OK && heading.level == 5);
    settle(app);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO);
    write_outline_step(app); settle(app);
    assert(tiku_richoutline_get(app->outline, 1, &heading) == TIKU_RICH_OK && heading.level == 2);
    write_action(app, W_TEXT);
    write_outline_step(app); settle(app);
    assert(tiku_richoutline_count(app->outline) == 2);
    generation = tiku_richdoc_generation(doc);
    assert(write_outline_event(app, &click));
    assert(app->outline_selected == 0);
    assert(tiku_richdoc_generation(doc) == generation);
    write_action(app, W_PREVIEW);
    assert(write_outline_width(app) == 0);
    write_action(app, W_HEADING_LEVEL + 5);
    assert(tiku_richdoc_generation(doc) == generation);
    write_action(app, W_PREVIEW);
    resize(app, 320, 240); settle(app);
    assert(write_body(app).w > 0 && write_outline_width(app) > 0);
    write_paint(app);
    resize(app, 900, 680); settle(app);
    write_action(app, W_OUTLINE);
    assert(!app->show_outline && !app->outline);
    assert(app->dialog.focus == W_BODY);
    write_action(app, W_OUTLINE_NEXT);
    assert(!app->show_outline && app->outline_selected == 1);
    {
        tiku_rich_paragraph_source_t paragraphs[70];
        tiku_rich_source_t source = {.paragraphs = paragraphs, .paragraph_count = 70};

        for (size_t i = 0; i < 70; i++) paragraphs[i] =
            (tiku_rich_paragraph_source_t){.text = "Section", .length = 7,
            .paragraph = {.present = TIKU_RICH_OUTLINE, .outline = 2}};
        assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
        assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
        assert(!write_adopt(app, doc));
        settle(app);
        write_action(app, W_OUTLINE); settle(app);
        assert(tiku_richoutline_count(app->outline) == 70);
        down.key = TIKU_KEY_END;
        event(app, &down);
        assert(app->outline_selected == 69 && app->outline_top > 0);
        event(app, &enter);
        assert(tiku_richoutline_get(app->outline, 69, &heading) == TIKU_RICH_OK);
        tiku_richsession_selection(tiku_richview_session(app->view), &anchor, &caret);
        assert(caret.paragraph == heading.position.paragraph);
        size_t top = app->outline_top;
        tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .x = 18,
                              .y = write_body(app).y + 40, .dy = -1};

        event(app, &wheel);
        assert(app->outline_top == top - 3);
        write_action(app, W_OUTLINE); settle(app);
        assert(!app->show_outline && !app->outline);
    }
}

/** @brief Drag indentation once, cancel captures, and edit with the keyboard. */
static void
ruler_workflow(write_app_t *app)
{
    tiku_richdoc_t *doc;
    tiku_rich_ruler_t ruler;
    tiku_rect_t original, body, box;
    tiku_event_t pointer = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1};
    tiku_event_t key = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE};
    uint64_t generation;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(!write_adopt(app, doc));
    resize(app, 880, 640);
    app->imported = 0;
    app->active = 1;
    write_action(app, W_ZOOM_RESET); settle(app);
    original = write_body(app);
    write_action(app, W_RULER); settle(app);
    body = write_body(app); box = write_ruler_bounds(app);
    assert(app->show_ruler && box.h == 28 && body.y == original.y + 32);
    assert(body.w == original.w && body.h == original.h - 32);
    assert(app->dialog.focus == W_RULER_CONTROL);
    pointer.x = body.x; pointer.y = box.y + 3;
    assert(write_ruler_hit(app, pointer.x, pointer.y) == 1);
    generation = tiku_richdoc_generation(doc);
    event(app, &pointer);
    assert(app->ruler_drag && cursor_shape == TIKU_CURSOR_RESIZE_H);
    pointer.type = TIKU_EVENT_POINTER_MOVE; pointer.x += 24;
    event(app, &pointer);
    assert(app->ruler_preview.position[0] == 24);
    assert(tiku_richdoc_generation(doc) == generation && !write_dirty(app));
    pointer.type = TIKU_EVENT_POINTER_UP; event(app, &pointer); settle(app);
    assert(!app->ruler_drag && tiku_richdoc_generation(doc) == generation + 1);
    assert(tiku_richruler_read(app->view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[0] == 24);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO); settle(app);
    assert(!write_dirty(app));
    pointer.type = TIKU_EVENT_POINTER_DOWN; pointer.x = body.x;
    event(app, &pointer);
    pointer.type = TIKU_EVENT_POINTER_MOVE; pointer.x += 50; event(app, &pointer);
    generation = tiku_richdoc_generation(doc);
    event(app, &key);
    assert(!app->ruler_drag);
    pointer.type = TIKU_EVENT_POINTER_UP; event(app, &pointer);
    assert(tiku_richdoc_generation(doc) == generation && !write_dirty(app));
    tiku_dialog_focus(&app->dialog, W_RULER_CONTROL);
    app->ruler_part = TIKU_RICH_RULER_LEFT;
    key.key = TIKU_KEY_RIGHT; key.modifiers = TIKU_MOD_SHIFT;
    event(app, &key); settle(app);
    assert(tiku_richruler_read(app->view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[1] == 10 && ruler.position[0] == 10);
    key.key = TIKU_KEY_DOWN; key.modifiers = 0; event(app, &key);
    assert(app->ruler_part == TIKU_RICH_RULER_RIGHT);
    key.key = TIKU_KEY_LEFT; event(app, &key); settle(app);
    assert(tiku_richruler_read(app->view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[2] == ruler.width - 1);
    key.key = TIKU_KEY_RETURN; event(app, &key);
    assert(app->dialog.focus == W_BODY);
    write_action(app, W_ZOOM_IN); settle(app);
    assert(tiku_richruler_read(app->view, &ruler) == TIKU_RICH_OK);
    box = write_ruler_bounds(app); body = write_body(app);
    pointer = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = body.x + (int)lround(ruler.position[0] * app->zoom), .y = box.y + 3};
    event(app, &pointer); assert(app->ruler_drag);
    pointer.type = TIKU_EVENT_POINTER_MOVE; pointer.x += 25;
    event(app, &pointer); assert(app->ruler_preview.position[0] == 30);
    pointer.type = TIKU_EVENT_POINTER_UP; event(app, &pointer); settle(app);
    assert(tiku_richruler_read(app->view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[0] == 30 && ruler.position[1] == 10);
    pointer.type = TIKU_EVENT_POINTER_DOWN; event(app, &pointer);
    assert(app->ruler_drag);
    tiku_event_t changed_size = {.type = TIKU_EVENT_RESIZE, .width = 700, .height = 480};
    generation = tiku_richdoc_generation(doc);
    event(app, &changed_size); settle(app);
    assert(!app->ruler_drag && generation == tiku_richdoc_generation(doc));
    tiku_richsession_set_read_only(tiku_richview_session(app->view), 1);
    box = write_ruler_bounds(app); body = write_body(app);
    assert(!write_ruler_hit(app, body.x + (int)(30 * app->zoom), box.y + 3));
    tiku_dialog_focus(&app->dialog, W_RULER_CONTROL);
    key.key = TIKU_KEY_RIGHT; event(app, &key);
    assert(tiku_richdoc_generation(doc) == generation);
    tiku_richsession_set_read_only(tiku_richview_session(app->view), 0);
    write_action(app, W_PREVIEW); settle(app);
    assert(!write_ruler_bounds(app).h);
    write_action(app, W_RULER); assert(app->show_ruler);
    write_action(app, W_PREVIEW); settle(app);
    resize(app, 320, 240); settle(app); write_paint(app);
    assert(write_ruler_bounds(app).w > 0 && write_body(app).h > 0);
    write_action(app, W_RULER); assert(!app->show_ruler);
    resize(app, 880, 640); write_action(app, W_ZOOM_RESET); settle(app);
}

/** @brief Select link occurrences without editing, activation or stale movement. */
static void
links_workflow(write_app_t *app)
{
    tiku_richdoc_t *doc;
    unsigned notices;
    const char *html = "<p><a href='https://example.org'>One</a> gap "
        "<a href='#two'>Two</a></p><p><a href='mailto:a@example.org'>Three</a></p>";
    tiku_rich_pos_t first, last;
    uint64_t generation;

    assert(tiku_richmarkup_read_html(html, strlen(html), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(!write_adopt(app, doc)); settle(app);
    generation = tiku_richdoc_generation(doc);
    write_action(app, W_LINK_NEXT); assert(app->linkscan);
    write_links_step(app); settle(app);
    assert(!app->linkscan && strstr(app->status, "https://example.org"));
    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    assert(first.byte == 0 && last.byte == 3);
    write_action(app, W_LINK_NEXT); write_links_step(app); settle(app);
    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    assert(first.byte == 8 && last.byte == 11 && strstr(app->status, "#two"));
    write_action(app, W_LINK_PREV); write_links_step(app); settle(app);
    assert(app->link_match.first.byte == 0 && app->link_match.last.byte == 3);
    assert(tiku_richdoc_generation(doc) == generation && !tiku_richdoc_dirty(doc));
    write_action(app, W_LINK_NEXT);
    assert(app->linkscan);
    tiku_richview_move(app->view, TIKU_RICH_MOVE_FINISH, 0);
    assert(!app->linkscan && !app->status[0]);
    write_links_step(app);
    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    assert(first.byte == 5 && last.byte == 5);
    write_action(app, W_LINK_PREV); write_links_step(app); settle(app);
    assert(strstr(app->status, "mailto:a@example.org"));
    tiku_richsession_set_read_only(tiku_richview_session(app->view), 1);
    write_action(app, W_LINK_PREV); write_links_step(app); settle(app);
    assert(strstr(app->status, "#two"));
    assert(tiku_richdoc_generation(doc) == generation);
    tiku_richsession_set_read_only(tiku_richview_session(app->view), 0);
    write_action(app, W_LINK_NEXT);
    write_action(app, W_FORMAT + TIKU_RICHCMD_BOLD);
    write_links_step(app); assert(!app->linkscan);
    html = "<p><a href='https://example.org'>a</a><a href='#accent'>\xcc\x81</a> tail</p>";
    assert(tiku_richmarkup_read_html(html, strlen(html), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(!write_adopt(app, doc)); settle(app);
    generation = tiku_richdoc_generation(doc);
    write_action(app, W_LINK_NEXT); write_links_step(app); settle(app);
    tiku_richsession_range(tiku_richview_session(app->view), &first, &last);
    assert(first.byte == 0 && last.byte == 3);
    assert(app->link_match.first.byte == 0 && app->link_match.last.byte == 1);
    write_action(app, W_LINK_NEXT); write_links_step(app); settle(app);
    assert(app->link_match.first.byte == 1 && app->link_match.last.byte == 3);
    assert(strstr(app->status, "#accent"));
    write_action(app, W_LINK_NEXT); write_links_step(app);
    assert(strstr(app->status, "No next hyperlink"));
    write_action(app, W_LINK_PREV); write_links_step(app); settle(app);
    assert(app->link_match.first.byte == 0 && app->link_match.last.byte == 1);
    assert(strstr(app->status, "https://example.org"));
    assert(tiku_richdoc_generation(doc) == generation && !tiku_richdoc_dirty(doc));
    write_paint(app);
    write_action(app, W_FORMAT + TIKU_RICHCMD_BOLD);
    assert(tiku_richdoc_dirty(doc));
}

/** @brief Exercise bookmark panels, persistence, link targets and editing guards. */
static void
bookmarks_workflow(write_app_t *app, const char *home)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_pos_t a, b, target;
    tiku_richsession_t *session;
    char path[WRITE_PATH];
    uint64_t generation;
    unsigned notices;
    const char *html = "<p><a href='#Finish'>Go</a> here</p><p>Destination</p>";

    assert(tiku_richmarkup_read_html(html, strlen(html), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(!write_adopt(app, doc)); settle(app);
    session = tiku_richview_session(app->view);
    tiku_richview_move(app->view, TIKU_RICH_MOVE_FINISH, 0);
    tiku_richsession_selection(session, &a, &target);
    write_action(app, W_BOOKMARK_ADD);
    assert(app->panel == W_PANEL_BOOKMARK_ADD);
    tiku_field_start(&app->fields[0], "Finish");
    write_action(app, W_APPLY); settle(app);
    assert(app->panel == W_PANEL_NONE && strstr(app->status, "Bookmark saved"));
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_find_bookmark(snapshot, "Finish", &b) == TIKU_RICH_OK);
    assert(b.paragraph == target.paragraph && b.byte == target.byte);
    snprintf(path, sizeof path, "%s/bookmarks.tkrd", home);
    assert(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    tiku_richsnapshot_release(snapshot);
    generation = tiku_richdoc_generation(doc);
    tiku_richview_move(app->view, TIKU_RICH_MOVE_START, 0);
    write_action(app, W_LINK_NEXT); write_links_step(app); settle(app);
    assert(strstr(app->status, "#Finish"));
    write_action(app, W_LINK_FOLLOW); settle(app);
    tiku_richsession_selection(session, &a, &b);
    assert(a.paragraph == target.paragraph && a.byte == target.byte);
    assert(strstr(app->status, "Bookmark reached"));
    assert(tiku_richdoc_generation(doc) == generation);
    tiku_richsession_set_read_only(session, 1);
    write_action(app, W_BOOKMARK_REMOVE);
    assert(app->panel == W_PANEL_NONE && tiku_richdoc_generation(doc) == generation);
    write_action(app, W_BOOKMARK_GO);
    assert(!strcmp(app->fields[0].text, "Finish"));
    tiku_field_start(&app->fields[0], "Missing");
    write_action(app, W_APPLY);
    assert(app->panel == W_PANEL_BOOKMARK_GO && strstr(app->status, "No matching"));
    tiku_field_start(&app->fields[0], "Finish");
    write_action(app, W_APPLY); settle(app);
    assert(app->panel == W_PANEL_NONE);
    tiku_richsession_set_read_only(session, 0);
    write_action(app, W_BOOKMARK_REMOVE);
    write_action(app, W_APPLY); settle(app);
    assert(strstr(app->status, "Bookmark removed"));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK); settle(app);
    write_paint(app);
    assert(!strcmp(app->menu_bookmarks[0], "Finish"));
    tiku_richview_move(app->view, TIKU_RICH_MOVE_START, 0);
    write_action(app, W_BOOKMARK_ITEM); settle(app);
    tiku_richsession_selection(session, &a, &b);
    assert(b.paragraph == target.paragraph && b.byte == target.byte);
    write_action(app, W_BOOKMARK_ADD);
    tiku_field_start(&app->fields[0], "Stale");
    assert(tiku_richsession_type(session, 0, "x", 1) == TIKU_RICH_OK);
    write_action(app, W_APPLY);
    assert(app->panel == W_PANEL_BOOKMARK_ADD && strstr(app->status, "Document changed"));
    write_action(app, W_CANCEL);
    assert(tiku_richio_load(path, NULL, &doc) == TIKU_RICHIO_OK);
    assert(!write_adopt(app, doc)); settle(app);
    write_action(app, W_BOOKMARK_GO); write_action(app, W_APPLY); settle(app);
    assert(strstr(app->status, "Bookmark reached"));
    assert(!tiku_richdoc_dirty(doc));
}

/** @brief Resolve a raw combining-mark link and navigate scalar bookmarks visually. */
static void
bookmark_boundaries(write_app_t *app)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t paragraph;
    tiku_rich_pos_t a, b, point;
    tiku_richsession_t *session;
    unsigned notices;
    uint64_t generation;
    const char *html = "<p><a href='https://example.org'>a</a>"
        "<a href='#Accent'>\xcc\x81</a> tail</p><p>Destination</p>";

    assert(tiku_richmarkup_read_html(html, strlen(html), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 1, &paragraph);
    point = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), paragraph.id, 0, TIKU_RICH_BEFORE};
    assert(tiku_richdoc_bookmark_set(doc, 0, "Accent", point) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 0, &paragraph);
    point.paragraph = paragraph.id; point.byte = 1;
    assert(tiku_richdoc_bookmark_set(doc, 0, "Before", point) == TIKU_RICH_OK);
    point.affinity = TIKU_RICH_AFTER;
    assert(tiku_richdoc_bookmark_set(doc, 0, "After", point) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    assert(!write_adopt(app, doc)); settle(app);
    session = tiku_richview_session(app->view);
    generation = tiku_richdoc_generation(doc);
    write_action(app, W_LINK_NEXT); write_links_step(app);
    write_action(app, W_LINK_FOLLOW);
    assert(strstr(app->status, "not external URLs"));
    tiku_richsession_selection(session, &a, &b);
    assert(a.byte == 0 && b.byte == 3 && a.paragraph == paragraph.id);
    write_action(app, W_LINK_NEXT); write_links_step(app);
    assert(app->link_match.first.byte == 1 && app->link_match.last.byte == 3);
    write_action(app, W_LINK_FOLLOW); write_bookmarks_step(app);
    tiku_richsession_selection(session, &a, &b);
    assert(a.paragraph != paragraph.id && !a.byte && !b.byte);
    for (unsigned i = 0; i < 2; i++) {
        write_action(app, W_BOOKMARK_GO);
        tiku_field_start(&app->fields[0], i ? "After" : "Before");
        write_action(app, W_ZOOM_IN);
        write_action(app, W_APPLY);
        settle(app); write_bookmarks_step(app);
        tiku_richsession_selection(session, &a, &b);
        assert(a.paragraph == paragraph.id && a.byte == (i ? 3 : 0));
        assert(b.byte == a.byte && !app->bookmark_pending);
    }
    assert(tiku_richdoc_generation(doc) == generation);
    write_action(app, W_ZOOM_RESET); settle(app);
}

/** @brief Import external HTML with styling, safe omission and atomic refusal. */
static void
html_clipboard(write_app_t *app)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    uint64_t generation;
    const char *html = "<p><b>Bold</b> <a href='https://example.org'>link</a></p>";
    size_t bytes;
    const void *data;
    unsigned notices;

    typed_clip = app->clip_supported = 1;
    app->clip_formats = 2;
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(!write_adopt(app, doc)); settle(app);
    assert(tiku_clipboard_import_typed("Bold link", 9, "text/html", html, strlen(html)));
    write_action(app, W_PASTE); clipboard_reply(app);
    expect(app, "Bold link");
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 0, &p);
    assert(p.runs[0].chars.bold);
    assert(!strcmp(p.runs[p.run_count - 1].link, "https://example.org"));
    tiku_richsnapshot_release(snapshot);
    write_action(app, W_ALL); write_action(app, W_COPY); clipboard_reply(app);
    data = tiku_clipboard_find("text/html", &bytes);
    assert(data && bytes);
    assert(tiku_richmarkup_read_html(data, bytes, NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 0, &p);
    assert(p.length == 9 && p.runs[0].chars.bold);
    tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
    generation = tiku_richdoc_generation(app->doc);
    assert(tiku_clipboard_import_typed("bad", 3, "text/html", "\xff", 1));
    write_action(app, W_PASTE); clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    expect(app, "Bold link");
    {
        tiku_event_t paste = {.type = TIKU_EVENT_KEY_DOWN, .key = 'v',
            .modifiers = TIKU_MOD_CMD | TIKU_MOD_SHIFT};

        assert(shortcut(app, &paste) == W_PASTE_TEXT);
        event(app, &paste);
        assert(app->clip_action == W_PASTE_TEXT);
        clipboard_reply(app);
    }
    expect(app, "bad");
    assert(tiku_richdoc_undo(app->doc) == TIKU_RICH_OK); settle(app);
    expect(app, "Bold link");
    generation = tiku_richdoc_generation(app->doc);
    html = "<p>Text</p><table><tr><td>Cell</td></tr></table>";
    assert(tiku_clipboard_import_typed("Text Cell", 9, "text/html", html, strlen(html)));
    write_action(app, W_PASTE); clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    expect(app, "Bold link");
    html = "<p><script>alert(1)</script><i>Safe</i></p>";
    assert(tiku_clipboard_import_typed("Safe", 4, "text/html", html, strlen(html)));
    write_action(app, W_PASTE); clipboard_reply(app);
    expect(app, "Safe");
    assert(strstr(app->status, "Active content omitted"));
    assert(tiku_richdoc_undo(app->doc) == TIKU_RICH_OK); settle(app);
    expect(app, "Bold link");
    write_action(app, W_ALL);
    generation = tiku_richdoc_generation(app->doc);
    html = "<script>alert(1)</script>";
    assert(tiku_clipboard_import_typed("", 0, "text/html", html, strlen(html)));
    write_action(app, W_PASTE); clipboard_reply(app);
    assert(tiku_richdoc_generation(app->doc) == generation);
    expect(app, "Bold link");
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(!write_adopt(app, doc)); settle(app);
    html = "<table><tr><td><b>One</b></td><td>Two</td></tr></table>";
    assert(tiku_clipboard_import_typed("One\tTwo", 7, "text/html", html, strlen(html)));
    write_action(app, W_PASTE); clipboard_reply(app);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraphs(snapshot) == 4);
    tiku_richsnapshot_paragraph(snapshot, 1, &p);
    assert(p.length == 3 && p.runs[0].chars.bold);
    tiku_richsnapshot_release(snapshot);
    write_action(app, W_FORMAT + TIKU_RICHCMD_UNDO); settle(app);
    expect(app, "");
    app->clip_formats = 1;
    replace_document(app, "Legacy");
    write_action(app, W_ALL); write_action(app, W_COPY); clipboard_reply(app);
    assert(tiku_clipboard_data(NULL, NULL) && !tiku_clipboard_find("text/html", NULL));
    app->clip_formats = 2;
    tiku_clipboard_set(NULL);
}

/** @brief Run the app without a desktop or board under an isolated home. */
int
main(void)
{
    char home[] = "/tmp/tiku-write-XXXXXX", path[WRITE_PATH];
    write_app_t *app = NULL;
    const char *old_home = getenv("HOME");
    char *saved_home = old_home ? strdup(old_home) : NULL;

    assert(mkdtemp(home));
    assert(!setenv("HOME", home, 1));
    file_versions(home);
    snprintf(path, sizeof path, "%s/sample.tkrd", home);
    assert(!start((void **)&app, &services));
    settle(app);
    expect(app, "");
    assert(!write_dirty(app));
    write_action(app, W_OPEN);
    assert(!strcmp(picker_directory, home));
    write_picked(app, app->id, "");
    editing(app);
    styles(app);
    many_styles(app);
    file_safety(app, path);
    stale_open(app, path);
    app = recovery(app);
    page_workflow(app, home);
    pictures(app, home);
    tables(app, home);
    cell_navigation(app);
    rich_clipboard(app);
    rich_cell_clipboard(app);
    html_export(app, home);
    markup_files(app, home);
    outline_workflow(app);
    ruler_workflow(app);
    links_workflow(app);
    bookmarks_workflow(app, home);
    bookmark_boundaries(app);
    html_clipboard(app);
    windows(app);
    stop(app);
    if (saved_home) { assert(!setenv("HOME", saved_home, 1)); free(saved_home); }
    else unsetenv("HOME");
    printf("TikuWrite: editing, formatting, atomic Replace All, clipboard, "
           "saved-state race, conflicts, recovery, resize and close PASS "
           "(%d frames; fixture %s)\n", frames, home);
    return 0;
}
