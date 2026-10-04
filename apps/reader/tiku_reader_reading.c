/* tiku_reader_reading.c -- reading.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static int reader_reserve(struct help_reader *r, size_t text, int runs, int blocks);
static void reader_reveal(tiku_reader_help_window_t *c, int offset);

/** @brief Reserve matching search/selection/outline storage before page commit. */
static int reader_reserve(struct help_reader *r, size_t text, int runs, int blocks)
{
    if (!r) return 1;
    if (r->text_cap >= text && r->starts_cap >= runs && r->folded_cap >= blocks)
        return 1;
    char *words = malloc(text);
    int *starts = calloc((size_t)runs, sizeof *starts);
    unsigned char *folded = calloc((size_t)blocks, 1);
    if (!words || !starts || !folded) {
        free(words); free(starts); free(folded); return 0;
    }
    free(r->text); free(r->starts); free(r->folded);
    r->text = words; r->starts = starts; r->folded = folded;
    r->text_cap = text; r->starts_cap = runs; r->folded_cap = blocks;
    return 1;
}

/** @brief The old page survives both parser and reading-tool allocation failure. */
int tiku_reader_replace(tiku_reader_help_window_t *c, const char *name,
                           const char *source)
{
    tiku_doc_t *doc = tiku_reader_document(name, source);
    if (!doc) {
        tiku_reader_say(c, "Document exceeds the rendering memory/complexity budget, or memory is unavailable.");
        return 0;
    }
    size_t text = (size_t)doc->ntext + doc->nblock + 1;
    if (text < TIKU_DOC_TEXT_MAX * 2u) text = TIKU_DOC_TEXT_MAX * 2u;
    int runs = doc->nrun > TIKU_DOC_RUNS_MAX ? doc->nrun : TIKU_DOC_RUNS_MAX;
    int blocks = doc->nblock + 1 > TIKU_DOC_BLOCKS_MAX ? doc->nblock + 1 : TIKU_DOC_BLOCKS_MAX;
    if (!reader_reserve(c->reader, text, runs, blocks)) {
        tiku_reader_doc_free(doc); tiku_reader_say(c, "Not enough memory for document reading tools."); return 0;
    }
    tiku_reader_doc_free(c->doc); c->doc = doc;
    return 1;
}

/** @brief Capture the logical reading location before another page replaces it. */
tiku_reader_help_position_t tiku_reader_position(tiku_reader_help_window_t *c)
{
    tiku_reader_help_position_t at = {.pixel = c->bar.value};
    if (c->doc) {
        at.width = c->doc->laid_width; at.height = c->doc->laid_height;
        at.place = tiku_doc_place(c->doc, c->bar.value);
    }
    return at;
}

/** @brief Optional tools fail closed when memory is unavailable. */
void tiku_reader_init(tiku_reader_help_window_t *c)
{
    c->reader = calloc(1, sizeof *c->reader);
    if (!c->reader) return;
    if (!reader_reserve(c->reader, TIKU_DOC_TEXT_MAX * 2u,
                        TIKU_DOC_RUNS_MAX, TIKU_DOC_BLOCKS_MAX)) {
        free(c->reader); c->reader = NULL; return;
    }
    tiku_field_start(&c->reader->query, "");
    tiku_field_input_init(&c->reader->input);
    tiku_field_start(&c->reader->location, "");
    tiku_field_input_init(&c->reader->location_input);
    tiku_list_init(&c->reader->list, 1);
    c->reader->match = c->reader->block = -1;
    tiku_reader_bookmarks_load(c);
}

/** @brief Free per-page images while retaining history and bookmarks. */
void tiku_reader_images_clear(struct help_reader *r)
{
    for (int i = 0; i < r->nimages; i++) tiku_image_free(&r->images[i].image);
    free(r->images); r->images = NULL;
    r->nimages = r->image_cap = 0; r->image_bytes = r->image_clock = 0;
}

/** @brief Flatten visible runs, retaining offsets for search and selection. */
int tiku_reader_text(const tiku_doc_t *d, char *out, size_t cap, int *starts)
{
    size_t n = 0;
    for (int i = 0; i < d->nblock; i++) {
        const tiku_doc_block_t *b = &d->block[i];
        for (int k = b->run0; k < b->run0 + b->nrun; k++) {
            const tiku_doc_run_t *run = &d->run[k];
            if (starts) starts[k] = (int)n;
            if (n + run->len + 2 >= cap) { out[n] = 0; return (int)n; }
            memcpy(out + n, d->text + run->off, run->len); n += run->len;
        }
        if (n && out[n-1] != '\n' && n + 1 < cap) out[n++] = '\n';
    }
    out[n] = 0; return (int)n;
}

/** @brief One new page invalidates selection, image pixels and match positions. */
void tiku_reader_loaded(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader; if (!r) return;
    tiku_reader_images_clear(r);
    memset(r->folded, 0, (size_t)r->folded_cap);
    r->follow_outline = 1;
    c->anchor[0] = 0;
    r->length = tiku_reader_text(c->doc, r->text, r->text_cap, r->starts);
    r->a = r->b = r->dragging = 0; r->match = r->block = -1;
    r->match_len = r->matches = 0; r->hovered[0] = 0;
    tiku_doc_images(c->doc, tiku_reader_image_size, c);
}

/** @brief ASCII case-insensitive, with exact UTF-8 matching for other scripts. */
int tiku_reader_match(const char *text, const char *query)
{
    while (*query) {
        if (!*text || tolower((unsigned char)*text++) !=
            tolower((unsigned char)*query++)) return 0;
    }
    return 1;
}

/** @brief Scroll an indexed text offset into view without relying on pixels. */
static void reader_reveal(tiku_reader_help_window_t *c, int offset)
{
    struct help_reader *r = c->reader;
    for (int i = 0; i < c->doc->npiece; i++) {
        const tiku_doc_piece_t *pc = &c->doc->piece[i];
        int start = r->starts[pc->run] + pc->off - c->doc->run[pc->run].off;
        if (offset >= start && offset < start + pc->len) {
            tiku_rect_t box = tiku_doc_piece_rect(c->doc, i);
            tiku_scroll_to(&c->bar, box.y); return;
        }
    }
}

int tiku_reader_help_window_find(tiku_reader_help_window_t *c, const char *query, int backwards)
{
    struct help_reader *r = c->reader;
    if (!r || !c->doc || !query || strlen(query) >= TIKU_FIELD_MAX) return 0;
    int prior = strcmp(query, r->query.text) ? -1 : r->match;
    char copy[TIKU_FIELD_MAX]; snprintf(copy, sizeof copy, "%s", query);
    tiku_field_start(&r->query, copy); r->match_len = (int)strlen(copy);
    r->matches = 0; int first = -1, last = -1, chosen = -1, ordinal = 0;
    for (int i = 0; r->match_len && i + r->match_len <= r->length; i++) {
        if (!tiku_reader_match(r->text + i, copy)) continue;
        r->matches++; if (first < 0) first = i; last = i;
        if ((!backwards && i > prior && chosen < 0) ||
            (backwards && prior >= 0 && i < prior)) chosen = i;
    }
    if (chosen < 0) chosen = backwards ? last : first;
    r->match = chosen;
    for (int i = 0; chosen >= 0 && i <= chosen; i++)
        if (tiku_reader_match(r->text + i, copy)) ordinal++;
    if (chosen >= 0) reader_reveal(c, chosen);
    snprintf(c->status, sizeof c->status, chosen < 0 ? "No matches." : "Match %d of %d", ordinal, r->matches);
    if (c->dirty) *c->dirty = 1;
    return chosen >= 0;
}

/** @brief Copy only whole accepted text; the clipboard refuses oversized ranges. */
int tiku_reader_help_window_copy(tiku_reader_help_window_t *c, int code)
{
    struct help_reader *r = c->reader; if (!r || !c->doc) return 0;
    int a = r->a < r->b ? r->a : r->b, b = r->a < r->b ? r->b : r->a;
    if (code) {
        int block = r->block;
        if (block < 0 || c->doc->block[block].kind != TIKU_DOC_BLOCK_PRE) {
            block = -1;
            for (int i = 0; i < c->doc->nblock; i++)
                if (c->doc->block[i].kind == TIKU_DOC_BLOCK_PRE &&
                    c->doc->block[i].y + c->doc->block[i].h >= c->bar.value &&
                    c->doc->block[i].y < c->bar.value + c->page_h) { block = i; break; }
        }
        if (block < 0 || !c->doc->block[block].nrun) { tiku_reader_say(c, "No code block selected or visible."); return 0; }
        const tiku_doc_block_t *blk = &c->doc->block[block];
        int last = blk->run0 + blk->nrun - 1;
        a = r->starts[blk->run0]; b = r->starts[last] + c->doc->run[last].len;
    }
    if (a == b) { tiku_reader_say(c, "Select text to copy, or use Copy code."); return 0; }
    int ok = tiku_clipboard_import(r->text + a, (size_t)(b - a));
    tiku_reader_say(c, ok ? "Copied to clipboard." : "Selection is too large for the clipboard; nothing was replaced.");
    return ok;
}
