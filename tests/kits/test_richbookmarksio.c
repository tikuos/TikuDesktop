/*
 * TikuDesktop Document Kit tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_richbookmarksio.c - checked bookmark persistence and schema boundaries.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio.h"
#include "tiku_richbookmark.h"
#include "tiku_richhtml.h"
#include "tiku_richmarkup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Decode an independently assembled required-bookmarks extension. */
static void
extension(void)
{
    tiku_richdoc_t *doc, *loaded = NULL;
    tiku_richsnapshot_t *snapshot;
    unsigned char *xml;
    char *fixture, *body, *required;
    size_t length;
    const char marks[] = "<bookmarks><bookmark name=\"Start\" paragraph=\"0\" "
        "byte=\"0\" after=\"0\"/></bookmarks>";

    assert(tiku_richio_read_text("abcd", 4, NULL, &doc) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    fixture = malloc(length + sizeof marks + 32);
    assert(fixture);
    required = strstr((char *)xml, "paragraphs styles lists links");
    body = strstr((char *)xml, "</tiku-richdoc>");
    assert(required && body);
    snprintf(fixture, length + sizeof marks + 32, "%.*sbookmarks %.*s%s%s",
        (int)(required - (char *)xml), xml, (int)(body - required), required,
        marks, body);
    assert(tiku_richio_read_xml(fixture, strlen(fixture), NULL, &loaded) ==
           TIKU_RICHIO_OK);
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_snapshot(loaded, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_bookmarks(snapshot) == 1);
    tiku_richdoc_release(loaded);
    free(fixture); free(xml);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Build paragraph and table-cell destinations with Unicode names. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    unsigned notices;
    const char *html = "<p><a href='#Finish'>Go to finish</a></p>"
        "<table><tr><td>Résumé</td><td>Other cell</td></tr></table><p>Finish</p>";

    assert(tiku_richmarkup_read_html(html, strlen(html), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 1, &p);
    assert(tiku_richdoc_bookmark_set(doc, 0, "Résumé & 1", (tiku_rich_pos_t){
        tiku_richsnapshot_body(snapshot), p.id, 3, TIKU_RICH_AFTER}) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 3, &p);
    assert(tiku_richdoc_bookmark_set(doc, 0, "Finish", (tiku_rich_pos_t){
        tiku_richsnapshot_body(snapshot), p.id, 0, TIKU_RICH_BEFORE}) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    return doc;
}

/** @brief Assert names, cell placement, affinity and clean loaded state. */
static void
check(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t position;

    assert(!tiku_richdoc_dirty(doc));
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_bookmarks(snapshot) == 2);
    assert(tiku_richsnapshot_resolve_link(snapshot, "#R%C3%A9sum%C3%A9%20%26%201", &position) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 1, &p);
    assert(position.paragraph == p.id && position.byte == 3 && position.affinity == TIKU_RICH_AFTER);
    assert(tiku_richsnapshot_resolve_link(snapshot, "#Finish", &position) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 3, &p);
    assert(position.paragraph == p.id && !position.byte && position.affinity == TIKU_RICH_BEFORE);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Replace a single XML fragment and require an empty failure output. */
static void
reject(const char *xml, const char *find, const char *replacement)
{
    tiku_richdoc_t *loaded = (void *)1;
    const char *at = strstr(xml, find);
    char *changed = malloc(strlen(xml) + strlen(replacement) + 1);

    assert(at && changed);
    sprintf(changed, "%.*s%s%s", (int)(at - xml), xml, replacement, at + strlen(find));
    assert(tiku_richio_read_xml(changed, strlen(changed), NULL, &loaded) != TIKU_RICHIO_OK);
    assert(!loaded);
    free(changed);
}

/** @brief Refuse unknown extensions, duplicate names and invalid source points. */
static void
malformed(const char *xml)
{
    reject(xml, " bookmarks", "");
    reject(xml, "<bookmarks>", "<bookmarks other=\"1\">");
    reject(xml, "<bookmarks>", "<bookmarks><unexpected/>");
    reject(xml, "name=\"Finish\"", "name=\"Résumé &amp; 1\"");
    reject(xml, "name=\"Finish\"", "name=\"\"");
    reject(xml, "name=\"Finish\"", "name=\"A&#10;B\"");
    reject(xml, "name=\"Finish\"", "name=\"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890123\"");
    reject(xml, "paragraph=\"3\"", "paragraph=\"4\"");
    reject(xml, "byte=\"3\"", "byte=\"2\"");
    reject(xml, "byte=\"3\"", "byte=\"99999\"");
    reject(xml, "after=\"1\"", "after=\"2\"");
    reject(xml, "after=\"1\"", "");
    reject(xml, "<bookmarks>", "<bookmarks/><bookmarks>");
}

/** @brief Cancellation callback with an exact refusal step. */
static int
cancel(void *context)
{
    size_t *remaining = context;

    return --*remaining == 0;
}

/** @brief Check cancellation at each conversion poll, including bookmarks. */
static void
cancellation(const char *xml, tiku_richsnapshot_t *snapshot)
{
    size_t remaining, nth, length;
    tiku_richio_options_t options = {.cancel = cancel, .ctx = &remaining};
    tiku_richdoc_t *loaded;
    unsigned char *out;
    tiku_richio_status_t status;

    for (nth = 1; nth < 10000; nth++) {
        remaining = nth; loaded = (void *)1;
        status = tiku_richio_read_xml(xml, strlen(xml), &options, &loaded);
        if (status == TIKU_RICHIO_OK) { check(loaded); tiku_richdoc_release(loaded); break; }
        assert(status == TIKU_RICHIO_CANCELLED && !loaded);
    }
    assert(nth < 10000 && nth > 5);
    for (nth = 1; nth < 10000; nth++) {
        remaining = nth; out = (void *)1; length = 123;
        status = tiku_richio_write_xml(snapshot, &options, &out, &length);
        if (status == TIKU_RICHIO_OK) { free(out); break; }
        assert(status == TIKU_RICHIO_CANCELLED && !out && !length);
    }
    assert(nth < 10000 && nth > 5);
}

/** @brief Verify explicit loss notices in formats without named destinations. */
static void
exports(tiku_richsnapshot_t *snapshot)
{
    unsigned char *out;
    size_t length;
    unsigned notices;
    char report[256];

    assert(tiku_richhtml_write(snapshot, NULL, &out, &length, &notices) == TIKU_RICHIO_OK);
    assert(notices & TIKU_RICHHTML_BOOKMARKS); free(out);
    assert(tiku_richmarkup_write_markdown(snapshot, NULL, &out, &length, &notices) == TIKU_RICHIO_OK);
    assert(notices & TIKU_RICHMARKUP_BOOKMARKS); free(out);
    tiku_richmarkup_report(notices, report, sizeof report);
    assert(strstr(report, "Bookmark targets omitted"));
}

/** @brief Round-trip native XML and ZIP, with optional GUI fixture output. */
static void
roundtrip(const char *path)
{
    tiku_richdoc_t *doc = fixture(), *loaded;
    tiku_richsnapshot_t *snapshot;
    unsigned char *xml;
    size_t length;
    char folder[] = "/tmp/tiku-bookmarks-XXXXXX", temporary[128];

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    assert(strstr((char *)xml, " bookmarks"));
    assert(tiku_richio_read_xml(xml, length, NULL, &loaded) == TIKU_RICHIO_OK);
    check(loaded); tiku_richdoc_release(loaded);
    malformed((char *)xml); cancellation((char *)xml, snapshot); exports(snapshot);
    assert(mkdtemp(folder));
    snprintf(temporary, sizeof temporary, "%s/bookmarks.tkrd", folder);
    assert(tiku_richio_save(temporary, snapshot, NULL) == TIKU_RICHIO_OK);
    assert(tiku_richio_load(temporary, NULL, &loaded) == TIKU_RICHIO_OK);
    check(loaded); tiku_richdoc_release(loaded);
    assert(!unlink(temporary) && !rmdir(folder));
    if (path) assert(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    free(xml); tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
}

/** @brief Exercise native bookmark input and output. */
int
main(int argc, char **argv)
{
    extension();
    roundtrip(argc > 1 ? argv[1] : NULL);
    puts("Bookmarks: native XML/ZIP, table/Unicode points, 13 malformed inputs, cancellation and export notices PASS");
    return 0;
}
