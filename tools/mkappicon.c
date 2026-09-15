/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mkappicon.c - the mark, at the sizes a Mac asks for.
 *
 * The mark is kept as geometry rather than as a picture, which is the
 * whole reason this can exist: an icon wants the same drawing at ten
 * sizes from 16 pixels to 1024, and a picture scaled to those would be
 * soft at every one of them.  Drawn at each size, each is sharp, and
 * the mark's own rule -- both cubes when there is room, the near one
 * alone when there is not, filled when there is barely any -- decides
 * what a small icon shows without anybody drawing a second version.
 *
 * Writes an .iconset directory; iconutil(1) turns it into an .icns.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_gfx.h"
#include "tiku_logo.h"

/** @brief One file in an iconset: a size, and whether it is the @2x. */
struct want {
    int  px;                    /* what is actually drawn        */
    int  named;                 /* the size the NAME claims      */
    int  twice;                 /* a @2x of that name            */
};

int
main(int argc, char **argv)
{
    /* The ten Apple looks for.  A @2x is the same pixels as the plain
     * file of twice the name, and both must be there: the name is what
     * the system indexes and the pixels are what it shows. */
    static const struct want WANT[] = {
        {   16,   16, 0 }, {   32,   16, 1 },
        {   32,   32, 0 }, {   64,   32, 1 },
        {  128,  128, 0 }, {  256,  128, 1 },
        {  256,  256, 0 }, {  512,  256, 1 },
        {  512,  512, 0 }, { 1024,  512, 1 }
    };
    const char *out;
    size_t i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <dir.iconset>\n", argv[0]);
        return 2;
    }
    out = argv[1];
    for (i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
        tiku_surface_t *s;
        tiku_rect_t r;
        char path[512];

        s = tiku_surface_new(WANT[i].px, WANT[i].px, TIKU_RGB(255, 255, 255));
        if (s == NULL) {
            fprintf(stderr, "%s: no room for %d px\n", argv[0], WANT[i].px);
            return 1;
        }
        r.x = 0;
        r.y = 0;
        r.w = WANT[i].px;
        r.h = WANT[i].px;
        /* With its ground: this is the thing on the box, not a mark
         * sitting on a surface that already has a colour. */
        tiku_logo_paint(s, r, TIKU_LOGO_GROUND);
        snprintf(path, sizeof path, "%s/icon_%dx%d%s.png", out,
                 WANT[i].named, WANT[i].named,
                 WANT[i].twice ? "@2x" : "");
        if (tiku_surface_png(s, path) != 0) {
            fprintf(stderr, "%s: cannot write %s\n", argv[0], path);
            tiku_surface_free(s);
            return 1;
        }
        tiku_surface_free(s);
    }
    printf("the mark at %zu sizes in %s\n", sizeof WANT / sizeof WANT[0], out);
    return 0;
}
