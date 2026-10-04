/* tiku_reader_document.h -- desktop document loading and parsing interface.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_READER_DOCUMENT_H_
#define TIKU_READER_DOCUMENT_H_

#include "tiku_doc.h"

/* Desktop safety budgets; the embedded document kit keeps its own limits. */
#define TIKU_READER_FILE_MAX (8u * 1024u * 1024u)
#define TIKU_READER_MEMORY_MAX (64u * 1024u * 1024u)

#pragma GCC visibility push(hidden)
/* Document. */
int tiku_reader_markdown(const char *name);
void tiku_reader_doc_free(tiku_doc_t *doc);
tiku_doc_t *tiku_reader_document(const char *name, const char *source);
/* Parse without font measurement; safe for independent background searches. */
tiku_doc_t *tiku_reader_document_text(const char *name, const char *source);
/* Grow layout tables within the desktop budget without rereading the file. */
void tiku_reader_document_layout(tiku_doc_t **doc, int width);
char *tiku_reader_source(const char *path, const char **why);

/* The allocator the parser and the folder search draw from: the C library's
 * unless a test substitutes one that fails.  free() releases either. */
typedef struct {
    void *(*malloc_fn)(size_t n);              /* malloc                */
    void *(*calloc_fn)(size_t n, size_t size); /* calloc                */
} tiku_reader_allocator_t;
/* Substitute the allocator for a test; NULL restores the C library's. */
void tiku_reader_test_allocator(const tiku_reader_allocator_t *allocator);
/* Allocate from the allocator in force. */
void *tiku_reader_malloc(size_t n);
void *tiku_reader_calloc(size_t n, size_t size);

#pragma GCC visibility pop

#endif /* TIKU_READER_DOCUMENT_H_ */
