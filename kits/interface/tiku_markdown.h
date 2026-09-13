/* TikuOS kits. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_MARKDOWN_H_
#define TIKU_MARKDOWN_H_

#include <stddef.h>

/**
 * @brief Convert a documentation Markdown subset to the tiku_doc HTML dialect.
 *
 * Supports headings, paragraphs, emphasis, code, inline links/images, lists,
 * quotes, rules and pipe tables. Raw HTML is literal text. No I/O or execution.
 * Not a CommonMark implementation; see the help-reader documentation.
 * Input and output must not overlap. Returns 1 on success; on insufficient
 * capacity returns 0 and clears output, never handing back broken markup.
 */
int tiku_markdown_html(char *out, size_t capacity, const char *markdown);

/** @brief Exact output bytes including NUL; zero if the work bound is exceeded. */
size_t tiku_markdown_html_size(const char *markdown);

#endif
