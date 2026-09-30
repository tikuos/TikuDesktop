/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xml.h - XML read into a tree.
 *
 * Office documents are XML in a ZIP.  This reads one part into elements
 * and text, with every namespace a reader here knows given a fixed
 * prefix -- "w:p" whatever the file called WordprocessingML -- so readers
 * match names, not URIs.  No DTD is read and no entity expanded but the
 * five and character references: a file cannot reach outside itself or
 * multiply itself.  Nesting is bounded, so readers may recurse.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_XML_H_
#define TIKU_XML_H_

#include <stddef.h>

/** @brief How deep elements may nest; a deeper file is refused. */
#define TIKU_XML_DEPTH 256

typedef struct tiku_xml tiku_xml_t;
typedef struct tiku_xml_node tiku_xml_node_t;

/** @brief An element, or a run of text between tags.  Read-only. */
struct tiku_xml_node {
    const char      *name;          /* "w:p"; NULL for text              */
    const char      *text;          /* text's characters, UTF-8; ""      */
    const char     **attr;          /* name, value, name, value ...      */
    int              nattr;
    tiku_xml_node_t *parent, *child, *next;
};

/**
 * @brief Read @p len bytes of XML (UTF-8, or UTF-16 with its mark).
 * @return the tree, or NULL with a sentence in @p why (which may be
 *         NULL) saying what was wrong and on which line.
 */
tiku_xml_t *tiku_xml_parse(const void *bytes, size_t len, char *why,
                           size_t max);

/** @brief Let a tree go; its nodes and strings go with it. */
void tiku_xml_free(tiku_xml_t *doc);

/** @brief The document's element. */
tiku_xml_node_t *tiku_xml_root(const tiku_xml_t *doc);

/** @brief Whether @p n is an element called @p name. */
int tiku_xml_is(const tiku_xml_node_t *n, const char *name);

/** @brief @p n's attribute @p name ("w:val", or "val" for one with no
 *         namespace), or NULL. */
const char *tiku_xml_attr(const tiku_xml_node_t *n, const char *name);

/** @brief @p n's first child element called @p name (NULL: any). */
tiku_xml_node_t *tiku_xml_child(const tiku_xml_node_t *n, const char *name);

/** @brief The next element after @p n among its siblings called @p name
 *         (NULL: any). */
tiku_xml_node_t *tiku_xml_next(const tiku_xml_node_t *n, const char *name);

/** @brief The element @p path leads to from @p n: child names joined by
 *         '/', as "w:body/w:sectPr/w:pgSz".  NULL when one is missing. */
tiku_xml_node_t *tiku_xml_path(const tiku_xml_node_t *n, const char *path);

/**
 * @brief The text of @p n and everything under it, joined, into @p out
 *        (@p max bytes with its end, cut short on a character's edge).
 * @return the whole length, as snprintf gives it.
 */
size_t tiku_xml_text(const tiku_xml_node_t *n, char *out, size_t max);

#endif /* TIKU_XML_H_ */
