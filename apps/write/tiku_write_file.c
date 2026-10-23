/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_file.c - version-checked document workers and atomic replacement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include "tiku_crypto.h"
#include "tiku_zip_write.h"
#include "tiku_sys.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
write_version(const char *path, write_version_t *out)
{
    struct stat after;
    tiku_hash_t hash;
    unsigned char buffer[16384];
    ssize_t count;
    size_t bytes = 0;
    int fd, ok = 1;

    memset(out, 0, sizeof *out);
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &out->stat) || !S_ISREG(out->stat.st_mode) ||
        out->stat.st_size < 0 ||
        (uint64_t)out->stat.st_size > WRITE_FILE_MAX) ok = 0;
    tiku_hash_init(&hash, TIKU_HASH_SHA256);
    while (ok) {
        count = read(fd, buffer, sizeof buffer);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = 0; break; }
        if (!count) break;
        bytes += (size_t)count;
        if (bytes > WRITE_FILE_MAX) { ok = 0; break; }
        tiku_hash_update(&hash, buffer, (size_t)count);
    }
    if (ok && (fstat(fd, &after) || !tiku_sys_stat_same(&out->stat, &after) ||
        bytes != (size_t)out->stat.st_size || lstat(path, &after) ||
        !tiku_sys_stat_same(&out->stat, &after))) ok = 0;
    close(fd);
    if (!ok) return -1;
    tiku_hash_final(&hash, out->hash);
    out->exists = 1;
    return 0;
}

int
write_version_equal(const write_version_t *a, const write_version_t *b)
{
    return a->exists == b->exists && (!a->exists ||
        (tiku_sys_stat_same(&a->stat, &b->stat) &&
         !memcmp(a->hash, b->hash, 32)));
}

/** @brief Guard replacement against cancellation and external file changes. */
static int
check(void *context)
{
    write_job_t *job = context;
    write_version_t actual;

    return atomic_load(&job->cancel) || write_version(job->path, &actual) ||
           (actual.exists && !(actual.stat.st_mode & 0222)) ||
           !write_version_equal(&actual, &job->expected);
}

/** @brief Cooperative cancellation read from the worker's private job. */
static int
cancelled(void *context)
{
    return atomic_load(&((write_job_t *)context)->cancel);
}

/** @brief Paginate and export with a worker-owned native font context. */
static int
produce_pdf(FILE *out, write_job_t *job)
{
    tiku_richfont_t *fonts = NULL;
    tiku_richpages_t *pages = NULL;
    tiku_richlayout_options_t options = {.leading = 3};
    tiku_rich_status_t status;
    unsigned char *data = NULL;
    size_t length = 0;
    int failed = 1;

    if (write_fonts(&fonts)) return -1;
    options.provider = tiku_richfont_provider(fonts);
    status = tiku_richpages_create(job->snapshot, &options, 0, &pages);
    if (status == TIKU_RICH_OK) {
        do {
            status = tiku_richpages_step(pages, 8192);
        } while (status == TIKU_RICH_PENDING && !cancelled(job));
    }
    if (status == TIKU_RICH_OK && !cancelled(job))
        status = tiku_richoutput_pdf(fonts, pages, cancelled, job,
                                     &data, &length);
    if (status == TIKU_RICH_OK && data)
        failed = fwrite(data, 1, length, out) != length;
    free(data);
    tiku_richpages_release(pages);
    tiku_richfont_release(fonts);
    return failed || cancelled(job) ? -1 : 0;
}

/** @brief Serialize the captured snapshot without accessing UI state. */
static int
payload(FILE *out, void *context)
{
    write_job_t *job = context;
    tiku_richio_options_t options = {.cancel = cancelled, .ctx = job};
    unsigned char *xml = NULL;
    size_t length = 0;
    tiku_zip_entry_t entry;
    int failed;

    if (job->kind == W_JOB_PDF) return produce_pdf(out, job);
    if (job->kind == W_JOB_HTML || job->kind == W_JOB_MARKDOWN) {
        job->status = job->kind == W_JOB_HTML ?
            tiku_richhtml_write(job->snapshot, &options,
                               &xml, &length, &job->notices) :
            tiku_richmarkup_write_markdown(job->snapshot, &options,
                                           &xml, &length, &job->notices);
        if (job->status != TIKU_RICHIO_OK) return -1;
        failed = fwrite(xml, 1, length, out) != length;
        free(xml);
        return failed || cancelled(job) ? -1 : 0;
    }
    if (job->kind == W_JOB_TEXT) {
        for (size_t i = 0; i < tiku_richsnapshot_paragraphs(job->snapshot);
             i++) {
            tiku_rich_paragraph_t paragraph;

            if (cancelled(job) || tiku_richsnapshot_paragraph(job->snapshot,
                i, &paragraph) != TIKU_RICH_OK) return -1;
            if (i && fputc('\n', out) == EOF) return -1;
            size_t at = 0;

            for (size_t r = 0; r < paragraph.run_count; r++) {
                const tiku_rich_run_t *run = &paragraph.runs[r];

                if (run->image) {
                    if (fputs("[Image: ", out) == EOF ||
                        fputs(run->image->alt, out) == EOF ||
                        fputc(']', out) == EOF) return -1;
                } else if (fwrite(paragraph.text + at, 1, run->length, out) !=
                           run->length) return -1;
                at += run->length;
            }
        }
        return 0;
    }
    job->status = tiku_richio_write_xml(job->snapshot, &options, &xml, &length);
    if (job->status != TIKU_RICHIO_OK) return -1;
    entry = (tiku_zip_entry_t){"document.xml", xml, length};
    failed = tiku_zip_write(out, &entry, 1) != TIKU_ARCHIVE_OK;
    free(xml);
    return failed || cancelled(job) ? -1 : 0;
}

/** @brief Digest exactly the temporary file before allowing replacement. */
static int
produce(FILE *out, void *context)
{
    write_job_t *job = context;
    tiku_hash_t hash;
    unsigned char buffer[16384];
    off_t at = 0;
    ssize_t length;

    if (payload(out, context) || fflush(out)) return -1;
    tiku_hash_init(&hash, TIKU_HASH_SHA256);
    for (;;) {
        length = pread(fileno(out), buffer, sizeof buffer, at);
        if (length < 0 && errno == EINTR) continue;
        if (length < 0 || cancelled(job)) return -1;
        if (!length) break;
        at += length;
        if ((uint64_t)at > WRITE_FILE_MAX) return -1;
        tiku_hash_update(&hash, buffer, (size_t)length);
    }
    tiku_hash_final(&hash, job->written_hash);
    return 0;
}

/** @brief Read bounded UTF-8 text, HTML or Markdown as a new document. */
static tiku_richio_status_t
load_text(write_job_t *job, const char *extension)
{
    int fd = open(job->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    char *text;
    size_t length = (size_t)job->expected.stat.st_size, at = 0;
    tiku_richio_options_t options = {.cancel = cancelled, .ctx = job};
    tiku_richio_status_t status;

    if (fd < 0) return TIKU_RICHIO_IO;
    if (length > (64u << 20)) { close(fd); return TIKU_RICHIO_LIMIT; }
    text = malloc(length + 1);
    if (!text) { close(fd); return TIKU_RICHIO_NO_MEMORY; }
    while (at < length && !cancelled(job)) {
        ssize_t n = read(fd, text + at, length - at);

        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        at += (size_t)n;
    }
    close(fd);
    text[at] = 0;
    status = cancelled(job) ? TIKU_RICHIO_CANCELLED : at != length ?
        TIKU_RICHIO_IO : TIKU_RICHIO_OK;
    if (status == TIKU_RICHIO_OK) {
        if (!strcasecmp(extension, ".html") || !strcasecmp(extension, ".htm"))
            status = tiku_richmarkup_read_html(text, length, &options,
                                               &job->loaded, &job->notices);
        else if (!strcasecmp(extension, ".md") || !strcasecmp(extension, ".markdown"))
            status = tiku_richmarkup_read_markdown(text, length, &options,
                                                   &job->loaded, &job->notices);
        else status = tiku_richio_read_text(text, length, &options, &job->loaded);
    }
    free(text);
    job->imported = 1;
    return status;
}

/** @brief Load native packages or explicitly named text/markup inputs. */
static void
load_job(write_job_t *job)
{
    const char *ext = strrchr(job->path, '.');
    tiku_richio_options_t options = {.cancel = cancelled, .ctx = job};

    if (write_version(job->path, &job->expected) || !job->expected.exists) {
        job->status = TIKU_RICHIO_IO;
        return;
    }
    if (ext && (!strcasecmp(ext, ".txt") || !strcasecmp(ext, ".html") ||
        !strcasecmp(ext, ".htm") || !strcasecmp(ext, ".md") ||
        !strcasecmp(ext, ".markdown"))) job->status = load_text(job, ext);
    else job->status = tiku_richio_load(job->path, &options, &job->loaded);
    if (job->status == TIKU_RICHIO_OK &&
        (write_version(job->path, &job->actual) ||
         !write_version_equal(&job->actual, &job->expected)))
        job->status = TIKU_RICHIO_IO;
    if (job->status != TIKU_RICHIO_OK) {
        tiku_richdoc_release(job->loaded);
        job->loaded = NULL;
    }
}

/** @brief Execute blocking I/O and publish outputs before completion. */
static void *
worker(void *context)
{
    write_job_t *job = context;

    if (job->kind == W_JOB_LOAD) load_job(job);
    else if (job->kind == W_JOB_IMAGE) {
        job->status = tiku_image_load(job->path, &job->image) == TIKU_IMG_OK ?
            TIKU_RICHIO_OK : TIKU_RICHIO_DAMAGED;
        if (job->status == TIKU_RICHIO_OK &&
            (size_t)job->image.w > TIKU_RICH_IMAGE_PIXELS_MAX /
                                   (unsigned)job->image.h)
            job->status = TIKU_RICHIO_LIMIT;
        if (cancelled(job)) job->status = TIKU_RICHIO_CANCELLED;
    }
    else if (job->kind == W_JOB_PROBE)
        job->status = write_version(job->path, &job->actual) ?
            TIKU_RICHIO_IO : TIKU_RICHIO_OK;
    else {
        if (job->kind == W_JOB_RECOVERY &&
            write_version(job->path, &job->expected))
            job->result = TIKU_ATOMIC_FAILED;
        else
            job->result = tiku_atomic_file_guarded(job->path,
                                                  produce, check, job);
        if (job->result == TIKU_ATOMIC_OK &&
            (write_version(job->path, &job->actual) ||
             memcmp(job->actual.hash, job->written_hash, 32)))
            job->result = TIKU_ATOMIC_UNCERTAIN;
    }
    atomic_store_explicit(&job->done, 1, memory_order_release);
    return NULL;
}

int
write_job_start(write_app_t *app, int kind, const char *path,
                const write_version_t *expected, int recovered)
{
    write_job_t *job;

    if (app->job || !path || !*path || strlen(path) >= WRITE_PATH) return -1;
    job = calloc(1, sizeof *job);
    if (!job) return -1;
    atomic_init(&job->done, 0);
    atomic_init(&job->cancel, 0);
    job->kind = kind;
    job->recovered = recovered;
    strcpy(job->path, path);
    job->generation = tiku_richdoc_generation(app->doc);
    if (expected) job->expected = *expected;
    job->first = app->pick_first;
    job->last = app->pick_last;
    if (kind != W_JOB_LOAD && kind != W_JOB_PROBE && kind != W_JOB_IMAGE &&
        tiku_richdoc_snapshot(app->doc,
        &job->snapshot) != TIKU_RICH_OK) { free(job); return -1; }
    if (pthread_create(&job->thread, NULL, worker, job)) {
        tiku_richsnapshot_release(job->snapshot);
        free(job);
        return -1;
    }
    app->job = job;
    app->repaint = 1;
    return 0;
}

/** @brief Adopt a loaded document only if the visible state was not edited. */
static void
finish_load(write_app_t *app, write_job_t *job)
{
    if (job->status != TIKU_RICHIO_OK) {
        snprintf(app->status, sizeof app->status, "%s",
                 tiku_richio_strerror(job->status));
    } else if (job->generation != tiku_richdoc_generation(app->doc)) {
        snprintf(app->status, sizeof app->status,
                 "Open cancelled: the document changed while loading.");
    } else if (write_adopt(app, job->loaded)) {
        snprintf(app->status, sizeof app->status, "Not enough memory to open.");
    } else {
        job->loaded = NULL;
        app->version = job->actual;
        app->imported = job->imported || job->recovered;
        snprintf(app->path, sizeof app->path, "%s",
                 app->imported ? "" : job->path);
        snprintf(app->status, sizeof app->status, "%s",
            app->imported ? "Opened a copy. Save as a TikuWrite document." :
                            "Opened document.");
        if (job->notices) {
            char report[192];

            tiku_richmarkup_report(job->notices, report, sizeof report);
            snprintf(app->status, sizeof app->status,
                     "Opened a copy. %s. Save as .tkrd.", report);
        }
        if (app->imported) app->recovered_generation = 0;
    }
}

int
write_job_finish(write_app_t *app, int wait)
{
    write_job_t *job = app->job;
    int success = 0, pending = 0, probe_save = 0;

    if (!job || (!wait && !atomic_load_explicit(&job->done,
        memory_order_acquire))) return 0;
    pthread_join(job->thread, NULL);
    app->job = NULL;
    if (job->kind == W_JOB_LOAD) finish_load(app, job);
    else if (job->kind == W_JOB_IMAGE) write_picture_finish(app, job);
    else if (job->kind == W_JOB_PROBE) {
        if (job->status != TIKU_RICHIO_OK) {
            snprintf(app->status, sizeof app->status,
                     "Cannot use this destination. "
                     "Choose a regular local file.");
        } else {
            strcpy(app->target, job->path);
            app->target_version = job->actual;
            if (job->actual.exists) app->prompt = W_PROMPT_OVERWRITE;
            else probe_save = app->pick_kind == W_EXPORT_TEXT ? W_JOB_TEXT :
                app->pick_kind == W_EXPORT_HTML ? W_JOB_HTML :
                app->pick_kind == W_EXPORT_MARKDOWN ? W_JOB_MARKDOWN :
                app->pick_kind == W_EXPORT_PDF ? W_JOB_PDF : W_JOB_SAVE;
        }
    }
    else if (job->result == TIKU_ATOMIC_OK) {
        success = 1;
        if (job->kind == W_JOB_RECOVERY)
            app->recovered_generation = job->generation;
        else if (job->kind == W_JOB_SAVE) {
            tiku_richdoc_mark_saved(app->doc, job->snapshot);
            strcpy(app->path, job->path);
            app->version = job->actual;
            app->imported = 0;
            if (!write_dirty(app)) {
                if (*app->recovery) unlink(app->recovery);
                pending = app->pending;
                app->pending = 0;
                app->prompt = 0;
            }
            snprintf(app->status, sizeof app->status, "%s",
                write_dirty(app) ? "Saved snapshot; newer edits remain." :
                                   "Saved.");
        } else if (job->kind == W_JOB_MARKDOWN) {
            char report[192];

            tiku_richmarkup_report(job->notices, report, sizeof report);
            snprintf(app->status, sizeof app->status, "Exported Markdown. %s.", report);
        } else if (job->kind == W_JOB_HTML) {
            snprintf(app->status, sizeof app->status,
                "Exported HTML. Browser controls layout.%s%s",
                job->notices & TIKU_RICHHTML_STYLES ?
                    " Named styles become CSS." : "",
                job->notices & TIKU_RICHHTML_BOOKMARKS ?
                    " Bookmark targets omitted." : "");
        } else snprintf(app->status, sizeof app->status, "%s",
            job->kind == W_JOB_PDF ? "Exported PDF." :
            "Exported plain text; pictures use alternate text, "
            "formatting is not included.");
    } else if (job->kind != W_JOB_RECOVERY &&
               job->result == TIKU_ATOMIC_CONFLICT) {
        app->prompt = W_PROMPT_CONFLICT;
        snprintf(app->status, sizeof app->status,
                 "The file changed outside TikuWrite. Use Save As.");
    } else {
        snprintf(app->status, sizeof app->status, "%s",
            job->result == TIKU_ATOMIC_UNCERTAIN ?
            "File replaced; durability could not be confirmed. Keep a copy." :
            "Save failed. Your document and recovery data are retained.");
        app->pending = 0;
    }
    tiku_richsnapshot_release(job->snapshot);
    tiku_richdoc_release(job->loaded);
    tiku_image_free(&job->image);
    free(job);
    app->repaint = 1;
    if (probe_save && !wait && write_job_start(app, probe_save, app->target,
                                              &app->target_version, 0))
        snprintf(app->status, sizeof app->status, "Could not start saving.");
    if (pending && !wait) write_action(app, pending);
    return success ? 1 : -1;
}
