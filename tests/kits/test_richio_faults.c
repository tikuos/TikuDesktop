/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richio_faults.c - allocation and short-write rollback for native files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_RICHIO_ALLOC_IMPLEMENTATION
#include "../fixtures/richio_alloc.h"
#include "tiku_richio.h"
#include "tiku_richclip.h"
#include "tiku_richhtml.h"
#include "tiku_richmarkup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richio faults:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

static size_t calls, fail_at, live, writes, short_at, injected;
static size_t heap_bytes, heap_peak;
/** @brief Aligned allocation prefix recording the requested payload. */
typedef union { max_align_t align; size_t bytes; } heap_header_t;

/** @brief Allocate counted payload and record simultaneous live-byte peaks. */
static void *
raw_allocate(size_t bytes)
{
    heap_header_t *header;

    if (bytes > SIZE_MAX - sizeof *header) return NULL;
    header = malloc(sizeof *header + bytes);
    if (!header) return NULL;
    header->bytes = bytes;
    live++;
    heap_bytes += bytes;
    if (heap_bytes > heap_peak) heap_peak = heap_bytes;
    return header + 1;
}

/** @brief Fail a selected allocation attempt. */
static int
fail(void)
{
    if (++calls != fail_at) return 0;
    injected++;
    return 1;
}

void *
richio_test_malloc(size_t n)
{
    if (fail()) return NULL;
    return raw_allocate(n);
}

void *
richio_test_calloc(size_t n, size_t size)
{
    void *p;

    if (fail()) return NULL;
    if (size && n > SIZE_MAX / size) return NULL;
    p = raw_allocate(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *
richio_test_realloc(void *old, size_t n)
{
    void *p;

    CHECK(n);
    if (fail()) return NULL;
    p = raw_allocate(n);
    if (p && old) {
        size_t bytes = ((heap_header_t *)old)[-1].bytes;

        memcpy(p, old, bytes < n ? bytes : n);
        richio_test_free(old);
    }
    return p;
}

char *
richio_test_strdup(const char *s)
{
    char *p = richio_test_malloc(strlen(s) + 1);

    if (p) memcpy(p, s, strlen(s) + 1);
    return p;
}

void
richio_test_free(void *p)
{
    if (p) {
        heap_header_t *header = (heap_header_t *)p - 1;

        CHECK(live && heap_bytes >= header->bytes);
        live--; heap_bytes -= header->bytes;
        free(header);
    }
}

size_t
richio_test_fwrite(const void *p, size_t size, size_t n, FILE *out)
{
    if (++writes == short_at) {
        CHECK(n);
        injected++;
        return fwrite(p, size, n - 1, out);
    }
    return fwrite(p, size, n, out);
}

/** @brief Import styled text through the allocator-instrumented model. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_rich_style_source_t style = {.name = "Title",
        .chars = {.present = TIKU_RICH_SIZE, .size = 24}};
    tiku_rich_paragraph_source_t p = {.text = "sample<&%", .length = 9,
        .style = 1, .list = {TIKU_RICH_LIST_DECIMAL, 0, 4}};
    tiku_rich_bookmark_source_t mark = {.name = "Saved point", .byte = 4, .after = 1};
    tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = &p, .paragraph_count = 1, .bookmarks = &mark, .bookmark_count = 1};
    tiku_richdoc_t *doc;
    static const uint32_t pixel = 0x8000aaff;
    const tiku_rich_image_t image = {1, 1, &pixel, 24, 12, "Picture & alt"};
    tiku_rich_run_source_t runs[] = {
        {.length = 9}, {.length = 3, .image = &image},
        {.length = 3, .image = &image}};

    p.text = "sample<&%\xef\xbf\xbc\xef\xbf\xbc";
    p.length = 15;
    p.runs = runs;
    p.run_count = 3;
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    {
        tiku_richsnapshot_t *snapshot;
        tiku_rich_paragraph_t first;
        tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
            .table = {2, 2, 0, 4, 1, 0xff000000, {2, 1}, 1}};
        tiku_rich_transaction_t tx = {0, &edit, 1};

        CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        CHECK(tiku_richsnapshot_paragraph(snapshot, 0, &first) == TIKU_RICH_OK);
        edit.paragraph = first.id;
        tiku_richsnapshot_release(snapshot);
        CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
        CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        {
            tiku_rich_block_t cell;

            CHECK(tiku_richsnapshot_block(snapshot, 2, &cell) == TIKU_RICH_OK);
            edit = (tiku_rich_edit_t){.kind = TIKU_RICH_CELL_SET,
                .object = cell.id, .background = 0x8044aaff};
        }
        tiku_richsnapshot_release(snapshot);
        CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
        tiku_richdoc_clear_history(doc);
    }
    return doc;
}

/** @brief Fail each allocation used for encoding and decoding a document. */
static void
memory_faults(tiku_richsnapshot_t *snapshot)
{
    unsigned char *xml, *other;
    size_t length, n, base, total, i;
    tiku_richdoc_t *doc;

    base = live;
    calls = 0;
    CHECK(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
          TIKU_RICHIO_OK);
    CHECK(strstr((char *)xml, "image-references"));
    total = calls;
    richio_test_free(xml);
    CHECK(live == base);
    for (i = 1; i <= total; i++) {
        calls = 0; fail_at = i;
        CHECK(tiku_richio_write_xml(snapshot, NULL, &other, &n) ==
              TIKU_RICHIO_NO_MEMORY);
        CHECK(!other && !n && live == base);
    }
    fail_at = 0;
    CHECK(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
          TIKU_RICHIO_OK);
    base = live;
    calls = 0;
    CHECK(tiku_richio_read_xml(xml, length, NULL, &doc) == TIKU_RICHIO_OK);
    total = calls;
    tiku_richdoc_release(doc);
    CHECK(live == base);
    for (i = 1; i <= total; i++) {
        calls = 0; fail_at = i;
        CHECK(tiku_richio_read_xml(xml, length, NULL, &doc) ==
              TIKU_RICHIO_NO_MEMORY);
        CHECK(!doc && live == base);
    }
    fail_at = 0;
    richio_test_free(xml);
}

/** @brief Verify the test-owned destination still contains its old bytes. */
static void
unchanged(const char *path)
{
    char bytes[16] = {0};
    FILE *f = fopen(path, "rb");

    CHECK(f && fread(bytes, 1, sizeof bytes, f) == 8 && !fclose(f));
    CHECK(!memcmp(bytes, "original", 8));
}

/** @brief A partial ZIP must not replace a user's existing file. */
static void
write_faults(tiku_richsnapshot_t *snapshot)
{
    char dir[] = "/tmp/tiku-richio-faults-XXXXXX", path[256];
    FILE *f;
    size_t i, total, base = live;

    CHECK(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/file.tkrd", dir);
    writes = 0;
    CHECK(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    total = writes;
    CHECK(total > 4 && live == base);
    for (i = 1; i <= total; i++) {
        f = fopen(path, "wb");
        CHECK(f && fwrite("original", 1, 8, f) == 8 && !fclose(f));
        writes = 0; short_at = i;
        CHECK(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_IO);
        CHECK(live == base);
        unchanged(path);
    }
    short_at = 0;
    CHECK(!unlink(path) && !rmdir(dir));
}

/** @brief Fail allocations across ZIP listing, CRC input and atomic saves. */
static void
package_faults(tiku_richsnapshot_t *snapshot)
{
    char dir[] = "/tmp/tiku-richio-package-XXXXXX", path[256];
    tiku_richdoc_t *doc;
    size_t i, total, base = live;
    tiku_richio_status_t status;

    CHECK(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/file.tkrd", dir);
    CHECK(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    calls = 0;
    CHECK(tiku_richio_load(path, NULL, &doc) == TIKU_RICHIO_OK);
    total = calls;
    tiku_richdoc_release(doc);
    CHECK(live == base);
    for (i = 1; i <= total; i++) {
        calls = 0; fail_at = i;
        status = tiku_richio_load(path, NULL, &doc);
        CHECK(status != TIKU_RICHIO_OK && !doc && live == base);
    }
    fail_at = 0;
    calls = 0;
    CHECK(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    total = calls;
    for (i = 1; i <= total; i++) {
        calls = 0; fail_at = i;
        status = tiku_richio_save(path, snapshot, NULL);
        CHECK(status != TIKU_RICHIO_OK && live == base);
        fail_at = 0;
        CHECK(tiku_richio_load(path, NULL, &doc) == TIKU_RICHIO_OK);
        tiku_richdoc_release(doc);
        CHECK(live == base);
    }
    CHECK(!unlink(path) && !rmdir(dir));
}

/** @brief Mutate valid XML reproducibly and require leak-free completion. */
static void
mutations(tiku_richsnapshot_t *snapshot)
{
    unsigned char *xml, *copy;
    size_t length, i, j, base;
    uint32_t seed = 19427;
    tiku_richdoc_t *doc;

    CHECK(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
          TIKU_RICHIO_OK);
    copy = malloc(length);
    CHECK(copy);
    base = live;
    for (i = 0; i < 4000; i++) {
        memcpy(copy, xml, length);
        for (j = 0; j < 1 + i % 4; j++) {
            seed = seed * 1664525u + 1013904223u;
            copy[seed % length] = (unsigned char)(seed >> 24);
        }
        (void)tiku_richio_read_xml(copy, length, NULL, &doc);
        tiku_richdoc_release(doc);
        CHECK(live == base);
    }
    free(copy);
    richio_test_free(xml);
}

/** @brief Refuse each text-import allocation without publishing a document. */
static void
text_faults(void)
{
    const char text[] = "UTF-8 café\r\nSecond paragraph\n";
    tiku_richdoc_t *doc;
    size_t base = live, total;

    calls = 0;
    CHECK(tiku_richio_read_text(text, sizeof text - 1, NULL, &doc) ==
          TIKU_RICHIO_OK);
    total = calls;
    tiku_richdoc_release(doc);
    CHECK(live == base);
    for (size_t i = 1; i <= total; i++) {
        calls = 0;
        fail_at = i;
        CHECK(tiku_richio_read_text(text, sizeof text - 1, NULL, &doc) ==
              TIKU_RICHIO_NO_MEMORY && !doc);
        CHECK(live == base);
    }
    fail_at = 0;
}

/** @brief Refuse each fragment allocation without leaked or partial output. */
static void
fragment_faults(tiku_richsnapshot_t *snapshot)
{
    tiku_rich_paragraph_t p;
    tiku_richfragment_t *source, *decoded;
    unsigned char *data, *other;
    size_t length, n, total, base;
    tiku_rich_pos_t first = {.body = tiku_richsnapshot_body(snapshot)};
    tiku_rich_pos_t last = first;

    CHECK(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
    first.paragraph = last.paragraph = p.id;
    last.byte = p.length;
    CHECK(tiku_richfragment_create(snapshot, first, last, &source) ==
          TIKU_RICH_OK);
    base = live;
    calls = 0;
    CHECK(tiku_richclip_write(source, NULL, &data, &length) == TIKU_RICHIO_OK);
    total = calls;
    richio_test_free(data);
    CHECK(live == base);
    for (size_t i = 1; i <= total; i++) {
        calls = 0; fail_at = i;
        CHECK(tiku_richclip_write(source, NULL, &other, &n) ==
              TIKU_RICHIO_NO_MEMORY);
        CHECK(!other && !n && live == base);
    }
    fail_at = 0;
    CHECK(tiku_richclip_write(source, NULL, &data, &length) == TIKU_RICHIO_OK);
    base = live;
    calls = 0;
    CHECK(tiku_richclip_read(data, length, NULL, &decoded) == TIKU_RICHIO_OK);
    total += calls;
    n = calls;
    tiku_richfragment_release(decoded);
    CHECK(live == base);
    for (size_t i = 1; i <= n; i++) {
        calls = 0; fail_at = i;
        CHECK(tiku_richclip_read(data, length, NULL, &decoded) ==
              TIKU_RICHIO_NO_MEMORY);
        CHECK(!decoded && live == base);
    }
    fail_at = 0;
    richio_test_free(data);
    tiku_richfragment_release(source);
    printf("Rich fragment faults: %zu allocation refusals PASS\n", total);
}

/** @brief Refuse every cell-envelope allocation without partial output. */
static void
cell_fragment_faults(tiku_richsnapshot_t *snapshot)
{
    tiku_rich_block_t table;
    tiku_rich_cells_t cells, result;
    tiku_richdoc_t *decoded;
    unsigned char *data, *other;
    size_t length, n, total, base;

    CHECK(tiku_richsnapshot_block(snapshot, 1, &table) == TIKU_RICH_OK);
    cells = (tiku_rich_cells_t){table.id, 0, 0, 2, 2};
    base = live;
    calls = 0;
    CHECK(tiku_richcellclip_write(snapshot, &cells, NULL, &data, &length) == TIKU_RICHIO_OK);
    total = calls;
    richio_test_free(data);
    CHECK(live == base);
    for (size_t i = 1; i <= total; i++) {
        calls = 0; fail_at = i;
        CHECK(tiku_richcellclip_write(snapshot, &cells, NULL, &other, &n) == TIKU_RICHIO_NO_MEMORY);
        CHECK(!other && !n && live == base);
    }
    fail_at = 0;
    CHECK(tiku_richcellclip_write(snapshot, &cells, NULL, &data, &length) == TIKU_RICHIO_OK);
    base = live;
    calls = 0;
    CHECK(tiku_richcellclip_read(data, length, NULL, &decoded, &result) == TIKU_RICHIO_OK);
    n = calls;
    total += n;
    tiku_richdoc_release(decoded);
    CHECK(live == base);
    for (size_t i = 1; i <= n; i++) {
        calls = 0; fail_at = i;
        CHECK(tiku_richcellclip_read(data, length, NULL, &decoded, &result) == TIKU_RICHIO_NO_MEMORY);
        CHECK(!decoded && !result.table && live == base);
    }
    fail_at = 0;
    richio_test_free(data);
    printf("Rich cell fragment faults: %zu allocation refusals PASS\n", total);
}

/** @brief Refuse the HTML output allocation without retaining partial data. */
static void
html_faults(const tiku_richsnapshot_t *snapshot)
{
    unsigned char *data;
    size_t length, base = live;
    unsigned notices;

    calls = 0;
    CHECK(tiku_richhtml_write(snapshot, NULL, &data, &length, &notices) ==
          TIKU_RICHIO_OK);
    CHECK(calls == 1 && !(notices & TIKU_RICHHTML_IMAGES));
    richio_test_free(data);
    CHECK(live == base);
    {
        tiku_richio_options_t options = {.xml_bytes = length - 1};

        calls = 0;
        CHECK(tiku_richhtml_write(snapshot, &options, &data, &length,
            &notices) == TIKU_RICHIO_LIMIT);
        CHECK(!calls && !data && !length && !notices && live == base);
    }
    calls = 0; fail_at = 1;
    CHECK(tiku_richhtml_write(snapshot, NULL, &data, &length, &notices) ==
          TIKU_RICHIO_NO_MEMORY);
    CHECK(!data && !length && !notices && live == base);
    fail_at = 0;
    puts("HTML output allocation refusal and ownership PASS");
}

/** @brief Fail each markup staging/model allocation and require exact cleanup. */
static void
markup_faults(void)
{
    const char *inputs[] = {
        "<h1>Title</h1><p>Hello <b>world</b> "
        "<a href='https://example.org'>link</a></p>"
        "<table><tr><td>A</td><td><table><tr><td>B</td></tr></table>"
        "</td></tr></table><pre>code</pre>",
        "# Title\n\nHello **world** [link](https://example.org)\n\n"
        "- one\n- two\n\n| A | B |\n| --- | --- |\n| C | D |\n"
    };
    size_t base = live;

    for (unsigned format = 0; format < 2; format++) {
        tiku_richio_status_t (*read)(const void *, size_t,
            const tiku_richio_options_t *, tiku_richdoc_t **, unsigned *) =
            format ? tiku_richmarkup_read_markdown : tiku_richmarkup_read_html;
        tiku_richdoc_t *doc = NULL;
        unsigned notices;

        calls = 0;
        CHECK(read(inputs[format], strlen(inputs[format]), NULL, &doc, &notices) == TIKU_RICHIO_OK);
        size_t total = calls;

        tiku_richdoc_release(doc);
        CHECK(live == base);
        for (size_t i = 1; i <= total; i++) {
            calls = 0; fail_at = i; doc = (void *)1; notices = 123;
            CHECK(read(inputs[format], strlen(inputs[format]), NULL, &doc, &notices) ==
                  TIKU_RICHIO_NO_MEMORY);
            CHECK(!doc && !notices && live == base);
        }
        fail_at = 0;
        printf("Markup %s: %zu allocation refusals PASS\n", format ? "Markdown" : "HTML", total);
    }
}

/** @brief Refuse every PNG decode/import allocation and retain no partial image. */
static void
markup_image_faults(const tiku_richsnapshot_t *snapshot)
{
    unsigned char *html;
    size_t length, total, baseline;
    unsigned notices;
    tiku_richdoc_t *doc;

    CHECK(tiku_richhtml_write(snapshot, NULL, &html, &length, &notices) == TIKU_RICHIO_OK);
    baseline = live;
    calls = 0;
    CHECK(tiku_richmarkup_read_html(html, length, NULL, &doc, &notices) == TIKU_RICHIO_OK);
    CHECK(!(notices & TIKU_RICHMARKUP_IMAGES));
    total = calls;
    tiku_richdoc_release(doc);
    CHECK(live == baseline);
    for (size_t i = 1; i <= total; i++) {
        calls = 0; fail_at = i; doc = (void *)1; notices = 123;
        CHECK(tiku_richmarkup_read_html(html, length, NULL, &doc, &notices) ==
              TIKU_RICHIO_NO_MEMORY);
        CHECK(!doc && !notices && live == baseline);
    }
    fail_at = 0;
    richio_test_free(html);
    printf("Markup pictures: %zu allocation refusals PASS\n", total);
}

/** @brief Measure transient PNG decoding under independently limited imports. */
static void
markup_image_budget(void)
{
    uint32_t pixels[4096] = {0};
    tiku_rich_image_t image = {64, 64, pixels, 64, 64, "Peak"};
    tiku_rich_run_source_t run = {.length = 3, .image = &image};
    tiku_rich_paragraph_source_t p = {.text = "\xef\xbf\xbc", .length = 3,
        .runs = &run, .run_count = 1};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    unsigned char *html;
    size_t length;
    unsigned notices;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richhtml_write(snapshot, NULL, &html, &length, &notices) == TIKU_RICHIO_OK);
    tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
    for (size_t budget = 1024; budget <= 196608; budget += 1024) {
        tiku_richio_options_t options = {.memory_bytes = budget,
                                         .document.memory_bytes = 1};
        size_t bytes = heap_bytes;

        heap_peak = bytes;
        CHECK(tiku_richmarkup_read_html(html, length, &options, &doc, &notices) ==
              TIKU_RICHIO_LIMIT);
        CHECK(!doc && !notices && heap_bytes == bytes);
        CHECK(heap_peak - bytes <= budget);
    }
    richio_test_free(html);
    puts("Markup pictures: 192 measured scratch-memory ceilings PASS");
}

/** @brief Mutate HTML bytes under limits and require complete refusal cleanup. */
static void
markup_mutations(void)
{
    const char *source = "<h2>T</h2><p style='color:#1234'>a &amp; <b>b</b></p>"
        "<table><tr><td>A</td><td><p>X</p></td></tr></table>"
        "<script>if (a < b) alert(1)</script><ol><li>item</li></ol>";
    char input[512];
    size_t length = strlen(source), base = live;
    uint32_t random = 47;

    for (unsigned i = 0; i < 4000; i++) {
        tiku_richdoc_t *doc = (void *)1;
        unsigned notices = 123;

        memcpy(input, source, length);
        random = random * 1664525u + 1013904223u;
        input[random % length] = (char)(random >> 24);
        if (i % 3 == 0) input[(random >> 8) % length] = '<';
        tiku_richio_status_t status = tiku_richmarkup_read_html(input,
            i % 5 ? length : random % length, NULL, &doc, &notices);

        if (status == TIKU_RICHIO_OK) tiku_richdoc_release(doc);
        else CHECK(!doc && !notices);
        CHECK(live == base);
    }
    puts("HTML: 4000 byte/truncation mutations, exact ownership PASS");
}

/** @brief Run allocation, output and malformed-input ownership checks. */
int
main(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *snapshot;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    memory_faults(snapshot);
    write_faults(snapshot);
    package_faults(snapshot);
    mutations(snapshot);
    text_faults();
    fragment_faults(snapshot);
    cell_fragment_faults(snapshot);
    html_faults(snapshot);
    markup_faults();
    markup_image_faults(snapshot);
    markup_image_budget();
    markup_mutations();
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    CHECK(!live);
    printf("Rich native faults: %zu allocation/write failures, "
           "4000 XML mutations, exact ownership PASS\n", injected);
    return 0;
}
