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
char *tiku_reader_source(const char *path, const char **why);

#pragma GCC visibility pop

#endif /* TIKU_READER_DOCUMENT_H_ */
