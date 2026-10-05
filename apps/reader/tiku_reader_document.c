/* tiku_reader_document.c -- a local document read whole and parsed into a
 * page whose tables start sized to it and grow as it fills them.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_document.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tiku_markdown.h"

static int reader_parse(tiku_doc_t *doc, const char *name, const char *source);

/** @brief The allocator in force: the C library's unless a test set one. */
static tiku_reader_allocator_t hooks = { malloc, calloc };

void tiku_reader_test_allocator(const tiku_reader_allocator_t *allocator)
{
    if (allocator) {
        hooks = *allocator;
    } else {
        hooks.malloc_fn = malloc;
        hooks.calloc_fn = calloc;
    }
}

void *tiku_reader_malloc(size_t n)
{
    return hooks.malloc_fn(n);
}

void *tiku_reader_calloc(size_t n, size_t size)
{
    return hooks.calloc_fn(n, size);
}

int tiku_reader_markdown(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".md") || !strcasecmp(ext, ".markdown"));
}

/** @brief Parse @p source into @p doc, Markdown converted to HTML first
 *         within TIKU_READER_MEMORY_MAX.  The page and the folder search
 *         both parse through here.  @return 1, or 0 when it failed. */
static int reader_parse(tiku_doc_t *doc, const char *name, const char *source)
{
    if (!tiku_reader_markdown(name)) { tiku_doc_read(doc, source); return 1; }
    size_t cap = tiku_markdown_html_size(source);
    if (!cap || cap > TIKU_READER_MEMORY_MAX) return 0;
    char *html = tiku_reader_malloc(cap);
    if (!html) return 0;
    int ok = tiku_markdown_html(html, cap, source);
    if (ok) tiku_doc_read(doc, html);
    free(html); return ok;
}

void tiku_reader_doc_free(tiku_doc_t *doc)
{
    if (doc) free(doc->storage);
    free(doc);
}

/** @brief Parse @p source into a new page, its tables sized from its
 *         length; each table that fills is doubled and the parse run
 *         again, for at most 16 passes.  @p layout lays it out at a nominal
 *         width too.  @return the page, or NULL. */
static tiku_doc_t *reader_document(const char *name, const char *source,
                                   int layout)
{
    size_t n = strlen(source);
    tiku_doc_limits_t l = {
        (int)(n * 2 + 4096), (int)(n / 64 + 256),
        (int)(n / 32 + 1024), (int)(n / 32 + 1024),
        (int)(n / 16 + 2048), (int)(n / 128 + 256),
        (int)(n / 256 + 64), (int)(n / 1024 + 32)
    };
    tiku_doc_t *doc = tiku_reader_calloc(1, sizeof *doc);
    if (!doc) return NULL;
    tiku_doc_init(doc);
    /* A small document is tried first in the kit's inline buffers. */
    int small = n < TIKU_DOC_TEXT_MAX / 2;
    for (int pass = 0; pass < 16; pass++) {
        if (!small) {
            size_t size = tiku_doc_storage_size(&l);
            if (!size || size > TIKU_READER_MEMORY_MAX) break;
            void *memory = tiku_reader_malloc(size);
            if (!memory) break;
            free(doc->storage);
            if (!tiku_doc_init_storage(doc, &l, memory, size)) {
                free(memory); doc->storage = NULL; break;
            }
        }
        if (!reader_parse(doc, name, source)) break;
        if (layout) (void)tiku_doc_layout(doc, 256);
        unsigned full = doc->full & (TIKU_DOC_FULL_TEXT | TIKU_DOC_FULL_BLOCKS |
            TIKU_DOC_FULL_RUNS | TIKU_DOC_FULL_LINKS | TIKU_DOC_FULL_ANCHORS |
            TIKU_DOC_FULL_IMAGES | TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES);
        if (!full) { doc->laid_width = TIKU_DOC_NONE; return doc; }
        if (small) { small = 0; continue; }
        if (full & TIKU_DOC_FULL_TEXT) l.text *= 2;
        if (full & TIKU_DOC_FULL_BLOCKS) l.blocks *= 2;
        if (full & TIKU_DOC_FULL_RUNS) l.runs *= 2;
        if (full & TIKU_DOC_FULL_LINKS) l.links *= 2;
        if (full & TIKU_DOC_FULL_ANCHORS) l.anchors *= 2;
        if (full & TIKU_DOC_FULL_IMAGES) l.images *= 2;
        if (full & TIKU_DOC_FULL_LINES) l.lines *= 2;
        if (full & TIKU_DOC_FULL_PIECES) l.pieces *= 2;
    }
    tiku_reader_doc_free(doc);
    return NULL;
}

tiku_doc_t *tiku_reader_document(const char *name, const char *source)
{
    return reader_document(name, source, 1);
}

tiku_doc_t *tiku_reader_document_text(const char *name, const char *source)
{
    return reader_document(name, source, 0);
}

/** @brief A copy of @p old's text, blocks, runs, links, anchors and images
 *         in new storage, with twice the room in each of its line and
 *         piece tables that the last layout filled; @p old is unchanged.
 *  @return the copy, or NULL. */
static tiku_doc_t *reader_grow_layout(const tiku_doc_t *old)
{
    tiku_doc_limits_t limits = old->limits;
    if (old->full & TIKU_DOC_FULL_LINES) limits.lines *= 2;
    if (old->full & TIKU_DOC_FULL_PIECES) limits.pieces *= 2;
    size_t bytes = tiku_doc_storage_size(&limits);
    if (!bytes || bytes > TIKU_READER_MEMORY_MAX) return NULL;
    tiku_doc_t *doc = tiku_reader_calloc(1, sizeof *doc);
    void *storage = tiku_reader_malloc(bytes);
    if (!doc || !storage ||
        !tiku_doc_init_storage(doc, &limits, storage, bytes)) {
        free(doc); free(storage); return NULL;
    }
    /* Runs, headings, links and the selection are named by index into
     * these tables, and the copy keeps every index. */
#define COPY_TABLE(member, count) do { \
    doc->count = old->count; \
    memcpy(doc->member, old->member, \
           (size_t)old->count * sizeof *old->member); \
} while (0)
    COPY_TABLE(text, ntext); COPY_TABLE(block, nblock); COPY_TABLE(run, nrun);
    COPY_TABLE(link, nlink); COPY_TABLE(anchor, nanchor);
    COPY_TABLE(image, nimage);
#undef COPY_TABLE
    doc->title = old->title;
    doc->full = old->full;
    doc->image_fn = old->image_fn; doc->image_ctx = old->image_ctx;
    return doc;
}

void tiku_reader_document_layout(tiku_doc_t **current, int width)
{
    for (;;) {
        tiku_doc_t *doc = *current;
        (void)tiku_doc_layout(doc, width);
        if (!(doc->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES))) return;
        tiku_doc_t *grown = reader_grow_layout(doc);
        /* Without room to grow, the page keeps its capacity notice. */
        if (!grown) return;
        *current = grown;
        tiku_reader_doc_free(doc);
    }
}

char *tiku_reader_source(const char *path, const char **why)
{
    struct stat st;
    *why = "Could not read the document.";
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd); return NULL;
    }
    if ((uint64_t)st.st_size > TIKU_READER_FILE_MAX) {
        *why = "File exceeds the 8 MiB desktop safety limit; nothing was replaced.";
        close(fd); return NULL;
    }
    size_t cap = (size_t)st.st_size;
    char *source = tiku_reader_malloc(cap + 1);
    FILE *f = fdopen(fd, "rb");
    if (!f || !source) {
        if (f) fclose(f); else close(fd);
        free(source); return NULL;
    }
    size_t n = fread(source, 1, cap, f);
    int extra = fgetc(f), bad = ferror(f);
    fclose(f);
    if (bad || n != cap || extra != EOF || memchr(source, 0, n)) {
        *why = "Document changed during reading, or contains binary data; nothing was replaced.";
        free(source); return NULL;
    }
    source[n] = 0;
    return source;
}
