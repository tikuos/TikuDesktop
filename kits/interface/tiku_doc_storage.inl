/* Caller-owned storage keeps the document parser allocation-free. */

/** @brief Count aligned bytes with bounded signed indices and checked products. */
static size_t doc_storage_part(int count, size_t size)
{
    if (count < 1 || count > 32 * 1024 * 1024 ||
        (size_t)count > (SIZE_MAX - 7) / size) return 0;
    return ((size_t)count * size + 7) & ~(size_t)7;
}

/** @brief Reject invalid capacities before a caller allocates anything. */
size_t tiku_doc_storage_size(const tiku_doc_limits_t *l)
{
    if (!l || l->blocks < 2) return 0;
    const int count[] = {l->text, l->blocks, l->runs, l->lines,
        l->pieces, l->links, l->anchors, l->images};
    const size_t size[] = {1, sizeof(tiku_doc_block_t), sizeof(tiku_doc_run_t),
        sizeof(tiku_doc_line_t), sizeof(tiku_doc_piece_t), sizeof(tiku_doc_link_t),
        sizeof(tiku_doc_anchor_t), sizeof(tiku_doc_image_t)};
    size_t total = 0;
    for (int i = 0; i < 8; i++) {
        size_t part = doc_storage_part(count[i], size[i]);
        if (!part || part > SIZE_MAX - total) return 0;
        total += part;
    }
    return total;
}

/** @brief Bind aligned subarrays; ownership stays with the caller. */
int tiku_doc_init_storage(tiku_doc_t *d, const tiku_doc_limits_t *limits,
                         void *storage, size_t bytes)
{
    size_t needed = tiku_doc_storage_size(limits);
    if (!d || !needed || !storage || ((uintptr_t)storage & 7u) || bytes < needed)
        return 0;
    tiku_doc_limits_t l = *limits;
    tiku_doc_init(d);
    memset(storage, 0, needed);
    unsigned char *p = storage;
    d->storage = storage; d->storage_bytes = bytes; d->limits = l;
    d->text = (void *)p; p += doc_storage_part(l.text, 1);
    d->block = (void *)p; p += doc_storage_part(l.blocks, sizeof *d->block);
    d->run = (void *)p; p += doc_storage_part(l.runs, sizeof *d->run);
    d->line = (void *)p; p += doc_storage_part(l.lines, sizeof *d->line);
    d->piece = (void *)p; p += doc_storage_part(l.pieces, sizeof *d->piece);
    d->link = (void *)p; p += doc_storage_part(l.links, sizeof *d->link);
    d->anchor = (void *)p; p += doc_storage_part(l.anchors, sizeof *d->anchor);
    d->image = (void *)p;
    return 1;
}
