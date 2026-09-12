/* TikuOS interface kit. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TEXT_LAYOUT_H_
#define TIKU_TEXT_LAYOUT_H_
#include <stddef.h>
#include "tiku_measure.h"

/* Bounded scratch space, shared by desktop and recording-side painters. */
#define TIKU_TEXT_LINE_MAX 256
/* Three dots also render on baked/board faces without a U+2026 glyph. */
#define TIKU_TEXT_ELLIPSIS "..."

/** @brief Fit one line, adding an ellipsis when necessary.
 * Returns nonzero if text was omitted. Output is always terminated when cap
 * is nonzero. Valid UTF-8 codepoints are never split; malformed bytes become
 * '?'. Newlines end a line. A box too small for an ellipsis stays empty.
 * Input and output must not overlap. This is not grapheme/IME layout.
 */
int tiku_text_elide(int face, const char *text, int width,
                    char *out, size_t cap);

/** @brief Read one wrapped line and advance *text, without allocation.
 * Returns 1 for a line (including explicit blank lines), 0 at end.
 * Space-delimited words wrap where possible; long words break at codepoints.
 * Set last for the final available line to show omitted text with an ellipsis.
 * The same bounded buffer contract as tiku_text_elide applies.
 */
int tiku_text_wrap_next(int face, const char **text, int width,
                        char *out, size_t cap, int last);
#endif
