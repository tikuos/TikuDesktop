/* tiku_ide_splash.c -- splash.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/**
 * @brief The banner the person chose, when they chose one.
 *
 * The picture is looked for where the desktop already keeps a person's
 * replacements -- ~/.config/<name>/splash.bmp (or .png) -- so choosing
 * a banner is putting a file there, the same gesture that replaces an
 * icon.  A picture that will not load is simply not worn.
 */
tiku_surface_t *
tiku_ide_splash_art_load(void)
{
    static const char *const KIND[] = { "bmp", "png" };
    const char *home = getenv("HOME");
    size_t k;

    for (k = 0; home != NULL && k < sizeof KIND / sizeof KIND[0]; k++) {
        char at[512];
        tiku_image_t img;
        tiku_surface_t *art;
        int i;

        snprintf(at, sizeof at, "%s/.config/%s/splash.%s", home,
                 APP_NAME, KIND[k]);
        if (tiku_image_load(at, &img) != TIKU_IMG_OK) {
            continue;
        }
        art = tiku_surface_new(img.w, img.h, TIKU_C_DOC);
        if (art != NULL) {
            for (i = 0; i < img.w * img.h; i++) {
                art->px[i] = img.px[i] & 0x00ffffffu;
            }
        }
        tiku_image_free(&img);
        return art;
    }
    return NULL;
}

#ifndef TIKU_IDE_C
/** @brief Apache's standard notice in the splash's otherwise empty lower area. */
void
tiku_ide_announce_license(tiku_surface_t *sf)
{
    /* Verbatim boilerplate from the repository LICENSE, without its
     * copyright placeholder; only the line breaks differ. */
    static const char *const notice[] = {
        "Licensed under the Apache License, Version 2.0 (the \"License\");",
        "you may not use this file except in compliance with the License. You may obtain a copy of",
        "the License at http://www.apache.org/licenses/LICENSE-2.0",
        "Unless required by applicable law or agreed to in writing, software distributed under the",
        "License is distributed on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF",
        "ANY KIND, either express or implied. See the License for the specific language governing",
        "permissions and limitations under the License."
    };
    const tiku_font_t *small = tiku_font_at(10);
    /* Same banner/name/status metrics as the shared splash painter, with
     * a four-pixel gap before the fine print. The bottom-right mark stays. */
    int y = sf->h / 2 + 12 + tiku_font_at(26)->height + 6 +
            tiku_font_plain()->height + 4;
    for (size_t i = 0; i < sizeof notice / sizeof *notice; i++) {
        tiku_text(sf, small, 18, y + small->ascent, notice[i],
                  TIKU_RGB(128, 128, 128));
        y += small->height - 1; /* Compact leading keeps the footer clear. */
    }
}
#endif

/** @brief This program's splash screen: its name, and the words that are true. */
void
tiku_ide_announce_paint_art(tiku_surface_t *sf, const char *status,
                   const tiku_surface_t *art)
{
    static const char *const LINES[] = {
        "by Ambuj Varshney",
#ifdef TIKU_IDE_C
        "the compiler is the target's own",
#else
        "the interpreter is the board's own",
#endif
        "drawn with the six kits"
    };

    if (status != NULL) {
        /* Keep the art, title and startup status; TikuBasic uses the
         * lower space for a quiet license notice rather than credits. */
        tiku_splash_paint(sf, APP_NAME, NULL, 0, NULL, status, art);
#ifndef TIKU_IDE_C
        tiku_ide_announce_license(sf);
#endif
        return;
    }
    tiku_splash_paint(sf, APP_NAME, LINES,
                           (int)(sizeof LINES / sizeof LINES[0]),
                           "TikuOS 0.06", status, art);
}

/**
 * @brief The About box: the announcement again, for as long as it is
 *        wanted this time.
 *
 * Asked for while already standing, it comes FORWARD rather than
 * doubling -- the same rule every window of this application keeps.
 */
void
tiku_ide_about_open(ide_t *st)
{
    if (st->about_surface != NULL) {
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->about_id);
        }
        return;
    }
    st->about_surface = tiku_surface_new(SPLASH_W, SPLASH_H,
                                              TIKU_C_PANEL);
    if (st->about_surface == NULL) {
        return;
    }
    st->about_id = st->services->open(st->services->ctx,
                                      APP_ABOUT, SPLASH_W,
                                      SPLASH_H);
    if (st->about_id == 0u) {
        tiku_surface_free(st->about_surface);
        st->about_surface = NULL;
        return;
    }
    tiku_ide_announce_paint_art(st->about_surface, NULL, st->splash_art);
    (void)st->services->frame(st->services->ctx, st->about_id,
                              st->about_surface->px, SPLASH_W,
                              SPLASH_H);
    if (st->services->place != NULL) {
        (void)st->services->place(st->services->ctx, st->about_id,
                                  TIKU_APP_PLACE_ANNOUNCE);
    }
}

void
tiku_ide_about_drop(ide_t *st)
{
    if (st->about_surface == NULL) {
        return;
    }
    st->services->close(st->services->ctx, st->about_id);
    tiku_surface_free(st->about_surface);
    st->about_surface = NULL;
}

/** @brief Let the splash go, whoever asked. */
void
tiku_ide_splash_drop(ide_t *st)
{
    if (st->splash_surface == NULL) {
        return;
    }
    st->services->close(st->services->ctx, st->splash_id);
    tiku_surface_free(st->splash_surface);
    st->splash_surface = NULL;
    /* The splash leaving is the project window arriving: its session
     * row frees first, so the project stands exactly where it would
     * have without the announcement. */
    if (st->proj_id == 0u) {
        st->proj_id = st->services->open(st->services->ctx, "Project",
                                         IDE_W, IDE_H);
        tiku_ide_project_sizeable(st);
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);
    }
}
