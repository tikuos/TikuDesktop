/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_odt_obj.c - objects embedded in OpenDocument text: a
 * draw:frame's draw:object, a folder of the package with a document of
 * its own.  A chart's is drawn from the data it keeps, where the frame
 * is laid out; anything else is shown by the picture of it kept beside
 * (the frame's draw:image), as the reader shows any picture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"
#include "tiku_office_odt.h"

#include <stdio.h>
#include <string.h>

/** @brief Object folder @p dir's part @p name ("content.xml") read.
 *  @return it, to free, or NULL. */
static tiku_xml_t *
object_part(of_pkg_t *pkg, const char *dir, const char *name)
{
    char part[512];

    snprintf(part, sizeof part, "%s/%s", dir, name);
    return of_pkg_xml(pkg, part);
}

/** @brief A chart object drawn in @p box (x y w h): of_doc_t's art_draw
 *         for OpenDocument text. */
static int
object_draw(of_doc_t *d, of_faces_t *fs, const of_art_t *a,
            const float *box, tiku_pdf_writer_t *w)
{
    tiku_xml_t *doc = object_part(d->pkg, a->part, "content.xml");
    tiku_xml_t *st = object_part(d->pkg, a->part, "styles.xml");
    dml_mat_t m = {1, 0, 0, 1, box[0], box[1]};
    dml_ctx_t c;
    int drawn;

    memset(&c, 0, sizeof c);
    c.pkg = d->pkg;
    c.part = a->part;
    c.w = w;
    c.fs = fs;
    c.d = d;
    c.text_pt = 10;
    dml_map_read(&c, NULL);
    drawn = ch_draw_odf(&c, tiku_xml_root(doc), tiku_xml_root(st), &m,
                        box[2], box[3]);
    dml_done(&c);
    tiku_xml_free(doc);
    tiku_xml_free(st);
    return drawn < 0 ? -1 : 0;
}

of_art_t *
od_object_art(od_t *x, const tiku_xml_node_t *obj)
{
    const char *href = tiku_xml_attr(obj, "xlink:href");
    char dir[256];
    tiku_xml_t *doc;
    of_art_t *a;
    int chart, n;

    while (href != NULL && href[0] == '.' && href[1] == '/') {
        href += 2;
    }
    if (href == NULL || strstr(href, "://") != NULL) {
        return NULL;
    }
    n = snprintf(dir, sizeof dir, "%s", href);
    while (n > 0 && dir[n - 1] == '/') {   /* "Object 1/": the folder */
        dir[--n] = '\0';
    }
    if (n <= 0 || (size_t)n >= sizeof dir ||
        (doc = object_part(x->pkg, dir, "content.xml")) == NULL) {
        return NULL;
    }
    chart = tiku_xml_path(tiku_xml_root(doc), "office:body/office:chart")
            != NULL;
    tiku_xml_free(doc);
    if (!chart || (a = of_alloc(&x->d->arena, sizeof *a)) == NULL) {
        return NULL;
    }
    a->part = of_strdup(&x->d->arena, dir);
    x->d->art_draw = object_draw;
    return a->part != NULL ? a : NULL;
}
