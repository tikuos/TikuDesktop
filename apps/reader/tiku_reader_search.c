/* tiku_reader_search.c -- search.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static void reader_search_page(tiku_reader_help_window_t *c, const char *name);
static void reader_search_dir(tiku_reader_help_window_t *c, const char *prefix, int depth, int *files);

/** @brief Search one page's decoded words, never HTML markup or filenames alone. */
static void reader_search_page(tiku_reader_help_window_t *c, const char *name)
{
    struct help_reader *r = c->reader;
    if (r->nrows >= HELP_ROWS) { r->limited = 1; return; }
    char path[TIKU_PATH_MAX];
    if (c->document_mode) {
        char candidate[TIKU_PATH_MAX];
        if (snprintf(candidate, sizeof candidate, "%s/%s", c->root, name) >=
            (int)sizeof candidate || !tiku_reader_document_path(candidate, path, sizeof path)) return;
    } else if (!tiku_reader_path(c, name, path, sizeof path)) return;
    struct stat st;
    if (stat(path, &st) || st.st_size < 0) { r->limited = 1; return; }
    if ((uint64_t)st.st_size > 32u * 1024u * 1024u - r->search_bytes) {
        r->limited = 1; return;
    }
    r->search_bytes += (size_t)st.st_size;
    const char *why;
    char *text = tiku_reader_source(path, &why);
    if (!text) { r->limited = 1; return; }
    tiku_doc_t *doc = tiku_reader_document(name, text);
    free(text);
    if (!doc) { r->limited = 1; return; }
    size_t cap = (size_t)doc->ntext + doc->nblock + 1;
    char *plain = malloc(cap);
    if (!plain) { r->limited = 1; tiku_reader_doc_free(doc); return; }
    if (doc->full & (TIKU_DOC_FULL_TEXT | TIKU_DOC_FULL_BLOCKS | TIKU_DOC_FULL_RUNS)) r->limited = 1;
    tiku_reader_text(doc, plain, cap, NULL);
    for (int i = 0; plain[i]; i++) {
        if (!tiku_reader_match(plain + i, r->query.text)) continue;
        help_row_t *row = &r->rows[r->nrows++]; memset(row, 0, sizeof *row);
        snprintf(row->page, sizeof row->page, "%s", c->document_mode ? path : name);
        snprintf(row->title, sizeof row->title, "%.127s", tiku_doc_title(doc) ? tiku_doc_title(doc) : name);
        snprintf(row->snippet, sizeof row->snippet, "%.150s", plain + i);
        for (char *p = row->snippet; *p; p++) if (*p == '\n' || *p == '\t') *p = ' ';
        break;
    }
    free(plain); tiku_reader_doc_free(doc);
}

/** @brief Walk at most 128 regular help files; do not follow directory symlinks. */
static void reader_search_dir(tiku_reader_help_window_t *c, const char *prefix, int depth, int *files)
{
    char path[TIKU_PATH_MAX + 256];
    if (depth > 4 || *files >= HELP_ROWS) { c->reader->limited = 1; return; }
    snprintf(path, sizeof path, "%s/%s", c->root, prefix);
    DIR *dir = opendir(path); if (!dir) return;
    struct dirent *entry;
    while (*files < HELP_ROWS && (entry = readdir(dir))) {
        char name[256]; struct stat st;
        if (entry->d_name[0] == '.') continue;
        int n = snprintf(name, sizeof name, "%s%s%s", prefix, *prefix ? "/" : "", entry->d_name);
        if (n >= (int)sizeof name) { c->reader->limited = 1; continue; }
        snprintf(path, sizeof path, "%s/%s", c->root, name);
        if (lstat(path, &st) || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!c->document_mode && !*prefix && strlen(entry->d_name) == 2) continue; /* locale bundles */
            reader_search_dir(c, name, depth + 1, files);
        } else if (S_ISREG(st.st_mode) && tiku_reader_document_file_supported(name)) {
            (*files)++; reader_search_page(c, name);
        }
    }
    if (*files >= HELP_ROWS) c->reader->limited = 1;
    closedir(dir);
}

/** @brief Result rows preserve the source page and show a short matching context. */
void tiku_reader_search(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader; if (!r || !r->query.text[0]) { tiku_reader_say(c, "Enter words to search for."); return; }
    if (!c->root[0]) { tiku_reader_say(c, "Open a document before searching its folder."); return; }
    r->nrows = r->limited = 0; r->side = 2; r->search_bytes = 0;
    int files = 0; reader_search_dir(c, "", 0, &files);
    tiku_list_set_count(&r->list, r->nrows); r->list.top = 0;
    tiku_list_select_only(&r->list, r->nrows ? 0 : -1);
    snprintf(c->status, sizeof c->status, "%d matching pages%s.", r->nrows, r->limited ? " (search limit reached)" : "");
}

/** @brief Build the outline from headings, not a second hard-coded contents list. */
void tiku_reader_outline(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader; if (!r || !c->doc) return;
    r->nrows = 0; int hidden = 0;
    for (int i = 0; i < c->doc->nblock && r->nrows < HELP_ROWS; i++) {
        const tiku_doc_block_t *b = &c->doc->block[i];
        if (b->kind != TIKU_DOC_BLOCK_H1 && b->kind != TIKU_DOC_BLOCK_H2 && b->kind != TIKU_DOC_BLOCK_H3) continue;
        int level = b->kind == TIKU_DOC_BLOCK_H1 ? 1 : b->kind == TIKU_DOC_BLOCK_H2 ? 2 : 3;
        if (hidden && level > hidden) continue;
        hidden = r->folded[i] ? level : 0;
        help_row_t *row = &r->rows[r->nrows++]; memset(row, 0, sizeof *row); row->block = i;
        row->level = level;
        for (int j = i+1; j < c->doc->nblock; j++) {
            int kind = c->doc->block[j].kind;
            if (kind != TIKU_DOC_BLOCK_H1 && kind != TIKU_DOC_BLOCK_H2 && kind != TIKU_DOC_BLOCK_H3) continue;
            int child_level = kind == TIKU_DOC_BLOCK_H1 ? 1 : kind == TIKU_DOC_BLOCK_H2 ? 2 : 3;
            row->children = child_level > level; break;
        }
        int used = 0;
        for (int k = b->run0; k < b->run0 + b->nrun && used < 126; k++) {
            int n = c->doc->run[k].len; if (n > 126-used) n = 126-used;
            memcpy(row->title+used, c->doc->text+c->doc->run[k].off, n); used += n;
        }
        row->title[used] = 0;
    }
    tiku_list_set_count(&r->list, r->nrows);
}
