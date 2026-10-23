/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notes.c - plain-text sticky notes backed by the shared Rich View.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_notes_private.h"
#include "tiku_client.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** @brief Resolve or create the notes directory without truncating its path. */
static int
notes_dir(char *out, size_t capacity)
{
    const char *given = getenv("TIKU_NOTES"), *home = getenv("HOME");
    struct stat info;
    char parent[512];
    int length;

    if (given && *given) length = snprintf(out, capacity, "%s", given);
    else if (home && *home) {
        length = snprintf(parent, sizeof parent, "%s/.local", home);
        if (length < 0 || (size_t)length >= sizeof parent) return -1;
        if (mkdir(parent, 0777) && errno != EEXIST) return -1;
        length = snprintf(parent, sizeof parent, "%s/.local/share", home);
        if (length < 0 || (size_t)length >= sizeof parent) return -1;
        if (mkdir(parent, 0777) && errno != EEXIST) return -1;
        length = snprintf(out, capacity, "%s/notes", parent);
    } else return -1;
    if (length < 0 || (size_t)length >= capacity) return -1;
    if (stat(out, &info)) return mkdir(out, 0777) ? -1 : 0;
    return S_ISDIR(info.st_mode) ? 0 : -1;
}

/** @brief Find a live note by its nonzero window identifier. */
static note_t *
note_of(notes_t *st, uint32_t id)
{
    int i;

    for (i = 0; id && i < NOTES_MAX; i++)
        if (st->note[i].used && st->note[i].id == id) return &st->note[i];
    return NULL;
}

/** @brief Close the window and release a note after collecting its worker. */
static void
note_free(notes_t *st, note_t *n)
{
    note_save_finish(n, 1);
    if (n->id) st->services->close(st->services->ctx, n->id);
    tiku_surface_free(n->surface);
    tiku_richview_destroy(n->view);
    tiku_richdoc_release(n->doc);
    memset(n, 0, sizeof *n);
}

/** @brief Allocate a bounded document and view in an unused note slot. */
static note_t *
note_alloc(notes_t *st)
{
    tiku_rich_limits_t limits = {.text_bytes = NOTE_TEXT_MAX,
        .memory_bytes = 128u * 1024u * 1024u,
        .history_bytes = 16u * 1024u * 1024u};
    tiku_richlayout_options_t options = {.width = NOTE_W - 2 * MARGIN,
        .provider = tiku_richfont_provider(st->fonts)};
    note_t *n;
    int i;

    for (i = 0; i < NOTES_MAX && st->note[i].used; i++) {}
    if (i == NOTES_MAX) return NULL;
    n = &st->note[i];
    memset(n, 0, sizeof *n);
    if (tiku_richdoc_create(&limits, &n->doc) != TIKU_RICH_OK) return NULL;
    if (tiku_richview_create(n->doc, &options, &n->view) != TIKU_RICH_OK) {
        tiku_richdoc_release(n->doc);
        n->doc = NULL;
        return NULL;
    }
    n->used = 1;
    n->w = NOTE_W;
    n->h = NOTE_H;
    return n;
}

/** @brief Open a note window with matching viewport and initialized pixels. */
static int
note_show(notes_t *st, note_t *n)
{
    char title[128];
    int i;

    if (n->id) return 0;
    if (!n->surface)
        n->surface = tiku_surface_new(n->w, n->h, PAPERS[n->paper].face);
    if (!n->surface) return -1;
    note_title(n, title, sizeof title);
    n->id = st->services->open(st->services->ctx, title, n->w, n->h);
    if (!n->id) return -1;
    for (i = 0; i < NOTES_MAX; i++) {
        note_t *other = &st->note[i];

        if (other != n && other->used && other->active) {
            other->active = 0;
            note_paint(st, other);
        }
    }
    n->active = 1;
    tiku_richview_viewport(n->view, n->w - 2 * MARGIN, n->h - 2 * MARGIN, 1);
    if (st->services->sizeable)
        st->services->sizeable(st->services->ctx, n->id);
    note_step(st, n);
    note_refresh(st, n);
    return 0;
}

/** @brief Create a note at an exclusively reserved fresh filename. */
static note_t *
note_new(notes_t *st, int paper)
{
    note_t *n = note_alloc(st);
    int fd = -1, length;

    if (!n) return NULL;
    n->paper = paper;
    while (st->next_number < UINT_MAX) {
        length = snprintf(n->path, sizeof n->path, "%s/note-%03u.txt",
                          st->dir, ++st->next_number);
        if (length < 0 || (size_t)length >= sizeof n->path) break;
        fd = open(n->path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { note_free(st, n); return NULL; }
    close(fd);
    n->metadata = 1;
    if (note_show(st, n)) {
        unlink(n->path);
        note_free(st, n);
        return NULL;
    }
    note_save_start(n);
    return n;
}

/** @brief Republish note lists after title, importance or window changes. */
static void
notes_republish(notes_t *st)
{
    int i;

    for (i = 0; i < NOTES_MAX; i++)
        if (st->note[i].used) note_publish(st, &st->note[i]);
}

/** @brief Sort fixed-size filenames in ascending byte order. */
static int
name_compare(const void *a, const void *b)
{
    return strcmp(a, b);
}

/** @brief Load up to 24 note files, retaining refused files on disk. */
static int
notes_load(notes_t *st)
{
    DIR *dir = opendir(st->dir);
    struct dirent *entry;
    char names[NOTES_MAX][64], path[600];
    int count = 0, opened = 0, i;

    if (!dir) return 0;
    while ((entry = readdir(dir)) != NULL && count < NOTES_MAX) {
        size_t length = strlen(entry->d_name);

        if (length > 9 && length < 64 && !strncmp(entry->d_name, "note-", 5) &&
            !strcmp(entry->d_name + length - 4, ".txt"))
            memcpy(names[count++], entry->d_name, length + 1);
    }
    closedir(dir);
    qsort(names, (size_t)count, sizeof names[0], name_compare);
    for (i = 0; i < count; i++) {
        note_t *n = note_alloc(st);

        if (!n) break;
        snprintf(path, sizeof path, "%s/%.63s", st->dir, names[i]);
        if (note_load(n, path) || note_show(st, n)) {
            fprintf(stderr, "Sticky Notes: cannot open %s; file unchanged\n",
                    path);
            note_free(st, n);
        } else opened++;
    }
    return opened;
}

/** @brief Create native fonts, load saved notes and publish their windows. */
static int
notes_start(void **state, const tiku_app_services_t *services)
{
    notes_t *st;

    if (!state || !services || !services->open || !services->close ||
        !services->frame || !services->menus) return -1;
    *state = NULL;
    st = calloc(1, sizeof *st);
    if (!st) return -1;
    st->services = services;
    if (notes_dir(st->dir, sizeof st->dir) ||
        tiku_richfont_system(NULL, &st->fonts) != TIKU_RICH_OK) {
        free(st);
        return -1;
    }
    if (!notes_load(st) && !note_new(st, 0)) {
        tiku_richfont_release(st->fonts);
        free(st);
        return -1;
    }
    notes_republish(st);
    *state = st;
    return 0;
}

/** @brief Drain workers on host shutdown; recover text after a save failure. */
static void
notes_stop(void *state)
{
    notes_t *st = state;
    int i;

    if (!st) return;
    for (i = 0; i < NOTES_MAX; i++) {
        note_t *n = &st->note[i];
        uint32_t window;
        char recovery[64];

        if (!n->used) continue;
        window = n->id;
        n->id = 0;
        while (n->queue_count) {
            tiku_richview_step(n->view, 65536);
            note_step(st, n);
        }
        n->id = window;
        note_save_finish(n, 1);
        if (note_dirty(n)) {
            note_save_start(n);
            note_save_finish(n, 1);
        }
        if (note_dirty(n) && note_recover(n, recovery, sizeof recovery))
            fprintf(stderr, "Sticky Notes: save and recovery failed: %s\n",
                    n->path);
        note_free(st, n);
    }
    tiku_richfont_release(st->fonts);
    free(st);
}

/** @brief Resize by acquiring replacement pixels before releasing old ones. */
static void
note_resize(notes_t *st, note_t *n, int width, int height)
{
    tiku_surface_t *surface;

    if (width < NOTE_MIN_W) width = NOTE_MIN_W;
    if (height < NOTE_MIN_H) height = NOTE_MIN_H;
    if (width == n->w && height == n->h) return;
    surface = tiku_surface_new(width, height, PAPERS[n->paper].face);
    if (!surface) {
        snprintf(n->error, sizeof n->error, "Cannot allocate resized window");
        note_refresh(st, n);
        return;
    }
    tiku_surface_free(n->surface);
    n->surface = surface;
    n->w = width;
    n->h = height;
    tiku_richview_viewport(n->view, width - 2 * MARGIN, height - 2 * MARGIN, 1);
    note_step(st, n);
    note_refresh(st, n);
}

/** @brief Execute note lifecycle actions or the shared Edit commands. */
static int
notes_pick(void *state, uint32_t window, int command)
{
    notes_t *st = state;
    note_t *n = note_of(st, window);
    int i, pending = 0;

    if (!n) return 0;
    if (!n->queue_count) {
        tiku_event_t boundary = {.type = TIKU_EVENT_KEY_DOWN};
        int handled;

        /* Menu actions interrupt an incomplete committed-text transfer. */
        tiku_richview_event(n->view, &boundary, &handled);
    }
    if (command >= CMD_UNDO && command <= CMD_SELECT_ALL) {
        note_edit(st, n, command);
    } else if (command == CMD_QUIT) {
        for (i = 0; i < NOTES_MAX; i++) {
            note_t *other = &st->note[i];

            if (!other->used) continue;
            if (note_dirty(other) || other->save || other->queue_count) {
                pending = 1;
                note_save_start(other);
            }
        }
        st->quitting = pending;
        return !pending;
    } else if (command == CMD_NEW) {
        if (!note_new(st, n->paper))
            snprintf(n->error, sizeof n->error, "Cannot create another note");
    } else if (command == CMD_SAVE) {
        note_save_start(n);
    } else if (command == CMD_DELETE) {
        if (n->save) return 0;
        if (unlink(n->path) && errno != ENOENT)
            snprintf(n->error, sizeof n->error, "Cannot delete note file");
        else note_free(st, n);
    } else if (command == CMD_IMPORTANT ||
               (command >= CMD_COLOUR && command < CMD_COLOUR + NPAPERS)) {
        if (command == CMD_IMPORTANT) n->important = !n->important;
        else n->paper = command - CMD_COLOUR;
        n->metadata++;
        n->closing = 0;
        st->quitting = 0;
        note_save_start(n);
    } else if (command >= CMD_SHOW && command < CMD_SHOW + NOTES_MAX) {
        note_t *other = &st->note[command - CMD_SHOW];

        if (other->used && other->id && st->services->raise_window)
            st->services->raise_window(st->services->ctx, other->id);
    }
    if (n->used) note_refresh(st, n);
    notes_republish(st);
    return 0;
}

/** @brief Handle window state, application shortcuts and view-local input. */
static int
notes_event_in(void *state, uint32_t window, const tiku_event_t *event)
{
    notes_t *st = state;
    note_t *n = note_of(st, window);
    unsigned key = event->key;
    int command = 0;

    if (!n) return 0;
    if (event->type == TIKU_EVENT_RESIZE) {
        note_resize(st, n, event->width, event->height);
        return 0;
    }
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    if (event->type == TIKU_EVENT_KEY_DOWN && event->modifiers & TIKU_MOD_CMD) {
        if (key == 'n') command = CMD_NEW;
        if (key == 's') command = CMD_SAVE;
        if (key == 'q') command = CMD_QUIT;
        if (key == 'c') command = CMD_COPY;
        if (key == 'x') command = CMD_CUT;
        if (key == 'v') command = CMD_PASTE;
        if (command) return notes_pick(st, window, command);
    }
    if (event->type == TIKU_EVENT_ACTIVATED) {
        int i;

        for (i = 0; i < NOTES_MAX; i++) {
            note_t *other = &st->note[i];

            if (other != n && other->used && other->active) {
                other->active = 0;
                note_paint(st, other);
            }
        }
        n->active = 1;
    }
    if (event->type == TIKU_EVENT_DEACTIVATED) n->active = 0;
    if (event->type == TIKU_EVENT_EXPOSE ||
        event->type == TIKU_EVENT_ACTIVATED ||
        event->type == TIKU_EVENT_CLIPBOARD)
        note_refresh(st, n);
    else note_input(st, n, event);
    return 0;
}

/** @brief Advance layout, collect saves and close successfully saved notes. */
static void
notes_tick(void *state, int64_t now_us)
{
    notes_t *st = state;
    int i, pending = 0, result, changed = 0;

    for (i = 0; i < NOTES_MAX; i++) {
        note_t *n = &st->note[i];

        if (!n->used) continue;
        note_step(st, n);
        result = note_save_finish(n, 0);
        if (result < 0) { n->closing = 0; st->quitting = 0; }
        if (result) { note_refresh(st, n); changed = 1; }
        if (note_dirty(n) && !n->save &&
            (n->closing || st->quitting ||
             now_us - n->edited_at >= SAVE_AFTER_US) &&
            now_us - n->attempted_at >= SAVE_AFTER_US) {
            n->attempted_at = now_us;
            if (note_save_start(n)) { n->closing = 0; st->quitting = 0; }
        }
        if (note_dirty(n) || n->save || n->queue_count) pending = 1;
        else if (n->closing) { note_free(st, n); changed = 1; }
    }
    if (st->quitting && !pending) {
        for (i = 0; i < NOTES_MAX; i++)
            if (st->note[i].used) note_free(st, &st->note[i]);
        st->quitting = 0;
    }
    if (changed) notes_republish(st);
}

/** @brief Keep dirty notes alive while saving after a host-initiated close. */
static void
notes_closed(void *state, uint32_t window)
{
    notes_t *st = state;
    note_t *n = note_of(st, window);

    if (!n) return;
    n->id = 0;
    if (note_dirty(n) || n->save || n->queue_count) {
        n->closing = 1;
        note_save_start(n);
        if (note_show(st, n))
            fprintf(stderr, "Sticky Notes: window closed with pending save\n");
    } else note_free(st, n);
    notes_republish(st);
}

/** @brief Sticky Notes' application lifecycle and event descriptor. */
const tiku_app_descriptor_t tiku_notes_app = {
    .id = "org.tikuos.notes", .name = "Sticky Notes", .start = notes_start,
    .stop = notes_stop, .tick = notes_tick, .pick = notes_pick,
    .event_in = notes_event_in, .closed = notes_closed
};

#ifdef TIKU_APP_SO
/** @brief Versioned descriptor exported by the application module. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_notes_app
};
#endif

#ifndef TIKU_APP_EMBED
/** @brief Run Sticky Notes as a desktop client. */
int
main(void)
{
    return tiku_client_run(&tiku_notes_app);
}
#endif
