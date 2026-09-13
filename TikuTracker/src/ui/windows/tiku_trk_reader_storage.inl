/* Desktop policy owns allocations; the shared document engine stays bounded. */

/** @brief Release a desktop page and its optional caller-owned tables. */
static void reader_doc_free(tiku_doc_t *doc)
{
    if (doc) free(doc->storage);
    free(doc);
}

/** @brief Parse with document-sized tables, growing only caps actually filled. */
static tiku_doc_t *reader_document(const char *name, const char *source)
{
    size_t n = strlen(source);
    tiku_doc_limits_t l = {
        (int)(n * 2 + 4096), (int)(n / 64 + 256),
        (int)(n / 32 + 1024), (int)(n / 32 + 1024),
        (int)(n / 16 + 2048), (int)(n / 128 + 256),
        (int)(n / 256 + 64), (int)(n / 1024 + 32)
    };
    tiku_doc_t *doc = calloc(1, sizeof *doc);
    if (!doc) return NULL;
    tiku_doc_init(doc);
    /* Small documents continue to use the embedded-size inline buffers. */
    int small = n < TIKU_DOC_TEXT_MAX / 2;
    for (int pass = 0; pass < 16; pass++) {
        if (!small) {
            size_t size = tiku_doc_storage_size(&l);
            if (!size || size > TIKU_TRK_READER_MEMORY_MAX) break;
            void *memory = malloc(size);
            if (!memory) break;
            free(doc->storage);
            if (!tiku_doc_init_storage(doc, &l, memory, size)) {
                free(memory); doc->storage = NULL; break;
            }
        }
        if (!reader_parse(doc, name, source)) break;
        (void)tiku_doc_layout(doc, 256);
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
    reader_doc_free(doc);
    return NULL;
}

/** @brief Read complete regular files; refuse oversize/change rather than cut. */
static char *reader_source(const char *path, const char **why)
{
    struct stat st;
    *why = "Could not read the document.";
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd); return NULL;
    }
    if ((uint64_t)st.st_size > TIKU_TRK_READER_FILE_MAX) {
        *why = "File exceeds the 8 MiB desktop safety limit; nothing was replaced.";
        close(fd); return NULL;
    }
    size_t cap = (size_t)st.st_size;
    char *source = malloc(cap + 1);
    FILE *f = fdopen(fd, "rb");
    if (!f || !source) {
        if (f) fclose(f); else close(fd);
        free(source); return NULL;
    }
    size_t n = fread(source, 1, cap, f);
    int extra = fgetc(f), bad = ferror(f);
    fclose(f);
    if (bad || extra != EOF || memchr(source, 0, n)) {
        *why = "Document changed during reading, or contains binary data; nothing was replaced.";
        free(source); return NULL;
    }
    source[n] = 0;
    return source;
}
