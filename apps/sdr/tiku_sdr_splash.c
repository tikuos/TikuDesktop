/* tiku_sdr_splash.c -- TikuSDR's splash and About box, two cards painted
 * by the interface kit's splash painter (tiku_splash.h).  The window opens
 * when the splash closes.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_sdr.h"
#include "tiku_splash.h"

/* The splash's status lines, each shown for a third of its time. */
static const char *const stage[] = {
    "waking the receiver…", "reading the kits…", "opening the analyser…"
};

/** @brief Paint the card onto @p s: with a @p status, TikuSDR's name, the
 *         status line and the license notice under them; with none, the
 *         About box's name, credit lines and version. */
static void card(tiku_surface_t *s, const char *status)
{
    static const char *const lines[] = {
        "by Ambuj Varshney",
        "the spectra are the board's own",
        "drawn with the six kits"
    };

    if (status) {
        tiku_splash_paint(s, "TikuSDR", NULL, 0, NULL, status, NULL);
        tiku_splash_license(s);
        return;
    }
    tiku_splash_paint(s, "TikuSDR", lines,
                      (int)(sizeof lines / sizeof lines[0]), "TikuOS 0.06",
                      NULL, NULL);
}

/** @brief Open a card in a window of its own titled @p title, placed as an
 *         announcement (TIKU_APP_PLACE_ANNOUNCE).
 *         @return the window, or 0 with @p *s NULL. */
static uint32_t card_open(sdr_t *st, tiku_surface_t **s, const char *title,
                          const char *status)
{
    const tiku_app_services_t *sv = st->services;
    uint32_t id;

    *s = tiku_surface_new(SDR_SPLASH_W, SDR_SPLASH_H, TIKU_C_PANEL);
    if (!*s) return 0;
    id = sv->open(sv->ctx, title, SDR_SPLASH_W, SDR_SPLASH_H);
    if (!id) {
        tiku_surface_free(*s);
        *s = NULL;
        return 0;
    }
    card(*s, status);
    (void)sv->frame(sv->ctx, id, (*s)->px, SDR_SPLASH_W, SDR_SPLASH_H);
    if (sv->place) (void)sv->place(sv->ctx, id, TIKU_APP_PLACE_ANNOUNCE);
    return id;
}

int sdr_splash_open(sdr_t *st)
{
    st->splash_id = card_open(st, &st->splash, "TikuSDR", stage[0]);
    st->splash_stage = 0;
    st->splash_from = 0;
    return st->splash_id ? 0 : -1;
}

void sdr_splash_tick(sdr_t *st, int64_t now)
{
    int at;

    if (!st->splash) return;
    if (!st->splash_from) st->splash_from = now;
    if (now - st->splash_from > SDR_SPLASH_US) {
        sdr_splash_drop(st);
        return;
    }
    at = (int)((now - st->splash_from) / (SDR_SPLASH_US / 3 + 1));
    if (at > 2) at = 2;
    if (at == st->splash_stage) return;
    st->splash_stage = at;
    card(st->splash, stage[at]);
    (void)st->services->frame(st->services->ctx, st->splash_id,
                              st->splash->px, SDR_SPLASH_W, SDR_SPLASH_H);
}

void sdr_splash_drop(sdr_t *st)
{
    if (!st->splash) return;
    st->services->close(st->services->ctx, st->splash_id);
    tiku_surface_free(st->splash);
    st->splash = NULL;
    /* The splash closes before the window opens, so the desktop places the
     * window as it would with no splash on screen. */
    if (!st->id) (void)sdr_window_open(st);
}

void sdr_about_open(sdr_t *st)
{
    const tiku_app_services_t *sv = st->services;

    if (st->about) {
        if (sv->raise_window) (void)sv->raise_window(sv->ctx, st->about_id);
        return;
    }
    st->about_id = card_open(st, &st->about, "About TikuSDR", NULL);
}

void sdr_about_drop(sdr_t *st)
{
    if (!st->about) return;
    st->services->close(st->services->ctx, st->about_id);
    tiku_surface_free(st->about);
    st->about = NULL;
    st->about_id = 0;
}
