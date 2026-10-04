/* Reflow allocation failures must preserve parsed text and permit retry. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../../apps/reader/tiku_reader_document.h"

static int fail_malloc, fail_calloc;

/** @brief malloc, failing once when a check arms it. */
static void *reader_malloc(size_t n)
{
    if (fail_malloc) { fail_malloc = 0; return NULL; }
    return malloc(n);
}

/** @brief calloc, failing once when a check arms it. */
static void *reader_calloc(size_t n, size_t size)
{
    if (fail_calloc) { fail_calloc = 0; return NULL; }
    return calloc(n, size);
}

int main(void)
{
    static const tiku_reader_allocator_t failing = {
        reader_malloc, reader_calloc
    };
    tiku_reader_test_allocator(&failing);
    char *source = malloc(50032); assert(source);
    memcpy(source, "<p>", 3);
    for (int i = 0; i < 10000; i++) memcpy(source+3+i*5, "word ", 5);
    strcpy(source+50003, "TAIL_MARKER</p>");
    for (int mode = 0; mode < 2; mode++) {
        tiku_doc_t *doc = tiku_reader_document("large.html", source);
        assert(doc);
        tiku_doc_t *before = doc;
        int bytes = doc->ntext, runs = doc->nrun;
        if (mode) fail_calloc = 1; else fail_malloc = 1;
        tiku_reader_document_layout(&doc, 127);
        assert(!fail_calloc && !fail_malloc); /* the growth asked and failed */
        assert(doc == before && doc->ntext == bytes && doc->nrun == runs);
        assert(doc->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES));
        assert(tiku_doc_check(doc));
        tiku_reader_document_layout(&doc, 127);
        assert(!(doc->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES)));
        assert(doc->ntext == bytes && tiku_doc_check(doc));
        tiku_reader_doc_free(doc);
    }
    fail_calloc = 1; assert(!tiku_reader_document("large.html", source));
    tiku_reader_test_allocator(NULL);
    free(source);
    puts("PASS: Reader initial allocation failure, failed reflow growth, "
         "preserved text and successful retry");
    return 0;
}
