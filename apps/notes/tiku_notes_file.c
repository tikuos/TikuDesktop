/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notes_file.c - checked plain-text loading and snapshot save workers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_notes_private.h"
#include "tiku_atomic_file.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int
note_dirty(const note_t *n)
{
    return n->metadata != n->saved_metadata || tiku_richdoc_dirty(n->doc);
}

/** @brief Serialize the existing note header and LF-terminated paragraphs. */
static int
produce(FILE *file, void *context)
{
    note_save_t *save = context;
    size_t i, count = tiku_richsnapshot_paragraphs(save->snapshot);
    tiku_rich_paragraph_t paragraph;

    if (fprintf(file, "# tiku-note paper=%s important=%d\n",
                PAPERS[save->paper].name, save->important) < 0) return -1;
    for (i = 0; i < count; i++) {
        if (tiku_richsnapshot_paragraph(save->snapshot, i, &paragraph) !=
            TIKU_RICH_OK) return -1;
        if (fwrite(paragraph.text, 1, paragraph.length, file) !=
            paragraph.length || fputc('\n', file) == EOF) return -1;
    }
    return ferror(file) ? -1 : 0;
}

/** @brief Save captured state without accessing the live model or view. */
static void *
save_worker(void *context)
{
    note_save_t *save = context;

    save->result = tiku_atomic_file(save->path, produce, save);
    atomic_store_explicit(&save->done, 1, memory_order_release);
    return NULL;
}

int
note_save_start(note_t *n)
{
    note_save_t *save;

    if (n->save || !note_dirty(n)) return 0;
    save = calloc(1, sizeof *save);
    if (!save) goto refused;
    if (tiku_richdoc_snapshot(n->doc, &save->snapshot) != TIKU_RICH_OK) {
        free(save);
        goto refused;
    }
    snprintf(save->path, sizeof save->path, "%s", n->path);
    save->paper = n->paper;
    save->important = n->important;
    save->metadata = n->metadata;
    atomic_init(&save->done, 0);
    if (pthread_create(&save->thread, NULL, save_worker, save)) {
        tiku_richsnapshot_release(save->snapshot);
        free(save);
        goto refused;
    }
    n->save = save;
    return 0;
refused:
    snprintf(n->error, sizeof n->error, "Cannot start save; edits kept");
    return -1;
}

int
note_save_finish(note_t *n, int wait)
{
    note_save_t *save = n->save;
    int result;

    if (!save) return 0;
    if (!wait && !atomic_load_explicit(&save->done, memory_order_acquire))
        return 0;
    pthread_join(save->thread, NULL);
    result = save->result ? -1 : 1;
    if (result > 0) {
        tiku_richdoc_mark_saved(n->doc, save->snapshot);
        n->saved_metadata = save->metadata;
        n->error[0] = '\0';
    } else {
        snprintf(n->error, sizeof n->error,
                 "Save failed; edits kept. Retry Save.");
    }
    tiku_richsnapshot_release(save->snapshot);
    free(save);
    n->save = NULL;
    return result;
}

/** @brief Read a regular file completely within the note byte ceiling. */
static char *
read_note(const char *path, size_t *length)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    FILE *file;
    char *text;
    size_t size;

    if (fd < 0) return NULL;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 1 ||
        (uint64_t)info.st_size > NOTE_FILE_MAX) {
        close(fd);
        return NULL;
    }
    size = (size_t)info.st_size;
    text = malloc(size + 1);
    file = fdopen(fd, "r");
    if (!text || !file) {
        free(text);
        if (file) fclose(file); else close(fd);
        return NULL;
    }
    if (fread(text, 1, size, file) != size || fgetc(file) != EOF ||
        ferror(file) || memchr(text, 0, size)) {
        fclose(file);
        free(text);
        return NULL;
    }
    if (fclose(file)) { free(text); return NULL; }
    text[size] = '\0';
    *length = size;
    return text;
}

/** @brief Replace a blank note's text atomically through the shared session. */
static int
load_body(note_t *n, char *body, size_t length)
{
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_REPLACE_TEXT};
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t paragraph;
    tiku_rich_pos_t start;
    tiku_rich_status_t status;

    if (length && body[length - 1] == '\n') length--;
    if (length && body[length - 1] == '\r') length--;
    if (length > NOTE_FILE_MAX - 256u) return -1;
    command.value.text.data = body;
    command.value.text.length = length;
    status = tiku_richsession_execute(tiku_richview_session(n->view), 0,
                                       &command);
    if (status != TIKU_RICH_OK && status != TIKU_RICH_NO_CHANGE) return -1;
    if (tiku_richdoc_snapshot(n->doc, &snapshot) != TIKU_RICH_OK) return -1;
    tiku_richdoc_mark_saved(n->doc, snapshot);
    tiku_richsnapshot_paragraph(snapshot, 0, &paragraph);
    start = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), paragraph.id,
                              0, TIKU_RICH_AFTER};
    tiku_richsession_select(tiku_richview_session(n->view), 0, start, start);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_clear_history(n->doc);
    return 0;
}

int
note_load(note_t *n, const char *path)
{
    char *text, *body, *value;
    size_t length = 0;
    int paper = 0, important, i, result;

    if (strlen(path) >= sizeof n->path) return -1;
    text = read_note(path, &length);
    if (!text) return -1;
    body = strchr(text, '\n');
    if (!body || body - text > 255 || strncmp(text, "# tiku-note ", 12)) {
        free(text);
        return -1;
    }
    *body++ = '\0';
    value = strstr(text, "paper=");
    if (value) {
        for (i = 0; i < NPAPERS; i++) {
            size_t size = strlen(PAPERS[i].name);

            if (!strncasecmp(value + 6, PAPERS[i].name, size) &&
                (value[6 + size] == ' ' || value[6 + size] == '\0' ||
                 value[6 + size] == '\r')) paper = i;
        }
    }
    value = strstr(text, "important=");
    important = value && value[10] == '1';
    result = load_body(n, body, length - (size_t)(body - text));
    if (!result) {
        n->paper = paper;
        n->important = important;
        n->saved_metadata = n->metadata;
        snprintf(n->path, sizeof n->path, "%s", path);
    }
    free(text);
    return result;
}

int
note_recover(note_t *n, char *path, size_t capacity)
{
    static const char pattern[] = "/tmp/tiku-notes-recovery-XXXXXX";
    note_save_t save = {.paper = n->paper, .important = n->important};
    int fd, result;
    FILE *file;

    if (!path || capacity < sizeof pattern) return -1;
    memcpy(path, pattern, sizeof pattern);
    if (tiku_richdoc_snapshot(n->doc, &save.snapshot) != TIKU_RICH_OK)
        return -1;
    fd = mkstemp(path);
    file = fd < 0 ? NULL : fdopen(fd, "w");
    if (!file) {
        if (fd >= 0) { close(fd); unlink(path); }
        tiku_richsnapshot_release(save.snapshot);
        return -1;
    }
    result = produce(file, &save);
    if (fflush(file) || fsync(fd)) result = -1;
    if (fclose(file)) result = -1;
    tiku_richsnapshot_release(save.snapshot);
    if (result) { unlink(path); return -1; }
    fprintf(stderr, "Sticky Notes: unsaved %s recovered to %s\n", n->path,
            path);
    return 0;
}
