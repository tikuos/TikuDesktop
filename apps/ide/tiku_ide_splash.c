/* tiku_ide_splash.c -- the splash window, the About box, the person's
 * banner art and the licence notice TikuBasic shows.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/**
 * @brief Load the person's banner: ~/.config/<APP_NAME>/splash.bmp, or
 *        splash.png when the BMP does not load.
 *
 * @return the picture, or NULL when there is none.
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

/** @brief Paint the splash onto @p sf: with a @p status, the name and the
 *         status line, and in TikuBasic the licence notice; with none,
 *         the About box's name, credit lines and version. */
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
        /* The art, title and status line; TikuBasic adds the licence
         * notice under them. */
        tiku_splash_paint(sf, APP_NAME, NULL, 0, NULL, status, art);
#ifndef TIKU_IDE_C
        tiku_splash_license(sf);
#endif
        return;
    }
    tiku_splash_paint(sf, APP_NAME, LINES,
                           (int)(sizeof LINES / sizeof LINES[0]),
                           "TikuOS 0.06", status, art);
}

/** @brief Open the About box, which stays until it is closed, or raise it
 *         when it is open. */
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

/** @brief Close the About box, if it is open. */
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

/** @brief Close the splash and open the Project window, unless that is
 *         already open.  Does nothing when no splash is open. */
void
tiku_ide_splash_drop(ide_t *st)
{
    if (st->splash_surface == NULL) {
        return;
    }
    st->services->close(st->services->ctx, st->splash_id);
    tiku_surface_free(st->splash_surface);
    st->splash_surface = NULL;
    /* The splash's window closes before the Project window opens, so
     * the Project window takes the window slot the splash held. */
    if (st->proj_id == 0u) {
        st->proj_id = st->services->open(st->services->ctx, "Project",
                                         IDE_W, IDE_H);
        tiku_ide_project_sizeable(st);
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);
    }
}
